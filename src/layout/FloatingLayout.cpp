#include "eternal/layout/FloatingLayout.hpp"

#include "eternal/core/Surface.hpp"

#include <algorithm>
#include <cmath>
#include <climits>
#include <numeric>

namespace eternal {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

FloatingLayout::FloatingLayout() : FloatingLayout(FloatingLayoutConfig{}) {}

FloatingLayout::FloatingLayout(FloatingLayoutConfig config)
    : config_(config) {}

FloatingLayout::~FloatingLayout() = default;

// ---------------------------------------------------------------------------
// ILayout identity
// ---------------------------------------------------------------------------

LayoutType       FloatingLayout::getType() const noexcept { return LayoutType::Floating; }
std::string_view FloatingLayout::getName() const noexcept { return "floating"; }

// ---------------------------------------------------------------------------
// Gaps
// ---------------------------------------------------------------------------

void FloatingLayout::setGaps(GapConfig gaps) {
    gaps_ = gaps;
}

// ---------------------------------------------------------------------------
// Window list
// ---------------------------------------------------------------------------

std::vector<Surface*> FloatingLayout::getWindows() const {
    return window_order_;
}

const FloatingWindowState* FloatingLayout::getWindowState(Surface* surface) const {
    auto it = states_.find(surface);
    return (it != states_.end()) ? &it->second : nullptr;
}

std::vector<Surface*> FloatingLayout::getWindowsByZOrder() const {
    std::vector<std::pair<Surface*, int>> sorted;
    sorted.reserve(window_order_.size());
    for (auto* s : window_order_) {
        auto it = states_.find(s);
        int z = (it != states_.end()) ? it->second.z_order : 0;
        sorted.emplace_back(s, z);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });

    std::vector<Surface*> result;
    result.reserve(sorted.size());
    for (auto& [s, z] : sorted) result.push_back(s);
    return result;
}

// ---------------------------------------------------------------------------
// Placement strategies
// ---------------------------------------------------------------------------

Box FloatingLayout::computePlacement(int width, int height) const {
    switch (config_.placement) {
        case PlacementStrategy::Cascade:       return computeCascadePlacement(width, height);
        case PlacementStrategy::Center:        return computeCenterPlacement(width, height);
        case PlacementStrategy::SmartOverlap:  return computeSmartPlacement(width, height);
        case PlacementStrategy::MousePosition: return computeMousePlacement(width, height);
    }
    return computeSmartPlacement(width, height);
}

Box FloatingLayout::computeCascadePlacement(int width, int height) const {
    int n = static_cast<int>(window_order_.size());
    int offX = config_.cascade_offset_x * (n % 10);
    int offY = config_.cascade_offset_y * (n % 10);
    return {available_area_.x + gaps_.outer + offX,
            available_area_.y + gaps_.outer + offY,
            width, height};
}

Box FloatingLayout::computeCenterPlacement(int width, int height) const {
    int cx = available_area_.x + (available_area_.width  - width)  / 2;
    int cy = available_area_.y + (available_area_.height - height) / 2;
    return {cx, cy, width, height};
}

Box FloatingLayout::computeSmartPlacement(int width, int height) const {
    int left   = available_area_.x + gaps_.outer;
    int top    = available_area_.y + gaps_.outer;
    int right  = available_area_.x + available_area_.width  - gaps_.outer;
    int bottom = available_area_.y + available_area_.height - gaps_.outer;

    if (states_.empty()) {
        int cx = left + (right - left - width) / 2;
        int cy = top  + (bottom - top - height) / 2;
        return {cx, cy, width, height};
    }

    // Grid-based least-overlap search.
    constexpr int kGridSteps = 10;
    int stepX = std::max(1, (right  - left - width)  / kGridSteps);
    int stepY = std::max(1, (bottom - top  - height) / kGridSteps);

    int bestX = left, bestY = top;
    long long bestOverlap = LLONG_MAX;

    for (int gy = 0; gy <= kGridSteps; ++gy) {
        for (int gx = 0; gx <= kGridSteps; ++gx) {
            int cx = left + gx * stepX;
            int cy = top  + gy * stepY;
            cx = std::min(cx, right  - width);
            cy = std::min(cy, bottom - height);

            long long totalOverlap = 0;
            for (auto& [surf, st] : states_) {
                if (st.minimized) continue;
                int ox = std::max(0, std::min(cx + width,  st.geometry.x + st.geometry.width)
                                     - std::max(cx, st.geometry.x));
                int oy = std::max(0, std::min(cy + height, st.geometry.y + st.geometry.height)
                                     - std::max(cy, st.geometry.y));
                totalOverlap += static_cast<long long>(ox) * oy;
            }

            if (totalOverlap < bestOverlap) {
                bestOverlap = totalOverlap;
                bestX = cx;
                bestY = cy;
                if (totalOverlap == 0) goto done;
            }
        }
    }
done:
    return {bestX, bestY, width, height};
}

Box FloatingLayout::computeMousePlacement(int width, int height) const {
    int x = mouse_x_ - width / 2;
    int y = mouse_y_ - height / 2;

    // Clamp to screen edges.
    int left   = available_area_.x + gaps_.outer;
    int top    = available_area_.y + gaps_.outer;
    int right  = available_area_.x + available_area_.width  - gaps_.outer;
    int bottom = available_area_.y + available_area_.height - gaps_.outer;

    x = std::clamp(x, left, std::max(left, right  - width));
    y = std::clamp(y, top,  std::max(top,  bottom - height));

    return {x, y, width, height};
}

// ---------------------------------------------------------------------------
// Size constraint enforcement
// ---------------------------------------------------------------------------

void FloatingLayout::enforceSizeConstraints(FloatingWindowState& state) const {
    int minW = std::max(config_.min_width,  state.min_width);
    int minH = std::max(config_.min_height, state.min_height);
    int maxW = state.max_width  > 0 ? state.max_width  : (config_.max_width  > 0 ? config_.max_width  : INT_MAX);
    int maxH = state.max_height > 0 ? state.max_height : (config_.max_height > 0 ? config_.max_height : INT_MAX);

    state.geometry.width  = std::clamp(state.geometry.width,  minW, maxW);
    state.geometry.height = std::clamp(state.geometry.height, minH, maxH);

    // Enforce aspect ratio if set.
    if (state.aspect_ratio > 0.01f) {
        float current = static_cast<float>(state.geometry.width) /
                        static_cast<float>(std::max(1, state.geometry.height));
        if (std::abs(current - state.aspect_ratio) > 0.01f) {
            state.geometry.height = static_cast<int>(
                static_cast<float>(state.geometry.width) / state.aspect_ratio);
            state.geometry.height = std::clamp(state.geometry.height, minH, maxH);
        }
    }
}

// ---------------------------------------------------------------------------
// Edge snapping
// ---------------------------------------------------------------------------

void FloatingLayout::applyEdgeSnap(Surface* surface, int& x, int& y) const {
    auto it = states_.find(surface);
    if (it == states_.end()) return;

    int w = it->second.geometry.width;
    int h = it->second.geometry.height;
    int t = config_.snap_threshold;

    int left   = available_area_.x + gaps_.outer;
    int top    = available_area_.y + gaps_.outer;
    int right  = available_area_.x + available_area_.width  - gaps_.outer;
    int bottom = available_area_.y + available_area_.height - gaps_.outer;

    // Snap left edge.
    if (std::abs(x - left) < t) x = left;
    // Snap right edge.
    if (std::abs(x + w - right) < t) x = right - w;
    // Snap top edge.
    if (std::abs(y - top) < t) y = top;
    // Snap bottom edge.
    if (std::abs(y + h - bottom) < t) y = bottom - h;
}

void FloatingLayout::applyWindowSnap(Surface* surface, int& x, int& y) const {
    auto it = states_.find(surface);
    if (it == states_.end()) return;

    int w = it->second.geometry.width;
    int h = it->second.geometry.height;
    int t = config_.window_snap_threshold;

    for (auto& [other, st] : states_) {
        if (other == surface || st.minimized) continue;

        int ox = st.geometry.x;
        int oy = st.geometry.y;
        int ow = st.geometry.width;
        int oh = st.geometry.height;

        // Vertical overlap check for horizontal snapping.
        bool vOverlap = (y < oy + oh) && (y + h > oy);
        if (vOverlap) {
            // Snap our left to their right.
            if (std::abs(x - (ox + ow)) < t) x = ox + ow;
            // Snap our right to their left.
            if (std::abs((x + w) - ox) < t) x = ox - w;
            // Snap left-to-left alignment.
            if (std::abs(x - ox) < t) x = ox;
            // Snap right-to-right alignment.
            if (std::abs((x + w) - (ox + ow)) < t) x = ox + ow - w;
        }

        // Horizontal overlap check for vertical snapping.
        bool hOverlap = (x < ox + ow) && (x + w > ox);
        if (hOverlap) {
            // Snap our top to their bottom.
            if (std::abs(y - (oy + oh)) < t) y = oy + oh;
            // Snap our bottom to their top.
            if (std::abs((y + h) - oy) < t) y = oy - h;
            // Snap top-to-top alignment.
            if (std::abs(y - oy) < t) y = oy;
            // Snap bottom-to-bottom alignment.
            if (std::abs((y + h) - (oy + oh)) < t) y = oy + oh - h;
        }
    }
}

// ---------------------------------------------------------------------------
// Snap guides (for visual feedback during drag)
// ---------------------------------------------------------------------------

std::vector<SnapGuide> FloatingLayout::computeSnapGuides(Surface* surface) const {
    std::vector<SnapGuide> guides;
    auto it = states_.find(surface);
    if (it == states_.end()) return guides;

    auto& g = it->second.geometry;
    int t = config_.window_snap_threshold;

    for (auto& [other, st] : states_) {
        if (other == surface || st.minimized) continue;

        int ox = st.geometry.x;
        int oy = st.geometry.y;
        int ow = st.geometry.width;
        int oh = st.geometry.height;

        // Check left-edge alignments.
        if (std::abs(g.x - ox) < t) {
            guides.push_back({SnapGuide::Orientation::Vertical, ox,
                             std::min(g.y, oy), std::max(g.y + g.height, oy + oh)});
        }
        // Check right-edge alignments.
        if (std::abs((g.x + g.width) - (ox + ow)) < t) {
            guides.push_back({SnapGuide::Orientation::Vertical, ox + ow,
                             std::min(g.y, oy), std::max(g.y + g.height, oy + oh)});
        }
        // Check top-edge alignments.
        if (std::abs(g.y - oy) < t) {
            guides.push_back({SnapGuide::Orientation::Horizontal, oy,
                             std::min(g.x, ox), std::max(g.x + g.width, ox + ow)});
        }
        // Check bottom-edge alignments.
        if (std::abs((g.y + g.height) - (oy + oh)) < t) {
            guides.push_back({SnapGuide::Orientation::Horizontal, oy + oh,
                             std::min(g.x, ox), std::max(g.x + g.width, ox + ow)});
        }
    }

    return guides;
}

// ---------------------------------------------------------------------------
// Resize handle hit testing
// ---------------------------------------------------------------------------

ResizeEdge FloatingLayout::hitTestResize(Surface* surface, int px, int py,
                                          int border_width) const
{
    auto it = states_.find(surface);
    if (it == states_.end()) return ResizeEdge::None;

    auto& g = it->second.geometry;

    // Check if the point is outside the window.
    if (px < g.x - border_width || px > g.x + g.width + border_width ||
        py < g.y - border_width || py > g.y + g.height + border_width) {
        return ResizeEdge::None;
    }

    bool onLeft   = (px >= g.x - border_width && px < g.x + border_width);
    bool onRight  = (px > g.x + g.width - border_width && px <= g.x + g.width + border_width);
    bool onTop    = (py >= g.y - border_width && py < g.y + border_width);
    bool onBottom = (py > g.y + g.height - border_width && py <= g.y + g.height + border_width);

    if (onTop && onLeft)     return ResizeEdge::TopLeft;
    if (onTop && onRight)    return ResizeEdge::TopRight;
    if (onBottom && onLeft)  return ResizeEdge::BottomLeft;
    if (onBottom && onRight) return ResizeEdge::BottomRight;
    if (onTop)               return ResizeEdge::Top;
    if (onBottom)            return ResizeEdge::Bottom;
    if (onLeft)              return ResizeEdge::Left;
    if (onRight)             return ResizeEdge::Right;

    return ResizeEdge::None;
}

// ---------------------------------------------------------------------------
// addWindow
// ---------------------------------------------------------------------------

void FloatingLayout::addWindow(Surface* surface) {
    if (!surface) return;

    constexpr int kDefaultW = 640;
    constexpr int kDefaultH = 480;

    Box geom = computePlacement(kDefaultW, kDefaultH);

    FloatingWindowState state;
    state.geometry = geom;
    state.z_order = next_z_++;
    state.min_width  = config_.min_width;
    state.min_height = config_.min_height;

    enforceSizeConstraints(state);
    surface->setGeometry(state.geometry.x, state.geometry.y,
                         state.geometry.width, state.geometry.height);

    window_order_.push_back(surface);
    states_[surface] = state;
    focused_index_ = static_cast<int>(window_order_.size()) - 1;
}

// ---------------------------------------------------------------------------
// removeWindow
// ---------------------------------------------------------------------------

void FloatingLayout::removeWindow(Surface* surface) {
    if (!surface) return;

    auto it = std::find(window_order_.begin(), window_order_.end(), surface);
    if (it == window_order_.end()) return;

    int idx = static_cast<int>(it - window_order_.begin());
    window_order_.erase(it);
    states_.erase(surface);

    if (window_order_.empty()) {
        focused_index_ = 0;
    } else {
        focused_index_ = std::min(idx, static_cast<int>(window_order_.size()) - 1);
    }
}

// ---------------------------------------------------------------------------
// Focus
// ---------------------------------------------------------------------------

void FloatingLayout::focusNext() {
    if (window_order_.empty()) return;
    focused_index_ = (focused_index_ + 1) % static_cast<int>(window_order_.size());
    if (config_.raise_on_focus) {
        raiseWindow(window_order_[focused_index_]);
    }
}

void FloatingLayout::focusPrev() {
    if (window_order_.empty()) return;
    int n = static_cast<int>(window_order_.size());
    focused_index_ = (focused_index_ - 1 + n) % n;
    if (config_.raise_on_focus) {
        raiseWindow(window_order_[focused_index_]);
    }
}

void FloatingLayout::focusDirection(Direction dir) {
    if (window_order_.size() < 2) return;

    Surface* cur = window_order_[focused_index_];
    auto& cg = states_[cur].geometry;
    int cx = cg.x + cg.width / 2;
    int cy = cg.y + cg.height / 2;

    int bestIdx = focused_index_;
    int bestDist = INT_MAX;

    for (int i = 0; i < static_cast<int>(window_order_.size()); ++i) {
        if (i == focused_index_) continue;
        auto sit = states_.find(window_order_[i]);
        if (sit == states_.end() || sit->second.minimized) continue;

        auto& og = sit->second.geometry;
        int nx = og.x + og.width / 2;
        int ny = og.y + og.height / 2;
        int dx = nx - cx;
        int dy = ny - cy;

        bool valid = false;
        switch (dir) {
            case Direction::Left:  valid = (dx < 0); break;
            case Direction::Right: valid = (dx > 0); break;
            case Direction::Up:    valid = (dy < 0); break;
            case Direction::Down:  valid = (dy > 0); break;
        }

        if (valid) {
            int dist = dx * dx + dy * dy;
            if (dist < bestDist) {
                bestDist = dist;
                bestIdx = i;
            }
        }
    }

    focused_index_ = bestIdx;
    if (config_.raise_on_focus) {
        raiseWindow(window_order_[focused_index_]);
    }
}

// ---------------------------------------------------------------------------
// moveWindow -- nudge by a fixed step with snapping
// ---------------------------------------------------------------------------

void FloatingLayout::moveWindow(Direction dir) {
    if (window_order_.empty()) return;

    Surface* cur = window_order_[focused_index_];
    auto it = states_.find(cur);
    if (it == states_.end()) return;

    constexpr int kStep = 20;
    auto& g = it->second.geometry;

    switch (dir) {
        case Direction::Left:  g.x -= kStep; break;
        case Direction::Right: g.x += kStep; break;
        case Direction::Up:    g.y -= kStep; break;
        case Direction::Down:  g.y += kStep; break;
    }

    applyEdgeSnap(cur, g.x, g.y);
    cur->setGeometry(g.x, g.y, g.width, g.height);
}

// ---------------------------------------------------------------------------
// resizeWindow
// ---------------------------------------------------------------------------

void FloatingLayout::resizeWindow(Surface* surface, SizeDelta delta) {
    auto it = states_.find(surface);
    if (it == states_.end()) return;

    auto& state = it->second;
    state.geometry.width  += delta.dx;
    state.geometry.height += delta.dy;
    enforceSizeConstraints(state);
    surface->setGeometry(state.geometry.x, state.geometry.y,
                         state.geometry.width, state.geometry.height);
}

// ---------------------------------------------------------------------------
// Floating-specific methods
// ---------------------------------------------------------------------------

void FloatingLayout::moveWindowTo(Surface* surface, int x, int y) {
    auto it = states_.find(surface);
    if (it == states_.end()) return;

    applyEdgeSnap(surface, x, y);
    applyWindowSnap(surface, x, y);

    it->second.geometry.x = x;
    it->second.geometry.y = y;
    surface->setGeometry(x, y, it->second.geometry.width, it->second.geometry.height);
}

void FloatingLayout::resizeWindowTo(Surface* surface, int width, int height) {
    auto it = states_.find(surface);
    if (it == states_.end()) return;

    auto& state = it->second;
    state.geometry.width  = width;
    state.geometry.height = height;
    enforceSizeConstraints(state);
    surface->setGeometry(state.geometry.x, state.geometry.y,
                         state.geometry.width, state.geometry.height);
}

void FloatingLayout::raiseWindow(Surface* surface) {
    auto it = states_.find(surface);
    if (it != states_.end()) {
        it->second.z_order = next_z_++;
    }
}

void FloatingLayout::lowerWindow(Surface* surface) {
    auto it = states_.find(surface);
    if (it != states_.end()) {
        // Find the lowest current z_order and go below it.
        int lowest = 0;
        for (auto& [s, st] : states_) {
            lowest = std::min(lowest, st.z_order);
        }
        it->second.z_order = lowest - 1;
    }
}

void FloatingLayout::setMinimized(Surface* surface, bool minimized) {
    auto it = states_.find(surface);
    if (it != states_.end()) {
        it->second.minimized = minimized;
    }
}

void FloatingLayout::snapWindow(Surface* surface, Direction dir) {
    auto it = states_.find(surface);
    if (it == states_.end()) return;

    auto& g = it->second.geometry;
    int left   = available_area_.x + gaps_.outer;
    int top    = available_area_.y + gaps_.outer;
    int right  = available_area_.x + available_area_.width  - gaps_.outer;
    int bottom = available_area_.y + available_area_.height - gaps_.outer;

    switch (dir) {
        case Direction::Left:  g.x = left;                break;
        case Direction::Right: g.x = right - g.width;     break;
        case Direction::Up:    g.y = top;                  break;
        case Direction::Down:  g.y = bottom - g.height;    break;
    }
    surface->setGeometry(g.x, g.y, g.width, g.height);
}

void FloatingLayout::centerWindow(Surface* surface) {
    auto it = states_.find(surface);
    if (it == states_.end()) return;

    auto& g = it->second.geometry;
    g.x = available_area_.x + (available_area_.width  - g.width)  / 2;
    g.y = available_area_.y + (available_area_.height - g.height) / 2;
    surface->setGeometry(g.x, g.y, g.width, g.height);
}

void FloatingLayout::tileHalf(Surface* surface, Direction dir) {
    auto it = states_.find(surface);
    if (it == states_.end()) return;

    int outer = gaps_.outer;
    int inner = gaps_.inner;
    auto& g = it->second.geometry;
    int halfW = (available_area_.width - 2 * outer - inner) / 2;
    int fullH = available_area_.height - 2 * outer;

    switch (dir) {
        case Direction::Left:
            g = {available_area_.x + outer, available_area_.y + outer, halfW, fullH};
            break;
        case Direction::Right:
            g = {available_area_.x + outer + halfW + inner,
                 available_area_.y + outer, halfW, fullH};
            break;
        default:
            break;
    }
    surface->setGeometry(g.x, g.y, g.width, g.height);
}

void FloatingLayout::updateSizeConstraints(Surface* surface, int minW, int minH,
                                            int maxW, int maxH) {
    auto it = states_.find(surface);
    if (it == states_.end()) return;

    auto& state = it->second;
    state.min_width  = std::max(1, minW);
    state.min_height = std::max(1, minH);
    state.max_width  = maxW;
    state.max_height = maxH;
    enforceSizeConstraints(state);
    surface->setGeometry(state.geometry.x, state.geometry.y,
                         state.geometry.width, state.geometry.height);
}

void FloatingLayout::setMousePosition(int mx, int my) noexcept {
    mouse_x_ = mx;
    mouse_y_ = my;
}

// ---------------------------------------------------------------------------
// recalculate -- floating keeps window positions, no forced layout
// ---------------------------------------------------------------------------

void FloatingLayout::recalculate(Box availableArea) {
    available_area_ = availableArea;
    for (auto* s : window_order_) {
        auto it = states_.find(s);
        if (it != states_.end() && !it->second.minimized) {
            auto& g = it->second.geometry;
            s->setGeometry(g.x, g.y, g.width, g.height);
        }
    }
}

} // namespace eternal

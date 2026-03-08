#include "eternal/layout/ScrollableLayout.hpp"

#include "eternal/core/Surface.hpp"

#include <algorithm>
#include <cmath>

namespace eternal {

// ---------------------------------------------------------------------------
// SpringState implementation
// ---------------------------------------------------------------------------

void SpringState::update(float dt) {
    // Critically-damped spring simulation.
    float displacement = position - target;
    float spring_force = -stiffness * displacement;
    float damping_force = -damping * velocity;
    float acceleration = (spring_force + damping_force) / mass;

    velocity += acceleration * dt;
    position += velocity * dt;
}

bool SpringState::isSettled(float threshold) const {
    return std::abs(position - target) < threshold &&
           std::abs(velocity) < threshold;
}

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

ScrollableLayout::ScrollableLayout()
    : ScrollableLayout(ScrollableLayoutConfig{}) {}

ScrollableLayout::ScrollableLayout(ScrollableLayoutConfig config)
    : config_(std::move(config)) {
    viewport_spring_.stiffness = config_.spring_stiffness;
    viewport_spring_.damping = config_.spring_damping;
}

ScrollableLayout::~ScrollableLayout() = default;

// ---------------------------------------------------------------------------
// ILayout identity
// ---------------------------------------------------------------------------

LayoutType       ScrollableLayout::getType() const noexcept { return LayoutType::Scrollable; }
std::string_view ScrollableLayout::getName() const noexcept { return "scrollable"; }

// ---------------------------------------------------------------------------
// Gaps
// ---------------------------------------------------------------------------

void ScrollableLayout::setGaps(GapConfig gaps) {
    gaps_ = gaps;
    recalculate(available_area_);
}

// ---------------------------------------------------------------------------
// Windows
// ---------------------------------------------------------------------------

std::vector<Surface*> ScrollableLayout::getWindows() const {
    std::vector<Surface*> result;
    for (auto& col : columns_) {
        result.insert(result.end(), col.windows.begin(), col.windows.end());
    }
    return result;
}

// ---------------------------------------------------------------------------
// Width resolution
// ---------------------------------------------------------------------------

int ScrollableLayout::resolveWidth(const ColumnWidth& cw) const {
    int usable_w = available_area_.width - 2 * gaps_.outer;
    if (auto* p = std::get_if<ProportionWidth>(&cw)) {
        return static_cast<int>(usable_w * p->proportion);
    }
    if (auto* f = std::get_if<FixedWidth>(&cw)) {
        return f->pixels;
    }
    return usable_w / 2;
}

// ---------------------------------------------------------------------------
// ensureFocusValid
// ---------------------------------------------------------------------------

void ScrollableLayout::ensureFocusValid() {
    if (columns_.empty()) {
        focused_column_ = 0;
        return;
    }
    focused_column_ = std::clamp(focused_column_, 0,
                                  static_cast<int>(columns_.size()) - 1);

    auto& col = columns_[focused_column_];
    if (!col.windows.empty()) {
        col.active_window_index = std::clamp(col.active_window_index, 0,
                                              static_cast<int>(col.windows.size()) - 1);
    }
}

// ---------------------------------------------------------------------------
// Viewport target calculation
// ---------------------------------------------------------------------------

int ScrollableLayout::computeColumnX(int columnIndex) const {
    int inner = gaps_.inner;
    int cur_x = 0;
    for (int ci = 0; ci < columnIndex && ci < static_cast<int>(columns_.size()); ++ci) {
        int col_w = resolveWidth(columns_[ci].width);
        col_w = std::max(1, col_w);
        cur_x += col_w + inner;
    }
    return cur_x;
}

void ScrollableLayout::updateViewportTarget() {
    if (columns_.empty()) {
        viewport_spring_.target = 0.0f;
        return;
    }

    ensureFocusValid();
    int usable_w = available_area_.width - 2 * gaps_.outer;
    int col_x = computeColumnX(focused_column_);
    int col_w = resolveWidth(columns_[focused_column_].width);

    switch (config_.center_focused) {
        case CenterFocusMode::Always: {
            int col_center = col_x + col_w / 2;
            int view_center = usable_w / 2;
            viewport_spring_.target = static_cast<float>(col_center - view_center);
            break;
        }
        case CenterFocusMode::OnOverflow: {
            float cur = viewport_spring_.target;
            // Check if column is fully visible.
            if (col_x < cur || col_x + col_w > cur + usable_w) {
                int col_center = col_x + col_w / 2;
                int view_center = usable_w / 2;
                viewport_spring_.target = static_cast<float>(col_center - view_center);
            }
            break;
        }
        case CenterFocusMode::Never: {
            float cur = viewport_spring_.target;
            // Ensure the focused column is visible.
            if (col_x < cur) {
                viewport_spring_.target = static_cast<float>(col_x);
            } else if (col_x + col_w > cur + usable_w) {
                viewport_spring_.target = static_cast<float>(col_x + col_w - usable_w);
            }
            break;
        }
    }

    // Clamp target to non-negative.
    if (viewport_spring_.target < 0.0f) viewport_spring_.target = 0.0f;

    // If spring scrolling is disabled, snap immediately.
    if (!config_.spring_scrolling) {
        viewport_spring_.position = viewport_spring_.target;
        viewport_spring_.velocity = 0.0f;
    }
}

// ---------------------------------------------------------------------------
// addWindow -- creates a new column to the right of the focused column
// ---------------------------------------------------------------------------

void ScrollableLayout::addWindow(Surface* surface) {
    if (!surface) return;

    float prop = static_cast<float>(config_.default_column_width_proportion_num)
               / static_cast<float>(config_.default_column_width_proportion_den);

    Column col;
    col.windows.push_back(surface);
    col.width = ProportionWidth{prop};
    col.active_window_index = 0;

    if (columns_.empty()) {
        columns_.push_back(std::move(col));
        focused_column_ = 0;
    } else {
        int insert_at = focused_column_ + 1;
        columns_.insert(columns_.begin() + insert_at, std::move(col));
        focused_column_ = insert_at;
    }

    updateViewportTarget();
    recalculate(available_area_);
}

// ---------------------------------------------------------------------------
// removeWindow
// ---------------------------------------------------------------------------

void ScrollableLayout::removeWindow(Surface* surface) {
    if (!surface) return;

    for (int ci = 0; ci < static_cast<int>(columns_.size()); ++ci) {
        auto& wins = columns_[ci].windows;
        auto it = std::find(wins.begin(), wins.end(), surface);
        if (it != wins.end()) {
            wins.erase(it);
            if (wins.empty()) {
                columns_.erase(columns_.begin() + ci);
            }
            ensureFocusValid();
            updateViewportTarget();
            recalculate(available_area_);
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// Focus navigation
// ---------------------------------------------------------------------------

void ScrollableLayout::focusNext() {
    if (columns_.empty()) return;

    auto& col = columns_[focused_column_];
    if (col.active_window_index + 1 < static_cast<int>(col.windows.size())) {
        col.active_window_index++;
    } else if (focused_column_ + 1 < static_cast<int>(columns_.size())) {
        focused_column_++;
        columns_[focused_column_].active_window_index = 0;
    }
    updateViewportTarget();
}

void ScrollableLayout::focusPrev() {
    if (columns_.empty()) return;

    auto& col = columns_[focused_column_];
    if (col.active_window_index > 0) {
        col.active_window_index--;
    } else if (focused_column_ > 0) {
        focused_column_--;
        auto& prev = columns_[focused_column_];
        prev.active_window_index = static_cast<int>(prev.windows.size()) - 1;
    }
    updateViewportTarget();
}

void ScrollableLayout::focusDirection(Direction dir) {
    if (columns_.empty()) return;

    switch (dir) {
        case Direction::Left:
            if (focused_column_ > 0) {
                focused_column_--;
                auto& col = columns_[focused_column_];
                col.active_window_index = std::clamp(
                    col.active_window_index, 0,
                    std::max(0, static_cast<int>(col.windows.size()) - 1));
            }
            break;
        case Direction::Right:
            if (focused_column_ + 1 < static_cast<int>(columns_.size())) {
                focused_column_++;
                auto& col = columns_[focused_column_];
                col.active_window_index = std::clamp(
                    col.active_window_index, 0,
                    std::max(0, static_cast<int>(col.windows.size()) - 1));
            }
            break;
        case Direction::Up: {
            auto& col = columns_[focused_column_];
            if (col.display_mode == ColumnDisplayMode::Tabbed) {
                // In tabbed mode, up cycles tabs.
                if (col.active_window_index > 0) col.active_window_index--;
            } else {
                if (col.active_window_index > 0) col.active_window_index--;
            }
            break;
        }
        case Direction::Down: {
            auto& col = columns_[focused_column_];
            if (col.display_mode == ColumnDisplayMode::Tabbed) {
                if (col.active_window_index + 1 < static_cast<int>(col.windows.size())) {
                    col.active_window_index++;
                }
            } else {
                if (col.active_window_index + 1 < static_cast<int>(col.windows.size())) {
                    col.active_window_index++;
                }
            }
            break;
        }
    }

    updateViewportTarget();
}

// ---------------------------------------------------------------------------
// moveWindow
// ---------------------------------------------------------------------------

void ScrollableLayout::moveWindow(Direction dir) {
    if (columns_.empty()) return;
    ensureFocusValid();

    auto& src_col = columns_[focused_column_];
    if (src_col.windows.empty()) return;

    Surface* surface = src_col.windows[src_col.active_window_index];

    switch (dir) {
        case Direction::Left: {
            src_col.windows.erase(src_col.windows.begin() + src_col.active_window_index);
            if (focused_column_ > 0) {
                auto& dst = columns_[focused_column_ - 1];
                dst.windows.push_back(surface);
                if (src_col.windows.empty()) {
                    columns_.erase(columns_.begin() + focused_column_);
                }
                focused_column_--;
                columns_[focused_column_].active_window_index =
                    static_cast<int>(columns_[focused_column_].windows.size()) - 1;
            } else {
                Column new_col;
                new_col.windows.push_back(surface);
                if (src_col.windows.empty()) {
                    columns_.erase(columns_.begin() + focused_column_);
                }
                columns_.insert(columns_.begin(), std::move(new_col));
                focused_column_ = 0;
                columns_[0].active_window_index = 0;
            }
            break;
        }
        case Direction::Right: {
            src_col.windows.erase(src_col.windows.begin() + src_col.active_window_index);
            if (focused_column_ + 1 < static_cast<int>(columns_.size())) {
                auto& dst = columns_[focused_column_ + 1];
                dst.windows.push_back(surface);
                if (src_col.windows.empty()) {
                    columns_.erase(columns_.begin() + focused_column_);
                } else {
                    focused_column_++;
                }
                columns_[focused_column_].active_window_index =
                    static_cast<int>(columns_[focused_column_].windows.size()) - 1;
            } else {
                Column new_col;
                new_col.windows.push_back(surface);
                if (src_col.windows.empty()) {
                    columns_.erase(columns_.begin() + focused_column_);
                }
                columns_.push_back(std::move(new_col));
                focused_column_ = static_cast<int>(columns_.size()) - 1;
                columns_[focused_column_].active_window_index = 0;
            }
            break;
        }
        case Direction::Up: {
            int idx = src_col.active_window_index;
            if (idx > 0) {
                std::swap(src_col.windows[idx], src_col.windows[idx - 1]);
                src_col.active_window_index = idx - 1;
            }
            break;
        }
        case Direction::Down: {
            int idx = src_col.active_window_index;
            if (idx + 1 < static_cast<int>(src_col.windows.size())) {
                std::swap(src_col.windows[idx], src_col.windows[idx + 1]);
                src_col.active_window_index = idx + 1;
            }
            break;
        }
    }

    ensureFocusValid();
    updateViewportTarget();
    recalculate(available_area_);
}

// ---------------------------------------------------------------------------
// resizeWindow -- adjust column width
// ---------------------------------------------------------------------------

void ScrollableLayout::resizeWindow(Surface* surface, SizeDelta delta) {
    if (!surface) return;

    for (int ci = 0; ci < static_cast<int>(columns_.size()); ++ci) {
        auto& wins = columns_[ci].windows;
        if (std::find(wins.begin(), wins.end(), surface) != wins.end()) {
            int current_w = resolveWidth(columns_[ci].width);
            int new_w = std::max(50, current_w + delta.dx);
            columns_[ci].width = FixedWidth{new_w};
            recalculate(available_area_);
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// recalculate -- position columns on the strip, apply viewport offset
// ---------------------------------------------------------------------------

void ScrollableLayout::recalculate(Box availableArea) {
    available_area_ = availableArea;
    if (columns_.empty()) return;

    int outer = gaps_.outer;
    int inner = gaps_.inner;

    int usable_w = availableArea.width  - 2 * outer;
    int usable_h = availableArea.height - 2 * outer;
    int base_x   = availableArea.x + outer;
    int base_y   = availableArea.y + outer;

    if (usable_w <= 0 || usable_h <= 0) return;

    int viewport_offset = static_cast<int>(viewport_spring_.position);

    // Position each column sequentially.
    int cur_x = 0;
    for (int ci = 0; ci < static_cast<int>(columns_.size()); ++ci) {
        auto& col = columns_[ci];
        int col_w = resolveWidth(col.width);
        col_w = std::max(1, col_w);

        int n = static_cast<int>(col.windows.size());
        if (n == 0) {
            cur_x += col_w + inner;
            continue;
        }

        int wx = base_x + cur_x - viewport_offset;

        if (col.display_mode == ColumnDisplayMode::Tabbed) {
            // In tabbed mode, all windows share the column geometry.
            // Only the active window is positioned visibly.
            constexpr int kTabBarHeight = 28;
            int win_y = base_y + kTabBarHeight;
            int win_h = usable_h - kTabBarHeight;
            win_h = std::max(1, win_h);

            for (int wi = 0; wi < n; ++wi) {
                if (wi == col.active_window_index) {
                    col.windows[wi]->setGeometry(wx, win_y, col_w, win_h);
                } else {
                    // Place off-screen or at same position (hidden by compositor).
                    col.windows[wi]->setGeometry(wx, win_y, col_w, win_h);
                }
            }
        } else {
            // Normal vertical stack within column.
            int total_inner_gap = inner * (n - 1);
            int win_h = (usable_h - total_inner_gap) / n;
            win_h = std::max(1, win_h);

            for (int wi = 0; wi < n; ++wi) {
                int wy = base_y + wi * (win_h + inner);
                int h = (wi == n - 1) ? (base_y + usable_h - wy) : win_h;
                col.windows[wi]->setGeometry(wx, wy, col_w, h);
            }
        }

        cur_x += col_w + inner;
    }
}

// ---------------------------------------------------------------------------
// Scrollable-specific
// ---------------------------------------------------------------------------

void ScrollableLayout::scrollLeft(float amount) {
    viewport_spring_.target -= amount * config_.scroll_speed;
    if (viewport_spring_.target < 0.0f) viewport_spring_.target = 0.0f;
    if (!config_.spring_scrolling) {
        viewport_spring_.position = viewport_spring_.target;
        viewport_spring_.velocity = 0.0f;
    }
    recalculate(available_area_);
}

void ScrollableLayout::scrollRight(float amount) {
    viewport_spring_.target += amount * config_.scroll_speed;
    if (!config_.spring_scrolling) {
        viewport_spring_.position = viewport_spring_.target;
        viewport_spring_.velocity = 0.0f;
    }
    recalculate(available_area_);
}

void ScrollableLayout::scrollToWindow(Surface* surface) {
    if (!surface) return;

    int inner = gaps_.inner;
    int usable_w = available_area_.width - 2 * gaps_.outer;

    int cur_x = 0;
    for (auto& col : columns_) {
        int col_w = resolveWidth(col.width);
        auto it = std::find(col.windows.begin(), col.windows.end(), surface);
        if (it != col.windows.end()) {
            float cur_vp = viewport_spring_.target;
            if (cur_x < cur_vp) {
                viewport_spring_.target = static_cast<float>(cur_x);
            } else if (cur_x + col_w > cur_vp + usable_w) {
                viewport_spring_.target = static_cast<float>(cur_x + col_w - usable_w);
            }
            if (viewport_spring_.target < 0.0f) viewport_spring_.target = 0.0f;
            if (!config_.spring_scrolling) {
                viewport_spring_.position = viewport_spring_.target;
                viewport_spring_.velocity = 0.0f;
            }
            recalculate(available_area_);
            return;
        }
        cur_x += col_w + inner;
    }
}

void ScrollableLayout::centerFocusedColumn() {
    if (columns_.empty()) return;
    ensureFocusValid();

    int usable_w = available_area_.width - 2 * gaps_.outer;
    int col_x = computeColumnX(focused_column_);
    int col_w = resolveWidth(columns_[focused_column_].width);

    int col_center = col_x + col_w / 2;
    int view_center = usable_w / 2;
    viewport_spring_.target = static_cast<float>(col_center - view_center);
    if (viewport_spring_.target < 0.0f) viewport_spring_.target = 0.0f;

    if (!config_.spring_scrolling) {
        viewport_spring_.position = viewport_spring_.target;
        viewport_spring_.velocity = 0.0f;
    }
    recalculate(available_area_);
}

void ScrollableLayout::setColumnWidth(ColumnWidth width) {
    if (columns_.empty()) return;
    ensureFocusValid();
    columns_[focused_column_].width = width;
    recalculate(available_area_);
}

void ScrollableLayout::cycleColumnWidth(bool forward) {
    if (columns_.empty()) return;
    ensureFocusValid();

    auto& presets = config_.preset_widths.widths;
    if (presets.empty()) return;

    auto& col = columns_[focused_column_];
    int current_w = resolveWidth(col.width);

    int best_idx = 0;
    int best_diff = INT32_MAX;
    for (int i = 0; i < static_cast<int>(presets.size()); ++i) {
        int pw = resolveWidth(presets[i]);
        int diff = std::abs(pw - current_w);
        if (diff < best_diff) {
            best_diff = diff;
            best_idx = i;
        }
    }

    int n = static_cast<int>(presets.size());
    int next_idx = forward ? (best_idx + 1) % n : (best_idx - 1 + n) % n;
    col.width = presets[next_idx];
    recalculate(available_area_);
}

void ScrollableLayout::addColumn() {
    float prop = static_cast<float>(config_.default_column_width_proportion_num)
               / static_cast<float>(config_.default_column_width_proportion_den);

    Column col;
    col.width = ProportionWidth{prop};

    int insert_at = focused_column_ + 1;
    if (insert_at > static_cast<int>(columns_.size())) {
        insert_at = static_cast<int>(columns_.size());
    }
    columns_.insert(columns_.begin() + insert_at, std::move(col));
}

void ScrollableLayout::removeColumn() {
    if (columns_.empty()) return;
    ensureFocusValid();

    auto& col = columns_[focused_column_];
    if (col.windows.empty()) {
        columns_.erase(columns_.begin() + focused_column_);
        ensureFocusValid();
        updateViewportTarget();
        recalculate(available_area_);
        return;
    }

    int target = (focused_column_ > 0) ? focused_column_ - 1
                                        : (focused_column_ + 1 < static_cast<int>(columns_.size())
                                               ? focused_column_ + 1 : -1);
    if (target < 0) return;

    auto& dst = columns_[target];
    dst.windows.insert(dst.windows.end(), col.windows.begin(), col.windows.end());
    columns_.erase(columns_.begin() + focused_column_);
    ensureFocusValid();
    updateViewportTarget();
    recalculate(available_area_);
}

void ScrollableLayout::splitColumn() {
    if (columns_.empty()) return;
    ensureFocusValid();

    auto& col = columns_[focused_column_];
    if (col.windows.size() < 2) return;

    Surface* surface = col.windows[col.active_window_index];
    col.windows.erase(col.windows.begin() + col.active_window_index);

    Column new_col;
    new_col.windows.push_back(surface);
    new_col.width = col.width;

    int insert_at = focused_column_ + 1;
    columns_.insert(columns_.begin() + insert_at, std::move(new_col));
    focused_column_ = insert_at;
    columns_[focused_column_].active_window_index = 0;

    ensureFocusValid();
    updateViewportTarget();
    recalculate(available_area_);
}

void ScrollableLayout::toggleColumnTabbed() {
    if (columns_.empty()) return;
    ensureFocusValid();

    auto& col = columns_[focused_column_];
    col.display_mode = (col.display_mode == ColumnDisplayMode::Normal)
                           ? ColumnDisplayMode::Tabbed
                           : ColumnDisplayMode::Normal;
    recalculate(available_area_);
}

float ScrollableLayout::getViewportPosition() const noexcept {
    return viewport_spring_.position;
}

float ScrollableLayout::getViewportTarget() const noexcept {
    return viewport_spring_.target;
}

const std::vector<Column>& ScrollableLayout::getColumns() const noexcept {
    return columns_;
}

int ScrollableLayout::getFocusedColumnIndex() const noexcept {
    return focused_column_;
}

bool ScrollableLayout::tickAnimation(float dt) {
    if (viewport_spring_.isSettled()) return false;
    viewport_spring_.update(dt);
    recalculate(available_area_);
    return !viewport_spring_.isSettled();
}

void ScrollableLayout::setCenterFocusMode(CenterFocusMode mode) {
    config_.center_focused = mode;
    updateViewportTarget();
    recalculate(available_area_);
}

std::vector<float> ScrollableLayout::getColumnSnapPositions() const {
    std::vector<float> positions;
    if (columns_.empty()) return positions;

    int inner = gaps_.inner;
    positions.reserve(columns_.size());

    int cur_x = 0;
    for (const auto& col : columns_) {
        positions.push_back(static_cast<float>(cur_x));
        int col_w = resolveWidth(col.width);
        cur_x += col_w + inner;
    }
    return positions;
}

float ScrollableLayout::getTotalContentWidth() const {
    if (columns_.empty()) return 0.0f;

    int inner = gaps_.inner;
    int total = 0;
    for (const auto& col : columns_) {
        total += resolveWidth(col.width) + inner;
    }
    // Remove trailing gap
    if (!columns_.empty()) total -= inner;
    return static_cast<float>(total);
}

void ScrollableLayout::setViewportPosition(float pos) {
    viewport_spring_.position = pos;
    viewport_spring_.target = pos;
    viewport_spring_.velocity = 0.0f;
    recalculate(available_area_);
}

} // namespace eternal

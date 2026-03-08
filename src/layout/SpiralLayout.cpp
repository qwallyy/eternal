#include "eternal/layout/SpiralLayout.hpp"

#include "eternal/core/Surface.hpp"

#include <algorithm>
#include <cmath>
#include <climits>

namespace eternal {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

SpiralLayout::SpiralLayout() : SpiralLayout(SpiralLayoutConfig{}) {}

SpiralLayout::SpiralLayout(SpiralLayoutConfig config)
    : config_(config) {
    config_.split_ratio = std::clamp(config_.split_ratio, 0.1f, 0.9f);
}

SpiralLayout::~SpiralLayout() = default;

// ---------------------------------------------------------------------------
// ILayout identity
// ---------------------------------------------------------------------------

LayoutType       SpiralLayout::getType() const noexcept { return LayoutType::Spiral; }
std::string_view SpiralLayout::getName() const noexcept { return "spiral"; }

// ---------------------------------------------------------------------------
// Gaps
// ---------------------------------------------------------------------------

void SpiralLayout::setGaps(GapConfig gaps) {
    gaps_ = gaps;
    recalculate(available_area_);
}

// ---------------------------------------------------------------------------
// Window list
// ---------------------------------------------------------------------------

std::vector<Surface*> SpiralLayout::getWindows() const {
    return windows_;
}

// ---------------------------------------------------------------------------
// Spiral-specific
// ---------------------------------------------------------------------------

void SpiralLayout::setSplitRatio(float ratio) {
    config_.split_ratio = std::clamp(ratio, 0.1f, 0.9f);
    recalculate(available_area_);
}

float SpiralLayout::getSplitRatio() const noexcept {
    return config_.split_ratio;
}

void SpiralLayout::setDirection(SpiralDirection direction) {
    config_.direction = direction;
    recalculate(available_area_);
}

void SpiralLayout::toggleDirection() {
    config_.direction = (config_.direction == SpiralDirection::Clockwise)
                            ? SpiralDirection::CounterClockwise
                            : SpiralDirection::Clockwise;
    recalculate(available_area_);
}

// ---------------------------------------------------------------------------
// addWindow
// ---------------------------------------------------------------------------

void SpiralLayout::addWindow(Surface* surface) {
    if (!surface) return;
    windows_.push_back(surface);
    focused_index_ = static_cast<int>(windows_.size()) - 1;
    recalculate(available_area_);
}

// ---------------------------------------------------------------------------
// removeWindow
// ---------------------------------------------------------------------------

void SpiralLayout::removeWindow(Surface* surface) {
    if (!surface) return;
    auto it = std::find(windows_.begin(), windows_.end(), surface);
    if (it == windows_.end()) return;

    int idx = static_cast<int>(it - windows_.begin());
    windows_.erase(it);

    if (windows_.empty()) {
        focused_index_ = 0;
    } else {
        focused_index_ = std::min(idx, static_cast<int>(windows_.size()) - 1);
    }

    recalculate(available_area_);
}

// ---------------------------------------------------------------------------
// Focus
// ---------------------------------------------------------------------------

void SpiralLayout::focusNext() {
    if (windows_.empty()) return;
    focused_index_ = (focused_index_ + 1) % static_cast<int>(windows_.size());
}

void SpiralLayout::focusPrev() {
    if (windows_.empty()) return;
    int n = static_cast<int>(windows_.size());
    focused_index_ = (focused_index_ - 1 + n) % n;
}

void SpiralLayout::focusDirection(Direction dir) {
    // Spiral is inherently sequential; left/up = prev, right/down = next.
    switch (dir) {
        case Direction::Left:
        case Direction::Up:
            focusPrev();
            break;
        case Direction::Right:
        case Direction::Down:
            focusNext();
            break;
    }
}

// ---------------------------------------------------------------------------
// moveWindow -- swap with neighbour
// ---------------------------------------------------------------------------

void SpiralLayout::moveWindow(Direction dir) {
    if (windows_.size() < 2) return;

    int n = static_cast<int>(windows_.size());
    int target = focused_index_;

    switch (dir) {
        case Direction::Right:
        case Direction::Down:
            target = (focused_index_ + 1) % n;
            break;
        case Direction::Left:
        case Direction::Up:
            target = (focused_index_ - 1 + n) % n;
            break;
    }

    if (target != focused_index_) {
        std::swap(windows_[focused_index_], windows_[target]);
        focused_index_ = target;
        recalculate(available_area_);
    }
}

// ---------------------------------------------------------------------------
// resizeWindow -- adjust split ratio
// ---------------------------------------------------------------------------

void SpiralLayout::resizeWindow(Surface* /*surface*/, SizeDelta delta) {
    int d = (std::abs(delta.dx) > std::abs(delta.dy)) ? delta.dx : delta.dy;
    int size = std::max(available_area_.width, available_area_.height);
    if (size <= 0) return;

    float rd = static_cast<float>(d) / static_cast<float>(size);
    config_.split_ratio = std::clamp(config_.split_ratio + rd, 0.1f, 0.9f);
    recalculate(available_area_);
}

// ---------------------------------------------------------------------------
// recalculate -- Fibonacci spiral subdivision
//
// First window gets ratio of the area, each subsequent window takes ratio
// of the remaining space. Split direction cycles: right, down, left, up
// (clockwise) creating a golden-spiral arrangement.
// ---------------------------------------------------------------------------

void SpiralLayout::recalculate(Box availableArea) {
    available_area_ = availableArea;
    int n = static_cast<int>(windows_.size());
    if (n == 0) return;

    bool single = (n == 1) && config_.no_gaps_when_only;
    int outer = single ? 0 : gaps_.outer;
    int inner = gaps_.inner;

    Box area {
        availableArea.x + outer,
        availableArea.y + outer,
        availableArea.width  - 2 * outer,
        availableArea.height - 2 * outer,
    };
    if (area.empty()) return;

    // Single window fills everything.
    if (n == 1) {
        windows_[0]->setGeometry(area.x, area.y, area.width, area.height);
        return;
    }

    float ratio = config_.split_ratio;
    Box remaining = area;

    for (int i = 0; i < n; ++i) {
        // Last window takes the entire remaining area.
        if (i == n - 1) {
            windows_[i]->setGeometry(remaining.x, remaining.y,
                                     remaining.width, remaining.height);
            break;
        }

        // Determine split phase; clockwise cycles right/down/left/up.
        int phase = i % 4;
        if (config_.direction == SpiralDirection::CounterClockwise) {
            constexpr int mirror[] = {2, 1, 0, 3};
            phase = mirror[phase];
        }

        Box win{};
        int halfGap = inner / 2;

        switch (phase) {
            case 0: { // Split right: window gets left portion.
                int w = static_cast<int>(remaining.width * ratio) - halfGap;
                w = std::max(1, w);
                win = {remaining.x, remaining.y, w, remaining.height};
                remaining = {remaining.x + w + inner, remaining.y,
                             remaining.width - w - inner, remaining.height};
                break;
            }
            case 1: { // Split down: window gets top portion.
                int h = static_cast<int>(remaining.height * ratio) - halfGap;
                h = std::max(1, h);
                win = {remaining.x, remaining.y, remaining.width, h};
                remaining = {remaining.x, remaining.y + h + inner,
                             remaining.width, remaining.height - h - inner};
                break;
            }
            case 2: { // Split left: window gets right portion.
                int w = static_cast<int>(remaining.width * ratio) - halfGap;
                w = std::max(1, w);
                int remW = remaining.width - w - inner;
                remW = std::max(1, remW);
                win = {remaining.x + remW + inner, remaining.y,
                       w, remaining.height};
                remaining = {remaining.x, remaining.y, remW, remaining.height};
                break;
            }
            case 3: { // Split up: window gets bottom portion.
                int h = static_cast<int>(remaining.height * ratio) - halfGap;
                h = std::max(1, h);
                int remH = remaining.height - h - inner;
                remH = std::max(1, remH);
                win = {remaining.x, remaining.y + remH + inner,
                       remaining.width, h};
                remaining = {remaining.x, remaining.y,
                             remaining.width, remH};
                break;
            }
        }

        remaining.width  = std::max(1, remaining.width);
        remaining.height = std::max(1, remaining.height);

        windows_[i]->setGeometry(win.x, win.y, win.width, win.height);
    }
}

} // namespace eternal

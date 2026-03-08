#include "eternal/layout/MonocleLayout.hpp"

#include "eternal/core/Surface.hpp"

#include <algorithm>

namespace eternal {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

MonocleLayout::MonocleLayout() : MonocleLayout(MonocleLayoutConfig{}) {}

MonocleLayout::MonocleLayout(MonocleLayoutConfig config)
    : config_(config) {}

MonocleLayout::~MonocleLayout() = default;

// ---------------------------------------------------------------------------
// ILayout identity
// ---------------------------------------------------------------------------

LayoutType       MonocleLayout::getType() const noexcept { return LayoutType::Monocle; }
std::string_view MonocleLayout::getName() const noexcept { return "monocle"; }

// ---------------------------------------------------------------------------
// Gaps
// ---------------------------------------------------------------------------

void MonocleLayout::setGaps(GapConfig gaps) {
    gaps_ = gaps;
    recalculate(available_area_);
}

// ---------------------------------------------------------------------------
// Window list
// ---------------------------------------------------------------------------

std::vector<Surface*> MonocleLayout::getWindows() const {
    return windows_;
}

int MonocleLayout::getWindowCount() const noexcept {
    return static_cast<int>(windows_.size());
}

int MonocleLayout::getFocusedIndex() const noexcept {
    return focused_index_;
}

// ---------------------------------------------------------------------------
// addWindow
// ---------------------------------------------------------------------------

void MonocleLayout::addWindow(Surface* surface) {
    if (!surface) return;
    windows_.push_back(surface);
    focused_index_ = static_cast<int>(windows_.size()) - 1;
    recalculate(available_area_);
}

// ---------------------------------------------------------------------------
// removeWindow
// ---------------------------------------------------------------------------

void MonocleLayout::removeWindow(Surface* surface) {
    if (!surface) return;
    auto it = std::find(windows_.begin(), windows_.end(), surface);
    if (it == windows_.end()) return;

    int idx = static_cast<int>(it - windows_.begin());
    windows_.erase(it);

    if (windows_.empty()) {
        focused_index_ = 0;
        return;
    }

    // auto_next_on_close: keep focus at the same position or wrap.
    focused_index_ = std::min(idx, static_cast<int>(windows_.size()) - 1);
    recalculate(available_area_);
}

// ---------------------------------------------------------------------------
// Focus navigation
// ---------------------------------------------------------------------------

void MonocleLayout::focusNext() {
    if (windows_.empty()) return;
    focused_index_ = (focused_index_ + 1) % static_cast<int>(windows_.size());
    recalculate(available_area_);
}

void MonocleLayout::focusPrev() {
    if (windows_.empty()) return;
    int n = static_cast<int>(windows_.size());
    focused_index_ = (focused_index_ - 1 + n) % n;
    recalculate(available_area_);
}

void MonocleLayout::focusDirection(Direction dir) {
    // In monocle, left/up = prev, right/down = next.
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
// moveWindow -- reorder in list
// ---------------------------------------------------------------------------

void MonocleLayout::moveWindow(Direction dir) {
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
    }
}

// ---------------------------------------------------------------------------
// resizeWindow -- no-op in monocle
// ---------------------------------------------------------------------------

void MonocleLayout::resizeWindow(Surface* /*surface*/, SizeDelta /*delta*/) {
    // Monocle windows always fill the available area.
}

// ---------------------------------------------------------------------------
// recalculate -- focused window gets full area
// ---------------------------------------------------------------------------

void MonocleLayout::recalculate(Box availableArea) {
    available_area_ = availableArea;
    if (windows_.empty()) return;

    // Clamp focused index.
    focused_index_ = std::clamp(focused_index_, 0,
                                static_cast<int>(windows_.size()) - 1);

    bool useGaps = !config_.no_gaps;
    // Smart fullscreen: single window gets zero gaps.
    if (windows_.size() == 1 && config_.smart_fullscreen) useGaps = false;

    int outer = useGaps ? gaps_.outer : 0;

    Box usable {
        availableArea.x + outer,
        availableArea.y + outer,
        availableArea.width  - 2 * outer,
        availableArea.height - 2 * outer,
    };
    if (usable.empty()) return;

    // Only the focused window gets the full area; others are not repositioned
    // but the compositor should hide them based on focus state.
    for (int i = 0; i < static_cast<int>(windows_.size()); ++i) {
        if (i == focused_index_) {
            windows_[i]->setGeometry(usable.x, usable.y,
                                     usable.width, usable.height);
        }
    }
}

} // namespace eternal

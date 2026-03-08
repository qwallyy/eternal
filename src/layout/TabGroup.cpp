#include "eternal/layout/TabGroup.hpp"

#include <algorithm>

namespace eternal {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

TabGroup::TabGroup(int group_id)
    : TabGroup(group_id, TabGroupConfig{}) {}

TabGroup::TabGroup(int group_id, TabGroupConfig config)
    : group_id_(group_id), config_(config) {}

TabGroup::~TabGroup() = default;

// ---------------------------------------------------------------------------
// Window management
// ---------------------------------------------------------------------------

void TabGroup::addWindow(Surface* surface) {
    if (!surface || locked_) return;

    // Avoid duplicates.
    if (std::find(windows_.begin(), windows_.end(), surface) != windows_.end()) return;

    windows_.push_back(surface);
    active_index_ = static_cast<int>(windows_.size()) - 1;
    applyGeometry();
}

void TabGroup::insertWindow(int index, Surface* surface) {
    if (!surface || locked_) return;
    if (std::find(windows_.begin(), windows_.end(), surface) != windows_.end()) return;

    index = std::clamp(index, 0, static_cast<int>(windows_.size()));
    windows_.insert(windows_.begin() + index, surface);
    active_index_ = index;
    applyGeometry();
}

void TabGroup::removeWindow(Surface* surface) {
    auto it = std::find(windows_.begin(), windows_.end(), surface);
    if (it == windows_.end()) return;

    int idx = static_cast<int>(it - windows_.begin());
    windows_.erase(it);

    if (windows_.empty()) {
        active_index_ = 0;
    } else {
        active_index_ = std::min(idx, static_cast<int>(windows_.size()) - 1);
    }

    applyGeometry();
}

const std::vector<Surface*>& TabGroup::getWindows() const noexcept {
    return windows_;
}

int TabGroup::tabCount() const noexcept {
    return static_cast<int>(windows_.size());
}

bool TabGroup::isEmpty() const noexcept {
    return windows_.empty();
}

// ---------------------------------------------------------------------------
// Active tab
// ---------------------------------------------------------------------------

int TabGroup::getActiveIndex() const noexcept {
    return active_index_;
}

Surface* TabGroup::getActiveSurface() const noexcept {
    if (windows_.empty()) return nullptr;
    int idx = std::clamp(active_index_, 0, static_cast<int>(windows_.size()) - 1);
    return windows_[idx];
}

void TabGroup::setActiveIndex(int index) {
    if (windows_.empty()) return;
    active_index_ = std::clamp(index, 0, static_cast<int>(windows_.size()) - 1);
    applyGeometry();
}

void TabGroup::setActive(Surface* surface) {
    auto it = std::find(windows_.begin(), windows_.end(), surface);
    if (it != windows_.end()) {
        active_index_ = static_cast<int>(it - windows_.begin());
        applyGeometry();
    }
}

void TabGroup::cycleNext() {
    if (windows_.size() < 2) return;
    active_index_ = (active_index_ + 1) % static_cast<int>(windows_.size());
    applyGeometry();
}

void TabGroup::cyclePrev() {
    if (windows_.size() < 2) return;
    int n = static_cast<int>(windows_.size());
    active_index_ = (active_index_ - 1 + n) % n;
    applyGeometry();
}

// ---------------------------------------------------------------------------
// Tab reordering
// ---------------------------------------------------------------------------

void TabGroup::moveTab(int from_index, int to_index) {
    int n = static_cast<int>(windows_.size());
    if (from_index < 0 || from_index >= n || to_index < 0 || to_index >= n) return;
    if (from_index == to_index) return;

    Surface* moving = windows_[from_index];
    windows_.erase(windows_.begin() + from_index);
    windows_.insert(windows_.begin() + to_index, moving);

    // Update active index to follow the active tab.
    if (active_index_ == from_index) {
        active_index_ = to_index;
    } else if (from_index < active_index_ && to_index >= active_index_) {
        active_index_--;
    } else if (from_index > active_index_ && to_index <= active_index_) {
        active_index_++;
    }
}

void TabGroup::swapTabs(int index_a, int index_b) {
    int n = static_cast<int>(windows_.size());
    if (index_a < 0 || index_a >= n || index_b < 0 || index_b >= n) return;

    std::swap(windows_[index_a], windows_[index_b]);

    if (active_index_ == index_a) active_index_ = index_b;
    else if (active_index_ == index_b) active_index_ = index_a;
}

// ---------------------------------------------------------------------------
// Group locking
// ---------------------------------------------------------------------------

void TabGroup::lock()   { locked_ = true; }
void TabGroup::unlock() { locked_ = false; }
bool TabGroup::isLocked() const noexcept { return locked_; }

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

void TabGroup::setGeometry(const Box& bounds) {
    geometry_ = bounds;
    applyGeometry();
}

const Box& TabGroup::getGeometry() const noexcept {
    return geometry_;
}

Box TabGroup::getContentArea() const noexcept {
    return {
        geometry_.x,
        geometry_.y + config_.tab_bar_height,
        geometry_.width,
        std::max(1, geometry_.height - config_.tab_bar_height),
    };
}

Box TabGroup::getTabBarArea() const noexcept {
    return {
        geometry_.x,
        geometry_.y,
        geometry_.width,
        config_.tab_bar_height,
    };
}

std::vector<TabInfo> TabGroup::computeTabInfos() const {
    std::vector<TabInfo> infos;
    if (windows_.empty()) return infos;

    int n = static_cast<int>(windows_.size());
    int available_w = geometry_.width - config_.tab_padding * 2;
    int tab_w = std::max(config_.tab_min_width, available_w / n);

    // If tabs are too wide, clamp to available width.
    if (tab_w * n > available_w) {
        tab_w = available_w / n;
    }

    int cur_x = geometry_.x + config_.tab_padding;
    for (int i = 0; i < n; ++i) {
        TabInfo info;
        info.surface = windows_[i];
        info.active = (i == active_index_);
        info.x = cur_x;
        info.y = geometry_.y;
        info.width = (i == n - 1) ? (geometry_.x + geometry_.width - config_.tab_padding - cur_x)
                                   : tab_w;
        info.height = config_.tab_bar_height;
        // Title would be fetched from surface->getTitle() in production.
        infos.push_back(info);
        cur_x += tab_w;
    }

    return infos;
}

int TabGroup::hitTestTab(int px, int py) const {
    auto bar = getTabBarArea();
    if (!bar.contains(px, py)) return -1;

    auto tabs = computeTabInfos();
    for (int i = 0; i < static_cast<int>(tabs.size()); ++i) {
        Box tb{tabs[i].x, tabs[i].y, tabs[i].width, tabs[i].height};
        if (tb.contains(px, py)) return i;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Apply geometry to surfaces
// ---------------------------------------------------------------------------

void TabGroup::applyGeometry() {
    if (windows_.empty()) return;

    Box content = getContentArea();
    for (int i = 0; i < static_cast<int>(windows_.size()); ++i) {
        // All surfaces get the content area geometry; the compositor only
        // renders the active one.  The inactive surfaces are placed at the
        // same position so they can be shown instantly on tab switch.
        windows_[i]->setGeometry(content.x, content.y, content.width, content.height);
    }
}

} // namespace eternal

#include "eternal/layout/MasterLayout.hpp"

#include "eternal/core/Surface.hpp"

#include <algorithm>
#include <cmath>

namespace eternal {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

MasterLayout::MasterLayout() : MasterLayout(MasterLayoutConfig{}) {}

MasterLayout::MasterLayout(MasterLayoutConfig config)
    : config_(config) {
    config_.master_ratio = std::clamp(config_.master_ratio, 0.05f, 0.95f);
    config_.master_count = std::max(1, config_.master_count);
}

MasterLayout::~MasterLayout() = default;

// ---------------------------------------------------------------------------
// ILayout identity
// ---------------------------------------------------------------------------

LayoutType       MasterLayout::getType() const noexcept { return LayoutType::Master; }
std::string_view MasterLayout::getName() const noexcept { return "master"; }

// ---------------------------------------------------------------------------
// Gaps
// ---------------------------------------------------------------------------

void MasterLayout::setGaps(GapConfig gaps) {
    gaps_ = gaps;
    recalculate(available_area_);
}

// ---------------------------------------------------------------------------
// Window list
// ---------------------------------------------------------------------------

std::vector<Surface*> MasterLayout::getWindows() const {
    std::vector<Surface*> all;
    all.reserve(masters_.size() + stack_.size());
    all.insert(all.end(), masters_.begin(), masters_.end());
    all.insert(all.end(), stack_.begin(),   stack_.end());
    return all;
}

bool MasterLayout::isInMaster(int fi) const noexcept {
    return fi < static_cast<int>(masters_.size());
}

Surface*& MasterLayout::surfaceAt(int index) {
    int nm = static_cast<int>(masters_.size());
    if (index < nm) return masters_[index];
    return stack_[index - nm];
}

// ---------------------------------------------------------------------------
// addWindow
// ---------------------------------------------------------------------------

void MasterLayout::addWindow(Surface* surface) {
    if (!surface) return;

    if (config_.new_on_top) {
        // New window becomes master; push existing masters to stack.
        if (static_cast<int>(masters_.size()) >= config_.master_count && !masters_.empty()) {
            stack_.insert(stack_.begin(), masters_.back());
            masters_.pop_back();
        }
        masters_.insert(masters_.begin(), surface);
    } else if (static_cast<int>(masters_.size()) < config_.master_count) {
        masters_.push_back(surface);
    } else {
        stack_.push_back(surface);
    }

    focused_index_ = static_cast<int>(masters_.size() + stack_.size()) - 1;
    recalculate(available_area_);
}

// ---------------------------------------------------------------------------
// removeWindow
// ---------------------------------------------------------------------------

void MasterLayout::removeWindow(Surface* surface) {
    if (!surface) return;

    auto mit = std::find(masters_.begin(), masters_.end(), surface);
    if (mit != masters_.end()) {
        masters_.erase(mit);
        // Promote from stack if master count is below target.
        while (static_cast<int>(masters_.size()) < config_.master_count && !stack_.empty()) {
            masters_.push_back(stack_.front());
            stack_.erase(stack_.begin());
        }
    } else {
        auto sit = std::find(stack_.begin(), stack_.end(), surface);
        if (sit != stack_.end()) stack_.erase(sit);
    }

    int total = static_cast<int>(masters_.size() + stack_.size());
    focused_index_ = total > 0 ? std::min(focused_index_, total - 1) : 0;

    recalculate(available_area_);
}

// ---------------------------------------------------------------------------
// Focus
// ---------------------------------------------------------------------------

void MasterLayout::focusNext() {
    auto all = getWindows();
    if (all.empty()) return;
    focused_index_ = (focused_index_ + 1) % static_cast<int>(all.size());
}

void MasterLayout::focusPrev() {
    auto all = getWindows();
    if (all.empty()) return;
    int n = static_cast<int>(all.size());
    focused_index_ = (focused_index_ - 1 + n) % n;
}

void MasterLayout::focusDirection(Direction dir) {
    auto all = getWindows();
    if (all.size() < 2) return;

    int nm = static_cast<int>(masters_.size());
    bool inMaster = (focused_index_ < nm);
    bool horizontal = (config_.orientation == MasterOrientation::Left ||
                       config_.orientation == MasterOrientation::Right ||
                       config_.orientation == MasterOrientation::Center);

    // Lateral movement crosses between master and stack.
    bool towardMaster = false;
    if (horizontal) {
        towardMaster = (config_.orientation == MasterOrientation::Left ||
                        config_.orientation == MasterOrientation::Center)
                           ? (dir == Direction::Left)
                           : (dir == Direction::Right);
    } else {
        towardMaster = (config_.orientation == MasterOrientation::Top)
                           ? (dir == Direction::Up)
                           : (dir == Direction::Down);
    }

    if (towardMaster && !inMaster && nm > 0) {
        // Jump to the master that's spatially closest in the perpendicular axis.
        focused_index_ = std::min(focused_index_, nm - 1);
        if (focused_index_ >= nm) focused_index_ = 0;
        else focused_index_ = std::clamp(focused_index_ - nm, 0, nm - 1);
        // Simple: go to first master.
        focused_index_ = 0;
    } else if (!towardMaster && inMaster && !stack_.empty()) {
        focused_index_ = nm;
    } else {
        // Move within the same group.
        int delta = (dir == Direction::Down || dir == Direction::Right) ? 1 : -1;
        int next = focused_index_ + delta;
        if (next >= 0 && next < static_cast<int>(all.size())) {
            focused_index_ = next;
        }
    }
}

// ---------------------------------------------------------------------------
// moveWindow -- swap focused with neighbour
// ---------------------------------------------------------------------------

void MasterLayout::moveWindow(Direction dir) {
    auto all = getWindows();
    if (all.size() < 2) return;

    int old = focused_index_;
    focusDirection(dir);
    if (focused_index_ != old) {
        std::swap(surfaceAt(old), surfaceAt(focused_index_));
        focused_index_ = old;
        recalculate(available_area_);
    }
}

// ---------------------------------------------------------------------------
// resizeWindow -- adjust master ratio with neighbor awareness
// ---------------------------------------------------------------------------

void MasterLayout::resizeWindow(Surface* /*surface*/, SizeDelta delta) {
    bool horizontal = (config_.orientation == MasterOrientation::Left ||
                       config_.orientation == MasterOrientation::Right ||
                       config_.orientation == MasterOrientation::Center);
    int total = horizontal ? available_area_.width : available_area_.height;
    if (total <= 0) return;

    float d = static_cast<float>(horizontal ? delta.dx : delta.dy)
              / static_cast<float>(total);

    // Smart resizing: if focused is in master, increase master ratio;
    // if in stack, decrease it.
    if (config_.smart_resizing && !masters_.empty() && !stack_.empty()) {
        int nm = static_cast<int>(masters_.size());
        if (focused_index_ >= nm) d = -d;  // stack window: reverse
    }

    config_.master_ratio = std::clamp(config_.master_ratio + d, 0.05f, 0.95f);
    recalculate(available_area_);
}

// ---------------------------------------------------------------------------
// Master-specific
// ---------------------------------------------------------------------------

void MasterLayout::swapWithMaster() {
    if (masters_.empty() || stack_.empty()) return;
    int nm = static_cast<int>(masters_.size());
    if (focused_index_ >= nm) {
        int si = focused_index_ - nm;
        std::swap(masters_.front(), stack_[si]);
        focused_index_ = 0;
        recalculate(available_area_);
    }
}

void MasterLayout::addMaster() {
    if (stack_.empty()) return;
    int nm = static_cast<int>(masters_.size());
    // Move the focused stack window (or the first stack window) to masters.
    if (focused_index_ >= nm) {
        int si = focused_index_ - nm;
        masters_.push_back(stack_[si]);
        stack_.erase(stack_.begin() + si);
        config_.master_count = static_cast<int>(masters_.size());
        focused_index_ = static_cast<int>(masters_.size()) - 1;
    } else {
        masters_.push_back(stack_.front());
        stack_.erase(stack_.begin());
        config_.master_count = static_cast<int>(masters_.size());
    }
    recalculate(available_area_);
}

void MasterLayout::removeMaster() {
    if (masters_.size() <= 1) return;
    stack_.insert(stack_.begin(), masters_.back());
    masters_.pop_back();
    config_.master_count = static_cast<int>(masters_.size());
    int total = static_cast<int>(masters_.size() + stack_.size());
    focused_index_ = std::min(focused_index_, total - 1);
    recalculate(available_area_);
}

void MasterLayout::setMasterCount(int count) {
    config_.master_count = std::max(1, count);

    while (static_cast<int>(masters_.size()) < config_.master_count && !stack_.empty()) {
        masters_.push_back(stack_.front());
        stack_.erase(stack_.begin());
    }
    while (static_cast<int>(masters_.size()) > config_.master_count && masters_.size() > 1) {
        stack_.insert(stack_.begin(), masters_.back());
        masters_.pop_back();
    }
    recalculate(available_area_);
}

void MasterLayout::setMasterRatio(float ratio) {
    config_.master_ratio = std::clamp(ratio, 0.05f, 0.95f);
    recalculate(available_area_);
}

void MasterLayout::setOrientation(MasterOrientation orientation) {
    config_.orientation = orientation;
    recalculate(available_area_);
}

void MasterLayout::cycleOrientation(bool forward) {
    constexpr MasterOrientation order[] = {
        MasterOrientation::Left, MasterOrientation::Right,
        MasterOrientation::Top,  MasterOrientation::Bottom,
        MasterOrientation::Center,
    };
    constexpr int count = 5;
    int cur = 0;
    for (int i = 0; i < count; ++i) {
        if (order[i] == config_.orientation) { cur = i; break; }
    }
    cur = forward ? (cur + 1) % count : (cur - 1 + count) % count;
    setOrientation(order[cur]);
}

void MasterLayout::promote() { swapWithMaster(); }

void MasterLayout::toggleCenterMaster() {
    config_.always_center_master = !config_.always_center_master;
    recalculate(available_area_);
}

int               MasterLayout::getMasterCount()   const noexcept { return config_.master_count; }
float             MasterLayout::getMasterRatio()   const noexcept { return config_.master_ratio; }
MasterOrientation MasterLayout::getOrientation()   const noexcept { return config_.orientation; }
bool              MasterLayout::isCenterMaster()   const noexcept { return config_.always_center_master; }

// ---------------------------------------------------------------------------
// Helper: lay out an area with a list of surfaces stacking along the given axis
// ---------------------------------------------------------------------------

namespace {
void layoutSurfaceList(std::vector<Surface*>& surfaces, Box area, int inner,
                       bool stack_vertically) {
    int n = static_cast<int>(surfaces.size());
    if (n == 0) return;

    int totalGap = inner * (n - 1);
    if (stack_vertically) {
        int winH = (area.height - totalGap) / n;
        winH = std::max(1, winH);
        for (int i = 0; i < n; ++i) {
            int y = area.y + i * (winH + inner);
            int h = (i == n - 1) ? (area.y + area.height - y) : winH;
            surfaces[i]->setGeometry(area.x, y, area.width, h);
        }
    } else {
        int winW = (area.width - totalGap) / n;
        winW = std::max(1, winW);
        for (int i = 0; i < n; ++i) {
            int x = area.x + i * (winW + inner);
            int w = (i == n - 1) ? (area.x + area.width - x) : winW;
            surfaces[i]->setGeometry(x, area.y, w, area.height);
        }
    }
}
} // anonymous namespace

// ---------------------------------------------------------------------------
// Layout: center-master mode
// ---------------------------------------------------------------------------

void MasterLayout::layoutCenterMaster(Box usable, int inner) {
    int ns = static_cast<int>(stack_.size());

    if (ns == 0) {
        // Masters fill everything.
        layoutSurfaceList(masters_, usable, inner, true);
        return;
    }

    // Split into three columns: left-stack, center-master, right-stack.
    int masterW = static_cast<int>(usable.width * config_.master_ratio);
    int stackTotalW = usable.width - masterW - 2 * inner;
    int leftW = stackTotalW / 2;
    int rightW = stackTotalW - leftW;

    int masterX = usable.x + leftW + inner;

    Box masterArea{masterX, usable.y, masterW, usable.height};
    layoutSurfaceList(masters_, masterArea, inner, true);

    // Split stack between left and right.
    int leftCount = (ns + 1) / 2;

    std::vector<Surface*> leftStack(stack_.begin(), stack_.begin() + leftCount);
    std::vector<Surface*> rightStack(stack_.begin() + leftCount, stack_.end());

    Box leftArea{usable.x, usable.y, leftW, usable.height};
    Box rightArea{masterX + masterW + inner, usable.y, rightW, usable.height};

    layoutSurfaceList(leftStack, leftArea, inner, true);
    layoutSurfaceList(rightStack, rightArea, inner, true);
}

// ---------------------------------------------------------------------------
// Layout: standard master-stack
// ---------------------------------------------------------------------------

void MasterLayout::layoutMasterStack(Box usable, int inner) {
    int total = static_cast<int>(masters_.size() + stack_.size());

    bool horizontal = (config_.orientation == MasterOrientation::Left ||
                       config_.orientation == MasterOrientation::Right);
    bool masterFirst = (config_.orientation == MasterOrientation::Left ||
                        config_.orientation == MasterOrientation::Top);

    // Single window fills usable area.
    if (total == 1) {
        Surface* s = !masters_.empty() ? masters_.front() : stack_.front();
        s->setGeometry(usable.x, usable.y, usable.width, usable.height);
        return;
    }

    // If no stack windows, masters fill everything.
    if (stack_.empty()) {
        layoutSurfaceList(masters_, usable, inner, horizontal);
        return;
    }

    // Split into master and stack regions.
    Box masterArea{}, stackArea{};

    if (horizontal) {
        int masterW = static_cast<int>(usable.width * config_.master_ratio) - inner / 2;
        int stackW  = usable.width - masterW - inner;

        if (masterFirst) {
            masterArea = {usable.x, usable.y, masterW, usable.height};
            stackArea  = {usable.x + masterW + inner, usable.y, stackW, usable.height};
        } else {
            stackArea  = {usable.x, usable.y, stackW, usable.height};
            masterArea = {usable.x + stackW + inner, usable.y, masterW, usable.height};
        }
    } else {
        int masterH = static_cast<int>(usable.height * config_.master_ratio) - inner / 2;
        int stackH  = usable.height - masterH - inner;

        if (masterFirst) {
            masterArea = {usable.x, usable.y, usable.width, masterH};
            stackArea  = {usable.x, usable.y + masterH + inner, usable.width, stackH};
        } else {
            stackArea  = {usable.x, usable.y, usable.width, stackH};
            masterArea = {usable.x, usable.y + stackH + inner, usable.width, masterH};
        }
    }

    layoutSurfaceList(masters_, masterArea, inner, horizontal);
    layoutSurfaceList(stack_, stackArea, inner, horizontal);
}

// ---------------------------------------------------------------------------
// recalculate
// ---------------------------------------------------------------------------

void MasterLayout::recalculate(Box availableArea) {
    available_area_ = availableArea;
    int total = static_cast<int>(masters_.size() + stack_.size());
    if (total == 0) return;

    bool single = (total == 1) && config_.no_gaps_when_only;
    int outer = single ? 0 : gaps_.outer;
    int inner = single ? 0 : gaps_.inner;

    Box usable{
        availableArea.x + outer,
        availableArea.y + outer,
        availableArea.width  - 2 * outer,
        availableArea.height - 2 * outer,
    };
    if (usable.empty()) return;

    if (config_.always_center_master || config_.orientation == MasterOrientation::Center) {
        layoutCenterMaster(usable, inner);
    } else {
        layoutMasterStack(usable, inner);
    }
}

} // namespace eternal

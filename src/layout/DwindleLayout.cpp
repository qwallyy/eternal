#include "eternal/layout/DwindleLayout.hpp"

#include <algorithm>
#include <cmath>
#include <climits>

namespace eternal {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

DwindleLayout::DwindleLayout() : DwindleLayout(DwindleLayoutConfig{}) {}

DwindleLayout::DwindleLayout(DwindleLayoutConfig config)
    : config_(config) {
    config_.split_ratio = std::clamp(config_.split_ratio, 0.1f, 0.9f);
}

DwindleLayout::~DwindleLayout() = default;

// ---------------------------------------------------------------------------
// ILayout identity
// ---------------------------------------------------------------------------

LayoutType       DwindleLayout::getType() const noexcept { return LayoutType::Dwindle; }
std::string_view DwindleLayout::getName() const noexcept { return "dwindle"; }

// ---------------------------------------------------------------------------
// Gaps
// ---------------------------------------------------------------------------

void DwindleLayout::setGaps(GapConfig gaps) {
    gaps_ = gaps;
    recalculate(available_area_);
}

// ---------------------------------------------------------------------------
// Window helpers
// ---------------------------------------------------------------------------

std::vector<Surface*> DwindleLayout::getWindows() const {
    return window_order_;
}

WindowNode* DwindleLayout::findFocusedLeaf() const {
    if (window_order_.empty() || !root_) return nullptr;
    int idx = std::clamp(focused_index_, 0,
                         static_cast<int>(window_order_.size()) - 1);
    return root_->findNode(window_order_[idx]);
}

SplitDir DwindleLayout::computeSplitDirection(const Box& area, int depth) const {
    if (has_forced_split_) {
        return forced_split_dir_;
    }
    if (config_.smart_split) {
        return (area.width >= area.height) ? SplitDir::Horizontal : SplitDir::Vertical;
    }
    if (config_.force_split) {
        return SplitDir::Horizontal;
    }
    // Alternate based on depth (dwindle pattern).
    return (depth % 2 == 0) ? SplitDir::Horizontal : SplitDir::Vertical;
}

// ---------------------------------------------------------------------------
// addWindow
// ---------------------------------------------------------------------------

void DwindleLayout::addWindow(Surface* surface) {
    if (!surface) return;

    window_order_.push_back(surface);
    focused_index_ = static_cast<int>(window_order_.size()) - 1;

    if (!root_) {
        root_ = makeLeafNode(surface);
        if (config_.pseudo_tile) {
            root_->setPseudoTiled(true);
            root_->setPreferredSize({0, 0, 640, 480});
        }
        recalculate(available_area_);
        return;
    }

    // Find the target leaf for the split. Use the focused window's leaf
    // if use_active_for_splits is set, otherwise the deepest right-most leaf.
    WindowNode* target = nullptr;
    if (config_.use_active_for_splits && focused_index_ > 0) {
        // The previously focused window (before this new one was added).
        int prev = focused_index_ - 1;
        if (prev >= 0 && prev < static_cast<int>(window_order_.size())) {
            target = root_->findNode(window_order_[prev]);
        }
    }
    if (!target) {
        // Fall back to deepest right-most leaf.
        std::vector<WindowNode*> leaves;
        root_->collectLeaves(leaves);
        if (!leaves.empty()) target = leaves.back();
    }
    if (!target) {
        recalculate(available_area_);
        return;
    }

    // Compute depth for alternating split direction.
    int depth = 0;
    for (auto* p = target; p->getParent(); p = p->getParent()) ++depth;

    SplitDir dir = computeSplitDirection(target->getGeometry(), depth);

    // The target leaf becomes a container with two children:
    // the old window and the new window.
    auto old_leaf = makeLeafNode(target->getSurface());
    old_leaf->setPseudoTiled(target->isPseudoTiled());
    old_leaf->setPreferredSize(target->getPreferredSize());

    auto new_leaf = makeLeafNode(surface);
    if (config_.pseudo_tile) {
        new_leaf->setPseudoTiled(true);
        new_leaf->setPreferredSize({0, 0, 640, 480});
    }

    // Convert target from leaf to container.
    target->setSurface(nullptr);
    target->setPseudoTiled(false);

    // Manually change type by reconstructing in place is not possible since
    // type_ is const-like. Instead, we work with the parent.
    // Actually, WindowNode type can be set at construction only.
    // We need to replace the target in its parent with a new container.

    auto container = makeContainerNode(dir, config_.split_ratio);
    container->addChild(std::move(old_leaf));
    container->addChild(std::move(new_leaf));

    if (target == root_.get()) {
        root_ = std::move(container);
    } else {
        auto* parent = target->getParent();
        int idx = target->indexInParent();
        auto old_owned = parent->removeChildAt(idx);
        parent->insertChild(idx, std::move(container));
    }

    has_forced_split_ = false;
    recalculate(available_area_);
}

// ---------------------------------------------------------------------------
// removeWindow
// ---------------------------------------------------------------------------

void DwindleLayout::removeWindow(Surface* surface) {
    if (!surface || !root_) return;

    auto it = std::find(window_order_.begin(), window_order_.end(), surface);
    if (it == window_order_.end()) return;

    int idx = static_cast<int>(it - window_order_.begin());
    window_order_.erase(it);

    if (window_order_.empty()) {
        root_.reset();
        focused_index_ = 0;
        return;
    }

    focused_index_ = std::clamp(idx, 0,
                                static_cast<int>(window_order_.size()) - 1);

    // Find the leaf and collapse its parent.
    WindowNode* leaf = root_->findNode(surface);
    if (!leaf) return;

    if (leaf == root_.get()) {
        // Root is a leaf; only window was already removed from window_order_.
        root_.reset();
        // Rebuild a single-leaf root from the remaining window.
        if (!window_order_.empty()) {
            root_ = makeLeafNode(window_order_[0]);
        }
        recalculate(available_area_);
        return;
    }

    auto* parent = leaf->getParent();
    if (!parent) {
        root_.reset();
        recalculate(available_area_);
        return;
    }

    // Find the sibling.
    int leaf_idx = leaf->indexInParent();
    int sibling_idx = (leaf_idx == 0) ? 1 : 0;
    auto sibling = parent->removeChildAt(sibling_idx > leaf_idx ? sibling_idx - 0 : sibling_idx);
    // Remove the leaf too (actually the parent is about to be replaced).

    if (parent == root_.get()) {
        // Replace root with sibling.
        root_ = std::move(sibling);
        root_->setParent(nullptr);
    } else {
        auto* grandparent = parent->getParent();
        int parent_idx = parent->indexInParent();

        if (config_.preserve_split && sibling) {
            // Preserve split direction of the sibling if it's a container.
        }

        auto old_parent = grandparent->removeChildAt(parent_idx);
        grandparent->insertChild(parent_idx, std::move(sibling));
    }

    recalculate(available_area_);
}

// ---------------------------------------------------------------------------
// Focus navigation
// ---------------------------------------------------------------------------

void DwindleLayout::focusNext() {
    if (window_order_.empty()) return;
    focused_index_ = (focused_index_ + 1) % static_cast<int>(window_order_.size());
}

void DwindleLayout::focusPrev() {
    if (window_order_.empty()) return;
    int n = static_cast<int>(window_order_.size());
    focused_index_ = (focused_index_ - 1 + n) % n;
}

void DwindleLayout::focusDirection(Direction dir) {
    if (window_order_.size() < 2 || !root_) return;

    Surface* focused = window_order_[focused_index_];
    WindowNode* fn = root_->findNode(focused);
    if (!fn) return;

    auto& fa = fn->getGeometry();
    int cx = fa.x + fa.width / 2;
    int cy = fa.y + fa.height / 2;

    Surface* best = nullptr;
    int best_dist = INT_MAX;

    for (int i = 0; i < static_cast<int>(window_order_.size()); ++i) {
        if (i == focused_index_) continue;
        WindowNode* n = root_->findNode(window_order_[i]);
        if (!n) continue;

        auto& na = n->getGeometry();
        int nx = na.x + na.width / 2;
        int ny = na.y + na.height / 2;
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
            if (dist < best_dist) {
                best_dist = dist;
                best = window_order_[i];
            }
        }
    }

    if (best) {
        auto jt = std::find(window_order_.begin(), window_order_.end(), best);
        focused_index_ = static_cast<int>(jt - window_order_.begin());
    }
}

// ---------------------------------------------------------------------------
// moveWindow -- swap focused with neighbour in direction
// ---------------------------------------------------------------------------

void DwindleLayout::moveWindow(Direction dir) {
    if (window_order_.size() < 2) return;
    int old_idx = focused_index_;
    focusDirection(dir);
    if (focused_index_ != old_idx && root_) {
        // Swap in the tree.
        WindowNode* a = root_->findNode(window_order_[old_idx]);
        WindowNode* b = root_->findNode(window_order_[focused_index_]);
        if (a && b) {
            WindowNode::swap(a, b);
        }
        std::swap(window_order_[old_idx], window_order_[focused_index_]);
        focused_index_ = old_idx;
        recalculate(available_area_);
    }
}

// ---------------------------------------------------------------------------
// resizeWindow -- adjust the parent split ratio
// ---------------------------------------------------------------------------

void DwindleLayout::resizeWindow(Surface* surface, SizeDelta delta) {
    if (!root_) return;
    WindowNode* leaf = root_->findNode(surface);
    if (!leaf || !leaf->getParent()) return;

    WindowNode* parent = leaf->getParent();
    if (!parent->isContainer()) return;

    auto& pa = parent->getGeometry();
    bool horiz = (parent->getSplitDir() == SplitDir::Horizontal);
    int size = horiz ? pa.width : pa.height;
    if (size <= 0) return;

    float d = static_cast<float>(horiz ? delta.dx : delta.dy)
              / static_cast<float>(size);

    // If this leaf is the first child, increase ratio; otherwise decrease.
    int leaf_idx = leaf->indexInParent();
    if (leaf_idx == 0) {
        parent->setSplitRatio(std::clamp(parent->getSplitRatio() + d, 0.1f, 0.9f));
    } else {
        parent->setSplitRatio(std::clamp(parent->getSplitRatio() - d, 0.1f, 0.9f));
    }

    recalculate(available_area_);
}

// ---------------------------------------------------------------------------
// recalculate -- recursive geometry computation via WindowNode
// ---------------------------------------------------------------------------

void DwindleLayout::recalculate(Box availableArea) {
    available_area_ = availableArea;
    if (!root_) return;

    bool single = (window_order_.size() == 1) && config_.no_gaps_when_only;
    int outer = single ? 0 : gaps_.outer;

    Box usable{
        availableArea.x + outer,
        availableArea.y + outer,
        availableArea.width  - 2 * outer,
        availableArea.height - 2 * outer,
    };

    if (usable.empty()) return;

    GapConfig effective_gaps = single ? GapConfig{0, 0} : gaps_;
    root_->computeGeometry(usable, effective_gaps);
}

// ---------------------------------------------------------------------------
// Dwindle-specific
// ---------------------------------------------------------------------------

void DwindleLayout::toggleSplit() {
    WindowNode* leaf = findFocusedLeaf();
    if (!leaf || !leaf->getParent()) return;

    auto* parent = leaf->getParent();
    if (!parent->isContainer()) return;

    auto dir = parent->getSplitDir();
    parent->setSplitDir(dir == SplitDir::Horizontal ? SplitDir::Vertical
                                                     : SplitDir::Horizontal);
    recalculate(available_area_);
}

void DwindleLayout::setSplitRatio(float ratio) {
    config_.split_ratio = std::clamp(ratio, 0.1f, 0.9f);
}

float DwindleLayout::getSplitRatio() const noexcept {
    return config_.split_ratio;
}

void DwindleLayout::togglePseudoTile() {
    WindowNode* leaf = findFocusedLeaf();
    if (!leaf || !leaf->isLeaf()) return;

    bool current = leaf->isPseudoTiled();
    leaf->setPseudoTiled(!current);
    if (!current) {
        // Set preferred size to current geometry.
        leaf->setPreferredSize(leaf->getGeometry());
    }
    recalculate(available_area_);
}

void DwindleLayout::swapWithSibling() {
    WindowNode* leaf = findFocusedLeaf();
    if (!leaf || !leaf->getParent()) return;

    auto* parent = leaf->getParent();
    if (parent->childCount() < 2) return;

    int idx = leaf->indexInParent();
    int sibling_idx = (idx == 0) ? 1 : 0;
    WindowNode* sibling = parent->childAt(sibling_idx);
    if (sibling) {
        WindowNode::swap(leaf, sibling);
        recalculate(available_area_);
    }
}

void DwindleLayout::rotateNode() {
    WindowNode* leaf = findFocusedLeaf();
    if (!leaf || !leaf->getParent()) return;

    auto* parent = leaf->getParent();
    if (parent->childCount() < 2) return;

    // Reverse the children order.
    auto child0 = parent->removeChildAt(0);
    parent->addChild(std::move(child0));
    recalculate(available_area_);
}

void DwindleLayout::forceSplitDirection(SplitDir dir) {
    forced_split_dir_ = dir;
    has_forced_split_ = true;
}

} // namespace eternal

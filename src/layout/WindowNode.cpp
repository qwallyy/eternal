#include "eternal/layout/WindowNode.hpp"

#include "eternal/core/Surface.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace eternal {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

WindowNode::WindowNode(NodeType type) : type_(type) {}
WindowNode::~WindowNode() = default;

// ---------------------------------------------------------------------------
// Child access
// ---------------------------------------------------------------------------

WindowNode* WindowNode::childAt(int index) const {
    if (index < 0 || index >= static_cast<int>(children_.size())) return nullptr;
    return children_[index].get();
}

void WindowNode::addChild(std::unique_ptr<WindowNode> child) {
    if (!child) return;
    child->setParent(this);
    children_.push_back(std::move(child));
}

void WindowNode::insertChild(int index, std::unique_ptr<WindowNode> child) {
    if (!child) return;
    child->setParent(this);
    index = std::clamp(index, 0, static_cast<int>(children_.size()));
    children_.insert(children_.begin() + index, std::move(child));
}

std::unique_ptr<WindowNode> WindowNode::removeChild(WindowNode* child) {
    for (auto it = children_.begin(); it != children_.end(); ++it) {
        if (it->get() == child) {
            auto owned = std::move(*it);
            children_.erase(it);
            owned->setParent(nullptr);
            return owned;
        }
    }
    return nullptr;
}

std::unique_ptr<WindowNode> WindowNode::removeChildAt(int index) {
    if (index < 0 || index >= static_cast<int>(children_.size())) return nullptr;
    auto owned = std::move(children_[index]);
    children_.erase(children_.begin() + index);
    owned->setParent(nullptr);
    return owned;
}

// ---------------------------------------------------------------------------
// Container properties
// ---------------------------------------------------------------------------

void WindowNode::setSplitRatio(float ratio) noexcept {
    split_ratio_ = std::clamp(ratio, 0.05f, 0.95f);
}

// ---------------------------------------------------------------------------
// Tab group properties
// ---------------------------------------------------------------------------

void WindowNode::addTabSurface(Surface* s) {
    if (!s) return;
    tab_surfaces_.push_back(s);
}

void WindowNode::removeTabSurface(Surface* s) {
    auto it = std::find(tab_surfaces_.begin(), tab_surfaces_.end(), s);
    if (it != tab_surfaces_.end()) {
        tab_surfaces_.erase(it);
        if (active_tab_ >= static_cast<int>(tab_surfaces_.size()) && !tab_surfaces_.empty()) {
            active_tab_ = static_cast<int>(tab_surfaces_.size()) - 1;
        }
        if (tab_surfaces_.empty()) {
            active_tab_ = 0;
        }
    }
}

void WindowNode::setActiveTab(int index) noexcept {
    if (!tab_surfaces_.empty()) {
        active_tab_ = std::clamp(index, 0, static_cast<int>(tab_surfaces_.size()) - 1);
    }
}

Surface* WindowNode::getActiveTabSurface() const noexcept {
    if (tab_surfaces_.empty()) return nullptr;
    int idx = std::clamp(active_tab_, 0, static_cast<int>(tab_surfaces_.size()) - 1);
    return tab_surfaces_[idx];
}

// ---------------------------------------------------------------------------
// Tree traversal
// ---------------------------------------------------------------------------

WindowNode* WindowNode::findNode(Surface* surface) {
    if (!surface) return nullptr;

    if (isLeaf() && surface_ == surface) return this;

    if (isTabGroup()) {
        for (auto* s : tab_surfaces_) {
            if (s == surface) return this;
        }
    }

    for (auto& child : children_) {
        if (auto* found = child->findNode(surface)) return found;
    }
    return nullptr;
}

const WindowNode* WindowNode::findNode(Surface* surface) const {
    return const_cast<WindowNode*>(this)->findNode(surface);
}

void WindowNode::insertAfter(WindowNode* sibling, std::unique_ptr<WindowNode> node) {
    if (!sibling || !sibling->parent_ || !node) return;
    auto* parent = sibling->parent_;
    int idx = sibling->indexInParent();
    if (idx >= 0) {
        parent->insertChild(idx + 1, std::move(node));
    }
}

void WindowNode::insertBefore(WindowNode* sibling, std::unique_ptr<WindowNode> node) {
    if (!sibling || !sibling->parent_ || !node) return;
    auto* parent = sibling->parent_;
    int idx = sibling->indexInParent();
    if (idx >= 0) {
        parent->insertChild(idx, std::move(node));
    }
}

std::unique_ptr<WindowNode> WindowNode::detach() {
    if (!parent_) return nullptr;
    return parent_->removeChild(this);
}

void WindowNode::swap(WindowNode* a, WindowNode* b) {
    if (!a || !b) return;

    // Swap surfaces for leaf nodes.
    if (a->isLeaf() && b->isLeaf()) {
        std::swap(a->surface_, b->surface_);
        std::swap(a->pseudo_tiled_, b->pseudo_tiled_);
        std::swap(a->preferred_size_, b->preferred_size_);
        return;
    }

    // Swap tab surfaces for tab group nodes.
    if (a->isTabGroup() && b->isTabGroup()) {
        std::swap(a->tab_surfaces_, b->tab_surfaces_);
        std::swap(a->active_tab_, b->active_tab_);
        std::swap(a->tab_locked_, b->tab_locked_);
        return;
    }

    // Mixed swap: exchange geometry only.
    std::swap(a->geometry_, b->geometry_);
}

void WindowNode::collectSurfaces(std::vector<Surface*>& out) const {
    if (isLeaf()) {
        if (surface_) out.push_back(surface_);
        return;
    }
    if (isTabGroup()) {
        for (auto* s : tab_surfaces_) {
            if (s) out.push_back(s);
        }
        return;
    }
    for (auto& child : children_) {
        child->collectSurfaces(out);
    }
}

void WindowNode::collectLeaves(std::vector<WindowNode*>& out) {
    if (isLeaf() || isTabGroup()) {
        out.push_back(this);
        return;
    }
    for (auto& child : children_) {
        child->collectLeaves(out);
    }
}

// ---------------------------------------------------------------------------
// Recursive geometry computation
// ---------------------------------------------------------------------------

void WindowNode::computeGeometry(Box bounds, const GapConfig& gaps) {
    geometry_ = bounds;

    if (isLeaf()) {
        if (surface_) {
            if (pseudo_tiled_) {
                // Center the preferred size within the tile bounds.
                int pw = std::min(preferred_size_.width, bounds.width);
                int ph = std::min(preferred_size_.height, bounds.height);
                int px = bounds.x + (bounds.width - pw) / 2;
                int py = bounds.y + (bounds.height - ph) / 2;
                surface_->setGeometry(px, py, pw, ph);
            } else {
                surface_->setGeometry(bounds.x, bounds.y, bounds.width, bounds.height);
            }
        }
        return;
    }

    if (isTabGroup()) {
        // All tab surfaces share the same geometry; only the active one is shown.
        for (auto* s : tab_surfaces_) {
            if (s) {
                s->setGeometry(bounds.x, bounds.y, bounds.width, bounds.height);
            }
        }
        return;
    }

    // Container node: split between children.
    int n = static_cast<int>(children_.size());
    if (n == 0) return;

    if (n == 1) {
        children_[0]->computeGeometry(bounds, gaps);
        return;
    }

    // Binary split for 2 children using split_ratio.
    if (n == 2) {
        int gap = gaps.inner;
        if (split_dir_ == SplitDir::Horizontal) {
            int left_w = static_cast<int>(bounds.width * split_ratio_) - gap / 2;
            int right_w = bounds.width - left_w - gap;
            Box left_box{bounds.x, bounds.y, left_w, bounds.height};
            Box right_box{bounds.x + left_w + gap, bounds.y, right_w, bounds.height};
            children_[0]->computeGeometry(left_box, gaps);
            children_[1]->computeGeometry(right_box, gaps);
        } else {
            int top_h = static_cast<int>(bounds.height * split_ratio_) - gap / 2;
            int bot_h = bounds.height - top_h - gap;
            Box top_box{bounds.x, bounds.y, bounds.width, top_h};
            Box bot_box{bounds.x, bounds.y + top_h + gap, bounds.width, bot_h};
            children_[0]->computeGeometry(top_box, gaps);
            children_[1]->computeGeometry(bot_box, gaps);
        }
        return;
    }

    // N-ary split: equal distribution.
    int gap = gaps.inner;
    int total_gap = gap * (n - 1);

    if (split_dir_ == SplitDir::Horizontal) {
        int avail = bounds.width - total_gap;
        int each_w = avail / n;
        int cur_x = bounds.x;
        for (int i = 0; i < n; ++i) {
            int w = (i == n - 1) ? (bounds.x + bounds.width - cur_x) : each_w;
            Box child_box{cur_x, bounds.y, w, bounds.height};
            children_[i]->computeGeometry(child_box, gaps);
            cur_x += w + gap;
        }
    } else {
        int avail = bounds.height - total_gap;
        int each_h = avail / n;
        int cur_y = bounds.y;
        for (int i = 0; i < n; ++i) {
            int h = (i == n - 1) ? (bounds.y + bounds.height - cur_y) : each_h;
            Box child_box{bounds.x, cur_y, bounds.width, h};
            children_[i]->computeGeometry(child_box, gaps);
            cur_y += h + gap;
        }
    }
}

// ---------------------------------------------------------------------------
// Index within parent
// ---------------------------------------------------------------------------

int WindowNode::indexInParent() const {
    if (!parent_) return -1;
    for (int i = 0; i < static_cast<int>(parent_->children_.size()); ++i) {
        if (parent_->children_[i].get() == this) return i;
    }
    return -1;
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

NodeState WindowNode::serialize() const {
    NodeState state;
    state.type = type_;
    state.split_dir = split_dir_;
    state.split_ratio = split_ratio_;
    state.geometry = geometry_;

    if (isLeaf() && surface_) {
        // We store a surface identifier. In practice this would be the
        // surface's unique id.  We use the pointer value cast to uint64 as
        // a stand-in; callers should replace this with a stable id.
        state.surface_id = reinterpret_cast<uintptr_t>(surface_);
    }

    if (isTabGroup()) {
        for (auto* s : tab_surfaces_) {
            state.tab_surface_ids.push_back(reinterpret_cast<uintptr_t>(s));
        }
        state.active_tab = active_tab_;
    }

    for (auto& child : children_) {
        state.children.push_back(child->serialize());
    }

    return state;
}

std::unique_ptr<WindowNode> WindowNode::deserialize(
    const NodeState& state,
    const std::function<Surface*(uint64_t)>& lookup)
{
    auto node = std::make_unique<WindowNode>(state.type);
    node->split_dir_ = state.split_dir;
    node->split_ratio_ = state.split_ratio;
    node->geometry_ = state.geometry;

    if (state.type == NodeType::Leaf && lookup) {
        node->surface_ = lookup(state.surface_id);
    }

    if (state.type == NodeType::TabGroup && lookup) {
        for (auto id : state.tab_surface_ids) {
            if (auto* s = lookup(id)) {
                node->tab_surfaces_.push_back(s);
            }
        }
        node->active_tab_ = state.active_tab;
    }

    for (auto& child_state : state.children) {
        auto child = deserialize(child_state, lookup);
        if (child) {
            node->addChild(std::move(child));
        }
    }

    return node;
}

// ---------------------------------------------------------------------------
// Factory helpers
// ---------------------------------------------------------------------------

std::unique_ptr<WindowNode> makeLeafNode(Surface* surface) {
    auto node = std::make_unique<WindowNode>(NodeType::Leaf);
    node->setSurface(surface);
    return node;
}

std::unique_ptr<WindowNode> makeContainerNode(SplitDir dir, float ratio) {
    auto node = std::make_unique<WindowNode>(NodeType::Container);
    node->setSplitDir(dir);
    node->setSplitRatio(ratio);
    return node;
}

std::unique_ptr<WindowNode> makeTabGroupNode() {
    return std::make_unique<WindowNode>(NodeType::TabGroup);
}

} // namespace eternal

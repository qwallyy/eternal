#pragma once

#include "eternal/layout/ILayout.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace eternal {

// ---------------------------------------------------------------------------
// Node type discriminator
// ---------------------------------------------------------------------------

enum class NodeType : uint8_t {
    Container,   // internal split node with children
    Leaf,        // holds a single window (Surface*)
    TabGroup,    // holds multiple windows displayed as tabs
};

// ---------------------------------------------------------------------------
// Split direction for container nodes
// ---------------------------------------------------------------------------

enum class SplitDir : uint8_t {
    Horizontal,  // children arranged left-to-right
    Vertical,    // children arranged top-to-bottom
};

// ---------------------------------------------------------------------------
// Serialized node state (for save/restore across layout switches)
// ---------------------------------------------------------------------------

struct NodeState {
    NodeType type = NodeType::Leaf;
    SplitDir split_dir = SplitDir::Horizontal;
    float split_ratio = 0.5f;
    Box geometry{};
    uint64_t surface_id = 0;              // only for Leaf
    std::vector<uint64_t> tab_surface_ids; // only for TabGroup
    int active_tab = 0;
    std::vector<NodeState> children;      // only for Container
};

// ---------------------------------------------------------------------------
// WindowNode -- abstract tree node for window layout
// ---------------------------------------------------------------------------

class WindowNode {
public:
    explicit WindowNode(NodeType type);
    virtual ~WindowNode();

    WindowNode(const WindowNode&) = delete;
    WindowNode& operator=(const WindowNode&) = delete;

    // ── Identity ────────────────────────────────────────────────────────

    [[nodiscard]] NodeType getType() const noexcept { return type_; }
    [[nodiscard]] bool isLeaf() const noexcept { return type_ == NodeType::Leaf; }
    [[nodiscard]] bool isContainer() const noexcept { return type_ == NodeType::Container; }
    [[nodiscard]] bool isTabGroup() const noexcept { return type_ == NodeType::TabGroup; }

    // ── Tree structure ──────────────────────────────────────────────────

    [[nodiscard]] WindowNode* getParent() const noexcept { return parent_; }
    void setParent(WindowNode* parent) noexcept { parent_ = parent; }

    [[nodiscard]] const std::vector<std::unique_ptr<WindowNode>>& getChildren() const noexcept {
        return children_;
    }
    [[nodiscard]] int childCount() const noexcept {
        return static_cast<int>(children_.size());
    }
    [[nodiscard]] WindowNode* childAt(int index) const;

    /// Add a child at the end.
    void addChild(std::unique_ptr<WindowNode> child);

    /// Insert a child at a specific index.
    void insertChild(int index, std::unique_ptr<WindowNode> child);

    /// Remove and return a child by pointer.
    std::unique_ptr<WindowNode> removeChild(WindowNode* child);

    /// Remove and return a child by index.
    std::unique_ptr<WindowNode> removeChildAt(int index);

    // ── Container properties ────────────────────────────────────────────

    [[nodiscard]] SplitDir getSplitDir() const noexcept { return split_dir_; }
    void setSplitDir(SplitDir dir) noexcept { split_dir_ = dir; }

    [[nodiscard]] float getSplitRatio() const noexcept { return split_ratio_; }
    void setSplitRatio(float ratio) noexcept;

    // ── Leaf properties ─────────────────────────────────────────────────

    [[nodiscard]] Surface* getSurface() const noexcept { return surface_; }
    void setSurface(Surface* s) noexcept { surface_ = s; }

    // ── Tab group properties ────────────────────────────────────────────

    [[nodiscard]] const std::vector<Surface*>& getTabSurfaces() const noexcept {
        return tab_surfaces_;
    }
    void addTabSurface(Surface* s);
    void removeTabSurface(Surface* s);
    [[nodiscard]] int getActiveTab() const noexcept { return active_tab_; }
    void setActiveTab(int index) noexcept;
    [[nodiscard]] Surface* getActiveTabSurface() const noexcept;
    [[nodiscard]] bool isTabLocked() const noexcept { return tab_locked_; }
    void setTabLocked(bool locked) noexcept { tab_locked_ = locked; }

    // ── Geometry ────────────────────────────────────────────────────────

    [[nodiscard]] const Box& getGeometry() const noexcept { return geometry_; }
    void setGeometry(const Box& box) noexcept { geometry_ = box; }

    /// Whether this window uses pseudo-tiling (preferred size within tile).
    [[nodiscard]] bool isPseudoTiled() const noexcept { return pseudo_tiled_; }
    void setPseudoTiled(bool v) noexcept { pseudo_tiled_ = v; }

    [[nodiscard]] Box getPreferredSize() const noexcept { return preferred_size_; }
    void setPreferredSize(const Box& box) noexcept { preferred_size_ = box; }

    // ── Tree traversal ──────────────────────────────────────────────────

    /// Find a node containing the given surface (DFS).
    [[nodiscard]] WindowNode* findNode(Surface* surface);

    /// Find a node containing the given surface (const DFS).
    [[nodiscard]] const WindowNode* findNode(Surface* surface) const;

    /// Insert a new node after the given sibling (in parent's children).
    static void insertAfter(WindowNode* sibling, std::unique_ptr<WindowNode> node);

    /// Insert a new node before the given sibling.
    static void insertBefore(WindowNode* sibling, std::unique_ptr<WindowNode> node);

    /// Remove this node from its parent and return the owning unique_ptr.
    std::unique_ptr<WindowNode> detach();

    /// Swap two leaf nodes (exchange surfaces and geometry).
    static void swap(WindowNode* a, WindowNode* b);

    /// Collect all leaf surfaces in tree order.
    void collectSurfaces(std::vector<Surface*>& out) const;

    /// Collect all leaf nodes in tree order.
    void collectLeaves(std::vector<WindowNode*>& out);

    // ── Recursive geometry computation ──────────────────────────────────

    /// Compute geometry for this node and all children given a bounding box
    /// and gap configuration.
    void computeGeometry(Box bounds, const GapConfig& gaps);

    // ── Serialization ───────────────────────────────────────────────────

    [[nodiscard]] NodeState serialize() const;
    static std::unique_ptr<WindowNode> deserialize(const NodeState& state,
                                                    const std::function<Surface*(uint64_t)>& lookup);

    // ── Index within parent ─────────────────────────────────────────────

    [[nodiscard]] int indexInParent() const;

private:
    NodeType type_;
    WindowNode* parent_ = nullptr;
    std::vector<std::unique_ptr<WindowNode>> children_;

    // Container
    SplitDir split_dir_ = SplitDir::Horizontal;
    float split_ratio_ = 0.5f;

    // Leaf
    Surface* surface_ = nullptr;
    bool pseudo_tiled_ = false;
    Box preferred_size_{};

    // TabGroup
    std::vector<Surface*> tab_surfaces_;
    int active_tab_ = 0;
    bool tab_locked_ = false;

    // Geometry (all node types)
    Box geometry_{};
};

// ---------------------------------------------------------------------------
// Factory helpers
// ---------------------------------------------------------------------------

std::unique_ptr<WindowNode> makeLeafNode(Surface* surface);
std::unique_ptr<WindowNode> makeContainerNode(SplitDir dir, float ratio = 0.5f);
std::unique_ptr<WindowNode> makeTabGroupNode();

} // namespace eternal

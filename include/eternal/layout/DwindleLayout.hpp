#pragma once

#include "eternal/layout/ILayout.hpp"
#include "eternal/layout/WindowNode.hpp"

#include <memory>
#include <vector>

namespace eternal {

// ---------------------------------------------------------------------------
// Dwindle layout configuration
// ---------------------------------------------------------------------------

struct DwindleLayoutConfig {
    bool force_split = false;         // always split right/bottom
    bool preserve_split = true;       // keep split direction on removal
    bool smart_split = false;         // split based on window aspect ratio
    bool smart_resizing = true;       // resize neighbours proportionally
    float split_ratio = 0.5f;        // initial split ratio
    bool no_gaps_when_only = false;   // remove gaps when only one window
    bool use_active_for_splits = true;
    bool pseudo_tile = false;         // enable pseudo-tiling by default
};

// ---------------------------------------------------------------------------
// Binary-tree dwindle layout (using WindowNode)
// ---------------------------------------------------------------------------

class DwindleLayout final : public ILayout {
public:
    DwindleLayout();
    explicit DwindleLayout(DwindleLayoutConfig config);
    ~DwindleLayout() override;

    // -- ILayout interface --------------------------------------------------

    void addWindow(Surface* surface) override;
    void removeWindow(Surface* surface) override;
    void focusNext() override;
    void focusPrev() override;
    void focusDirection(Direction dir) override;
    void moveWindow(Direction dir) override;
    void resizeWindow(Surface* surface, SizeDelta delta) override;
    void recalculate(Box availableArea) override;
    [[nodiscard]] std::vector<Surface*> getWindows() const override;
    void setGaps(GapConfig gaps) override;
    [[nodiscard]] LayoutType getType() const noexcept override;
    [[nodiscard]] std::string_view getName() const noexcept override;

    // -- Dwindle-specific ---------------------------------------------------

    /// Toggle the split direction of the focused node's parent.
    void toggleSplit();

    /// Set the split ratio of the focused node's parent.
    void setSplitRatio(float ratio);

    /// Get the current default split ratio.
    [[nodiscard]] float getSplitRatio() const noexcept;

    /// Toggle pseudo-tiling for the focused window.
    void togglePseudoTile();

    /// Swap the focused window with its sibling.
    void swapWithSibling();

    /// Rotate the tree at the focused node's parent (swap children).
    void rotateNode();

    /// Force the next split direction.
    void forceSplitDirection(SplitDir dir);

    /// Get the root WindowNode (for inspection/serialization).
    [[nodiscard]] WindowNode* getRoot() const noexcept { return root_.get(); }

private:
    WindowNode* findFocusedLeaf() const;
    SplitDir computeSplitDirection(const Box& area, int depth) const;

    std::unique_ptr<WindowNode> root_;
    std::vector<Surface*> window_order_;
    int focused_index_ = 0;
    Box available_area_;
    GapConfig gaps_;
    DwindleLayoutConfig config_;
    SplitDir forced_split_dir_ = SplitDir::Horizontal;
    bool has_forced_split_ = false;
};

} // namespace eternal

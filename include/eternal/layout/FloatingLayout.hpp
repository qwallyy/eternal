#pragma once

#include "eternal/layout/ILayout.hpp"

#include <unordered_map>
#include <vector>

namespace eternal {

// ---------------------------------------------------------------------------
// Floating layout configuration
// ---------------------------------------------------------------------------

enum class PlacementStrategy : uint8_t {
    Cascade,
    Center,
    SmartOverlap,
    MousePosition,
};

struct FloatingLayoutConfig {
    PlacementStrategy placement = PlacementStrategy::SmartOverlap;
    int cascade_offset_x = 30;
    int cascade_offset_y = 30;
    int snap_threshold = 10;            // pixel distance for edge snapping
    int window_snap_threshold = 8;      // pixel distance for window-to-window snap
    bool raise_on_focus = true;
    int min_width  = 50;
    int min_height = 50;
    int max_width  = 0;                 // 0 = no maximum
    int max_height = 0;
};

// ---------------------------------------------------------------------------
// Resize handle hit zones
// ---------------------------------------------------------------------------

enum class ResizeEdge : uint8_t {
    None        = 0,
    Top         = 1 << 0,
    Bottom      = 1 << 1,
    Left        = 1 << 2,
    Right       = 1 << 3,
    TopLeft     = Top | Left,
    TopRight    = Top | Right,
    BottomLeft  = Bottom | Left,
    BottomRight = Bottom | Right,
};

inline ResizeEdge operator|(ResizeEdge a, ResizeEdge b) {
    return static_cast<ResizeEdge>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline bool operator&(ResizeEdge a, ResizeEdge b) {
    return (static_cast<uint8_t>(a) & static_cast<uint8_t>(b)) != 0;
}

// ---------------------------------------------------------------------------
// Per-window floating state
// ---------------------------------------------------------------------------

struct FloatingWindowState {
    Box geometry{};
    Box saved_geometry{};               // pre-fullscreen / pre-maximize
    bool minimized = false;
    int z_order = 0;
    int min_width  = 1;
    int min_height = 1;
    int max_width  = 0;                 // 0 = unlimited
    int max_height = 0;
    float aspect_ratio = 0.0f;          // 0 = no constraint
};

// ---------------------------------------------------------------------------
// Snap guide (visual feedback during interactive move/resize)
// ---------------------------------------------------------------------------

struct SnapGuide {
    enum class Orientation { Horizontal, Vertical };
    Orientation orientation;
    int position = 0;                   // x for vertical, y for horizontal
    int start = 0;                      // extent start
    int end = 0;                        // extent end
};

// ---------------------------------------------------------------------------
// Floating layout -- free-form window placement with snapping
// ---------------------------------------------------------------------------

class FloatingLayout final : public ILayout {
public:
    FloatingLayout();
    explicit FloatingLayout(FloatingLayoutConfig config);
    ~FloatingLayout() override;

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

    // -- Floating-specific --------------------------------------------------

    /// Move a window to an absolute position with edge snapping.
    void moveWindowTo(Surface* surface, int x, int y);

    /// Resize a window to an absolute size with constraint enforcement.
    void resizeWindowTo(Surface* surface, int width, int height);

    /// Raise a window to the top of the Z-order.
    void raiseWindow(Surface* surface);

    /// Lower a window to the bottom of the Z-order.
    void lowerWindow(Surface* surface);

    /// Minimize / unminimize a window.
    void setMinimized(Surface* surface, bool minimized);

    /// Snap a window to the given screen edge.
    void snapWindow(Surface* surface, Direction dir);

    /// Center a window in the available area.
    void centerWindow(Surface* surface);

    /// Tile the focused window to the left/right half.
    void tileHalf(Surface* surface, Direction dir);

    /// Hit-test which resize edge/corner the pointer is over.
    [[nodiscard]] ResizeEdge hitTestResize(Surface* surface, int px, int py,
                                           int border_width = 5) const;

    /// Get snapping guides for the current drag position (for visual feedback).
    [[nodiscard]] std::vector<SnapGuide> computeSnapGuides(Surface* surface) const;

    /// Apply window-to-window snapping to a proposed position.
    void applyWindowSnap(Surface* surface, int& x, int& y) const;

    /// Apply screen-edge snapping to a proposed position.
    void applyEdgeSnap(Surface* surface, int& x, int& y) const;

    /// Get the floating state of a window.
    [[nodiscard]] const FloatingWindowState* getWindowState(Surface* surface) const;

    /// Update min/max size constraints from xdg_toplevel hints.
    void updateSizeConstraints(Surface* surface, int minW, int minH,
                               int maxW, int maxH);

    /// Get windows sorted by z-order (back to front).
    [[nodiscard]] std::vector<Surface*> getWindowsByZOrder() const;

    /// Set the mouse position for MousePosition placement strategy.
    void setMousePosition(int mx, int my) noexcept;

private:
    Box computePlacement(int width, int height) const;
    Box computeCascadePlacement(int width, int height) const;
    Box computeCenterPlacement(int width, int height) const;
    Box computeSmartPlacement(int width, int height) const;
    Box computeMousePlacement(int width, int height) const;

    void enforceSizeConstraints(FloatingWindowState& state) const;
    void sortByZOrder();

    std::vector<Surface*> window_order_;
    std::unordered_map<Surface*, FloatingWindowState> states_;
    int focused_index_ = 0;
    Box available_area_;
    GapConfig gaps_;
    FloatingLayoutConfig config_;
    int next_z_ = 0;
    int mouse_x_ = 0;
    int mouse_y_ = 0;
};

} // namespace eternal

#pragma once

#include "eternal/layout/ILayout.hpp"

#include <optional>
#include <variant>
#include <vector>

namespace eternal {

// ---------------------------------------------------------------------------
// Column width: either a proportion of viewport (0.0-1.0) or fixed pixels.
// ---------------------------------------------------------------------------

struct ProportionWidth {
    float proportion = 0.5f;
};

struct FixedWidth {
    int pixels = 800;
};

using ColumnWidth = std::variant<ProportionWidth, FixedWidth>;

// ---------------------------------------------------------------------------
// Column display mode
// ---------------------------------------------------------------------------

enum class ColumnDisplayMode : uint8_t {
    Normal,
    Tabbed,
};

// ---------------------------------------------------------------------------
// Center-focused-column mode
// ---------------------------------------------------------------------------

enum class CenterFocusMode : uint8_t {
    Never,
    Always,
    OnOverflow,   // only center when the column would be partially off-screen
};

// ---------------------------------------------------------------------------
// A single column in the scrollable layout
// ---------------------------------------------------------------------------

struct Column {
    std::vector<Surface*> windows;
    ColumnWidth width = ProportionWidth{0.5f};
    ColumnDisplayMode display_mode = ColumnDisplayMode::Normal;
    int active_window_index = 0;
};

// ---------------------------------------------------------------------------
// Preset width cycle values
// ---------------------------------------------------------------------------

struct PresetWidths {
    std::vector<ColumnWidth> widths = {
        ProportionWidth{1.0f / 3.0f},
        ProportionWidth{0.5f},
        ProportionWidth{2.0f / 3.0f},
    };
};

// ---------------------------------------------------------------------------
// Spring physics state for smooth viewport scrolling
// ---------------------------------------------------------------------------

struct SpringState {
    float position = 0.0f;
    float velocity = 0.0f;
    float target = 0.0f;
    float stiffness = 180.0f;   // spring constant
    float damping = 24.0f;      // damping coefficient
    float mass = 1.0f;

    /// Advance the spring simulation by dt seconds.
    void update(float dt);

    /// Whether the spring has reached equilibrium.
    [[nodiscard]] bool isSettled(float threshold = 0.5f) const;
};

// ---------------------------------------------------------------------------
// Scrollable layout configuration
// ---------------------------------------------------------------------------

struct ScrollableLayoutConfig {
    PresetWidths preset_widths;
    float scroll_speed = 1.0f;
    CenterFocusMode center_focused = CenterFocusMode::Never;
    int default_column_width_proportion_num = 1;
    int default_column_width_proportion_den = 2;
    bool spring_scrolling = true;
    float spring_stiffness = 180.0f;
    float spring_damping = 24.0f;
    bool never_resize_on_open = true;  // new windows don't resize existing
};

// ---------------------------------------------------------------------------
// Niri-style infinite scrollable layout
// ---------------------------------------------------------------------------

class ScrollableLayout final : public ILayout {
public:
    ScrollableLayout();
    explicit ScrollableLayout(ScrollableLayoutConfig config);
    ~ScrollableLayout() override;

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

    // -- Scrollable-specific ------------------------------------------------

    /// Scroll the viewport left by the given pixel amount.
    void scrollLeft(float amount);

    /// Scroll the viewport right by the given pixel amount.
    void scrollRight(float amount);

    /// Scroll so that the given window is visible.
    void scrollToWindow(Surface* surface);

    /// Center the viewport on the currently focused column.
    void centerFocusedColumn();

    /// Set the width of the focused column.
    void setColumnWidth(ColumnWidth width);

    /// Cycle the focused column through preset widths.
    void cycleColumnWidth(bool forward);

    /// Insert a new empty column to the right of the focused one.
    void addColumn();

    /// Remove the focused column (windows are moved to the adjacent column).
    void removeColumn();

    /// Split the focused column: move the focused window into a new column.
    void splitColumn();

    /// Toggle the focused column between normal and tabbed mode.
    void toggleColumnTabbed();

    /// Get the current viewport X position (animated).
    [[nodiscard]] float getViewportPosition() const noexcept;

    /// Get the target viewport position (before animation settles).
    [[nodiscard]] float getViewportTarget() const noexcept;

    /// Get all columns.
    [[nodiscard]] const std::vector<Column>& getColumns() const noexcept;

    /// Get the index of the focused column.
    [[nodiscard]] int getFocusedColumnIndex() const noexcept;

    /// Advance spring animation by dt seconds.  Returns true if still animating.
    bool tickAnimation(float dt);

    /// Set the center focus mode.
    void setCenterFocusMode(CenterFocusMode mode);

    /// Get snap targets (column left-edge positions) for kinetic scroll.
    [[nodiscard]] std::vector<float> getColumnSnapPositions() const;

    /// Get the total content width of all columns.
    [[nodiscard]] float getTotalContentWidth() const;

    /// Set the viewport position directly (for kinetic scroll integration).
    void setViewportPosition(float pos);

private:
    void ensureFocusValid();
    void updateViewportTarget();
    int computeColumnX(int columnIndex) const;
    int resolveWidth(const ColumnWidth& cw) const;

    std::vector<Column> columns_;
    int focused_column_ = 0;
    SpringState viewport_spring_;
    Box available_area_;
    GapConfig gaps_;
    ScrollableLayoutConfig config_;
};

} // namespace eternal

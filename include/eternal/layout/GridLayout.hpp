#pragma once

#include "eternal/layout/ILayout.hpp"

#include <vector>

namespace eternal {

// ---------------------------------------------------------------------------
// Grid layout configuration
// ---------------------------------------------------------------------------

struct GridLayoutConfig {
    int columns = 0;              // 0 = auto-compute based on window count
    bool auto_resize = true;      // dynamically adjust grid dimensions
    bool keep_aspect_ratio = false;
    float cell_aspect_ratio = 1.0f;
};

// ---------------------------------------------------------------------------
// Grid layout -- arrange windows in an even grid
// ---------------------------------------------------------------------------

class GridLayout final : public ILayout {
public:
    GridLayout();
    explicit GridLayout(GridLayoutConfig config);
    ~GridLayout() override;

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

    // -- Grid-specific ------------------------------------------------------

    /// Set the number of columns (0 = auto).
    void setColumns(int columns);

    /// Get the computed number of columns.
    [[nodiscard]] int getColumns() const noexcept;

    /// Get the computed number of rows.
    [[nodiscard]] int getRows() const noexcept;

private:
    void computeGridDimensions();

    std::vector<Surface*> windows_;
    int focused_index_ = 0;
    int computed_cols_ = 0;
    int computed_rows_ = 0;
    Box available_area_;
    GapConfig gaps_;
    GridLayoutConfig config_;
};

} // namespace eternal

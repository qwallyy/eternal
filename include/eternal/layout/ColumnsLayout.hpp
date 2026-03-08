#pragma once

#include "eternal/layout/ILayout.hpp"

#include <vector>

namespace eternal {

// ---------------------------------------------------------------------------
// Columns layout configuration
// ---------------------------------------------------------------------------

struct ColumnsLayoutConfig {
    int column_count = 3;            // fixed number of columns
    bool equal_width = true;         // all columns the same width
    std::vector<float> ratios;       // per-column width ratios (if !equal_width)
    bool no_gaps_when_only = false;
};

// ---------------------------------------------------------------------------
// Fixed-columns layout -- windows fill columns left to right
// ---------------------------------------------------------------------------

class ColumnsLayout final : public ILayout {
public:
    ColumnsLayout();
    explicit ColumnsLayout(ColumnsLayoutConfig config);
    ~ColumnsLayout() override;

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

    // -- Columns-specific ---------------------------------------------------

    /// Set the number of columns.
    void setColumnCount(int count);

    /// Get the number of columns.
    [[nodiscard]] int getColumnCount() const noexcept;

    /// Set per-column width ratios (must sum to ~1.0).
    void setRatios(std::vector<float> ratios);

    /// Get per-column width ratios.
    [[nodiscard]] const std::vector<float>& getRatios() const noexcept;

private:
    /// Columns stored as a vector of vectors of Surface*.
    std::vector<std::vector<Surface*>> columns_;
    int focused_column_ = 0;
    int focused_row_ = 0;
    Box available_area_;
    GapConfig gaps_;
    ColumnsLayoutConfig config_;
};

} // namespace eternal

#pragma once

#include "eternal/layout/ILayout.hpp"

#include <vector>

namespace eternal {

// ---------------------------------------------------------------------------
// Spiral layout configuration
// ---------------------------------------------------------------------------

enum class SpiralDirection : uint8_t {
    Clockwise,
    CounterClockwise,
};

struct SpiralLayoutConfig {
    float split_ratio = 0.5f;
    SpiralDirection direction = SpiralDirection::Clockwise;
    bool smart_resize = true;
    bool no_gaps_when_only = false;
};

// ---------------------------------------------------------------------------
// Fibonacci spiral layout
// ---------------------------------------------------------------------------

class SpiralLayout final : public ILayout {
public:
    SpiralLayout();
    explicit SpiralLayout(SpiralLayoutConfig config);
    ~SpiralLayout() override;

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

    // -- Spiral-specific ----------------------------------------------------

    /// Set the split ratio.
    void setSplitRatio(float ratio);

    /// Get the current split ratio.
    [[nodiscard]] float getSplitRatio() const noexcept;

    /// Set the spiral direction.
    void setDirection(SpiralDirection direction);

    /// Toggle the spiral direction.
    void toggleDirection();

private:
    std::vector<Surface*> windows_;
    int focused_index_ = 0;
    Box available_area_;
    GapConfig gaps_;
    SpiralLayoutConfig config_;
};

} // namespace eternal

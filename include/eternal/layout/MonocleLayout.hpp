#pragma once

#include "eternal/layout/ILayout.hpp"

#include <vector>

namespace eternal {

// ---------------------------------------------------------------------------
// Monocle layout configuration
// ---------------------------------------------------------------------------

struct MonocleLayoutConfig {
    bool no_gaps = false;            // remove gaps in monocle
    bool no_border = false;          // remove border in monocle
    bool show_count = true;          // show window count indicator
    bool smart_fullscreen = true;    // single window = fullscreen
};

// ---------------------------------------------------------------------------
// Monocle layout -- all windows fullscreen, stacked
// ---------------------------------------------------------------------------

class MonocleLayout final : public ILayout {
public:
    MonocleLayout();
    explicit MonocleLayout(MonocleLayoutConfig config);
    ~MonocleLayout() override;

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

    // -- Monocle-specific ---------------------------------------------------

    /// Get the number of windows.
    [[nodiscard]] int getWindowCount() const noexcept;

    /// Get the index of the focused window.
    [[nodiscard]] int getFocusedIndex() const noexcept;

private:
    std::vector<Surface*> windows_;
    int focused_index_ = 0;
    Box available_area_;
    GapConfig gaps_;
    MonocleLayoutConfig config_;
};

} // namespace eternal

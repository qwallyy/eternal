#pragma once

#include "eternal/layout/ILayout.hpp"

#include <vector>

namespace eternal {

// ---------------------------------------------------------------------------
// Master layout configuration
// ---------------------------------------------------------------------------

enum class MasterOrientation : uint8_t {
    Left,
    Right,
    Top,
    Bottom,
    Center,
};

struct MasterLayoutConfig {
    float master_ratio = 0.55f;
    int master_count = 1;
    MasterOrientation orientation = MasterOrientation::Left;
    bool new_on_top = false;
    bool no_gaps_when_only = false;
    bool smart_resizing = true;
    bool drop_at_cursor = false;
    bool inherit_fullscreen = true;
    bool always_center_master = false;
};

// ---------------------------------------------------------------------------
// Master-stack layout
// ---------------------------------------------------------------------------

class MasterLayout final : public ILayout {
public:
    MasterLayout();
    explicit MasterLayout(MasterLayoutConfig config);
    ~MasterLayout() override;

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

    // -- Master-specific ----------------------------------------------------

    /// Swap the focused window with the master.
    void swapWithMaster();

    /// Add the focused stack window to the master area.
    void addMaster();

    /// Remove the last master window back to the stack.
    void removeMaster();

    /// Increase or decrease the number of master windows.
    void setMasterCount(int count);

    /// Adjust the master ratio.
    void setMasterRatio(float ratio);

    /// Change the orientation.
    void setOrientation(MasterOrientation orientation);

    /// Cycle the orientation.
    void cycleOrientation(bool forward);

    /// Promote the focused stack window to master.
    void promote();

    /// Toggle always_center_master mode.
    void toggleCenterMaster();

    [[nodiscard]] int getMasterCount() const noexcept;
    [[nodiscard]] float getMasterRatio() const noexcept;
    [[nodiscard]] MasterOrientation getOrientation() const noexcept;
    [[nodiscard]] bool isCenterMaster() const noexcept;

private:
    void layoutMasterStack(Box usable, int inner);
    void layoutCenterMaster(Box usable, int inner);
    bool isInMaster(int focused_index) const noexcept;
    Surface*& surfaceAt(int index);

    std::vector<Surface*> masters_;
    std::vector<Surface*> stack_;
    int focused_index_ = 0;
    Box available_area_;
    GapConfig gaps_;
    MasterLayoutConfig config_;
};

} // namespace eternal

#pragma once

#include "eternal/layout/ILayout.hpp"

#include <string>
#include <vector>

namespace eternal {

// ---------------------------------------------------------------------------
// Tab rendering info (for the compositor's tab bar renderer)
// ---------------------------------------------------------------------------

struct TabInfo {
    Surface* surface = nullptr;
    std::string title;
    bool active = false;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

// ---------------------------------------------------------------------------
// Tab group configuration
// ---------------------------------------------------------------------------

struct TabGroupConfig {
    int tab_bar_height = 28;
    int tab_min_width = 80;
    int tab_padding = 4;
    bool show_close_button = true;
};

// ---------------------------------------------------------------------------
// TabGroup -- manages a set of windows displayed as tabs
// ---------------------------------------------------------------------------

class TabGroup {
public:
    explicit TabGroup(int group_id);
    TabGroup(int group_id, TabGroupConfig config);
    ~TabGroup();

    TabGroup(const TabGroup&) = delete;
    TabGroup& operator=(const TabGroup&) = delete;

    // -- Group identity -----------------------------------------------------

    [[nodiscard]] int getGroupId() const noexcept { return group_id_; }

    // -- Window management --------------------------------------------------

    /// Add a window to the tab group.
    void addWindow(Surface* surface);

    /// Insert a window at a specific tab index.
    void insertWindow(int index, Surface* surface);

    /// Remove a window from the tab group.
    void removeWindow(Surface* surface);

    /// Get all windows in tab order.
    [[nodiscard]] const std::vector<Surface*>& getWindows() const noexcept;

    /// Get the number of tabs.
    [[nodiscard]] int tabCount() const noexcept;

    /// Whether the group is empty.
    [[nodiscard]] bool isEmpty() const noexcept;

    // -- Active tab ---------------------------------------------------------

    /// Get the currently active tab index.
    [[nodiscard]] int getActiveIndex() const noexcept;

    /// Get the active tab's surface.
    [[nodiscard]] Surface* getActiveSurface() const noexcept;

    /// Set the active tab by index.
    void setActiveIndex(int index);

    /// Set the active tab by surface.
    void setActive(Surface* surface);

    /// Cycle to the next tab.
    void cycleNext();

    /// Cycle to the previous tab.
    void cyclePrev();

    // -- Tab reordering -----------------------------------------------------

    /// Move a tab from one index to another (drag reordering).
    void moveTab(int from_index, int to_index);

    /// Swap two tabs.
    void swapTabs(int index_a, int index_b);

    // -- Group locking ------------------------------------------------------

    /// Lock the group to prevent additions.
    void lock();

    /// Unlock the group.
    void unlock();

    /// Whether the group is locked.
    [[nodiscard]] bool isLocked() const noexcept;

    // -- Geometry -----------------------------------------------------------

    /// Set the overall bounds of the tab group (including tab bar).
    void setGeometry(const Box& bounds);

    /// Get the overall bounds.
    [[nodiscard]] const Box& getGeometry() const noexcept;

    /// Get the content area (below the tab bar).
    [[nodiscard]] Box getContentArea() const noexcept;

    /// Get the tab bar area.
    [[nodiscard]] Box getTabBarArea() const noexcept;

    /// Compute individual tab rendering info.
    [[nodiscard]] std::vector<TabInfo> computeTabInfos() const;

    /// Hit-test which tab index is at the given position.
    /// Returns -1 if not on a tab.
    [[nodiscard]] int hitTestTab(int px, int py) const;

    // -- Rendering application ----------------------------------------------

    /// Apply geometry to the active surface (call after setGeometry).
    void applyGeometry();

private:
    int group_id_;
    std::vector<Surface*> windows_;
    int active_index_ = 0;
    bool locked_ = false;
    Box geometry_{};
    TabGroupConfig config_;
};

} // namespace eternal

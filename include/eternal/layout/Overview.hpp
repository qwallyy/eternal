#pragma once

#include "eternal/layout/ILayout.hpp"

#include <functional>
#include <string>
#include <vector>

namespace eternal {

class Workspace;
class Output;

// ---------------------------------------------------------------------------
// Overview arrangement mode
// ---------------------------------------------------------------------------

enum class OverviewArrangement : uint8_t {
    Grid,           // miniature windows in a grid
    Strip,          // horizontal strip (matches scrollable layout)
};

// ---------------------------------------------------------------------------
// Overview window thumbnail
// ---------------------------------------------------------------------------

struct OverviewThumbnail {
    Surface* surface = nullptr;
    Box original_geometry{};     // the window's real geometry
    Box thumbnail_geometry{};    // the scaled geometry in overview space
    float scale = 1.0f;
    bool focused = false;
    bool hovered = false;
};

// ---------------------------------------------------------------------------
// Workspace preview strip entry
// ---------------------------------------------------------------------------

struct WorkspacePreview {
    Workspace* workspace = nullptr;
    Box geometry{};              // position in the preview strip
    bool active = false;
    bool hovered = false;
};

// ---------------------------------------------------------------------------
// Overview configuration
// ---------------------------------------------------------------------------

struct OverviewConfig {
    OverviewArrangement arrangement = OverviewArrangement::Grid;
    float zoom_level = 0.15f;           // scale of thumbnails in grid mode
    float strip_zoom = 0.2f;            // scale in strip mode
    int padding = 20;                   // spacing between thumbnails
    float animation_duration = 0.35f;   // enter/exit animation time in seconds
    float workspace_strip_height = 80;  // height of workspace preview strip
    bool show_workspace_strip = true;
    bool show_window_titles = true;
};

// ---------------------------------------------------------------------------
// Overview -- zoom-out view showing all windows in miniature
// ---------------------------------------------------------------------------

class Overview {
public:
    Overview();
    explicit Overview(OverviewConfig config);
    ~Overview();

    Overview(const Overview&) = delete;
    Overview& operator=(const Overview&) = delete;

    // -- Activation ---------------------------------------------------------

    /// Enter overview mode.
    void enter(const std::vector<Surface*>& windows,
               const std::vector<Workspace*>& workspaces,
               Box available_area);

    /// Exit overview mode and return to the selected window.
    void exit();

    /// Toggle overview mode.
    void toggle(const std::vector<Surface*>& windows,
                const std::vector<Workspace*>& workspaces,
                Box available_area);

    /// Whether overview mode is currently active.
    [[nodiscard]] bool isActive() const noexcept;

    // -- Animation ----------------------------------------------------------

    /// Advance the enter/exit animation by dt seconds.
    /// Returns true if still animating.
    bool tickAnimation(float dt);

    /// Get the current zoom progress (0.0 = normal, 1.0 = fully zoomed out).
    [[nodiscard]] float getZoomProgress() const noexcept;

    /// Whether the enter/exit animation is still running.
    [[nodiscard]] bool isAnimating() const noexcept;

    // -- Navigation ---------------------------------------------------------

    /// Move selection to the next window.
    void selectNext();

    /// Move selection to the previous window.
    void selectPrev();

    /// Move selection in a spatial direction.
    void selectDirection(Direction dir);

    /// Get the currently selected (focused) surface.
    [[nodiscard]] Surface* getSelectedSurface() const;

    /// Get the index of the selected window.
    [[nodiscard]] int getSelectedIndex() const noexcept;

    // -- Interaction --------------------------------------------------------

    /// Handle a click at the given position.
    /// Returns the surface that was clicked, or nullptr.
    Surface* handleClick(int px, int py);

    /// Handle a mouse move for hover feedback.
    void handleMouseMove(int px, int py);

    // -- Search / filter ----------------------------------------------------

    /// Set a search filter string.  Windows not matching are hidden.
    void setFilter(const std::string& filter);

    /// Clear the search filter.
    void clearFilter();

    /// Get the current filter string.
    [[nodiscard]] const std::string& getFilter() const noexcept;

    // -- Rendering data -----------------------------------------------------

    /// Get all window thumbnails for rendering.
    [[nodiscard]] const std::vector<OverviewThumbnail>& getThumbnails() const noexcept;

    /// Get workspace preview entries.
    [[nodiscard]] const std::vector<WorkspacePreview>& getWorkspacePreviews() const noexcept;

    /// Recompute thumbnail positions (call after window list or area changes).
    void recalculate();

    // -- Configuration ------------------------------------------------------

    void setConfig(const OverviewConfig& config);
    [[nodiscard]] const OverviewConfig& getConfig() const noexcept;

private:
    void layoutGrid();
    void layoutStrip();
    void layoutWorkspaceStrip();
    bool matchesFilter(Surface* surface) const;

    OverviewConfig config_;
    Box available_area_{};

    bool active_ = false;
    float zoom_progress_ = 0.0f;     // 0 = normal, 1 = overview
    bool entering_ = false;
    bool exiting_ = false;

    std::vector<Surface*> all_windows_;
    std::vector<Surface*> filtered_windows_;
    std::vector<Workspace*> workspaces_;

    std::vector<OverviewThumbnail> thumbnails_;
    std::vector<WorkspacePreview> workspace_previews_;

    int selected_index_ = 0;
    std::string filter_;
};

} // namespace eternal

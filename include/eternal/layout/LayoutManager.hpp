#pragma once

#include "eternal/layout/ILayout.hpp"

#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace eternal {

// ---------------------------------------------------------------------------
// Animated window transition state (for layout switching)
// ---------------------------------------------------------------------------

struct WindowTransition {
    Surface* surface = nullptr;
    Box from{};
    Box to{};
    float progress = 0.0f;   // 0..1
};

// ---------------------------------------------------------------------------
// Per-workspace layout state (for serialization)
// ---------------------------------------------------------------------------

struct WorkspaceLayoutState {
    LayoutType layout_type = LayoutType::Scrollable;
    GapConfig gaps;
    // Opaque serialized state from the layout itself.
    std::vector<uint8_t> serialized_data;
};

// ---------------------------------------------------------------------------
// LayoutManager
// ---------------------------------------------------------------------------

class LayoutManager {
public:
    LayoutManager();
    ~LayoutManager();

    LayoutManager(const LayoutManager&) = delete;
    LayoutManager& operator=(const LayoutManager&) = delete;

    /// Set the active layout for the current workspace.
    void setLayout(LayoutType type);

    /// Get the currently active layout.
    [[nodiscard]] ILayout* getLayout() const noexcept;

    /// Cycle to the next (or previous) layout type.
    void cycleLayout(bool forward);

    /// Get the layout assigned to a specific workspace.
    [[nodiscard]] ILayout* getLayoutForWorkspace(int workspace_id) const;

    /// Override the layout for a specific workspace.
    void setLayoutForWorkspace(int workspace_id, LayoutType type);

    /// Register a layout instance.  The LayoutManager takes ownership.
    void registerLayout(LayoutType type, std::unique_ptr<ILayout> layout);

    // -- Seamless layout transitions (Task 36) ------------------------------

    /// Switch layout with animated geometry transition.
    /// Captures current window positions, switches layout, recalculates,
    /// and sets up interpolation state.
    void switchLayoutAnimated(LayoutType newType, Box availableArea);

    /// Advance transition animations by dt seconds.
    /// Returns true if still animating.
    bool tickTransition(float dt);

    /// Whether a layout transition is currently in progress.
    [[nodiscard]] bool isTransitioning() const noexcept;

    /// Get the current transition states for rendering interpolated geometry.
    [[nodiscard]] const std::vector<WindowTransition>& getTransitions() const noexcept;

    // -- Gap management (Task 38) -------------------------------------------

    /// Set the global gap configuration.
    void setGlobalGaps(GapConfig gaps);

    /// Get the global gap configuration.
    [[nodiscard]] GapConfig getGlobalGaps() const noexcept;

    /// Set a gap override for a specific workspace.
    void setWorkspaceGaps(int workspace_id, GapConfig gaps);

    /// Remove a workspace-specific gap override.
    void clearWorkspaceGaps(int workspace_id);

    /// Get the effective gaps for a workspace (workspace override or global).
    [[nodiscard]] GapConfig getEffectiveGaps(int workspace_id) const;

    /// Set the no_gaps_when_only flag globally.
    void setNoGapsWhenOnly(bool enabled);

    [[nodiscard]] bool getNoGapsWhenOnly() const noexcept;

    // -- State serialization (Task 36) --------------------------------------

    /// Save the layout state for a workspace.
    [[nodiscard]] WorkspaceLayoutState saveWorkspaceState(int workspace_id) const;

    /// Restore layout state for a workspace.
    void restoreWorkspaceState(int workspace_id, const WorkspaceLayoutState& state);

private:
    /// Ensure all built-in layout types are registered.
    void registerDefaults();

    std::unordered_map<LayoutType, std::unique_ptr<ILayout>> layouts_;
    std::unordered_map<int, LayoutType> workspace_layouts_;
    std::unordered_map<int, GapConfig> workspace_gaps_;
    LayoutType active_type_ = LayoutType::Scrollable;
    GapConfig global_gaps_{5, 10};
    bool no_gaps_when_only_ = false;

    // Transition state.
    std::vector<WindowTransition> transitions_;
    float transition_duration_ = 0.3f;  // seconds
    float transition_elapsed_ = 0.0f;
    bool transitioning_ = false;
};

} // namespace eternal

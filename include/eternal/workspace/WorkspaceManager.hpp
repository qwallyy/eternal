#pragma once

#include <eternal/workspace/Workspace.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace eternal {

class Server;
struct Surface;
struct Output;

// ---------------------------------------------------------------------------
// Workspace-to-monitor binding config (Task 84)
// ---------------------------------------------------------------------------

struct WorkspaceBinding {
    WorkspaceID workspaceId = 0;
    std::string workspaceName;
    std::string outputName;       // connector name (e.g. "DP-1")
    bool persistent = false;      // keep workspace even if empty
};

// ---------------------------------------------------------------------------
// Spring state for workspace switch animation (Task 39)
// ---------------------------------------------------------------------------

struct WorkspaceSpring {
    float position = 0.0f;
    float velocity = 0.0f;
    float target = 0.0f;
    float stiffness = 200.0f;
    float damping = 26.0f;

    void update(float dt);
    [[nodiscard]] bool isSettled(float threshold = 0.5f) const;
};

// ---------------------------------------------------------------------------
// WorkspaceManager
// ---------------------------------------------------------------------------

class WorkspaceManager {
public:
    explicit WorkspaceManager(Server& server);
    ~WorkspaceManager();

    // -- Workspace lifecycle ------------------------------------------------

    /// Create a new workspace.
    WorkspaceID createWorkspace(const std::string& name, Output* output);

    /// Create a named persistent workspace.
    WorkspaceID createNamedWorkspace(const std::string& name, Output* output);

    /// Delete a workspace by id.  Windows are moved to the nearest workspace.
    void deleteWorkspace(WorkspaceID id);

    // -- Workspace switching ------------------------------------------------

    /// Switch the active workspace on the relevant output.
    void switchTo(WorkspaceID id);
    void switchToNext();
    void switchToPrev();

    /// Switch to workspace by number (1-based).
    void switchToNumber(int number);

    /// Switch to workspace by name.
    void switchToName(const std::string& name);

    // -- Window management --------------------------------------------------

    /// Move a window to a different workspace.
    void moveWindowTo(Surface* surface, WorkspaceID wsId);

    /// Move a window to the next workspace.
    void moveWindowToNext(Surface* surface);

    /// Move a window to the previous workspace.
    void moveWindowToPrev(Surface* surface);

    // -- Queries ------------------------------------------------------------

    /// Get the currently active workspace on an output.
    [[nodiscard]] Workspace* getActiveWorkspace(Output* output) const;

    /// Get all workspaces on an output (in vertical stack order).
    [[nodiscard]] std::vector<Workspace*> getWorkspaces(Output* output) const;

    /// Get all workspaces across all outputs.
    [[nodiscard]] std::vector<Workspace*> getAllWorkspaces() const;

    /// Get or toggle the special (scratchpad) workspace.
    [[nodiscard]] Workspace* getSpecialWorkspace() const;
    void toggleSpecialWorkspace();

    /// Swap two workspaces.
    void swapWorkspaces(WorkspaceID a, WorkspaceID b);

    /// Move a workspace to a different output.
    void moveWorkspaceToOutput(WorkspaceID wsId, Output* output);

    /// Total number of workspaces.
    [[nodiscard]] std::size_t getWorkspaceCount() const;

    /// Find a workspace by name.
    [[nodiscard]] Workspace* findWorkspace(const std::string& name) const;

    /// Find a workspace by id.
    [[nodiscard]] Workspace* getWorkspace(WorkspaceID id) const;

    // -- Dynamic workspace management (Niri-style, Task 39) -----------------

    /// Ensure there is always one empty workspace at the bottom of each
    /// output's stack.  Called after window add/remove.
    void ensureEmptyTrailing(Output* output);

    /// Remove empty non-persistent workspaces (except the trailing empty one).
    void pruneEmptyWorkspaces(Output* output);

    /// Re-number workspaces on an output sequentially (1, 2, 3...).
    void renumberWorkspaces(Output* output);

    // -- Animation (Task 39) ------------------------------------------------

    /// Advance workspace switch animation by dt seconds.
    bool tickAnimation(float dt);

    /// Whether a workspace switch animation is in progress.
    [[nodiscard]] bool isAnimating() const noexcept;

    /// Get the current vertical scroll offset for an output.
    [[nodiscard]] float getScrollOffset(Output* output) const;

    /// Get the animation spring for an output.
    [[nodiscard]] const WorkspaceSpring* getSpring(Output* output) const;

    // -- Monitor assignment (Task 84) ---------------------------------------

    void addBinding(const WorkspaceBinding& binding);
    void applyBindings();
    void bindToOutput(WorkspaceID wsId, const std::string& outputName);
    void moveWorkspaceToOutputByName(WorkspaceID wsId,
                                     const std::string& outputName);
    void handleOutputDisconnected(Output* output);
    void handleOutputConnected(Output* output);

    [[nodiscard]] const std::vector<WorkspaceBinding>& getBindings() const {
        return m_bindings;
    }

private:
    /// Find the workspace containing a given surface.
    Workspace* findWorkspaceForSurface(Surface* surface) const;

    /// Get the active workspace index in the output's stack.
    int getActiveIndex(Output* output) const;

    /// Find a fallback output when the bound output is unavailable.
    Output* findFallbackOutput(Output* exclude = nullptr) const;

    Server& m_server;
    std::vector<std::unique_ptr<Workspace>> m_workspaces;
    WorkspaceID m_nextId = 1;
    WorkspaceID m_specialId = -1;

    // Workspace-to-monitor bindings (Task 84).
    std::vector<WorkspaceBinding> m_bindings;
    std::unordered_map<WorkspaceID, std::string> m_boundOutput;

    // Per-output animation state (Task 39).
    struct OutputAnimState {
        Output* output = nullptr;
        WorkspaceSpring spring;
    };
    std::vector<OutputAnimState> m_animStates;

    OutputAnimState* getAnimState(Output* output);
    const OutputAnimState* getAnimState(Output* output) const;
    void ensureAnimState(Output* output);
};

} // namespace eternal

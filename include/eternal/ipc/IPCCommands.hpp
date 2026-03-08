#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace eternal {

class Server;

// ---------------------------------------------------------------------------
// Dispatcher enumeration
// ---------------------------------------------------------------------------

enum class Dispatcher {
    Exec,
    KillActive,
    CloseWindow,
    MoveWindow,
    ResizeWindow,
    Fullscreen,
    Maximize,
    ToggleFloating,
    Pin,
    FocusWindow,
    SwapWindow,
    CenterWindow,
    SetOpacity,
    SetTiled,
    Workspace,
    MoveToWorkspace,
    MoveToWorkspaceSilent,
    ToggleSpecialWorkspace,
    SwapWorkspaces,
    RenameWorkspace,
    FocusMonitor,
    MoveCurrentWorkspaceToMonitor,
    SwapActiveWorkspaces,
    DPMS,
    ToggleSplit,
    SwapWithMaster,
    AddMaster,
    RemoveMaster,
    CycleLayout,
    SwitchLayout,
    ScrollLeft,
    ScrollRight,
    ScrollUp,
    ScrollDown,
    CenterColumn,
    SetColumnWidth,
    ScrollColumn,
    ScrollToWindow,
    ToggleGroup,
    ChangeGroupActive,
    LockActiveGroup,
    MoveWindowIntoGroup,
    MoveWindowOutOfGroup,
    LockGroups,
    ToggleOverview,
    ZoomIn,
    ZoomOut,
    Screenshot,
    ScreenRecord,
    Exit,
    Reload,
    ForceIdle,
    SetCursor,
    SwitchXKBLayout,
    ExecOnce,
    Submap,
    ForceRendererReload,
    MoveWindowPixel,
    ResizeWindowPixel,
    SwapNext,
    SwapPrev,
    FocusUrgentOrLast,
    FocusCurrentOrLast,
    Global,
    Pass,
    SendShortcut,
    TagWindow,
    SetFloating,
    SetTiling,
    Pseudo,
    BringActiveToTop,
    AlterZOrder,
    FocusMaster,
    MoveFocus,
    MoveCursor,
    SplitRatio,
    FocusWorkspaceOnCurrentMonitor,
    ChangeMonitor,
    LockScreen,
    DenyWindowFromGroup,
    SetMonitor,
    // Task 68, 69: additional dispatchers
    WorkspaceNext,
    WorkspacePrev,
    MoveWindowDirection,     // movewindow l/r/u/d
};

struct DispatcherCommand {
    Dispatcher type;
    std::string args;
};

// ---------------------------------------------------------------------------
// Custom dispatcher registration -- for plugin use (Task 76)
// ---------------------------------------------------------------------------

using CustomDispatcherFn = std::function<std::string(const std::string& args)>;

/// Register a custom dispatcher by name. Returns true on success.
bool registerCustomDispatcher(const std::string& name, CustomDispatcherFn fn);

/// Unregister a custom dispatcher.
bool unregisterCustomDispatcher(const std::string& name);

/// Check if a custom dispatcher exists.
bool hasCustomDispatcher(const std::string& name);

// ---------------------------------------------------------------------------
// Dispatcher API
// ---------------------------------------------------------------------------

/// Execute a dispatcher command and return a result string.
std::string executeDispatcher(Server& server, Dispatcher dispatcher,
                              const std::string& args);

/// Parse a dispatcher name string into the enum.
Dispatcher parseDispatcher(const std::string& name);

/// Get the string name of a dispatcher.
std::string getDispatcherName(Dispatcher dispatcher);

/// List all available dispatcher names.
std::vector<std::string> listDispatchers();

} // namespace eternal

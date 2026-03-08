#include "eternal/ipc/IPCCommands.hpp"
#include "eternal/utils/Logger.hpp"

#include <algorithm>
#include <mutex>
#include <unordered_map>

namespace eternal {

// ===========================================================================
// Custom dispatcher registry -- Task 76
// ===========================================================================

namespace {

struct CustomDispatcherRegistry {
    std::mutex mutex;
    std::unordered_map<std::string, CustomDispatcherFn> dispatchers;
};

CustomDispatcherRegistry& getCustomRegistry() {
    static CustomDispatcherRegistry registry;
    return registry;
}

} // anonymous namespace

bool registerCustomDispatcher(const std::string& name, CustomDispatcherFn fn) {
    auto& reg = getCustomRegistry();
    std::lock_guard lock(reg.mutex);
    if (reg.dispatchers.contains(name)) {
        LOG_WARN("Custom dispatcher '{}' already registered", name);
        return false;
    }
    reg.dispatchers[name] = std::move(fn);
    LOG_INFO("Registered custom dispatcher: {}", name);
    return true;
}

bool unregisterCustomDispatcher(const std::string& name) {
    auto& reg = getCustomRegistry();
    std::lock_guard lock(reg.mutex);
    return reg.dispatchers.erase(name) > 0;
}

bool hasCustomDispatcher(const std::string& name) {
    auto& reg = getCustomRegistry();
    std::lock_guard lock(reg.mutex);
    return reg.dispatchers.contains(name);
}

// ===========================================================================
// Dispatcher table
// ===========================================================================

namespace {

struct DispatcherEntry {
    Dispatcher dispatcher;
    const char* name;
};

constexpr DispatcherEntry kDispatcherTable[] = {
    {Dispatcher::Exec,                          "exec"},
    {Dispatcher::ExecOnce,                      "exec_once"},
    {Dispatcher::KillActive,                    "killactive"},
    {Dispatcher::CloseWindow,                   "closewindow"},
    {Dispatcher::MoveWindow,                    "movewindow"},
    {Dispatcher::ResizeWindow,                  "resizewindow"},
    {Dispatcher::Fullscreen,                    "fullscreen"},
    {Dispatcher::Maximize,                      "maximize"},
    {Dispatcher::ToggleFloating,                "togglefloating"},
    {Dispatcher::Pin,                           "pin"},
    {Dispatcher::FocusWindow,                   "focuswindow"},
    {Dispatcher::SwapWindow,                    "swapwindow"},
    {Dispatcher::CenterWindow,                  "centerwindow"},
    {Dispatcher::SetOpacity,                    "setopacity"},
    {Dispatcher::SetTiled,                      "settiled"},
    {Dispatcher::Workspace,                     "workspace"},
    {Dispatcher::MoveToWorkspace,               "movetoworkspace"},
    {Dispatcher::MoveToWorkspaceSilent,         "movetoworkspacesilent"},
    {Dispatcher::ToggleSpecialWorkspace,        "togglespecialworkspace"},
    {Dispatcher::SwapWorkspaces,                "swapworkspaces"},
    {Dispatcher::RenameWorkspace,               "renameworkspace"},
    {Dispatcher::FocusMonitor,                  "focusmonitor"},
    {Dispatcher::MoveCurrentWorkspaceToMonitor, "movecurrentworkspacetomonitor"},
    {Dispatcher::SwapActiveWorkspaces,          "swapactiveworkspaces"},
    {Dispatcher::DPMS,                          "dpms"},
    {Dispatcher::SetMonitor,                    "setmonitor"},
    {Dispatcher::ToggleSplit,                   "togglesplit"},
    {Dispatcher::SwapWithMaster,                "swapwithmaster"},
    {Dispatcher::AddMaster,                     "addmaster"},
    {Dispatcher::RemoveMaster,                  "removemaster"},
    {Dispatcher::CycleLayout,                   "cyclelayout"},
    {Dispatcher::SwitchLayout,                  "switchlayout"},
    {Dispatcher::ScrollLeft,                    "scrollleft"},
    {Dispatcher::ScrollRight,                   "scrollright"},
    {Dispatcher::ScrollUp,                      "scrollup"},
    {Dispatcher::ScrollDown,                    "scrolldown"},
    {Dispatcher::CenterColumn,                  "centercolumn"},
    {Dispatcher::SetColumnWidth,                "setcolumnwidth"},
    {Dispatcher::ScrollColumn,                  "scrollcolumn"},
    {Dispatcher::ScrollToWindow,                "scrolltowindow"},
    {Dispatcher::ToggleGroup,                   "togglegroup"},
    {Dispatcher::ChangeGroupActive,             "changegroupactive"},
    {Dispatcher::LockActiveGroup,               "lockactivegroup"},
    {Dispatcher::MoveWindowIntoGroup,           "movewindowintogroup"},
    {Dispatcher::MoveWindowOutOfGroup,          "movewindowoutofgroup"},
    {Dispatcher::LockGroups,                    "lockgroups"},
    {Dispatcher::DenyWindowFromGroup,           "denywindowfromgroup"},
    {Dispatcher::ToggleOverview,                "toggleoverview"},
    {Dispatcher::ZoomIn,                        "zoomin"},
    {Dispatcher::ZoomOut,                       "zoomout"},
    {Dispatcher::Screenshot,                    "screenshot"},
    {Dispatcher::ScreenRecord,                  "screenrecord"},
    {Dispatcher::Exit,                          "exit"},
    {Dispatcher::Reload,                        "reload"},
    {Dispatcher::ForceIdle,                     "forceidle"},
    {Dispatcher::SetCursor,                     "setcursor"},
    {Dispatcher::SwitchXKBLayout,               "switchxkblayout"},
    {Dispatcher::Submap,                        "submap"},
    {Dispatcher::ForceRendererReload,           "forcerendererreload"},
    {Dispatcher::MoveWindowPixel,               "movewindowpixel"},
    {Dispatcher::ResizeWindowPixel,             "resizewindowpixel"},
    {Dispatcher::SwapNext,                      "swapnext"},
    {Dispatcher::SwapPrev,                      "swapprev"},
    {Dispatcher::FocusUrgentOrLast,             "focusurgentorlast"},
    {Dispatcher::FocusCurrentOrLast,            "focuscurrentorlast"},
    {Dispatcher::Global,                        "global_shortcut"},
    {Dispatcher::Pass,                          "pass"},
    {Dispatcher::SendShortcut,                  "sendshortcut"},
    {Dispatcher::TagWindow,                     "tagwindow"},
    {Dispatcher::BringActiveToTop,              "bringactivetotop"},
    {Dispatcher::AlterZOrder,                   "alterzorder"},
    {Dispatcher::FocusMaster,                   "focusmaster"},
    {Dispatcher::Pseudo,                        "pseudo"},
    {Dispatcher::MoveFocus,                     "movefocus"},
    {Dispatcher::MoveCursor,                    "movecursor"},
    {Dispatcher::SetFloating,                   "setfloating"},
    {Dispatcher::SetTiling,                     "settiling"},
    {Dispatcher::SplitRatio,                    "splitratio"},
    {Dispatcher::FocusWorkspaceOnCurrentMonitor,"focusworkspaceoncurrentmonitor"},
    {Dispatcher::ChangeMonitor,                 "changemonitor"},
    {Dispatcher::LockScreen,                    "lockscreen"},
    {Dispatcher::WorkspaceNext,                 "workspacenext"},
    {Dispatcher::WorkspacePrev,                 "workspaceprev"},
    {Dispatcher::MoveWindowDirection,           "movewindowdirection"},
};

const std::unordered_map<std::string, Dispatcher>& nameToDispatcher() {
    static const auto* map = [] {
        auto* m = new std::unordered_map<std::string, Dispatcher>();
        for (const auto& entry : kDispatcherTable)
            m->emplace(entry.name, entry.dispatcher);
        return m;
    }();
    return *map;
}

const std::unordered_map<Dispatcher, std::string>& dispatcherToName() {
    static const auto* map = [] {
        auto* m = new std::unordered_map<Dispatcher, std::string>();
        for (const auto& entry : kDispatcherTable)
            m->emplace(entry.dispatcher, entry.name);
        return m;
    }();
    return *map;
}

std::string jsonOk(const std::string& dispatcher, const std::string& args) {
    return R"({"ok":true,"dispatcher":")" + dispatcher +
           R"(","args":")" + args + R"("})";
}

std::string jsonError(const std::string& dispatcher, const std::string& reason) {
    return R"({"ok":false,"dispatcher":")" + dispatcher +
           R"(","error":")" + reason + R"("})";
}

} // anonymous namespace

// ===========================================================================
// executeDispatcher -- Task 68, 69
// ===========================================================================

std::string executeDispatcher(Server& /*server*/, Dispatcher dispatcher,
                              const std::string& args) {
    const std::string name = getDispatcherName(dispatcher);

    switch (dispatcher) {
    // -- Process execution ---------------------------------------------------
    case Dispatcher::Exec:
        LOG_INFO("ipc: exec '{}'", args);
        if (args.empty()) return jsonError(name, "no command specified");
        return jsonOk(name, args);

    case Dispatcher::ExecOnce:
        LOG_INFO("ipc: exec_once '{}'", args);
        if (args.empty()) return jsonError(name, "no command specified");
        return jsonOk(name, args);

    // -- Window lifecycle ----------------------------------------------------
    case Dispatcher::KillActive:
        LOG_INFO("ipc: killactive");
        return jsonOk(name, args);

    case Dispatcher::CloseWindow:
        LOG_INFO("ipc: closewindow '{}'", args);
        return jsonOk(name, args);

    // -- Window movement & sizing -- Task 69 ---------------------------------
    case Dispatcher::MoveWindow:
        LOG_INFO("ipc: movewindow '{}'", args);
        if (args.empty()) return jsonError(name, "direction required (l/r/u/d)");
        return jsonOk(name, args);

    case Dispatcher::MoveWindowDirection:
        LOG_INFO("ipc: movewindowdirection '{}'", args);
        if (args.empty()) return jsonError(name, "direction required (l/r/u/d)");
        // Validate direction
        if (args != "l" && args != "r" && args != "u" && args != "d" &&
            args != "left" && args != "right" && args != "up" && args != "down") {
            return jsonError(name, "invalid direction, use l/r/u/d or left/right/up/down");
        }
        return jsonOk(name, args);

    case Dispatcher::ResizeWindow:
        LOG_INFO("ipc: resizewindow '{}'", args);
        if (args.empty()) return jsonError(name, "resize amount required (dx dy)");
        return jsonOk(name, args);

    case Dispatcher::MoveWindowPixel:
        LOG_INFO("ipc: movewindowpixel '{}'", args);
        if (args.empty()) return jsonError(name, "pixel offset required (x y)");
        return jsonOk(name, args);

    case Dispatcher::ResizeWindowPixel:
        LOG_INFO("ipc: resizewindowpixel '{}'", args);
        if (args.empty()) return jsonError(name, "pixel size required (x y)");
        return jsonOk(name, args);

    case Dispatcher::CenterWindow:
        LOG_INFO("ipc: centerwindow");
        return jsonOk(name, args);

    // -- Window state --------------------------------------------------------
    case Dispatcher::Fullscreen:
        LOG_INFO("ipc: fullscreen '{}'", args);
        return jsonOk(name, args);

    case Dispatcher::Maximize:
        LOG_INFO("ipc: maximize");
        return jsonOk(name, args);

    case Dispatcher::ToggleFloating:
        LOG_INFO("ipc: togglefloating");
        return jsonOk(name, args);

    case Dispatcher::SetFloating:
        LOG_INFO("ipc: setfloating '{}'", args);
        return jsonOk(name, args);

    case Dispatcher::SetTiling:
        LOG_INFO("ipc: settiling '{}'", args);
        return jsonOk(name, args);

    case Dispatcher::Pin:
        LOG_INFO("ipc: pin");
        return jsonOk(name, args);

    case Dispatcher::SetOpacity:
        LOG_INFO("ipc: setopacity '{}'", args);
        if (args.empty()) return jsonError(name, "opacity value required");
        return jsonOk(name, args);

    case Dispatcher::SetTiled:
        LOG_INFO("ipc: settiled");
        return jsonOk(name, args);

    case Dispatcher::Pseudo:
        LOG_INFO("ipc: pseudo");
        return jsonOk(name, args);

    // -- Focus ---------------------------------------------------------------
    case Dispatcher::FocusWindow:
        LOG_INFO("ipc: focuswindow '{}'", args);
        return jsonOk(name, args);

    case Dispatcher::MoveFocus:
        LOG_INFO("ipc: movefocus '{}'", args);
        if (args.empty()) return jsonError(name, "direction required");
        return jsonOk(name, args);

    case Dispatcher::MoveCursor:
        LOG_INFO("ipc: movecursor '{}'", args);
        if (args.empty()) return jsonError(name, "coordinates required");
        return jsonOk(name, args);

    case Dispatcher::FocusMaster:
        LOG_INFO("ipc: focusmaster '{}'", args);
        return jsonOk(name, args);

    case Dispatcher::FocusUrgentOrLast:
        LOG_INFO("ipc: focusurgentorlast");
        return jsonOk(name, args);

    case Dispatcher::FocusCurrentOrLast:
        LOG_INFO("ipc: focuscurrentorlast");
        return jsonOk(name, args);

    // -- Swap ----------------------------------------------------------------
    case Dispatcher::SwapWindow:
        LOG_INFO("ipc: swapwindow '{}'", args);
        return jsonOk(name, args);

    case Dispatcher::SwapNext:
        LOG_INFO("ipc: swapnext");
        return jsonOk(name, args);

    case Dispatcher::SwapPrev:
        LOG_INFO("ipc: swapprev");
        return jsonOk(name, args);

    case Dispatcher::SwapWithMaster:
        LOG_INFO("ipc: swapwithmaster '{}'", args);
        return jsonOk(name, args);

    // -- Workspace -- Task 68 ------------------------------------------------
    case Dispatcher::Workspace:
        LOG_INFO("ipc: workspace '{}'", args);
        if (args.empty()) return jsonError(name, "workspace identifier required");
        // Support: workspace N, workspace next, workspace prev, workspace name:xxx
        return jsonOk(name, args);

    case Dispatcher::WorkspaceNext:
        LOG_INFO("ipc: workspace next");
        return jsonOk(name, "next");

    case Dispatcher::WorkspacePrev:
        LOG_INFO("ipc: workspace prev");
        return jsonOk(name, "prev");

    case Dispatcher::MoveToWorkspace:
        LOG_INFO("ipc: movetoworkspace '{}'", args);
        if (args.empty()) return jsonError(name, "workspace identifier required");
        return jsonOk(name, args);

    case Dispatcher::MoveToWorkspaceSilent:
        LOG_INFO("ipc: movetoworkspacesilent '{}'", args);
        if (args.empty()) return jsonError(name, "workspace identifier required");
        return jsonOk(name, args);

    case Dispatcher::ToggleSpecialWorkspace:
        LOG_INFO("ipc: togglespecialworkspace '{}'", args);
        return jsonOk(name, args);

    case Dispatcher::SwapWorkspaces:
        LOG_INFO("ipc: swapworkspaces '{}'", args);
        if (args.empty()) return jsonError(name, "workspace pair required");
        return jsonOk(name, args);

    case Dispatcher::RenameWorkspace:
        LOG_INFO("ipc: renameworkspace '{}'", args);
        if (args.empty()) return jsonError(name, "workspace id and name required");
        return jsonOk(name, args);

    case Dispatcher::FocusWorkspaceOnCurrentMonitor:
        LOG_INFO("ipc: focusworkspaceoncurrentmonitor '{}'", args);
        if (args.empty()) return jsonError(name, "workspace identifier required");
        return jsonOk(name, args);

    // -- Monitor -------------------------------------------------------------
    case Dispatcher::FocusMonitor:
        LOG_INFO("ipc: focusmonitor '{}'", args);
        if (args.empty()) return jsonError(name, "monitor identifier required");
        return jsonOk(name, args);

    case Dispatcher::MoveCurrentWorkspaceToMonitor:
        LOG_INFO("ipc: movecurrentworkspacetomonitor '{}'", args);
        if (args.empty()) return jsonError(name, "monitor identifier required");
        return jsonOk(name, args);

    case Dispatcher::SwapActiveWorkspaces:
        LOG_INFO("ipc: swapactiveworkspaces '{}'", args);
        if (args.empty()) return jsonError(name, "monitor pair required");
        return jsonOk(name, args);

    case Dispatcher::ChangeMonitor:
        LOG_INFO("ipc: changemonitor '{}'", args);
        return jsonOk(name, args);

    case Dispatcher::DPMS:
        LOG_INFO("ipc: dpms '{}'", args);
        return jsonOk(name, args);

    case Dispatcher::SetMonitor:
        LOG_INFO("ipc: setmonitor '{}'", args);
        if (args.empty()) return jsonError(name, "monitor config required");
        return jsonOk(name, args);

    // -- Layout -- Task 68 ---------------------------------------------------
    case Dispatcher::ToggleSplit:
        LOG_INFO("ipc: togglesplit");
        return jsonOk(name, args);

    case Dispatcher::SplitRatio:
        LOG_INFO("ipc: splitratio '{}'", args);
        if (args.empty()) return jsonError(name, "ratio value required");
        return jsonOk(name, args);

    case Dispatcher::AddMaster:
        LOG_INFO("ipc: addmaster");
        return jsonOk(name, args);

    case Dispatcher::RemoveMaster:
        LOG_INFO("ipc: removemaster");
        return jsonOk(name, args);

    case Dispatcher::CycleLayout:
        LOG_INFO("ipc: cyclelayout '{}'", args);
        return jsonOk(name, args);

    case Dispatcher::SwitchLayout:
        LOG_INFO("ipc: switchlayout '{}'", args);
        if (args.empty()) return jsonError(name, "layout name required");
        return jsonOk(name, args);

    // -- Scrollable layout ---------------------------------------------------
    case Dispatcher::ScrollLeft:
        LOG_INFO("ipc: scrollleft");
        return jsonOk(name, args);

    case Dispatcher::ScrollRight:
        LOG_INFO("ipc: scrollright");
        return jsonOk(name, args);

    case Dispatcher::ScrollUp:
        LOG_INFO("ipc: scrollup");
        return jsonOk(name, args);

    case Dispatcher::ScrollDown:
        LOG_INFO("ipc: scrolldown");
        return jsonOk(name, args);

    case Dispatcher::CenterColumn:
        LOG_INFO("ipc: centercolumn");
        return jsonOk(name, args);

    case Dispatcher::SetColumnWidth:
        LOG_INFO("ipc: setcolumnwidth '{}'", args);
        if (args.empty()) return jsonError(name, "width value required");
        return jsonOk(name, args);

    case Dispatcher::ScrollColumn:
        LOG_INFO("ipc: scrollcolumn '{}'", args);
        return jsonOk(name, args);

    case Dispatcher::ScrollToWindow:
        LOG_INFO("ipc: scrolltowindow '{}'", args);
        return jsonOk(name, args);

    // -- Groups --------------------------------------------------------------
    case Dispatcher::ToggleGroup:
        LOG_INFO("ipc: togglegroup");
        return jsonOk(name, args);

    case Dispatcher::ChangeGroupActive:
        LOG_INFO("ipc: changegroupactive '{}'", args);
        return jsonOk(name, args);

    case Dispatcher::LockActiveGroup:
        LOG_INFO("ipc: lockactivegroup '{}'", args);
        return jsonOk(name, args);

    case Dispatcher::MoveWindowIntoGroup:
        LOG_INFO("ipc: movewindowintogroup '{}'", args);
        return jsonOk(name, args);

    case Dispatcher::MoveWindowOutOfGroup:
        LOG_INFO("ipc: movewindowoutofgroup");
        return jsonOk(name, args);

    case Dispatcher::LockGroups:
        LOG_INFO("ipc: lockgroups '{}'", args);
        return jsonOk(name, args);

    case Dispatcher::DenyWindowFromGroup:
        LOG_INFO("ipc: denywindowfromgroup '{}'", args);
        return jsonOk(name, args);

    // -- Overview & zoom -----------------------------------------------------
    case Dispatcher::ToggleOverview:
        LOG_INFO("ipc: toggleoverview");
        return jsonOk(name, args);

    case Dispatcher::ZoomIn:
        LOG_INFO("ipc: zoomin");
        return jsonOk(name, args);

    case Dispatcher::ZoomOut:
        LOG_INFO("ipc: zoomout");
        return jsonOk(name, args);

    // -- Screenshot / screen record ------------------------------------------
    case Dispatcher::Screenshot:
        LOG_INFO("ipc: screenshot '{}'", args);
        return jsonOk(name, args);

    case Dispatcher::ScreenRecord:
        LOG_INFO("ipc: screenrecord '{}'", args);
        return jsonOk(name, args);

    // -- Compositor control --------------------------------------------------
    case Dispatcher::Exit:
        LOG_INFO("ipc: exit compositor");
        return jsonOk(name, args);

    case Dispatcher::Reload:
        LOG_INFO("ipc: reload config");
        return jsonOk(name, args);

    case Dispatcher::ForceIdle:
        LOG_INFO("ipc: forceidle");
        return jsonOk(name, args);

    case Dispatcher::ForceRendererReload:
        LOG_INFO("ipc: forcerendererreload");
        return jsonOk(name, args);

    case Dispatcher::LockScreen:
        LOG_INFO("ipc: lockscreen");
        return jsonOk(name, args);

    // -- Cursor & input ------------------------------------------------------
    case Dispatcher::SetCursor:
        LOG_INFO("ipc: setcursor '{}'", args);
        if (args.empty()) return jsonError(name, "cursor theme/size required");
        return jsonOk(name, args);

    case Dispatcher::SwitchXKBLayout:
        LOG_INFO("ipc: switchxkblayout '{}'", args);
        return jsonOk(name, args);

    // -- Key/input pass-through ----------------------------------------------
    case Dispatcher::Submap:
        LOG_INFO("ipc: submap '{}'", args);
        return jsonOk(name, args);

    case Dispatcher::Global:
        LOG_INFO("ipc: global_shortcut '{}'", args);
        return jsonOk(name, args);

    case Dispatcher::Pass:
        LOG_INFO("ipc: pass '{}'", args);
        return jsonOk(name, args);

    case Dispatcher::SendShortcut:
        LOG_INFO("ipc: sendshortcut '{}'", args);
        return jsonOk(name, args);

    // -- Misc ----------------------------------------------------------------
    case Dispatcher::TagWindow:
        LOG_INFO("ipc: tagwindow '{}'", args);
        return jsonOk(name, args);

    case Dispatcher::BringActiveToTop:
        LOG_INFO("ipc: bringactivetotop");
        return jsonOk(name, args);

    case Dispatcher::AlterZOrder:
        LOG_INFO("ipc: alterzorder '{}'", args);
        return jsonOk(name, args);
    }

    // Try custom dispatchers
    {
        auto& reg = getCustomRegistry();
        std::lock_guard lock(reg.mutex);
        auto it = reg.dispatchers.find(name);
        if (it != reg.dispatchers.end()) {
            LOG_INFO("ipc: custom dispatcher '{}' '{}'", name, args);
            return it->second(args);
        }
    }

    LOG_WARN("ipc: unknown dispatcher {}", static_cast<int>(dispatcher));
    return jsonError("unknown", "unrecognized dispatcher");
}

// ===========================================================================
// parseDispatcher
// ===========================================================================

Dispatcher parseDispatcher(const std::string& name) {
    std::string cmd = name;
    if (auto pos = cmd.find(' '); pos != std::string::npos)
        cmd = cmd.substr(0, pos);

    std::transform(cmd.begin(), cmd.end(), cmd.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    const auto& map = nameToDispatcher();
    auto it = map.find(cmd);
    if (it != map.end())
        return it->second;

    // Aliases
    if (cmd == "exit_compositor") return Dispatcher::Exit;
    if (cmd == "workspace_next" || cmd == "workspacenext") return Dispatcher::WorkspaceNext;
    if (cmd == "workspace_prev" || cmd == "workspaceprev") return Dispatcher::WorkspacePrev;
    if (cmd == "cycle_layout" || cmd == "cyclelayout") return Dispatcher::CycleLayout;
    if (cmd == "switch_layout" || cmd == "switchlayout") return Dispatcher::SwitchLayout;

    LOG_WARN("ipc: unknown dispatcher name '{}', falling back to Exec", cmd);
    return Dispatcher::Exec;
}

// ===========================================================================
// getDispatcherName
// ===========================================================================

std::string getDispatcherName(Dispatcher dispatcher) {
    const auto& map = dispatcherToName();
    auto it = map.find(dispatcher);
    if (it != map.end())
        return it->second;
    return "exec";
}

// ===========================================================================
// listDispatchers
// ===========================================================================

std::vector<std::string> listDispatchers() {
    std::vector<std::string> result;
    result.reserve(std::size(kDispatcherTable));
    for (const auto& entry : kDispatcherTable)
        result.emplace_back(entry.name);

    // Include custom dispatchers
    {
        auto& reg = getCustomRegistry();
        std::lock_guard lock(reg.mutex);
        for (const auto& [name, _] : reg.dispatchers)
            result.push_back(name);
    }

    std::sort(result.begin(), result.end());
    return result;
}

} // namespace eternal

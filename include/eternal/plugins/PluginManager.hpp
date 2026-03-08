#pragma once

#include "PluginAPI.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace eternal {

// ---------------------------------------------------------------------------
// Hook storage -- Task 73
// ---------------------------------------------------------------------------

template <typename CallbackT>
struct HookEntry {
    CallbackT callback;
    HookPriority priority;
    std::string plugin_name; // which plugin registered this hook
};

template <typename CallbackT>
class HookList {
public:
    void add(const std::string& plugin, CallbackT cb, HookPriority priority) {
        std::lock_guard lock(mutex_);
        entries_.push_back({std::move(cb), priority, plugin});
        // Sort by priority (lower value = higher priority)
        std::sort(entries_.begin(), entries_.end(),
                  [](const auto& a, const auto& b) {
                      return static_cast<int>(a.priority) < static_cast<int>(b.priority);
                  });
    }

    void removeByPlugin(const std::string& plugin) {
        std::lock_guard lock(mutex_);
        std::erase_if(entries_, [&](const auto& e) { return e.plugin_name == plugin; });
    }

    template <typename... Args>
    void invoke(Args&&... args) const {
        std::lock_guard lock(mutex_);
        for (const auto& entry : entries_) {
            if (entry.callback)
                entry.callback(std::forward<Args>(args)...);
        }
    }

    [[nodiscard]] size_t size() const {
        std::lock_guard lock(mutex_);
        return entries_.size();
    }

private:
    mutable std::mutex mutex_;
    std::vector<HookEntry<CallbackT>> entries_;
};

// ---------------------------------------------------------------------------
// Loaded plugin handle -- Task 72
// ---------------------------------------------------------------------------

struct LoadedPlugin {
    std::string path;
    PluginInfo  info;
    void*       dl_handle = nullptr;   // dlopen handle
    PluginExitFunc exit_fn = nullptr;
    int         ref_count = 1;         // Reference counting for unload
    bool        hot_reloadable = true; // Can be hot-reloaded

    // Registered resources (for cleanup on unload)
    std::vector<std::string> registered_dispatchers;
    std::vector<std::string> registered_layouts;
    std::vector<std::string> registered_decorations;
    std::vector<std::string> registered_shaders;
    std::vector<std::string> registered_ipc_commands;
};

// ---------------------------------------------------------------------------
// Plugin dependency node
// ---------------------------------------------------------------------------

struct PluginDependencyNode {
    std::string name;
    std::vector<std::string> depends_on;
    bool loaded = false;
    bool loading = false; // cycle detection
};

// ---------------------------------------------------------------------------
// PluginManager -- Task 72, 73, 74, 75, 76, 77, 78, 80
// ---------------------------------------------------------------------------

class PluginManager {
public:
    PluginManager();
    ~PluginManager();

    PluginManager(const PluginManager&) = delete;
    PluginManager& operator=(const PluginManager&) = delete;

    // -- Plugin lifecycle -- Task 72 -----------------------------------------

    /// Load a plugin shared object from \p path.
    bool loadPlugin(const std::string& path);

    /// Unload a plugin by name (with reference counting).
    bool unloadPlugin(const std::string& name);

    /// Reload a plugin by name -- Task 80
    bool reloadPlugin(const std::string& name);

    /// Retrieve metadata for a loaded plugin.
    const PluginInfo* getPlugin(const std::string& name) const;

    /// Return names of all currently loaded plugins.
    std::vector<std::string> listPlugins() const;

    /// Check if a plugin is loaded.
    bool isLoaded(const std::string& name) const;

    // -- Auto-loading from config -- Task 77 ---------------------------------

    /// Load all plugins listed in config directory.
    void loadFromDirectory(const std::filesystem::path& dir);

    /// Load plugins from config plugin list.
    void loadFromConfig(const std::vector<std::pair<std::string, std::string>>& plugins);

    // -- Dependency resolution -- Task 77 ------------------------------------

    /// Resolve and load plugins in dependency order.
    bool resolveAndLoad(const std::vector<std::string>& pluginPaths);

    // -- Hot-reload watching -- Task 80 --------------------------------------

    /// Start watching loaded plugin .so files for changes.
    void startWatching();

    /// Stop watching.
    void stopWatching();

    /// Process pending inotify events for plugin files.
    void processPendingReloads();

    /// Get the inotify fd for event loop integration.
    [[nodiscard]] int getWatchFd() const noexcept;

    // -- Hook system -- Task 73 ----------------------------------------------

    HookList<WindowCallback>& onWindowCreate() { return hooks_.window_create; }
    HookList<WindowCallback>& onWindowDestroy() { return hooks_.window_destroy; }
    HookList<WindowCallback>& onWindowFocus() { return hooks_.window_focus; }
    HookList<WindowMoveCallback>& onWindowMove() { return hooks_.window_move; }
    HookList<WorkspaceCallback>& onWorkspaceCreate() { return hooks_.workspace_create; }
    HookList<WorkspaceCallback>& onWorkspaceSwitch() { return hooks_.workspace_switch; }
    HookList<RenderCallback>& onRenderPre() { return hooks_.render_pre; }
    HookList<RenderCallback>& onRenderPost() { return hooks_.render_post; }
    HookList<ConfigReloadCallback>& onConfigReload() { return hooks_.config_reload; }

    // -- Layout registration -- Task 74 --------------------------------------

    void registerLayout(const std::string& plugin, const std::string& name,
                        LayoutFactory factory);
    void unregisterLayout(const std::string& name);
    LayoutFactory getLayoutFactory(const std::string& name) const;
    std::vector<std::string> listLayouts() const;

    // -- Shader registration -- Task 75 --------------------------------------

    struct ShaderDef {
        std::string name;
        std::string vertex_source;
        std::string fragment_source;
        std::string plugin_name;
        bool compiled = false;
    };

    void registerShader(const std::string& plugin, const std::string& name,
                        const std::string& vertex, const std::string& fragment);
    void unregisterShader(const std::string& name);
    const ShaderDef* getShader(const std::string& name) const;
    std::vector<std::string> listShaders() const;

    // -- IPC command registration -- Task 76 ---------------------------------

    void registerIPCCommand(const std::string& plugin, const std::string& name,
                            IPCCommandCallback callback);
    void unregisterIPCCommand(const std::string& name);

    // -- API access --

    PluginAPI& api() noexcept { return api_; }
    const PluginAPI& api() const noexcept { return api_; }

private:
    void initAPI();
    bool checkVersionCompatibility(const PluginInfo& info) const;
    void cleanupPluginResources(const std::string& pluginName);

    PluginAPI api_{};
    mutable std::mutex mutex_;
    std::unordered_map<std::string, LoadedPlugin> plugins_;

    // Hook lists -- Task 73
    struct Hooks {
        HookList<WindowCallback> window_create;
        HookList<WindowCallback> window_destroy;
        HookList<WindowCallback> window_focus;
        HookList<WindowMoveCallback> window_move;
        HookList<WorkspaceCallback> workspace_create;
        HookList<WorkspaceCallback> workspace_switch;
        HookList<RenderCallback> render_pre;
        HookList<RenderCallback> render_post;
        HookList<ConfigReloadCallback> config_reload;
    } hooks_;

    // Layout registry -- Task 74
    std::unordered_map<std::string, LayoutFactory> layout_factories_;

    // Shader registry -- Task 75
    std::unordered_map<std::string, ShaderDef> shaders_;

    // IPC command registry -- Task 76
    std::unordered_map<std::string, IPCCommandCallback> ipc_commands_;

    // Plugin file watching -- Task 80
    struct WatchState;
    std::unique_ptr<WatchState> watch_state_;

    // Current plugin being loaded (for API callbacks to know context)
    std::string current_loading_plugin_;
};

} // namespace eternal

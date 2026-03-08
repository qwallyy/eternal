#include "eternal/plugins/PluginManager.hpp"
#include "eternal/ipc/IPCCommands.hpp"
#include "eternal/utils/Logger.hpp"

#include <algorithm>
#include <dlfcn.h>
#include <queue>

#ifdef __linux__
#include <poll.h>
#include <sys/inotify.h>
#include <unistd.h>
#endif

namespace eternal {

// ===========================================================================
// Static API callback implementations
// ===========================================================================

static PluginManager* s_active_manager = nullptr;

static void api_registerDispatcher(const char* name, DispatcherCallback callback) {
    if (!s_active_manager || !name) return;
    LOG_DEBUG("Plugin registered dispatcher: {}", name);
    registerCustomDispatcher(name, [cb = std::move(callback)](const std::string& args) -> std::string {
        cb(args);
        return R"({"ok":true,"dispatcher":")" + std::string("custom") + R"("})";
    });
    // Track for cleanup
    auto& mgr = *s_active_manager;
    if (!mgr.api().api_version) return; // safety
}

static void api_unregisterDispatcher(const char* name) {
    if (!name) return;
    unregisterCustomDispatcher(name);
}

static void api_registerDecoration(const char* name, DecorationFactory factory) {
    LOG_DEBUG("Plugin registered decoration: {}", name ? name : "null");
    (void)factory;
}

static void api_unregisterDecoration(const char* name) {
    LOG_DEBUG("Plugin unregistered decoration: {}", name ? name : "null");
}

static void api_registerLayout(const char* name, LayoutFactory factory) {
    if (!s_active_manager || !name) return;
    LOG_DEBUG("Plugin registered layout: {}", name);
    s_active_manager->registerLayout("", name, std::move(factory));
}

static void api_unregisterLayout(const char* name) {
    if (!s_active_manager || !name) return;
    s_active_manager->unregisterLayout(name);
}

static void api_registerShader(const char* name, const char* vertex,
                                const char* fragment,
                                ShaderValidationResult result_cb) {
    if (!s_active_manager || !name) return;
    LOG_DEBUG("Plugin registered shader: {}", name);
    std::string v = vertex ? vertex : "";
    std::string f = fragment ? fragment : "";
    s_active_manager->registerShader("", name, v, f);
    if (result_cb) result_cb(true, nullptr);
}

static void api_unregisterShader(const char* name) {
    if (!s_active_manager || !name) return;
    s_active_manager->unregisterShader(name);
}

static void api_bindShaderToWindow(const char* /*shader*/, uint64_t /*window_id*/) {
    // Stub: would bind shader to specific window
}

static void api_bindShaderGlobal(const char* /*shader*/) {
    // Stub: would bind shader as global post-processing
}

static void api_registerIPCCommand(const char* name, IPCCommandCallback callback) {
    if (!s_active_manager || !name) return;
    LOG_DEBUG("Plugin registered IPC command: {}", name);
    s_active_manager->registerIPCCommand("", name, std::move(callback));
}

static void api_unregisterIPCCommand(const char* name) {
    if (!s_active_manager || !name) return;
    s_active_manager->unregisterIPCCommand(name);
}

// -- Hook registrations -- Task 73
static void api_hookWindowCreate(WindowCallback callback, HookPriority priority) {
    if (!s_active_manager) return;
    s_active_manager->onWindowCreate().add("", std::move(callback), priority);
}

static void api_hookWindowDestroy(WindowCallback callback, HookPriority priority) {
    if (!s_active_manager) return;
    s_active_manager->onWindowDestroy().add("", std::move(callback), priority);
}

static void api_hookWindowFocus(WindowCallback callback, HookPriority priority) {
    if (!s_active_manager) return;
    s_active_manager->onWindowFocus().add("", std::move(callback), priority);
}

static void api_hookWindowMove(WindowMoveCallback callback, HookPriority priority) {
    if (!s_active_manager) return;
    s_active_manager->onWindowMove().add("", std::move(callback), priority);
}

static void api_hookWorkspaceCreate(WorkspaceCallback callback, HookPriority priority) {
    if (!s_active_manager) return;
    s_active_manager->onWorkspaceCreate().add("", std::move(callback), priority);
}

static void api_hookWorkspaceSwitch(WorkspaceCallback callback, HookPriority priority) {
    if (!s_active_manager) return;
    s_active_manager->onWorkspaceSwitch().add("", std::move(callback), priority);
}

static void api_hookRenderPre(RenderCallback callback, HookPriority priority) {
    if (!s_active_manager) return;
    s_active_manager->onRenderPre().add("", std::move(callback), priority);
}

static void api_hookRenderPost(RenderCallback callback, HookPriority priority) {
    if (!s_active_manager) return;
    s_active_manager->onRenderPost().add("", std::move(callback), priority);
}

static void api_hookConfigReload(ConfigReloadCallback callback, HookPriority priority) {
    if (!s_active_manager) return;
    s_active_manager->onConfigReload().add("", std::move(callback), priority);
}

// -- Config API
static void api_addConfigValue(const char* name, ConfigValue default_value) {
    LOG_DEBUG("Plugin added config value: {}", name ? name : "null");
    (void)default_value;
}

static bool api_getConfigValue(const char* name, ConfigValue* out) {
    LOG_DEBUG("Plugin queried config value: {}", name ? name : "null");
    (void)out;
    return false;
}

static bool api_setConfigValue(const char* name, const ConfigValue* value) {
    LOG_DEBUG("Plugin set config value: {}", name ? name : "null");
    (void)value;
    return false;
}

// -- Compositor query stubs
static uint64_t api_getActiveWindow() { return 0; }
static int api_getActiveWorkspace() { return 1; }
static const char* api_getActiveMonitor() { return "default"; }
static int api_getWindowCount() { return 0; }
static int api_getWorkspaceCount() { return 1; }

// -- Window manipulation stubs
static void api_moveWindow(uint64_t /*wid*/, int /*x*/, int /*y*/) {}
static void api_resizeWindow(uint64_t /*wid*/, int /*w*/, int /*h*/) {}
static void api_focusWindow(uint64_t /*wid*/) {}
static void api_closeWindow(uint64_t /*wid*/) {}
static void api_setWindowFloat(uint64_t /*wid*/, bool /*f*/) {}
static void api_setWindowFullscreen(uint64_t /*wid*/, bool /*f*/) {}
static void api_switchWorkspace(int /*ws*/) {}
static void api_moveWindowToWorkspace(uint64_t /*wid*/, int /*ws*/) {}

static void api_log(LogLevel level, const char* message) {
    if (message)
        Logger::instance().log(level, "[plugin] {}", message);
}

static void api_sendMessage(const char* /*target*/, const char* /*msg*/) {}
static void api_onMessage(void (*/*cb*/)(const char*, const char*)) {}

// ===========================================================================
// Watch state for hot-reloading -- Task 80
// ===========================================================================

struct PluginManager::WatchState {
#ifdef __linux__
    int inotify_fd = -1;
    std::unordered_map<int, std::string> wd_to_plugin; // watch descriptor -> plugin name
#endif
};

// ===========================================================================
// PluginManager construction / destruction
// ===========================================================================

PluginManager::PluginManager()
    : watch_state_(std::make_unique<WatchState>()) {
    s_active_manager = this;
    initAPI();
}

PluginManager::~PluginManager() {
    stopWatching();

    // Unload all plugins in reverse order
    std::vector<std::string> names;
    for (const auto& [name, _] : plugins_)
        names.push_back(name);
    std::reverse(names.begin(), names.end());

    for (const auto& name : names) {
        auto it = plugins_.find(name);
        if (it == plugins_.end()) continue;
        auto& plugin = it->second;

        if (plugin.exit_fn) {
            try {
                plugin.exit_fn();
            } catch (...) {
                LOG_ERROR("Plugin '{}' threw exception during unload", name);
            }
        }
        if (plugin.dl_handle) {
            dlclose(plugin.dl_handle);
        }
    }
    plugins_.clear();

    if (s_active_manager == this)
        s_active_manager = nullptr;
}

// ===========================================================================
// API initialization -- Task 71
// ===========================================================================

void PluginManager::initAPI() {
    api_.api_version = ETERNAL_PLUGIN_API_VERSION;

    // Dispatchers
    api_.registerDispatcher   = api_registerDispatcher;
    api_.unregisterDispatcher = api_unregisterDispatcher;

    // Decorations & layouts
    api_.registerDecoration   = api_registerDecoration;
    api_.unregisterDecoration = api_unregisterDecoration;
    api_.registerLayout       = api_registerLayout;
    api_.unregisterLayout     = api_unregisterLayout;

    // Shaders
    api_.registerShader       = api_registerShader;
    api_.unregisterShader     = api_unregisterShader;
    api_.bindShaderToWindow   = api_bindShaderToWindow;
    api_.bindShaderGlobal     = api_bindShaderGlobal;

    // IPC commands
    api_.registerIPCCommand   = api_registerIPCCommand;
    api_.unregisterIPCCommand = api_unregisterIPCCommand;

    // Hooks
    api_.hookWindowCreate     = api_hookWindowCreate;
    api_.hookWindowDestroy    = api_hookWindowDestroy;
    api_.hookWindowFocus      = api_hookWindowFocus;
    api_.hookWindowMove       = api_hookWindowMove;
    api_.hookWorkspaceCreate  = api_hookWorkspaceCreate;
    api_.hookWorkspaceSwitch  = api_hookWorkspaceSwitch;
    api_.hookRenderPre        = api_hookRenderPre;
    api_.hookRenderPost       = api_hookRenderPost;
    api_.hookConfigReload     = api_hookConfigReload;

    // Config
    api_.addConfigValue       = api_addConfigValue;
    api_.getConfigValue       = api_getConfigValue;
    api_.setConfigValue       = api_setConfigValue;

    // Compositor queries
    api_.getActiveWindow      = api_getActiveWindow;
    api_.getActiveWorkspace   = api_getActiveWorkspace;
    api_.getActiveMonitor     = api_getActiveMonitor;
    api_.getWindowCount       = api_getWindowCount;
    api_.getWorkspaceCount    = api_getWorkspaceCount;

    // Window manipulation
    api_.moveWindow           = api_moveWindow;
    api_.resizeWindow         = api_resizeWindow;
    api_.focusWindow          = api_focusWindow;
    api_.closeWindow          = api_closeWindow;
    api_.setWindowFloat       = api_setWindowFloat;
    api_.setWindowFullscreen  = api_setWindowFullscreen;
    api_.switchWorkspace      = api_switchWorkspace;
    api_.moveWindowToWorkspace = api_moveWindowToWorkspace;

    // Logging
    api_.log                  = api_log;

    // Plugin communication
    api_.sendMessage          = api_sendMessage;
    api_.onMessage            = api_onMessage;
}

// ===========================================================================
// Version compatibility -- Task 78
// ===========================================================================

bool PluginManager::checkVersionCompatibility(const PluginInfo& info) const {
    uint32_t plugin_major = (info.api_version >> 16) & 0xFF;
    uint32_t plugin_minor = (info.api_version >> 8) & 0xFF;
    uint32_t host_major = ETERNAL_PLUGIN_API_VERSION_MAJOR;
    uint32_t host_minor = ETERNAL_PLUGIN_API_VERSION_MINOR;

    // Major version must match exactly
    if (plugin_major != host_major) {
        LOG_ERROR("Plugin '{}' requires API v{}.x but host is v{}.{}.{}",
                  info.name, plugin_major, host_major, host_minor,
                  ETERNAL_PLUGIN_API_VERSION_PATCH);
        return false;
    }

    // Minor version: plugin should not require a newer minor than host
    if (plugin_minor > host_minor) {
        LOG_WARN("Plugin '{}' was built for API v{}.{} but host is v{}.{} -- "
                 "some features may not work",
                 info.name, plugin_major, plugin_minor, host_major, host_minor);
        // Warning only, still allow loading
    }

    return true;
}

// ===========================================================================
// Plugin loading -- Task 72
// ===========================================================================

bool PluginManager::loadPlugin(const std::string& path) {
    std::lock_guard lock(mutex_);

    // Open with RTLD_NOW (resolve all symbols immediately) and RTLD_LOCAL
    // (don't export symbols to other plugins)
    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        const char* err = dlerror();
        LOG_ERROR("Failed to load plugin '{}': {}", path, err ? err : "unknown error");
        return false;
    }

    // Clear any previous error
    dlerror();

    // Look up entry point
    auto entry_fn = reinterpret_cast<PluginEntryFunc>(dlsym(handle, "eternal_plugin_entry"));
    if (!entry_fn) {
        const char* err = dlerror();
        LOG_ERROR("Plugin '{}' has no eternal_plugin_entry symbol: {}",
                  path, err ? err : "symbol not found");
        dlclose(handle);
        return false;
    }

    // Set current loading context
    current_loading_plugin_ = path;

    // Call entry function
    PluginInfo info;
    try {
        info = entry_fn(&api_);
    } catch (const std::exception& e) {
        LOG_ERROR("Plugin '{}' threw exception during init: {}", path, e.what());
        dlclose(handle);
        current_loading_plugin_.clear();
        return false;
    } catch (...) {
        LOG_ERROR("Plugin '{}' threw unknown exception during init", path);
        dlclose(handle);
        current_loading_plugin_.clear();
        return false;
    }

    current_loading_plugin_.clear();

    if (info.name.empty()) {
        LOG_ERROR("Plugin '{}' returned empty name", path);
        dlclose(handle);
        return false;
    }

    // Check for duplicate
    if (plugins_.contains(info.name)) {
        LOG_WARN("Plugin '{}' is already loaded, skipping", info.name);
        dlclose(handle);
        return false;
    }

    // Version compatibility check -- Task 78
    if (!checkVersionCompatibility(info)) {
        LOG_ERROR("Plugin '{}' failed version compatibility check, refusing to load",
                  info.name);
        // Try to call exit if available
        auto exit_fn = reinterpret_cast<PluginExitFunc>(dlsym(handle, "eternal_plugin_exit"));
        if (exit_fn) exit_fn();
        dlclose(handle);
        return false;
    }

    // Look up exit function (optional)
    auto exit_fn = reinterpret_cast<PluginExitFunc>(dlsym(handle, "eternal_plugin_exit"));

    LoadedPlugin lp;
    lp.path      = path;
    lp.info      = std::move(info);
    lp.dl_handle = handle;
    lp.exit_fn   = exit_fn;
    lp.ref_count = 1;

    std::string name = lp.info.name;
    LOG_INFO("Loaded plugin '{}' v{} by {} (API v{}.{}.{})",
             name, lp.info.version, lp.info.author,
             (lp.info.api_version >> 16) & 0xFF,
             (lp.info.api_version >> 8) & 0xFF,
             lp.info.api_version & 0xFF);

    plugins_.emplace(std::move(name), std::move(lp));
    return true;
}

bool PluginManager::unloadPlugin(const std::string& name) {
    std::lock_guard lock(mutex_);

    auto it = plugins_.find(name);
    if (it == plugins_.end()) {
        LOG_WARN("Cannot unload plugin '{}': not loaded", name);
        return false;
    }

    auto& plugin = it->second;

    // Reference counting
    plugin.ref_count--;
    if (plugin.ref_count > 0) {
        LOG_DEBUG("Plugin '{}' ref_count decremented to {}", name, plugin.ref_count);
        return true;
    }

    // Clean up registered resources
    cleanupPluginResources(name);

    // Call exit function
    if (plugin.exit_fn) {
        try {
            plugin.exit_fn();
        } catch (const std::exception& e) {
            LOG_ERROR("Plugin '{}' threw exception during exit: {}", name, e.what());
        } catch (...) {
            LOG_ERROR("Plugin '{}' threw unknown exception during exit", name);
        }
    }

    // Close the shared library
    if (plugin.dl_handle) {
        if (dlclose(plugin.dl_handle) != 0) {
            const char* err = dlerror();
            LOG_WARN("dlclose failed for plugin '{}': {}", name, err ? err : "unknown");
        }
    }

    LOG_INFO("Unloaded plugin '{}'", name);
    plugins_.erase(it);
    return true;
}

void PluginManager::cleanupPluginResources(const std::string& pluginName) {
    auto it = plugins_.find(pluginName);
    if (it == plugins_.end()) return;
    auto& plugin = it->second;

    // Remove hooks registered by this plugin
    hooks_.window_create.removeByPlugin(pluginName);
    hooks_.window_destroy.removeByPlugin(pluginName);
    hooks_.window_focus.removeByPlugin(pluginName);
    hooks_.window_move.removeByPlugin(pluginName);
    hooks_.workspace_create.removeByPlugin(pluginName);
    hooks_.workspace_switch.removeByPlugin(pluginName);
    hooks_.render_pre.removeByPlugin(pluginName);
    hooks_.render_post.removeByPlugin(pluginName);
    hooks_.config_reload.removeByPlugin(pluginName);

    // Remove registered dispatchers
    for (const auto& d : plugin.registered_dispatchers)
        unregisterCustomDispatcher(d);

    // Remove registered layouts
    for (const auto& l : plugin.registered_layouts)
        layout_factories_.erase(l);

    // Remove registered shaders
    for (const auto& s : plugin.registered_shaders)
        shaders_.erase(s);

    // Remove registered IPC commands
    for (const auto& c : plugin.registered_ipc_commands)
        ipc_commands_.erase(c);
}

// ===========================================================================
// Reload -- Task 80
// ===========================================================================

bool PluginManager::reloadPlugin(const std::string& name) {
    std::string path;
    {
        std::lock_guard lock(mutex_);
        auto it = plugins_.find(name);
        if (it == plugins_.end()) {
            LOG_WARN("Cannot reload plugin '{}': not loaded", name);
            return false;
        }
        path = it->second.path;
    }

    LOG_INFO("Reloading plugin '{}' from {}", name, path);

    // Unload (force, ignoring ref count)
    {
        std::lock_guard lock(mutex_);
        auto it = plugins_.find(name);
        if (it != plugins_.end())
            it->second.ref_count = 1; // Force unload
    }
    unloadPlugin(name);

    // Load from same path
    return loadPlugin(path);
}

// ===========================================================================
// Query
// ===========================================================================

const PluginInfo* PluginManager::getPlugin(const std::string& name) const {
    std::lock_guard lock(mutex_);
    auto it = plugins_.find(name);
    if (it == plugins_.end()) return nullptr;
    return &it->second.info;
}

std::vector<std::string> PluginManager::listPlugins() const {
    std::lock_guard lock(mutex_);
    std::vector<std::string> names;
    names.reserve(plugins_.size());
    for (const auto& [name, _] : plugins_)
        names.push_back(name);
    return names;
}

bool PluginManager::isLoaded(const std::string& name) const {
    std::lock_guard lock(mutex_);
    return plugins_.contains(name);
}

// ===========================================================================
// Auto-loading -- Task 77
// ===========================================================================

void PluginManager::loadFromDirectory(const std::filesystem::path& dir) {
    if (!std::filesystem::exists(dir)) {
        LOG_DEBUG("Plugin directory does not exist: {}", dir.string());
        return;
    }

    std::vector<std::string> paths;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.is_regular_file()) {
            auto ext = entry.path().extension().string();
            if (ext == ".so" || ext == ".dylib") {
                paths.push_back(entry.path().string());
            }
        }
    }

    std::sort(paths.begin(), paths.end());

    for (const auto& path : paths) {
        LOG_INFO("Auto-loading plugin: {}", path);
        loadPlugin(path);
    }
}

void PluginManager::loadFromConfig(
    const std::vector<std::pair<std::string, std::string>>& plugins) {
    for (const auto& [name, path] : plugins) {
        if (path.empty()) {
            LOG_WARN("Plugin '{}' has no path specified, skipping", name);
            continue;
        }
        LOG_INFO("Loading plugin from config: {} ({})", name, path);
        loadPlugin(path);
    }
}

// ===========================================================================
// Dependency resolution -- Task 77
// ===========================================================================

bool PluginManager::resolveAndLoad(const std::vector<std::string>& pluginPaths) {
    // First pass: probe all plugins for their info (without full init)
    std::unordered_map<std::string, PluginDependencyNode> nodes;
    std::unordered_map<std::string, std::string> name_to_path;

    for (const auto& path : pluginPaths) {
        void* handle = dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL);
        if (!handle) continue;

        // Try to get info without full init
        auto info_fn = reinterpret_cast<PluginInfoFunc>(dlsym(handle, "eternal_plugin_info"));
        if (info_fn) {
            PluginInfo info = info_fn();
            if (!info.name.empty()) {
                PluginDependencyNode node;
                node.name = info.name;
                node.depends_on = info.dependencies;
                nodes[info.name] = std::move(node);
                name_to_path[info.name] = path;
            }
        } else {
            // No info function; just use the path as a name placeholder
            std::string name = std::filesystem::path(path).stem().string();
            PluginDependencyNode node;
            node.name = name;
            nodes[name] = std::move(node);
            name_to_path[name] = path;
        }

        dlclose(handle);
    }

    // Topological sort
    std::vector<std::string> load_order;
    std::unordered_set<std::string> visited;
    std::unordered_set<std::string> in_progress;

    std::function<bool(const std::string&)> visit;
    visit = [&](const std::string& name) -> bool {
        if (visited.count(name)) return true;
        if (in_progress.count(name)) {
            LOG_ERROR("Circular dependency detected involving plugin '{}'", name);
            return false;
        }

        in_progress.insert(name);

        auto it = nodes.find(name);
        if (it != nodes.end()) {
            for (const auto& dep : it->second.depends_on) {
                if (!visit(dep)) return false;
            }
        }

        in_progress.erase(name);
        visited.insert(name);
        load_order.push_back(name);
        return true;
    };

    for (const auto& [name, _] : nodes) {
        if (!visit(name)) return false;
    }

    // Load in dependency order
    bool all_ok = true;
    for (const auto& name : load_order) {
        auto pit = name_to_path.find(name);
        if (pit == name_to_path.end()) {
            LOG_WARN("Plugin '{}' is a dependency but no path found", name);
            continue;
        }
        if (!loadPlugin(pit->second)) {
            LOG_ERROR("Failed to load plugin '{}' from {}", name, pit->second);
            all_ok = false;
        }
    }

    return all_ok;
}

// ===========================================================================
// Hot-reload file watching -- Task 80
// ===========================================================================

void PluginManager::startWatching() {
#ifdef __linux__
    if (watch_state_->inotify_fd >= 0)
        return; // Already watching

    watch_state_->inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (watch_state_->inotify_fd < 0) {
        LOG_ERROR("PluginManager: inotify_init1 failed: {}", strerror(errno));
        return;
    }

    std::lock_guard lock(mutex_);
    for (const auto& [name, plugin] : plugins_) {
        if (!plugin.hot_reloadable) continue;

        int wd = inotify_add_watch(watch_state_->inotify_fd,
                                    plugin.path.c_str(),
                                    IN_CLOSE_WRITE | IN_MODIFY);
        if (wd >= 0) {
            watch_state_->wd_to_plugin[wd] = name;
            LOG_DEBUG("Watching plugin file: {} (wd={})", plugin.path, wd);
        }
    }
#endif
}

void PluginManager::stopWatching() {
#ifdef __linux__
    if (!watch_state_ || watch_state_->inotify_fd < 0)
        return;

    for (const auto& [wd, _] : watch_state_->wd_to_plugin)
        inotify_rm_watch(watch_state_->inotify_fd, wd);
    watch_state_->wd_to_plugin.clear();

    ::close(watch_state_->inotify_fd);
    watch_state_->inotify_fd = -1;
#endif
}

int PluginManager::getWatchFd() const noexcept {
#ifdef __linux__
    return watch_state_ ? watch_state_->inotify_fd : -1;
#else
    return -1;
#endif
}

void PluginManager::processPendingReloads() {
#ifdef __linux__
    if (!watch_state_ || watch_state_->inotify_fd < 0)
        return;

    struct pollfd pfd{};
    pfd.fd = watch_state_->inotify_fd;
    pfd.events = POLLIN;

    if (::poll(&pfd, 1, 0) <= 0)
        return;

    alignas(struct inotify_event) char buf[4096];
    std::unordered_set<std::string> to_reload;

    while (true) {
        ssize_t n = ::read(watch_state_->inotify_fd, buf, sizeof(buf));
        if (n <= 0) break;

        const char* ptr = buf;
        while (ptr < buf + n) {
            const auto* event = reinterpret_cast<const struct inotify_event*>(ptr);
            auto it = watch_state_->wd_to_plugin.find(event->wd);
            if (it != watch_state_->wd_to_plugin.end()) {
                to_reload.insert(it->second);
            }
            ptr += sizeof(struct inotify_event) + event->len;
        }
    }

    for (const auto& name : to_reload) {
        LOG_INFO("Hot-reloading plugin: {}", name);
        reloadPlugin(name);

        // Re-establish watch for the reloaded plugin
        std::lock_guard lock(mutex_);
        auto pit = plugins_.find(name);
        if (pit != plugins_.end()) {
            int wd = inotify_add_watch(watch_state_->inotify_fd,
                                        pit->second.path.c_str(),
                                        IN_CLOSE_WRITE | IN_MODIFY);
            if (wd >= 0) {
                watch_state_->wd_to_plugin[wd] = name;
            }
        }
    }
#endif
}

// ===========================================================================
// Layout registration -- Task 74
// ===========================================================================

void PluginManager::registerLayout(const std::string& plugin, const std::string& name,
                                    LayoutFactory factory) {
    std::lock_guard lock(mutex_);
    layout_factories_[name] = std::move(factory);
    LOG_INFO("Registered custom layout: {}", name);

    // Track for cleanup
    if (!plugin.empty()) {
        auto it = plugins_.find(plugin);
        if (it != plugins_.end())
            it->second.registered_layouts.push_back(name);
    }
}

void PluginManager::unregisterLayout(const std::string& name) {
    std::lock_guard lock(mutex_);
    layout_factories_.erase(name);
}

LayoutFactory PluginManager::getLayoutFactory(const std::string& name) const {
    std::lock_guard lock(mutex_);
    auto it = layout_factories_.find(name);
    if (it != layout_factories_.end())
        return it->second;
    return nullptr;
}

std::vector<std::string> PluginManager::listLayouts() const {
    std::lock_guard lock(mutex_);
    std::vector<std::string> names;
    for (const auto& [name, _] : layout_factories_)
        names.push_back(name);
    return names;
}

// ===========================================================================
// Shader registration -- Task 75
// ===========================================================================

void PluginManager::registerShader(const std::string& plugin, const std::string& name,
                                    const std::string& vertex, const std::string& fragment) {
    std::lock_guard lock(mutex_);
    ShaderDef def;
    def.name = name;
    def.vertex_source = vertex;
    def.fragment_source = fragment;
    def.plugin_name = plugin;
    def.compiled = false; // Will be compiled by renderer
    shaders_[name] = std::move(def);
    LOG_INFO("Registered shader: {}", name);

    if (!plugin.empty()) {
        auto it = plugins_.find(plugin);
        if (it != plugins_.end())
            it->second.registered_shaders.push_back(name);
    }
}

void PluginManager::unregisterShader(const std::string& name) {
    std::lock_guard lock(mutex_);
    shaders_.erase(name);
}

const PluginManager::ShaderDef* PluginManager::getShader(const std::string& name) const {
    std::lock_guard lock(mutex_);
    auto it = shaders_.find(name);
    if (it != shaders_.end())
        return &it->second;
    return nullptr;
}

std::vector<std::string> PluginManager::listShaders() const {
    std::lock_guard lock(mutex_);
    std::vector<std::string> names;
    for (const auto& [name, _] : shaders_)
        names.push_back(name);
    return names;
}

// ===========================================================================
// IPC command registration -- Task 76
// ===========================================================================

void PluginManager::registerIPCCommand(const std::string& plugin, const std::string& name,
                                        IPCCommandCallback callback) {
    std::lock_guard lock(mutex_);
    ipc_commands_[name] = std::move(callback);

    // Also register as a custom dispatcher for IPC routing
    registerCustomDispatcher(name, [this, name](const std::string& args) -> std::string {
        std::lock_guard lk(mutex_);
        auto it = ipc_commands_.find(name);
        if (it != ipc_commands_.end() && it->second)
            return it->second(args);
        return R"({"ok":false,"error":"IPC command not found"})";
    });

    if (!plugin.empty()) {
        auto it = plugins_.find(plugin);
        if (it != plugins_.end())
            it->second.registered_ipc_commands.push_back(name);
    }
}

void PluginManager::unregisterIPCCommand(const std::string& name) {
    std::lock_guard lock(mutex_);
    ipc_commands_.erase(name);
    unregisterCustomDispatcher(name);
}

} // namespace eternal

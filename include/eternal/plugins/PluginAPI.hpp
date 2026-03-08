#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace eternal {

// ---------------------------------------------------------------------------
// API version for compatibility checking -- Task 78
// ---------------------------------------------------------------------------

constexpr uint32_t ETERNAL_PLUGIN_API_VERSION_MAJOR = 1;
constexpr uint32_t ETERNAL_PLUGIN_API_VERSION_MINOR = 0;
constexpr uint32_t ETERNAL_PLUGIN_API_VERSION_PATCH = 0;

constexpr uint32_t ETERNAL_PLUGIN_API_VERSION =
    (ETERNAL_PLUGIN_API_VERSION_MAJOR << 16) |
    (ETERNAL_PLUGIN_API_VERSION_MINOR << 8) |
    ETERNAL_PLUGIN_API_VERSION_PATCH;

// ---------------------------------------------------------------------------
// Plugin metadata -- Task 71
// ---------------------------------------------------------------------------

struct PluginInfo {
    std::string name;
    std::string version;
    std::string author;
    std::string description;
    uint32_t api_version = ETERNAL_PLUGIN_API_VERSION;  // Task 78

    // Optional dependency list
    std::vector<std::string> dependencies;
};

// ---------------------------------------------------------------------------
// Types used by the plugin API
// ---------------------------------------------------------------------------

using ConfigValue = std::variant<int64_t, double, bool, std::string>;
using DispatcherCallback = std::function<void(const std::string& args)>;
using WindowCallback = std::function<void(uint64_t window_id)>;
using WindowMoveCallback = std::function<void(uint64_t window_id, int x, int y, int w, int h)>;
using WorkspaceCallback = std::function<void(int workspace_id)>;
using RenderCallback = std::function<void(const char* output_name)>;
using ConfigReloadCallback = std::function<void()>;
using DecorationFactory = std::function<void*(uint64_t window_id)>;
using LayoutFactory = std::function<void*(int workspace_id)>;
using ShaderValidationResult = std::function<void(bool success, const char* error)>;

/// Custom IPC command callback -- Task 76
using IPCCommandCallback = std::function<std::string(const std::string& args)>;

// ---------------------------------------------------------------------------
// Hook priority -- Task 73
// ---------------------------------------------------------------------------

enum class HookPriority : int {
    Highest = -1000,
    High    = -100,
    Normal  = 0,
    Low     = 100,
    Lowest  = 1000,
};

// ---------------------------------------------------------------------------
// Log levels -- forward-declared from Logger.hpp
// If Logger.hpp is already included, this is a no-op.
// Otherwise, we provide the definition here for plugin-only builds.
// ---------------------------------------------------------------------------

#ifndef ETERNAL_LOG_LEVEL_DEFINED
#define ETERNAL_LOG_LEVEL_DEFINED
enum class LogLevel : uint8_t {
    Trace = 0,
    Debug,
    Info,
    Warn,
    Error,
    Critical,
};
#endif

// ---------------------------------------------------------------------------
// Plugin API -- Task 71: function pointers for all compositor APIs
// ---------------------------------------------------------------------------

struct PluginAPI {
    // -- API version --
    uint32_t api_version = ETERNAL_PLUGIN_API_VERSION;

    // -- Dispatchers -- Task 76
    void (*registerDispatcher)(const char* name, DispatcherCallback callback) = nullptr;
    void (*unregisterDispatcher)(const char* name) = nullptr;

    // -- Decorations & layouts -- Task 74
    void (*registerDecoration)(const char* name, DecorationFactory factory) = nullptr;
    void (*unregisterDecoration)(const char* name) = nullptr;
    void (*registerLayout)(const char* name, LayoutFactory factory) = nullptr;
    void (*unregisterLayout)(const char* name) = nullptr;

    // -- Shaders -- Task 75
    void (*registerShader)(const char* name, const char* glsl_vertex,
                           const char* glsl_fragment,
                           ShaderValidationResult result_cb) = nullptr;
    void (*unregisterShader)(const char* name) = nullptr;
    void (*bindShaderToWindow)(const char* shader_name, uint64_t window_id) = nullptr;
    void (*bindShaderGlobal)(const char* shader_name) = nullptr;

    // -- IPC command registration -- Task 76
    void (*registerIPCCommand)(const char* name, IPCCommandCallback callback) = nullptr;
    void (*unregisterIPCCommand)(const char* name) = nullptr;

    // -- Hooks -- Task 73
    void (*hookWindowCreate)(WindowCallback callback, HookPriority priority) = nullptr;
    void (*hookWindowDestroy)(WindowCallback callback, HookPriority priority) = nullptr;
    void (*hookWindowFocus)(WindowCallback callback, HookPriority priority) = nullptr;
    void (*hookWindowMove)(WindowMoveCallback callback, HookPriority priority) = nullptr;
    void (*hookWorkspaceCreate)(WorkspaceCallback callback, HookPriority priority) = nullptr;
    void (*hookWorkspaceSwitch)(WorkspaceCallback callback, HookPriority priority) = nullptr;
    void (*hookRenderPre)(RenderCallback callback, HookPriority priority) = nullptr;
    void (*hookRenderPost)(RenderCallback callback, HookPriority priority) = nullptr;
    void (*hookConfigReload)(ConfigReloadCallback callback, HookPriority priority) = nullptr;

    // -- Configuration helpers --
    void (*addConfigValue)(const char* name, ConfigValue default_value) = nullptr;
    bool (*getConfigValue)(const char* name, ConfigValue* out) = nullptr;
    bool (*setConfigValue)(const char* name, const ConfigValue* value) = nullptr;

    // -- Compositor queries --
    uint64_t (*getActiveWindow)() = nullptr;
    int      (*getActiveWorkspace)() = nullptr;
    const char* (*getActiveMonitor)() = nullptr;
    int      (*getWindowCount)() = nullptr;
    int      (*getWorkspaceCount)() = nullptr;

    // -- Window manipulation --
    void (*moveWindow)(uint64_t window_id, int x, int y) = nullptr;
    void (*resizeWindow)(uint64_t window_id, int w, int h) = nullptr;
    void (*focusWindow)(uint64_t window_id) = nullptr;
    void (*closeWindow)(uint64_t window_id) = nullptr;
    void (*setWindowFloat)(uint64_t window_id, bool floating) = nullptr;
    void (*setWindowFullscreen)(uint64_t window_id, bool fullscreen) = nullptr;

    // -- Workspace --
    void (*switchWorkspace)(int workspace_id) = nullptr;
    void (*moveWindowToWorkspace)(uint64_t window_id, int workspace_id) = nullptr;

    // -- Logging --
    void (*log)(LogLevel level, const char* message) = nullptr;

    // -- Plugin communication --
    void (*sendMessage)(const char* target_plugin, const char* message) = nullptr;
    void (*onMessage)(void (*callback)(const char* from_plugin, const char* message)) = nullptr;
};

// ---------------------------------------------------------------------------
// Plugin entry / exit signatures -- Task 71
// ---------------------------------------------------------------------------

using PluginEntryFunc = PluginInfo (*)(const PluginAPI* api);
using PluginExitFunc = void (*)();
using PluginInfoFunc = PluginInfo (*)();

// ---------------------------------------------------------------------------
// Macros for plugin authors -- Task 71
// ---------------------------------------------------------------------------

/// Declare the plugin initialization function.
/// The function receives the API struct and should return PluginInfo.
#define ETERNAL_PLUGIN_INIT(func)                                               \
    extern "C" [[gnu::visibility("default")]]                                   \
    eternal::PluginInfo eternal_plugin_entry(const eternal::PluginAPI* api) {    \
        return func(api);                                                       \
    }

/// Declare the plugin cleanup function.
#define ETERNAL_PLUGIN_FINI(func)                                               \
    extern "C" [[gnu::visibility("default")]]                                   \
    void eternal_plugin_exit() {                                                \
        func();                                                                 \
    }

/// Declare the plugin info function (for metadata without loading).
#define ETERNAL_PLUGIN_INFO(info_struct)                                         \
    extern "C" [[gnu::visibility("default")]]                                   \
    eternal::PluginInfo eternal_plugin_info() {                                 \
        return info_struct;                                                      \
    }

// Legacy macros for backward compatibility
#define ETERNAL_PLUGIN_ENTRY(func) ETERNAL_PLUGIN_INIT(func)
#define ETERNAL_PLUGIN_EXIT(func)  ETERNAL_PLUGIN_FINI(func)

} // namespace eternal

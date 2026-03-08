#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace eternal {

// ---------------------------------------------------------------------------
// General
// ---------------------------------------------------------------------------

struct GeneralConfig {
    std::string layout = "scrollable";
    int border_size = 2;
    int gaps_in = 5;
    int gaps_out = 10;
    bool focus_follows_mouse = true;
    std::string cursor_theme = "default";
    int cursor_size = 24;
    int vrr = 0;               // 0 = off, 1 = on, 2 = fullscreen-only
    bool allow_tearing = false;
    bool resize_on_border = true;
    int extend_border_grab_area = 15;
    bool hover_icon_on_border = true;
    bool no_focus_fallback = false;
    bool close_special_on_empty = true;
    bool new_window_takes_over_fullscreen = false;
};

// ---------------------------------------------------------------------------
// Decoration
// ---------------------------------------------------------------------------

struct ShadowConfig {
    bool enabled = true;
    int range = 4;
    int render_power = 3;
    float opacity = 1.0f;
    std::string color = "0x1a1a1aee";
    std::string color_inactive;
    int offset_x = 0;
    int offset_y = 0;
    float scale = 1.0f;
};

struct BlurConfig {
    bool enabled = true;
    int size = 8;
    int passes = 1;
    bool noise = true;
    float noise_intensity = 0.0117f;
    float contrast = 0.8916f;
    float brightness = 0.8172f;
    float vibrancy = 0.1696f;
    float vibrancy_darkness = 0.0f;
    bool xray = false;
    bool popups = false;
    bool special = false;
    bool new_optimizations = true;
};

struct BorderGradient {
    std::vector<std::string> colors_active = {"0x33ccffee", "0x00ff99ee"};
    std::vector<std::string> colors_inactive = {"0x595959aa"};
    int angle = 45;
};

struct BorderConfig {
    bool enabled = true;
    BorderGradient gradient;
};

struct DecorationConfig {
    int rounding = 0;
    float active_opacity = 1.0f;
    float inactive_opacity = 1.0f;
    bool dim_inactive = false;
    float dim_strength = 0.5f;
    ShadowConfig shadow;
    BlurConfig blur;
    BorderConfig border;
};

// ---------------------------------------------------------------------------
// Animation
// ---------------------------------------------------------------------------

struct ConfigBezierCurve {
    std::string name;
    float x1, y1, x2, y2;
};

struct SpringConfig {
    float stiffness = 200.0f;
    float damping = 20.0f;
    float mass = 1.0f;
};

struct AnimationDef {
    bool enabled = true;
    float speed = 1.0f;
    std::string bezier = "default";
    std::string style = "slide";   // e.g. slide, popin, fade
    float style_param = 0.0f;     // e.g. popin percentage
};

struct AnimationConfig {
    bool enabled = true;
    float speed_multiplier = 1.0f;
    std::unordered_map<std::string, ConfigBezierCurve> bezier_curves;
    AnimationDef window_open;
    AnimationDef window_close;
    AnimationDef window_move;
    AnimationDef window_resize;
    AnimationDef fade_in;
    AnimationDef fade_out;
    AnimationDef workspace_switch;
    SpringConfig spring;
};

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

struct KeyboardConfig {
    std::string rules;
    std::string model;
    std::string layout = "us";
    std::string variant;
    std::string options;
    int repeat_rate = 25;
    int repeat_delay = 600;
    bool numlock_by_default = false;
    bool capslock_by_default = false;
};

struct TouchpadConfig {
    bool disable_while_typing = true;
    bool natural_scroll = false;
    int scroll_factor = 1;
    bool middle_button_emulation = false;
    bool tap_button_map = false;   // false = lrm, true = lmr
    bool clickfinger_behavior = false;
    bool tap_to_click = true;
    bool drag_lock = false;
    bool tap_and_drag = true;
};

struct PointerConfig {
    float sensitivity = 0.0f;
    bool accel_profile_flat = false;
    bool natural_scroll = false;
    int scroll_factor = 1;
    bool left_handed = false;
};

struct TabletConfig {
    std::string transform;
    std::string output;
    std::string region_position;
    std::string region_size;
    bool relative_input = false;
};

struct InputDeviceOverride {
    std::string device_name;
    std::optional<float> sensitivity;
    std::optional<bool> accel_profile_flat;
    std::optional<bool> natural_scroll;
    std::optional<int> scroll_factor;
    std::optional<bool> left_handed;
    std::optional<bool> tap_to_click;
    std::optional<bool> disable_while_typing;
    std::optional<bool> middle_button_emulation;
    std::optional<bool> drag_lock;
};

struct InputConfig {
    KeyboardConfig keyboard;
    TouchpadConfig touchpad;
    PointerConfig pointer;
    TabletConfig tablet;
    bool follow_mouse = true;
    bool float_switch_override = true;
    std::vector<InputDeviceOverride> device_overrides;
};

// ---------------------------------------------------------------------------
// Keybinds
// ---------------------------------------------------------------------------

enum class BindFlag : uint32_t {
    None        = 0,
    Locked      = 1 << 0,
    Release     = 1 << 1,
    Repeat      = 1 << 2,
    Mouse       = 1 << 3,
    NonConsuming= 1 << 4,
    Transparent = 1 << 5,
    IgnoreMods  = 1 << 6,
};

inline BindFlag operator|(BindFlag a, BindFlag b) {
    return static_cast<BindFlag>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline BindFlag operator&(BindFlag a, BindFlag b) {
    return static_cast<BindFlag>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

struct Keybind {
    uint32_t mods = 0;            // modifier mask (Ctrl, Alt, Super, Shift)
    std::string key;              // key name or mouse button
    std::string dispatcher;       // e.g. "exec", "killactive", "workspace"
    std::string args;             // dispatcher arguments
    BindFlag flags = BindFlag::None;
    std::string submap;           // empty = default, else submap name
};

struct BindConfig {
    std::vector<Keybind> keybinds;
};

// ---------------------------------------------------------------------------
// Gestures
// ---------------------------------------------------------------------------

struct GestureMapping {
    std::string dispatcher;
    std::string args;
};

struct GestureConfig {
    std::unordered_map<std::string, GestureMapping> swipe;
    std::unordered_map<std::string, GestureMapping> pinch;
    std::unordered_map<std::string, GestureMapping> hold;
};

// ---------------------------------------------------------------------------
// Monitor
// ---------------------------------------------------------------------------

struct MonitorConfig {
    std::string name;              // e.g. "DP-1", "" for default
    int width = 0;
    int height = 0;
    float refresh_rate = 0.0f;
    int x = 0;
    int y = 0;
    float scale = 1.0f;
    int transform = 0;            // wl_output transform
    bool enabled = true;
    bool mirror_of_primary = false;
    std::string mirror_of;
    int bitdepth = 8;
    bool vrr = false;
};

// ---------------------------------------------------------------------------
// Window rules
// ---------------------------------------------------------------------------

struct WindowRuleConfig {
    // Matching criteria
    std::string window_class;
    std::string window_title;
    std::string window_class_regex;
    std::string window_title_regex;
    bool is_floating = false;
    bool is_fullscreen = false;

    // Actions
    std::optional<bool> floating;
    std::optional<bool> fullscreen;
    std::optional<bool> pinned;
    std::optional<int> workspace;
    std::optional<std::string> monitor;
    std::optional<float> opacity;
    std::optional<float> opacity_inactive;
    std::optional<bool> noblur;
    std::optional<bool> noshadow;
    std::optional<bool> noborder;
    std::optional<bool> norounding;
    std::optional<int> rounding;
    std::optional<std::string> animation_style;
    std::optional<std::pair<int, int>> size;
    std::optional<std::pair<int, int>> min_size;
    std::optional<std::pair<int, int>> max_size;
    std::optional<std::pair<int, int>> position;
    std::optional<bool> center;
};

// ---------------------------------------------------------------------------
// Workspace rules
// ---------------------------------------------------------------------------

struct WorkspaceRuleConfig {
    int workspace_id = -1;
    std::string name;
    std::string monitor;
    std::string default_layout;
    bool persistent = false;
    int gaps_in = -1;
    int gaps_out = -1;
    int border_size = -1;
    int rounding = -1;
};

// ---------------------------------------------------------------------------
// Plugins
// ---------------------------------------------------------------------------

struct PluginConfig {
    std::string name;
    std::string path;
    bool enabled = true;
    std::unordered_map<std::string, std::string> settings;
};

// ---------------------------------------------------------------------------
// Environment
// ---------------------------------------------------------------------------

struct EnvironmentConfig {
    std::unordered_map<std::string, std::string> variables;
};

// ---------------------------------------------------------------------------
// Exec
// ---------------------------------------------------------------------------

struct ExecConfig {
    std::vector<std::string> exec_once;     // run once on startup
    std::vector<std::string> exec;          // run on every reload
    std::vector<std::string> exec_shutdown; // run on shutdown
};

// ---------------------------------------------------------------------------
// XWayland
// ---------------------------------------------------------------------------

struct XWaylandConfig {
    bool enabled = true;
    bool use_nearest_neighbor = true;
    bool force_zero_scaling = false;
};

// ---------------------------------------------------------------------------
// Permissions
// ---------------------------------------------------------------------------

struct PermissionsConfig {
    bool screencopy = true;
    bool keyboard_grab = true;
    bool input_inhibit = true;
    std::vector<std::string> screencopy_allowlist;
    std::vector<std::string> keyboard_grab_allowlist;
};

// ---------------------------------------------------------------------------
// Debug
// ---------------------------------------------------------------------------

struct DebugConfig {
    bool overlay = false;
    bool damage_tracking = true;
    bool disable_logs = false;
    bool disable_time = false;
    int log_level = 2;            // 0=error 1=warn 2=info 3=debug 4=trace
    bool colored_stdout = true;
    bool enable_stdout_logs = true;
    bool manual_crash = false;
};

// ---------------------------------------------------------------------------
// Runtime option value
// ---------------------------------------------------------------------------

using RuntimeOptionValue = std::variant<std::monostate, int64_t, double, bool, std::string>;

// ---------------------------------------------------------------------------
// Config change callback
// ---------------------------------------------------------------------------

using ConfigChangeCallback = std::function<void()>;
using ConfigErrorCallback = std::function<void(const std::string& error)>;

// ---------------------------------------------------------------------------
// ConfigManager
// ---------------------------------------------------------------------------

class ConfigManager {
public:
    ConfigManager();
    ~ConfigManager();

    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    /// Load configuration from the given KDL file.
    void load(const std::filesystem::path& path);

    /// Reload the current configuration file. Returns true on success.
    bool reload();

    /// Start watching the configuration file for changes (inotify).
    void watchFile();

    /// Stop watching the configuration file.
    void stopWatching();

    /// Process any pending inotify events (call from event loop).
    void processPendingEvents();

    /// Get the inotify fd for integration with event loops. Returns -1 if not watching.
    [[nodiscard]] int getInotifyFd() const noexcept;

    /// Register a callback invoked after every successful reload.
    void onConfigChange(ConfigChangeCallback callback);

    /// Register a callback for parse errors during reload.
    void onConfigError(ConfigErrorCallback callback);

    // -- Runtime option get/set (Task 70) -----------------------------------

    /// Set a config option at runtime (no file write). Returns true on success.
    bool setOption(const std::string& path, const RuntimeOptionValue& value);

    /// Get a config option value. Returns monostate if not found.
    [[nodiscard]] RuntimeOptionValue getOption(const std::string& path) const;

    /// List all gettable option paths.
    [[nodiscard]] std::vector<std::string> listOptions() const;

    // -- Accessors ----------------------------------------------------------

    [[nodiscard]] const GeneralConfig& getGeneral() const noexcept;
    [[nodiscard]] const DecorationConfig& getDecoration() const noexcept;
    [[nodiscard]] const AnimationConfig& getAnimation() const noexcept;
    [[nodiscard]] const InputConfig& getInput() const noexcept;
    [[nodiscard]] const BindConfig& getBinds() const noexcept;
    [[nodiscard]] const GestureConfig& getGestures() const noexcept;
    [[nodiscard]] const std::vector<MonitorConfig>& getMonitors() const noexcept;
    [[nodiscard]] const std::vector<WindowRuleConfig>& getWindowRules() const noexcept;
    [[nodiscard]] const std::vector<WorkspaceRuleConfig>& getWorkspaceRules() const noexcept;
    [[nodiscard]] const std::vector<PluginConfig>& getPlugins() const noexcept;
    [[nodiscard]] const EnvironmentConfig& getEnvironment() const noexcept;
    [[nodiscard]] const ExecConfig& getExec() const noexcept;
    [[nodiscard]] const XWaylandConfig& getXWayland() const noexcept;
    [[nodiscard]] const PermissionsConfig& getPermissions() const noexcept;
    [[nodiscard]] const DebugConfig& getDebug() const noexcept;

    [[nodiscard]] const std::filesystem::path& getConfigPath() const noexcept;

private:
    void applyDefaults();
    void parseDocument();

    void parseGeneral();
    void parseDecoration();
    void parseAnimation();
    void parseInput();
    void parseBinds();
    void parseGestures();
    void parseMonitors();
    void parseWindowRules();
    void parseWorkspaceRules();
    void parsePlugins();
    void parseEnvironment();
    void parseExec();
    void parseXWayland();
    void parsePermissions();
    void parseDebug();

    struct Impl;
    std::unique_ptr<Impl> impl_;

    std::filesystem::path config_path_;
    mutable std::mutex mutex_;

    GeneralConfig general_;
    DecorationConfig decoration_;
    AnimationConfig animation_;
    InputConfig input_;
    BindConfig binds_;
    GestureConfig gestures_;
    std::vector<MonitorConfig> monitors_;
    std::vector<WindowRuleConfig> window_rules_;
    std::vector<WorkspaceRuleConfig> workspace_rules_;
    std::vector<PluginConfig> plugins_;
    EnvironmentConfig environment_;
    ExecConfig exec_;
    XWaylandConfig xwayland_;
    PermissionsConfig permissions_;
    DebugConfig debug_;

    std::vector<ConfigChangeCallback> change_callbacks_;
    std::vector<ConfigErrorCallback> error_callbacks_;
};

} // namespace eternal

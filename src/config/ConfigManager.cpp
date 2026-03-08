#include "eternal/config/ConfigManager.hpp"
#include "eternal/config/KDLParser.hpp"
#include "eternal/utils/Logger.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <regex>
#include <stdexcept>

#ifdef __linux__
#include <poll.h>
#include <sys/inotify.h>
#include <unistd.h>
#endif

namespace eternal {

// ===========================================================================
// Helpers
// ===========================================================================

namespace {

std::string getString(const KDLValue& val, const std::string& fallback = "") {
    if (auto* s = std::get_if<std::string>(&val))
        return *s;
    return fallback;
}

int64_t getInt(const KDLValue& val, int64_t fallback = 0) {
    if (auto* i = std::get_if<int64_t>(&val))
        return *i;
    if (auto* d = std::get_if<double>(&val))
        return static_cast<int64_t>(*d);
    return fallback;
}

double getDouble(const KDLValue& val, double fallback = 0.0) {
    if (auto* d = std::get_if<double>(&val))
        return *d;
    if (auto* i = std::get_if<int64_t>(&val))
        return static_cast<double>(*i);
    return fallback;
}

bool getBool(const KDLValue& val, bool fallback = false) {
    if (auto* b = std::get_if<bool>(&val))
        return *b;
    return fallback;
}

float getFloat(const KDLValue& val, float fallback = 0.0f) {
    return static_cast<float>(getDouble(val, static_cast<double>(fallback)));
}

KDLValue getProp(const KDLNode& node, const std::string& key) {
    auto it = node.properties.find(key);
    if (it != node.properties.end())
        return it->second;
    return std::monostate{};
}

std::string firstArgString(const KDLNode& node, const std::string& fallback = "") {
    if (!node.arguments.empty())
        return getString(node.arguments[0], fallback);
    return fallback;
}

int64_t firstArgInt(const KDLNode& node, int64_t fallback = 0) {
    if (!node.arguments.empty())
        return getInt(node.arguments[0], fallback);
    return fallback;
}

double firstArgDouble(const KDLNode& node, double fallback = 0.0) {
    if (!node.arguments.empty())
        return getDouble(node.arguments[0], fallback);
    return fallback;
}

bool firstArgBool(const KDLNode& node, bool fallback = false) {
    if (!node.arguments.empty())
        return getBool(node.arguments[0], fallback);
    return fallback;
}

/// Parse modifier string like "SUPER,SHIFT" into a bitmask
uint32_t parseMods(const std::string& mods) {
    uint32_t mask = 0;
    auto upper = mods;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    if (upper.find("SUPER") != std::string::npos) mask |= (1u << 0);
    if (upper.find("SHIFT") != std::string::npos) mask |= (1u << 1);
    if (upper.find("CTRL") != std::string::npos || upper.find("CONTROL") != std::string::npos)
        mask |= (1u << 2);
    if (upper.find("ALT") != std::string::npos) mask |= (1u << 3);
    if (upper.find("MOD2") != std::string::npos) mask |= (1u << 4);
    if (upper.find("MOD3") != std::string::npos) mask |= (1u << 5);
    if (upper.find("MOD5") != std::string::npos) mask |= (1u << 6);
    return mask;
}

/// Parse bind flags from a string
BindFlag parseBindFlags(const std::string& flags) {
    BindFlag result = BindFlag::None;
    auto lower = flags;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower.find("locked") != std::string::npos)       result = result | BindFlag::Locked;
    if (lower.find("release") != std::string::npos)      result = result | BindFlag::Release;
    if (lower.find("repeat") != std::string::npos)       result = result | BindFlag::Repeat;
    if (lower.find("mouse") != std::string::npos)        result = result | BindFlag::Mouse;
    if (lower.find("nonconsuming") != std::string::npos) result = result | BindFlag::NonConsuming;
    if (lower.find("transparent") != std::string::npos)  result = result | BindFlag::Transparent;
    if (lower.find("ignoremods") != std::string::npos)   result = result | BindFlag::IgnoreMods;
    return result;
}

} // anonymous namespace

// ===========================================================================
// ConfigManager::Impl (inotify state)
// ===========================================================================

struct ConfigManager::Impl {
#ifdef __linux__
    int inotify_fd = -1;
    int watch_fd = -1;
#endif
    KDLDocument document;
    // Track whether first load has happened (for exec_once vs exec)
    bool initial_load_done = false;
};

// ===========================================================================
// ConfigManager construction / destruction
// ===========================================================================

ConfigManager::ConfigManager()
    : impl_(std::make_unique<Impl>()) {
    applyDefaults();
}

ConfigManager::~ConfigManager() {
    stopWatching();
}

// ===========================================================================
// Loading
// ===========================================================================

void ConfigManager::load(const std::filesystem::path& path) {
    std::lock_guard lock(mutex_);
    config_path_ = path;
    impl_->document = KDLParser::parseFile(path);
    applyDefaults();
    parseDocument();
    impl_->initial_load_done = true;
}

bool ConfigManager::reload() {
    if (config_path_.empty())
        return false;

    try {
        auto newDoc = KDLParser::parseFile(config_path_);

        // Atomically swap: parse into temporaries, then move
        {
            std::lock_guard lock(mutex_);
            impl_->document = std::move(newDoc);
            applyDefaults();
            parseDocument();
        }

        LOG_INFO("Config reloaded successfully from {}", config_path_.string());

        // Notify listeners
        for (const auto& cb : change_callbacks_)
            cb();

        return true;
    } catch (const KDLParseError& e) {
        std::string errMsg = e.what();
        LOG_ERROR("Config reload failed: {}", errMsg);

        // Notify error listeners
        for (const auto& cb : error_callbacks_)
            cb(errMsg);

        return false;
    } catch (const std::exception& e) {
        std::string errMsg = e.what();
        LOG_ERROR("Config reload failed: {}", errMsg);

        for (const auto& cb : error_callbacks_)
            cb(errMsg);

        return false;
    }
}

// ===========================================================================
// File watching (inotify) -- Task 62
// ===========================================================================

void ConfigManager::watchFile() {
#ifdef __linux__
    if (config_path_.empty())
        return;

    stopWatching();

    impl_->inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (impl_->inotify_fd < 0) {
        LOG_ERROR("ConfigManager: inotify_init1 failed: {}", strerror(errno));
        return;
    }

    // Watch the parent directory too (handles editors that rename+write)
    auto parent = config_path_.parent_path();
    impl_->watch_fd = inotify_add_watch(
        impl_->inotify_fd,
        config_path_.c_str(),
        IN_MODIFY | IN_CLOSE_WRITE | IN_MOVE_SELF);

    if (impl_->watch_fd < 0) {
        LOG_ERROR("ConfigManager: inotify_add_watch failed on {}: {}",
                  config_path_.string(), strerror(errno));
        // Try watching parent directory instead
        impl_->watch_fd = inotify_add_watch(
            impl_->inotify_fd,
            parent.c_str(),
            IN_MODIFY | IN_CLOSE_WRITE | IN_CREATE | IN_MOVED_TO);
        if (impl_->watch_fd < 0) {
            LOG_ERROR("ConfigManager: inotify_add_watch failed on {}: {}",
                      parent.string(), strerror(errno));
            ::close(impl_->inotify_fd);
            impl_->inotify_fd = -1;
            return;
        }
    }

    LOG_INFO("Watching config file: {}", config_path_.string());
#endif
}

void ConfigManager::stopWatching() {
#ifdef __linux__
    if (impl_->watch_fd >= 0 && impl_->inotify_fd >= 0) {
        inotify_rm_watch(impl_->inotify_fd, impl_->watch_fd);
        impl_->watch_fd = -1;
    }
    if (impl_->inotify_fd >= 0) {
        ::close(impl_->inotify_fd);
        impl_->inotify_fd = -1;
    }
#endif
}

int ConfigManager::getInotifyFd() const noexcept {
#ifdef __linux__
    return impl_->inotify_fd;
#else
    return -1;
#endif
}

void ConfigManager::processPendingEvents() {
#ifdef __linux__
    if (impl_->inotify_fd < 0)
        return;

    struct pollfd pfd {};
    pfd.fd = impl_->inotify_fd;
    pfd.events = POLLIN;

    if (::poll(&pfd, 1, 0) <= 0)
        return;

    // Drain the inotify buffer
    alignas(struct inotify_event) char buf[4096];
    bool should_reload = false;
    while (true) {
        ssize_t n = ::read(impl_->inotify_fd, buf, sizeof(buf));
        if (n <= 0)
            break;

        // Check if the event is relevant (when watching parent dir)
        const char* ptr = buf;
        while (ptr < buf + n) {
            const auto* event = reinterpret_cast<const struct inotify_event*>(ptr);
            if (event->len > 0) {
                std::string name(event->name);
                if (name == config_path_.filename().string()) {
                    should_reload = true;
                }
            } else {
                // Direct file watch -- always relevant
                should_reload = true;
            }
            ptr += sizeof(struct inotify_event) + event->len;
        }
    }

    if (should_reload) {
        // Re-establish watch if file was moved/replaced
        if (impl_->watch_fd >= 0) {
            auto test_wd = inotify_add_watch(
                impl_->inotify_fd,
                config_path_.c_str(),
                IN_MODIFY | IN_CLOSE_WRITE | IN_MOVE_SELF);
            if (test_wd >= 0 && test_wd != impl_->watch_fd) {
                inotify_rm_watch(impl_->inotify_fd, impl_->watch_fd);
                impl_->watch_fd = test_wd;
            }
        }

        reload(); // reload() handles errors internally
    }
#endif
}

void ConfigManager::onConfigChange(ConfigChangeCallback callback) {
    change_callbacks_.push_back(std::move(callback));
}

void ConfigManager::onConfigError(ConfigErrorCallback callback) {
    error_callbacks_.push_back(std::move(callback));
}

// ===========================================================================
// Runtime option get/set -- Task 70
// ===========================================================================

bool ConfigManager::setOption(const std::string& path, const RuntimeOptionValue& value) {
    std::lock_guard lock(mutex_);

    // general.* options
    if (path == "general:border_size") {
        if (auto* v = std::get_if<int64_t>(&value)) { general_.border_size = static_cast<int>(*v); return true; }
    } else if (path == "general:gaps_in") {
        if (auto* v = std::get_if<int64_t>(&value)) { general_.gaps_in = static_cast<int>(*v); return true; }
    } else if (path == "general:gaps_out") {
        if (auto* v = std::get_if<int64_t>(&value)) { general_.gaps_out = static_cast<int>(*v); return true; }
    } else if (path == "general:focus_follows_mouse") {
        if (auto* v = std::get_if<bool>(&value)) { general_.focus_follows_mouse = *v; return true; }
    } else if (path == "general:layout") {
        if (auto* v = std::get_if<std::string>(&value)) { general_.layout = *v; return true; }
    } else if (path == "general:cursor_theme") {
        if (auto* v = std::get_if<std::string>(&value)) { general_.cursor_theme = *v; return true; }
    } else if (path == "general:cursor_size") {
        if (auto* v = std::get_if<int64_t>(&value)) { general_.cursor_size = static_cast<int>(*v); return true; }
    } else if (path == "general:vrr") {
        if (auto* v = std::get_if<int64_t>(&value)) { general_.vrr = static_cast<int>(*v); return true; }
    } else if (path == "general:allow_tearing") {
        if (auto* v = std::get_if<bool>(&value)) { general_.allow_tearing = *v; return true; }
    } else if (path == "general:resize_on_border") {
        if (auto* v = std::get_if<bool>(&value)) { general_.resize_on_border = *v; return true; }
    }
    // decoration.* options
    else if (path == "decoration:rounding") {
        if (auto* v = std::get_if<int64_t>(&value)) { decoration_.rounding = static_cast<int>(*v); return true; }
    } else if (path == "decoration:active_opacity") {
        if (auto* v = std::get_if<double>(&value)) { decoration_.active_opacity = static_cast<float>(*v); return true; }
    } else if (path == "decoration:inactive_opacity") {
        if (auto* v = std::get_if<double>(&value)) { decoration_.inactive_opacity = static_cast<float>(*v); return true; }
    } else if (path == "decoration:dim_inactive") {
        if (auto* v = std::get_if<bool>(&value)) { decoration_.dim_inactive = *v; return true; }
    } else if (path == "decoration:dim_strength") {
        if (auto* v = std::get_if<double>(&value)) { decoration_.dim_strength = static_cast<float>(*v); return true; }
    } else if (path == "decoration:shadow:enabled") {
        if (auto* v = std::get_if<bool>(&value)) { decoration_.shadow.enabled = *v; return true; }
    } else if (path == "decoration:blur:enabled") {
        if (auto* v = std::get_if<bool>(&value)) { decoration_.blur.enabled = *v; return true; }
    } else if (path == "decoration:blur:size") {
        if (auto* v = std::get_if<int64_t>(&value)) { decoration_.blur.size = static_cast<int>(*v); return true; }
    } else if (path == "decoration:blur:passes") {
        if (auto* v = std::get_if<int64_t>(&value)) { decoration_.blur.passes = static_cast<int>(*v); return true; }
    } else if (path == "decoration:border:enabled") {
        if (auto* v = std::get_if<bool>(&value)) { decoration_.border.enabled = *v; return true; }
    }
    // animation.* options
    else if (path == "animation:enabled") {
        if (auto* v = std::get_if<bool>(&value)) { animation_.enabled = *v; return true; }
    } else if (path == "animation:speed_multiplier") {
        if (auto* v = std::get_if<double>(&value)) { animation_.speed_multiplier = static_cast<float>(*v); return true; }
    }
    // input.* options
    else if (path == "input:follow_mouse") {
        if (auto* v = std::get_if<bool>(&value)) { input_.follow_mouse = *v; return true; }
    } else if (path == "input:keyboard:layout") {
        if (auto* v = std::get_if<std::string>(&value)) { input_.keyboard.layout = *v; return true; }
    } else if (path == "input:keyboard:repeat_rate") {
        if (auto* v = std::get_if<int64_t>(&value)) { input_.keyboard.repeat_rate = static_cast<int>(*v); return true; }
    } else if (path == "input:keyboard:repeat_delay") {
        if (auto* v = std::get_if<int64_t>(&value)) { input_.keyboard.repeat_delay = static_cast<int>(*v); return true; }
    } else if (path == "input:pointer:sensitivity") {
        if (auto* v = std::get_if<double>(&value)) { input_.pointer.sensitivity = static_cast<float>(*v); return true; }
    } else if (path == "input:touchpad:natural_scroll") {
        if (auto* v = std::get_if<bool>(&value)) { input_.touchpad.natural_scroll = *v; return true; }
    } else if (path == "input:touchpad:tap_to_click") {
        if (auto* v = std::get_if<bool>(&value)) { input_.touchpad.tap_to_click = *v; return true; }
    }
    // debug.* options
    else if (path == "debug:overlay") {
        if (auto* v = std::get_if<bool>(&value)) { debug_.overlay = *v; return true; }
    } else if (path == "debug:damage_tracking") {
        if (auto* v = std::get_if<bool>(&value)) { debug_.damage_tracking = *v; return true; }
    } else if (path == "debug:log_level") {
        if (auto* v = std::get_if<int64_t>(&value)) { debug_.log_level = static_cast<int>(*v); return true; }
    }

    LOG_WARN("Unknown or type-mismatched config option: {}", path);
    return false;
}

RuntimeOptionValue ConfigManager::getOption(const std::string& path) const {
    std::lock_guard lock(mutex_);

    // general.*
    if (path == "general:border_size") return static_cast<int64_t>(general_.border_size);
    if (path == "general:gaps_in") return static_cast<int64_t>(general_.gaps_in);
    if (path == "general:gaps_out") return static_cast<int64_t>(general_.gaps_out);
    if (path == "general:focus_follows_mouse") return general_.focus_follows_mouse;
    if (path == "general:layout") return general_.layout;
    if (path == "general:cursor_theme") return general_.cursor_theme;
    if (path == "general:cursor_size") return static_cast<int64_t>(general_.cursor_size);
    if (path == "general:vrr") return static_cast<int64_t>(general_.vrr);
    if (path == "general:allow_tearing") return general_.allow_tearing;
    if (path == "general:resize_on_border") return general_.resize_on_border;
    if (path == "general:extend_border_grab_area") return static_cast<int64_t>(general_.extend_border_grab_area);
    if (path == "general:hover_icon_on_border") return general_.hover_icon_on_border;
    if (path == "general:no_focus_fallback") return general_.no_focus_fallback;
    if (path == "general:close_special_on_empty") return general_.close_special_on_empty;
    if (path == "general:new_window_takes_over_fullscreen") return general_.new_window_takes_over_fullscreen;

    // decoration.*
    if (path == "decoration:rounding") return static_cast<int64_t>(decoration_.rounding);
    if (path == "decoration:active_opacity") return static_cast<double>(decoration_.active_opacity);
    if (path == "decoration:inactive_opacity") return static_cast<double>(decoration_.inactive_opacity);
    if (path == "decoration:dim_inactive") return decoration_.dim_inactive;
    if (path == "decoration:dim_strength") return static_cast<double>(decoration_.dim_strength);
    if (path == "decoration:shadow:enabled") return decoration_.shadow.enabled;
    if (path == "decoration:blur:enabled") return decoration_.blur.enabled;
    if (path == "decoration:blur:size") return static_cast<int64_t>(decoration_.blur.size);
    if (path == "decoration:blur:passes") return static_cast<int64_t>(decoration_.blur.passes);
    if (path == "decoration:border:enabled") return decoration_.border.enabled;

    // animation.*
    if (path == "animation:enabled") return animation_.enabled;
    if (path == "animation:speed_multiplier") return static_cast<double>(animation_.speed_multiplier);

    // input.*
    if (path == "input:follow_mouse") return input_.follow_mouse;
    if (path == "input:keyboard:layout") return input_.keyboard.layout;
    if (path == "input:keyboard:repeat_rate") return static_cast<int64_t>(input_.keyboard.repeat_rate);
    if (path == "input:keyboard:repeat_delay") return static_cast<int64_t>(input_.keyboard.repeat_delay);
    if (path == "input:pointer:sensitivity") return static_cast<double>(input_.pointer.sensitivity);
    if (path == "input:touchpad:natural_scroll") return input_.touchpad.natural_scroll;
    if (path == "input:touchpad:tap_to_click") return input_.touchpad.tap_to_click;

    // debug.*
    if (path == "debug:overlay") return debug_.overlay;
    if (path == "debug:damage_tracking") return debug_.damage_tracking;
    if (path == "debug:log_level") return static_cast<int64_t>(debug_.log_level);

    return std::monostate{};
}

std::vector<std::string> ConfigManager::listOptions() const {
    return {
        "general:border_size", "general:gaps_in", "general:gaps_out",
        "general:focus_follows_mouse", "general:layout", "general:cursor_theme",
        "general:cursor_size", "general:vrr", "general:allow_tearing",
        "general:resize_on_border", "general:extend_border_grab_area",
        "general:hover_icon_on_border", "general:no_focus_fallback",
        "general:close_special_on_empty", "general:new_window_takes_over_fullscreen",
        "decoration:rounding", "decoration:active_opacity", "decoration:inactive_opacity",
        "decoration:dim_inactive", "decoration:dim_strength",
        "decoration:shadow:enabled", "decoration:blur:enabled",
        "decoration:blur:size", "decoration:blur:passes", "decoration:border:enabled",
        "animation:enabled", "animation:speed_multiplier",
        "input:follow_mouse", "input:keyboard:layout",
        "input:keyboard:repeat_rate", "input:keyboard:repeat_delay",
        "input:pointer:sensitivity", "input:touchpad:natural_scroll",
        "input:touchpad:tap_to_click",
        "debug:overlay", "debug:damage_tracking", "debug:log_level",
    };
}

// ===========================================================================
// Accessors
// ===========================================================================

const GeneralConfig& ConfigManager::getGeneral() const noexcept { return general_; }
const DecorationConfig& ConfigManager::getDecoration() const noexcept { return decoration_; }
const AnimationConfig& ConfigManager::getAnimation() const noexcept { return animation_; }
const InputConfig& ConfigManager::getInput() const noexcept { return input_; }
const BindConfig& ConfigManager::getBinds() const noexcept { return binds_; }
const GestureConfig& ConfigManager::getGestures() const noexcept { return gestures_; }
const std::vector<MonitorConfig>& ConfigManager::getMonitors() const noexcept { return monitors_; }
const std::vector<WindowRuleConfig>& ConfigManager::getWindowRules() const noexcept { return window_rules_; }
const std::vector<WorkspaceRuleConfig>& ConfigManager::getWorkspaceRules() const noexcept { return workspace_rules_; }
const std::vector<PluginConfig>& ConfigManager::getPlugins() const noexcept { return plugins_; }
const EnvironmentConfig& ConfigManager::getEnvironment() const noexcept { return environment_; }
const ExecConfig& ConfigManager::getExec() const noexcept { return exec_; }
const XWaylandConfig& ConfigManager::getXWayland() const noexcept { return xwayland_; }
const PermissionsConfig& ConfigManager::getPermissions() const noexcept { return permissions_; }
const DebugConfig& ConfigManager::getDebug() const noexcept { return debug_; }
const std::filesystem::path& ConfigManager::getConfigPath() const noexcept { return config_path_; }

// ===========================================================================
// Default values
// ===========================================================================

void ConfigManager::applyDefaults() {
    general_ = GeneralConfig{};
    decoration_ = DecorationConfig{};
    animation_ = AnimationConfig{};
    input_ = InputConfig{};
    binds_ = BindConfig{};
    gestures_ = GestureConfig{};
    monitors_.clear();
    window_rules_.clear();
    workspace_rules_.clear();
    plugins_.clear();
    environment_ = EnvironmentConfig{};
    exec_ = ExecConfig{};
    xwayland_ = XWaylandConfig{};
    permissions_ = PermissionsConfig{};
    debug_ = DebugConfig{};

    // Register default bezier curves
    animation_.bezier_curves["default"] = BezierCurve{"default", 0.25f, 0.1f, 0.25f, 1.0f};
    animation_.bezier_curves["linear"] = BezierCurve{"linear", 0.0f, 0.0f, 1.0f, 1.0f};
    animation_.bezier_curves["ease"] = BezierCurve{"ease", 0.25f, 0.1f, 0.25f, 1.0f};
    animation_.bezier_curves["easeIn"] = BezierCurve{"easeIn", 0.42f, 0.0f, 1.0f, 1.0f};
    animation_.bezier_curves["easeOut"] = BezierCurve{"easeOut", 0.0f, 0.0f, 0.58f, 1.0f};
    animation_.bezier_curves["easeInOut"] = BezierCurve{"easeInOut", 0.42f, 0.0f, 0.58f, 1.0f};
}

// ===========================================================================
// Document parsing -- dispatch to section parsers
// ===========================================================================

void ConfigManager::parseDocument() {
    const auto& doc = impl_->document;

    if (doc.getNode("general"))     parseGeneral();
    if (doc.getNode("decoration"))  parseDecoration();
    if (doc.getNode("animation") || doc.getNode("animations"))  parseAnimation();
    if (doc.getNode("input"))       parseInput();
    if (doc.getNode("bind") || doc.getNode("binds"))  parseBinds();
    if (doc.getNode("gestures"))    parseGestures();

    if (!doc.getNodes("monitor").empty())          parseMonitors();
    if (!doc.getNodes("windowrule").empty() ||
        !doc.getNodes("window-rule").empty())       parseWindowRules();
    if (!doc.getNodes("workspacerule").empty())    parseWorkspaceRules();
    if (!doc.getNodes("plugin").empty())           parsePlugins();

    if (doc.getNode("env"))           parseEnvironment();
    if (doc.getNode("exec"))          parseExec();
    if (doc.getNode("xwayland"))      parseXWayland();
    if (doc.getNode("permissions"))   parsePermissions();
    if (doc.getNode("debug"))         parseDebug();
}

// ===========================================================================
// Section parsers -- Task 57: General
// ===========================================================================

void ConfigManager::parseGeneral() {
    const auto* node = impl_->document.getNode("general");
    if (!node) return;

    for (const auto& child : node->children) {
        const auto& n = child.name;
        if (n == "layout")               general_.layout = firstArgString(child, general_.layout);
        else if (n == "border_size")     general_.border_size = static_cast<int>(firstArgInt(child, general_.border_size));
        else if (n == "gaps_in")         general_.gaps_in = static_cast<int>(firstArgInt(child, general_.gaps_in));
        else if (n == "gaps_out")        general_.gaps_out = static_cast<int>(firstArgInt(child, general_.gaps_out));
        else if (n == "focus_follows_mouse") general_.focus_follows_mouse = firstArgBool(child, general_.focus_follows_mouse);
        else if (n == "cursor_theme")    general_.cursor_theme = firstArgString(child, general_.cursor_theme);
        else if (n == "cursor_size")     general_.cursor_size = static_cast<int>(firstArgInt(child, general_.cursor_size));
        else if (n == "vrr")             general_.vrr = static_cast<int>(firstArgInt(child, general_.vrr));
        else if (n == "allow_tearing")   general_.allow_tearing = firstArgBool(child, general_.allow_tearing);
        else if (n == "resize_on_border") general_.resize_on_border = firstArgBool(child, general_.resize_on_border);
        else if (n == "extend_border_grab_area") general_.extend_border_grab_area = static_cast<int>(firstArgInt(child, general_.extend_border_grab_area));
        else if (n == "hover_icon_on_border") general_.hover_icon_on_border = firstArgBool(child, general_.hover_icon_on_border);
        else if (n == "no_focus_fallback") general_.no_focus_fallback = firstArgBool(child, general_.no_focus_fallback);
        else if (n == "close_special_on_empty") general_.close_special_on_empty = firstArgBool(child, general_.close_special_on_empty);
        else if (n == "new_window_takes_over_fullscreen") general_.new_window_takes_over_fullscreen = firstArgBool(child, general_.new_window_takes_over_fullscreen);
    }

    // Also support properties directly on the general node
    for (const auto& [key, val] : node->properties) {
        if (key == "layout") general_.layout = getString(val, general_.layout);
        else if (key == "border_size") general_.border_size = static_cast<int>(getInt(val, general_.border_size));
        else if (key == "gaps_in") general_.gaps_in = static_cast<int>(getInt(val, general_.gaps_in));
        else if (key == "gaps_out") general_.gaps_out = static_cast<int>(getInt(val, general_.gaps_out));
        else if (key == "focus_follows_mouse") general_.focus_follows_mouse = getBool(val, general_.focus_follows_mouse);
    }
}

void ConfigManager::parseDecoration() {
    const auto* node = impl_->document.getNode("decoration");
    if (!node) return;

    for (const auto& child : node->children) {
        const auto& n = child.name;
        if (n == "rounding")           decoration_.rounding = static_cast<int>(firstArgInt(child, decoration_.rounding));
        else if (n == "active_opacity")  decoration_.active_opacity = static_cast<float>(firstArgDouble(child, decoration_.active_opacity));
        else if (n == "inactive_opacity") decoration_.inactive_opacity = static_cast<float>(firstArgDouble(child, decoration_.inactive_opacity));
        else if (n == "dim_inactive")    decoration_.dim_inactive = firstArgBool(child, decoration_.dim_inactive);
        else if (n == "dim_strength")    decoration_.dim_strength = static_cast<float>(firstArgDouble(child, decoration_.dim_strength));
        else if (n == "shadow") {
            for (const auto& sc : child.children) {
                const auto& sn = sc.name;
                if (sn == "enabled")        decoration_.shadow.enabled = firstArgBool(sc, decoration_.shadow.enabled);
                else if (sn == "range")     decoration_.shadow.range = static_cast<int>(firstArgInt(sc, decoration_.shadow.range));
                else if (sn == "render_power") decoration_.shadow.render_power = static_cast<int>(firstArgInt(sc, decoration_.shadow.render_power));
                else if (sn == "opacity")   decoration_.shadow.opacity = static_cast<float>(firstArgDouble(sc, decoration_.shadow.opacity));
                else if (sn == "color")     decoration_.shadow.color = firstArgString(sc, decoration_.shadow.color);
                else if (sn == "color_inactive") decoration_.shadow.color_inactive = firstArgString(sc, decoration_.shadow.color_inactive);
                else if (sn == "offset_x")  decoration_.shadow.offset_x = static_cast<int>(firstArgInt(sc, decoration_.shadow.offset_x));
                else if (sn == "offset_y")  decoration_.shadow.offset_y = static_cast<int>(firstArgInt(sc, decoration_.shadow.offset_y));
                else if (sn == "scale")     decoration_.shadow.scale = static_cast<float>(firstArgDouble(sc, decoration_.shadow.scale));
            }
        } else if (n == "blur") {
            for (const auto& bc : child.children) {
                const auto& bn = bc.name;
                if (bn == "enabled")           decoration_.blur.enabled = firstArgBool(bc, decoration_.blur.enabled);
                else if (bn == "size")         decoration_.blur.size = static_cast<int>(firstArgInt(bc, decoration_.blur.size));
                else if (bn == "passes")       decoration_.blur.passes = static_cast<int>(firstArgInt(bc, decoration_.blur.passes));
                else if (bn == "noise")        decoration_.blur.noise = firstArgBool(bc, decoration_.blur.noise);
                else if (bn == "noise_intensity") decoration_.blur.noise_intensity = static_cast<float>(firstArgDouble(bc, decoration_.blur.noise_intensity));
                else if (bn == "contrast")     decoration_.blur.contrast = static_cast<float>(firstArgDouble(bc, decoration_.blur.contrast));
                else if (bn == "brightness")   decoration_.blur.brightness = static_cast<float>(firstArgDouble(bc, decoration_.blur.brightness));
                else if (bn == "vibrancy")     decoration_.blur.vibrancy = static_cast<float>(firstArgDouble(bc, decoration_.blur.vibrancy));
                else if (bn == "vibrancy_darkness") decoration_.blur.vibrancy_darkness = static_cast<float>(firstArgDouble(bc, decoration_.blur.vibrancy_darkness));
                else if (bn == "xray")         decoration_.blur.xray = firstArgBool(bc, decoration_.blur.xray);
                else if (bn == "popups")       decoration_.blur.popups = firstArgBool(bc, decoration_.blur.popups);
                else if (bn == "special")      decoration_.blur.special = firstArgBool(bc, decoration_.blur.special);
                else if (bn == "new_optimizations") decoration_.blur.new_optimizations = firstArgBool(bc, decoration_.blur.new_optimizations);
            }
        } else if (n == "border") {
            for (const auto& bc : child.children) {
                const auto& bn = bc.name;
                if (bn == "enabled")           decoration_.border.enabled = firstArgBool(bc, decoration_.border.enabled);
                else if (bn == "gradient") {
                    decoration_.border.gradient.colors_active.clear();
                    decoration_.border.gradient.colors_inactive.clear();
                    for (const auto& gc : bc.children) {
                        if (gc.name == "active") {
                            for (const auto& arg : gc.arguments)
                                decoration_.border.gradient.colors_active.push_back(getString(arg));
                        } else if (gc.name == "inactive") {
                            for (const auto& arg : gc.arguments)
                                decoration_.border.gradient.colors_inactive.push_back(getString(arg));
                        } else if (gc.name == "angle") {
                            decoration_.border.gradient.angle = static_cast<int>(firstArgInt(gc, decoration_.border.gradient.angle));
                        }
                    }
                }
            }
        }
    }
}

// ===========================================================================
// Task 60: Parse animation curves
// ===========================================================================

void ConfigManager::parseAnimation() {
    const auto* node = impl_->document.getNode("animation");
    if (!node) node = impl_->document.getNode("animations");
    if (!node) return;

    auto parseAnimDef = [](const KDLNode& parent, AnimationDef& def) {
        for (const auto& child : parent.children) {
            const auto& n = child.name;
            if (n == "enabled")      def.enabled = firstArgBool(child, def.enabled);
            else if (n == "speed")   def.speed = static_cast<float>(firstArgDouble(child, def.speed));
            else if (n == "bezier")  def.bezier = firstArgString(child, def.bezier);
            else if (n == "style")   def.style = firstArgString(child, def.style);
            else if (n == "style_param") def.style_param = static_cast<float>(firstArgDouble(child, def.style_param));
        }
        // Also accept inline: window_open 1.0 "slide" bezier="myBezier"
        if (parent.arguments.size() >= 1)
            def.speed = getFloat(parent.arguments[0], def.speed);
        if (parent.arguments.size() >= 2)
            def.style = getString(parent.arguments[1], def.style);
        auto bezProp = getProp(parent, "bezier");
        if (auto* bs = std::get_if<std::string>(&bezProp))
            def.bezier = *bs;
        auto enabledProp = getProp(parent, "enabled");
        if (auto* eb = std::get_if<bool>(&enabledProp))
            def.enabled = *eb;
    };

    for (const auto& child : node->children) {
        const auto& n = child.name;
        if (n == "enabled")            animation_.enabled = firstArgBool(child, animation_.enabled);
        else if (n == "speed_multiplier") animation_.speed_multiplier = static_cast<float>(firstArgDouble(child, animation_.speed_multiplier));
        else if (n == "bezier") {
            // bezier "name" x1 y1 x2 y2
            if (child.arguments.size() >= 5) {
                BezierCurve curve;
                curve.name = getString(child.arguments[0]);
                curve.x1 = getFloat(child.arguments[1]);
                curve.y1 = getFloat(child.arguments[2]);
                curve.x2 = getFloat(child.arguments[3]);
                curve.y2 = getFloat(child.arguments[4]);
                animation_.bezier_curves[curve.name] = curve;
            }
            // Also support properties: bezier name=x1=... format
            // bezier { name "ease-custom"; points 0.2 0.0 0.8 1.0 }
            if (!child.children.empty()) {
                BezierCurve curve;
                for (const auto& bc : child.children) {
                    if (bc.name == "name") curve.name = firstArgString(bc);
                    else if (bc.name == "points" && bc.arguments.size() >= 4) {
                        curve.x1 = getFloat(bc.arguments[0]);
                        curve.y1 = getFloat(bc.arguments[1]);
                        curve.x2 = getFloat(bc.arguments[2]);
                        curve.y2 = getFloat(bc.arguments[3]);
                    }
                }
                if (!curve.name.empty())
                    animation_.bezier_curves[curve.name] = curve;
            }
        }
        else if (n == "window_open" || n == "window-open")   parseAnimDef(child, animation_.window_open);
        else if (n == "window_close" || n == "window-close")  parseAnimDef(child, animation_.window_close);
        else if (n == "window_move" || n == "window-move")   parseAnimDef(child, animation_.window_move);
        else if (n == "window_resize" || n == "window-resize") parseAnimDef(child, animation_.window_resize);
        else if (n == "fade_in" || n == "fade-in")       parseAnimDef(child, animation_.fade_in);
        else if (n == "fade_out" || n == "fade-out")      parseAnimDef(child, animation_.fade_out);
        else if (n == "workspace_switch" || n == "workspace-switch") parseAnimDef(child, animation_.workspace_switch);
        else if (n == "spring") {
            for (const auto& sc : child.children) {
                if (sc.name == "stiffness")  animation_.spring.stiffness = static_cast<float>(firstArgDouble(sc, animation_.spring.stiffness));
                else if (sc.name == "damping") animation_.spring.damping = static_cast<float>(firstArgDouble(sc, animation_.spring.damping));
                else if (sc.name == "mass")  animation_.spring.mass = static_cast<float>(firstArgDouble(sc, animation_.spring.mass));
            }
            // Also accept inline: spring 200.0 20.0 1.0
            if (child.arguments.size() >= 1) animation_.spring.stiffness = getFloat(child.arguments[0], animation_.spring.stiffness);
            if (child.arguments.size() >= 2) animation_.spring.damping = getFloat(child.arguments[1], animation_.spring.damping);
            if (child.arguments.size() >= 3) animation_.spring.mass = getFloat(child.arguments[2], animation_.spring.mass);
        }
    }
}

// ===========================================================================
// Task 58: Parse input with per-device overrides
// ===========================================================================

void ConfigManager::parseInput() {
    const auto* node = impl_->document.getNode("input");
    if (!node) return;

    for (const auto& child : node->children) {
        const auto& n = child.name;
        if (n == "follow_mouse")       input_.follow_mouse = firstArgBool(child, input_.follow_mouse);
        else if (n == "float_switch_override") input_.float_switch_override = firstArgBool(child, input_.float_switch_override);
        else if (n == "keyboard") {
            for (const auto& kc : child.children) {
                const auto& kn = kc.name;
                if (kn == "rules")          input_.keyboard.rules = firstArgString(kc, input_.keyboard.rules);
                else if (kn == "model")     input_.keyboard.model = firstArgString(kc, input_.keyboard.model);
                else if (kn == "layout")    input_.keyboard.layout = firstArgString(kc, input_.keyboard.layout);
                else if (kn == "variant")   input_.keyboard.variant = firstArgString(kc, input_.keyboard.variant);
                else if (kn == "options")   input_.keyboard.options = firstArgString(kc, input_.keyboard.options);
                else if (kn == "repeat_rate")  input_.keyboard.repeat_rate = static_cast<int>(firstArgInt(kc, input_.keyboard.repeat_rate));
                else if (kn == "repeat_delay") input_.keyboard.repeat_delay = static_cast<int>(firstArgInt(kc, input_.keyboard.repeat_delay));
                else if (kn == "numlock_by_default") input_.keyboard.numlock_by_default = firstArgBool(kc, input_.keyboard.numlock_by_default);
                else if (kn == "capslock_by_default") input_.keyboard.capslock_by_default = firstArgBool(kc, input_.keyboard.capslock_by_default);
            }
        } else if (n == "touchpad") {
            for (const auto& tc : child.children) {
                const auto& tn = tc.name;
                if (tn == "disable_while_typing")  input_.touchpad.disable_while_typing = firstArgBool(tc, input_.touchpad.disable_while_typing);
                else if (tn == "natural_scroll")   input_.touchpad.natural_scroll = firstArgBool(tc, input_.touchpad.natural_scroll);
                else if (tn == "scroll_factor")    input_.touchpad.scroll_factor = static_cast<int>(firstArgInt(tc, input_.touchpad.scroll_factor));
                else if (tn == "middle_button_emulation") input_.touchpad.middle_button_emulation = firstArgBool(tc, input_.touchpad.middle_button_emulation);
                else if (tn == "tap_to_click")     input_.touchpad.tap_to_click = firstArgBool(tc, input_.touchpad.tap_to_click);
                else if (tn == "drag_lock")        input_.touchpad.drag_lock = firstArgBool(tc, input_.touchpad.drag_lock);
                else if (tn == "tap_and_drag")     input_.touchpad.tap_and_drag = firstArgBool(tc, input_.touchpad.tap_and_drag);
                else if (tn == "clickfinger_behavior") input_.touchpad.clickfinger_behavior = firstArgBool(tc, input_.touchpad.clickfinger_behavior);
                else if (tn == "tap_button_map")   input_.touchpad.tap_button_map = firstArgBool(tc, input_.touchpad.tap_button_map);
            }
        } else if (n == "pointer") {
            for (const auto& pc : child.children) {
                const auto& pn = pc.name;
                if (pn == "sensitivity")       input_.pointer.sensitivity = static_cast<float>(firstArgDouble(pc, input_.pointer.sensitivity));
                else if (pn == "accel_profile_flat") input_.pointer.accel_profile_flat = firstArgBool(pc, input_.pointer.accel_profile_flat);
                else if (pn == "natural_scroll") input_.pointer.natural_scroll = firstArgBool(pc, input_.pointer.natural_scroll);
                else if (pn == "scroll_factor")  input_.pointer.scroll_factor = static_cast<int>(firstArgInt(pc, input_.pointer.scroll_factor));
                else if (pn == "left_handed")    input_.pointer.left_handed = firstArgBool(pc, input_.pointer.left_handed);
            }
        } else if (n == "tablet") {
            for (const auto& tc : child.children) {
                const auto& tn = tc.name;
                if (tn == "transform")        input_.tablet.transform = firstArgString(tc, input_.tablet.transform);
                else if (tn == "output")      input_.tablet.output = firstArgString(tc, input_.tablet.output);
                else if (tn == "region_position") input_.tablet.region_position = firstArgString(tc, input_.tablet.region_position);
                else if (tn == "region_size") input_.tablet.region_size = firstArgString(tc, input_.tablet.region_size);
                else if (tn == "relative_input") input_.tablet.relative_input = firstArgBool(tc, input_.tablet.relative_input);
            }
        } else if (n == "device") {
            // Per-device override: device "device-name" { ... }
            InputDeviceOverride override;
            override.device_name = firstArgString(child);
            for (const auto& dc : child.children) {
                const auto& dn = dc.name;
                if (dn == "sensitivity")
                    override.sensitivity = static_cast<float>(firstArgDouble(dc));
                else if (dn == "accel_profile_flat")
                    override.accel_profile_flat = firstArgBool(dc);
                else if (dn == "natural_scroll")
                    override.natural_scroll = firstArgBool(dc);
                else if (dn == "scroll_factor")
                    override.scroll_factor = static_cast<int>(firstArgInt(dc));
                else if (dn == "left_handed")
                    override.left_handed = firstArgBool(dc);
                else if (dn == "tap_to_click")
                    override.tap_to_click = firstArgBool(dc);
                else if (dn == "disable_while_typing")
                    override.disable_while_typing = firstArgBool(dc);
                else if (dn == "middle_button_emulation")
                    override.middle_button_emulation = firstArgBool(dc);
                else if (dn == "drag_lock")
                    override.drag_lock = firstArgBool(dc);
            }
            input_.device_overrides.push_back(std::move(override));
        }
    }
}

// ===========================================================================
// Task 59: Parse keybindings (bind, bindm, bindl, bindr, bindt, submaps)
// ===========================================================================

void ConfigManager::parseBinds() {
    const auto& doc = impl_->document;
    binds_.keybinds.clear();

    auto parseSingleBind = [&](const KDLNode& child, BindFlag extraFlags,
                               const std::string& currentSubmap) {
        Keybind kb;
        kb.flags = extraFlags;
        kb.submap = currentSubmap;

        // Parse modifier flags from properties
        auto modVal = getProp(child, "mods");
        if (auto* modStr = std::get_if<std::string>(&modVal)) {
            kb.mods = parseMods(*modStr);
        }

        // Key from node name
        kb.key = child.name;

        // Dispatcher and args from arguments
        if (child.arguments.size() >= 1)
            kb.dispatcher = getString(child.arguments[0]);
        if (child.arguments.size() >= 2)
            kb.args = getString(child.arguments[1]);
        // Join remaining arguments with space
        for (size_t i = 3; i < child.arguments.size(); ++i) {
            kb.args += " " + getString(child.arguments[i]);
        }

        // Flags from properties
        auto flagVal = getProp(child, "flags");
        if (auto* flagStr = std::get_if<std::string>(&flagVal)) {
            kb.flags = kb.flags | parseBindFlags(*flagStr);
        }

        binds_.keybinds.push_back(std::move(kb));
    };

    // Process bind variants: bind, bindm (mouse), bindl (locked),
    // bindr (release), bindt (transparent), binds (with children)
    auto processBindNodes = [&](const char* nodeName, BindFlag extraFlags) {
        for (const auto* node : doc.getNodes(nodeName)) {
            // bind nodes with children (block form)
            if (!node->children.empty()) {
                std::string currentSubmap;
                for (const auto& child : node->children) {
                    if (child.name == "submap") {
                        // Enter a submap block
                        currentSubmap = firstArgString(child);
                        for (const auto& sc : child.children) {
                            parseSingleBind(sc, extraFlags, currentSubmap);
                        }
                        currentSubmap.clear();
                    } else {
                        parseSingleBind(child, extraFlags, currentSubmap);
                    }
                }
            }
            // Inline bind: bind "SUPER" "Return" "exec" "alacritty"
            else if (node->arguments.size() >= 3) {
                Keybind kb;
                kb.flags = extraFlags;
                kb.mods = parseMods(getString(node->arguments[0]));
                kb.key = getString(node->arguments[1]);
                kb.dispatcher = getString(node->arguments[2]);
                if (node->arguments.size() >= 4)
                    kb.args = getString(node->arguments[3]);
                for (size_t i = 4; i < node->arguments.size(); ++i)
                    kb.args += " " + getString(node->arguments[i]);
                binds_.keybinds.push_back(std::move(kb));
            }
        }
    };

    processBindNodes("bind", BindFlag::None);
    processBindNodes("binds", BindFlag::None);
    processBindNodes("bindm", BindFlag::Mouse);
    processBindNodes("bindl", BindFlag::Locked);
    processBindNodes("bindr", BindFlag::Release);
    processBindNodes("bindt", BindFlag::Transparent);
    processBindNodes("binde", BindFlag::Repeat);
}

void ConfigManager::parseGestures() {
    const auto* node = impl_->document.getNode("gestures");
    if (!node) return;

    gestures_ = GestureConfig{};

    for (const auto& child : node->children) {
        const auto& n = child.name;

        if (n == "swipe") {
            for (const auto& sc : child.children) {
                GestureMapping m;
                if (sc.arguments.size() >= 1) m.dispatcher = getString(sc.arguments[0]);
                if (sc.arguments.size() >= 2) m.args = getString(sc.arguments[1]);
                gestures_.swipe[sc.name] = std::move(m);
            }
        } else if (n == "pinch") {
            for (const auto& sc : child.children) {
                GestureMapping m;
                if (sc.arguments.size() >= 1) m.dispatcher = getString(sc.arguments[0]);
                if (sc.arguments.size() >= 2) m.args = getString(sc.arguments[1]);
                gestures_.pinch[sc.name] = std::move(m);
            }
        } else if (n == "hold") {
            for (const auto& sc : child.children) {
                GestureMapping m;
                if (sc.arguments.size() >= 1) m.dispatcher = getString(sc.arguments[0]);
                if (sc.arguments.size() >= 2) m.args = getString(sc.arguments[1]);
                gestures_.hold[sc.name] = std::move(m);
            }
        }
    }
}

// ===========================================================================
// Task 58: Parse monitor with VRR, transform, scale
// ===========================================================================

void ConfigManager::parseMonitors() {
    monitors_.clear();
    for (const auto* node : impl_->document.getNodes("monitor")) {
        MonitorConfig mc;
        mc.name = firstArgString(*node);

        // Inline: monitor "DP-1" 1920 1080 60.0 0 0 scale=1.5
        if (node->arguments.size() >= 4) {
            mc.width = static_cast<int>(getInt(node->arguments[1]));
            mc.height = static_cast<int>(getInt(node->arguments[2]));
            mc.refresh_rate = getFloat(node->arguments[3]);
        }
        if (node->arguments.size() >= 6) {
            mc.x = static_cast<int>(getInt(node->arguments[4]));
            mc.y = static_cast<int>(getInt(node->arguments[5]));
        }
        // Properties on inline form
        auto scaleProp = getProp(*node, "scale");
        if (!std::holds_alternative<std::monostate>(scaleProp))
            mc.scale = getFloat(scaleProp, mc.scale);
        auto transformProp = getProp(*node, "transform");
        if (!std::holds_alternative<std::monostate>(transformProp))
            mc.transform = static_cast<int>(getInt(transformProp, mc.transform));
        auto vrrProp = getProp(*node, "vrr");
        if (!std::holds_alternative<std::monostate>(vrrProp))
            mc.vrr = getBool(vrrProp, mc.vrr);

        for (const auto& child : node->children) {
            const auto& n = child.name;
            if (n == "resolution" || n == "mode") {
                if (child.arguments.size() >= 2) {
                    mc.width = static_cast<int>(getInt(child.arguments[0]));
                    mc.height = static_cast<int>(getInt(child.arguments[1]));
                }
                if (child.arguments.size() >= 3)
                    mc.refresh_rate = getFloat(child.arguments[2]);
            }
            else if (n == "position") {
                if (child.arguments.size() >= 2) {
                    mc.x = static_cast<int>(getInt(child.arguments[0]));
                    mc.y = static_cast<int>(getInt(child.arguments[1]));
                }
            }
            else if (n == "scale")      mc.scale = static_cast<float>(firstArgDouble(child, mc.scale));
            else if (n == "transform")  mc.transform = static_cast<int>(firstArgInt(child, mc.transform));
            else if (n == "enabled")    mc.enabled = firstArgBool(child, mc.enabled);
            else if (n == "mirror")     mc.mirror_of = firstArgString(child);
            else if (n == "bitdepth")   mc.bitdepth = static_cast<int>(firstArgInt(child, mc.bitdepth));
            else if (n == "vrr")        mc.vrr = firstArgBool(child, mc.vrr);
            else if (n == "refresh_rate" || n == "refresh") mc.refresh_rate = static_cast<float>(firstArgDouble(child, mc.refresh_rate));
        }

        monitors_.push_back(std::move(mc));
    }
}

// ===========================================================================
// Task 61: Parse window rules
// ===========================================================================

void ConfigManager::parseWindowRules() {
    window_rules_.clear();

    auto processRuleNodes = [&](const char* nodeName) {
        for (const auto* node : impl_->document.getNodes(nodeName)) {
            WindowRuleConfig wr;

            for (const auto& child : node->children) {
                const auto& n = child.name;
                // Matching criteria
                if (n == "class" || n == "app_id" || n == "app-id")
                    wr.window_class = firstArgString(child);
                else if (n == "title")         wr.window_title = firstArgString(child);
                else if (n == "class_regex" || n == "app_id_regex" || n == "app-id-regex")
                    wr.window_class_regex = firstArgString(child);
                else if (n == "title_regex")   wr.window_title_regex = firstArgString(child);
                else if (n == "is_floating")   wr.is_floating = firstArgBool(child);
                else if (n == "is_fullscreen") wr.is_fullscreen = firstArgBool(child);
                // Actions
                else if (n == "float")         wr.floating = firstArgBool(child);
                else if (n == "fullscreen")    wr.fullscreen = firstArgBool(child);
                else if (n == "pinned")        wr.pinned = firstArgBool(child);
                else if (n == "workspace")     wr.workspace = static_cast<int>(firstArgInt(child));
                else if (n == "monitor")       wr.monitor = firstArgString(child);
                else if (n == "opacity")       wr.opacity = static_cast<float>(firstArgDouble(child));
                else if (n == "opacity_inactive") wr.opacity_inactive = static_cast<float>(firstArgDouble(child));
                else if (n == "noblur")        wr.noblur = firstArgBool(child);
                else if (n == "noshadow")      wr.noshadow = firstArgBool(child);
                else if (n == "noborder")      wr.noborder = firstArgBool(child);
                else if (n == "norounding")    wr.norounding = firstArgBool(child);
                else if (n == "rounding")      wr.rounding = static_cast<int>(firstArgInt(child));
                else if (n == "animation_style" || n == "animation-style")
                    wr.animation_style = firstArgString(child);
                else if (n == "center")        wr.center = firstArgBool(child);
                else if (n == "size") {
                    if (child.arguments.size() >= 2)
                        wr.size = {static_cast<int>(getInt(child.arguments[0])),
                                   static_cast<int>(getInt(child.arguments[1]))};
                }
                else if (n == "min_size" || n == "min-size") {
                    if (child.arguments.size() >= 2)
                        wr.min_size = {static_cast<int>(getInt(child.arguments[0])),
                                       static_cast<int>(getInt(child.arguments[1]))};
                }
                else if (n == "max_size" || n == "max-size") {
                    if (child.arguments.size() >= 2)
                        wr.max_size = {static_cast<int>(getInt(child.arguments[0])),
                                       static_cast<int>(getInt(child.arguments[1]))};
                }
                else if (n == "position") {
                    if (child.arguments.size() >= 2)
                        wr.position = {static_cast<int>(getInt(child.arguments[0])),
                                       static_cast<int>(getInt(child.arguments[1]))};
                }
            }

            window_rules_.push_back(std::move(wr));
        }
    };

    processRuleNodes("windowrule");
    processRuleNodes("window-rule");
}

void ConfigManager::parseWorkspaceRules() {
    workspace_rules_.clear();
    for (const auto* node : impl_->document.getNodes("workspacerule")) {
        WorkspaceRuleConfig wr;
        wr.workspace_id = static_cast<int>(firstArgInt(*node, -1));

        for (const auto& child : node->children) {
            const auto& n = child.name;
            if (n == "name")            wr.name = firstArgString(child);
            else if (n == "monitor")    wr.monitor = firstArgString(child);
            else if (n == "layout")     wr.default_layout = firstArgString(child);
            else if (n == "persistent") wr.persistent = firstArgBool(child);
            else if (n == "gaps_in")    wr.gaps_in = static_cast<int>(firstArgInt(child, wr.gaps_in));
            else if (n == "gaps_out")   wr.gaps_out = static_cast<int>(firstArgInt(child, wr.gaps_out));
            else if (n == "border_size") wr.border_size = static_cast<int>(firstArgInt(child, wr.border_size));
            else if (n == "rounding")   wr.rounding = static_cast<int>(firstArgInt(child, wr.rounding));
        }

        workspace_rules_.push_back(std::move(wr));
    }
}

void ConfigManager::parsePlugins() {
    plugins_.clear();
    for (const auto* node : impl_->document.getNodes("plugin")) {
        PluginConfig pc;
        pc.name = firstArgString(*node);

        for (const auto& child : node->children) {
            const auto& n = child.name;
            if (n == "path")         pc.path = firstArgString(child);
            else if (n == "enabled") pc.enabled = firstArgBool(child, true);
            else {
                pc.settings[n] = firstArgString(child);
            }
        }

        plugins_.push_back(std::move(pc));
    }
}

void ConfigManager::parseEnvironment() {
    const auto* node = impl_->document.getNode("env");
    if (!node) return;

    environment_.variables.clear();
    for (const auto& child : node->children) {
        if (!child.arguments.empty())
            environment_.variables[child.name] = getString(child.arguments[0]);
    }
}

void ConfigManager::parseExec() {
    const auto* node = impl_->document.getNode("exec");
    if (!node) return;

    exec_ = ExecConfig{};
    for (const auto& child : node->children) {
        const auto& n = child.name;
        if (n == "once") {
            for (const auto& arg : child.arguments)
                exec_.exec_once.push_back(getString(arg));
            for (const auto& sc : child.children)
                exec_.exec_once.push_back(firstArgString(sc, sc.name));
        } else if (n == "always") {
            for (const auto& arg : child.arguments)
                exec_.exec.push_back(getString(arg));
            for (const auto& sc : child.children)
                exec_.exec.push_back(firstArgString(sc, sc.name));
        } else if (n == "shutdown") {
            for (const auto& arg : child.arguments)
                exec_.exec_shutdown.push_back(getString(arg));
            for (const auto& sc : child.children)
                exec_.exec_shutdown.push_back(firstArgString(sc, sc.name));
        }
    }
}

void ConfigManager::parseXWayland() {
    const auto* node = impl_->document.getNode("xwayland");
    if (!node) return;

    for (const auto& child : node->children) {
        const auto& n = child.name;
        if (n == "enabled")                 xwayland_.enabled = firstArgBool(child, xwayland_.enabled);
        else if (n == "use_nearest_neighbor") xwayland_.use_nearest_neighbor = firstArgBool(child, xwayland_.use_nearest_neighbor);
        else if (n == "force_zero_scaling") xwayland_.force_zero_scaling = firstArgBool(child, xwayland_.force_zero_scaling);
    }
}

void ConfigManager::parsePermissions() {
    const auto* node = impl_->document.getNode("permissions");
    if (!node) return;

    for (const auto& child : node->children) {
        const auto& n = child.name;
        if (n == "screencopy")       permissions_.screencopy = firstArgBool(child, permissions_.screencopy);
        else if (n == "keyboard_grab") permissions_.keyboard_grab = firstArgBool(child, permissions_.keyboard_grab);
        else if (n == "input_inhibit") permissions_.input_inhibit = firstArgBool(child, permissions_.input_inhibit);
        else if (n == "screencopy_allowlist") {
            permissions_.screencopy_allowlist.clear();
            for (const auto& arg : child.arguments)
                permissions_.screencopy_allowlist.push_back(getString(arg));
        } else if (n == "keyboard_grab_allowlist") {
            permissions_.keyboard_grab_allowlist.clear();
            for (const auto& arg : child.arguments)
                permissions_.keyboard_grab_allowlist.push_back(getString(arg));
        }
    }
}

void ConfigManager::parseDebug() {
    const auto* node = impl_->document.getNode("debug");
    if (!node) return;

    for (const auto& child : node->children) {
        const auto& n = child.name;
        if (n == "overlay")             debug_.overlay = firstArgBool(child, debug_.overlay);
        else if (n == "damage_tracking") debug_.damage_tracking = firstArgBool(child, debug_.damage_tracking);
        else if (n == "disable_logs")   debug_.disable_logs = firstArgBool(child, debug_.disable_logs);
        else if (n == "disable_time")   debug_.disable_time = firstArgBool(child, debug_.disable_time);
        else if (n == "log_level")      debug_.log_level = static_cast<int>(firstArgInt(child, debug_.log_level));
        else if (n == "colored_stdout") debug_.colored_stdout = firstArgBool(child, debug_.colored_stdout);
        else if (n == "enable_stdout_logs") debug_.enable_stdout_logs = firstArgBool(child, debug_.enable_stdout_logs);
        else if (n == "manual_crash")   debug_.manual_crash = firstArgBool(child, debug_.manual_crash);
    }
}

} // namespace eternal

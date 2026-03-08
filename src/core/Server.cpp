#include "eternal/core/Server.hpp"

#include "eternal/animation/AnimationEngine.hpp"
#include "eternal/config/ConfigManager.hpp"
#include "eternal/core/Compositor.hpp"
#include "eternal/core/OutputManager.hpp"
#include "eternal/core/Output.hpp"
#include "eternal/core/Surface.hpp"
#include "eternal/input/InputManager.hpp"
#include "eternal/input/Keyboard.hpp"
#include "eternal/input/Pointer.hpp"
#include "eternal/plugins/PluginManager.hpp"
#include "eternal/workspace/WorkspaceManager.hpp"
#include "eternal/xwayland/XWaylandManager.hpp"
#include "eternal/utils/Logger.hpp"

#include "eternal/utils/WlrSceneCompat.h"

extern "C" {
#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xdg_shell.h>
}

#include <cstdlib>
#include <cctype>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <optional>
#include <spawn.h>
#include <string_view>
#include <unistd.h>
#include <vector>
#include <new>

extern char **environ;

namespace eternal {

namespace {

struct XdgSurfaceListenerState {
    wl_listener listener{};
    Server* server = nullptr;
};

void onNewXdgToplevel(wl_listener* listener, void* data) {
    XdgSurfaceListenerState* state = wl_container_of(listener, state, listener);
    auto* toplevel = static_cast<wlr_xdg_toplevel*>(data);
    if (!state || !state->server || !toplevel) {
        return;
    }

    auto& compositor = state->server->getCompositor();
    auto* surface = compositor.createSurface(toplevel);
    if (!surface) {
        LOG_ERROR("Failed to create toplevel surface");
        return;
    }

    if (auto* output = compositor.getActiveOutput()) {
        surface->setOutput(output);
    }

    LOG_INFO("Registered new XDG toplevel");
}

std::optional<std::filesystem::path> resolveConfigPath() {
    std::vector<std::filesystem::path> candidates;

    if (const char* env = std::getenv("ETERNAL_CONFIG"); env && env[0] != '\0') {
        candidates.emplace_back(env);
    }
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && xdg[0] != '\0') {
        candidates.emplace_back(std::filesystem::path(xdg) / "eternal" / "eternal.kdl");
    }
    if (const char* home = std::getenv("HOME"); home && home[0] != '\0') {
        candidates.emplace_back(std::filesystem::path(home) / ".config" / "eternal" / "eternal.kdl");
    }
    candidates.emplace_back("/etc/eternal/eternal.kdl");
    candidates.emplace_back("/usr/share/eternal/eternal.kdl");

    for (const auto& path : candidates) {
        std::error_code ec;
        if (!path.empty() && std::filesystem::exists(path, ec) && !ec) {
            return path;
        }
    }
    return std::nullopt;
}

std::string configModifiersToString(uint32_t mods) {
    std::vector<std::string_view> parts;
    if (mods & (1u << 0)) parts.emplace_back("super");
    if (mods & (1u << 1)) parts.emplace_back("shift");
    if (mods & (1u << 2)) parts.emplace_back("ctrl");
    if (mods & (1u << 3)) parts.emplace_back("alt");
    if (mods & (1u << 4)) parts.emplace_back("mod2");
    if (mods & (1u << 5)) parts.emplace_back("mod3");
    if (mods & (1u << 6)) parts.emplace_back("mod5");

    std::string result;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i != 0) {
            result += '+';
        }
        result += parts[i];
    }
    return result;
}

std::string normalizeConfigKey(std::string key) {
    std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (key == "period_off") return "period";
    if (key == "bracketright_m") return "bracketright";
    if (key == "bracketleft_m") return "bracketleft";
    if (key == "l_lock") return "l";
    if (key == "mouse_up" || key == "mouse_down") return {};
    return key;
}

bool isMouseBinding(const Keybind& bind) {
    return (bind.flags & BindFlag::Mouse) == BindFlag::Mouse ||
           bind.key.rfind("mouse:", 0) == 0;
}

uint64_t monotonicMs() {
    using clock = std::chrono::steady_clock;
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            clock::now().time_since_epoch()).count());
}

WindowAnimStyle parseWindowAnimStyle(const std::string& style) {
    std::string normalized = style;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (normalized == "none" || normalized == "off") {
        return WindowAnimStyle::None;
    }
    if (normalized == "fade" || normalized == "crossfade") {
        return WindowAnimStyle::Fade;
    }
    if (normalized == "zoom") {
        return WindowAnimStyle::Zoom;
    }
    if (normalized == "popin" || normalized == "pop-in") {
        return WindowAnimStyle::PopIn;
    }
    if (normalized == "slidefade" || normalized == "slide-fade") {
        return WindowAnimStyle::SlideFade;
    }
    return WindowAnimStyle::Slide;
}

void applyConfigEnvironment(const ConfigManager& configManager) {
    for (const auto& [name, value] : configManager.getEnvironment().variables) {
        if (name.empty()) {
            continue;
        }
        setenv(name.c_str(), value.c_str(), 1);
    }
}

void applyInputConfig(InputManager& inputManager, const ConfigManager& configManager) {
    const auto& inputCfg = configManager.getInput();

    if (auto* keyboard = inputManager.getKeyboard()) {
        keyboard->setLayout(inputCfg.keyboard.layout);
        keyboard->setVariant(inputCfg.keyboard.variant);
        keyboard->setModel(inputCfg.keyboard.model);
        keyboard->setOptions(inputCfg.keyboard.options);
        keyboard->setRepeatRate(inputCfg.keyboard.repeat_rate);
        keyboard->setRepeatDelay(inputCfg.keyboard.repeat_delay);
        keyboard->setNumlockByDefault(inputCfg.keyboard.numlock_by_default);
        keyboard->setCapslockByDefault(inputCfg.keyboard.capslock_by_default);
        keyboard->applyLayoutConfig();
    }

    if (auto* pointer = inputManager.getPointer()) {
        pointer->setSensitivity(inputCfg.pointer.sensitivity);
        pointer->setNaturalScroll(inputCfg.pointer.natural_scroll);
        pointer->setScrollFactor(static_cast<double>(inputCfg.pointer.scroll_factor));
        pointer->setLeftHanded(inputCfg.pointer.left_handed);
        pointer->setAccelProfile(
            inputCfg.pointer.accel_profile_flat ? AccelProfile::Flat : AccelProfile::Adaptive);
    }
}

void applyAnimationConfig(AnimationEngine& animationEngine,
                          const ConfigManager& configManager) {
    const auto& cfg = configManager.getAnimation();

    animationEngine.setEnabled(cfg.enabled);
    animationEngine.setGlobalSpeed(cfg.speed_multiplier);

    for (const auto& [name, curve] : cfg.bezier_curves) {
        animationEngine.curveManager().add(
            name, curve.x1, curve.y1, curve.x2, curve.y2);
    }

    WindowAnimConfig windowCfg;
    windowCfg.openStyle = cfg.window_open.enabled
        ? parseWindowAnimStyle(cfg.window_open.style)
        : WindowAnimStyle::None;
    windowCfg.closeStyle = WindowAnimStyle::None;
    windowCfg.openDuration_ms = std::max(1.0f, cfg.window_open.speed);
    windowCfg.closeDuration_ms = std::max(1.0f, cfg.window_close.speed);
    windowCfg.openCurve = cfg.window_open.bezier;
    windowCfg.closeCurve = cfg.window_close.bezier;
    animationEngine.setWindowAnimConfig(windowCfg);

    MoveResizeConfig moveResizeCfg;
    moveResizeCfg.moveDuration_ms = std::max(1.0f, cfg.window_move.speed);
    moveResizeCfg.resizeDuration_ms = std::max(1.0f, cfg.window_resize.speed);
    moveResizeCfg.moveCurve = cfg.window_move.bezier;
    moveResizeCfg.resizeCurve = cfg.window_resize.bezier;
    moveResizeCfg.enableMoveAnim = cfg.enabled && cfg.window_move.enabled;
    // Resizing the client every frame causes a configure storm. Keep the
    // resize path immediate until there is a texture-space scale renderer.
    moveResizeCfg.enableResizeAnim = false;
    animationEngine.setMoveResizeConfig(moveResizeCfg);
}

void loadConfiguredKeybinds(InputManager& inputManager, const ConfigManager& configManager) {
    inputManager.clearKeybinds();

    std::size_t loaded = 0;
    for (const auto& bind : configManager.getBinds().keybinds) {
        const std::string mods = configModifiersToString(bind.mods);

        if (isMouseBinding(bind)) {
            if (bind.key.rfind("mouse:", 0) != 0) {
                LOG_WARN("Skipping unsupported non-button mouse binding '{}'", bind.key);
                continue;
            }
            if (inputManager.addMouseKeybind(mods, bind.key, bind.dispatcher, bind.args,
                                             bind.submap)) {
                ++loaded;
            }
            continue;
        }

        const std::string key = normalizeConfigKey(bind.key);
        if (key.empty()) {
            LOG_WARN("Skipping unsupported key binding '{}'", bind.key);
            continue;
        }

        if (inputManager.addKeybind(mods, key, bind.dispatcher, bind.args, bind.submap)) {
            ++loaded;
        } else {
            LOG_WARN("Failed to install key binding '{} {} -> {}'",
                     mods, key, bind.dispatcher);
        }
    }

    LOG_INFO("Loaded {} keybindings from {}", loaded,
             configManager.getConfigPath().empty()
                 ? std::string("built-in defaults")
                 : configManager.getConfigPath().string());
}

void spawnCommand(const std::string& command) {
    if (command.empty()) {
        return;
    }

    pid_t pid = 0;
    char* const argv[] = {
        const_cast<char*>("/bin/sh"),
        const_cast<char*>("-lc"),
        const_cast<char*>(command.c_str()),
        nullptr
    };
    const int rc = posix_spawnp(&pid, "/bin/sh", nullptr, nullptr, argv, environ);
    if (rc != 0) {
        LOG_ERROR("Failed to spawn '{}': {}", command, std::strerror(rc));
        return;
    }
    LOG_INFO("Spawned '{}'", command);
}

} // namespace

Server::Server() = default;

Server::~Server() {
    m_running = false;
    stopAnimationTimer();

    if (m_newXdgSurfaceListener) {
        XdgSurfaceListenerState* state =
            wl_container_of(m_newXdgSurfaceListener, state, listener);
        wl_list_remove(&state->listener.link);
        delete state;
        m_newXdgSurfaceListener = nullptr;
    }

    m_inputManager.reset();
    m_outputManager.reset();
    m_compositor.reset();
    m_workspaceManager.reset();
    m_pluginManager.reset();

    if (m_xwaylandManager) {
        m_xwaylandManager->shutdown();
        m_xwaylandManager.reset();
    }

    if (m_display) {
        wl_display_destroy_clients(m_display);
        wl_display_destroy(m_display);
    }
}

bool Server::initWayland() {
    m_display = wl_display_create();
    if (!m_display) {
        LOG_ERROR("Failed to create wl_display");
        return false;
    }
    return true;
}

bool Server::initBackend() {
    m_backend = wlr_backend_autocreate(wl_display_get_event_loop(m_display), &m_session);
    if (!m_backend) {
        LOG_ERROR("Failed to create wlr_backend");
        return false;
    }

    m_renderer = wlr_renderer_autocreate(m_backend);
    if (!m_renderer) {
        LOG_ERROR("Failed to create wlr_renderer");
        return false;
    }
    wlr_renderer_init_wl_display(m_renderer, m_display);

    m_allocator = wlr_allocator_autocreate(m_backend, m_renderer);
    if (!m_allocator) {
        LOG_ERROR("Failed to create wlr_allocator");
        return false;
    }

    return true;
}

bool Server::initScene() {
    if (!wlr_compositor_create(m_display, 5, m_renderer)) {
        LOG_ERROR("Failed to create wlroots compositor");
        return false;
    }
    if (!wlr_subcompositor_create(m_display)) {
        LOG_ERROR("Failed to create wlroots subcompositor");
        return false;
    }
    if (!wlr_data_device_manager_create(m_display)) {
        LOG_ERROR("Failed to create data device manager");
        return false;
    }

    m_outputLayout = wlr_output_layout_create(m_display);
    if (!m_outputLayout) {
        LOG_ERROR("Failed to create output layout");
        return false;
    }

    m_scene = wlr_scene_create();
    if (!m_scene) {
        LOG_ERROR("Failed to create scene");
        return false;
    }
    wlr_scene_attach_output_layout(m_scene, m_outputLayout);

    m_seat = wlr_seat_create(m_display, "seat0");
    if (!m_seat) {
        LOG_ERROR("Failed to create seat");
        return false;
    }

    m_xdgShell = wlr_xdg_shell_create(m_display, 3);
    if (!m_xdgShell) {
        LOG_ERROR("Failed to create xdg-shell");
        return false;
    }

    auto* xdg_listener = new (std::nothrow) XdgSurfaceListenerState();
    if (!xdg_listener) {
        LOG_ERROR("Failed to allocate XDG surface listener");
        return false;
    }
    xdg_listener->server = this;
    xdg_listener->listener.notify = onNewXdgToplevel;
    wl_signal_add(&m_xdgShell->events.new_toplevel, &xdg_listener->listener);
    m_newXdgSurfaceListener = &xdg_listener->listener;

    return true;
}

bool Server::initSubsystems() {
    m_configManager = std::make_unique<ConfigManager>();
    m_pluginManager = std::make_unique<PluginManager>();
    m_workspaceManager = std::make_unique<WorkspaceManager>(*this);
    m_compositor = std::make_unique<Compositor>(*this);
    m_outputManager = std::make_unique<OutputManager>(*this);
    m_inputManager = std::make_unique<InputManager>(
        *this, m_seat, m_backend, m_session, m_outputLayout, m_display);
    m_animationEngine = std::make_unique<AnimationEngine>();
    m_xwaylandManager = std::make_unique<XWaylandManager>();

    if (!m_compositor->init()) {
        LOG_ERROR("Failed to initialize compositor");
        return false;
    }
    if (!m_outputManager->init()) {
        LOG_ERROR("Failed to initialize output manager");
        return false;
    }

    m_inputManager->setScene(m_scene);
    m_inputManager->setupBackendListeners();

    m_xwaylandManager->init(*this);

    m_inputManager->registerKeybindDispatcher("exec", [](const std::string& args) {
        spawnCommand(args);
    });
    m_inputManager->registerKeybindDispatcher("killactive", [this](const std::string&) {
        if (auto* surface = m_compositor->getFocusedSurface()) {
            surface->close();
        }
    });
    m_inputManager->registerKeybindDispatcher("fullscreen", [this](const std::string& args) {
        if (auto* surface = m_compositor->getFocusedSurface()) {
            if (args == "1") {
                surface->toggleMaximized();
            } else {
                surface->toggleFullscreen();
            }
        }
    });
    m_inputManager->registerKeybindDispatcher("togglefloating", [this](const std::string&) {
        if (auto* surface = m_compositor->getFocusedSurface()) {
            surface->setFloating(!surface->isFloating());
        }
    });
    m_inputManager->registerKeybindDispatcher("pin", [this](const std::string&) {
        if (auto* surface = m_compositor->getFocusedSurface()) {
            if (surface->isPinned()) {
                surface->unpin();
            } else {
                surface->pin();
            }
        }
    });
    m_inputManager->registerKeybindDispatcher("workspace", [this](const std::string& args) {
        if (args == "next") {
            m_workspaceManager->switchToNext();
            return;
        }
        if (args == "prev") {
            m_workspaceManager->switchToPrev();
            return;
        }

        try {
            m_workspaceManager->switchToNumber(std::stoi(args));
        } catch (...) {
            m_workspaceManager->switchToName(args);
        }
    });
    m_inputManager->registerKeybindDispatcher("movetoworkspace", [this](const std::string& args) {
        auto* surface = m_compositor->getFocusedSurface();
        if (!surface) {
            return;
        }

        if (args == "next") {
            m_workspaceManager->moveWindowToNext(surface);
            m_workspaceManager->switchToNext();
            return;
        }
        if (args == "prev") {
            m_workspaceManager->moveWindowToPrev(surface);
            m_workspaceManager->switchToPrev();
            return;
        }

        if (args == "special") {
            auto* special = m_workspaceManager->getSpecialWorkspace();
            if (special) {
                m_workspaceManager->moveWindowTo(surface, special->getId());
            }
            return;
        }

        try {
            m_workspaceManager->moveWindowTo(surface, std::stoi(args));
        } catch (...) {
            if (auto* workspace = m_workspaceManager->findWorkspace(args)) {
                m_workspaceManager->moveWindowTo(surface, workspace->getId());
            }
        }
    });
    m_inputManager->registerKeybindDispatcher("movetoworkspacesilent",
                                              [this](const std::string& args) {
        auto* surface = m_compositor->getFocusedSurface();
        if (!surface) {
            return;
        }

        try {
            m_workspaceManager->moveWindowTo(surface, std::stoi(args));
        } catch (...) {
            if (auto* workspace = m_workspaceManager->findWorkspace(args)) {
                m_workspaceManager->moveWindowTo(surface, workspace->getId());
            }
        }
    });
    m_inputManager->registerKeybindDispatcher("togglespecialworkspace",
                                              [this](const std::string&) {
        m_workspaceManager->toggleSpecialWorkspace();
    });
    m_inputManager->registerKeybindDispatcher("focuswindow", [this](const std::string& args) {
        if (args == "l" || args == "u") {
            m_compositor->focusPrev();
        } else if (args == "r" || args == "d") {
            m_compositor->focusNext();
        }
    });
    m_inputManager->registerKeybindDispatcher("exit", [this](const std::string&) {
        quit();
    });
    m_inputManager->registerKeybindDispatcher("reload", [this](const std::string&) {
        const auto configPath = m_configManager->getConfigPath();
        if (configPath.empty()) {
            return;
        }
        if (m_configManager->reload()) {
            applyConfigEnvironment(*m_configManager);
            applyInputConfig(*m_inputManager, *m_configManager);
            applyAnimationConfig(*m_animationEngine, *m_configManager);
            loadConfiguredKeybinds(*m_inputManager, *m_configManager);
        }
    });

    if (auto configPath = resolveConfigPath()) {
        try {
            m_configManager->load(*configPath);
            LOG_INFO("Loaded config from {}", configPath->string());
            applyConfigEnvironment(*m_configManager);
            applyInputConfig(*m_inputManager, *m_configManager);
            applyAnimationConfig(*m_animationEngine, *m_configManager);
            loadConfiguredKeybinds(*m_inputManager, *m_configManager);
        } catch (const std::exception& e) {
            LOG_ERROR("Failed to load config '{}': {}", configPath->string(), e.what());
        }
    } else {
        LOG_WARN("No Eternal config file found; starting with built-in defaults");
    }

    if (!startAnimationTimer()) {
        LOG_ERROR("Failed to initialize animation timer");
        return false;
    }
    return true;
}

void Server::setupSignalHandlers() {}

bool Server::init() {
    LOG_INFO("Initialising Eternal server");

    if (!initWayland()) {
        return false;
    }
    if (!initBackend()) {
        return false;
    }
    if (!initScene()) {
        return false;
    }
    if (!initSubsystems()) {
        return false;
    }

    return true;
}

int Server::handleAnimationTimer() {
    if (!m_animationEngine) {
        return 0;
    }

    const uint64_t now = monotonicMs();
    float deltaMs = 16.0f;
    if (m_lastAnimationTickMs != 0 && now > m_lastAnimationTickMs) {
        deltaMs = static_cast<float>(std::min<uint64_t>(now - m_lastAnimationTickMs, 50));
    }
    m_lastAnimationTickMs = now;

    m_animationEngine->update(deltaMs);

    const bool active = m_animationEngine->getActiveCount() != 0 ||
                        !m_animationEngine->getGhostSurfaces().empty() ||
                        m_animationEngine->isWorkspaceTransitionActive() ||
                        m_animationEngine->isKineticScrollActive();

    if (active) {
        for (const auto& output : m_compositor->getOutputs()) {
            if (output) {
                output->addFullDamage();
            }
        }
        wl_event_source_timer_update(m_animationTimer, 16);
    } else if (m_animationTimer) {
        wl_event_source_timer_update(m_animationTimer, -1);
    }

    return 0;
}

bool Server::startAnimationTimer() {
    if (!m_display || m_animationTimer) {
        return m_animationTimer != nullptr;
    }

    wl_event_loop* loop = wl_display_get_event_loop(m_display);
    if (!loop) {
        return false;
    }

    m_animationTimer = wl_event_loop_add_timer(
        loop,
        [](void* data) -> int {
            return static_cast<Server*>(data)->handleAnimationTimer();
        },
        this);
    if (!m_animationTimer) {
        return false;
    }

    m_lastAnimationTickMs = monotonicMs();
    wl_event_source_timer_update(m_animationTimer, 16);
    return true;
}

void Server::stopAnimationTimer() {
    if (!m_animationTimer) {
        return;
    }

    wl_event_source_remove(m_animationTimer);
    m_animationTimer = nullptr;
    m_lastAnimationTickMs = 0;
}

void Server::scheduleAnimationTick() {
    if (m_animationTimer) {
        wl_event_source_timer_update(m_animationTimer, 16);
    }
}

void Server::run() {
    if (!m_display || !m_backend) {
        LOG_ERROR("Server is not initialized");
        return;
    }

    const char* socket = wl_display_add_socket_auto(m_display);
    if (!socket) {
        LOG_ERROR("Failed to open Wayland socket");
        return;
    }

    if (!wlr_backend_start(m_backend)) {
        LOG_ERROR("Failed to start wlr_backend");
        return;
    }

    setenv("WAYLAND_DISPLAY", socket, 1);
    m_socketPath = socket;
    m_running = true;

    if (m_configManager) {
        for (const auto& command : m_configManager->getExec().exec_once) {
            spawnCommand(command);
        }
    }

    LOG_INFO("Running on WAYLAND_DISPLAY={}", socket);
    wl_display_run(m_display);
    m_running = false;
}

void Server::quit() {
    if (m_display) {
        wl_display_terminate(m_display);
    }
}

} // namespace eternal

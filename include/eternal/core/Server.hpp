#pragma once
#include <memory>
#include <string>

extern "C" {
#include <wayland-server-core.h>

struct wlr_backend;
struct wlr_renderer;
struct wlr_allocator;
struct wlr_scene;
struct wlr_output_layout;
struct wlr_seat;
struct wlr_session;
struct wlr_xdg_shell;
struct wl_listener;
}

namespace eternal {

class ConfigManager;
class WorkspaceManager;
class InputManager;
class AnimationEngine;
class PluginManager;
class Compositor;
class XWaylandManager;
class OutputManager;

class Server {
public:
    Server();
    ~Server();

    bool init();
    void run();
    void quit();
    void scheduleAnimationTick();

    struct wl_display* getDisplay() const { return m_display; }
    wlr_backend* getBackend() const { return m_backend; }
    wlr_renderer* getRenderer() const { return m_renderer; }
    wlr_allocator* getAllocator() const { return m_allocator; }
    wlr_scene* getScene() const { return m_scene; }
    wlr_output_layout* getOutputLayout() const { return m_outputLayout; }
    wlr_seat* getSeat() const { return m_seat; }
    wlr_session* getSession() const { return m_session; }

    ConfigManager& getConfigManager() { return *m_configManager; }
    WorkspaceManager& getWorkspaceManager() { return *m_workspaceManager; }
    InputManager& getInputManager() { return *m_inputManager; }
    AnimationEngine& getAnimationEngine() { return *m_animationEngine; }
    PluginManager& getPluginManager() { return *m_pluginManager; }
    Compositor& getCompositor() { return *m_compositor; }
    XWaylandManager& getXWaylandManager() { return *m_xwaylandManager; }
    OutputManager& getOutputManager() { return *m_outputManager; }

    std::string getSocketPath() const { return m_socketPath; }
    bool isRunning() const { return m_running; }

private:
    bool initWayland();
    bool initBackend();
    bool initRenderer();
    bool initScene();
    bool initSubsystems();
    int handleAnimationTimer();
    bool startAnimationTimer();
    void stopAnimationTimer();
    void setupSignalHandlers();

    struct wl_display* m_display = nullptr;
    wlr_backend* m_backend = nullptr;
    wlr_renderer* m_renderer = nullptr;
    wlr_allocator* m_allocator = nullptr;
    wlr_scene* m_scene = nullptr;
    wlr_output_layout* m_outputLayout = nullptr;
    wlr_seat* m_seat = nullptr;
    wlr_session* m_session = nullptr;
    wlr_xdg_shell* m_xdgShell = nullptr;

    std::unique_ptr<ConfigManager> m_configManager;
    std::unique_ptr<WorkspaceManager> m_workspaceManager;
    std::unique_ptr<InputManager> m_inputManager;
    std::unique_ptr<AnimationEngine> m_animationEngine;
    std::unique_ptr<PluginManager> m_pluginManager;
    std::unique_ptr<Compositor> m_compositor;
    std::unique_ptr<XWaylandManager> m_xwaylandManager;
    std::unique_ptr<OutputManager> m_outputManager;

    std::string m_socketPath;
    bool m_running = false;

    struct wl_listener* m_newXdgSurfaceListener = nullptr;
    struct wl_event_source* m_animationTimer = nullptr;
    uint64_t m_lastAnimationTickMs = 0;
};

} // namespace eternal

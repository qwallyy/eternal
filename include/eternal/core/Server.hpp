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
}

namespace eternal {

class ConfigManager;
class WorkspaceManager;
class LayoutManager;
class AnimationEngine;
class InputManager;
class IPCServer;
class PluginManager;
class Compositor;
class DecorationManager;
class XWaylandManager;
class OutputManager;
class OutputMirrorManager;
class DesktopPortal;
class Clipboard;
class DragDrop;
class IdleManager;
class SessionLock;
class Screenshot;
class ScreenRecorder;
class Accessibility;

class Server {
public:
    Server();
    ~Server();

    bool init();
    void run();
    void quit();

    struct wl_display* getDisplay() const { return m_display; }
    wlr_backend* getBackend() const { return m_backend; }
    wlr_renderer* getRenderer() const { return m_renderer; }
    wlr_allocator* getAllocator() const { return m_allocator; }
    wlr_scene* getScene() const { return m_scene; }
    wlr_output_layout* getOutputLayout() const { return m_outputLayout; }

    ConfigManager& getConfigManager() { return *m_configManager; }
    WorkspaceManager& getWorkspaceManager() { return *m_workspaceManager; }
    LayoutManager& getLayoutManager() { return *m_layoutManager; }
    AnimationEngine& getAnimationEngine() { return *m_animationEngine; }
    InputManager& getInputManager() { return *m_inputManager; }
    IPCServer& getIPCServer() { return *m_ipcServer; }
    PluginManager& getPluginManager() { return *m_pluginManager; }
    Compositor& getCompositor() { return *m_compositor; }
    DecorationManager& getDecorationManager() { return *m_decorationManager; }
    XWaylandManager& getXWaylandManager() { return *m_xwaylandManager; }
    OutputManager& getOutputManager() { return *m_outputManager; }
    OutputMirrorManager& getOutputMirrorManager() { return *m_outputMirrorManager; }
    DesktopPortal& getDesktopPortal() { return *m_desktopPortal; }
    Clipboard& getClipboard() { return *m_clipboard; }
    DragDrop& getDragDrop() { return *m_dragDrop; }
    IdleManager& getIdleManager() { return *m_idleManager; }
    SessionLock& getSessionLock() { return *m_sessionLock; }
    Screenshot& getScreenshot() { return *m_screenshot; }
    ScreenRecorder& getScreenRecorder() { return *m_screenRecorder; }
    Accessibility& getAccessibility() { return *m_accessibility; }

    std::string getSocketPath() const { return m_socketPath; }
    bool isRunning() const { return m_running; }

private:
    bool initWayland();
    bool initBackend();
    bool initRenderer();
    bool initScene();
    bool initSubsystems();
    void setupSignalHandlers();

    struct wl_display* m_display = nullptr;
    wlr_backend* m_backend = nullptr;
    wlr_renderer* m_renderer = nullptr;
    wlr_allocator* m_allocator = nullptr;
    wlr_scene* m_scene = nullptr;
    wlr_output_layout* m_outputLayout = nullptr;

    std::unique_ptr<ConfigManager> m_configManager;
    std::unique_ptr<WorkspaceManager> m_workspaceManager;
    std::unique_ptr<LayoutManager> m_layoutManager;
    std::unique_ptr<AnimationEngine> m_animationEngine;
    std::unique_ptr<InputManager> m_inputManager;
    std::unique_ptr<IPCServer> m_ipcServer;
    std::unique_ptr<PluginManager> m_pluginManager;
    std::unique_ptr<Compositor> m_compositor;
    std::unique_ptr<DecorationManager> m_decorationManager;
    std::unique_ptr<XWaylandManager> m_xwaylandManager;
    std::unique_ptr<OutputManager> m_outputManager;
    std::unique_ptr<OutputMirrorManager> m_outputMirrorManager;
    std::unique_ptr<DesktopPortal> m_desktopPortal;
    std::unique_ptr<Clipboard> m_clipboard;
    std::unique_ptr<DragDrop> m_dragDrop;
    std::unique_ptr<IdleManager> m_idleManager;
    std::unique_ptr<SessionLock> m_sessionLock;
    std::unique_ptr<Screenshot> m_screenshot;
    std::unique_ptr<ScreenRecorder> m_screenRecorder;
    std::unique_ptr<Accessibility> m_accessibility;

    std::string m_socketPath;
    bool m_running = false;
};

} // namespace eternal

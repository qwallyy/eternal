#include "eternal/core/Server.hpp"

#include "eternal/config/ConfigManager.hpp"
#include "eternal/core/Compositor.hpp"
#include "eternal/core/OutputManager.hpp"
#include "eternal/core/Surface.hpp"
#include "eternal/input/InputManager.hpp"
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
#include <new>

namespace eternal {

namespace {

struct XdgSurfaceListenerState {
    wl_listener listener{};
    Server* server = nullptr;
};

void onNewXdgSurface(wl_listener* listener, void* data) {
    XdgSurfaceListenerState* state = wl_container_of(listener, state, listener);
    auto* xdg_surface = static_cast<wlr_xdg_surface*>(data);
    if (!state || !state->server || !xdg_surface) {
        return;
    }

    if (xdg_surface->role != WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
        return;
    }

    auto& compositor = state->server->getCompositor();
    auto* surface = compositor.createSurface(xdg_surface->toplevel);
    if (!surface) {
        LOG_ERROR("Failed to create toplevel surface");
        return;
    }

    if (auto* output = compositor.getActiveOutput()) {
        surface->setOutput(output);
    }

    LOG_INFO("Registered new XDG toplevel");
}

} // namespace

Server::Server() = default;

Server::~Server() {
    m_running = false;

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
    xdg_listener->listener.notify = onNewXdgSurface;
    wl_signal_add(&m_xdgShell->events.new_surface, &xdg_listener->listener);
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
        m_seat, m_backend, m_session, m_outputLayout, m_display);
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

#include "eternal/utils/Logger.hpp"
#include "eternal/plugins/PluginManager.hpp"
#include "eternal/rules/WindowRules.hpp"
#include "eternal/rules/WorkspaceRules.hpp"
#include "eternal/xwayland/XWaylandManager.hpp"
#include "eternal/input/InputManager.hpp"
#include "eternal/input/KeybindManager.hpp"
#include "eternal/gestures/GestureRecognizer.hpp"
#include "eternal/utils/Signal.hpp"

#include "eternal/utils/WlrSceneCompat.h"

extern "C" {
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
}

#include <memory>
#include <string>

namespace eternal {

// ---------------------------------------------------------------------------
// Server - central compositor object
// ---------------------------------------------------------------------------

class Server {
public:
    Server();
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    /// Initialise the Wayland display, backend, renderer, etc.
    bool init();

    /// Load (or reload) the configuration file.
    bool loadConfig(const std::string& path = {});

    /// Enter the Wayland event loop. Blocks until shutdown.
    void run();

    /// Request a graceful shutdown.
    void quit();

    // Sub-systems
    PluginManager&      plugins()        noexcept { return plugins_; }
    WindowRules&        windowRules()    noexcept { return window_rules_; }
    WorkspaceRules&     workspaceRules() noexcept { return workspace_rules_; }
    XWaylandManager&    xwayland()       noexcept { return xwayland_; }
    InputManager*       inputManager()   noexcept { return input_manager_.get(); }

    // Signals
    Signal<uint64_t>& onWindowCreate()  { return sig_window_create_; }
    Signal<uint64_t>& onWindowDestroy() { return sig_window_destroy_; }
    Signal<int>&      onWorkspaceChange() { return sig_workspace_change_; }

    // wlroots accessors (for sub-modules that need them)
    wl_display*        wlDisplay()     const noexcept { return display_; }
    wlr_backend*       wlrBackend()    const noexcept { return backend_; }
    wlr_renderer*      wlrRenderer()   const noexcept { return renderer_; }
    wlr_allocator*     wlrAllocator()  const noexcept { return allocator_; }
    wlr_output_layout* outputLayout()  const noexcept { return output_layout_; }
    wlr_seat*          wlrSeat()       const noexcept { return seat_; }
    wlr_scene*         wlrScene()      const noexcept { return scene_; }

private:
    // Wayland / wlroots core objects
    wl_display*        display_       = nullptr;
    wlr_backend*       backend_       = nullptr;
    wlr_renderer*      renderer_      = nullptr;
    wlr_allocator*     allocator_     = nullptr;
    wlr_compositor*    compositor_    = nullptr;
    wlr_subcompositor* subcompositor_ = nullptr;
    wlr_output_layout* output_layout_ = nullptr;
    wlr_seat*          seat_          = nullptr;
    wlr_xdg_shell*    xdg_shell_     = nullptr;
    wlr_scene*         scene_         = nullptr;

    // Listeners
    wl_listener new_output_listener_{};
    wl_listener new_xdg_surface_listener_{};

    // Sub-systems
    PluginManager   plugins_;
    WindowRules     window_rules_;
    WorkspaceRules  workspace_rules_;
    XWaylandManager xwayland_;
    std::unique_ptr<InputManager> input_manager_;

    // Signals
    Signal<uint64_t> sig_window_create_;
    Signal<uint64_t> sig_window_destroy_;
    Signal<int>      sig_workspace_change_;

    // Static event callbacks
    static void onNewOutput(wl_listener* listener, void* data);
    static void onNewXdgSurface(wl_listener* listener, void* data);
};

// ---------------------------------------------------------------------------
// Implementation
// ---------------------------------------------------------------------------

Server::Server() = default;

Server::~Server() {
    // Destroy input manager before wlroots objects
    input_manager_.reset();

    xwayland_.shutdown();

    if (display_) {
        wl_display_destroy_clients(display_);
        wl_display_destroy(display_);
    }
}

bool Server::init() {
    wlr_log_init(WLR_DEBUG, nullptr);
    LOG_INFO("Initialising Eternal compositor");

    display_ = wl_display_create();
    if (!display_) {
        LOG_ERROR("Failed to create wl_display");
        return false;
    }

    backend_ = wlr_backend_autocreate(wl_display_get_event_loop(display_), nullptr);
    if (!backend_) {
        LOG_ERROR("Failed to create wlr_backend");
        return false;
    }

    renderer_ = wlr_renderer_autocreate(backend_);
    if (!renderer_) {
        LOG_ERROR("Failed to create wlr_renderer");
        return false;
    }
    wlr_renderer_init_wl_display(renderer_, display_);

    allocator_ = wlr_allocator_autocreate(backend_, renderer_);
    if (!allocator_) {
        LOG_ERROR("Failed to create wlr_allocator");
        return false;
    }

    compositor_    = wlr_compositor_create(display_, 5, renderer_);
    subcompositor_ = wlr_subcompositor_create(display_);
    wlr_data_device_manager_create(display_);

    output_layout_ = wlr_output_layout_create(display_);

    // Create scene graph
    scene_ = wlr_scene_create();
    wlr_scene_attach_output_layout(scene_, output_layout_);

    // Output listener
    new_output_listener_.notify = onNewOutput;
    wl_signal_add(&backend_->events.new_output, &new_output_listener_);

    seat_ = wlr_seat_create(display_, "seat0");

    // Create input manager (Tasks 21-30) - replaces the old simple new_input listener
    input_manager_ = std::make_unique<InputManager>(seat_, backend_,
                                                     output_layout_, display_);
    input_manager_->setScene(scene_);
    input_manager_->setupBackendListeners();

    xdg_shell_ = wlr_xdg_shell_create(display_, 3);
    new_xdg_surface_listener_.notify = onNewXdgSurface;
    wl_signal_add(&xdg_shell_->events.new_surface, &new_xdg_surface_listener_);

    // XWayland
    xwayland_.init(*this);

    LOG_INFO("Server initialised successfully");
    return true;
}

bool Server::loadConfig(const std::string& path) {
    LOG_INFO("Loading configuration{}", path.empty() ? "" : " from " + path);
    // TODO: parse config file and populate window_rules_, workspace_rules_,
    //       input device configs, keybindings, gesture bindings, etc.
    return true;
}

void Server::run() {
    const char* socket = wl_display_add_socket_auto(display_);
    if (!socket) {
        LOG_ERROR("Failed to open Wayland socket");
        return;
    }

    if (!wlr_backend_start(backend_)) {
        LOG_ERROR("Failed to start wlr_backend");
        return;
    }

    LOG_INFO("Running on WAYLAND_DISPLAY={}", socket);
    setenv("WAYLAND_DISPLAY", socket, 1);

    wl_display_run(display_);
}

void Server::quit() {
    if (display_) {
        wl_display_terminate(display_);
    }
}

// ---------------------------------------------------------------------------
// Static callbacks
// ---------------------------------------------------------------------------

void Server::onNewOutput(wl_listener* listener, void* data) {
    Server* server = wl_container_of(listener, server, new_output_listener_);
    auto* output = static_cast<wlr_output*>(data);

    LOG_INFO("New output: {}", output->name);

    if (!wlr_output_init_render(output, server->allocator_, server->renderer_)) {
        LOG_ERROR("Failed to init render for output {}", output->name);
        return;
    }

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);

    auto* mode = wlr_output_preferred_mode(output);
    if (mode) {
        wlr_output_state_set_mode(&state, mode);
    }

    wlr_output_commit_state(output, &state);
    wlr_output_state_finish(&state);

    wlr_output_layout_add_auto(server->output_layout_, output);
}

void Server::onNewXdgSurface(wl_listener* listener, void* data) {
    Server* server = wl_container_of(listener, server, new_xdg_surface_listener_);
    auto* xdg_surface = static_cast<wlr_xdg_surface*>(data);

    if (xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
        LOG_DEBUG("New XDG toplevel surface");
        // TODO: create Surface wrapper, apply window rules, emit signal
        server->sig_window_create_.emit(0);
    }
}

} // namespace eternal

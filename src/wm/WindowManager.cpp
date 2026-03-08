#include "WindowManager.hpp"
#include <stdexcept>
#include <iostream>

extern "C" {
#include <wlr/util/log.h>
}

WindowManager::WindowManager() : 
    display(nullptr), backend(nullptr), renderer(nullptr),
    allocator(nullptr), compositor(nullptr), subcompositor(nullptr),
    output_layout(nullptr) {}

WindowManager::~WindowManager() {
    if (display) {
        wl_display_destroy_clients(display);
        wl_display_destroy(display);
    }
}

bool WindowManager::init() {
    wlr_log_init(WLR_DEBUG, nullptr);

    display = wl_display_create();
    if (!display) {
        wlr_log(WLR_ERROR, "Failed to create Wayland display");
        return false;
    }

    backend = wlr_backend_autocreate(wl_display_get_event_loop(display), nullptr);
    if (!backend) {
        wlr_log(WLR_ERROR, "Failed to create wlr_backend");
        return false;
    }

    renderer = wlr_renderer_autocreate(backend);
    if (!renderer) {
        wlr_log(WLR_ERROR, "Failed to create wlr_renderer");
        return false;
    }

    wlr_renderer_init_wl_display(renderer, display);

    allocator = wlr_allocator_autocreate(backend, renderer);
    if (!allocator) {
        wlr_log(WLR_ERROR, "Failed to create wlr_allocator");
        return false;
    }

    compositor = wlr_compositor_create(display, 5, renderer);
    subcompositor = wlr_subcompositor_create(display);
    wlr_data_device_manager_create(display);

    output_layout = wlr_output_layout_create(display);

    new_output_listener.notify = server_new_output;
    wl_signal_add(&backend->events.new_output, &new_output_listener);

    seat = wlr_seat_create(display, "seat0");

    new_input_listener.notify = server_new_input;
    wl_signal_add(&backend->events.new_input, &new_input_listener);

    wl_list_init(&views);
    xdg_shell = wlr_xdg_shell_create(display, 3);
    new_xdg_surface_listener.notify = server_new_xdg_surface;
    wl_signal_add(&xdg_shell->events.new_surface, &new_xdg_surface_listener);

    return true;
}

void WindowManager::server_new_output(wl_listener* listener, void* data) {
    WindowManager* wm = wl_container_of(listener, wm, new_output_listener);
    auto* wlr_output = static_cast<struct wlr_output*>(data);

    wlr_log(WLR_INFO, "New output detected: %s", wlr_output->name);

    if (!wlr_output_init_render(wlr_output, wm->allocator, wm->renderer)) {
        wlr_log(WLR_ERROR, "Failed to initialize output render");
        return;
    }

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);

    struct wlr_output_mode* mode = wlr_output_preferred_mode(wlr_output);
    if (mode != nullptr) {
        wlr_output_state_set_mode(&state, mode);
    }

    wlr_output_commit_state(wlr_output, &state);
    wlr_output_state_finish(&state);

    wlr_output_layout_add_auto(wm->output_layout, wlr_output);
}

void WindowManager::server_new_input(wl_listener* listener, void* data) {
    WindowManager* wm = wl_container_of(listener, wm, new_input_listener);
    auto* device = static_cast<struct wlr_input_device*>(data);
    wlr_log(WLR_INFO, "New input device detected: %s", device->name);
}

#include "View.hpp"
void WindowManager::server_new_xdg_surface(wl_listener* listener, void* data) {
    WindowManager* wm = wl_container_of(listener, wm, new_xdg_surface_listener);
    auto* xdg_surface = static_cast<wlr_xdg_surface*>(data);

    if (xdg_surface->role == WLR_XDG_SURFACE_ROLE_TOPLEVEL) {
        new View(wm, xdg_surface->toplevel);
    }
}

void WindowManager::run() {
    const char* socket = wl_display_add_socket_auto(display);
    if (!socket) {
        wlr_log(WLR_ERROR, "Failed to add Wayland socket");
        return;
    }

    if (!wlr_backend_start(backend)) {
        wlr_log(WLR_ERROR, "Failed to start backend");
        return;
    }

    wlr_log(WLR_INFO, "Running Wayland compositor on WAYLAND_DISPLAY=%s", socket);
    wl_display_run(display);
}

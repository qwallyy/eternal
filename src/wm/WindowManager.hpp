#pragma once

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
#include <wayland-server-core.h>
}

class View;

class WindowManager {
    friend class View;
public:
    WindowManager();
    ~WindowManager();

    bool init();
    void run();

private:
    wl_display* display;
    wlr_backend* backend;
    wlr_renderer* renderer;
    wlr_allocator* allocator;
    wlr_compositor* compositor;
    wlr_subcompositor* subcompositor;
    wlr_output_layout* output_layout;
    wlr_seat* seat;
    
    wlr_xdg_shell* xdg_shell;
    wl_list views;

    wl_listener new_output_listener;
    wl_listener new_input_listener;
    wl_listener new_xdg_surface_listener;

    static void server_new_output(wl_listener* listener, void* data);
    static void server_new_input(wl_listener* listener, void* data);
    static void server_new_xdg_surface(wl_listener* listener, void* data);
    
    // Output event handlers
    struct Output;
    static void output_frame(wl_listener* listener, void* data);
    static void output_destroy(wl_listener* listener, void* data);
};

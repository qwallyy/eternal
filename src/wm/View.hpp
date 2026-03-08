#pragma once
#include "WindowManager.hpp"
#include "eternal/utils/WlrSceneCompat.h"

class View {
public:
    View(WindowManager* wm, wlr_xdg_toplevel* toplevel);
    ~View();

    WindowManager* wm;
    wlr_xdg_toplevel* xdg_toplevel;
    wlr_scene_tree* scene_tree; // If we use wlr_scene
    wl_list link;

    wl_listener map_listener;
    wl_listener unmap_listener;
    wl_listener destroy_listener;
    wl_listener request_move_listener;
    wl_listener request_resize_listener;
    wl_listener request_maximize_listener;
    wl_listener request_fullscreen_listener;

    int x = 0, y = 0;

    static void view_map(wl_listener* listener, void* data);
    static void view_unmap(wl_listener* listener, void* data);
    static void view_destroy(wl_listener* listener, void* data);
    static void view_request_move(wl_listener* listener, void* data);
    static void view_request_resize(wl_listener* listener, void* data);
    static void view_request_maximize(wl_listener* listener, void* data);
    static void view_request_fullscreen(wl_listener* listener, void* data);
};

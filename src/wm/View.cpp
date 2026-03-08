#include "View.hpp"
#include <wlr/types/wlr_xdg_shell.h>

namespace {

void initListener(wl_listener& listener) {
    listener.notify = nullptr;
    wl_list_init(&listener.link);
}

void resetListener(wl_listener& listener) {
    wl_list_remove(&listener.link);
    wl_list_init(&listener.link);
    listener.notify = nullptr;
}

}

View::View(WindowManager* server, wlr_xdg_toplevel* toplevel) {
    this->wm = server;
    this->xdg_toplevel = toplevel;
    wl_list_init(&this->link);
    initListener(map_listener);
    initListener(unmap_listener);
    initListener(destroy_listener);

    // Listeners and scene setup will go here

    // Listen to the map event
    map_listener.notify = view_map;
    wl_signal_add(&toplevel->base->surface->events.map, &map_listener);

    unmap_listener.notify = view_unmap;
    wl_signal_add(&toplevel->base->surface->events.unmap, &unmap_listener);

    destroy_listener.notify = view_destroy;
    wl_signal_add(&toplevel->events.destroy, &destroy_listener);
}

View::~View() {
    resetListener(map_listener);
    resetListener(unmap_listener);
    resetListener(destroy_listener);
    wl_list_remove(&link);
    wl_list_init(&link);
}

void View::view_map(wl_listener* listener, void* data) {
    View* view = wl_container_of(listener, view, map_listener);
    wl_list_insert(&view->wm->views, &view->link);
}

void View::view_unmap(wl_listener* listener, void* data) {
    View* view = wl_container_of(listener, view, unmap_listener);
    wl_list_remove(&view->link);
    wl_list_init(&view->link);
}

void View::view_destroy(wl_listener* listener, void* data) {
    View* view = wl_container_of(listener, view, destroy_listener);
    delete view;
}

void View::view_request_move(wl_listener* listener, void* data) {}
void View::view_request_resize(wl_listener* listener, void* data) {}
void View::view_request_maximize(wl_listener* listener, void* data) {}
void View::view_request_fullscreen(wl_listener* listener, void* data) {}

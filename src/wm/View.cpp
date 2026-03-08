#include "View.hpp"
#include <wlr/types/wlr_xdg_shell.h>

View::View(WindowManager* server, wlr_xdg_toplevel* toplevel) {
    this->wm = server;
    this->xdg_toplevel = toplevel;

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
    wl_list_remove(&map_listener.link);
    wl_list_remove(&unmap_listener.link);
    wl_list_remove(&destroy_listener.link);
}

void View::view_map(wl_listener* listener, void* data) {
    View* view = wl_container_of(listener, view, map_listener);
    wl_list_insert(&view->wm->views, &view->link);
}

void View::view_unmap(wl_listener* listener, void* data) {
    View* view = wl_container_of(listener, view, unmap_listener);
    wl_list_remove(&view->link);
}

void View::view_destroy(wl_listener* listener, void* data) {
    View* view = wl_container_of(listener, view, destroy_listener);
    delete view;
}

void View::view_request_move(wl_listener* listener, void* data) {}
void View::view_request_resize(wl_listener* listener, void* data) {}
void View::view_request_maximize(wl_listener* listener, void* data) {}
void View::view_request_fullscreen(wl_listener* listener, void* data) {}

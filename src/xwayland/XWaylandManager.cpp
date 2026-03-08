#include "eternal/xwayland/XWaylandManager.hpp"
#include "eternal/utils/Logger.hpp"

#include <algorithm>
#include <unordered_map>
#include <vector>

extern "C" {
#ifdef ETERNAL_HAS_XWAYLAND
// wlr/xwayland/xwayland.h uses 'class' as a C struct member name,
// which is a reserved keyword in C++.  Work around it by macro-renaming.
#define class class_
#include <wlr/xwayland.h>
#include <wlr/xwayland/xwayland.h>
#undef class
#include <xcb/xcb.h>
#endif
#include <wayland-server-core.h>
#include <wlr/types/wlr_compositor.h>
}

namespace eternal {

// ---------------------------------------------------------------------------
// Impl (pimpl)
// ---------------------------------------------------------------------------

struct XWaylandManager::Impl {
#ifdef ETERNAL_HAS_XWAYLAND
    wlr_xwayland* xwayland = nullptr;
    wl_listener new_surface_listener{};
    wl_listener ready_listener{};
#endif
    Server* server = nullptr;
    bool running = false;
    bool enabled = true;

    // All tracked X11 surface states.
    std::unordered_map<wlr_xwayland_surface*, std::unique_ptr<X11SurfaceState>> surface_map;

    // Unmanaged (override-redirect) surface list for rendering order.
    std::vector<X11SurfaceState*> unmanaged;

    // EWMH atoms (set in onReady).
    struct {
        uint32_t net_wm_state = 0;
        uint32_t net_wm_state_fullscreen = 0;
        uint32_t net_wm_state_maximized_vert = 0;
        uint32_t net_wm_state_maximized_horz = 0;
        uint32_t net_wm_state_modal = 0;
        uint32_t net_wm_state_demands_attention = 0;
        uint32_t net_wm_state_skip_taskbar = 0;
        uint32_t net_wm_state_skip_pager = 0;
        uint32_t net_wm_window_type = 0;
        uint32_t net_wm_window_type_normal = 0;
        uint32_t net_wm_window_type_dialog = 0;
        uint32_t net_wm_window_type_utility = 0;
        uint32_t net_wm_window_type_toolbar = 0;
        uint32_t net_wm_window_type_splash = 0;
        uint32_t net_wm_window_type_menu = 0;
        uint32_t net_wm_window_type_tooltip = 0;
        uint32_t net_wm_window_type_notification = 0;
        uint32_t net_wm_window_type_dock = 0;
        uint32_t net_wm_window_type_desktop = 0;
        uint32_t net_active_window = 0;
        uint32_t wm_protocols = 0;
        uint32_t wm_delete_window = 0;
    } atoms;
};

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

XWaylandManager::XWaylandManager() : impl_(std::make_unique<Impl>()) {}

XWaylandManager::~XWaylandManager() {
    shutdown();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool XWaylandManager::init(Server& server) {
    impl_->server = &server;

#ifdef ETERNAL_HAS_XWAYLAND
    if (!impl_->enabled) {
        LOG_INFO("XWayland support is disabled");
        return true;
    }

    // In a real compositor this would call wlr_xwayland_create() and wire
    // up the new-surface and ready listeners.
    //
    // impl_->xwayland = wlr_xwayland_create(display, compositor, lazy);
    // impl_->new_surface_listener.notify = onNewSurface;
    // wl_signal_add(&impl_->xwayland->events.new_surface,
    //               &impl_->new_surface_listener);
    // impl_->ready_listener.notify = onReady;
    // wl_signal_add(&impl_->xwayland->events.ready,
    //               &impl_->ready_listener);

    impl_->running = true;
    LOG_INFO("XWayland initialised");
    return true;
#else
    LOG_WARN("Eternal was compiled without XWayland support");
    return false;
#endif
}

void XWaylandManager::shutdown() {
    if (!impl_ || !impl_->running) return;

#ifdef ETERNAL_HAS_XWAYLAND
    if (impl_->xwayland) {
        wl_list_remove(&impl_->new_surface_listener.link);
        wl_list_remove(&impl_->ready_listener.link);
        // wlr_xwayland_destroy(impl_->xwayland);
        impl_->xwayland = nullptr;
    }
#endif

    impl_->surface_map.clear();
    impl_->unmanaged.clear();
    impl_->running = false;
    LOG_INFO("XWayland shut down");
}

bool XWaylandManager::isRunning() const noexcept {
    return impl_ && impl_->running;
}

void XWaylandManager::setEnabled(bool enabled) {
    if (impl_) impl_->enabled = enabled;
}

void* XWaylandManager::getSurface(wlr_xwayland_surface* xwl_surface) const {
    if (!impl_) return nullptr;
    auto it = impl_->surface_map.find(xwl_surface);
    if (it != impl_->surface_map.end()) {
        return it->second->compositor_surface;
    }
    return nullptr;
}

X11SurfaceState* XWaylandManager::getState(wlr_xwayland_surface* xwl_surface) const {
    if (!impl_) return nullptr;
    auto it = impl_->surface_map.find(xwl_surface);
    if (it != impl_->surface_map.end()) return it->second.get();
    return nullptr;
}

std::vector<X11SurfaceState*> XWaylandManager::getUnmanagedSurfaces() const {
    if (!impl_) return {};
    return impl_->unmanaged;
}

std::vector<X11SurfaceState*> XWaylandManager::getManagedSurfaces() const {
    std::vector<X11SurfaceState*> result;
    if (!impl_) return result;
    for (auto& [key, state] : impl_->surface_map) {
        if (!state->override_redirect) {
            result.push_back(state.get());
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Window operations
// ---------------------------------------------------------------------------

void XWaylandManager::configureWindow(wlr_xwayland_surface* surface,
                                       int x, int y, int w, int h) {
#ifdef ETERNAL_HAS_XWAYLAND
    if (surface) {
        wlr_xwayland_surface_configure(surface, x, y, w, h);
    }
#else
    (void)surface; (void)x; (void)y; (void)w; (void)h;
#endif
}

void XWaylandManager::setNetWmState(wlr_xwayland_surface* surface,
                                     bool fullscreen, bool maximized, bool modal) {
#ifdef ETERNAL_HAS_XWAYLAND
    if (surface) {
        wlr_xwayland_surface_set_fullscreen(surface, fullscreen);
        wlr_xwayland_surface_set_maximized(surface, maximized);
    }
    auto* state = getState(surface);
    if (state) {
        state->fullscreen = fullscreen;
        state->maximized_h = maximized;
        state->maximized_v = maximized;
        state->modal = modal;
    }
#else
    (void)surface; (void)fullscreen; (void)maximized; (void)modal;
#endif
}

void XWaylandManager::focusWindow(wlr_xwayland_surface* surface) {
#ifdef ETERNAL_HAS_XWAYLAND
    if (!impl_ || !impl_->xwayland || !surface) return;
    wlr_xwayland_set_seat(impl_->xwayland, nullptr); // would pass actual seat
    // In production: set _NET_ACTIVE_WINDOW, restack to top.
    LOG_DEBUG("XWayland: focusing window");
#else
    (void)surface;
#endif
}

void XWaylandManager::closeWindow(wlr_xwayland_surface* surface) {
#ifdef ETERNAL_HAS_XWAYLAND
    if (surface) {
        wlr_xwayland_surface_close(surface);
    }
#else
    (void)surface;
#endif
}

// ---------------------------------------------------------------------------
// Top-level event handlers
// ---------------------------------------------------------------------------

void XWaylandManager::onNewSurface(struct wl_listener* listener, void* data) {
#ifdef ETERNAL_HAS_XWAYLAND
    auto* xwl_surface = static_cast<wlr_xwayland_surface*>(data);
    // The Impl is embedded in the listener; use container_of pattern.
    // For now, demonstrate the intended flow.

    LOG_DEBUG("XWayland: new surface (class='{}', title='{}')",
              xwl_surface->class_ ? xwl_surface->class_ : "",
              xwl_surface->title ? xwl_surface->title : "");

    // Create state and wire up per-surface listeners.
    auto state = std::make_unique<X11SurfaceState>();
    state->xwl_surface = xwl_surface;
    state->override_redirect = xwl_surface->override_redirect;

    // Set up listeners.
    state->map_listener.notify = onSurfaceMap;
    wl_signal_add(&xwl_surface->surface->events.map, &state->map_listener);

    state->unmap_listener.notify = onSurfaceUnmap;
    wl_signal_add(&xwl_surface->surface->events.unmap, &state->unmap_listener);

    state->destroy_listener.notify = onSurfaceDestroy;
    wl_signal_add(&xwl_surface->events.destroy, &state->destroy_listener);

    state->request_configure_listener.notify = onSurfaceRequestConfigure;
    wl_signal_add(&xwl_surface->events.request_configure,
                  &state->request_configure_listener);

    state->request_fullscreen_listener.notify = onSurfaceRequestFullscreen;
    wl_signal_add(&xwl_surface->events.request_fullscreen,
                  &state->request_fullscreen_listener);

    state->request_maximize_listener.notify = onSurfaceRequestMaximize;
    wl_signal_add(&xwl_surface->events.request_maximize,
                  &state->request_maximize_listener);

    state->set_title_listener.notify = onSurfaceSetTitle;
    wl_signal_add(&xwl_surface->events.set_title, &state->set_title_listener);

    state->set_class_listener.notify = onSurfaceSetClass;
    wl_signal_add(&xwl_surface->events.set_class, &state->set_class_listener);

    state->set_parent_listener.notify = onSurfaceSetParent;
    wl_signal_add(&xwl_surface->events.set_parent, &state->set_parent_listener);

    state->set_override_redirect_listener.notify = onSurfaceSetOverrideRedirect;
    wl_signal_add(&xwl_surface->events.set_override_redirect,
                  &state->set_override_redirect_listener);

    // Store in the map. We can't use container_of from a static function
    // without the Impl pointer, so in production we'd store a back-pointer.
    // impl_->surface_map[xwl_surface] = std::move(state);
#endif
    (void)listener;
    (void)data;
}

void XWaylandManager::onReady(struct wl_listener* listener, void* data) {
#ifdef ETERNAL_HAS_XWAYLAND
    LOG_INFO("XWayland: server ready, setting EWMH atoms");
    // In production:
    // - Intern all EWMH atoms via xcb_intern_atom.
    // - Set _NET_SUPPORTED, _NET_SUPPORTING_WM_CHECK.
    // - Set the seat on the xwayland server.
    // - Send initial _NET_ACTIVE_WINDOW.
#endif
    (void)listener;
    (void)data;
}

// ---------------------------------------------------------------------------
// Per-surface event handlers
// ---------------------------------------------------------------------------

void XWaylandManager::onSurfaceMap(struct wl_listener* listener, void* data) {
#ifdef ETERNAL_HAS_XWAYLAND
    LOG_DEBUG("XWayland: surface mapped");
    // In production:
    // 1. Read ICCCM size hints (WM_NORMAL_HINTS).
    // 2. Read EWMH window type (_NET_WM_WINDOW_TYPE).
    // 3. Read WM_HINTS for urgency.
    // 4. Create a compositor Surface and add to layout.
    // 5. If override_redirect, add to unmanaged list and position at
    //    the X11 client's requested coordinates.
    // 6. Track parent/transient relationship.
#endif
    (void)listener;
    (void)data;
}

void XWaylandManager::onSurfaceUnmap(struct wl_listener* listener, void* data) {
#ifdef ETERNAL_HAS_XWAYLAND
    LOG_DEBUG("XWayland: surface unmapped");
    // Remove from layout / unmanaged list.
#endif
    (void)listener;
    (void)data;
}

void XWaylandManager::onSurfaceDestroy(struct wl_listener* listener, void* data) {
#ifdef ETERNAL_HAS_XWAYLAND
    LOG_DEBUG("XWayland: surface destroyed");
    // Remove from surface_map, clean up listeners.
#endif
    (void)listener;
    (void)data;
}

void XWaylandManager::onSurfaceRequestConfigure(struct wl_listener* listener, void* data) {
#ifdef ETERNAL_HAS_XWAYLAND
    auto* event = static_cast<wlr_xwayland_surface_configure_event*>(data);
    LOG_DEBUG("XWayland: configure request ({}, {}, {}x{})",
              event->x, event->y, event->width, event->height);

    // For override-redirect windows, honour the request.
    // For managed windows, the compositor decides the geometry and
    // sends its own configure event.
    // state->requested_x = event->x; etc.
    wlr_xwayland_surface_configure(event->surface,
                                    event->x, event->y,
                                    event->width, event->height);
#endif
    (void)listener;
    (void)data;
}

void XWaylandManager::onSurfaceRequestFullscreen(struct wl_listener* listener, void* data) {
#ifdef ETERNAL_HAS_XWAYLAND
    LOG_DEBUG("XWayland: fullscreen request");
    // Toggle fullscreen via compositor layout.
#endif
    (void)listener;
    (void)data;
}

void XWaylandManager::onSurfaceRequestMaximize(struct wl_listener* listener, void* data) {
#ifdef ETERNAL_HAS_XWAYLAND
    LOG_DEBUG("XWayland: maximize request");
#endif
    (void)listener;
    (void)data;
}

void XWaylandManager::onSurfaceRequestMove(struct wl_listener* listener, void* data) {
#ifdef ETERNAL_HAS_XWAYLAND
    LOG_DEBUG("XWayland: move request");
    // Initiate interactive move in the compositor.
#endif
    (void)listener;
    (void)data;
}

void XWaylandManager::onSurfaceRequestResize(struct wl_listener* listener, void* data) {
#ifdef ETERNAL_HAS_XWAYLAND
    LOG_DEBUG("XWayland: resize request");
#endif
    (void)listener;
    (void)data;
}

void XWaylandManager::onSurfaceSetTitle(struct wl_listener* listener, void* data) {
#ifdef ETERNAL_HAS_XWAYLAND
    LOG_DEBUG("XWayland: title changed");
    // Update compositor Surface title.
#endif
    (void)listener;
    (void)data;
}

void XWaylandManager::onSurfaceSetClass(struct wl_listener* listener, void* data) {
#ifdef ETERNAL_HAS_XWAYLAND
    LOG_DEBUG("XWayland: class changed");
    // Update compositor Surface app_id.
#endif
    (void)listener;
    (void)data;
}

void XWaylandManager::onSurfaceSetParent(struct wl_listener* listener, void* data) {
#ifdef ETERNAL_HAS_XWAYLAND
    LOG_DEBUG("XWayland: parent changed");
    // Update parent/transient relationship.
#endif
    (void)listener;
    (void)data;
}

void XWaylandManager::onSurfaceSetHints(struct wl_listener* listener, void* data) {
#ifdef ETERNAL_HAS_XWAYLAND
    LOG_DEBUG("XWayland: hints changed");
    // Re-read WM_HINTS for urgency flag.
#endif
    (void)listener;
    (void)data;
}

void XWaylandManager::onSurfaceSetOverrideRedirect(struct wl_listener* listener, void* data) {
#ifdef ETERNAL_HAS_XWAYLAND
    LOG_DEBUG("XWayland: override_redirect changed");
    // Move between managed and unmanaged tracking.
#endif
    (void)listener;
    (void)data;
}

// ---------------------------------------------------------------------------
// ICCCM / EWMH helpers
// ---------------------------------------------------------------------------

void XWaylandManager::readSizeHints(X11SurfaceState* state) {
    if (!state || !state->xwl_surface) return;

#ifdef ETERNAL_HAS_XWAYLAND
    auto* hints = state->xwl_surface->size_hints;
    if (!hints) return;

    state->size_hints.has_min_size = true;
    state->size_hints.min_width = hints->min_width;
    state->size_hints.min_height = hints->min_height;

    if (hints->max_width > 0 && hints->max_height > 0) {
        state->size_hints.has_max_size = true;
        state->size_hints.max_width = hints->max_width;
        state->size_hints.max_height = hints->max_height;
    }

    if (hints->base_width > 0 && hints->base_height > 0) {
        state->size_hints.has_base_size = true;
        state->size_hints.base_width = hints->base_width;
        state->size_hints.base_height = hints->base_height;
    }

    if (hints->width_inc > 0 && hints->height_inc > 0) {
        state->size_hints.has_resize_inc = true;
        state->size_hints.width_inc = hints->width_inc;
        state->size_hints.height_inc = hints->height_inc;
    }

    if (hints->min_aspect_num > 0 && hints->min_aspect_den > 0) {
        state->size_hints.has_aspect = true;
        state->size_hints.min_aspect = static_cast<float>(hints->min_aspect_num)
                                     / static_cast<float>(hints->min_aspect_den);
        state->size_hints.max_aspect = static_cast<float>(hints->max_aspect_num)
                                     / static_cast<float>(hints->max_aspect_den);
    }
#endif
}

void XWaylandManager::readWindowType(X11SurfaceState* state) {
    if (!state || !state->xwl_surface) return;

#ifdef ETERNAL_HAS_XWAYLAND
    // In production, read _NET_WM_WINDOW_TYPE from the surface's atoms.
    // Map the atom to our enum.
    size_t n = state->xwl_surface->window_type_len;
    if (n == 0) {
        state->window_type = X11WindowType::Normal;
        return;
    }

    xcb_atom_t* types = state->xwl_surface->window_type;
    auto& atoms = impl_->atoms;

    for (size_t i = 0; i < n; ++i) {
        if (types[i] == atoms.net_wm_window_type_dialog) {
            state->window_type = X11WindowType::Dialog;
            return;
        }
        if (types[i] == atoms.net_wm_window_type_utility) {
            state->window_type = X11WindowType::Utility;
            return;
        }
        if (types[i] == atoms.net_wm_window_type_toolbar) {
            state->window_type = X11WindowType::Toolbar;
            return;
        }
        if (types[i] == atoms.net_wm_window_type_splash) {
            state->window_type = X11WindowType::Splash;
            return;
        }
        if (types[i] == atoms.net_wm_window_type_menu) {
            state->window_type = X11WindowType::Menu;
            return;
        }
        if (types[i] == atoms.net_wm_window_type_tooltip) {
            state->window_type = X11WindowType::Tooltip;
            return;
        }
        if (types[i] == atoms.net_wm_window_type_notification) {
            state->window_type = X11WindowType::Notification;
            return;
        }
        if (types[i] == atoms.net_wm_window_type_dock) {
            state->window_type = X11WindowType::Dock;
            return;
        }
        if (types[i] == atoms.net_wm_window_type_desktop) {
            state->window_type = X11WindowType::Desktop;
            return;
        }
    }

    state->window_type = X11WindowType::Normal;
#endif
}

void XWaylandManager::updateParentRelationship(X11SurfaceState* state) {
    if (!state || !state->xwl_surface) return;

#ifdef ETERNAL_HAS_XWAYLAND
    auto* parent = state->xwl_surface->parent;
    state->parent = parent;

    // Update the parent's children list.
    if (parent) {
        auto* parent_state = getState(parent);
        if (parent_state) {
            auto& children = parent_state->children;
            if (std::find(children.begin(), children.end(), state->xwl_surface) == children.end()) {
                children.push_back(state->xwl_surface);
            }
        }
    }
#endif
}

void XWaylandManager::handleOverrideRedirect(wlr_xwayland_surface* surface) {
    LOG_DEBUG("XWayland: handling override-redirect window");

#ifdef ETERNAL_HAS_XWAYLAND
    auto* state = getState(surface);
    if (!state) return;

    // Override-redirect windows (menus, tooltips, popups) are positioned
    // at the X11 client's requested coordinates without tiling.
    state->override_redirect = true;
    trackUnmanaged(state);
#else
    (void)surface;
#endif
}

void XWaylandManager::configureX11Window(wlr_xwayland_surface* surface) {
    LOG_DEBUG("XWayland: configuring X11 window");

#ifdef ETERNAL_HAS_XWAYLAND
    auto* state = getState(surface);
    if (!state) return;

    // For managed windows, apply the compositor's layout geometry.
    // Respect ICCCM size hints.
    int w = state->requested_width;
    int h = state->requested_height;

    if (state->size_hints.has_min_size) {
        w = std::max(w, state->size_hints.min_width);
        h = std::max(h, state->size_hints.min_height);
    }
    if (state->size_hints.has_max_size && state->size_hints.max_width > 0) {
        w = std::min(w, state->size_hints.max_width);
        h = std::min(h, state->size_hints.max_height);
    }
    if (state->size_hints.has_resize_inc && state->size_hints.width_inc > 0) {
        int base_w = state->size_hints.has_base_size ? state->size_hints.base_width : 0;
        int base_h = state->size_hints.has_base_size ? state->size_hints.base_height : 0;
        w = base_w + ((w - base_w) / state->size_hints.width_inc) * state->size_hints.width_inc;
        h = base_h + ((h - base_h) / state->size_hints.height_inc) * state->size_hints.height_inc;
    }

    configureWindow(surface, state->requested_x, state->requested_y, w, h);
#else
    (void)surface;
#endif
}

// ---------------------------------------------------------------------------
// Unmanaged surface tracking (Task 42)
// ---------------------------------------------------------------------------

void XWaylandManager::trackUnmanaged(X11SurfaceState* state) {
    if (!state || !impl_) return;
    auto& vec = impl_->unmanaged;
    if (std::find(vec.begin(), vec.end(), state) == vec.end()) {
        vec.push_back(state);
    }
}

void XWaylandManager::untrackUnmanaged(X11SurfaceState* state) {
    if (!state || !impl_) return;
    auto& vec = impl_->unmanaged;
    vec.erase(std::remove(vec.begin(), vec.end(), state), vec.end());
}

} // namespace eternal

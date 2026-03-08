#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

extern "C" {
#include <wayland-server-core.h>
}

// Forward declarations for wlroots types.
struct wlr_xwayland;
struct wlr_xwayland_surface;

namespace eternal {

class Server;
struct Surface;

// ---------------------------------------------------------------------------
// EWMH window type mapping
// ---------------------------------------------------------------------------

enum class X11WindowType : uint8_t {
    Normal,
    Dialog,
    Utility,
    Toolbar,
    Splash,
    Menu,
    DropdownMenu,
    PopupMenu,
    Tooltip,
    Notification,
    Combo,
    DND,
    Desktop,
    Dock,
    Unknown,
};

// ---------------------------------------------------------------------------
// ICCCM size hints (from WM_NORMAL_HINTS)
// ---------------------------------------------------------------------------

struct X11SizeHints {
    int min_width = 0;
    int min_height = 0;
    int max_width = 0;
    int max_height = 0;
    int base_width = 0;
    int base_height = 0;
    int width_inc = 0;
    int height_inc = 0;
    float min_aspect = 0.0f;
    float max_aspect = 0.0f;
    bool has_min_size = false;
    bool has_max_size = false;
    bool has_base_size = false;
    bool has_resize_inc = false;
    bool has_aspect = false;
};

// ---------------------------------------------------------------------------
// Per-X11-surface state
// ---------------------------------------------------------------------------

struct X11SurfaceState {
    wlr_xwayland_surface* xwl_surface = nullptr;
    Surface* compositor_surface = nullptr;

    // ICCCM / EWMH state.
    X11WindowType window_type = X11WindowType::Normal;
    X11SizeHints size_hints{};
    bool override_redirect = false;
    bool modal = false;
    bool fullscreen = false;
    bool maximized_h = false;
    bool maximized_v = false;
    bool demands_attention = false;
    bool skip_taskbar = false;
    bool skip_pager = false;

    // Parent / transient relationship.
    wlr_xwayland_surface* parent = nullptr;
    std::vector<wlr_xwayland_surface*> children;

    // Requested geometry from the X11 client.
    int requested_x = 0;
    int requested_y = 0;
    int requested_width = 0;
    int requested_height = 0;

    // wl_listener wrappers for per-surface events.
    struct wl_listener map_listener{};
    struct wl_listener unmap_listener{};
    struct wl_listener destroy_listener{};
    struct wl_listener request_configure_listener{};
    struct wl_listener request_fullscreen_listener{};
    struct wl_listener request_minimize_listener{};
    struct wl_listener request_maximize_listener{};
    struct wl_listener request_move_listener{};
    struct wl_listener request_resize_listener{};
    struct wl_listener set_title_listener{};
    struct wl_listener set_class_listener{};
    struct wl_listener set_role_listener{};
    struct wl_listener set_parent_listener{};
    struct wl_listener set_hints_listener{};
    struct wl_listener set_override_redirect_listener{};
};

// ---------------------------------------------------------------------------
// XWaylandManager -- manages the XWayland sub-process and X11 windows
// ---------------------------------------------------------------------------

class XWaylandManager {
public:
    XWaylandManager();
    ~XWaylandManager();

    XWaylandManager(const XWaylandManager&) = delete;
    XWaylandManager& operator=(const XWaylandManager&) = delete;

    /// Initialise XWayland for the given compositor server.
    bool init(Server& server);

    /// Tear down XWayland and destroy all managed state.
    void shutdown();

    /// Whether the XWayland sub-process is currently running.
    [[nodiscard]] bool isRunning() const noexcept;

    /// Enable or disable XWayland support at runtime.
    void setEnabled(bool enabled);

    /// Look up our internal surface wrapper for a given xwayland surface.
    [[nodiscard]] void* getSurface(wlr_xwayland_surface* xwl_surface) const;

    /// Look up the X11 surface state.
    [[nodiscard]] X11SurfaceState* getState(wlr_xwayland_surface* xwl_surface) const;

    /// Get all tracked override-redirect (unmanaged) surfaces.
    [[nodiscard]] std::vector<X11SurfaceState*> getUnmanagedSurfaces() const;

    /// Get all tracked managed surfaces.
    [[nodiscard]] std::vector<X11SurfaceState*> getManagedSurfaces() const;

    /// Send a configure event to an X11 window.
    void configureWindow(wlr_xwayland_surface* surface, int x, int y, int w, int h);

    /// Set EWMH _NET_WM_STATE on a surface.
    void setNetWmState(wlr_xwayland_surface* surface, bool fullscreen,
                       bool maximized, bool modal);

    /// Focus an X11 window (set _NET_ACTIVE_WINDOW, restack).
    void focusWindow(wlr_xwayland_surface* surface);

    /// Close an X11 window (send WM_DELETE_WINDOW or XKillClient).
    void closeWindow(wlr_xwayland_surface* surface);

private:
    // Top-level event handlers.
    static void onNewSurface(struct wl_listener* listener, void* data);
    static void onReady(struct wl_listener* listener, void* data);

    // Per-surface event handlers.
    static void onSurfaceMap(struct wl_listener* listener, void* data);
    static void onSurfaceUnmap(struct wl_listener* listener, void* data);
    static void onSurfaceDestroy(struct wl_listener* listener, void* data);
    static void onSurfaceRequestConfigure(struct wl_listener* listener, void* data);
    static void onSurfaceRequestFullscreen(struct wl_listener* listener, void* data);
    static void onSurfaceRequestMaximize(struct wl_listener* listener, void* data);
    static void onSurfaceRequestMove(struct wl_listener* listener, void* data);
    static void onSurfaceRequestResize(struct wl_listener* listener, void* data);
    static void onSurfaceSetTitle(struct wl_listener* listener, void* data);
    static void onSurfaceSetClass(struct wl_listener* listener, void* data);
    static void onSurfaceSetParent(struct wl_listener* listener, void* data);
    static void onSurfaceSetHints(struct wl_listener* listener, void* data);
    static void onSurfaceSetOverrideRedirect(struct wl_listener* listener, void* data);

    // ICCCM / EWMH helpers.
    void readSizeHints(X11SurfaceState* state);
    void readWindowType(X11SurfaceState* state);
    void updateParentRelationship(X11SurfaceState* state);
    void handleOverrideRedirect(wlr_xwayland_surface* surface);
    void configureX11Window(wlr_xwayland_surface* surface);

    // Unmanaged surface handling.
    void trackUnmanaged(X11SurfaceState* state);
    void untrackUnmanaged(X11SurfaceState* state);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace eternal

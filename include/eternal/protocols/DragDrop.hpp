#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

extern "C" {
#include <wayland-server-core.h>
}

struct wlr_drag;
struct wlr_seat;
struct wlr_surface;

namespace eternal {

class Server;
class Surface;
class Output;

// ---------------------------------------------------------------------------
// Drag state tracking
// ---------------------------------------------------------------------------

enum class DragState {
    Idle,
    Started,
    Entered,   // cursor entered a valid drop target
    Dragging,  // actively dragging over surfaces
    Dropped,
    Cancelled,
};

/// Represents an active drag-and-drop operation.
struct DragSession {
    wlr_drag* wlrDrag = nullptr;
    wlr_surface* iconSurface = nullptr;   // DnD icon following cursor
    Surface* sourceSurface = nullptr;
    Surface* targetSurface = nullptr;
    DragState state = DragState::Idle;

    // Icon position (layout coordinates).
    double iconX = 0.0;
    double iconY = 0.0;

    // MIME types offered by the drag source.
    std::vector<std::string> mimeTypes;
};

// ---------------------------------------------------------------------------
// DragDrop manager
// ---------------------------------------------------------------------------

class DragDrop {
public:
    explicit DragDrop(Server& server);
    ~DragDrop();

    DragDrop(const DragDrop&) = delete;
    DragDrop& operator=(const DragDrop&) = delete;

    /// Initialize drag and drop support.
    bool init();

    /// Shutdown and cancel any active drag.
    void shutdown();

    // ── Drag lifecycle ──────────────────────────────────────────────────

    /// Handle the start of a drag operation from a client.
    void onDragStart(wlr_drag* drag);

    /// Handle pointer motion during a drag.
    void onDragMotion(double layoutX, double layoutY, uint32_t time);

    /// Handle a drop (pointer button release during drag).
    void onDragDrop();

    /// Handle drag cancellation (escape or source destruction).
    void onDragCancel();

    /// Handle the drag icon destroy event.
    void onDragIconDestroy();

    // ── Drop target ──────────────────────────────────────────────────────

    /// Determine the drop target surface at the given layout coordinates.
    Surface* findDropTarget(double layoutX, double layoutY) const;

    /// Whether a drag is currently in progress.
    [[nodiscard]] bool isDragging() const {
        return m_session.state != DragState::Idle;
    }

    /// Get the current drag session.
    [[nodiscard]] const DragSession& getSession() const { return m_session; }

    // ── Icon rendering ──────────────────────────────────────────────────

    /// Update the drag icon position.
    void updateIconPosition(double layoutX, double layoutY);

    /// Get the drag icon surface for rendering.
    [[nodiscard]] wlr_surface* getIconSurface() const {
        return m_session.iconSurface;
    }

    /// Get the icon position in layout coordinates.
    void getIconPosition(double& outX, double& outY) const {
        outX = m_session.iconX;
        outY = m_session.iconY;
    }

    // ── Cross-surface support ───────────────────────────────────────────

    /// Check if the drag has crossed from one surface to another.
    bool hasCrossedSurface() const { return m_crossedSurface; }

    /// Send drag enter/leave events to surfaces.
    void updateDragFocus(Surface* newTarget);

private:
    /// Wire up listeners for a wlr_drag.
    void setupDragListeners();

    /// Clean up listeners.
    void cleanupDragListeners();

    Server& m_server;
    DragSession m_session;
    bool m_crossedSurface = false;

    // wl_listeners for the drag.
    struct wl_listener m_dragDestroyListener{};
    struct wl_listener m_dragIconMapListener{};
    struct wl_listener m_dragIconUnmapListener{};
    struct wl_listener m_dragIconDestroyListener{};

    // Seat request_start_drag listener.
    struct wl_listener m_requestStartDragListener{};
    struct wl_listener m_startDragListener{};
};

} // namespace eternal

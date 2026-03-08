#include "eternal/protocols/DragDrop.hpp"
#include "eternal/core/Server.hpp"
#include "eternal/core/Compositor.hpp"
#include "eternal/core/Surface.hpp"
#include "eternal/core/Output.hpp"
#include "eternal/utils/Logger.hpp"

extern "C" {
#include <wayland-server-core.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_seat.h>
}

namespace eternal {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

DragDrop::DragDrop(Server& server)
    : m_server(server) {}

DragDrop::~DragDrop() {
    shutdown();
}

bool DragDrop::init() {
    // Wire up the seat's request_start_drag and start_drag signals.
    // These are emitted by wlr_seat when a client initiates a drag.

    m_requestStartDragListener.notify =
        [](struct wl_listener* listener, void* data) {
            DragDrop* self = wl_container_of(listener, self,
                                              m_requestStartDragListener);
            auto* event = static_cast<struct wlr_seat_request_start_drag_event*>(data);

            // Validate the drag request.
            // In a simple compositor, always accept:
            // wlr_seat_start_drag(seat, event->drag, event->serial);
            (void)self;
            (void)event;
        };

    m_startDragListener.notify =
        [](struct wl_listener* listener, void* data) {
            DragDrop* self = wl_container_of(listener, self,
                                              m_startDragListener);
            auto* drag = static_cast<wlr_drag*>(data);
            self->onDragStart(drag);
        };

    // wl_signal_add(&seat->events.request_start_drag,
    //               &m_requestStartDragListener);
    // wl_signal_add(&seat->events.start_drag, &m_startDragListener);

    LOG_INFO("DragDrop: initialized");
    return true;
}

void DragDrop::shutdown() {
    if (isDragging()) {
        onDragCancel();
    }
    cleanupDragListeners();
    LOG_INFO("DragDrop: shut down");
}

// ---------------------------------------------------------------------------
// Drag lifecycle
// ---------------------------------------------------------------------------

void DragDrop::onDragStart(wlr_drag* drag) {
    if (!drag) return;

    m_session = {};
    m_session.wlrDrag = drag;
    m_session.state = DragState::Started;
    m_crossedSurface = false;

    // Check for drag icon.
    if (drag->icon) {
        m_session.iconSurface = drag->icon->surface;
    }

    // Extract MIME types from the data source.
    // if (drag->source) {
    //     struct wlr_data_source* source = drag->source;
    //     ... iterate source->mime_types ...
    // }

    setupDragListeners();

    LOG_INFO("DragDrop: drag started");
}

void DragDrop::onDragMotion(double layoutX, double layoutY, uint32_t time) {
    if (!isDragging()) return;

    // Update icon position.
    updateIconPosition(layoutX, layoutY);

    // Find the surface under the cursor.
    Surface* target = findDropTarget(layoutX, layoutY);
    if (target != m_session.targetSurface) {
        updateDragFocus(target);
    }

    // Send motion event to the current target.
    // if (m_session.targetSurface) {
    //     // Compute surface-local coordinates.
    //     double sx = layoutX - targetGeo.x;
    //     double sy = layoutY - targetGeo.y;
    //     wlr_seat_pointer_notify_motion(seat, time, sx, sy);
    // }

    (void)time;
}

void DragDrop::onDragDrop() {
    if (!isDragging()) return;

    m_session.state = DragState::Dropped;

    LOG_INFO("DragDrop: drop on '{}'",
             m_session.targetSurface
                 ? m_session.targetSurface->getTitle()
                 : "none");

    // Notify the target surface of the drop.
    // wlr_seat_pointer_notify_button(seat, ...);

    // Clean up.
    m_session.state = DragState::Idle;
    cleanupDragListeners();
}

void DragDrop::onDragCancel() {
    if (!isDragging()) return;

    m_session.state = DragState::Cancelled;
    LOG_INFO("DragDrop: drag cancelled");

    // Send leave to current target.
    if (m_session.targetSurface) {
        updateDragFocus(nullptr);
    }

    m_session = {};
    cleanupDragListeners();
}

void DragDrop::onDragIconDestroy() {
    m_session.iconSurface = nullptr;
    LOG_DEBUG("DragDrop: icon destroyed");
}

// ---------------------------------------------------------------------------
// Drop target
// ---------------------------------------------------------------------------

Surface* DragDrop::findDropTarget(double layoutX, double layoutY) const {
    return m_server.getCompositor().getSurfaceAt(layoutX, layoutY);
}

void DragDrop::updateIconPosition(double layoutX, double layoutY) {
    m_session.iconX = layoutX;
    m_session.iconY = layoutY;

    // Schedule damage at old and new icon position for rendering.
    // TODO: damage the icon region on the appropriate output.
}

void DragDrop::updateDragFocus(Surface* newTarget) {
    Surface* oldTarget = m_session.targetSurface;

    if (oldTarget == newTarget) return;

    // Send leave event to old target.
    if (oldTarget) {
        // wlr_seat_pointer_notify_clear_focus(seat);
        LOG_DEBUG("DragDrop: left '{}'", oldTarget->getTitle());
    }

    m_session.targetSurface = newTarget;

    // Send enter event to new target.
    if (newTarget) {
        m_session.state = DragState::Entered;
        m_crossedSurface = true;
        // wlr_seat_pointer_notify_enter(seat, newTarget->getWlrSurface(),
        //                               sx, sy);
        LOG_DEBUG("DragDrop: entered '{}'", newTarget->getTitle());
    } else {
        m_session.state = DragState::Dragging;
    }
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void DragDrop::setupDragListeners() {
    if (!m_session.wlrDrag) return;

    m_dragDestroyListener.notify =
        [](struct wl_listener* listener, void* data) {
            DragDrop* self = wl_container_of(listener, self,
                                              m_dragDestroyListener);
            self->onDragCancel();
            (void)data;
        };
    wl_signal_add(&m_session.wlrDrag->events.destroy, &m_dragDestroyListener);

    if (m_session.wlrDrag->icon) {
        m_dragIconDestroyListener.notify =
            [](struct wl_listener* listener, void* data) {
                DragDrop* self = wl_container_of(listener, self,
                                                  m_dragIconDestroyListener);
                self->onDragIconDestroy();
                (void)data;
            };
        wl_signal_add(&m_session.wlrDrag->icon->events.destroy,
                      &m_dragIconDestroyListener);
    }
}

void DragDrop::cleanupDragListeners() {
    if (m_session.wlrDrag) {
        wl_list_remove(&m_dragDestroyListener.link);
        if (m_session.iconSurface) {
            wl_list_remove(&m_dragIconDestroyListener.link);
        }
    }
}

} // namespace eternal

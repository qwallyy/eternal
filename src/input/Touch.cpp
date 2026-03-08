#include "eternal/input/Touch.hpp"
#include "eternal/input/InputManager.hpp"
#include "eternal/utils/Logger.hpp"

#include "eternal/utils/WlrSceneCompat.h"

extern "C" {
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output_layout.h>
#include <linux/input-event-codes.h>
}

#include <cassert>

namespace eternal {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

Touch::Touch(InputManager* manager)
    : manager_(manager)
{
    assert(manager_);
}

Touch::~Touch() {
    if (wlrTouch_) {
        wl_list_remove(&downListener_.link);
        wl_list_remove(&upListener_.link);
        wl_list_remove(&motionListener_.link);
        wl_list_remove(&cancelListener_.link);
        wl_list_remove(&frameListener_.link);
        wl_list_remove(&destroyListener_.link);
    }
}

// ---------------------------------------------------------------------------
// Device attachment
// ---------------------------------------------------------------------------

void Touch::attachDevice(wlr_touch* touch) {
    if (wlrTouch_) {
        wl_list_remove(&downListener_.link);
        wl_list_remove(&upListener_.link);
        wl_list_remove(&motionListener_.link);
        wl_list_remove(&cancelListener_.link);
        wl_list_remove(&frameListener_.link);
        wl_list_remove(&destroyListener_.link);
    }

    wlrTouch_ = touch;

    downListener_.notify = handleDownEvent;
    wl_signal_add(&wlrTouch_->events.down, &downListener_);

    upListener_.notify = handleUpEvent;
    wl_signal_add(&wlrTouch_->events.up, &upListener_);

    motionListener_.notify = handleMotionEvent;
    wl_signal_add(&wlrTouch_->events.motion, &motionListener_);

    cancelListener_.notify = handleCancelEvent;
    wl_signal_add(&wlrTouch_->events.cancel, &cancelListener_);

    frameListener_.notify = handleFrameEvent;
    wl_signal_add(&wlrTouch_->events.frame, &frameListener_);

    destroyListener_.notify = handleDestroy;
    wl_signal_add(&wlrTouch_->base.events.destroy, &destroyListener_);
}

// ---------------------------------------------------------------------------
// Task 28: Touch surface focus based on position
// ---------------------------------------------------------------------------

wlr_surface* Touch::surfaceAtTouch(double x, double y, double& sx, double& sy) const {
    wlr_scene* scene = manager_->getScene();
    if (!scene) return nullptr;

    wlr_scene_node* node = wlr_scene_node_at(&scene->tree.node, x, y, &sx, &sy);
    if (node && node->type == WLR_SCENE_NODE_BUFFER) {
        auto* sceneBuf = wlr_scene_buffer_from_node(node);
        auto* sceneSurface = wlr_scene_surface_try_from_buffer(sceneBuf);
        if (sceneSurface) {
            return sceneSurface->surface;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Task 28: Touch-to-pointer emulation helpers
// ---------------------------------------------------------------------------

void Touch::emulatePointerDown(double x, double y, uint32_t timeMsec) {
    wlr_cursor* cursor = manager_->getCursor();
    if (cursor) {
        wlr_cursor_warp(cursor, nullptr, x, y);
    }

    // Find surface and set pointer focus
    double sx = 0, sy = 0;
    wlr_surface* surface = surfaceAtTouch(x, y, sx, sy);
    if (surface) {
        wlr_seat_pointer_notify_enter(manager_->getSeat(), surface, sx, sy);
        wlr_seat_pointer_notify_button(manager_->getSeat(), timeMsec,
                                       BTN_LEFT, WL_POINTER_BUTTON_STATE_PRESSED);
    }
    emulationButtonDown_ = true;
}

void Touch::emulatePointerMotion(double x, double y, uint32_t timeMsec) {
    wlr_cursor* cursor = manager_->getCursor();
    if (cursor) {
        wlr_cursor_warp(cursor, nullptr, x, y);
    }

    double sx = 0, sy = 0;
    wlr_surface* surface = surfaceAtTouch(x, y, sx, sy);
    if (surface) {
        wlr_seat_pointer_notify_enter(manager_->getSeat(), surface, sx, sy);
        wlr_seat_pointer_notify_motion(manager_->getSeat(), timeMsec, sx, sy);
    }
}

void Touch::emulatePointerUp(uint32_t timeMsec) {
    if (emulationButtonDown_) {
        wlr_seat_pointer_notify_button(manager_->getSeat(), timeMsec,
                                       BTN_LEFT, WL_POINTER_BUTTON_STATE_RELEASED);
        emulationButtonDown_ = false;
    }
}

// ---------------------------------------------------------------------------
// Task 28: Multi-touch point tracking (down, motion, up, cancel, frame)
// ---------------------------------------------------------------------------

void Touch::handleDownEvent(wl_listener* listener, void* data) {
    Touch* self = wl_container_of(listener, self, downListener_);
    auto* event = static_cast<wlr_touch_down_event*>(data);

    self->handleDown(event->touch_id, event->x, event->y, event->time_msec);
}

void Touch::handleUpEvent(wl_listener* listener, void* data) {
    Touch* self = wl_container_of(listener, self, upListener_);
    auto* event = static_cast<wlr_touch_up_event*>(data);

    self->handleUp(event->touch_id, event->time_msec);
}

void Touch::handleMotionEvent(wl_listener* listener, void* data) {
    Touch* self = wl_container_of(listener, self, motionListener_);
    auto* event = static_cast<wlr_touch_motion_event*>(data);

    self->handleMotion(event->touch_id, event->x, event->y, event->time_msec);
}

void Touch::handleCancelEvent(wl_listener* listener, void* /*data*/) {
    Touch* self = wl_container_of(listener, self, cancelListener_);
    self->handleCancel();
}

void Touch::handleFrameEvent(wl_listener* listener, void* /*data*/) {
    Touch* self = wl_container_of(listener, self, frameListener_);
    self->handleFrame();
}

void Touch::handleDestroy(wl_listener* listener, void* /*data*/) {
    Touch* self = wl_container_of(listener, self, destroyListener_);

    wl_list_remove(&self->downListener_.link);
    wl_list_remove(&self->upListener_.link);
    wl_list_remove(&self->motionListener_.link);
    wl_list_remove(&self->cancelListener_.link);
    wl_list_remove(&self->frameListener_.link);
    wl_list_remove(&self->destroyListener_.link);
    self->wlrTouch_ = nullptr;
}

// ---------------------------------------------------------------------------
// Task 28: Touch down - track point, find focus surface, optionally emulate
// ---------------------------------------------------------------------------

void Touch::handleDown(int32_t id, double x, double y, uint32_t timeMsec) {
    // Find the surface under the touch point
    double sx = 0, sy = 0;
    wlr_surface* surface = surfaceAtTouch(x, y, sx, sy);

    // Track the point
    touchPoints_[id] = TouchPoint{id, x, y, true, surface};

    if (downCallback_) {
        downCallback_(id, x, y);
    }

    // Task 28: Touch-to-pointer emulation for single-touch
    if (emulatePointer_ && touchPoints_.size() == 1) {
        emulationPointId_ = id;
        emulatePointerDown(x, y, timeMsec);
    }

    // Forward touch event to focused surface
    if (surface) {
        wlr_seat_touch_notify_down(manager_->getSeat(), surface,
                                   timeMsec, id, sx, sy);
    }
}

// ---------------------------------------------------------------------------
// Task 28: Touch up
// ---------------------------------------------------------------------------

void Touch::handleUp(int32_t id, uint32_t timeMsec) {
    touchPoints_.erase(id);

    if (upCallback_) {
        upCallback_(id);
    }

    // Pointer emulation: release if this was the emulation point
    if (emulatePointer_ && id == emulationPointId_) {
        emulatePointerUp(timeMsec);
        emulationPointId_ = -1;
    }

    wlr_seat_touch_notify_up(manager_->getSeat(), timeMsec, id);
}

// ---------------------------------------------------------------------------
// Task 28: Touch motion
// ---------------------------------------------------------------------------

void Touch::handleMotion(int32_t id, double x, double y, uint32_t timeMsec) {
    auto it = touchPoints_.find(id);
    if (it != touchPoints_.end()) {
        it->second.x = x;
        it->second.y = y;

        // Update focused surface on motion
        double sx = 0, sy = 0;
        wlr_surface* surface = surfaceAtTouch(x, y, sx, sy);
        it->second.focusedSurface = surface;
    }

    if (motionCallback_) {
        motionCallback_(id, x, y);
    }

    // Pointer emulation
    if (emulatePointer_ && id == emulationPointId_) {
        emulatePointerMotion(x, y, timeMsec);
    }

    // Forward to seat
    wlr_seat_touch_notify_motion(manager_->getSeat(), timeMsec, id, x, y);
}

// ---------------------------------------------------------------------------
// Task 28: Touch cancel and frame
// ---------------------------------------------------------------------------

void Touch::handleCancel() {
    // Cancel pointer emulation if active
    if (emulatePointer_ && emulationPointId_ >= 0) {
        emulatePointerUp(0);
        emulationPointId_ = -1;
    }

    touchPoints_.clear();

    if (cancelCallback_) {
        cancelCallback_();
    }
}

void Touch::handleFrame() {
    if (frameCallback_) {
        frameCallback_();
    }
    wlr_seat_touch_notify_frame(manager_->getSeat());
}

// ---------------------------------------------------------------------------
// Point queries
// ---------------------------------------------------------------------------

const TouchPoint* Touch::getPoint(int32_t id) const {
    auto it = touchPoints_.find(id);
    if (it != touchPoints_.end()) {
        return &it->second;
    }
    return nullptr;
}

size_t Touch::getActivePointCount() const {
    return touchPoints_.size();
}

const std::unordered_map<int32_t, TouchPoint>& Touch::getActivePoints() const {
    return touchPoints_;
}

// ---------------------------------------------------------------------------
// Callback setters
// ---------------------------------------------------------------------------

void Touch::setDownCallback(DownCallback callback) { downCallback_ = std::move(callback); }
void Touch::setUpCallback(UpCallback callback) { upCallback_ = std::move(callback); }
void Touch::setMotionCallback(MotionCallback callback) { motionCallback_ = std::move(callback); }
void Touch::setCancelCallback(CancelCallback callback) { cancelCallback_ = std::move(callback); }
void Touch::setFrameCallback(FrameCallback callback) { frameCallback_ = std::move(callback); }

} // namespace eternal

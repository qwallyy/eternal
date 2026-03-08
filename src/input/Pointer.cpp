#include "eternal/input/Pointer.hpp"
#include "eternal/input/InputManager.hpp"
#include "eternal/input/Keyboard.hpp"
#include "eternal/input/KeybindManager.hpp"
#include "eternal/utils/Logger.hpp"

#include "eternal/utils/WlrSceneCompat.h"

extern "C" {
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <linux/input-event-codes.h>
}

#include <cassert>
#include <cmath>

namespace eternal {

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

} // namespace

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

Pointer::Pointer(InputManager* manager)
    : manager_(manager)
    , accelProfile_(AccelProfile::Adaptive)
    , scrollMethod_(ScrollMethod::TwoFinger)
{
    assert(manager_);

    initListener(motionListener_);
    initListener(motionAbsoluteListener_);
    initListener(buttonListener_);
    initListener(axisListener_);
    initListener(frameListener_);
    initListener(destroyListener_);
}

Pointer::~Pointer() {
    if (wlrPointer_) {
        resetListener(destroyListener_);
    }
}

// ---------------------------------------------------------------------------
// Device attachment
// ---------------------------------------------------------------------------

void Pointer::attachDevice(wlr_pointer* pointer) {
    // Remove old destroy listener if we're replacing a device
    if (wlrPointer_) {
        resetListener(destroyListener_);
    }

    wlrPointer_ = pointer;

    destroyListener_.notify = handleDestroy;
    wl_signal_add(&wlrPointer_->base.events.destroy, &destroyListener_);
}

// ---------------------------------------------------------------------------
// Static listener handlers
// These are now largely handled by InputManager's cursor listeners.
// We keep them for direct device attachment if needed.
// ---------------------------------------------------------------------------

void Pointer::handleMotionEvent(wl_listener* listener, void* data) {
    Pointer* self = wl_container_of(listener, self, motionListener_);
    auto* event = static_cast<wlr_pointer_motion_event*>(data);
    self->handleMotion(event->delta_x, event->delta_y, event->time_msec);
}

void Pointer::handleMotionAbsoluteEvent(wl_listener* listener, void* data) {
    Pointer* self = wl_container_of(listener, self, motionAbsoluteListener_);
    auto* event = static_cast<wlr_pointer_motion_absolute_event*>(data);
    self->handleMotionAbsolute(event->x, event->y, event->time_msec);
}

void Pointer::handleButtonEvent(wl_listener* listener, void* data) {
    Pointer* self = wl_container_of(listener, self, buttonListener_);
    auto* event = static_cast<wlr_pointer_button_event*>(data);
    self->handleButton(event->button, static_cast<wlr_button_state>(event->state),
                       event->time_msec);
}

void Pointer::handleAxisEvent(wl_listener* listener, void* data) {
    Pointer* self = wl_container_of(listener, self, axisListener_);
    auto* event = static_cast<wlr_pointer_axis_event*>(data);
    self->handleAxis(event->orientation, event->delta, event->delta_discrete,
                     event->source, event->relative_direction, event->time_msec);
}

void Pointer::handleFrameEvent(wl_listener* listener, void* /*data*/) {
    Pointer* self = wl_container_of(listener, self, frameListener_);
    self->handleFrame();
}

void Pointer::handleDestroy(wl_listener* listener, void* /*data*/) {
    Pointer* self = wl_container_of(listener, self, destroyListener_);
    resetListener(self->destroyListener_);
    self->wlrPointer_ = nullptr;
}

// ---------------------------------------------------------------------------
// Task 23: Pointer motion handling
// ---------------------------------------------------------------------------

void Pointer::handleMotion(double dx, double dy, uint32_t timeMsec) {
    // Apply sensitivity
    double sdx = dx * sensitivity_;
    double sdy = dy * sensitivity_;

    // Update local position tracking from the cursor
    wlr_cursor* cursor = manager_->getCursor();
    if (cursor) {
        cursorX_ = cursor->x;
        cursorY_ = cursor->y;
    } else {
        cursorX_ += sdx;
        cursorY_ += sdy;
    }

    clampCursorToOutput();

    // Handle interactive move/resize
    if (interactiveMode_ != InteractiveMode::None) {
        // Motion during drag: keybind manager handles the move/resize
        auto* keybindMgr = manager_->getKeybindManager();
        if (keybindMgr && keybindMgr->isDragActive()) {
            keybindMgr->onMouseDragMotion(sdx, sdy);
        }
        return;
    }

    if (motionCallback_) {
        motionCallback_(sdx, sdy);
    }

    // Process motion through scene tree and update focus
    processMotion(timeMsec);
}

void Pointer::handleMotionAbsolute(double x, double y, uint32_t timeMsec) {
    const double oldX = cursorX_;
    const double oldY = cursorY_;
    cursorX_ = x;
    cursorY_ = y;

    if (motionCallback_) {
        motionCallback_(x - oldX, y - oldY);
    }

    processMotion(timeMsec);
}

void Pointer::processMotion(uint32_t timeMsec) {
    // Task 23: Surface-under-cursor lookup via scene tree
    auto result = surfaceAtCursor();

    if (result.surface) {
        // Notify seat of pointer enter/motion on the surface
        wlr_seat_pointer_notify_enter(manager_->getSeat(), result.surface,
                                      result.sx, result.sy);
        wlr_seat_pointer_notify_motion(manager_->getSeat(), timeMsec,
                                       result.sx, result.sy);
    } else {
        // No surface under cursor; clear pointer focus and set default cursor
        wlr_seat_pointer_clear_focus(manager_->getSeat());
    }
}

// ---------------------------------------------------------------------------
// Task 23: Surface-under-cursor lookup via scene tree
// ---------------------------------------------------------------------------

Pointer::SurfaceAtResult Pointer::surfaceAtCursor() const {
    SurfaceAtResult result;

    wlr_scene* scene = manager_->getScene();
    if (!scene) return result;

    double sx = 0, sy = 0;
    wlr_scene_node* node = wlr_scene_node_at(
        &scene->tree.node, cursorX_, cursorY_, &sx, &sy);

    if (node && node->type == WLR_SCENE_NODE_BUFFER) {
        auto* sceneBuf = wlr_scene_buffer_from_node(node);
        auto* sceneSurface = wlr_scene_surface_try_from_buffer(sceneBuf);
        if (sceneSurface) {
            result.surface = sceneSurface->surface;
            result.sx = sx;
            result.sy = sy;
            result.node = node;
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// Task 23: Focus management
// ---------------------------------------------------------------------------

void Pointer::updateFocus(uint32_t timeMsec) {
    processMotion(timeMsec);
}

void Pointer::setFocusedSurface(wlr_surface* surface, double sx, double sy) {
    focusedSurface_ = surface;
    if (surface) {
        wlr_seat_pointer_notify_enter(manager_->getSeat(), surface, sx, sy);
    } else {
        wlr_seat_pointer_clear_focus(manager_->getSeat());
    }
}

// ---------------------------------------------------------------------------
// Task 24: Pointer button handling
// ---------------------------------------------------------------------------

void Pointer::handleButton(uint32_t button, wlr_button_state state, uint32_t timeMsec) {
    bool pressed = (state == WLR_BUTTON_PRESSED);
    uint32_t mods = manager_->getKeyboard()->getModifiers();

    // Task 24: Double-click detection
    if (pressed) {
        bool dblClick = isDoubleClick(button, timeMsec);
        if (dblClick) {
            LOG_DEBUG("Double-click detected on button {}", button);
        }
    }

    // Task 24: Interactive move/resize initiation on Super+click
    if (pressed && (mods & WLR_MODIFIER_LOGO)) {
        if (button == BTN_LEFT) {
            beginInteractiveMove();
            return;
        } else if (button == BTN_RIGHT) {
            // Resize from nearest edge
            beginInteractiveResize(0);
            return;
        }
    }

    // End interactive mode on button release
    if (!pressed && interactiveMode_ != InteractiveMode::None) {
        endInteractive();
    }

    // Task 24: Click-to-focus logic
    if (pressed) {
        auto result = surfaceAtCursor();
        if (result.surface) {
            // Focus the clicked surface for keyboard input too
            wlr_seat_keyboard_notify_enter(manager_->getSeat(), result.surface,
                nullptr, 0, nullptr);
        }
    }

    // User callback
    if (buttonCallback_) {
        buttonCallback_(button, state);
    }

    // Forward to focused surface via seat
    wlr_seat_pointer_notify_button(manager_->getSeat(), timeMsec, button,
                                   static_cast<enum wl_pointer_button_state>(state));
}

// ---------------------------------------------------------------------------
// Task 24: Double-click detection
// ---------------------------------------------------------------------------

bool Pointer::isDoubleClick(uint32_t button, uint32_t timeMsec) const {
    if (button != lastClickButton_) {
        lastClickButton_ = button;
        lastClickTime_ = timeMsec;
        lastClickX_ = cursorX_;
        lastClickY_ = cursorY_;
        return false;
    }

    uint32_t dt = timeMsec - lastClickTime_;
    double dist = std::sqrt((cursorX_ - lastClickX_) * (cursorX_ - lastClickX_) +
                            (cursorY_ - lastClickY_) * (cursorY_ - lastClickY_));

    // Reset tracking for next potential double click
    lastClickTime_ = timeMsec;
    lastClickX_ = cursorX_;
    lastClickY_ = cursorY_;

    return (dt <= kDoubleClickTimeMs && dist <= kDoubleClickDistancePx);
}

// ---------------------------------------------------------------------------
// Task 24: Interactive move/resize
// ---------------------------------------------------------------------------

void Pointer::beginInteractiveMove() {
    interactiveMode_ = InteractiveMode::Move;
    grabX_ = cursorX_;
    grabY_ = cursorY_;
    LOG_DEBUG("Begin interactive move at ({}, {})", grabX_, grabY_);
}

void Pointer::beginInteractiveResize(uint32_t edges) {
    interactiveMode_ = InteractiveMode::Resize;
    grabX_ = cursorX_;
    grabY_ = cursorY_;
    resizeEdges_ = edges;
    LOG_DEBUG("Begin interactive resize at ({}, {})", grabX_, grabY_);
}

void Pointer::endInteractive() {
    if (interactiveMode_ == InteractiveMode::None) return;

    LOG_DEBUG("End interactive mode");
    interactiveMode_ = InteractiveMode::None;

    auto* keybindMgr = manager_->getKeybindManager();
    if (keybindMgr && keybindMgr->isDragActive()) {
        keybindMgr->onMouseDragEnd();
    }
}

// ---------------------------------------------------------------------------
// Task 25: Pointer axis (scroll) handling
// ---------------------------------------------------------------------------

void Pointer::handleAxis(enum wl_pointer_axis orientation, double delta,
                          int32_t deltaDiscrete, enum wl_pointer_axis_source source,
                          enum wl_pointer_axis_relative_direction relativeDirection,
                          uint32_t timeMsec) {
    // Apply scroll factor
    double adjustedDelta = delta * scrollFactor_;
    int32_t adjustedDiscrete = static_cast<int32_t>(
        static_cast<double>(deltaDiscrete) * scrollFactor_);

    // Natural scroll toggle
    if (naturalScroll_) {
        adjustedDelta = -adjustedDelta;
        adjustedDiscrete = -adjustedDiscrete;
    }

    // User callback (e.g., for workspace switching on vertical scroll)
    if (axisCallback_) {
        axisCallback_(orientation, adjustedDelta);
    }

    // Forward to focused surface via seat
    wlr_seat_pointer_notify_axis(manager_->getSeat(), timeMsec,
                                 orientation, adjustedDelta, adjustedDiscrete,
                                 source, relativeDirection);
}

void Pointer::handleFrame() {
    wlr_seat_pointer_notify_frame(manager_->getSeat());
}

// ---------------------------------------------------------------------------
// Cursor manipulation
// ---------------------------------------------------------------------------

void Pointer::warpTo(double x, double y) {
    cursorX_ = x;
    cursorY_ = y;
    clampCursorToOutput();

    wlr_cursor* cursor = manager_->getCursor();
    if (cursor) {
        wlr_cursor_warp(cursor, nullptr, x, y);
    }
}

void Pointer::confineToOutput(wlr_output* output) {
    confinedOutput_ = output;
    clampCursorToOutput();
}

Pointer::Position Pointer::getPosition() const {
    wlr_cursor* cursor = manager_->getCursor();
    if (cursor) {
        return {cursor->x, cursor->y};
    }
    return {cursorX_, cursorY_};
}

// ---------------------------------------------------------------------------
// Configuration setters
// ---------------------------------------------------------------------------

void Pointer::setSensitivity(double sensitivity) { sensitivity_ = sensitivity; }
void Pointer::setAccelProfile(AccelProfile profile) { accelProfile_ = profile; }
void Pointer::setNaturalScroll(bool enabled) { naturalScroll_ = enabled; }
void Pointer::setScrollFactor(double factor) { scrollFactor_ = factor; }
void Pointer::setScrollMethod(ScrollMethod method) { scrollMethod_ = method; }
void Pointer::setLeftHanded(bool enabled) { leftHanded_ = enabled; }
void Pointer::setMiddleEmulation(bool enabled) { middleEmulation_ = enabled; }

void Pointer::setMotionCallback(MotionCallback callback) { motionCallback_ = std::move(callback); }
void Pointer::setButtonCallback(ButtonCallback callback) { buttonCallback_ = std::move(callback); }
void Pointer::setAxisCallback(AxisCallback callback) { axisCallback_ = std::move(callback); }

// ---------------------------------------------------------------------------
// Task 23: Confine cursor to output boundaries
// ---------------------------------------------------------------------------

void Pointer::clampCursorToOutput() {
    if (!confinedOutput_) return;

    int width = 0;
    int height = 0;
    wlr_output_effective_resolution(confinedOutput_, &width, &height);

    cursorX_ = std::clamp(cursorX_, 0.0, static_cast<double>(width));
    cursorY_ = std::clamp(cursorY_, 0.0, static_cast<double>(height));
}

} // namespace eternal

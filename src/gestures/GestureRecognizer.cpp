#include "eternal/gestures/GestureRecognizer.hpp"
#include "eternal/utils/Logger.hpp"

#include <cmath>

namespace eternal {

GestureRecognizer::GestureRecognizer() = default;
GestureRecognizer::~GestureRecognizer() = default;

// ---------------------------------------------------------------------------
// Task 26: libinput gesture event processing
// ---------------------------------------------------------------------------

void GestureRecognizer::processLibinputEvent(libinput_event* event) {
    auto type = libinput_event_get_type(event);

    switch (type) {
    case LIBINPUT_EVENT_GESTURE_SWIPE_BEGIN: {
        auto* gestureEvent = libinput_event_get_gesture_event(event);
        uint32_t fingers = static_cast<uint32_t>(
            libinput_event_gesture_get_finger_count(gestureEvent));
        onSwipeBegin(fingers);
        break;
    }
    case LIBINPUT_EVENT_GESTURE_SWIPE_UPDATE: {
        auto* gestureEvent = libinput_event_get_gesture_event(event);
        double dx = libinput_event_gesture_get_dx(gestureEvent);
        double dy = libinput_event_gesture_get_dy(gestureEvent);
        onSwipeUpdate(dx, dy);
        break;
    }
    case LIBINPUT_EVENT_GESTURE_SWIPE_END: {
        auto* gestureEvent = libinput_event_get_gesture_event(event);
        bool cancelled = libinput_event_gesture_get_cancelled(gestureEvent);
        onSwipeEnd(cancelled);
        break;
    }
    case LIBINPUT_EVENT_GESTURE_PINCH_BEGIN: {
        auto* gestureEvent = libinput_event_get_gesture_event(event);
        uint32_t fingers = static_cast<uint32_t>(
            libinput_event_gesture_get_finger_count(gestureEvent));
        onPinchBegin(fingers);
        break;
    }
    case LIBINPUT_EVENT_GESTURE_PINCH_UPDATE: {
        auto* gestureEvent = libinput_event_get_gesture_event(event);
        double scale = libinput_event_gesture_get_scale(gestureEvent);
        double rotation = libinput_event_gesture_get_angle_delta(gestureEvent);
        onPinchUpdate(scale, rotation);
        break;
    }
    case LIBINPUT_EVENT_GESTURE_PINCH_END: {
        auto* gestureEvent = libinput_event_get_gesture_event(event);
        bool cancelled = libinput_event_gesture_get_cancelled(gestureEvent);
        onPinchEnd(cancelled);
        break;
    }
    case LIBINPUT_EVENT_GESTURE_HOLD_BEGIN: {
        auto* gestureEvent = libinput_event_get_gesture_event(event);
        uint32_t fingers = static_cast<uint32_t>(
            libinput_event_gesture_get_finger_count(gestureEvent));
        onHoldBegin(fingers);
        break;
    }
    case LIBINPUT_EVENT_GESTURE_HOLD_END: {
        auto* gestureEvent = libinput_event_get_gesture_event(event);
        bool cancelled = libinput_event_gesture_get_cancelled(gestureEvent);
        onHoldEnd(cancelled);
        break;
    }
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Task 26: 3-finger swipe tracking, 4-finger swipe, direction detection
// ---------------------------------------------------------------------------

void GestureRecognizer::onSwipeBegin(uint32_t fingers) {
    gestureActive_ = true;
    activeGestureType_ = GestureType::Swipe;
    activeFingers_ = fingers;
    swipeDx_ = 0.0;
    swipeDy_ = 0.0;

    LOG_DEBUG("Swipe gesture begin: {} fingers", fingers);

    if (swipeBeginDispatcher_) {
        swipeBeginDispatcher_(fingers);
    }
}

void GestureRecognizer::onSwipeUpdate(double dx, double dy) {
    if (!gestureActive_ || activeGestureType_ != GestureType::Swipe) return;

    // 1:1 gesture tracking for smooth animations
    swipeDx_ += dx;
    swipeDy_ += dy;

    if (swipeUpdateDispatcher_) {
        SwipeEvent event{
            activeFingers_,
            swipeDx_,
            swipeDy_,
            computeSwipeDirection(),
            false
        };
        swipeUpdateDispatcher_(event);
    }
}

void GestureRecognizer::onSwipeEnd(bool cancelled) {
    if (!gestureActive_ || activeGestureType_ != GestureType::Swipe) return;

    GestureDirection direction = computeSwipeDirection();

    if (swipeEndDispatcher_) {
        SwipeEvent event{
            activeFingers_,
            swipeDx_,
            swipeDy_,
            direction,
            cancelled
        };
        swipeEndDispatcher_(event);
    }

    // Fire configurable dispatcher actions unless cancelled
    if (!cancelled) {
        fireGestureActions(GestureType::Swipe, activeFingers_, direction, 1.0);
    } else {
        LOG_DEBUG("Swipe gesture cancelled ({} fingers)", activeFingers_);
    }

    gestureActive_ = false;
    activeFingers_ = 0;
    swipeDx_ = 0.0;
    swipeDy_ = 0.0;
}

// ---------------------------------------------------------------------------
// Task 26: Pinch gesture (scale + rotation)
// ---------------------------------------------------------------------------

void GestureRecognizer::onPinchBegin(uint32_t fingers) {
    gestureActive_ = true;
    activeGestureType_ = GestureType::Pinch;
    activeFingers_ = fingers;
    pinchScale_ = 1.0;
    pinchRotation_ = 0.0;

    LOG_DEBUG("Pinch gesture begin: {} fingers", fingers);

    if (pinchBeginDispatcher_) {
        pinchBeginDispatcher_(fingers);
    }
}

void GestureRecognizer::onPinchUpdate(double scale, double rotation) {
    if (!gestureActive_ || activeGestureType_ != GestureType::Pinch) return;

    // 1:1 tracking
    pinchScale_ = scale;
    pinchRotation_ += rotation;

    if (pinchUpdateDispatcher_) {
        PinchEvent event{activeFingers_, pinchScale_, pinchRotation_, false};
        pinchUpdateDispatcher_(event);
    }
}

void GestureRecognizer::onPinchEnd(bool cancelled) {
    if (!gestureActive_ || activeGestureType_ != GestureType::Pinch) return;

    if (pinchEndDispatcher_) {
        PinchEvent event{activeFingers_, pinchScale_, pinchRotation_, cancelled};
        pinchEndDispatcher_(event);
    }

    if (!cancelled) {
        fireGestureActions(GestureType::Pinch, activeFingers_,
                           GestureDirection::None, pinchScale_);
    } else {
        LOG_DEBUG("Pinch gesture cancelled ({} fingers)", activeFingers_);
    }

    gestureActive_ = false;
    activeFingers_ = 0;
    pinchScale_ = 1.0;
    pinchRotation_ = 0.0;
}

// ---------------------------------------------------------------------------
// Hold gestures
// ---------------------------------------------------------------------------

void GestureRecognizer::onHoldBegin(uint32_t fingers) {
    gestureActive_ = true;
    activeGestureType_ = GestureType::Hold;
    activeFingers_ = fingers;

    LOG_DEBUG("Hold gesture begin: {} fingers", fingers);

    if (holdBeginDispatcher_) {
        holdBeginDispatcher_(fingers);
    }
}

void GestureRecognizer::onHoldEnd(bool cancelled) {
    if (!gestureActive_ || activeGestureType_ != GestureType::Hold) return;

    if (holdEndDispatcher_) {
        holdEndDispatcher_(activeFingers_);
    }

    if (!cancelled) {
        fireGestureActions(GestureType::Hold, activeFingers_,
                           GestureDirection::None, 1.0);
    }

    gestureActive_ = false;
    activeFingers_ = 0;
}

// ---------------------------------------------------------------------------
// Dispatcher setters
// ---------------------------------------------------------------------------

void GestureRecognizer::setSwipeBeginDispatcher(SwipeBeginDispatcher dispatcher) {
    swipeBeginDispatcher_ = std::move(dispatcher);
}
void GestureRecognizer::setSwipeUpdateDispatcher(SwipeUpdateDispatcher dispatcher) {
    swipeUpdateDispatcher_ = std::move(dispatcher);
}
void GestureRecognizer::setSwipeEndDispatcher(SwipeEndDispatcher dispatcher) {
    swipeEndDispatcher_ = std::move(dispatcher);
}
void GestureRecognizer::setPinchBeginDispatcher(PinchBeginDispatcher dispatcher) {
    pinchBeginDispatcher_ = std::move(dispatcher);
}
void GestureRecognizer::setPinchUpdateDispatcher(PinchUpdateDispatcher dispatcher) {
    pinchUpdateDispatcher_ = std::move(dispatcher);
}
void GestureRecognizer::setPinchEndDispatcher(PinchEndDispatcher dispatcher) {
    pinchEndDispatcher_ = std::move(dispatcher);
}
void GestureRecognizer::setHoldBeginDispatcher(HoldBeginDispatcher dispatcher) {
    holdBeginDispatcher_ = std::move(dispatcher);
}
void GestureRecognizer::setHoldEndDispatcher(HoldEndDispatcher dispatcher) {
    holdEndDispatcher_ = std::move(dispatcher);
}

// ---------------------------------------------------------------------------
// Task 26: Configurable gesture-to-dispatcher action mapping
// ---------------------------------------------------------------------------

void GestureRecognizer::addGestureBinding(const GestureActionBinding& binding) {
    gestureBindings_.push_back(binding);
}

void GestureRecognizer::clearGestureBindings() {
    gestureBindings_.clear();
}

void GestureRecognizer::registerDispatcher(const std::string& name, DispatcherFunc func) {
    dispatchers_[name] = std::move(func);
}

void GestureRecognizer::fireGestureActions(GestureType type, uint32_t fingers,
                                            GestureDirection direction, double scale) {
    for (const auto& binding : gestureBindings_) {
        if (binding.type != type) continue;
        if (binding.fingers != fingers) continue;

        // For swipe, check direction match (None = any direction)
        if (type == GestureType::Swipe && binding.direction != GestureDirection::None) {
            if (binding.direction != direction) continue;
        }

        auto it = dispatchers_.find(binding.dispatcher);
        if (it != dispatchers_.end()) {
            LOG_DEBUG("Firing gesture action: {} ({})", binding.dispatcher, binding.args);
            it->second(binding.args);
        }
    }
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void GestureRecognizer::setSwipeThreshold(double threshold) {
    swipeThreshold_ = threshold;
}

void GestureRecognizer::setPinchThreshold(double threshold) {
    pinchThreshold_ = threshold;
}

void GestureRecognizer::setHoldDuration(uint32_t milliseconds) {
    holdDuration_ = milliseconds;
}

// ---------------------------------------------------------------------------
// Query
// ---------------------------------------------------------------------------

std::optional<GestureType> GestureRecognizer::getActiveGestureType() const {
    if (!gestureActive_) return std::nullopt;
    return activeGestureType_;
}

// ---------------------------------------------------------------------------
// Direction computation
// ---------------------------------------------------------------------------

GestureDirection GestureRecognizer::computeSwipeDirection() const {
    double absDx = std::abs(swipeDx_);
    double absDy = std::abs(swipeDy_);

    double maxDelta = std::max(absDx, absDy);
    if (maxDelta < swipeThreshold_) {
        return GestureDirection::None;
    }

    if (absDx > absDy) {
        return (swipeDx_ > 0) ? GestureDirection::Right : GestureDirection::Left;
    } else {
        return (swipeDy_ > 0) ? GestureDirection::Down : GestureDirection::Up;
    }
}

} // namespace eternal

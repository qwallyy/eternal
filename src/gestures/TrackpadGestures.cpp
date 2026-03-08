#include "eternal/gestures/TrackpadGestures.hpp"
#include "eternal/gestures/GestureRecognizer.hpp"

#include <cassert>
#include <cmath>

namespace eternal {

TrackpadGestures::TrackpadGestures(GestureRecognizer* recognizer)
    : recognizer_(recognizer)
{
    assert(recognizer_);
}

TrackpadGestures::~TrackpadGestures() {
    if (bound_) {
        unbind();
    }
}

void TrackpadGestures::bind() {
    if (bound_) return;

    recognizer_->setSwipeBeginDispatcher([this](uint32_t fingers) {
        handleSwipeBegin(fingers);
    });

    recognizer_->setSwipeUpdateDispatcher([this](const SwipeEvent& event) {
        handleSwipeUpdate(event.dx, event.dy, event.fingers);
    });

    recognizer_->setSwipeEndDispatcher([this](const SwipeEvent& event) {
        if (!event.cancelled) {
            handleSwipeEnd(event.fingers, event.dx, event.dy);
        }
    });

    recognizer_->setPinchBeginDispatcher([this](uint32_t fingers) {
        handlePinchBegin(fingers);
    });

    recognizer_->setPinchUpdateDispatcher([this](const PinchEvent& event) {
        handlePinchUpdate(event.scale, event.rotation, event.fingers);
    });

    recognizer_->setPinchEndDispatcher([this](const PinchEvent& event) {
        if (!event.cancelled) {
            handlePinchEnd(event.fingers, event.scale);
        }
    });

    recognizer_->setHoldBeginDispatcher([this](uint32_t fingers) {
        handleHoldBegin(fingers);
    });

    recognizer_->setHoldEndDispatcher([this](uint32_t fingers) {
        handleHoldEnd(fingers);
    });

    bound_ = true;
}

void TrackpadGestures::unbind() {
    if (!bound_) return;

    recognizer_->setSwipeBeginDispatcher(nullptr);
    recognizer_->setSwipeUpdateDispatcher(nullptr);
    recognizer_->setSwipeEndDispatcher(nullptr);
    recognizer_->setPinchBeginDispatcher(nullptr);
    recognizer_->setPinchUpdateDispatcher(nullptr);
    recognizer_->setPinchEndDispatcher(nullptr);
    recognizer_->setHoldBeginDispatcher(nullptr);
    recognizer_->setHoldEndDispatcher(nullptr);

    bound_ = false;
}

// --- Configuration ---

void TrackpadGestures::setSwipeAction(uint32_t fingers, TrackpadAction action) {
    swipeActions_[fingers] = action;
    swipeCustomCommands_.erase(fingers);
}

void TrackpadGestures::setSwipeAction(uint32_t fingers, const std::string& customCommand) {
    swipeActions_[fingers] = TrackpadAction::Custom;
    swipeCustomCommands_[fingers] = customCommand;
}

void TrackpadGestures::setPinchAction(uint32_t fingers, TrackpadAction action) {
    pinchActions_[fingers] = action;
}

void TrackpadGestures::setHoldAction(uint32_t fingers, TrackpadAction action) {
    holdActions_[fingers] = action;
}

TrackpadAction TrackpadGestures::getSwipeAction(uint32_t fingers) const {
    auto it = swipeActions_.find(fingers);
    return (it != swipeActions_.end()) ? it->second : TrackpadAction::None;
}

TrackpadAction TrackpadGestures::getPinchAction(uint32_t fingers) const {
    auto it = pinchActions_.find(fingers);
    return (it != pinchActions_.end()) ? it->second : TrackpadAction::None;
}

TrackpadAction TrackpadGestures::getHoldAction(uint32_t fingers) const {
    auto it = holdActions_.find(fingers);
    return (it != holdActions_.end()) ? it->second : TrackpadAction::None;
}

// --- Callbacks ---

void TrackpadGestures::setWorkspaceSwitchCallback(WorkspaceSwitchCallback callback) {
    workspaceSwitchCallback_ = std::move(callback);
}

void TrackpadGestures::setOverviewCallback(OverviewCallback callback) {
    overviewCallback_ = std::move(callback);
}

void TrackpadGestures::setZoomCallback(ZoomCallback callback) {
    zoomCallback_ = std::move(callback);
}

void TrackpadGestures::setDragCallback(DragCallback callback) {
    dragCallback_ = std::move(callback);
}

void TrackpadGestures::setCustomCallback(CustomCallback callback) {
    customCallback_ = std::move(callback);
}

// --- Internal handlers ---

void TrackpadGestures::handleSwipeBegin(uint32_t fingers) {
    if (!enabled_) return;
    (void)fingers;
}

void TrackpadGestures::handleSwipeUpdate(double dx, double dy, uint32_t fingers) {
    if (!enabled_) return;

    auto action = getSwipeAction(fingers);
    if (action == TrackpadAction::HoldDrag && dragCallback_) {
        dragCallback_(dx, dy);
    }
}

void TrackpadGestures::handleSwipeEnd(uint32_t fingers, double dx, double dy) {
    if (!enabled_) return;

    auto action = getSwipeAction(fingers);
    dispatchAction(action, fingers, dx, dy, 1.0);
}

void TrackpadGestures::handlePinchBegin(uint32_t fingers) {
    if (!enabled_) return;
    (void)fingers;
}

void TrackpadGestures::handlePinchUpdate(double scale, double rotation, uint32_t fingers) {
    if (!enabled_) return;
    (void)rotation;

    auto action = getPinchAction(fingers);
    if (action == TrackpadAction::PinchZoom && zoomCallback_) {
        zoomCallback_(scale);
    }
}

void TrackpadGestures::handlePinchEnd(uint32_t fingers, double scale) {
    if (!enabled_) return;

    auto action = getPinchAction(fingers);
    dispatchAction(action, fingers, 0.0, 0.0, scale);
}

void TrackpadGestures::handleHoldBegin(uint32_t fingers) {
    if (!enabled_) return;
    (void)fingers;
}

void TrackpadGestures::handleHoldEnd(uint32_t fingers) {
    if (!enabled_) return;

    auto action = getHoldAction(fingers);
    dispatchAction(action, fingers, 0.0, 0.0, 1.0);
}

void TrackpadGestures::dispatchAction(TrackpadAction action, uint32_t fingers,
                                       double dx, double dy, double scale) {
    switch (action) {
    case TrackpadAction::None:
        break;

    case TrackpadAction::WorkspaceSwitch:
        if (workspaceSwitchCallback_) {
            int direction = (std::abs(dx) > std::abs(dy))
                ? (dx > 0 ? 1 : -1)
                : (dy > 0 ? 1 : -1);
            workspaceSwitchCallback_(direction);
        }
        break;

    case TrackpadAction::Overview:
        if (overviewCallback_) {
            overviewCallback_(dy < 0);
        }
        break;

    case TrackpadAction::PinchZoom:
        if (zoomCallback_) {
            zoomCallback_(scale);
        }
        break;

    case TrackpadAction::PinchOverview:
        if (overviewCallback_) {
            overviewCallback_(scale < 1.0);
        }
        break;

    case TrackpadAction::HoldDrag:
        break;

    case TrackpadAction::Custom: {
        if (customCallback_) {
            auto it = swipeCustomCommands_.find(fingers);
            if (it != swipeCustomCommands_.end()) {
                customCallback_(it->second);
            }
        }
        break;
    }
    }
}

} // namespace eternal

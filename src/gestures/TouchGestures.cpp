#include "eternal/gestures/TouchGestures.hpp"
#include "eternal/input/Touch.hpp"

#include <cassert>
#include <cmath>

namespace eternal {

TouchGestures::TouchGestures(Touch* touch)
    : touch_(touch)
{
    assert(touch_);
}

TouchGestures::~TouchGestures() {
    if (bound_) {
        unbind();
    }
}

void TouchGestures::bind() {
    if (bound_) return;

    touch_->setDownCallback([this](int32_t id, double x, double y) {
        processTouchDown(id, x, y);
    });

    touch_->setUpCallback([this](int32_t id) {
        processTouchUp(id);
    });

    touch_->setMotionCallback([this](int32_t id, double x, double y) {
        processTouchMotion(id, x, y);
    });

    touch_->setCancelCallback([this]() {
        processTouchCancel();
    });

    touch_->setFrameCallback([this]() {
        processTouchFrame();
    });

    bound_ = true;
}

void TouchGestures::unbind() {
    if (!bound_) return;

    touch_->setDownCallback(nullptr);
    touch_->setUpCallback(nullptr);
    touch_->setMotionCallback(nullptr);
    touch_->setCancelCallback(nullptr);
    touch_->setFrameCallback(nullptr);

    bound_ = false;
}

// --- Configuration ---

void TouchGestures::setEdgeSwipeAction(EdgeSwipeDirection direction, const std::string& action) {
    edgeSwipeActions_[direction] = action;
}

void TouchGestures::setEdgeSwipeConfig(const EdgeSwipeConfig& config) {
    edgeSwipeConfig_ = config;
}

void TouchGestures::setTapZoneAction(TapZone zone, const std::string& action) {
    tapZoneActions_[zone] = action;
}

void TouchGestures::setTapZoneConfig(const TapZoneConfig& config) {
    tapZoneConfig_ = config;
}

void TouchGestures::setLongPressDuration(uint32_t milliseconds) {
    longPressDuration_ = milliseconds;
}

void TouchGestures::setMultiFingerTapAction(uint32_t fingers, const std::string& action) {
    multiFingerTapActions_[fingers] = action;
}

void TouchGestures::setScreenDimensions(uint32_t width, uint32_t height) {
    screenWidth_ = width;
    screenHeight_ = height;
}

// --- Callbacks ---

void TouchGestures::setEdgeSwipeCallback(EdgeSwipeCallback callback) {
    edgeSwipeCallback_ = std::move(callback);
}

void TouchGestures::setTapZoneCallback(TapZoneCallback callback) {
    tapZoneCallback_ = std::move(callback);
}

void TouchGestures::setLongPressCallback(LongPressCallback callback) {
    longPressCallback_ = std::move(callback);
}

void TouchGestures::setMultiFingerTapCallback(MultiFingerTapCallback callback) {
    multiFingerTapCallback_ = std::move(callback);
}

// --- Touch event processing ---

void TouchGestures::processTouchDown(int32_t id, double x, double y) {
    if (!enabled_) return;

    TrackedTouch tracked;
    tracked.id = id;
    tracked.startX = x;
    tracked.startY = y;
    tracked.currentX = x;
    tracked.currentY = y;
    tracked.startTime = std::chrono::steady_clock::now();
    tracked.moved = false;

    trackedTouches_[id] = tracked;
}

void TouchGestures::processTouchUp(int32_t id) {
    if (!enabled_) return;

    auto it = trackedTouches_.find(id);
    if (it == trackedTouches_.end()) return;

    auto& tracked = it->second;

    // Check for edge swipe
    if (tracked.moved && edgeSwipeConfig_.enabled) {
        checkEdgeSwipe(tracked);
    }

    // Check for tap in zone (not moved significantly)
    if (!tracked.moved && tapZoneConfig_.enabled) {
        checkTapZone(tracked);
    }

    // Check for long press
    if (!tracked.moved && isLongPress(tracked) && longPressCallback_) {
        longPressCallback_(tracked.startX, tracked.startY);
    }

    trackedTouches_.erase(it);

    // Check for multi-finger tap when all fingers are lifted
    // (we check on each up; the last finger up triggers the check)
    if (trackedTouches_.empty()) {
        // The total finger count was the max number of simultaneous touches
        // We track this implicitly: the count of touches that were just active
    }
}

void TouchGestures::processTouchMotion(int32_t id, double x, double y) {
    if (!enabled_) return;

    auto it = trackedTouches_.find(id);
    if (it == trackedTouches_.end()) return;

    auto& tracked = it->second;
    tracked.currentX = x;
    tracked.currentY = y;

    // Determine if the touch has moved significantly
    double dx = x - tracked.startX;
    double dy = y - tracked.startY;
    double distance = std::sqrt(dx * dx + dy * dy);

    if (distance > 10.0) { // 10 pixel movement threshold
        tracked.moved = true;
    }
}

void TouchGestures::processTouchCancel() {
    trackedTouches_.clear();
}

void TouchGestures::processTouchFrame() {
    if (!enabled_) return;

    // Check for multi-finger tap: all touches are recent and none moved
    checkMultiFingerTap();
}

// --- Detection helpers ---

bool TouchGestures::isEdgeTouch(double x, double y, EdgeSwipeDirection& outDirection) const {
    double threshold = edgeSwipeConfig_.edgeThreshold;
    double sw = static_cast<double>(screenWidth_);
    double sh = static_cast<double>(screenHeight_);

    if (x < threshold) {
        outDirection = EdgeSwipeDirection::Left;
        return true;
    }
    if (x > sw - threshold) {
        outDirection = EdgeSwipeDirection::Right;
        return true;
    }
    if (y < threshold) {
        outDirection = EdgeSwipeDirection::Top;
        return true;
    }
    if (y > sh - threshold) {
        outDirection = EdgeSwipeDirection::Bottom;
        return true;
    }
    return false;
}

TapZone TouchGestures::determineTapZone(double x, double y) const {
    double sw = static_cast<double>(screenWidth_);
    double sh = static_cast<double>(screenHeight_);
    double zw = sw * tapZoneConfig_.zoneWidth;
    double zh = sh * tapZoneConfig_.zoneHeight;

    bool isLeft = x < zw;
    bool isRight = x > sw - zw;
    bool isTop = y < zh;
    bool isBottom = y > sh - zh;

    if (isTop && isLeft) return TapZone::TopLeft;
    if (isTop && isRight) return TapZone::TopRight;
    if (isBottom && isLeft) return TapZone::BottomLeft;
    if (isBottom && isRight) return TapZone::BottomRight;
    return TapZone::Center;
}

bool TouchGestures::isLongPress(const TrackedTouch& touch) const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - touch.startTime).count();
    return static_cast<uint32_t>(elapsed) >= longPressDuration_;
}

void TouchGestures::checkEdgeSwipe(const TrackedTouch& touch) {
    EdgeSwipeDirection direction;
    if (!isEdgeTouch(touch.startX, touch.startY, direction)) return;

    double dx = touch.currentX - touch.startX;
    double dy = touch.currentY - touch.startY;
    double distance = std::sqrt(dx * dx + dy * dy);

    if (distance < edgeSwipeConfig_.swipeDistance) return;

    // Verify swipe direction matches the edge
    bool valid = false;
    switch (direction) {
    case EdgeSwipeDirection::Left:
        valid = dx > 0; // Swiping inward from left edge
        break;
    case EdgeSwipeDirection::Right:
        valid = dx < 0; // Swiping inward from right edge
        break;
    case EdgeSwipeDirection::Top:
        valid = dy > 0; // Swiping downward from top edge
        break;
    case EdgeSwipeDirection::Bottom:
        valid = dy < 0; // Swiping upward from bottom edge
        break;
    }

    if (valid && edgeSwipeCallback_) {
        edgeSwipeCallback_(direction);
    }
}

void TouchGestures::checkTapZone(const TrackedTouch& touch) {
    if (!tapZoneCallback_) return;

    TapZone zone = determineTapZone(touch.startX, touch.startY);

    // Only trigger if there is an action mapped for this zone
    auto it = tapZoneActions_.find(zone);
    if (it != tapZoneActions_.end()) {
        tapZoneCallback_(zone);
    }
}

void TouchGestures::checkMultiFingerTap() {
    if (trackedTouches_.empty()) return;

    // All current touches must be stationary (not moved)
    uint32_t fingerCount = 0;
    for (const auto& [id, touch] : trackedTouches_) {
        if (touch.moved) return; // At least one finger moved, not a tap
        ++fingerCount;
    }

    if (fingerCount < 2) return; // Need at least 2 fingers for multi-finger tap

    // Check if there is an action mapped
    auto it = multiFingerTapActions_.find(fingerCount);
    if (it != multiFingerTapActions_.end() && multiFingerTapCallback_) {
        multiFingerTapCallback_(fingerCount);
    }
}

} // namespace eternal

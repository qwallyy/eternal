#pragma once

extern "C" {
#include <libinput.h>
}

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>

namespace eternal {

enum class GestureType {
    Swipe,
    Pinch,
    Hold
};

enum class GestureDirection {
    None,
    Up,
    Down,
    Left,
    Right
};

struct SwipeEvent {
    uint32_t fingers;
    double dx;
    double dy;
    GestureDirection direction;
    bool cancelled;    // True if the gesture was cancelled
};

struct PinchEvent {
    uint32_t fingers;
    double scale;
    double rotation;
    bool cancelled;
};

struct HoldEvent {
    uint32_t fingers;
    bool cancelled;
};

/// Configurable gesture action mapping
struct GestureActionBinding {
    GestureType type;
    uint32_t fingers;
    GestureDirection direction;  // For swipe; None means any direction
    std::string dispatcher;
    std::string args;
};

class GestureRecognizer {
public:
    GestureRecognizer();
    ~GestureRecognizer();

    GestureRecognizer(const GestureRecognizer&) = delete;
    GestureRecognizer& operator=(const GestureRecognizer&) = delete;

    // -----------------------------------------------------------------------
    // libinput gesture event processing (Task 26)
    // -----------------------------------------------------------------------

    /// Process a raw libinput gesture event. Called by InputManager.
    void processLibinputEvent(libinput_event* event);

    // -----------------------------------------------------------------------
    // Swipe gesture events
    // -----------------------------------------------------------------------
    void onSwipeBegin(uint32_t fingers);
    void onSwipeUpdate(double dx, double dy);
    void onSwipeEnd(bool cancelled = false);

    // -----------------------------------------------------------------------
    // Pinch gesture events
    // -----------------------------------------------------------------------
    void onPinchBegin(uint32_t fingers);
    void onPinchUpdate(double scale, double rotation);
    void onPinchEnd(bool cancelled = false);

    // -----------------------------------------------------------------------
    // Hold gesture events
    // -----------------------------------------------------------------------
    void onHoldBegin(uint32_t fingers);
    void onHoldEnd(bool cancelled = false);

    // Dispatchers - external code registers these to react to recognized gestures
    using SwipeBeginDispatcher = std::function<void(uint32_t fingers)>;
    using SwipeUpdateDispatcher = std::function<void(const SwipeEvent& event)>;
    using SwipeEndDispatcher = std::function<void(const SwipeEvent& event)>;
    using PinchBeginDispatcher = std::function<void(uint32_t fingers)>;
    using PinchUpdateDispatcher = std::function<void(const PinchEvent& event)>;
    using PinchEndDispatcher = std::function<void(const PinchEvent& event)>;
    using HoldBeginDispatcher = std::function<void(uint32_t fingers)>;
    using HoldEndDispatcher = std::function<void(uint32_t fingers)>;

    void setSwipeBeginDispatcher(SwipeBeginDispatcher dispatcher);
    void setSwipeUpdateDispatcher(SwipeUpdateDispatcher dispatcher);
    void setSwipeEndDispatcher(SwipeEndDispatcher dispatcher);
    void setPinchBeginDispatcher(PinchBeginDispatcher dispatcher);
    void setPinchUpdateDispatcher(PinchUpdateDispatcher dispatcher);
    void setPinchEndDispatcher(PinchEndDispatcher dispatcher);
    void setHoldBeginDispatcher(HoldBeginDispatcher dispatcher);
    void setHoldEndDispatcher(HoldEndDispatcher dispatcher);

    // -----------------------------------------------------------------------
    // Configurable gesture-to-dispatcher action mapping (Task 26)
    // -----------------------------------------------------------------------

    void addGestureBinding(const GestureActionBinding& binding);
    void clearGestureBindings();

    using DispatcherFunc = std::function<void(const std::string& args)>;
    void registerDispatcher(const std::string& name, DispatcherFunc func);

    // Configuration
    void setSwipeThreshold(double threshold);
    void setPinchThreshold(double threshold);
    void setHoldDuration(uint32_t milliseconds);

    [[nodiscard]] double getSwipeThreshold() const { return swipeThreshold_; }
    [[nodiscard]] double getPinchThreshold() const { return pinchThreshold_; }
    [[nodiscard]] uint32_t getHoldDuration() const { return holdDuration_; }

    // Query active gesture
    [[nodiscard]] bool isGestureActive() const { return gestureActive_; }
    [[nodiscard]] std::optional<GestureType> getActiveGestureType() const;
    [[nodiscard]] uint32_t getActiveFingers() const { return activeFingers_; }

    // 1:1 gesture tracking values (Task 26)
    [[nodiscard]] double getSwipeDx() const { return swipeDx_; }
    [[nodiscard]] double getSwipeDy() const { return swipeDy_; }
    [[nodiscard]] double getPinchScale() const { return pinchScale_; }
    [[nodiscard]] double getPinchRotation() const { return pinchRotation_; }

private:
    // Thresholds
    double swipeThreshold_ = 50.0;
    double pinchThreshold_ = 0.1;
    uint32_t holdDuration_ = 500;

    // Active gesture state
    bool gestureActive_ = false;
    GestureType activeGestureType_ = GestureType::Swipe;
    uint32_t activeFingers_ = 0;

    // Accumulated swipe delta (1:1 tracking)
    double swipeDx_ = 0.0;
    double swipeDy_ = 0.0;

    // Accumulated pinch state (1:1 tracking)
    double pinchScale_ = 1.0;
    double pinchRotation_ = 0.0;

    // Dispatchers
    SwipeBeginDispatcher swipeBeginDispatcher_;
    SwipeUpdateDispatcher swipeUpdateDispatcher_;
    SwipeEndDispatcher swipeEndDispatcher_;
    PinchBeginDispatcher pinchBeginDispatcher_;
    PinchUpdateDispatcher pinchUpdateDispatcher_;
    PinchEndDispatcher pinchEndDispatcher_;
    HoldBeginDispatcher holdBeginDispatcher_;
    HoldEndDispatcher holdEndDispatcher_;

    // Gesture action bindings (Task 26)
    std::vector<GestureActionBinding> gestureBindings_;
    std::unordered_map<std::string, DispatcherFunc> dispatchers_;

    [[nodiscard]] GestureDirection computeSwipeDirection() const;

    /// Fire dispatcher actions matching the completed gesture
    void fireGestureActions(GestureType type, uint32_t fingers,
                            GestureDirection direction, double scale);
};

} // namespace eternal

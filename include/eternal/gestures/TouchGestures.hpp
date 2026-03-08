#pragma once

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <string>
#include <chrono>

namespace eternal {

class Touch;

enum class EdgeSwipeDirection {
    Left,
    Right,
    Top,
    Bottom
};

enum class TapZone {
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
    Center
};

struct EdgeSwipeConfig {
    double edgeThreshold = 20.0;    // Pixels from screen edge to trigger
    double swipeDistance = 100.0;    // Minimum distance to recognize swipe
    bool enabled = true;
};

struct TapZoneConfig {
    double zoneWidth = 0.33;        // Fraction of screen width for corner zones
    double zoneHeight = 0.33;       // Fraction of screen height for corner zones
    bool enabled = true;
};

class TouchGestures {
public:
    explicit TouchGestures(Touch* touch);
    ~TouchGestures();

    TouchGestures(const TouchGestures&) = delete;
    TouchGestures& operator=(const TouchGestures&) = delete;

    // Edge swipe configuration
    void setEdgeSwipeAction(EdgeSwipeDirection direction, const std::string& action);
    void setEdgeSwipeConfig(const EdgeSwipeConfig& config);
    [[nodiscard]] const EdgeSwipeConfig& getEdgeSwipeConfig() const { return edgeSwipeConfig_; }

    // Tap zone configuration
    void setTapZoneAction(TapZone zone, const std::string& action);
    void setTapZoneConfig(const TapZoneConfig& config);
    [[nodiscard]] const TapZoneConfig& getTapZoneConfig() const { return tapZoneConfig_; }

    // Long press configuration
    void setLongPressDuration(uint32_t milliseconds);
    [[nodiscard]] uint32_t getLongPressDuration() const { return longPressDuration_; }

    // Multi-finger tap configuration
    void setMultiFingerTapAction(uint32_t fingers, const std::string& action);

    // Screen dimensions (needed for zone calculation)
    void setScreenDimensions(uint32_t width, uint32_t height);

    // Callbacks
    using EdgeSwipeCallback = std::function<void(EdgeSwipeDirection direction)>;
    using TapZoneCallback = std::function<void(TapZone zone)>;
    using LongPressCallback = std::function<void(double x, double y)>;
    using MultiFingerTapCallback = std::function<void(uint32_t fingers)>;

    void setEdgeSwipeCallback(EdgeSwipeCallback callback);
    void setTapZoneCallback(TapZoneCallback callback);
    void setLongPressCallback(LongPressCallback callback);
    void setMultiFingerTapCallback(MultiFingerTapCallback callback);

    // Enable/disable
    void setEnabled(bool enabled) { enabled_ = enabled; }
    [[nodiscard]] bool isEnabled() const { return enabled_; }

    // Bind to touch input
    void bind();
    void unbind();

    // Process touch events (called from Touch callbacks)
    void processTouchDown(int32_t id, double x, double y);
    void processTouchUp(int32_t id);
    void processTouchMotion(int32_t id, double x, double y);
    void processTouchCancel();
    void processTouchFrame();

private:
    Touch* touch_ = nullptr;
    bool enabled_ = true;
    bool bound_ = false;

    // Screen dimensions
    uint32_t screenWidth_ = 1920;
    uint32_t screenHeight_ = 1080;

    // Configuration
    EdgeSwipeConfig edgeSwipeConfig_;
    TapZoneConfig tapZoneConfig_;
    uint32_t longPressDuration_ = 500;

    // Action mappings
    std::unordered_map<EdgeSwipeDirection, std::string> edgeSwipeActions_;
    std::unordered_map<TapZone, std::string> tapZoneActions_;
    std::unordered_map<uint32_t, std::string> multiFingerTapActions_;

    // Callbacks
    EdgeSwipeCallback edgeSwipeCallback_;
    TapZoneCallback tapZoneCallback_;
    LongPressCallback longPressCallback_;
    MultiFingerTapCallback multiFingerTapCallback_;

    // Touch tracking state
    struct TrackedTouch {
        int32_t id;
        double startX, startY;
        double currentX, currentY;
        std::chrono::steady_clock::time_point startTime;
        bool moved;
    };
    std::unordered_map<int32_t, TrackedTouch> trackedTouches_;

    // Detection helpers
    [[nodiscard]] bool isEdgeTouch(double x, double y, EdgeSwipeDirection& outDirection) const;
    [[nodiscard]] TapZone determineTapZone(double x, double y) const;
    [[nodiscard]] bool isLongPress(const TrackedTouch& touch) const;
    void checkEdgeSwipe(const TrackedTouch& touch);
    void checkTapZone(const TrackedTouch& touch);
    void checkMultiFingerTap();
};

} // namespace eternal

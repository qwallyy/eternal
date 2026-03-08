#pragma once

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <string>

namespace eternal {

class GestureRecognizer;

enum class TrackpadAction {
    None,
    WorkspaceSwitch,
    Overview,
    PinchZoom,
    PinchOverview,
    HoldDrag,
    Custom
};

struct TrackpadGestureBinding {
    uint32_t fingers;
    TrackpadAction action;
    std::string customCommand;  // Only used when action == Custom
};

class TrackpadGestures {
public:
    explicit TrackpadGestures(GestureRecognizer* recognizer);
    ~TrackpadGestures();

    TrackpadGestures(const TrackpadGestures&) = delete;
    TrackpadGestures& operator=(const TrackpadGestures&) = delete;

    // Configure per-finger-count swipe actions
    void setSwipeAction(uint32_t fingers, TrackpadAction action);
    void setSwipeAction(uint32_t fingers, const std::string& customCommand);

    // Configure pinch actions
    void setPinchAction(uint32_t fingers, TrackpadAction action);

    // Configure hold actions
    void setHoldAction(uint32_t fingers, TrackpadAction action);

    // Query current bindings
    [[nodiscard]] TrackpadAction getSwipeAction(uint32_t fingers) const;
    [[nodiscard]] TrackpadAction getPinchAction(uint32_t fingers) const;
    [[nodiscard]] TrackpadAction getHoldAction(uint32_t fingers) const;

    // Action dispatchers - external code can respond to recognized actions
    using WorkspaceSwitchCallback = std::function<void(int direction)>;
    using OverviewCallback = std::function<void(bool enter)>;
    using ZoomCallback = std::function<void(double scale)>;
    using DragCallback = std::function<void(double dx, double dy)>;
    using CustomCallback = std::function<void(const std::string& command)>;

    void setWorkspaceSwitchCallback(WorkspaceSwitchCallback callback);
    void setOverviewCallback(OverviewCallback callback);
    void setZoomCallback(ZoomCallback callback);
    void setDragCallback(DragCallback callback);
    void setCustomCallback(CustomCallback callback);

    // Enable/disable
    void setEnabled(bool enabled) { enabled_ = enabled; }
    [[nodiscard]] bool isEnabled() const { return enabled_; }

    // Bind to recognizer
    void bind();
    void unbind();

private:
    GestureRecognizer* recognizer_ = nullptr;
    bool enabled_ = true;
    bool bound_ = false;

    // Per-finger-count action mappings
    std::unordered_map<uint32_t, TrackpadAction> swipeActions_ = {
        {3, TrackpadAction::WorkspaceSwitch},
        {4, TrackpadAction::Overview}
    };
    std::unordered_map<uint32_t, TrackpadAction> pinchActions_ = {
        {2, TrackpadAction::PinchZoom},
        {4, TrackpadAction::PinchOverview}
    };
    std::unordered_map<uint32_t, TrackpadAction> holdActions_ = {
        {3, TrackpadAction::HoldDrag}
    };

    // Custom command mappings
    std::unordered_map<uint32_t, std::string> swipeCustomCommands_;

    // Callbacks
    WorkspaceSwitchCallback workspaceSwitchCallback_;
    OverviewCallback overviewCallback_;
    ZoomCallback zoomCallback_;
    DragCallback dragCallback_;
    CustomCallback customCallback_;

    // Internal gesture dispatch
    void handleSwipeBegin(uint32_t fingers);
    void handleSwipeUpdate(double dx, double dy, uint32_t fingers);
    void handleSwipeEnd(uint32_t fingers, double dx, double dy);
    void handlePinchBegin(uint32_t fingers);
    void handlePinchUpdate(double scale, double rotation, uint32_t fingers);
    void handlePinchEnd(uint32_t fingers, double scale);
    void handleHoldBegin(uint32_t fingers);
    void handleHoldEnd(uint32_t fingers);

    void dispatchAction(TrackpadAction action, uint32_t fingers, double dx, double dy, double scale);
};

} // namespace eternal

#pragma once

extern "C" {
#include <wayland-server-core.h>
#include <wlr/types/wlr_tablet_tool.h>
#include <wlr/types/wlr_tablet_pad.h>
#include <wlr/types/wlr_tablet_v2.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_seat.h>
}

#include <cstdint>
#include <functional>
#include <unordered_map>

namespace eternal {

class InputManager;

enum class ProximityState {
    In,
    Out
};

enum class TipState {
    Down,
    Up
};

struct TabletAxisEvent {
    double x;
    double y;
    double pressure;
    double tiltX;
    double tiltY;
    double distance;
    double rotation;
    double slider;
};

struct AreaMapping {
    double x;
    double y;
    double width;
    double height;
};

/// Tracks per-tool state for the tablet v2 protocol
struct TabletToolState {
    wlr_tablet_v2_tablet_tool* v2Tool = nullptr;
    wlr_surface* currentSurface = nullptr;
    bool tipDown = false;
    double lastX = 0.0;
    double lastY = 0.0;
};

class Tablet {
public:
    explicit Tablet(InputManager* manager);
    ~Tablet();

    Tablet(const Tablet&) = delete;
    Tablet& operator=(const Tablet&) = delete;

    // Core event handling
    void handleAxis(double x, double y, double pressure, double tiltX, double tiltY);
    void handleProximity(ProximityState state, double x, double y,
                         wlr_tablet_tool* tool);
    void handleTip(TipState state, double x, double y, wlr_tablet_tool* tool);
    void handleButton(uint32_t button, wlr_button_state state, wlr_tablet_tool* tool);
    void attachDevice(wlr_tablet* tablet);

    // wp_tablet_v2 protocol setup (Task 27)
    void initTabletV2(wl_display* display);

    // Mapping configuration
    void setOutputMapping(wlr_output* output);
    void setAreaMapping(const AreaMapping& area);
    void setRotation(double degrees);

    [[nodiscard]] wlr_output* getOutputMapping() const { return outputMapping_; }
    [[nodiscard]] const AreaMapping& getAreaMapping() const { return areaMapping_; }
    [[nodiscard]] double getRotation() const { return rotation_; }

    // Event callbacks
    using AxisCallback = std::function<void(const TabletAxisEvent& event)>;
    using ProximityCallback = std::function<void(ProximityState state)>;
    using TipCallback = std::function<void(TipState state)>;
    using ButtonCallback = std::function<void(uint32_t button, wlr_button_state state)>;

    void setAxisCallback(AxisCallback callback);
    void setProximityCallback(ProximityCallback callback);
    void setTipCallback(TipCallback callback);
    void setButtonCallback(ButtonCallback callback);

private:
    InputManager* manager_ = nullptr;
    wlr_tablet* wlrTablet_ = nullptr;

    // wp_tablet_v2 protocol objects
    wlr_tablet_manager_v2* tabletManagerV2_ = nullptr;
    wlr_tablet_v2_tablet* tabletV2_ = nullptr;

    // Per-tool v2 state tracking
    std::unordered_map<wlr_tablet_tool*, TabletToolState> toolStates_;

    // Mapping
    wlr_output* outputMapping_ = nullptr;
    AreaMapping areaMapping_ = {0.0, 0.0, 1.0, 1.0};
    double rotation_ = 0.0;

    // Current tool state
    bool inProximity_ = false;
    bool tipDown_ = false;

    // Callbacks
    AxisCallback axisCallback_;
    ProximityCallback proximityCallback_;
    TipCallback tipCallback_;
    ButtonCallback buttonCallback_;

    // Wayland listeners
    wl_listener axisListener_ = {};
    wl_listener proximityListener_ = {};
    wl_listener tipListener_ = {};
    wl_listener buttonListener_ = {};
    wl_listener destroyListener_ = {};

    static void handleAxisEvent(wl_listener* listener, void* data);
    static void handleProximityEvent(wl_listener* listener, void* data);
    static void handleTipEvent(wl_listener* listener, void* data);
    static void handleButtonEvent(wl_listener* listener, void* data);
    static void handleDestroy(wl_listener* listener, void* data);

    // Transform coordinates based on mapping/rotation
    void transformCoordinates(double& x, double& y) const;

    // Map tablet coords to output layout coords
    void mapToOutput(double& x, double& y) const;

    // Get or create v2 tool state for a wlr_tablet_tool
    TabletToolState& getOrCreateToolState(wlr_tablet_tool* tool);

    // Forward axis event through tablet v2 protocol
    void forwardAxisV2(double x, double y, double pressure,
                       double tiltX, double tiltY, wlr_tablet_tool* tool);
};

} // namespace eternal

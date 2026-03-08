#pragma once

extern "C" {
#include <wayland-server-core.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_cursor.h>

struct wlr_scene_node;
}

#include <cstdint>
#include <functional>

namespace eternal {

class InputManager;

enum class AccelProfile;
enum class ScrollMethod;

/// Interactive operation in progress (super+click move/resize)
enum class InteractiveMode {
    None,
    Move,
    Resize,
};

class Pointer {
public:
    explicit Pointer(InputManager* manager);
    ~Pointer();

    Pointer(const Pointer&) = delete;
    Pointer& operator=(const Pointer&) = delete;

    // Core event handling
    void handleMotion(double dx, double dy, uint32_t timeMsec);
    void handleMotionAbsolute(double x, double y, uint32_t timeMsec);
    void handleButton(uint32_t button, wlr_button_state state, uint32_t timeMsec);
    void handleAxis(enum wl_pointer_axis orientation, double delta,
                    int32_t deltaDiscrete, enum wl_pointer_axis_source source,
                    enum wl_pointer_axis_relative_direction relativeDirection, uint32_t timeMsec);
    void handleFrame();
    void attachDevice(wlr_pointer* pointer);

    // Cursor manipulation
    void warpTo(double x, double y);
    void confineToOutput(wlr_output* output);
    struct Position { double x; double y; };
    [[nodiscard]] Position getPosition() const;

    // Surface-under-cursor lookup via scene tree
    struct SurfaceAtResult {
        wlr_surface* surface = nullptr;
        double sx = 0.0;
        double sy = 0.0;
        wlr_scene_node* node = nullptr;
    };
    [[nodiscard]] SurfaceAtResult surfaceAtCursor() const;

    // Focus management
    void updateFocus(uint32_t timeMsec);
    void setFocusedSurface(wlr_surface* surface, double sx, double sy);

    // Interactive move/resize (Task 24: super+click)
    void beginInteractiveMove();
    void beginInteractiveResize(uint32_t edges);
    void endInteractive();
    [[nodiscard]] InteractiveMode getInteractiveMode() const { return interactiveMode_; }

    // Double-click detection (Task 24)
    [[nodiscard]] bool isDoubleClick(uint32_t button, uint32_t timeMsec) const;

    // Configuration
    void setSensitivity(double sensitivity);
    void setAccelProfile(AccelProfile profile);
    void setNaturalScroll(bool enabled);
    void setScrollFactor(double factor);
    void setScrollMethod(ScrollMethod method);
    void setLeftHanded(bool enabled);
    void setMiddleEmulation(bool enabled);

    // Configuration getters
    [[nodiscard]] double getSensitivity() const { return sensitivity_; }
    [[nodiscard]] AccelProfile getAccelProfile() const { return accelProfile_; }
    [[nodiscard]] bool getNaturalScroll() const { return naturalScroll_; }
    [[nodiscard]] double getScrollFactor() const { return scrollFactor_; }
    [[nodiscard]] ScrollMethod getScrollMethod() const { return scrollMethod_; }
    [[nodiscard]] bool getLeftHanded() const { return leftHanded_; }
    [[nodiscard]] bool getMiddleEmulation() const { return middleEmulation_; }

    // Event callbacks
    using MotionCallback = std::function<void(double dx, double dy)>;
    using ButtonCallback = std::function<void(uint32_t button, wlr_button_state state)>;
    using AxisCallback = std::function<void(enum wl_pointer_axis orientation, double delta)>;

    void setMotionCallback(MotionCallback callback);
    void setButtonCallback(ButtonCallback callback);
    void setAxisCallback(AxisCallback callback);

private:
    InputManager* manager_ = nullptr;
    wlr_pointer* wlrPointer_ = nullptr;

    // Cursor position
    double cursorX_ = 0.0;
    double cursorY_ = 0.0;

    // Confinement
    wlr_output* confinedOutput_ = nullptr;

    // Currently focused surface
    wlr_surface* focusedSurface_ = nullptr;

    // Interactive move/resize state
    InteractiveMode interactiveMode_ = InteractiveMode::None;
    double grabX_ = 0.0;
    double grabY_ = 0.0;
    uint32_t resizeEdges_ = 0;

    // Double-click detection
    mutable uint32_t lastClickButton_ = 0;
    mutable uint32_t lastClickTime_ = 0;
    mutable double lastClickX_ = 0.0;
    mutable double lastClickY_ = 0.0;
    static constexpr uint32_t kDoubleClickTimeMs = 400;
    static constexpr double kDoubleClickDistancePx = 5.0;

    // Configuration
    double sensitivity_ = 1.0;
    AccelProfile accelProfile_;
    bool naturalScroll_ = false;
    double scrollFactor_ = 1.0;
    ScrollMethod scrollMethod_;
    bool leftHanded_ = false;
    bool middleEmulation_ = false;

    // Callbacks
    MotionCallback motionCallback_;
    ButtonCallback buttonCallback_;
    AxisCallback axisCallback_;

    // Wayland listeners
    wl_listener motionListener_ = {};
    wl_listener motionAbsoluteListener_ = {};
    wl_listener buttonListener_ = {};
    wl_listener axisListener_ = {};
    wl_listener frameListener_ = {};
    wl_listener destroyListener_ = {};

    static void handleMotionEvent(wl_listener* listener, void* data);
    static void handleMotionAbsoluteEvent(wl_listener* listener, void* data);
    static void handleButtonEvent(wl_listener* listener, void* data);
    static void handleAxisEvent(wl_listener* listener, void* data);
    static void handleFrameEvent(wl_listener* listener, void* data);
    static void handleDestroy(wl_listener* listener, void* data);

    void clampCursorToOutput();
    void processMotion(uint32_t timeMsec);
};

} // namespace eternal

#pragma once

extern "C" {
#include <wayland-server-core.h>
#include <wlr/types/wlr_touch.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_output_layout.h>
}

#include <cstdint>
#include <functional>
#include <unordered_map>

namespace eternal {

class InputManager;

struct TouchPoint {
    int32_t id;
    double x;
    double y;
    bool active;
    wlr_surface* focusedSurface = nullptr;  // Surface this point is focused on
};

class Touch {
public:
    explicit Touch(InputManager* manager);
    ~Touch();

    Touch(const Touch&) = delete;
    Touch& operator=(const Touch&) = delete;

    // Core event handling
    void handleDown(int32_t id, double x, double y, uint32_t timeMsec);
    void handleUp(int32_t id, uint32_t timeMsec);
    void handleMotion(int32_t id, double x, double y, uint32_t timeMsec);
    void handleCancel();
    void handleFrame();
    void attachDevice(wlr_touch* touch);

    // Touch-to-pointer emulation for single touch (Task 28)
    void setEmulatePointer(bool enabled) { emulatePointer_ = enabled; }
    [[nodiscard]] bool getEmulatePointer() const { return emulatePointer_; }

    // Touch point queries
    [[nodiscard]] const TouchPoint* getPoint(int32_t id) const;
    [[nodiscard]] size_t getActivePointCount() const;
    [[nodiscard]] const std::unordered_map<int32_t, TouchPoint>& getActivePoints() const;

    // Event callbacks
    using DownCallback = std::function<void(int32_t id, double x, double y)>;
    using UpCallback = std::function<void(int32_t id)>;
    using MotionCallback = std::function<void(int32_t id, double x, double y)>;
    using CancelCallback = std::function<void()>;
    using FrameCallback = std::function<void()>;

    void setDownCallback(DownCallback callback);
    void setUpCallback(UpCallback callback);
    void setMotionCallback(MotionCallback callback);
    void setCancelCallback(CancelCallback callback);
    void setFrameCallback(FrameCallback callback);

private:
    InputManager* manager_ = nullptr;
    wlr_touch* wlrTouch_ = nullptr;

    // Active touch points tracked by ID
    std::unordered_map<int32_t, TouchPoint> touchPoints_;

    // Touch-to-pointer emulation
    bool emulatePointer_ = true;
    int32_t emulationPointId_ = -1;     // The touch ID being used for pointer emulation
    bool emulationButtonDown_ = false;

    // Callbacks
    DownCallback downCallback_;
    UpCallback upCallback_;
    MotionCallback motionCallback_;
    CancelCallback cancelCallback_;
    FrameCallback frameCallback_;

    // Wayland listeners
    wl_listener downListener_ = {};
    wl_listener upListener_ = {};
    wl_listener motionListener_ = {};
    wl_listener cancelListener_ = {};
    wl_listener frameListener_ = {};
    wl_listener destroyListener_ = {};

    static void handleDownEvent(wl_listener* listener, void* data);
    static void handleUpEvent(wl_listener* listener, void* data);
    static void handleMotionEvent(wl_listener* listener, void* data);
    static void handleCancelEvent(wl_listener* listener, void* data);
    static void handleFrameEvent(wl_listener* listener, void* data);
    static void handleDestroy(wl_listener* listener, void* data);

    // Look up the surface at a given touch position
    wlr_surface* surfaceAtTouch(double x, double y, double& sx, double& sy) const;

    // Pointer emulation helpers
    void emulatePointerDown(double x, double y, uint32_t timeMsec);
    void emulatePointerMotion(double x, double y, uint32_t timeMsec);
    void emulatePointerUp(uint32_t timeMsec);
};

} // namespace eternal

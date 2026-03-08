#pragma once

extern "C" {
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_touch.h>
#include <wlr/types/wlr_tablet_tool.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <libinput.h>

struct wlr_scene;
}

#include <memory>
#include <vector>
#include <functional>
#include <string>

namespace eternal {

class Keyboard;
class Pointer;
class Touch;
class Tablet;
class KeybindManager;
class GestureRecognizer;

enum class AccelProfile {
    Flat,
    Adaptive,
    Custom
};

enum class ScrollMethod {
    NoScroll,
    TwoFinger,
    Edge,
    OnButtonDown
};

/// Per-device configuration loaded from config file
struct InputDeviceConfig {
    std::string name;                       // Device name pattern (or "*" for all)
    double sensitivity = 0.0;               // Pointer speed [-1.0, 1.0]
    AccelProfile accelProfile = AccelProfile::Adaptive;
    bool naturalScroll = false;
    bool tapToClick = true;
    bool disableWhileTyping = true;
    bool leftHanded = false;
    bool middleEmulation = false;
    ScrollMethod scrollMethod = ScrollMethod::TwoFinger;
    double scrollFactor = 1.0;
    int32_t repeatRate = 25;
    int32_t repeatDelay = 600;
    std::string kbLayout = "us";
    std::string kbVariant;
    std::string kbModel;
    std::string kbOptions;
};

class InputManager {
public:
    InputManager(wlr_seat* seat, wlr_backend* backend, wlr_session* session,
                 wlr_output_layout* outputLayout, wl_display* display);
    ~InputManager();

    InputManager(const InputManager&) = delete;
    InputManager& operator=(const InputManager&) = delete;

    // Device hotplug
    void addDevice(wlr_input_device* device);
    void removeDevice(wlr_input_device* device);

    // Device accessors
    [[nodiscard]] Keyboard* getKeyboard() const;
    [[nodiscard]] Pointer* getPointer() const;
    [[nodiscard]] Touch* getTouch() const;
    [[nodiscard]] Tablet* getTablet() const;
    [[nodiscard]] KeybindManager* getKeybindManager() const;
    [[nodiscard]] GestureRecognizer* getGestureRecognizer() const;

    // Input event processing
    void processKeyPress(uint32_t key, uint32_t mods, wl_keyboard_key_state state);
    void processPointerMotion(double dx, double dy);
    void processPointerButton(uint32_t button, wlr_button_state state);
    void processAxis(enum wl_pointer_axis_source source, enum wl_pointer_axis orientation, double delta);

    // Configuration
    void setRepeatRate(int32_t rate, int32_t delay);
    void setNaturalScroll(bool enabled);
    void setTapToClick(bool enabled);
    void setAccelProfile(AccelProfile profile);
    void setScrollMethod(ScrollMethod method);

    /// Apply per-device config. Call after loading config.
    void addDeviceConfig(const InputDeviceConfig& config);
    void applyDeviceConfigs();

    // Listener setup
    void setupBackendListeners();

    // Seat access
    [[nodiscard]] wlr_seat* getSeat() const { return seat_; }
    [[nodiscard]] wlr_session* getSession() const { return session_; }
    [[nodiscard]] wlr_cursor* getCursor() const { return cursor_; }
    [[nodiscard]] wlr_output_layout* getOutputLayout() const { return outputLayout_; }
    [[nodiscard]] wl_display* getDisplay() const { return display_; }
    [[nodiscard]] wlr_scene* getScene() const { return scene_; }

    void setScene(wlr_scene* scene) { scene_ = scene; }

    // Callbacks for key/button bindings
    using KeyCallback = std::function<void(uint32_t key, uint32_t mods)>;
    void setKeyBindingCallback(KeyCallback callback);

    // libinput log handler
    static void libinputLogHandler(libinput* li, libinput_log_priority priority,
                                   const char* fmt, va_list args);

private:
    wlr_seat* seat_ = nullptr;
    wlr_backend* backend_ = nullptr;
    wlr_session* session_ = nullptr;
    wlr_output_layout* outputLayout_ = nullptr;
    wl_display* display_ = nullptr;
    wlr_scene* scene_ = nullptr;

    // Cursor manager
    wlr_cursor* cursor_ = nullptr;
    wlr_xcursor_manager* xcursorManager_ = nullptr;

    std::unique_ptr<Keyboard> keyboard_;
    std::unique_ptr<Pointer> pointer_;
    std::unique_ptr<Touch> touch_;
    std::unique_ptr<Tablet> tablet_;
    std::unique_ptr<KeybindManager> keybindManager_;
    std::unique_ptr<GestureRecognizer> gestureRecognizer_;

    struct TrackedDevice {
        wlr_input_device* device;
        InputManager* manager;
        wl_listener destroyListener;
    };
    std::vector<std::unique_ptr<TrackedDevice>> devices_;
    std::vector<InputDeviceConfig> deviceConfigs_;

    KeyCallback keyBindingCallback_;

    // Wayland listeners
    wl_listener newInputListener_ = {};
    wl_listener cursorMotionListener_ = {};
    wl_listener cursorMotionAbsoluteListener_ = {};
    wl_listener cursorButtonListener_ = {};
    wl_listener cursorAxisListener_ = {};
    wl_listener cursorFrameListener_ = {};

    static void handleNewInput(wl_listener* listener, void* data);
    static void handleCursorMotion(wl_listener* listener, void* data);
    static void handleCursorMotionAbsolute(wl_listener* listener, void* data);
    static void handleCursorButton(wl_listener* listener, void* data);
    static void handleCursorAxis(wl_listener* listener, void* data);
    static void handleCursorFrame(wl_listener* listener, void* data);
    static void handleDeviceDestroy(wl_listener* listener, void* data);

    void configureLibinputDevice(wlr_input_device* device);
    void setupKeyboard(wlr_input_device* device);
    void setupPointer(wlr_input_device* device);
    void setupTouch(wlr_input_device* device);
    void setupTablet(wlr_input_device* device);

    /// Find the best matching device config for a given device
    const InputDeviceConfig* findDeviceConfig(const std::string& deviceName) const;

    /// Apply a single config to a libinput device handle
    void applyLibinputConfig(libinput_device* dev, const InputDeviceConfig& config);

    /// Initialize the wlr_cursor and xcursor theme
    void initCursor();
};

} // namespace eternal

#include "eternal/input/InputManager.hpp"
#include "eternal/input/Keyboard.hpp"
#include "eternal/input/Pointer.hpp"
#include "eternal/input/Touch.hpp"
#include "eternal/input/Tablet.hpp"
#include "eternal/input/KeybindManager.hpp"
#include "eternal/gestures/GestureRecognizer.hpp"
#include "eternal/utils/Logger.hpp"

extern "C" {
#include <wlr/backend/libinput.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_xcursor_manager.h>
}

#include <algorithm>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fnmatch.h>

namespace eternal {

// ---------------------------------------------------------------------------
// Task 21: libinput log handler
// ---------------------------------------------------------------------------

void InputManager::libinputLogHandler(libinput* /*li*/,
                                      libinput_log_priority priority,
                                      const char* fmt, va_list args) {
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, args);

    // Strip trailing newline
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') {
        buf[len - 1] = '\0';
    }

    switch (priority) {
    case LIBINPUT_LOG_PRIORITY_DEBUG:
        LOG_DEBUG("[libinput] {}", buf);
        break;
    case LIBINPUT_LOG_PRIORITY_INFO:
        LOG_INFO("[libinput] {}", buf);
        break;
    case LIBINPUT_LOG_PRIORITY_ERROR:
        LOG_ERROR("[libinput] {}", buf);
        break;
    default:
        LOG_DEBUG("[libinput] {}", buf);
        break;
    }
}

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

InputManager::InputManager(wlr_seat* seat, wlr_backend* backend,
                           wlr_output_layout* outputLayout, wl_display* display)
    : seat_(seat)
    , backend_(backend)
    , outputLayout_(outputLayout)
    , display_(display)
    , keyboard_(std::make_unique<Keyboard>(this))
    , pointer_(std::make_unique<Pointer>(this))
    , touch_(std::make_unique<Touch>(this))
    , tablet_(std::make_unique<Tablet>(this))
    , keybindManager_(std::make_unique<KeybindManager>(this))
    , gestureRecognizer_(std::make_unique<GestureRecognizer>())
{
    assert(seat_);
    assert(backend_);
    assert(outputLayout_);
    assert(display_);

    initCursor();
}

InputManager::~InputManager() {
    wl_list_remove(&newInputListener_.link);

    if (cursor_) {
        wl_list_remove(&cursorMotionListener_.link);
        wl_list_remove(&cursorMotionAbsoluteListener_.link);
        wl_list_remove(&cursorButtonListener_.link);
        wl_list_remove(&cursorAxisListener_.link);
        wl_list_remove(&cursorFrameListener_.link);
        wlr_cursor_destroy(cursor_);
    }
    if (xcursorManager_) {
        wlr_xcursor_manager_destroy(xcursorManager_);
    }
}

// ---------------------------------------------------------------------------
// Task 21: Initialize wlr_cursor (the compositor-side cursor)
// ---------------------------------------------------------------------------

void InputManager::initCursor() {
    cursor_ = wlr_cursor_create();
    wlr_cursor_attach_output_layout(cursor_, outputLayout_);

    xcursorManager_ = wlr_xcursor_manager_create(nullptr, 24);
    wlr_xcursor_manager_load(xcursorManager_, 1);

    // Wire cursor events
    cursorMotionListener_.notify = handleCursorMotion;
    wl_signal_add(&cursor_->events.motion, &cursorMotionListener_);

    cursorMotionAbsoluteListener_.notify = handleCursorMotionAbsolute;
    wl_signal_add(&cursor_->events.motion_absolute, &cursorMotionAbsoluteListener_);

    cursorButtonListener_.notify = handleCursorButton;
    wl_signal_add(&cursor_->events.button, &cursorButtonListener_);

    cursorAxisListener_.notify = handleCursorAxis;
    wl_signal_add(&cursor_->events.axis, &cursorAxisListener_);

    cursorFrameListener_.notify = handleCursorFrame;
    wl_signal_add(&cursor_->events.frame, &cursorFrameListener_);

    // Set default cursor image
    wlr_cursor_set_xcursor(cursor_, xcursorManager_, "default");
}

// ---------------------------------------------------------------------------
// Backend listener setup
// ---------------------------------------------------------------------------

void InputManager::setupBackendListeners() {
    newInputListener_.notify = handleNewInput;
    wl_signal_add(&backend_->events.new_input, &newInputListener_);
}

void InputManager::handleNewInput(wl_listener* listener, void* data) {
    InputManager* self = wl_container_of(listener, self, newInputListener_);
    auto* device = static_cast<wlr_input_device*>(data);
    self->addDevice(device);
}

// ---------------------------------------------------------------------------
// Cursor event handlers (Task 21: events routed through wlr_cursor)
// ---------------------------------------------------------------------------

void InputManager::handleCursorMotion(wl_listener* listener, void* data) {
    InputManager* self = wl_container_of(listener, self, cursorMotionListener_);
    auto* event = static_cast<wlr_pointer_motion_event*>(data);

    wlr_cursor_move(self->cursor_, &event->pointer->base,
                    event->delta_x, event->delta_y);

    self->pointer_->handleMotion(event->delta_x, event->delta_y, event->time_msec);
}

void InputManager::handleCursorMotionAbsolute(wl_listener* listener, void* data) {
    InputManager* self = wl_container_of(listener, self, cursorMotionAbsoluteListener_);
    auto* event = static_cast<wlr_pointer_motion_absolute_event*>(data);

    wlr_cursor_warp_absolute(self->cursor_, &event->pointer->base, event->x, event->y);

    self->pointer_->handleMotionAbsolute(self->cursor_->x, self->cursor_->y,
                                         event->time_msec);
}

void InputManager::handleCursorButton(wl_listener* listener, void* data) {
    InputManager* self = wl_container_of(listener, self, cursorButtonListener_);
    auto* event = static_cast<wlr_pointer_button_event*>(data);

    // Let keybind manager try to consume mouse buttons first
    uint32_t mods = self->keyboard_->getModifiers();
    bool pressed = (event->state == WL_POINTER_BUTTON_STATE_PRESSED);

    if (self->keybindManager_->onMouseButton(event->button, mods, pressed)) {
        return;  // Consumed by binding
    }

    self->pointer_->handleButton(event->button,
                                 static_cast<wlr_button_state>(event->state),
                                 event->time_msec);
}

void InputManager::handleCursorAxis(wl_listener* listener, void* data) {
    InputManager* self = wl_container_of(listener, self, cursorAxisListener_);
    auto* event = static_cast<wlr_pointer_axis_event*>(data);

    self->pointer_->handleAxis(event->orientation, event->delta,
                               event->delta_discrete, event->source,
                               event->relative_direction, event->time_msec);
}

void InputManager::handleCursorFrame(wl_listener* listener, void* /*data*/) {
    InputManager* self = wl_container_of(listener, self, cursorFrameListener_);
    self->pointer_->handleFrame();
}

// ---------------------------------------------------------------------------
// Device hotplug
// ---------------------------------------------------------------------------

void InputManager::addDevice(wlr_input_device* device) {
    devices_.push_back(device);
    LOG_INFO("Input device added: {} (type {})", device->name,
             static_cast<int>(device->type));

    // Task 21: configure libinput properties from per-device config
    configureLibinputDevice(device);

    switch (device->type) {
    case WLR_INPUT_DEVICE_KEYBOARD:
        setupKeyboard(device);
        break;
    case WLR_INPUT_DEVICE_POINTER:
        setupPointer(device);
        break;
    case WLR_INPUT_DEVICE_TOUCH:
        setupTouch(device);
        break;
    case WLR_INPUT_DEVICE_TABLET:
        setupTablet(device);
        break;
    default:
        LOG_DEBUG("Unhandled input device type: {}", static_cast<int>(device->type));
        break;
    }

    // Update seat capabilities based on attached devices
    uint32_t caps = 0;
    for (auto* dev : devices_) {
        switch (dev->type) {
        case WLR_INPUT_DEVICE_KEYBOARD:
            caps |= WL_SEAT_CAPABILITY_KEYBOARD;
            break;
        case WLR_INPUT_DEVICE_POINTER:
            caps |= WL_SEAT_CAPABILITY_POINTER;
            break;
        case WLR_INPUT_DEVICE_TOUCH:
            caps |= WL_SEAT_CAPABILITY_TOUCH;
            break;
        default:
            break;
        }
    }
    wlr_seat_set_capabilities(seat_, caps);
}

void InputManager::removeDevice(wlr_input_device* device) {
    LOG_INFO("Input device removed: {}", device->name);

    auto it = std::find(devices_.begin(), devices_.end(), device);
    if (it != devices_.end()) {
        devices_.erase(it);
    }

    // Re-evaluate seat capabilities
    uint32_t caps = 0;
    for (auto* dev : devices_) {
        switch (dev->type) {
        case WLR_INPUT_DEVICE_KEYBOARD:
            caps |= WL_SEAT_CAPABILITY_KEYBOARD;
            break;
        case WLR_INPUT_DEVICE_POINTER:
            caps |= WL_SEAT_CAPABILITY_POINTER;
            break;
        case WLR_INPUT_DEVICE_TOUCH:
            caps |= WL_SEAT_CAPABILITY_TOUCH;
            break;
        default:
            break;
        }
    }
    wlr_seat_set_capabilities(seat_, caps);
}

// ---------------------------------------------------------------------------
// Device accessors
// ---------------------------------------------------------------------------

Keyboard* InputManager::getKeyboard() const { return keyboard_.get(); }
Pointer* InputManager::getPointer() const { return pointer_.get(); }
Touch* InputManager::getTouch() const { return touch_.get(); }
Tablet* InputManager::getTablet() const { return tablet_.get(); }
KeybindManager* InputManager::getKeybindManager() const { return keybindManager_.get(); }
GestureRecognizer* InputManager::getGestureRecognizer() const { return gestureRecognizer_.get(); }

// ---------------------------------------------------------------------------
// Input event processing (high-level API)
// ---------------------------------------------------------------------------

void InputManager::processKeyPress(uint32_t key, uint32_t mods, wl_keyboard_key_state state) {
    if (keyBindingCallback_ && state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        keyBindingCallback_(key, mods);
    }
    keyboard_->handleKey(key, state);
}

void InputManager::processPointerMotion(double dx, double dy) {
    pointer_->handleMotion(dx, dy, 0);
}

void InputManager::processPointerButton(uint32_t button, wlr_button_state state) {
    pointer_->handleButton(button, state, 0);
}

void InputManager::processAxis(enum wl_pointer_axis_source source,
                                enum wl_pointer_axis orientation,
                                double delta) {
    pointer_->handleAxis(orientation, delta, 0, source,
                         WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL, 0);
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void InputManager::setRepeatRate(int32_t rate, int32_t delay) {
    keyboard_->setRepeatRate(rate);
    keyboard_->setRepeatDelay(delay);
}

void InputManager::setNaturalScroll(bool enabled) {
    pointer_->setNaturalScroll(enabled);
}

void InputManager::setTapToClick(bool enabled) {
    for (auto* device : devices_) {
        if (!wlr_input_device_is_libinput(device)) continue;
        auto* libinputDevice = wlr_libinput_get_device_handle(device);
        if (libinput_device_config_tap_get_finger_count(libinputDevice) > 0) {
            libinput_device_config_tap_set_enabled(
                libinputDevice,
                enabled ? LIBINPUT_CONFIG_TAP_ENABLED : LIBINPUT_CONFIG_TAP_DISABLED);
        }
    }
}

void InputManager::setAccelProfile(AccelProfile profile) {
    pointer_->setAccelProfile(profile);

    for (auto* device : devices_) {
        if (!wlr_input_device_is_libinput(device)) continue;
        auto* libinputDevice = wlr_libinput_get_device_handle(device);
        enum libinput_config_accel_profile lp = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
        switch (profile) {
        case AccelProfile::Flat:
            lp = LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT;
            break;
        case AccelProfile::Adaptive:
            lp = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
            break;
        case AccelProfile::Custom:
            lp = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
            break;
        }
        libinput_device_config_accel_set_profile(libinputDevice, lp);
    }
}

void InputManager::setScrollMethod(ScrollMethod method) {
    pointer_->setScrollMethod(method);

    for (auto* device : devices_) {
        if (!wlr_input_device_is_libinput(device)) continue;
        auto* libinputDevice = wlr_libinput_get_device_handle(device);
        enum libinput_config_scroll_method lm = LIBINPUT_CONFIG_SCROLL_NO_SCROLL;
        switch (method) {
        case ScrollMethod::NoScroll:
            lm = LIBINPUT_CONFIG_SCROLL_NO_SCROLL;
            break;
        case ScrollMethod::TwoFinger:
            lm = LIBINPUT_CONFIG_SCROLL_2FG;
            break;
        case ScrollMethod::Edge:
            lm = LIBINPUT_CONFIG_SCROLL_EDGE;
            break;
        case ScrollMethod::OnButtonDown:
            lm = LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN;
            break;
        }
        libinput_device_config_scroll_set_method(libinputDevice, lm);
    }
}

void InputManager::setKeyBindingCallback(KeyCallback callback) {
    keyBindingCallback_ = std::move(callback);
}

// ---------------------------------------------------------------------------
// Task 21: Per-device configuration
// ---------------------------------------------------------------------------

void InputManager::addDeviceConfig(const InputDeviceConfig& config) {
    deviceConfigs_.push_back(config);
}

void InputManager::applyDeviceConfigs() {
    for (auto* device : devices_) {
        configureLibinputDevice(device);
    }
}

const InputDeviceConfig* InputManager::findDeviceConfig(const std::string& deviceName) const {
    // First try exact match, then glob match
    for (const auto& config : deviceConfigs_) {
        if (config.name == deviceName) {
            return &config;
        }
    }
    for (const auto& config : deviceConfigs_) {
        if (fnmatch(config.name.c_str(), deviceName.c_str(), 0) == 0) {
            return &config;
        }
    }
    return nullptr;
}

void InputManager::applyLibinputConfig(libinput_device* dev, const InputDeviceConfig& config) {
    // Sensitivity / acceleration speed
    libinput_device_config_accel_set_speed(dev, config.sensitivity);

    // Acceleration profile
    {
        uint32_t supported = libinput_device_config_accel_get_profiles(dev);
        enum libinput_config_accel_profile lp = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
        switch (config.accelProfile) {
        case AccelProfile::Flat:
            if (supported & LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT)
                lp = LIBINPUT_CONFIG_ACCEL_PROFILE_FLAT;
            break;
        case AccelProfile::Adaptive:
            lp = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
            break;
        case AccelProfile::Custom:
            lp = LIBINPUT_CONFIG_ACCEL_PROFILE_ADAPTIVE;
            break;
        }
        libinput_device_config_accel_set_profile(dev, lp);
    }

    // Natural scroll
    if (libinput_device_config_scroll_has_natural_scroll(dev)) {
        libinput_device_config_scroll_set_natural_scroll_enabled(
            dev, config.naturalScroll ? 1 : 0);
    }

    // Tap-to-click
    if (libinput_device_config_tap_get_finger_count(dev) > 0) {
        libinput_device_config_tap_set_enabled(
            dev, config.tapToClick ? LIBINPUT_CONFIG_TAP_ENABLED
                                   : LIBINPUT_CONFIG_TAP_DISABLED);
    }

    // Disable-while-typing
    if (libinput_device_config_dwt_is_available(dev)) {
        libinput_device_config_dwt_set_enabled(
            dev, config.disableWhileTyping ? LIBINPUT_CONFIG_DWT_ENABLED
                                           : LIBINPUT_CONFIG_DWT_DISABLED);
    }

    // Left-handed
    if (libinput_device_config_left_handed_is_available(dev)) {
        libinput_device_config_left_handed_set(dev, config.leftHanded ? 1 : 0);
    }

    // Middle emulation
    if (libinput_device_config_middle_emulation_is_available(dev)) {
        libinput_device_config_middle_emulation_set_enabled(
            dev, config.middleEmulation
                     ? LIBINPUT_CONFIG_MIDDLE_EMULATION_ENABLED
                     : LIBINPUT_CONFIG_MIDDLE_EMULATION_DISABLED);
    }

    // Scroll method
    {
        uint32_t supported = libinput_device_config_scroll_get_methods(dev);
        enum libinput_config_scroll_method lm = LIBINPUT_CONFIG_SCROLL_NO_SCROLL;
        switch (config.scrollMethod) {
        case ScrollMethod::NoScroll:
            lm = LIBINPUT_CONFIG_SCROLL_NO_SCROLL;
            break;
        case ScrollMethod::TwoFinger:
            if (supported & LIBINPUT_CONFIG_SCROLL_2FG)
                lm = LIBINPUT_CONFIG_SCROLL_2FG;
            break;
        case ScrollMethod::Edge:
            if (supported & LIBINPUT_CONFIG_SCROLL_EDGE)
                lm = LIBINPUT_CONFIG_SCROLL_EDGE;
            break;
        case ScrollMethod::OnButtonDown:
            if (supported & LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN)
                lm = LIBINPUT_CONFIG_SCROLL_ON_BUTTON_DOWN;
            break;
        }
        libinput_device_config_scroll_set_method(dev, lm);
    }

    LOG_DEBUG("Applied device config for: {}", config.name);
}

// ---------------------------------------------------------------------------
// Task 21: Configure libinput device from per-device config
// ---------------------------------------------------------------------------

void InputManager::configureLibinputDevice(wlr_input_device* device) {
    if (!wlr_input_device_is_libinput(device)) return;

    auto* libinputDevice = wlr_libinput_get_device_handle(device);

    // Look for per-device config
    const InputDeviceConfig* config = findDeviceConfig(device->name);
    if (config) {
        applyLibinputConfig(libinputDevice, *config);
        return;
    }

    // Look for wildcard config
    const InputDeviceConfig* wildcardConfig = findDeviceConfig("*");
    if (wildcardConfig) {
        applyLibinputConfig(libinputDevice, *wildcardConfig);
        return;
    }

    // Default configuration
    if (libinput_device_config_tap_get_finger_count(libinputDevice) > 0) {
        libinput_device_config_tap_set_enabled(libinputDevice, LIBINPUT_CONFIG_TAP_ENABLED);
    }

    if (libinput_device_config_scroll_has_natural_scroll(libinputDevice)) {
        libinput_device_config_scroll_set_natural_scroll_enabled(
            libinputDevice, pointer_->getNaturalScroll() ? 1 : 0);
    }

    // Log device capabilities
    bool hasPointer = libinput_device_has_capability(libinputDevice, LIBINPUT_DEVICE_CAP_POINTER);
    bool hasKeyboard = libinput_device_has_capability(libinputDevice, LIBINPUT_DEVICE_CAP_KEYBOARD);
    bool hasTouch = libinput_device_has_capability(libinputDevice, LIBINPUT_DEVICE_CAP_TOUCH);
    bool hasTablet = libinput_device_has_capability(libinputDevice, LIBINPUT_DEVICE_CAP_TABLET_TOOL);
    bool hasGesture = libinput_device_has_capability(libinputDevice, LIBINPUT_DEVICE_CAP_GESTURE);

    LOG_DEBUG("Device '{}' capabilities: pointer={}, keyboard={}, touch={}, "
              "tablet={}, gesture={}",
              device->name, hasPointer, hasKeyboard, hasTouch, hasTablet, hasGesture);
}

// ---------------------------------------------------------------------------
// Device setup
// ---------------------------------------------------------------------------

void InputManager::setupKeyboard(wlr_input_device* device) {
    auto* wlrKeyboard = wlr_keyboard_from_input_device(device);

    // Apply per-device keyboard layout config
    const InputDeviceConfig* config = findDeviceConfig(device->name);
    if (config) {
        keyboard_->setLayout(config->kbLayout);
        keyboard_->setVariant(config->kbVariant);
        keyboard_->setModel(config->kbModel);
        keyboard_->setOptions(config->kbOptions);
        keyboard_->setRepeatRate(config->repeatRate);
        keyboard_->setRepeatDelay(config->repeatDelay);
        keyboard_->applyLayoutConfig();
    }

    keyboard_->attachDevice(wlrKeyboard);
    wlr_seat_set_keyboard(seat_, wlrKeyboard);
}

void InputManager::setupPointer(wlr_input_device* device) {
    // Attach to cursor so events are routed through wlr_cursor
    wlr_cursor_attach_input_device(cursor_, device);

    auto* wlrPointer = wlr_pointer_from_input_device(device);
    pointer_->attachDevice(wlrPointer);

    // Apply per-device pointer config
    const InputDeviceConfig* config = findDeviceConfig(device->name);
    if (config) {
        pointer_->setSensitivity(config->sensitivity);
        pointer_->setNaturalScroll(config->naturalScroll);
        pointer_->setScrollFactor(config->scrollFactor);
        pointer_->setLeftHanded(config->leftHanded);
        pointer_->setMiddleEmulation(config->middleEmulation);
    }
}

void InputManager::setupTouch(wlr_input_device* device) {
    wlr_cursor_attach_input_device(cursor_, device);

    auto* wlrTouch = wlr_touch_from_input_device(device);
    touch_->attachDevice(wlrTouch);
}

void InputManager::setupTablet(wlr_input_device* device) {
    auto* wlrTablet = wlr_tablet_from_input_device(device);
    tablet_->attachDevice(wlrTablet);

    // Initialize tablet v2 protocol if not already done
    tablet_->initTabletV2(display_);
}

} // namespace eternal

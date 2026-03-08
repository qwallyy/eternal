#include "eternal/utils/Logger.hpp"
#include "eternal/utils/Signal.hpp"

extern "C" {
#include <wayland-server-core.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_pointer.h>
#include <xkbcommon/xkbcommon.h>
}

#include <memory>
#include <string>
#include <vector>

namespace eternal {

namespace {

void initListener(wl_listener& listener) {
    listener.notify = nullptr;
    wl_list_init(&listener.link);
}

void resetListener(wl_listener& listener) {
    wl_list_remove(&listener.link);
    wl_list_init(&listener.link);
    listener.notify = nullptr;
}

} // namespace

// ---------------------------------------------------------------------------
// Seat - wraps wlr_seat and manages input devices
// ---------------------------------------------------------------------------

class Seat {
public:
    explicit Seat(struct wlr_seat* wlr_seat);
    ~Seat();

    Seat(const Seat&) = delete;
    Seat& operator=(const Seat&) = delete;

    /// Register a new input device with this seat.
    void addDevice(struct wlr_input_device* device);

    /// Set keyboard focus to a given wlr_surface (may be nullptr to clear).
    void setKeyboardFocus(struct wlr_surface* surface);

    /// Set pointer focus.
    void setPointerFocus(struct wlr_surface* surface, double sx, double sy);

    [[nodiscard]] struct wlr_seat* wlrSeat() const noexcept { return seat_; }

    // Signals
    Signal<uint32_t>& onKey()           { return sig_key_; }
    Signal<>&         onPointerMotion() { return sig_pointer_motion_; }
    Signal<uint32_t>& onPointerButton() { return sig_pointer_button_; }

private:
    void setupKeyboard(struct wlr_input_device* device);
    void setupPointer(struct wlr_input_device* device);
    void setupTouch(struct wlr_input_device* device);

    // Keyboard callbacks
    static void onKeyboardKey(struct wl_listener* listener, void* data);
    static void onKeyboardModifiers(struct wl_listener* listener, void* data);
    static void onKeyboardDestroy(struct wl_listener* listener, void* data);

    struct KeyboardState {
        Seat* seat = nullptr;
        struct wlr_keyboard* keyboard = nullptr;
        struct wl_listener key_listener{};
        struct wl_listener modifiers_listener{};
        struct wl_listener destroy_listener{};
    };

    struct wlr_seat* seat_ = nullptr;
    struct xkb_context* xkb_ctx_ = nullptr;

    std::vector<std::unique_ptr<KeyboardState>> keyboards_;

    Signal<uint32_t> sig_key_;
    Signal<>         sig_pointer_motion_;
    Signal<uint32_t> sig_pointer_button_;
};

// ---------------------------------------------------------------------------
// Implementation
// ---------------------------------------------------------------------------

Seat::Seat(struct wlr_seat* wlr_seat) : seat_(wlr_seat) {
    xkb_ctx_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!xkb_ctx_) {
        LOG_ERROR("Failed to create xkb_context");
    }
    LOG_DEBUG("Seat created");
}

Seat::~Seat() {
    for (auto& keyboard : keyboards_) {
        resetListener(keyboard->key_listener);
        resetListener(keyboard->modifiers_listener);
        resetListener(keyboard->destroy_listener);
    }
    keyboards_.clear();
    if (xkb_ctx_) {
        xkb_context_unref(xkb_ctx_);
    }
    LOG_DEBUG("Seat destroyed");
}

void Seat::addDevice(struct wlr_input_device* device) {
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
        default:
            LOG_DEBUG("Ignoring input device type {}", static_cast<int>(device->type));
            break;
    }
}

void Seat::setKeyboardFocus(struct wlr_surface* surface) {
    if (!seat_) return;

    struct wlr_keyboard* keyboard = wlr_seat_get_keyboard(seat_);
    if (!keyboard) return;

    if (surface) {
        wlr_seat_keyboard_notify_enter(seat_, surface,
            keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
    } else {
        wlr_seat_keyboard_notify_clear_focus(seat_);
    }
}

void Seat::setPointerFocus(struct wlr_surface* surface, double sx, double sy) {
    if (!seat_) return;

    uint32_t time_msec = 0;  // TODO: get actual timestamp
    wlr_seat_pointer_notify_enter(seat_, surface, sx, sy);
    wlr_seat_pointer_notify_motion(seat_, time_msec, sx, sy);
}

void Seat::setupKeyboard(struct wlr_input_device* device) {
    auto* wlr_kb = wlr_keyboard_from_input_device(device);

    // Apply default keymap
    struct xkb_keymap* keymap = xkb_keymap_new_from_names(
        xkb_ctx_, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (keymap) {
        wlr_keyboard_set_keymap(wlr_kb, keymap);
        xkb_keymap_unref(keymap);
    }

    wlr_keyboard_set_repeat_info(wlr_kb, 25, 600);

    auto state = std::make_unique<KeyboardState>();
    state->seat = this;
    state->keyboard = wlr_kb;
    initListener(state->key_listener);
    initListener(state->modifiers_listener);
    initListener(state->destroy_listener);

    state->key_listener.notify = onKeyboardKey;
    wl_signal_add(&wlr_kb->events.key, &state->key_listener);

    state->modifiers_listener.notify = onKeyboardModifiers;
    wl_signal_add(&wlr_kb->events.modifiers, &state->modifiers_listener);

    state->destroy_listener.notify = onKeyboardDestroy;
    wl_signal_add(&device->events.destroy, &state->destroy_listener);

    wlr_seat_set_keyboard(seat_, wlr_kb);

    // Update capabilities
    wlr_seat_set_capabilities(seat_,
        seat_->capabilities | WL_SEAT_CAPABILITY_KEYBOARD);

    keyboards_.push_back(std::move(state));

    LOG_INFO("Keyboard added: {}", device->name);
}

void Seat::setupPointer(struct wlr_input_device* device) {
    wlr_seat_set_capabilities(seat_,
        seat_->capabilities | WL_SEAT_CAPABILITY_POINTER);
    LOG_INFO("Pointer added: {}", device->name);
}

void Seat::setupTouch(struct wlr_input_device* device) {
    wlr_seat_set_capabilities(seat_,
        seat_->capabilities | WL_SEAT_CAPABILITY_TOUCH);
    LOG_INFO("Touch added: {}", device->name);
}

// ---------------------------------------------------------------------------
// Keyboard callbacks
// ---------------------------------------------------------------------------

void Seat::onKeyboardKey(struct wl_listener* listener, void* data) {
    KeyboardState* state = wl_container_of(listener, state, key_listener);
    auto* event = static_cast<struct wlr_keyboard_key_event*>(data);

    state->seat->sig_key_.emit(event->keycode);

    // Forward to seat
    wlr_seat_set_keyboard(state->seat->seat_, state->keyboard);
    wlr_seat_keyboard_notify_key(state->seat->seat_,
        event->time_msec, event->keycode, event->state);
}

void Seat::onKeyboardModifiers(struct wl_listener* listener, void* data) {
    KeyboardState* state = wl_container_of(listener, state, modifiers_listener);
    (void)data;

    wlr_seat_set_keyboard(state->seat->seat_, state->keyboard);
    wlr_seat_keyboard_notify_modifiers(state->seat->seat_,
        &state->keyboard->modifiers);
}

void Seat::onKeyboardDestroy(struct wl_listener* listener, void* data) {
    KeyboardState* state = wl_container_of(listener, state, destroy_listener);
    (void)data;

    resetListener(state->key_listener);
    resetListener(state->modifiers_listener);
    resetListener(state->destroy_listener);

    auto& keyboards = state->seat->keyboards_;
    keyboards.erase(
        std::remove_if(keyboards.begin(), keyboards.end(),
            [state](const auto& p) { return p.get() == state; }),
        keyboards.end());
}

} // namespace eternal

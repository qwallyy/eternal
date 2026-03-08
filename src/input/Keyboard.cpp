#include "eternal/input/Keyboard.hpp"
#include "eternal/input/InputManager.hpp"
#include "eternal/input/KeybindManager.hpp"
#include "eternal/utils/Logger.hpp"

extern "C" {
#include <wlr/backend/session.h>
#include <wlr/types/wlr_seat.h>
}

#include <algorithm>
#include <cassert>
#include <cstring>

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

unsigned vtFromKeysym(xkb_keysym_t keysym) {
    if (keysym >= XKB_KEY_XF86Switch_VT_1 && keysym <= XKB_KEY_XF86Switch_VT_12) {
        return static_cast<unsigned>(keysym - XKB_KEY_XF86Switch_VT_1 + 1);
    }
    if (keysym >= XKB_KEY_F1 && keysym <= XKB_KEY_F12) {
        return static_cast<unsigned>(keysym - XKB_KEY_F1 + 1);
    }
    return 0;
}

bool handleVirtualTerminalSwitch(InputManager* manager, xkb_keysym_t keysym, bool pressed) {
    if (!pressed || !manager) {
        return false;
    }

    auto* keyboard = manager->getKeyboard();
    auto* session = manager->getSession();
    if (!keyboard || !session) {
        return false;
    }

    if (!keyboard->isCtrlPressed() || !keyboard->isAltPressed()) {
        return false;
    }

    const unsigned vt = vtFromKeysym(keysym);
    if (vt == 0) {
        return false;
    }

    if (session->vtnr == 0) {
        LOG_WARN("Virtual terminal switching is not supported for this session");
        return true;
    }

    if (!wlr_session_change_vt(session, vt)) {
        LOG_ERROR("Failed to switch to VT{}", vt);
    } else {
        LOG_INFO("Switching to VT{}", vt);
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

Keyboard::Keyboard(InputManager* manager)
    : manager_(manager)
{
    assert(manager_);
    initListener(keyListener_);
    initListener(modifiersListener_);
    initListener(destroyListener_);
    initXkb();
}

Keyboard::~Keyboard() {
    stopRepeat();

    if (wlrKeyboard_) {
        resetListener(keyListener_);
        resetListener(modifiersListener_);
        resetListener(destroyListener_);
    }
    destroyXkb();
}

// ---------------------------------------------------------------------------
// Task 22: Full xkb_context, xkb_keymap, xkb_state lifecycle
// ---------------------------------------------------------------------------

void Keyboard::initXkb() {
    xkbContext_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!xkbContext_) {
        LOG_ERROR("Failed to create xkb_context");
        return;
    }

    applyLayoutConfig();
}

void Keyboard::destroyXkb() {
    if (xkbState_) {
        xkb_state_unref(xkbState_);
        xkbState_ = nullptr;
    }
    if (xkbKeymap_) {
        xkb_keymap_unref(xkbKeymap_);
        xkbKeymap_ = nullptr;
    }
    if (xkbContext_) {
        xkb_context_unref(xkbContext_);
        xkbContext_ = nullptr;
    }
}

void Keyboard::applyLayoutConfig() {
    if (!xkbContext_) return;

    xkb_rule_names rules = {};
    rules.layout = layout_.c_str();
    rules.variant = variant_.empty() ? nullptr : variant_.c_str();
    rules.model = model_.empty() ? nullptr : model_.c_str();
    rules.options = options_.empty() ? nullptr : options_.c_str();

    auto* newKeymap = xkb_keymap_new_from_names(
        xkbContext_, &rules, XKB_KEYMAP_COMPILE_NO_FLAGS);

    if (!newKeymap) {
        LOG_ERROR("Failed to compile xkb keymap for layout '{}'", layout_);
        return;
    }

    auto* newState = xkb_state_new(newKeymap);
    if (!newState) {
        xkb_keymap_unref(newKeymap);
        LOG_ERROR("Failed to create xkb_state");
        return;
    }

    // Replace old state
    if (xkbState_) xkb_state_unref(xkbState_);
    if (xkbKeymap_) xkb_keymap_unref(xkbKeymap_);

    xkbKeymap_ = newKeymap;
    xkbState_ = newState;
    currentLayoutIndex_ = 0;

    // Apply to wlr keyboard if attached
    if (wlrKeyboard_) {
        wlr_keyboard_set_keymap(wlrKeyboard_, xkbKeymap_);
    }

    LOG_INFO("Keyboard layout set to '{}' (variant: '{}', model: '{}', options: '{}')",
             layout_, variant_, model_, options_);
}

// ---------------------------------------------------------------------------
// Device attachment
// ---------------------------------------------------------------------------

void Keyboard::attachDevice(wlr_keyboard* keyboard) {
    // If we already have a keyboard attached, remove old listeners
    if (wlrKeyboard_) {
        resetListener(keyListener_);
        resetListener(modifiersListener_);
        resetListener(destroyListener_);
    }

    wlrKeyboard_ = keyboard;

    // Set keymap on the wlr keyboard
    if (xkbKeymap_) {
        wlr_keyboard_set_keymap(wlrKeyboard_, xkbKeymap_);
    }

    // Set repeat rate
    wlr_keyboard_set_repeat_info(wlrKeyboard_, repeatRate_, repeatDelay_);

    // Wire up listeners
    keyListener_.notify = handleKeyEvent;
    wl_signal_add(&wlrKeyboard_->events.key, &keyListener_);

    modifiersListener_.notify = handleModifiers;
    wl_signal_add(&wlrKeyboard_->events.modifiers, &modifiersListener_);

    destroyListener_.notify = handleDestroy;
    wl_signal_add(&wlrKeyboard_->base.events.destroy, &destroyListener_);

    applyLockDefaults();
}

// ---------------------------------------------------------------------------
// Task 22: Key press/release with proper state tracking + key repeat
// ---------------------------------------------------------------------------

void Keyboard::handleKeyEvent(wl_listener* listener, void* data) {
    Keyboard* self = wl_container_of(listener, self, keyListener_);
    auto* event = static_cast<wlr_keyboard_key_event*>(data);

    uint32_t keycode = event->keycode + 8; // Convert evdev to XKB keycode
    auto state = static_cast<wl_keyboard_key_state>(event->state);
    bool pressed = (state == WL_KEYBOARD_KEY_STATE_PRESSED);

    // Update internal XKB state
    if (self->xkbState_) {
        xkb_state_update_key(self->xkbState_, keycode,
            pressed ? XKB_KEY_DOWN : XKB_KEY_UP);
    }

    // Track pressed keys
    if (pressed) {
        if (std::find(self->pressedKeys_.begin(), self->pressedKeys_.end(),
                      event->keycode) == self->pressedKeys_.end()) {
            self->pressedKeys_.push_back(event->keycode);
        }
    } else {
        self->pressedKeys_.erase(
            std::remove(self->pressedKeys_.begin(), self->pressedKeys_.end(),
                        event->keycode),
            self->pressedKeys_.end());
    }

    // Translate keysym for binding matching
    xkb_keysym_t keysym = XKB_KEY_NoSymbol;
    if (self->xkbState_) {
        keysym = xkb_state_key_get_one_sym(self->xkbState_, keycode);
    }

    if (handleVirtualTerminalSwitch(self->manager_, keysym, pressed)) {
        if (!pressed && self->repeatKeycode_ == event->keycode) {
            self->stopRepeat();
        }
        return;
    }

    uint32_t mods = self->getModifiers();

    // Try keybind manager first (Task 29)
    auto* keybindMgr = self->manager_->getKeybindManager();
    if (keybindMgr) {
        bool consumed = keybindMgr->onKeyEvent(keysym, mods, pressed, false);
        if (consumed) {
            // Key was consumed by a binding; still update repeat
            if (pressed) {
                self->startRepeat(event->keycode);
            } else {
                if (self->repeatKeycode_ == event->keycode) {
                    self->stopRepeat();
                }
            }
            return;
        }
    }

    // Notify handler
    if (self->keyHandler_) {
        self->keyHandler_(event->keycode, mods, state);
    }

    // Forward to seat
    wlr_seat_set_keyboard(self->manager_->getSeat(), self->wlrKeyboard_);
    wlr_seat_keyboard_notify_key(self->manager_->getSeat(),
        event->time_msec, event->keycode, event->state);

    // Key repeat management
    if (pressed) {
        // Check if this key should repeat (has a keysym and keymap says it repeats)
        if (self->xkbKeymap_ &&
            xkb_keymap_key_repeats(self->xkbKeymap_, keycode)) {
            self->startRepeat(event->keycode);
        }
    } else {
        if (self->repeatKeycode_ == event->keycode) {
            self->stopRepeat();
        }
    }
}

void Keyboard::handleModifiers(wl_listener* listener, void* data) {
    Keyboard* self = wl_container_of(listener, self, modifiersListener_);
    (void)data;

    wlr_seat_set_keyboard(self->manager_->getSeat(), self->wlrKeyboard_);
    wlr_seat_keyboard_notify_modifiers(self->manager_->getSeat(),
        &self->wlrKeyboard_->modifiers);
}

void Keyboard::handleDestroy(wl_listener* listener, void* data) {
    Keyboard* self = wl_container_of(listener, self, destroyListener_);
    (void)data;

    self->stopRepeat();

    resetListener(self->keyListener_);
    resetListener(self->modifiersListener_);
    resetListener(self->destroyListener_);
    self->wlrKeyboard_ = nullptr;
    self->pressedKeys_.clear();
}

// ---------------------------------------------------------------------------
// Task 22: Key repeat via wl_event_source timer
// ---------------------------------------------------------------------------

void Keyboard::startRepeat(uint32_t keycode) {
    if (repeatRate_ <= 0) return;

    repeatKeycode_ = keycode;
    repeatActive_ = true;

    wl_display* display = manager_->getDisplay();
    if (!display) return;

    wl_event_loop* loop = wl_display_get_event_loop(display);

    // Remove existing timer if any
    if (repeatTimer_) {
        wl_event_source_remove(repeatTimer_);
    }

    // Start timer with the initial delay
    repeatTimer_ = wl_event_loop_add_timer(loop, handleRepeatTimer, this);
    wl_event_source_timer_update(repeatTimer_, repeatDelay_);
}

void Keyboard::stopRepeat() {
    repeatActive_ = false;
    repeatKeycode_ = 0;
    if (repeatTimer_) {
        wl_event_source_remove(repeatTimer_);
        repeatTimer_ = nullptr;
    }
}

int Keyboard::handleRepeatTimer(void* data) {
    auto* self = static_cast<Keyboard*>(data);

    if (!self->repeatActive_ || !self->wlrKeyboard_) {
        return 0;
    }

    uint32_t keycode = self->repeatKeycode_ + 8;
    xkb_keysym_t keysym = XKB_KEY_NoSymbol;
    if (self->xkbState_) {
        keysym = xkb_state_key_get_one_sym(self->xkbState_, keycode);
    }

    uint32_t mods = self->getModifiers();

    // Try keybind manager with isRepeat=true
    auto* keybindMgr = self->manager_->getKeybindManager();
    if (keybindMgr) {
        bool consumed = keybindMgr->onKeyEvent(keysym, mods, true, true);
        if (consumed) {
            // Reschedule for next repeat
            int intervalMs = 1000 / self->repeatRate_;
            wl_event_source_timer_update(self->repeatTimer_, intervalMs);
            return 0;
        }
    }

    // Forward repeat to seat
    wlr_seat_keyboard_notify_key(self->manager_->getSeat(),
        0, self->repeatKeycode_, WL_KEYBOARD_KEY_STATE_PRESSED);

    // Reschedule for next repeat at the repeat rate interval
    int intervalMs = 1000 / self->repeatRate_;
    wl_event_source_timer_update(self->repeatTimer_, intervalMs);

    return 0;
}

// ---------------------------------------------------------------------------
// High-level handleKey (for external callers, not from wl_listener)
// ---------------------------------------------------------------------------

void Keyboard::handleKey(uint32_t keycode, wl_keyboard_key_state state) {
    if (!xkbState_) return;

    uint32_t xkbKeycode = keycode + 8;
    bool pressed = (state == WL_KEYBOARD_KEY_STATE_PRESSED);

    xkb_state_update_key(xkbState_, xkbKeycode,
        pressed ? XKB_KEY_DOWN : XKB_KEY_UP);

    // Track pressed keys
    if (pressed) {
        if (std::find(pressedKeys_.begin(), pressedKeys_.end(), keycode) ==
            pressedKeys_.end()) {
            pressedKeys_.push_back(keycode);
        }
    } else {
        pressedKeys_.erase(
            std::remove(pressedKeys_.begin(), pressedKeys_.end(), keycode),
            pressedKeys_.end());
    }

    if (keyHandler_) {
        keyHandler_(keycode, getModifiers(), state);
    }
}

// ---------------------------------------------------------------------------
// Task 22: Layout switching (switchxkblayout dispatcher)
// ---------------------------------------------------------------------------

void Keyboard::switchLayout() {
    uint32_t count = getLayoutCount();
    if (count <= 1) return;

    uint32_t next = (currentLayoutIndex_ + 1) % count;
    switchLayoutTo(next);
}

void Keyboard::switchLayoutTo(uint32_t index) {
    if (!xkbKeymap_ || !xkbState_) return;

    uint32_t count = getLayoutCount();
    if (index >= count) {
        LOG_WARN("Layout index {} out of range (total: {})", index, count);
        return;
    }

    currentLayoutIndex_ = index;

    // Update the xkb state to use the new layout
    // We need to set the layout on both latched and locked
    xkb_state_update_mask(xkbState_,
        xkb_state_serialize_mods(xkbState_, XKB_STATE_MODS_DEPRESSED),
        xkb_state_serialize_mods(xkbState_, XKB_STATE_MODS_LATCHED),
        xkb_state_serialize_mods(xkbState_, XKB_STATE_MODS_LOCKED),
        0, 0, currentLayoutIndex_);

    // Update the wlr_keyboard modifiers and notify clients via seat
    if (wlrKeyboard_) {
        wlrKeyboard_->modifiers.group = currentLayoutIndex_;
        wlr_seat_keyboard_notify_modifiers(manager_->getSeat(),
            &wlrKeyboard_->modifiers);
    }

    LOG_INFO("Switched to keyboard layout index {}: {}", index, getLayoutName(index));
}

uint32_t Keyboard::getLayoutCount() const {
    if (!xkbKeymap_) return 0;
    return xkb_keymap_num_layouts(xkbKeymap_);
}

uint32_t Keyboard::getCurrentLayout() const {
    return currentLayoutIndex_;
}

std::string Keyboard::getLayoutName(uint32_t index) const {
    if (!xkbKeymap_) return {};
    const char* name = xkb_keymap_layout_get_name(xkbKeymap_, index);
    return name ? std::string(name) : std::string{};
}

// ---------------------------------------------------------------------------
// Layout setters
// ---------------------------------------------------------------------------

void Keyboard::setLayout(const std::string& layout) { layout_ = layout; }
void Keyboard::setVariant(const std::string& variant) { variant_ = variant; }
void Keyboard::setModel(const std::string& model) { model_ = model; }
void Keyboard::setOptions(const std::string& options) { options_ = options; }

// ---------------------------------------------------------------------------
// Task 22: Modifier tracking
// ---------------------------------------------------------------------------

uint32_t Keyboard::getModifiers() const {
    if (!wlrKeyboard_) {
        if (!xkbState_) return 0;

        uint32_t mods = 0;
        if (xkb_state_mod_name_is_active(xkbState_, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE))
            mods |= WLR_MODIFIER_SHIFT;
        if (xkb_state_mod_name_is_active(xkbState_, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE))
            mods |= WLR_MODIFIER_CTRL;
        if (xkb_state_mod_name_is_active(xkbState_, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE))
            mods |= WLR_MODIFIER_ALT;
        if (xkb_state_mod_name_is_active(xkbState_, XKB_MOD_NAME_LOGO, XKB_STATE_MODS_EFFECTIVE))
            mods |= WLR_MODIFIER_LOGO;
        return mods;
    }
    return wlr_keyboard_get_modifiers(wlrKeyboard_);
}

bool Keyboard::isModPressed(uint32_t mod) const {
    return (getModifiers() & mod) != 0;
}

bool Keyboard::isShiftPressed() const {
    return isModPressed(WLR_MODIFIER_SHIFT);
}

bool Keyboard::isCtrlPressed() const {
    return isModPressed(WLR_MODIFIER_CTRL);
}

bool Keyboard::isAltPressed() const {
    return isModPressed(WLR_MODIFIER_ALT);
}

bool Keyboard::isSuperPressed() const {
    return isModPressed(WLR_MODIFIER_LOGO);
}

bool Keyboard::isCapsLockActive() const {
    if (!xkbState_) return false;
    return xkb_state_mod_name_is_active(xkbState_, XKB_MOD_NAME_CAPS,
                                        XKB_STATE_MODS_LOCKED) > 0;
}

bool Keyboard::isNumLockActive() const {
    if (!xkbState_) return false;
    return xkb_state_mod_name_is_active(xkbState_, XKB_MOD_NAME_NUM,
                                        XKB_STATE_MODS_LOCKED) > 0;
}

// ---------------------------------------------------------------------------
// Keysym translation
// ---------------------------------------------------------------------------

xkb_keysym_t Keyboard::translateKeysym(uint32_t keycode) const {
    if (!xkbState_) return XKB_KEY_NoSymbol;
    return xkb_state_key_get_one_sym(xkbState_, keycode + 8);
}

std::vector<xkb_keysym_t> Keyboard::translateKeysyms(uint32_t keycode) const {
    std::vector<xkb_keysym_t> result;
    if (!xkbState_) return result;

    const xkb_keysym_t* syms = nullptr;
    int count = xkb_state_key_get_syms(xkbState_, keycode + 8, &syms);
    result.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        result.push_back(syms[i]);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Repeat configuration
// ---------------------------------------------------------------------------

void Keyboard::setRepeatRate(int32_t rate) {
    repeatRate_ = rate;
    if (wlrKeyboard_) {
        wlr_keyboard_set_repeat_info(wlrKeyboard_, repeatRate_, repeatDelay_);
    }
}

void Keyboard::setRepeatDelay(int32_t delay) {
    repeatDelay_ = delay;
    if (wlrKeyboard_) {
        wlr_keyboard_set_repeat_info(wlrKeyboard_, repeatRate_, repeatDelay_);
    }
}

void Keyboard::setKeyHandler(KeyHandler handler) {
    keyHandler_ = std::move(handler);
}

bool Keyboard::isKeyPressed(uint32_t keycode) const {
    return std::find(pressedKeys_.begin(), pressedKeys_.end(), keycode) !=
           pressedKeys_.end();
}

// ---------------------------------------------------------------------------
// Lock key defaults
// ---------------------------------------------------------------------------

void Keyboard::applyLockDefaults() {
    if (!xkbState_ || !wlrKeyboard_) return;

    if (numlockByDefault_) {
        xkb_keycode_t numLockKey = 0;
        auto minKey = xkb_keymap_min_keycode(xkbKeymap_);
        auto maxKey = xkb_keymap_max_keycode(xkbKeymap_);
        for (xkb_keycode_t k = minKey; k <= maxKey; ++k) {
            const xkb_keysym_t* syms = nullptr;
            int nsyms = xkb_keymap_key_get_syms_by_level(xkbKeymap_, k, 0, 0, &syms);
            for (int i = 0; i < nsyms; ++i) {
                if (syms[i] == XKB_KEY_Num_Lock) {
                    numLockKey = k;
                    break;
                }
            }
            if (numLockKey) break;
        }
        if (numLockKey) {
            xkb_state_update_key(xkbState_, numLockKey, XKB_KEY_DOWN);
            xkb_state_update_key(xkbState_, numLockKey, XKB_KEY_UP);
        }
    }

    if (capslockByDefault_) {
        xkb_keycode_t capsLockKey = 0;
        auto minKey = xkb_keymap_min_keycode(xkbKeymap_);
        auto maxKey = xkb_keymap_max_keycode(xkbKeymap_);
        for (xkb_keycode_t k = minKey; k <= maxKey; ++k) {
            const xkb_keysym_t* syms = nullptr;
            int nsyms = xkb_keymap_key_get_syms_by_level(xkbKeymap_, k, 0, 0, &syms);
            for (int i = 0; i < nsyms; ++i) {
                if (syms[i] == XKB_KEY_Caps_Lock) {
                    capsLockKey = k;
                    break;
                }
            }
            if (capsLockKey) break;
        }
        if (capsLockKey) {
            xkb_state_update_key(xkbState_, capsLockKey, XKB_KEY_DOWN);
            xkb_state_update_key(xkbState_, capsLockKey, XKB_KEY_UP);
        }
    }
}

} // namespace eternal

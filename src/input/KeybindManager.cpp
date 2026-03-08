#include "eternal/input/KeybindManager.hpp"
#include "eternal/input/InputManager.hpp"
#include "eternal/input/Keyboard.hpp"
#include "eternal/utils/Logger.hpp"

extern "C" {
#include <wayland-server-core.h>
#include <wlr/types/wlr_keyboard.h>
}

#include <algorithm>
#include <cassert>
#include <sstream>

namespace eternal {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

KeybindManager::KeybindManager(InputManager* manager)
    : manager_(manager)
{
    assert(manager_);

    // Register built-in dispatchers
    registerDispatcher("submap", [this](const std::string& args) {
        if (args == "reset" || args.empty()) {
            exitSubmap();
        } else {
            enterSubmap(args);
        }
    });

    registerDispatcher("switchxkblayout", [this](const std::string& /*args*/) {
        auto* kb = manager_->getKeyboard();
        if (kb) {
            kb->switchLayout();
        }
    });
}

KeybindManager::~KeybindManager() {
    if (chordTimer_) {
        wl_event_source_remove(chordTimer_);
        chordTimer_ = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Task 29: Parse bind config entries
// Format: "MODIFIERS, key, dispatcher, args"
// Flags can be prefixed: "bind[flags] = ..."
// ---------------------------------------------------------------------------

static std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return {};
    auto end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
}

static std::vector<std::string> splitByComma(const std::string& s) {
    std::vector<std::string> parts;
    std::istringstream stream(s);
    std::string part;
    while (std::getline(stream, part, ',')) {
        parts.push_back(trim(part));
    }
    return parts;
}

uint32_t KeybindManager::parseModifiers(const std::string& modStr) {
    uint32_t mods = 0;
    std::string upper = modStr;
    std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

    // Split by '+' or space
    std::istringstream stream(upper);
    std::string token;
    while (stream >> token) {
        // Also split by '+'
        std::istringstream tokenStream(token);
        std::string sub;
        while (std::getline(tokenStream, sub, '+')) {
            sub = trim(sub);
            if (sub.empty()) continue;

            if (sub == "SUPER" || sub == "MOD4" || sub == "LOGO" || sub == "WIN") {
                mods |= WLR_MODIFIER_LOGO;
            } else if (sub == "CTRL" || sub == "CONTROL") {
                mods |= WLR_MODIFIER_CTRL;
            } else if (sub == "ALT" || sub == "MOD1") {
                mods |= WLR_MODIFIER_ALT;
            } else if (sub == "SHIFT") {
                mods |= WLR_MODIFIER_SHIFT;
            } else if (sub == "MOD2") {
                mods |= WLR_MODIFIER_MOD2;
            } else if (sub == "MOD3") {
                mods |= WLR_MODIFIER_MOD3;
            } else if (sub == "MOD5") {
                mods |= WLR_MODIFIER_MOD5;
            }
        }
    }
    return mods;
}

xkb_keysym_t KeybindManager::parseKeysym(const std::string& keyStr) {
    if (keyStr.empty()) return XKB_KEY_NoSymbol;

    // Handle mouse: prefixed keys
    if (keyStr.starts_with("mouse:")) {
        return XKB_KEY_NoSymbol;  // Not a keyboard keysym
    }

    // Try xkb_keysym_from_name (case insensitive)
    xkb_keysym_t sym = xkb_keysym_from_name(keyStr.c_str(), XKB_KEYSYM_CASE_INSENSITIVE);
    if (sym != XKB_KEY_NoSymbol) return sym;

    // Try with common aliases
    std::string lower = keyStr;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    if (lower == "return" || lower == "enter") return XKB_KEY_Return;
    if (lower == "space") return XKB_KEY_space;
    if (lower == "tab") return XKB_KEY_Tab;
    if (lower == "escape" || lower == "esc") return XKB_KEY_Escape;
    if (lower == "backspace") return XKB_KEY_BackSpace;
    if (lower == "delete") return XKB_KEY_Delete;
    if (lower == "home") return XKB_KEY_Home;
    if (lower == "end") return XKB_KEY_End;
    if (lower == "pageup" || lower == "page_up") return XKB_KEY_Page_Up;
    if (lower == "pagedown" || lower == "page_down") return XKB_KEY_Page_Down;
    if (lower == "left") return XKB_KEY_Left;
    if (lower == "right") return XKB_KEY_Right;
    if (lower == "up") return XKB_KEY_Up;
    if (lower == "down") return XKB_KEY_Down;
    if (lower == "print") return XKB_KEY_Print;

    LOG_WARN("Unknown keysym: '{}'", keyStr);
    return XKB_KEY_NoSymbol;
}

BindFlag KeybindManager::parseFlags(const std::string& flagStr) {
    BindFlag flags = BindFlag::None;
    for (char c : flagStr) {
        switch (c) {
        case 'r': flags = flags | BindFlag::Repeat; break;
        case 'e': flags = flags | BindFlag::Release; break;
        case 'l': flags = flags | BindFlag::Locked; break;
        case 't': flags = flags | BindFlag::Transparent; break;
        case 'm': flags = flags | BindFlag::Mouse; break;
        default: break;
        }
    }
    return flags;
}

bool KeybindManager::addBind(const std::string& bindString) {
    // Format: "MODS, key, dispatcher, args"
    // or with chord: "MODS, key key2, dispatcher, args"
    auto parts = splitByComma(bindString);
    if (parts.size() < 3) {
        LOG_WARN("Invalid bind string (need at least 3 comma-separated parts): '{}'",
                 bindString);
        return false;
    }

    Keybinding binding;
    binding.modifiers = parseModifiers(parts[0]);

    // Check for chord syntax: "key key2" (space-separated)
    std::string keyPart = parts[1];
    auto spacePos = keyPart.find(' ');
    if (spacePos != std::string::npos) {
        // Task 30: Chord binding
        std::string firstKey = trim(keyPart.substr(0, spacePos));
        std::string secondKey = trim(keyPart.substr(spacePos + 1));

        binding.keysym = parseKeysym(firstKey);
        binding.chordKeysym = parseKeysym(secondKey);
        // Chord second key inherits same modifiers by default;
        // could extend to parse "MODS key MODS2 key2" but keep simple
        binding.chordModifiers = binding.modifiers;
    } else {
        binding.keysym = parseKeysym(keyPart);
    }

    if (binding.keysym == XKB_KEY_NoSymbol && !binding.chordKeysym) {
        LOG_WARN("Failed to parse keysym from: '{}'", parts[1]);
        return false;
    }

    binding.dispatcher = parts[2];

    // Join remaining parts as args (in case args contain commas)
    if (parts.size() > 3) {
        std::string args;
        for (size_t i = 3; i < parts.size(); ++i) {
            if (!args.empty()) args += ", ";
            args += parts[i];
        }
        binding.args = args;
    }

    binding.submap = activeSubmap_;

    bindings_.push_back(binding);

    LOG_DEBUG("Added keybinding: mods={:#x} key={:#x} -> {} {}",
              binding.modifiers, static_cast<uint32_t>(binding.keysym),
              binding.dispatcher, binding.args);
    return true;
}

void KeybindManager::addBind(const Keybinding& binding) {
    bindings_.push_back(binding);
}

// ---------------------------------------------------------------------------
// Task 29: Mouse button bindings (bind with mouse flag)
// Format: "MODS, mouse:BUTTON, dispatcher, args"
// ---------------------------------------------------------------------------

bool KeybindManager::addMouseBind(const std::string& bindString) {
    auto parts = splitByComma(bindString);
    if (parts.size() < 3) {
        LOG_WARN("Invalid mouse bind string: '{}'", bindString);
        return false;
    }

    Keybinding binding;
    binding.modifiers = parseModifiers(parts[0]);

    // Parse mouse button
    std::string keyPart = parts[1];
    if (keyPart.starts_with("mouse:")) {
        try {
            binding.mouseButton = static_cast<uint32_t>(
                std::stoul(keyPart.substr(6)));
        } catch (...) {
            LOG_WARN("Invalid mouse button: '{}'", keyPart);
            return false;
        }
    } else {
        LOG_WARN("Mouse bind requires 'mouse:BUTTON' format: '{}'", keyPart);
        return false;
    }

    binding.dispatcher = parts[2];
    binding.flags = BindFlag::Mouse | BindFlag::MouseDrag;

    if (parts.size() > 3) {
        std::string args;
        for (size_t i = 3; i < parts.size(); ++i) {
            if (!args.empty()) args += ", ";
            args += parts[i];
        }
        binding.args = args;
    }

    binding.submap = activeSubmap_;

    bindings_.push_back(binding);

    LOG_DEBUG("Added mouse binding: mods={:#x} button={} -> {} {}",
              binding.modifiers, binding.mouseButton,
              binding.dispatcher, binding.args);
    return true;
}

void KeybindManager::removeBind(uint32_t mods, xkb_keysym_t keysym) {
    bindings_.erase(
        std::remove_if(bindings_.begin(), bindings_.end(),
            [mods, keysym, this](const Keybinding& b) {
                return b.modifiers == mods && b.keysym == keysym &&
                       b.submap == activeSubmap_;
            }),
        bindings_.end());
}

void KeybindManager::clearBindings() {
    bindings_.clear();
}

// ---------------------------------------------------------------------------
// Task 29: Match incoming key events against bindings
// ---------------------------------------------------------------------------

const Keybinding* KeybindManager::findKeyBinding(xkb_keysym_t keysym,
                                                   uint32_t modifiers,
                                                   bool release,
                                                   bool isRepeat) const {
    for (const auto& binding : bindings_) {
        // Must be in the current submap
        if (binding.submap != activeSubmap_) continue;

        // Skip mouse bindings
        if (hasFlag(binding.flags, BindFlag::Mouse)) continue;

        // Check release flag
        bool bindRelease = hasFlag(binding.flags, BindFlag::Release);
        if (release != bindRelease) continue;

        // Check repeat flag
        if (isRepeat && !hasFlag(binding.flags, BindFlag::Repeat)) continue;

        // Match modifiers
        if (binding.modifiers != modifiers) continue;

        // Match keysym
        if (binding.keysym != keysym) continue;

        return &binding;
    }
    return nullptr;
}

const Keybinding* KeybindManager::findMouseBinding(uint32_t button,
                                                     uint32_t modifiers,
                                                     bool release) const {
    for (const auto& binding : bindings_) {
        if (binding.submap != activeSubmap_) continue;
        if (!hasFlag(binding.flags, BindFlag::Mouse)) continue;
        if (binding.mouseButton != button) continue;
        if (binding.modifiers != modifiers) continue;

        bool bindRelease = hasFlag(binding.flags, BindFlag::Release);
        if (release != bindRelease) continue;

        return &binding;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Task 29 + 30: Key event handling with chord support
// ---------------------------------------------------------------------------

bool KeybindManager::onKeyEvent(xkb_keysym_t keysym, uint32_t modifiers,
                                 bool pressed, bool isRepeat) {
    // ---------------------
    // Task 30: Chord state machine
    // ---------------------

    if (chordState_ == ChordState::Pending) {
        // We are waiting for the second key of a chord
        if (!pressed) return false;  // Ignore releases while waiting for chord

        assert(pendingChordBinding_);

        if (pendingChordBinding_->chordKeysym &&
            keysym == *pendingChordBinding_->chordKeysym) {
            // Check modifiers for the second key
            // (typically same as first, but can differ)
            bool modsMatch = (pendingChordBinding_->chordModifiers == modifiers);
            if (modsMatch) {
                // Chord completed! Execute the dispatcher.
                LOG_DEBUG("Chord completed: {} + {} -> {}",
                          pendingChordBinding_->keysym,
                          static_cast<uint32_t>(keysym),
                          pendingChordBinding_->dispatcher);

                executeDispatcher(pendingChordBinding_->dispatcher,
                                  pendingChordBinding_->args);

                cancelChord();
                return true;
            }
        }

        // Wrong key pressed - cancel the chord
        LOG_DEBUG("Chord cancelled: wrong second key");
        cancelChord();
        // Fall through to try matching as a normal key
    }

    // ---------------------
    // Normal binding match
    // ---------------------

    const Keybinding* binding = findKeyBinding(keysym, modifiers, !pressed, isRepeat);
    if (!binding) return false;

    // Task 30: If this binding has a chord second key, start the chord
    if (binding->chordKeysym && pressed && !isRepeat) {
        chordState_ = ChordState::Pending;
        pendingChordBinding_ = binding;
        startChordTimer();

        // Notify visual indicator
        if (chordIndicatorCallback_) {
            chordIndicatorCallback_(true, buildChordDescription(*binding));
        }

        LOG_DEBUG("Chord pending: waiting for second key after {}",
                  static_cast<uint32_t>(keysym));
        return true;  // Consume the first key
    }

    // Execute the binding
    executeDispatcher(binding->dispatcher, binding->args);

    // Task 29: Transparent flag - pass the key event through after handling
    return !hasFlag(binding->flags, BindFlag::Transparent);
}

// ---------------------------------------------------------------------------
// Task 29: Mouse button binding matching
// ---------------------------------------------------------------------------

bool KeybindManager::onMouseButton(uint32_t button, uint32_t modifiers, bool pressed) {
    const Keybinding* binding = findMouseBinding(button, modifiers, !pressed);
    if (!binding) return false;

    if (pressed && hasFlag(binding->flags, BindFlag::MouseDrag)) {
        // Start a mouse drag binding
        dragActive_ = true;
        activeDragBinding_ = binding;
        LOG_DEBUG("Mouse drag binding started: {} {}", binding->dispatcher, binding->args);
    }

    executeDispatcher(binding->dispatcher, binding->args);

    return !hasFlag(binding->flags, BindFlag::Transparent);
}

// ---------------------------------------------------------------------------
// Task 29: Mouse drag bindings (bindm for move/resize)
// ---------------------------------------------------------------------------

void KeybindManager::onMouseDragMotion(double dx, double dy) {
    if (!dragActive_ || !activeDragBinding_) return;

    // The move/resize dispatchers can use the delta from the pointer
    // The actual window movement is handled by the dispatcher
    (void)dx;
    (void)dy;
}

void KeybindManager::onMouseDragEnd() {
    if (!dragActive_) return;

    LOG_DEBUG("Mouse drag binding ended");
    dragActive_ = false;
    activeDragBinding_ = nullptr;
}

// ---------------------------------------------------------------------------
// Task 29: Execute dispatcher commands on match
// ---------------------------------------------------------------------------

void KeybindManager::executeDispatcher(const std::string& dispatcher,
                                        const std::string& args) {
    auto it = dispatchers_.find(dispatcher);
    if (it == dispatchers_.end()) {
        LOG_WARN("Unknown dispatcher: '{}'", dispatcher);
        return;
    }

    LOG_DEBUG("Executing dispatcher: {} (args: '{}')", dispatcher, args);
    it->second(args);
}

// ---------------------------------------------------------------------------
// Task 29: Submap / mode support
// ---------------------------------------------------------------------------

void KeybindManager::enterSubmap(const std::string& name) {
    LOG_INFO("Entering submap: '{}'", name);
    activeSubmap_ = name;
}

void KeybindManager::exitSubmap() {
    LOG_INFO("Exiting submap '{}' -> global", activeSubmap_);
    activeSubmap_.clear();
}

// ---------------------------------------------------------------------------
// Dispatcher registration
// ---------------------------------------------------------------------------

void KeybindManager::registerDispatcher(const std::string& name, DispatcherFunc func) {
    dispatchers_[name] = std::move(func);
}

// ---------------------------------------------------------------------------
// Task 30: Chord timeout
// ---------------------------------------------------------------------------

void KeybindManager::startChordTimer() {
    wl_display* display = manager_->getDisplay();
    if (!display) return;

    wl_event_loop* loop = wl_display_get_event_loop(display);

    if (chordTimer_) {
        wl_event_source_remove(chordTimer_);
    }

    chordTimer_ = wl_event_loop_add_timer(loop, onChordTimeout, this);
    wl_event_source_timer_update(chordTimer_, chordTimeoutMs_);
}

void KeybindManager::cancelChord() {
    chordState_ = ChordState::Idle;
    pendingChordBinding_ = nullptr;

    if (chordTimer_) {
        wl_event_source_remove(chordTimer_);
        chordTimer_ = nullptr;
    }

    // Notify visual indicator that chord is no longer pending
    if (chordIndicatorCallback_) {
        chordIndicatorCallback_(false, {});
    }
}

int KeybindManager::onChordTimeout(void* data) {
    auto* self = static_cast<KeybindManager*>(data);

    if (self->chordState_ == ChordState::Pending) {
        LOG_DEBUG("Chord timeout - resetting");
        self->cancelChord();
    }

    return 0;
}

// ---------------------------------------------------------------------------
// Task 30: Visual indicator description
// ---------------------------------------------------------------------------

std::string KeybindManager::buildChordDescription(const Keybinding& binding) const {
    std::string desc;

    // Build modifier prefix
    if (binding.modifiers & WLR_MODIFIER_LOGO) desc += "Super+";
    if (binding.modifiers & WLR_MODIFIER_CTRL) desc += "Ctrl+";
    if (binding.modifiers & WLR_MODIFIER_ALT) desc += "Alt+";
    if (binding.modifiers & WLR_MODIFIER_SHIFT) desc += "Shift+";

    // First key
    char nameBuf[64];
    xkb_keysym_get_name(binding.keysym, nameBuf, sizeof(nameBuf));
    desc += nameBuf;

    desc += " -> waiting for: ";

    // Second key (chord)
    if (binding.chordKeysym) {
        if (binding.chordModifiers & WLR_MODIFIER_LOGO) desc += "Super+";
        if (binding.chordModifiers & WLR_MODIFIER_CTRL) desc += "Ctrl+";
        if (binding.chordModifiers & WLR_MODIFIER_ALT) desc += "Alt+";
        if (binding.chordModifiers & WLR_MODIFIER_SHIFT) desc += "Shift+";

        xkb_keysym_get_name(*binding.chordKeysym, nameBuf, sizeof(nameBuf));
        desc += nameBuf;
    }

    return desc;
}

void KeybindManager::setChordIndicatorCallback(ChordIndicatorCallback callback) {
    chordIndicatorCallback_ = std::move(callback);
}

} // namespace eternal

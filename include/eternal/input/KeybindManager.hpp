#pragma once

extern "C" {
#include <wayland-server-core.h>
#include <xkbcommon/xkbcommon.h>
}

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace eternal {

class InputManager;

// ---------------------------------------------------------------------------
// Bind flags - bitfield controlling when/how a binding fires
// ---------------------------------------------------------------------------

enum class BindFlag : uint32_t {
    None        = 0,
    Release     = 1 << 0,   // Fire on key release instead of press
    Repeat      = 1 << 1,   // Allow key-repeat to re-trigger
    Locked      = 1 << 2,   // Active even when screen is locked
    Transparent = 1 << 3,   // Pass the key event through after handling
    Mouse       = 1 << 4,   // This is a mouse button binding
    MouseDrag   = 1 << 5,   // bindm: mouse drag for move/resize
};

inline BindFlag operator|(BindFlag a, BindFlag b) {
    return static_cast<BindFlag>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline BindFlag operator&(BindFlag a, BindFlag b) {
    return static_cast<BindFlag>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline bool hasFlag(BindFlag flags, BindFlag test) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(test)) != 0;
}

// ---------------------------------------------------------------------------
// Keybinding definition
// ---------------------------------------------------------------------------

struct Keybinding {
    uint32_t    modifiers = 0;          // WLR_MODIFIER_* mask
    xkb_keysym_t keysym = XKB_KEY_NoSymbol;
    uint32_t    mouseButton = 0;        // Non-zero for mouse bindings
    std::string dispatcher;             // Dispatcher name (e.g. "exec", "killactive")
    std::string args;                   // Dispatcher arguments
    BindFlag    flags = BindFlag::None;
    std::string submap;                 // Which submap this binding belongs to ("" = global)

    // Chord support (Task 30): optional second key in a chord sequence
    std::optional<xkb_keysym_t> chordKeysym;
    uint32_t    chordModifiers = 0;
};

// ---------------------------------------------------------------------------
// Chord state machine (Task 30)
// ---------------------------------------------------------------------------

enum class ChordState {
    Idle,       // No chord in progress
    Pending,    // First key of chord matched, waiting for second key
};

// ---------------------------------------------------------------------------
// KeybindManager - parses, stores, and matches key/mouse bindings
// ---------------------------------------------------------------------------

class KeybindManager {
public:
    explicit KeybindManager(InputManager* manager);
    ~KeybindManager();

    KeybindManager(const KeybindManager&) = delete;
    KeybindManager& operator=(const KeybindManager&) = delete;

    // -----------------------------------------------------------------------
    // Binding registration
    // -----------------------------------------------------------------------

    /// Parse and add a binding from config string: "SUPER, Return, exec, kitty"
    bool addBind(const std::string& bindString);

    /// Add a binding from explicit fields
    void addBind(const Keybinding& binding);

    /// Add a mouse-drag binding (bindm): "SUPER, mouse:272, movewindow"
    bool addMouseBind(const std::string& bindString);

    /// Remove all bindings matching the dispatcher in the current submap
    void removeBind(uint32_t mods, xkb_keysym_t keysym);

    /// Remove all bindings
    void clearBindings();

    // -----------------------------------------------------------------------
    // Event matching
    // -----------------------------------------------------------------------

    /// Called on keyboard key events. Returns true if the event was consumed.
    bool onKeyEvent(xkb_keysym_t keysym, uint32_t modifiers,
                    bool pressed, bool isRepeat);

    /// Called on mouse button events. Returns true if consumed.
    bool onMouseButton(uint32_t button, uint32_t modifiers, bool pressed);

    /// Called on mouse motion while a drag binding is active.
    void onMouseDragMotion(double dx, double dy);

    /// Called when mouse drag ends.
    void onMouseDragEnd();

    // -----------------------------------------------------------------------
    // Submap / mode support
    // -----------------------------------------------------------------------

    /// Enter a named submap (mode). Bindings in that submap become active.
    void enterSubmap(const std::string& name);

    /// Exit back to the global submap.
    void exitSubmap();

    /// Get current submap name ("" = global).
    [[nodiscard]] const std::string& currentSubmap() const { return activeSubmap_; }

    // -----------------------------------------------------------------------
    // Chord state queries (Task 30)
    // -----------------------------------------------------------------------

    [[nodiscard]] ChordState chordState() const { return chordState_; }
    [[nodiscard]] bool isChordPending() const { return chordState_ == ChordState::Pending; }

    // -----------------------------------------------------------------------
    // Dispatcher registration
    // -----------------------------------------------------------------------

    /// Register a named dispatcher function
    using DispatcherFunc = std::function<void(const std::string& args)>;
    void registerDispatcher(const std::string& name, DispatcherFunc func);

    // -----------------------------------------------------------------------
    // Chord visual indicator callback (Task 30)
    // -----------------------------------------------------------------------

    using ChordIndicatorCallback = std::function<void(bool pending, const std::string& description)>;
    void setChordIndicatorCallback(ChordIndicatorCallback callback);

    // -----------------------------------------------------------------------
    // Configuration
    // -----------------------------------------------------------------------

    /// Set chord timeout in milliseconds (default 500ms).
    void setChordTimeout(uint32_t ms) { chordTimeoutMs_ = ms; }
    [[nodiscard]] uint32_t getChordTimeout() const { return chordTimeoutMs_; }

    // -----------------------------------------------------------------------
    // Query
    // -----------------------------------------------------------------------

    [[nodiscard]] const std::vector<Keybinding>& getBindings() const { return bindings_; }

    /// Check if a drag binding is currently active
    [[nodiscard]] bool isDragActive() const { return dragActive_; }

private:
    InputManager* manager_ = nullptr;

    // All registered bindings
    std::vector<Keybinding> bindings_;

    // Registered dispatcher functions
    std::unordered_map<std::string, DispatcherFunc> dispatchers_;

    // Active submap
    std::string activeSubmap_;

    // -----------------------------------------------------------------------
    // Chord state machine (Task 30)
    // -----------------------------------------------------------------------

    ChordState chordState_ = ChordState::Idle;
    const Keybinding* pendingChordBinding_ = nullptr;
    wl_event_source* chordTimer_ = nullptr;
    uint32_t chordTimeoutMs_ = 500;

    ChordIndicatorCallback chordIndicatorCallback_;

    void startChordTimer();
    void cancelChord();
    static int onChordTimeout(void* data);

    // -----------------------------------------------------------------------
    // Mouse drag state
    // -----------------------------------------------------------------------

    bool dragActive_ = false;
    const Keybinding* activeDragBinding_ = nullptr;

    // -----------------------------------------------------------------------
    // Internal helpers
    // -----------------------------------------------------------------------

    /// Find matching binding(s) for a key event in the current submap
    const Keybinding* findKeyBinding(xkb_keysym_t keysym, uint32_t modifiers,
                                     bool release, bool isRepeat) const;

    /// Find matching binding for a mouse button event
    const Keybinding* findMouseBinding(uint32_t button, uint32_t modifiers,
                                       bool release) const;

    /// Execute a dispatcher by name with args
    void executeDispatcher(const std::string& dispatcher, const std::string& args);

    /// Parse modifier string ("SUPER", "CTRL+SHIFT", etc.) into WLR_MODIFIER mask
    static uint32_t parseModifiers(const std::string& modStr);

    /// Parse keysym name to xkb_keysym_t
    static xkb_keysym_t parseKeysym(const std::string& keyStr);

    /// Parse bind flags from prefix tokens
    static BindFlag parseFlags(const std::string& flagStr);

    /// Build a human-readable description of a pending chord for the indicator
    std::string buildChordDescription(const Keybinding& binding) const;
};

} // namespace eternal

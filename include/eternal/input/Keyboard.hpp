#pragma once

extern "C" {
#include <wayland-server-core.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_seat.h>
#include <xkbcommon/xkbcommon.h>
}

#include <cstdint>
#include <string>
#include <functional>
#include <vector>

namespace eternal {

class InputManager;

class Keyboard {
public:
    explicit Keyboard(InputManager* manager);
    ~Keyboard();

    Keyboard(const Keyboard&) = delete;
    Keyboard& operator=(const Keyboard&) = delete;

    // Core handling
    void handleKey(uint32_t keycode, wl_keyboard_key_state state);
    void attachDevice(wlr_keyboard* keyboard);

    // Layout configuration
    void setLayout(const std::string& layout);
    void setVariant(const std::string& variant);
    void setModel(const std::string& model);
    void setOptions(const std::string& options);
    void applyLayoutConfig();

    /// Switch to the next keyboard layout. Wraps around.
    void switchLayout();

    /// Switch to a specific layout index.
    void switchLayoutTo(uint32_t index);

    /// Get the number of layouts in the current keymap.
    [[nodiscard]] uint32_t getLayoutCount() const;

    /// Get the current layout index.
    [[nodiscard]] uint32_t getCurrentLayout() const;

    /// Get layout name by index.
    [[nodiscard]] std::string getLayoutName(uint32_t index) const;

    // Modifier queries
    [[nodiscard]] uint32_t getModifiers() const;
    [[nodiscard]] bool isModPressed(uint32_t mod) const;

    // Individual modifier queries
    [[nodiscard]] bool isShiftPressed() const;
    [[nodiscard]] bool isCtrlPressed() const;
    [[nodiscard]] bool isAltPressed() const;
    [[nodiscard]] bool isSuperPressed() const;
    [[nodiscard]] bool isCapsLockActive() const;
    [[nodiscard]] bool isNumLockActive() const;

    // Keysym translation
    [[nodiscard]] xkb_keysym_t translateKeysym(uint32_t keycode) const;

    /// Translate a keysym taking into account current modifiers, returning all syms.
    [[nodiscard]] std::vector<xkb_keysym_t> translateKeysyms(uint32_t keycode) const;

    // Repeat configuration
    void setRepeatRate(int32_t rate);
    void setRepeatDelay(int32_t delay);
    [[nodiscard]] int32_t getRepeatRate() const { return repeatRate_; }
    [[nodiscard]] int32_t getRepeatDelay() const { return repeatDelay_; }

    // Lock key defaults
    void setNumlockByDefault(bool enabled) { numlockByDefault_ = enabled; }
    void setCapslockByDefault(bool enabled) { capslockByDefault_ = enabled; }
    [[nodiscard]] bool getNumlockByDefault() const { return numlockByDefault_; }
    [[nodiscard]] bool getCapslockByDefault() const { return capslockByDefault_; }

    // Key event callback
    using KeyHandler = std::function<void(uint32_t keycode, uint32_t mods, wl_keyboard_key_state state)>;
    void setKeyHandler(KeyHandler handler);

    // Access the underlying wlr keyboard
    [[nodiscard]] wlr_keyboard* getWlrKeyboard() const { return wlrKeyboard_; }

    // Pressed key tracking
    [[nodiscard]] bool isKeyPressed(uint32_t keycode) const;
    [[nodiscard]] const std::vector<uint32_t>& getPressedKeys() const { return pressedKeys_; }

private:
    InputManager* manager_ = nullptr;
    wlr_keyboard* wlrKeyboard_ = nullptr;

    // XKB state
    xkb_context* xkbContext_ = nullptr;
    xkb_keymap* xkbKeymap_ = nullptr;
    xkb_state* xkbState_ = nullptr;

    // Repeat settings
    int32_t repeatRate_ = 25;
    int32_t repeatDelay_ = 600;

    // Layout settings
    std::string layout_ = "us";
    std::string variant_;
    std::string model_;
    std::string options_;
    uint32_t currentLayoutIndex_ = 0;

    // Lock key defaults
    bool numlockByDefault_ = false;
    bool capslockByDefault_ = false;

    // Pressed keys for state tracking
    std::vector<uint32_t> pressedKeys_;

    KeyHandler keyHandler_;

    // Key repeat via wl_event_source timer
    wl_event_source* repeatTimer_ = nullptr;
    uint32_t repeatKeycode_ = 0;
    bool repeatActive_ = false;

    // Wayland listeners
    wl_listener keyListener_ = {};
    wl_listener modifiersListener_ = {};
    wl_listener destroyListener_ = {};

    static void handleKeyEvent(wl_listener* listener, void* data);
    static void handleModifiers(wl_listener* listener, void* data);
    static void handleDestroy(wl_listener* listener, void* data);
    static int handleRepeatTimer(void* data);

    void initXkb();
    void destroyXkb();
    void applyLockDefaults();

    void startRepeat(uint32_t keycode);
    void stopRepeat();
};

} // namespace eternal

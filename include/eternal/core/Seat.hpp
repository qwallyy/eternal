#pragma once
#include <cstdint>
#include <string>

extern "C" {
#include <wayland-server-core.h>
}

namespace eternal {

class Server;
class Surface;

struct wlr_seat;
struct wlr_surface;
struct wlr_data_source;
struct wlr_primary_selection_source;

/// Represents the logical seat (keyboard + pointer + touch focus group).
class Seat {
public:
    explicit Seat(Server& server, wlr_seat* seat);
    ~Seat();

    Seat(const Seat&) = delete;
    Seat& operator=(const Seat&) = delete;
    Seat(Seat&&) = delete;
    Seat& operator=(Seat&&) = delete;

    // ── Focus management ────────────────────────────────────────────────

    /// Set keyboard focus to the given surface (nullptr to clear).
    void setKeyboardFocus(Surface* surface);

    /// Returns the surface that currently holds keyboard focus.
    Surface* getKeyboardFocusedSurface() const { return m_keyboardFocus; }

    /// Set pointer focus to the given surface at local coordinates.
    void setPointerFocus(Surface* surface, double localX, double localY);

    // ── Drag & drop ─────────────────────────────────────────────────────

    /// Initiate a drag operation from the focused surface.
    void startDrag();

    // ── Cursor ──────────────────────────────────────────────────────────

    /// Set the cursor image by XCursor theme name (e.g. "left_ptr").
    void setCursor(const std::string& name);

    // ── Selection (clipboard) ───────────────────────────────────────────

    /// Set the clipboard data source for this seat.
    void setSelection(wlr_data_source* source, uint32_t serial);

    /// Get the current primary selection source (middle-click paste).
    wlr_primary_selection_source* getPrimarySelection() const;

    // ── Input event forwarding ──────────────────────────────────────────

    /// Notify the seat of absolute pointer motion.
    void handlePointerMotion(double x, double y, uint32_t timeMsec);

    /// Notify the seat of a pointer button press / release.
    /// @param button  Linux input event code (e.g. BTN_LEFT).
    /// @param state   WLR_BUTTON_PRESSED or WLR_BUTTON_RELEASED.
    void handlePointerButton(uint32_t button, uint32_t state, uint32_t timeMsec);

    /// Notify the seat of a keyboard key press / release.
    /// @param key     Linux input event code.
    /// @param state   WL_KEYBOARD_KEY_STATE_PRESSED or _RELEASED.
    void handleKeyboardKey(uint32_t key, uint32_t state, uint32_t timeMsec);

    /// Notify the seat that keyboard modifier state has changed.
    void handleKeyboardModifiers();

    // ── Underlying wlroots objects ──────────────────────────────────────

    wlr_seat* getWlrSeat() const { return m_seat; }

    /// Reference back to the owning server.
    Server& getServer() const { return m_server; }

private:
    // ── Listener callbacks ──────────────────────────────────────────────

    static void onRequestSetCursor(struct wl_listener* listener, void* data);
    static void onRequestSetSelection(struct wl_listener* listener, void* data);
    static void onRequestSetPrimarySelection(struct wl_listener* listener, void* data);
    static void onRequestStartDrag(struct wl_listener* listener, void* data);
    static void onStartDrag(struct wl_listener* listener, void* data);
    static void onDestroy(struct wl_listener* listener, void* data);

    // ── Data ────────────────────────────────────────────────────────────

    Server& m_server;
    wlr_seat* m_seat = nullptr;
    Surface* m_keyboardFocus = nullptr;

    // ── wl_listener wrappers ────────────────────────────────────────────

    struct wl_listener m_requestSetCursorListener{};
    struct wl_listener m_requestSetSelectionListener{};
    struct wl_listener m_requestSetPrimarySelectionListener{};
    struct wl_listener m_requestStartDragListener{};
    struct wl_listener m_startDragListener{};
    struct wl_listener m_destroyListener{};
};

} // namespace eternal

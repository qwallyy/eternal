#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

extern "C" {
#include <wayland-server-core.h>
}

struct wlr_session_lock_v1;
struct wlr_session_lock_surface_v1;

namespace eternal {

class Server;
class Output;

// ---------------------------------------------------------------------------
// Lock state
// ---------------------------------------------------------------------------

enum class LockState {
    Unlocked,
    Locking,    // transition: waiting for lock surfaces
    Locked,     // fully locked, lock surfaces visible
    Unlocking,  // transition: unlock handshake in progress
};

/// Per-output lock surface tracking.
struct LockSurface {
    Output* output = nullptr;
    wlr_session_lock_surface_v1* wlrSurface = nullptr;
    bool mapped = false;

    struct wl_listener mapListener{};
    struct wl_listener destroyListener{};
};

// ---------------------------------------------------------------------------
// SessionLock - ext-session-lock-v1
// ---------------------------------------------------------------------------

class SessionLock {
public:
    explicit SessionLock(Server& server);
    ~SessionLock();

    SessionLock(const SessionLock&) = delete;
    SessionLock& operator=(const SessionLock&) = delete;

    /// Initialize the session lock protocol.
    bool init();

    /// Shutdown session lock.
    void shutdown();

    // ── Lock lifecycle ──────────────────────────────────────────────────

    /// Handle a new lock request from a client.
    void onNewLock(wlr_session_lock_v1* lock);

    /// Handle a lock surface being created.
    void onNewLockSurface(wlr_session_lock_surface_v1* surface,
                          Output* output);

    /// Handle the lock being finished (client unlocks).
    void onUnlock();

    /// Handle the lock being destroyed.
    void onLockDestroy();

    // ── State queries ───────────────────────────────────────────────────

    /// Whether the session is currently locked.
    [[nodiscard]] bool isLocked() const {
        return m_state == LockState::Locked || m_state == LockState::Locking;
    }

    /// Get the current lock state.
    [[nodiscard]] LockState getState() const { return m_state; }

    // ── Input handling ──────────────────────────────────────────────────

    /// Check if keyboard input should be intercepted (during lock).
    [[nodiscard]] bool shouldInterceptInput() const { return isLocked(); }

    /// Check if a given action should be blocked during lock.
    [[nodiscard]] bool isActionBlocked(const std::string& action) const;

    // ── Lock surface rendering ──────────────────────────────────────────

    /// Get the lock surface for a given output, or nullptr.
    [[nodiscard]] LockSurface* getLockSurface(Output* output) const;

    /// Whether all outputs have mapped lock surfaces.
    [[nodiscard]] bool allSurfacesMapped() const;

    // ── Callbacks ───────────────────────────────────────────────────────

    using LockCallback = std::function<void(LockState)>;
    void onStateChange(LockCallback callback);

private:
    /// Transition to a new lock state.
    void transitionTo(LockState newState);

    /// Send the lock-locked event to confirm the lock.
    void confirmLock();

    /// Block compositor features during lock.
    void blockFeatures();

    /// Unblock compositor features after unlock.
    void unblockFeatures();

    Server& m_server;
    LockState m_state = LockState::Unlocked;

    wlr_session_lock_v1* m_currentLock = nullptr;

    // Per-output lock surfaces.
    std::vector<std::unique_ptr<LockSurface>> m_lockSurfaces;

    // Blocked actions during lock.
    std::vector<std::string> m_blockedActions;

    // State change callbacks.
    std::vector<LockCallback> m_stateCallbacks;

    // Listeners.
    struct wl_listener m_newLockListener{};
    struct wl_listener m_lockDestroyListener{};
    struct wl_listener m_lockUnlockListener{};
    struct wl_listener m_lockNewSurfaceListener{};

    bool m_initialized = false;
};

} // namespace eternal

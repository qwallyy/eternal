#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

extern "C" {
#include <wayland-server-core.h>
}

struct wlr_idle_notifier_v1;
struct wlr_idle_inhibit_manager_v1;
struct wlr_seat;
struct wlr_surface;

namespace eternal {

class Server;
class Output;

// ---------------------------------------------------------------------------
// Idle state
// ---------------------------------------------------------------------------

enum class IdleState {
    Active,    // user is actively using the system
    Idle,      // idle timeout reached
    Locked,    // screen is locked
    Suspended, // DPMS off / suspended
};

/// Callback invoked on idle state transitions.
using IdleStateCallback = std::function<void(IdleState oldState,
                                              IdleState newState)>;

// ---------------------------------------------------------------------------
// Idle inhibitor tracking
// ---------------------------------------------------------------------------

struct IdleInhibitor {
    struct wlr_surface* surface = nullptr;
    struct wl_listener destroyListener{};
    bool active = true;
};

// ---------------------------------------------------------------------------
// IdleManager - ext-idle-notify-v1 + idle-inhibit
// ---------------------------------------------------------------------------

class IdleManager {
public:
    explicit IdleManager(Server& server);
    ~IdleManager();

    IdleManager(const IdleManager&) = delete;
    IdleManager& operator=(const IdleManager&) = delete;

    /// Initialize idle notification and inhibit protocols.
    bool init();

    /// Shutdown idle management.
    void shutdown();

    // ── Idle timeout ────────────────────────────────────────────────────

    /// Set the idle timeout in seconds.
    void setIdleTimeout(int seconds);

    /// Get the current idle timeout.
    [[nodiscard]] int getIdleTimeout() const { return m_idleTimeoutSec; }

    /// Notify that user input occurred (resets idle timer).
    void onActivity();

    /// Update idle state (call from event loop).
    void update(double nowSec);

    /// Get the current idle state.
    [[nodiscard]] IdleState getState() const { return m_state; }

    /// Time in seconds since last user activity.
    [[nodiscard]] double getIdleDuration(double nowSec) const;

    // ── State transition callbacks ──────────────────────────────────────

    /// Register a callback for idle state changes.
    void onStateChange(IdleStateCallback callback);

    // ── DPMS integration ────────────────────────────────────────────────

    /// Configure the idle->lock->suspend pipeline.
    /// dimTimeout: seconds before dimming (0 to skip).
    /// lockTimeout: seconds after dim before locking (0 to skip).
    /// suspendTimeout: seconds after lock before DPMS off (0 to skip).
    void configurePipeline(int dimTimeout, int lockTimeout, int suspendTimeout);

    // ── Idle inhibit protocol ───────────────────────────────────────────

    /// Check if any idle inhibitor is currently active.
    [[nodiscard]] bool isInhibited() const;

    /// Get the number of active inhibitors.
    [[nodiscard]] size_t getInhibitorCount() const;

    /// Handle a new idle inhibitor from a client.
    void onNewInhibitor(struct wlr_surface* surface);

    /// Handle an inhibitor being destroyed.
    void onInhibitorDestroyed(struct wlr_surface* surface);

private:
    /// Transition to a new idle state.
    void transitionTo(IdleState newState);

    Server& m_server;

    // ext-idle-notify-v1
    wlr_idle_notifier_v1* m_idleNotifier = nullptr;

    // idle-inhibit-unstable-v1
    wlr_idle_inhibit_manager_v1* m_inhibitManager = nullptr;

    // State.
    IdleState m_state = IdleState::Active;
    int m_idleTimeoutSec = 300;  // default 5 minutes
    double m_lastActivityTime = 0.0;

    // Pipeline timeouts.
    int m_dimTimeout = 0;
    int m_lockTimeout = 0;
    int m_suspendTimeout = 0;

    // Active inhibitors.
    std::vector<std::unique_ptr<IdleInhibitor>> m_inhibitors;

    // State change callbacks.
    std::vector<IdleStateCallback> m_stateCallbacks;

    // Listeners.
    struct wl_listener m_newInhibitorListener{};

    bool m_initialized = false;
};

} // namespace eternal

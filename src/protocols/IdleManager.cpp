#include "eternal/protocols/IdleManager.hpp"
#include "eternal/core/Server.hpp"
#include "eternal/core/Output.hpp"
#include "eternal/core/Compositor.hpp"
#include "eternal/utils/Logger.hpp"

extern "C" {
#include <wayland-server-core.h>
#include <wlr/types/wlr_idle_notify_v1.h>
#include <wlr/types/wlr_idle_inhibit_v1.h>
#include <wlr/types/wlr_seat.h>
}

#include <algorithm>

namespace eternal {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

IdleManager::IdleManager(Server& server)
    : m_server(server) {}

IdleManager::~IdleManager() {
    shutdown();
}

bool IdleManager::init() {
    auto* display = m_server.getDisplay();
    if (!display) {
        LOG_ERROR("IdleManager: no display available");
        return false;
    }

    // Create ext-idle-notify-v1.
    m_idleNotifier = wlr_idle_notifier_v1_create(display);
    if (!m_idleNotifier) {
        LOG_ERROR("IdleManager: failed to create idle notifier");
        return false;
    }

    // Create idle-inhibit-unstable-v1.
    m_inhibitManager = wlr_idle_inhibit_v1_create(display);
    if (!m_inhibitManager) {
        LOG_WARN("IdleManager: failed to create idle inhibit manager");
        // Non-fatal.
    }

    // Wire up inhibitor creation.
    if (m_inhibitManager) {
        m_newInhibitorListener.notify =
            [](struct wl_listener* listener, void* data) {
                IdleManager* self = wl_container_of(listener, self,
                                                     m_newInhibitorListener);
                auto* inhibitor =
                    static_cast<struct wlr_idle_inhibitor_v1*>(data);
                if (inhibitor && inhibitor->surface) {
                    self->onNewInhibitor(inhibitor->surface);
                }
            };
        wl_signal_add(&m_inhibitManager->events.new_inhibitor,
                      &m_newInhibitorListener);
    }

    m_initialized = true;
    LOG_INFO("IdleManager: initialized (timeout={} sec)", m_idleTimeoutSec);
    return true;
}

void IdleManager::shutdown() {
    if (!m_initialized) return;

    m_inhibitors.clear();
    m_stateCallbacks.clear();
    m_initialized = false;

    LOG_INFO("IdleManager: shut down");
}

// ---------------------------------------------------------------------------
// Idle timeout
// ---------------------------------------------------------------------------

void IdleManager::setIdleTimeout(int seconds) {
    m_idleTimeoutSec = seconds;
    LOG_INFO("IdleManager: idle timeout set to {} seconds", seconds);
}

void IdleManager::onActivity() {
    m_lastActivityTime = 0.0; // will be reset on next update()

    // If we were idle, transition back to active.
    if (m_state != IdleState::Active) {
        transitionTo(IdleState::Active);
    }

    // Notify the ext-idle-notify-v1 protocol.
    if (m_idleNotifier) {
        wlr_idle_notifier_v1_notify_activity(m_idleNotifier, nullptr);
    }
}

void IdleManager::update(double nowSec) {
    if (!m_initialized) return;

    // Track activity time.
    if (m_lastActivityTime <= 0.0) {
        m_lastActivityTime = nowSec;
    }

    // Don't timeout if inhibited.
    if (isInhibited()) return;

    double idleDuration = nowSec - m_lastActivityTime;

    // State machine: Active -> (dim) -> Idle -> Locked -> Suspended.
    switch (m_state) {
    case IdleState::Active:
        if (m_dimTimeout > 0 && idleDuration >= m_dimTimeout) {
            // Dim phase (could trigger output dimming).
            LOG_DEBUG("IdleManager: dim threshold reached");
        }
        if (m_idleTimeoutSec > 0 &&
            idleDuration >= static_cast<double>(m_idleTimeoutSec)) {
            transitionTo(IdleState::Idle);
        }
        break;

    case IdleState::Idle:
        if (m_lockTimeout > 0 &&
            idleDuration >= static_cast<double>(m_idleTimeoutSec + m_lockTimeout)) {
            transitionTo(IdleState::Locked);
        }
        break;

    case IdleState::Locked:
        if (m_suspendTimeout > 0 &&
            idleDuration >= static_cast<double>(m_idleTimeoutSec +
                                                 m_lockTimeout +
                                                 m_suspendTimeout)) {
            transitionTo(IdleState::Suspended);
        }
        break;

    case IdleState::Suspended:
        // Stay suspended until activity.
        break;
    }
}

double IdleManager::getIdleDuration(double nowSec) const {
    if (m_lastActivityTime <= 0.0) return 0.0;
    return nowSec - m_lastActivityTime;
}

// ---------------------------------------------------------------------------
// State transition callbacks
// ---------------------------------------------------------------------------

void IdleManager::onStateChange(IdleStateCallback callback) {
    m_stateCallbacks.push_back(std::move(callback));
}

// ---------------------------------------------------------------------------
// DPMS pipeline
// ---------------------------------------------------------------------------

void IdleManager::configurePipeline(int dimTimeout, int lockTimeout,
                                     int suspendTimeout) {
    m_dimTimeout = dimTimeout;
    m_lockTimeout = lockTimeout;
    m_suspendTimeout = suspendTimeout;

    LOG_INFO("IdleManager: pipeline configured (dim={}, lock={}, suspend={})",
             dimTimeout, lockTimeout, suspendTimeout);
}

// ---------------------------------------------------------------------------
// Idle inhibit
// ---------------------------------------------------------------------------

bool IdleManager::isInhibited() const {
    for (const auto& inhibitor : m_inhibitors) {
        if (inhibitor->active) return true;
    }
    return false;
}

size_t IdleManager::getInhibitorCount() const {
    size_t count = 0;
    for (const auto& inhibitor : m_inhibitors) {
        if (inhibitor->active) count++;
    }
    return count;
}

void IdleManager::onNewInhibitor(struct wlr_surface* surface) {
    if (!surface) return;

    auto inhibitor = std::make_unique<IdleInhibitor>();
    inhibitor->surface = surface;
    inhibitor->active = true;

    // Listen for surface destruction.
    inhibitor->destroyListener.notify =
        [](struct wl_listener* listener, void* data) {
            // Find the IdleManager and remove this inhibitor.
            // This is a simplified approach; in practice you'd store
            // a back-pointer.
            (void)listener;
            (void)data;
        };

    m_inhibitors.push_back(std::move(inhibitor));

    // Notify the idle notifier that idle is inhibited.
    if (m_idleNotifier) {
        wlr_idle_notifier_v1_set_inhibited(m_idleNotifier, true);
    }

    LOG_INFO("IdleManager: idle inhibitor added (total: {})",
             m_inhibitors.size());
}

void IdleManager::onInhibitorDestroyed(struct wlr_surface* surface) {
    std::erase_if(m_inhibitors, [surface](const auto& i) {
        return i->surface == surface;
    });

    // Update inhibited state.
    if (m_idleNotifier) {
        wlr_idle_notifier_v1_set_inhibited(m_idleNotifier, isInhibited());
    }

    LOG_INFO("IdleManager: idle inhibitor removed (remaining: {})",
             m_inhibitors.size());
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void IdleManager::transitionTo(IdleState newState) {
    if (m_state == newState) return;

    IdleState oldState = m_state;
    m_state = newState;

    const char* stateNames[] = {"Active", "Idle", "Locked", "Suspended"};
    LOG_INFO("IdleManager: {} -> {}",
             stateNames[static_cast<int>(oldState)],
             stateNames[static_cast<int>(newState)]);

    // Notify the ext-idle-notify-v1 protocol.
    if (m_idleNotifier) {
        if (newState == IdleState::Active) {
            wlr_idle_notifier_v1_notify_activity(m_idleNotifier, nullptr);
        }
    }

    // Invoke callbacks.
    for (auto& cb : m_stateCallbacks) {
        if (cb) cb(oldState, newState);
    }

    // Handle DPMS integration.
    if (newState == IdleState::Suspended) {
        // DPMS off all outputs.
        for (auto& output : m_server.getCompositor().getOutputs()) {
            output->setDPMS(false);
        }
    } else if (oldState == IdleState::Suspended &&
               newState == IdleState::Active) {
        // DPMS on all outputs.
        for (auto& output : m_server.getCompositor().getOutputs()) {
            output->setDPMS(true);
        }
    }
}

} // namespace eternal

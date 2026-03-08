#include "eternal/protocols/SessionLock.hpp"
#include "eternal/core/Server.hpp"
#include "eternal/core/Output.hpp"
#include "eternal/core/Compositor.hpp"
#include "eternal/utils/Logger.hpp"

extern "C" {
#include <wayland-server-core.h>
#include <wlr/types/wlr_session_lock_v1.h>
}

#include <algorithm>

namespace eternal {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

SessionLock::SessionLock(Server& server)
    : m_server(server) {
    // Actions blocked during lock.
    m_blockedActions = {
        "workspace",
        "movetoworkspace",
        "overview",
        "togglespecialworkspace",
        "exec",
        "movewindow",
        "resizewindow",
        "togglefloating",
        "fullscreen",
        "killactive",
    };
}

SessionLock::~SessionLock() {
    shutdown();
}

bool SessionLock::init() {
    auto* display = m_server.getDisplay();
    if (!display) {
        LOG_ERROR("SessionLock: no display available");
        return false;
    }

    // Create the ext-session-lock-v1 manager.
    auto* lockManager = wlr_session_lock_manager_v1_create(display);
    if (!lockManager) {
        LOG_ERROR("SessionLock: failed to create session lock manager");
        return false;
    }

    // Listen for new lock requests.
    m_newLockListener.notify =
        [](struct wl_listener* listener, void* data) {
            SessionLock* self = wl_container_of(listener, self,
                                                 m_newLockListener);
            auto* lock = static_cast<wlr_session_lock_v1*>(data);
            self->onNewLock(lock);
        };
    wl_signal_add(&lockManager->events.new_lock, &m_newLockListener);

    m_initialized = true;
    LOG_INFO("SessionLock: initialized");
    return true;
}

void SessionLock::shutdown() {
    if (!m_initialized) return;

    if (isLocked()) {
        onUnlock();
    }

    m_lockSurfaces.clear();
    m_stateCallbacks.clear();
    m_initialized = false;

    LOG_INFO("SessionLock: shut down");
}

// ---------------------------------------------------------------------------
// Lock lifecycle
// ---------------------------------------------------------------------------

void SessionLock::onNewLock(wlr_session_lock_v1* lock) {
    if (!lock) return;

    if (isLocked()) {
        // Already locked; reject the new lock.
        wlr_session_lock_v1_destroy(lock);
        LOG_WARN("SessionLock: rejected new lock (already locked)");
        return;
    }

    m_currentLock = lock;
    transitionTo(LockState::Locking);

    // Wire up lock events.
    m_lockDestroyListener.notify =
        [](struct wl_listener* listener, void* data) {
            SessionLock* self = wl_container_of(listener, self,
                                                 m_lockDestroyListener);
            self->onLockDestroy();
            (void)data;
        };
    wl_signal_add(&lock->events.destroy, &m_lockDestroyListener);

    m_lockUnlockListener.notify =
        [](struct wl_listener* listener, void* data) {
            SessionLock* self = wl_container_of(listener, self,
                                                 m_lockUnlockListener);
            self->onUnlock();
            (void)data;
        };
    wl_signal_add(&lock->events.unlock, &m_lockUnlockListener);

    m_lockNewSurfaceListener.notify =
        [](struct wl_listener* listener, void* data) {
            SessionLock* self = wl_container_of(listener, self,
                                                 m_lockNewSurfaceListener);
            auto* surface =
                static_cast<wlr_session_lock_surface_v1*>(data);
            // Determine which output this surface is for.
            // In wlroots, the surface has an output field.
            Output* output = nullptr;
            for (auto& o : self->m_server.getCompositor().getOutputs()) {
                if (o->getWlrOutput() == surface->output) {
                    output = o.get();
                    break;
                }
            }
            self->onNewLockSurface(surface, output);
        };
    wl_signal_add(&lock->events.new_surface, &m_lockNewSurfaceListener);

    // Block compositor features.
    blockFeatures();

    LOG_INFO("SessionLock: new lock request accepted");
}

void SessionLock::onNewLockSurface(wlr_session_lock_surface_v1* surface,
                                    Output* output) {
    if (!surface || !output) return;

    auto lockSurface = std::make_unique<LockSurface>();
    lockSurface->output = output;
    lockSurface->wlrSurface = surface;
    lockSurface->mapped = false;

    // Listen for map/destroy.
    lockSurface->mapListener.notify =
        [](struct wl_listener* listener, void* data) {
            LockSurface* ls = wl_container_of(listener, ls, mapListener);
            ls->mapped = true;
            (void)data;
        };
    wl_signal_add(&surface->surface->events.map, &lockSurface->mapListener);

    lockSurface->destroyListener.notify =
        [](struct wl_listener* listener, void* data) {
            LockSurface* ls = wl_container_of(listener, ls, destroyListener);
            ls->mapped = false;
            (void)data;
        };
    wl_signal_add(&surface->events.destroy, &lockSurface->destroyListener);

    // Configure the lock surface with the output dimensions.
    const auto& box = output->getBox();
    wlr_session_lock_surface_v1_configure(surface, box.width, box.height);

    m_lockSurfaces.push_back(std::move(lockSurface));

    LOG_INFO("SessionLock: lock surface created for output '{}'",
             output->getName());

    // Check if all outputs have lock surfaces.
    if (allSurfacesMapped()) {
        confirmLock();
    }
}

void SessionLock::onUnlock() {
    if (m_state == LockState::Unlocked) return;

    transitionTo(LockState::Unlocking);

    // Clean up lock surfaces.
    m_lockSurfaces.clear();

    // Unblock features.
    unblockFeatures();

    m_currentLock = nullptr;
    transitionTo(LockState::Unlocked);

    LOG_INFO("SessionLock: session unlocked");
}

void SessionLock::onLockDestroy() {
    if (m_currentLock) {
        wl_list_remove(&m_lockDestroyListener.link);
        wl_list_remove(&m_lockUnlockListener.link);
        wl_list_remove(&m_lockNewSurfaceListener.link);
    }

    if (isLocked()) {
        // Lock client crashed while locked: this is a security concern.
        // Keep the session locked with black surfaces.
        LOG_WARN("SessionLock: lock client destroyed while locked!");
        m_currentLock = nullptr;
        // Don't transition to unlocked -- keep locked state.
    } else {
        m_currentLock = nullptr;
    }
}

// ---------------------------------------------------------------------------
// State queries
// ---------------------------------------------------------------------------

bool SessionLock::isActionBlocked(const std::string& action) const {
    if (!isLocked()) return false;

    return std::find(m_blockedActions.begin(), m_blockedActions.end(),
                     action) != m_blockedActions.end();
}

LockSurface* SessionLock::getLockSurface(Output* output) const {
    for (const auto& ls : m_lockSurfaces) {
        if (ls->output == output) return ls.get();
    }
    return nullptr;
}

bool SessionLock::allSurfacesMapped() const {
    auto& outputs = m_server.getCompositor().getOutputs();
    if (outputs.empty()) return false;

    for (auto& output : outputs) {
        auto* ls = getLockSurface(output.get());
        if (!ls || !ls->mapped) return false;
    }
    return true;
}

void SessionLock::onStateChange(LockCallback callback) {
    m_stateCallbacks.push_back(std::move(callback));
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void SessionLock::transitionTo(LockState newState) {
    if (m_state == newState) return;

    LockState old = m_state;
    m_state = newState;

    const char* names[] = {"Unlocked", "Locking", "Locked", "Unlocking"};
    LOG_INFO("SessionLock: {} -> {}",
             names[static_cast<int>(old)],
             names[static_cast<int>(newState)]);

    for (auto& cb : m_stateCallbacks) {
        if (cb) cb(newState);
    }
}

void SessionLock::confirmLock() {
    if (!m_currentLock) return;

    // Send the locked event to the lock client.
    wlr_session_lock_v1_send_locked(m_currentLock);
    transitionTo(LockState::Locked);

    LOG_INFO("SessionLock: lock confirmed (all surfaces mapped)");
}

void SessionLock::blockFeatures() {
    // In a full implementation, this would prevent:
    // - Workspace switching
    // - Window overview
    // - Application launching
    // - Window manipulation
    // The blocked actions list is checked by dispatchers.

    LOG_DEBUG("SessionLock: compositor features blocked");
}

void SessionLock::unblockFeatures() {
    LOG_DEBUG("SessionLock: compositor features unblocked");
}

} // namespace eternal

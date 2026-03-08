#include "eternal/animation/AnimationEngine.hpp"
#include "eternal/core/Surface.hpp"
#include "eternal/core/Output.hpp"
#include "eternal/core/Server.hpp"
#include "eternal/core/Compositor.hpp"
#include "eternal/utils/Logger.hpp"

extern "C" {
#include <wayland-server-core.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/types/wlr_xdg_decoration_v1.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_seat.h>
}

#include <algorithm>
#include <cassert>
#include <cstring>

namespace eternal {

// ===========================================================================
// Popup implementation
// ===========================================================================

Popup::Popup(Server& server, struct wlr_xdg_popup* popup, Surface* parent)
    : m_server(server), m_popup(popup), m_parent(parent)
{
    assert(popup);
    assert(parent);

    m_mapListener.notify = onMap;
    wl_signal_add(&popup->base->surface->events.map, &m_mapListener);

    m_unmapListener.notify = onUnmap;
    wl_signal_add(&popup->base->surface->events.unmap, &m_unmapListener);

    m_destroyListener.notify = onDestroy;
    wl_signal_add(&popup->events.destroy, &m_destroyListener);

    m_newPopupListener.notify = onNewPopup;
    wl_signal_add(&popup->base->events.new_popup, &m_newPopupListener);

    m_repositionListener.notify = onReposition;
    wl_signal_add(&popup->events.reposition, &m_repositionListener);

    LOG_DEBUG("Popup created for surface (app_id='{}')", parent->getAppId());
}

Popup::~Popup() {
    wl_list_remove(&m_mapListener.link);
    wl_list_remove(&m_unmapListener.link);
    wl_list_remove(&m_destroyListener.link);
    wl_list_remove(&m_newPopupListener.link);
    wl_list_remove(&m_repositionListener.link);
}

void Popup::getPosition(int& outX, int& outY) const {
    if (!m_popup) {
        outX = 0;
        outY = 0;
        return;
    }

    // The popup geometry is relative to the parent xdg_surface.
    outX = m_popup->current.geometry.x;
    outY = m_popup->current.geometry.y;
}

void Popup::reposition() {
    // Unconstrain the popup: ensure it fits within the output bounds.
    if (!m_popup || !m_parent) return;

    Output* output = m_parent->getOutput();
    if (!output) return;

    const auto& outBox = output->getBox();
    const auto& parentGeo = m_parent->getGeometry();

    // Build a constraint box in the parent surface's coordinate space.
    struct wlr_box constraint = {
        .x      = outBox.x - parentGeo.x,
        .y      = outBox.y - parentGeo.y,
        .width  = outBox.width,
        .height = outBox.height,
    };

    wlr_xdg_popup_unconstrain_from_box(m_popup, &constraint);
}

void Popup::dismiss() {
    if (!m_popup) return;

    // Dismiss child popups first (bottom-up).
    for (auto& child : m_childPopups) {
        child->dismiss();
    }

    wlr_xdg_popup_destroy(m_popup);
}

struct wlr_surface* Popup::getWlrSurface() const {
    if (!m_popup || !m_popup->base) return nullptr;
    return m_popup->base->surface;
}

void Popup::forEachPopup(const IterFn& fn) {
    fn(this);
    for (auto& child : m_childPopups) {
        child->forEachPopup(fn);
    }
}

void Popup::onMap(struct wl_listener* listener, void* data) {
    Popup* self = wl_container_of(listener, self, m_mapListener);
    (void)data;

    self->m_mapped = true;
    self->reposition();

    // Damage the popup area on the parent's output.
    if (self->m_parent && self->m_parent->getOutput()) {
        int px, py;
        self->getPosition(px, py);
        const auto& parentGeo = self->m_parent->getGeometry();

        struct wlr_box popGeo;
        wlr_xdg_surface_get_geometry(self->m_popup->base, &popGeo);

        self->m_parent->getOutput()->addDamageBox(
            parentGeo.x + px, parentGeo.y + py,
            popGeo.width, popGeo.height);
    }

    LOG_DEBUG("Popup mapped");
}

void Popup::onUnmap(struct wl_listener* listener, void* data) {
    Popup* self = wl_container_of(listener, self, m_unmapListener);
    (void)data;

    // Damage the area the popup was occupying.
    if (self->m_mapped && self->m_parent && self->m_parent->getOutput()) {
        int px, py;
        self->getPosition(px, py);
        const auto& parentGeo = self->m_parent->getGeometry();

        struct wlr_box popGeo;
        wlr_xdg_surface_get_geometry(self->m_popup->base, &popGeo);

        self->m_parent->getOutput()->addDamageBox(
            parentGeo.x + px, parentGeo.y + py,
            popGeo.width, popGeo.height);
    }

    self->m_mapped = false;
    LOG_DEBUG("Popup unmapped");
}

void Popup::onDestroy(struct wl_listener* listener, void* data) {
    Popup* self = wl_container_of(listener, self, m_destroyListener);
    (void)data;

    LOG_DEBUG("Popup destroyed");

    // Remove ourselves from the parent surface's popup list.
    // The parent owns us via unique_ptr, so we find and erase.
    auto& parentPopups = self->m_parent->m_popups;
    parentPopups.erase(
        std::remove_if(parentPopups.begin(), parentPopups.end(),
            [self](const auto& p) { return p.get() == self; }),
        parentPopups.end());
    // Note: `self` is now deleted.
}

void Popup::onNewPopup(struct wl_listener* listener, void* data) {
    Popup* self = wl_container_of(listener, self, m_newPopupListener);
    auto* xdg_popup = static_cast<struct wlr_xdg_popup*>(data);

    auto child = std::make_unique<Popup>(self->m_server, xdg_popup, self->m_parent);
    self->m_childPopups.push_back(std::move(child));
}

void Popup::onReposition(struct wl_listener* listener, void* data) {
    Popup* self = wl_container_of(listener, self, m_repositionListener);
    (void)data;

    // Damage old position, reposition, damage new position.
    if (self->m_parent && self->m_parent->getOutput() && self->m_mapped) {
        int px, py;
        self->getPosition(px, py);
        const auto& parentGeo = self->m_parent->getGeometry();

        struct wlr_box popGeo;
        wlr_xdg_surface_get_geometry(self->m_popup->base, &popGeo);

        // Damage old.
        self->m_parent->getOutput()->addDamageBox(
            parentGeo.x + px, parentGeo.y + py,
            popGeo.width, popGeo.height);
    }

    self->reposition();

    if (self->m_parent && self->m_parent->getOutput() && self->m_mapped) {
        int px, py;
        self->getPosition(px, py);
        const auto& parentGeo = self->m_parent->getGeometry();

        struct wlr_box popGeo;
        wlr_xdg_surface_get_geometry(self->m_popup->base, &popGeo);

        // Damage new.
        self->m_parent->getOutput()->addDamageBox(
            parentGeo.x + px, parentGeo.y + py,
            popGeo.width, popGeo.height);
    }
}

// ===========================================================================
// SubsurfaceNode implementation
// ===========================================================================

SubsurfaceNode::SubsurfaceNode(Server& server, struct wlr_subsurface* subsurface,
                               Surface* parentSurface)
    : m_server(server), m_subsurface(subsurface), m_parentSurface(parentSurface)
{
    assert(subsurface);
    assert(parentSurface);

    m_mapListener.notify = onMap;
    wl_signal_add(&subsurface->surface->events.map, &m_mapListener);

    m_unmapListener.notify = onUnmap;
    wl_signal_add(&subsurface->surface->events.unmap, &m_unmapListener);

    m_destroyListener.notify = onDestroy;
    wl_signal_add(&subsurface->events.destroy, &m_destroyListener);

    m_commitListener.notify = onCommit;
    wl_signal_add(&subsurface->surface->events.commit, &m_commitListener);

    m_newSubsurfaceListener.notify = onNewSubsurface;
    wl_signal_add(&subsurface->surface->events.new_subsurface,
                  &m_newSubsurfaceListener);

    // If the subsurface is already mapped (can happen with wl_subsurface),
    // mark it as such.
    m_mapped = subsurface->surface->mapped;

    LOG_DEBUG("Subsurface created for surface (app_id='{}')",
              parentSurface->getAppId());
}

SubsurfaceNode::~SubsurfaceNode() {
    wl_list_remove(&m_mapListener.link);
    wl_list_remove(&m_unmapListener.link);
    wl_list_remove(&m_destroyListener.link);
    wl_list_remove(&m_commitListener.link);
    wl_list_remove(&m_newSubsurfaceListener.link);
}

void SubsurfaceNode::getPosition(int& outX, int& outY) const {
    if (!m_subsurface) {
        outX = 0;
        outY = 0;
        return;
    }

    outX = m_subsurface->current.x;
    outY = m_subsurface->current.y;
}

bool SubsurfaceNode::isSynced() const {
    if (!m_subsurface) return true;
    return m_subsurface->synchronized;
}

void SubsurfaceNode::forEachSubsurface(const IterFn& fn) {
    fn(this);
    for (auto& child : m_childSubsurfaces) {
        child->forEachSubsurface(fn);
    }
}

void SubsurfaceNode::onMap(struct wl_listener* listener, void* data) {
    SubsurfaceNode* self = wl_container_of(listener, self, m_mapListener);
    (void)data;

    self->m_mapped = true;

    // Damage the subsurface area.
    Output* output = self->m_parentSurface->getOutput();
    if (output) {
        int sx, sy;
        self->getPosition(sx, sy);
        const auto& parentGeo = self->m_parentSurface->getGeometry();

        output->addDamageBox(
            parentGeo.x + sx, parentGeo.y + sy,
            self->m_subsurface->surface->current.width,
            self->m_subsurface->surface->current.height);
    }

    LOG_DEBUG("Subsurface mapped");
}

void SubsurfaceNode::onUnmap(struct wl_listener* listener, void* data) {
    SubsurfaceNode* self = wl_container_of(listener, self, m_unmapListener);
    (void)data;

    // Damage the area the subsurface was occupying.
    Output* output = self->m_parentSurface->getOutput();
    if (output && self->m_mapped) {
        int sx, sy;
        self->getPosition(sx, sy);
        const auto& parentGeo = self->m_parentSurface->getGeometry();

        output->addDamageBox(
            parentGeo.x + sx, parentGeo.y + sy,
            self->m_subsurface->surface->current.width,
            self->m_subsurface->surface->current.height);
    }

    self->m_mapped = false;
    LOG_DEBUG("Subsurface unmapped");
}

void SubsurfaceNode::onDestroy(struct wl_listener* listener, void* data) {
    SubsurfaceNode* self = wl_container_of(listener, self, m_destroyListener);
    (void)data;

    LOG_DEBUG("Subsurface destroyed");

    // Remove from the parent surface's subsurface list.
    auto& parentSubs = self->m_parentSurface->m_subsurfaces;
    parentSubs.erase(
        std::remove_if(parentSubs.begin(), parentSubs.end(),
            [self](const auto& p) { return p.get() == self; }),
        parentSubs.end());
}

void SubsurfaceNode::onCommit(struct wl_listener* listener, void* data) {
    SubsurfaceNode* self = wl_container_of(listener, self, m_commitListener);
    (void)data;

    if (!self->m_mapped) return;

    // Damage the subsurface area on commit (content changed).
    Output* output = self->m_parentSurface->getOutput();
    if (output) {
        int sx, sy;
        self->getPosition(sx, sy);
        const auto& parentGeo = self->m_parentSurface->getGeometry();

        output->addDamageBox(
            parentGeo.x + sx, parentGeo.y + sy,
            self->m_subsurface->surface->current.width,
            self->m_subsurface->surface->current.height);
    }
}

void SubsurfaceNode::onNewSubsurface(struct wl_listener* listener, void* data) {
    SubsurfaceNode* self = wl_container_of(listener, self, m_newSubsurfaceListener);
    auto* sub = static_cast<struct wlr_subsurface*>(data);

    auto child = std::make_unique<SubsurfaceNode>(
        self->m_server, sub, self->m_parentSurface);
    self->m_childSubsurfaces.push_back(std::move(child));
}

// ===========================================================================
// Surface implementation
// ===========================================================================

Surface::Surface(Server& server, wlr_xdg_toplevel* toplevel)
    : m_server(server), m_toplevel(toplevel)
{
    assert(toplevel);
    m_xdgSurface = toplevel->base;

    // Cache initial identification.
    if (toplevel->app_id) {
        m_appId = toplevel->app_id;
    }
    if (toplevel->title) {
        m_title = toplevel->title;
    }

    // Surface map/unmap events.
    m_mapListener.notify = onMap;
    wl_signal_add(&m_xdgSurface->surface->events.map, &m_mapListener);

    m_unmapListener.notify = onUnmap;
    wl_signal_add(&m_xdgSurface->surface->events.unmap, &m_unmapListener);

    // Toplevel destroy.
    m_destroyListener.notify = onDestroy;
    wl_signal_add(&toplevel->events.destroy, &m_destroyListener);

    // Surface commit (for damage tracking and state updates).
    m_commitListener.notify = onCommit;
    wl_signal_add(&m_xdgSurface->surface->events.commit, &m_commitListener);

    // Toplevel requests.
    m_requestFullscreenListener.notify = onRequestFullscreen;
    wl_signal_add(&toplevel->events.request_fullscreen,
                  &m_requestFullscreenListener);

    m_requestMaximizeListener.notify = onRequestMaximize;
    wl_signal_add(&toplevel->events.request_maximize,
                  &m_requestMaximizeListener);

    m_requestMoveListener.notify = onRequestMove;
    wl_signal_add(&toplevel->events.request_move, &m_requestMoveListener);

    m_requestResizeListener.notify = onRequestResize;
    wl_signal_add(&toplevel->events.request_resize, &m_requestResizeListener);

    // Title / app_id changes.
    m_setTitleListener.notify = onSetTitle;
    wl_signal_add(&toplevel->events.set_title, &m_setTitleListener);

    m_setAppIdListener.notify = onSetAppId;
    wl_signal_add(&toplevel->events.set_app_id, &m_setAppIdListener);

    // New popup surfaces under this toplevel.
    m_newPopupListener.notify = onNewPopup;
    wl_signal_add(&m_xdgSurface->events.new_popup, &m_newPopupListener);

    // New subsurfaces.
    m_newSubsurfaceListener.notify = onNewSubsurface;
    wl_signal_add(&m_xdgSurface->surface->events.new_subsurface,
                  &m_newSubsurfaceListener);

    // Ack configure from client.
    m_ackConfigureListener.notify = onAckConfigure;
    wl_signal_add(&m_xdgSurface->events.ack_configure,
                  &m_ackConfigureListener);

    // Scan for any existing subsurfaces that were created before we
    // registered our listener (can happen with protocol race conditions).
    struct wlr_subsurface* existing_sub;
    wl_list_for_each(existing_sub,
                     &m_xdgSurface->surface->current.subsurfaces_below,
                     current.link)
    {
        addSubsurface(existing_sub);
    }
    wl_list_for_each(existing_sub,
                     &m_xdgSurface->surface->current.subsurfaces_above,
                     current.link)
    {
        addSubsurface(existing_sub);
    }

    LOG_DEBUG("Surface created (app_id='{}', title='{}')", m_appId, m_title);
}

Surface::~Surface() {
    wl_list_remove(&m_mapListener.link);
    wl_list_remove(&m_unmapListener.link);
    wl_list_remove(&m_destroyListener.link);
    wl_list_remove(&m_commitListener.link);
    wl_list_remove(&m_requestFullscreenListener.link);
    wl_list_remove(&m_requestMaximizeListener.link);
    wl_list_remove(&m_requestMoveListener.link);
    wl_list_remove(&m_requestResizeListener.link);
    wl_list_remove(&m_setTitleListener.link);
    wl_list_remove(&m_setAppIdListener.link);
    wl_list_remove(&m_newPopupListener.link);
    wl_list_remove(&m_newSubsurfaceListener.link);
    wl_list_remove(&m_ackConfigureListener.link);

    // Clean up popups and subsurfaces.
    m_popups.clear();
    m_subsurfaces.clear();

    LOG_DEBUG("Surface destroyed (app_id='{}')", m_appId);
}

// ---------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------

void Surface::focus() {
    if (!m_toplevel || !m_mapped) return;

    m_server.getCompositor().setFocusedSurface(this);
}

void Surface::close() {
    if (m_toplevel) {
        wlr_xdg_toplevel_send_close(m_toplevel);
    }
}

void Surface::move(int x, int y) {
    if (m_server.getAnimationEngine().isEnabled() && m_mapped) {
        m_server.getAnimationEngine().animateGeometry(
            this, x, y, m_geometry.width, m_geometry.height);
        m_server.scheduleAnimationTick();
        return;
    }

    int oldX = m_geometry.x;
    int oldY = m_geometry.y;

    m_geometry.x = x;
    m_geometry.y = y;

    // Damage both old and new positions on the output.
    if (m_output) {
        m_output->damageSurfaceMove(this, oldX, oldY);
    }
}

void Surface::resize(int width, int height) {
    if (!m_toplevel) return;

    // Damage current position before the resize.
    damageOutput();

    // Store the desired size in pending state and configure the client.
    m_pendingState.width  = width;
    m_pendingState.height = height;

    wlr_xdg_toplevel_set_size(m_toplevel, width, height);
    sendConfigure();
}

void Surface::setFullscreen(bool fullscreen) {
    if (!m_toplevel) return;
    if (m_fullscreen == fullscreen) return;

    m_pendingState.fullscreen = fullscreen;
    wlr_xdg_toplevel_set_fullscreen(m_toplevel, fullscreen);
    sendConfigure();
}

void Surface::setMaximized(bool maximized) {
    if (!m_toplevel) return;
    if (m_maximized == maximized) return;

    m_pendingState.maximized = maximized;
    wlr_xdg_toplevel_set_maximized(m_toplevel, maximized);
    sendConfigure();
}

void Surface::setFloating(bool floating) {
    m_floating = floating;
}

void Surface::toggleFullscreen() {
    if (m_fullscreen) {
        // Restore pre-fullscreen geometry.
        setFullscreen(false);
        restoreGeometry();
    } else {
        // Save current geometry and go fullscreen.
        saveGeometry();
        setFullscreen(true);

        // Fill the entire output area (bypass decorations).
        if (m_output) {
            auto& ob = m_output->getBox();
            move(ob.x, ob.y);
            resize(ob.width, ob.height);
        }
    }
}

void Surface::fullscreenOnOutput(Output* targetOutput) {
    if (!targetOutput) return;

    saveGeometry();
    setOutput(targetOutput);
    setFullscreen(true);

    auto& ob = targetOutput->getBox();
    move(ob.x, ob.y);
    resize(ob.width, ob.height);
}

void Surface::toggleMaximized() {
    if (m_maximized) {
        setMaximized(false);
        restoreGeometry();
    } else {
        saveGeometry();
        setMaximized(true);

        // Fill usable area (respecting bars/panels).
        // In production, the compositor would query the LayerShellManager
        // for the usable area.  For now, use the full output area minus
        // a reasonable margin for panels.
        if (m_output) {
            auto& ob = m_output->getBox();
            // The actual usable area would come from LayerShellManager::arrangeOutput.
            move(ob.x, ob.y);
            resize(ob.width, ob.height);
        }
    }
}

void Surface::saveGeometry() {
    m_savedGeometry = m_geometry;
    m_hasSavedGeometry = true;
}

void Surface::restoreGeometry() {
    if (!m_hasSavedGeometry) return;
    move(m_savedGeometry.x, m_savedGeometry.y);
    resize(m_savedGeometry.width, m_savedGeometry.height);
    m_hasSavedGeometry = false;
}

void Surface::setOpacity(float opacity) {
    if (opacity < 0.0f) opacity = 0.0f;
    if (opacity > 1.0f) opacity = 1.0f;
    m_opacity = opacity;

    // Opacity change requires a repaint.
    damageOutput();
}

void Surface::setActivated(bool activated) {
    if (!m_toplevel) return;
    m_activated = activated;

    wlr_xdg_toplevel_set_activated(m_toplevel, activated);

    // Activated state change should trigger a repaint for decoration updates.
    damageOutput();
}

void Surface::toFront() {
    // Reorder in the compositor's surface list.
    // Implementation depends on the compositor's stacking model.
    damageOutput();
}

void Surface::toBack() {
    damageOutput();
}

void Surface::pin() {
    m_pinned = true;
}

void Surface::unpin() {
    m_pinned = false;
}

// ---------------------------------------------------------------------------
// Configure cycle
// ---------------------------------------------------------------------------

uint32_t Surface::sendConfigure() {
    if (!m_xdgSurface) return 0;

    // The xdg_surface protocol handles configure serial tracking internally.
    // wlr_xdg_toplevel_set_* functions queue state; calling
    // wlr_xdg_surface_schedule_configure sends the configure event.
    uint32_t serial = wlr_xdg_surface_schedule_configure(m_xdgSurface);
    m_pendingConfigureSerial = serial;

    LOG_DEBUG("Surface '{}' configure sent (serial={})", m_appId, serial);
    return serial;
}

// ---------------------------------------------------------------------------
// Output association
// ---------------------------------------------------------------------------

void Surface::setOutput(Output* output) {
    if (m_output == output) return;

    // Damage old output.
    if (m_output && m_mapped) {
        m_output->damageSurface(this);
    }

    m_output = output;

    // Damage new output.
    if (m_output && m_mapped) {
        m_output->damageSurface(this);
    }
}

// ---------------------------------------------------------------------------
// Decoration mode
// ---------------------------------------------------------------------------

void Surface::setDecorationMode(DecorationMode mode) {
    if (m_decorationMode == mode) return;
    m_decorationMode = mode;

    // Decoration mode change may alter the geometry (server-side decorations
    // add a title bar). Damage the surface area.
    damageOutput();
}

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

void Surface::setGeometry(const SurfaceBox& box) {
    setGeometry(box.x, box.y, box.width, box.height);
}

void Surface::setGeometry(int x, int y, int width, int height) {
    const int old_width = m_geometry.width;
    const int old_height = m_geometry.height;

    // Damage old area.
    damageOutput();

    m_geometry.x = x;
    m_geometry.y = y;
    m_geometry.width  = width;
    m_geometry.height = height;

    // If the size changed, configure the client.
    if (m_toplevel && (width != old_width || height != old_height)) {
        m_pendingState.width  = width;
        m_pendingState.height = height;
        wlr_xdg_toplevel_set_size(m_toplevel, width, height);
        sendConfigure();
    }

    // Damage new area.
    damageOutput();
}

SurfaceBox Surface::getXdgGeometry() const {
    if (!m_xdgSurface) return {};

    struct wlr_box geo;
    wlr_xdg_surface_get_geometry(m_xdgSurface, &geo);
    return {geo.x, geo.y, geo.width, geo.height};
}

// ---------------------------------------------------------------------------
// Surface tree iteration
// ---------------------------------------------------------------------------

struct wlr_surface* Surface::getWlrSurface() const {
    if (!m_xdgSurface) return nullptr;
    return m_xdgSurface->surface;
}

void Surface::forEachRenderSurface(const SurfaceIterFn& fn) {
    if (!m_xdgSurface || !m_xdgSurface->surface) return;

    // wlr_xdg_surface_for_each_surface iterates the xdg_surface,
    // its subsurfaces, and popup surfaces in the correct stacking order.
    struct IterData {
        const SurfaceIterFn* fn;
    };
    IterData data{&fn};

    wlr_xdg_surface_for_each_surface(m_xdgSurface,
        [](struct wlr_surface* surface, int sx, int sy, void* udata) {
            auto* d = static_cast<IterData*>(udata);
            (*d->fn)(surface, sx, sy);
        },
        &data);
}

void Surface::forEachSubsurface(const SurfaceIterFn& fn) {
    if (!m_xdgSurface || !m_xdgSurface->surface) return;

    struct IterData {
        const SurfaceIterFn* fn;
    };
    IterData data{&fn};

    wlr_surface_for_each_surface(m_xdgSurface->surface,
        [](struct wlr_surface* surface, int sx, int sy, void* udata) {
            auto* d = static_cast<IterData*>(udata);
            (*d->fn)(surface, sx, sy);
        },
        &data);
}

void Surface::forEachPopup(const SurfaceIterFn& fn) {
    if (!m_xdgSurface) return;

    struct IterData {
        const SurfaceIterFn* fn;
    };
    IterData data{&fn};

    wlr_xdg_surface_for_each_popup_surface(m_xdgSurface,
        [](struct wlr_surface* surface, int sx, int sy, void* udata) {
            auto* d = static_cast<IterData*>(udata);
            (*d->fn)(surface, sx, sy);
        },
        &data);
}

// ---------------------------------------------------------------------------
// Popup management
// ---------------------------------------------------------------------------

void Surface::dismissAllPopups() {
    // Dismiss in reverse order (newest first).
    while (!m_popups.empty()) {
        m_popups.back()->dismiss();
    }
}

void Surface::addPopup(struct wlr_xdg_popup* popup) {
    auto p = std::make_unique<Popup>(m_server, popup, this);
    m_popups.push_back(std::move(p));
}

void Surface::addSubsurface(struct wlr_subsurface* subsurface) {
    // Check if we already track this subsurface.
    for (auto& existing : m_subsurfaces) {
        if (existing->getWlrSubsurface() == subsurface) return;
    }

    auto node = std::make_unique<SubsurfaceNode>(m_server, subsurface, this);
    m_subsurfaces.push_back(std::move(node));
}

// ---------------------------------------------------------------------------
// Damage helpers
// ---------------------------------------------------------------------------

void Surface::damageOutput() {
    if (!m_output || !m_mapped) return;
    m_output->damageSurface(this);
}

// ---------------------------------------------------------------------------
// Listener callbacks
// ---------------------------------------------------------------------------

void Surface::onMap(struct wl_listener* listener, void* data) {
    Surface* self = wl_container_of(listener, self, m_mapListener);
    (void)data;

    self->m_mapped = true;

    // Update geometry from the xdg_surface.
    struct wlr_box geo;
    wlr_xdg_surface_get_geometry(self->m_xdgSurface, &geo);
    self->m_geometry.width  = geo.width;
    self->m_geometry.height = geo.height;

    // Damage the output so the new surface is rendered.
    if (self->m_output) {
        self->m_output->damageSurface(self);
    }

    // If no output is assigned yet, try to find one based on position.
    if (!self->m_output) {
        auto& compositor = self->m_server.getCompositor();
        Output* out = compositor.getOutputAt(
            self->m_geometry.x + self->m_geometry.width / 2.0,
            self->m_geometry.y + self->m_geometry.height / 2.0);
        if (out) {
            self->m_output = out;
            out->damageSurface(self);
        }
    }

    self->m_server.getCompositor().setFocusedSurface(self);

    if (self->m_server.getAnimationEngine().isEnabled()) {
        self->m_server.getAnimationEngine().animateWindowOpen(
            self,
            self->m_geometry.x,
            self->m_geometry.y,
            self->m_geometry.width,
            self->m_geometry.height);
        self->m_server.scheduleAnimationTick();
    }

    LOG_DEBUG("Surface '{}' mapped ({}x{} at {},{})",
              self->m_appId,
              self->m_geometry.width, self->m_geometry.height,
              self->m_geometry.x, self->m_geometry.y);
}

void Surface::onUnmap(struct wl_listener* listener, void* data) {
    Surface* self = wl_container_of(listener, self, m_unmapListener);
    (void)data;

    // Damage the area where the surface was, so it gets cleared.
    if (self->m_output) {
        self->m_output->damageSurface(self);
    }

    self->m_mapped = false;

    // Dismiss all popups when the toplevel unmaps.
    self->dismissAllPopups();

    // If this surface had keyboard focus, clear it.
    auto& compositor = self->m_server.getCompositor();
    if (compositor.getFocusedSurface() == self) {
        compositor.setFocusedSurface(nullptr);
    }

    LOG_DEBUG("Surface '{}' unmapped", self->m_appId);
}

void Surface::onDestroy(struct wl_listener* listener, void* data) {
    Surface* self = wl_container_of(listener, self, m_destroyListener);
    (void)data;

    LOG_DEBUG("Surface '{}' destroy requested", self->m_appId);

    // Let the compositor handle cleanup.
    self->m_server.getCompositor().destroySurface(self);
}

void Surface::onCommit(struct wl_listener* listener, void* data) {
    Surface* self = wl_container_of(listener, self, m_commitListener);
    (void)data;

    if (!self->m_mapped) return;

    // Check if the surface geometry changed (e.g. client-side resize
    // in response to our configure).
    struct wlr_box geo;
    wlr_xdg_surface_get_geometry(self->m_xdgSurface, &geo);

    bool size_changed = (geo.width != self->m_geometry.width ||
                         geo.height != self->m_geometry.height);

    if (size_changed) {
        // Damage old area.
        self->damageOutput();

        self->m_geometry.width  = geo.width;
        self->m_geometry.height = geo.height;

        // Damage new area.
        self->damageOutput();
    } else {
        // Content changed, damage the surface area.
        self->damageOutput();
    }
}

void Surface::onRequestFullscreen(struct wl_listener* listener, void* data) {
    Surface* self = wl_container_of(listener, self, m_requestFullscreenListener);
    (void)data;

    bool want_fullscreen = !self->m_fullscreen;

    if (self->m_output) {
        if (want_fullscreen) {
            // Save the current geometry for restoration.
            // Set fullscreen size to the output dimensions.
            const auto& outBox = self->m_output->getBox();
            self->setFullscreen(true);
            self->setGeometry(outBox.x, outBox.y, outBox.width, outBox.height);
        } else {
            self->setFullscreen(false);
            // The compositor should restore the previous geometry;
            // for now, just reconfigure with current geometry.
        }
    }

    LOG_DEBUG("Surface '{}' request fullscreen (now={})",
              self->m_appId, self->m_fullscreen);
}

void Surface::onRequestMaximize(struct wl_listener* listener, void* data) {
    Surface* self = wl_container_of(listener, self, m_requestMaximizeListener);
    (void)data;

    bool want_maximized = !self->m_maximized;

    if (self->m_output) {
        if (want_maximized) {
            const auto& outBox = self->m_output->getBox();
            self->setMaximized(true);
            self->setGeometry(outBox.x, outBox.y, outBox.width, outBox.height);
        } else {
            self->setMaximized(false);
        }
    }

    LOG_DEBUG("Surface '{}' request maximize (now={})",
              self->m_appId, self->m_maximized);
}

void Surface::onRequestMove(struct wl_listener* listener, void* data) {
    Surface* self = wl_container_of(listener, self, m_requestMoveListener);
    (void)data;

    // The input manager / seat should handle the interactive move.
    // We just log it here and the seat will initiate a grab.
    LOG_DEBUG("Surface '{}' request move", self->m_appId);
}

void Surface::onRequestResize(struct wl_listener* listener, void* data) {
    Surface* self = wl_container_of(listener, self, m_requestResizeListener);
    (void)data;

    LOG_DEBUG("Surface '{}' request resize", self->m_appId);
}

void Surface::onSetTitle(struct wl_listener* listener, void* data) {
    Surface* self = wl_container_of(listener, self, m_setTitleListener);
    (void)data;

    if (self->m_toplevel && self->m_toplevel->title) {
        self->m_title = self->m_toplevel->title;
    } else {
        self->m_title.clear();
    }

    // Title change may require a decoration repaint.
    if (self->m_decorationMode == DecorationMode::ServerSide) {
        self->damageOutput();
    }
}

void Surface::onSetAppId(struct wl_listener* listener, void* data) {
    Surface* self = wl_container_of(listener, self, m_setAppIdListener);
    (void)data;

    if (self->m_toplevel && self->m_toplevel->app_id) {
        self->m_appId = self->m_toplevel->app_id;
    } else {
        self->m_appId.clear();
    }
}

void Surface::onNewPopup(struct wl_listener* listener, void* data) {
    Surface* self = wl_container_of(listener, self, m_newPopupListener);
    auto* xdg_popup = static_cast<struct wlr_xdg_popup*>(data);

    self->addPopup(xdg_popup);
    LOG_DEBUG("New popup for surface '{}'", self->m_appId);
}

void Surface::onNewSubsurface(struct wl_listener* listener, void* data) {
    Surface* self = wl_container_of(listener, self, m_newSubsurfaceListener);
    auto* sub = static_cast<struct wlr_subsurface*>(data);

    self->addSubsurface(sub);
    LOG_DEBUG("New subsurface for surface '{}'", self->m_appId);
}

void Surface::onAckConfigure(struct wl_listener* listener, void* data) {
    Surface* self = wl_container_of(listener, self, m_ackConfigureListener);
    auto* event = static_cast<struct wlr_xdg_surface_configure*>(data);

    if (event->serial == self->m_pendingConfigureSerial) {
        // Client acknowledged our configure. Apply the pending state.
        self->m_fullscreen = self->m_pendingState.fullscreen;
        self->m_maximized  = self->m_pendingState.maximized;
        self->m_activated  = self->m_pendingState.activated;

        self->m_pendingConfigureSerial = 0;

        LOG_DEBUG("Surface '{}' ack_configure (serial={}, fullscreen={}, "
                  "maximized={}, activated={})",
                  self->m_appId, event->serial,
                  self->m_fullscreen, self->m_maximized, self->m_activated);
    }
    // If the serial doesn't match, it's an ack for an older configure
    // that we don't care about anymore.
}

// ===========================================================================
// XdgDecorationManager implementation
// ===========================================================================

XdgDecorationManager::XdgDecorationManager(Server& server)
    : m_server(server)
{
}

XdgDecorationManager::~XdgDecorationManager() {
    if (m_newDecorationListener.link.next) {
        wl_list_remove(&m_newDecorationListener.link);
    }
    m_decorations.clear();
}

bool XdgDecorationManager::init() {
    struct wl_display* display = m_server.getDisplay();
    if (!display) return false;

    m_wlrManager = wlr_xdg_decoration_manager_v1_create(display);
    if (!m_wlrManager) {
        LOG_ERROR("Failed to create xdg-decoration-manager-v1");
        return false;
    }

    m_newDecorationListener.notify = onNewDecoration;
    wl_signal_add(&m_wlrManager->events.new_toplevel_decoration,
                  &m_newDecorationListener);

    LOG_INFO("XDG decoration manager initialized (preferred={})",
             m_preferredMode == DecorationMode::ServerSide ? "server-side"
                                                            : "client-side");
    return true;
}

void XdgDecorationManager::onNewDecoration(struct wl_listener* listener,
                                            void* data)
{
    XdgDecorationManager* self =
        wl_container_of(listener, self, m_newDecorationListener);
    auto* deco = static_cast<struct wlr_xdg_toplevel_decoration_v1*>(data);

    auto state = std::make_unique<DecorationState>();
    state->manager = self;

    // Listen for mode changes and destruction.
    state->modeListener.notify = DecorationState::onMode;
    wl_signal_add(&deco->events.request_mode, &state->modeListener);

    state->destroyListener.notify = DecorationState::onDestroy;
    wl_signal_add(&deco->events.destroy, &state->destroyListener);

    // Set our preferred mode immediately.
    enum wlr_xdg_toplevel_decoration_v1_mode wlr_mode;
    switch (self->m_preferredMode) {
    case DecorationMode::ServerSide:
        wlr_mode = WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
        break;
    case DecorationMode::ClientSide:
        wlr_mode = WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
        break;
    default:
        wlr_mode = WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
        break;
    }

    wlr_xdg_toplevel_decoration_v1_set_mode(deco, wlr_mode);

    self->m_decorations.push_back(std::move(state));

    LOG_DEBUG("New xdg toplevel decoration registered");
}

void XdgDecorationManager::DecorationState::onMode(
    struct wl_listener* listener, void* data)
{
    DecorationState* self = wl_container_of(listener, self, modeListener);
    auto* deco = static_cast<struct wlr_xdg_toplevel_decoration_v1*>(data);
    (void)self;

    // The client is requesting a specific decoration mode.
    // We respond with our preferred mode. In a more advanced implementation
    // we could honour the client's preference for certain app_ids.

    enum wlr_xdg_toplevel_decoration_v1_mode wlr_mode;
    switch (self->manager->m_preferredMode) {
    case DecorationMode::ServerSide:
        wlr_mode = WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
        break;
    case DecorationMode::ClientSide:
        wlr_mode = WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
        break;
    default:
        wlr_mode = WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
        break;
    }

    wlr_xdg_toplevel_decoration_v1_set_mode(deco, wlr_mode);

    // Update the corresponding Surface's decoration mode.
    struct wlr_xdg_toplevel* toplevel = deco->toplevel;
    if (toplevel) {
        // Look up the Surface from the compositor.
        auto& surfaces = self->manager->m_server.getCompositor().getSurfaces();
        for (auto& surf : surfaces) {
            if (surf && surf->getToplevel() == toplevel) {
                DecorationMode mode =
                    (deco->current.mode ==
                     WLR_XDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE)
                        ? DecorationMode::ServerSide
                        : DecorationMode::ClientSide;
                surf->setDecorationMode(mode);
                break;
            }
        }
    }

    LOG_DEBUG("Decoration mode request handled (mode={})",
              static_cast<int>(deco->current.mode));
}

void XdgDecorationManager::DecorationState::onDestroy(
    struct wl_listener* listener, void* data)
{
    DecorationState* self = wl_container_of(listener, self, destroyListener);
    (void)data;

    wl_list_remove(&self->modeListener.link);
    wl_list_remove(&self->destroyListener.link);

    auto& decorations = self->manager->m_decorations;
    decorations.erase(
        std::remove_if(decorations.begin(), decorations.end(),
            [self](const auto& p) { return p.get() == self; }),
        decorations.end());
}

} // namespace eternal

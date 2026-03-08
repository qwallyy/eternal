#include "eternal/core/Output.hpp"
#include "eternal/core/Surface.hpp"
#include "eternal/core/Server.hpp"
#include "eternal/core/Compositor.hpp"
#include "eternal/render/Renderer.hpp"
#include "eternal/utils/Logger.hpp"

extern "C" {
#include <wayland-server-core.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_damage_ring.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/pass.h>
#include <wlr/render/allocator.h>
#include <wlr/util/transform.h>
#include <pixman.h>
}

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>

namespace eternal {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

Output::Output(Server& server, wlr_output* wlrOutput)
    : m_server(server), m_wlrOutput(wlrOutput)
{
    assert(wlrOutput);

    // Cache identification strings.
    m_name   = wlrOutput->name   ? wlrOutput->name   : "";
    m_make   = wlrOutput->make   ? wlrOutput->make   : "";
    m_model  = wlrOutput->model  ? wlrOutput->model  : "";
    m_serial = wlrOutput->serial ? wlrOutput->serial : "";

    m_scale       = wlrOutput->scale;
    m_refreshRate = wlrOutput->refresh;

    // Compute initial geometry.
    m_geometry.width  = wlrOutput->width;
    m_geometry.height = wlrOutput->height;

    // Initialize damage tracking.
    wlr_damage_ring_init(&m_damageRing);
    m_damageRing.width  = m_geometry.width;
    m_damageRing.height = m_geometry.height;
    pixman_region32_init(&m_currentDamage);
    m_fullDamage = true;
    m_firstFrame = true;

    // Hook up wl_listeners.
    m_frameListener.notify = onFrame;
    wl_signal_add(&wlrOutput->events.frame, &m_frameListener);

    m_requestStateListener.notify = onRequestState;
    wl_signal_add(&wlrOutput->events.request_state, &m_requestStateListener);

    m_destroyListener.notify = onDestroy;
    wl_signal_add(&wlrOutput->events.destroy, &m_destroyListener);

    LOG_INFO("Output '{}' created ({}x{} @ {}mHz, scale={})",
             m_name, m_geometry.width, m_geometry.height, m_refreshRate, m_scale);
}

Output::~Output() {
    wl_list_remove(&m_frameListener.link);
    wl_list_remove(&m_requestStateListener.link);
    wl_list_remove(&m_destroyListener.link);

    pixman_region32_fini(&m_currentDamage);
    wlr_damage_ring_finish(&m_damageRing);

    LOG_DEBUG("Output '{}' destroyed", m_name);
}

// ---------------------------------------------------------------------------
// State modifiers
// ---------------------------------------------------------------------------

bool Output::enable() {
    if (!m_wlrOutput) return false;

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);

    auto* mode = wlr_output_preferred_mode(m_wlrOutput);
    if (mode) {
        wlr_output_state_set_mode(&state, mode);
    }

    bool ok = wlr_output_commit_state(m_wlrOutput, &state);
    wlr_output_state_finish(&state);

    if (ok) {
        m_dpmsOn = true;
        m_geometry.width  = m_wlrOutput->width;
        m_geometry.height = m_wlrOutput->height;
        m_refreshRate     = m_wlrOutput->refresh;

        // Reset damage ring for the new dimensions.
        wlr_damage_ring_finish(&m_damageRing);
        wlr_damage_ring_init(&m_damageRing);
        m_damageRing.width  = m_geometry.width;
        m_damageRing.height = m_geometry.height;
        m_fullDamage = true;
    }

    return ok;
}

void Output::disable() {
    if (!m_wlrOutput) return;

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, false);
    wlr_output_commit_state(m_wlrOutput, &state);
    wlr_output_state_finish(&state);

    m_dpmsOn = false;
}

bool Output::setMode(int width, int height, int refreshMHz) {
    if (!m_wlrOutput) return false;

    struct wlr_output_mode* best = nullptr;
    struct wlr_output_mode* mode;
    wl_list_for_each(mode, &m_wlrOutput->modes, link) {
        if (mode->width == width && mode->height == height) {
            if (refreshMHz == 0) {
                // Pick the highest refresh.
                if (!best || mode->refresh > best->refresh) {
                    best = mode;
                }
            } else if (mode->refresh == refreshMHz) {
                best = mode;
                break;
            }
        }
    }

    if (!best) {
        LOG_WARN("No matching mode {}x{}@{} for output '{}'",
                 width, height, refreshMHz, m_name);
        return false;
    }

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_mode(&state, best);
    bool ok = wlr_output_commit_state(m_wlrOutput, &state);
    wlr_output_state_finish(&state);

    if (ok) {
        m_geometry.width  = m_wlrOutput->width;
        m_geometry.height = m_wlrOutput->height;
        m_refreshRate     = m_wlrOutput->refresh;

        // Reinitialize damage ring for new resolution.
        wlr_damage_ring_finish(&m_damageRing);
        wlr_damage_ring_init(&m_damageRing);
        m_damageRing.width  = m_geometry.width;
        m_damageRing.height = m_geometry.height;
        m_fullDamage = true;
    }

    return ok;
}

void Output::setScale(float scale) {
    if (!m_wlrOutput || scale <= 0.0f) return;

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_scale(&state, scale);
    if (wlr_output_commit_state(m_wlrOutput, &state)) {
        m_scale = scale;

        // Effective resolution changes with scale.
        int w, h;
        wlr_output_effective_resolution(m_wlrOutput, &w, &h);
        m_geometry.width  = w;
        m_geometry.height = h;

        wlr_damage_ring_finish(&m_damageRing);
        wlr_damage_ring_init(&m_damageRing);
        m_damageRing.width  = m_geometry.width;
        m_damageRing.height = m_geometry.height;
        m_fullDamage = true;
    }
    wlr_output_state_finish(&state);
}

void Output::setTransform(int transform) {
    if (!m_wlrOutput) return;

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_transform(&state,
        static_cast<enum wl_output_transform>(transform));
    if (wlr_output_commit_state(m_wlrOutput, &state)) {
        m_transform = transform;
        m_geometry.width  = m_wlrOutput->width;
        m_geometry.height = m_wlrOutput->height;

        wlr_damage_ring_finish(&m_damageRing);
        wlr_damage_ring_init(&m_damageRing);
        m_damageRing.width  = m_geometry.width;
        m_damageRing.height = m_geometry.height;
        m_fullDamage = true;
    }
    wlr_output_state_finish(&state);
}

bool Output::setGamma(const std::vector<uint16_t>& r,
                      const std::vector<uint16_t>& g,
                      const std::vector<uint16_t>& b)
{
    if (!m_wlrOutput) return false;
    if (r.empty() && g.empty() && b.empty()) {
        // Reset gamma.
        m_gammaR.clear();
        m_gammaG.clear();
        m_gammaB.clear();
        return true;
    }

    if (r.size() != g.size() || g.size() != b.size()) {
        LOG_ERROR("Gamma LUT sizes must match");
        return false;
    }

    m_gammaR = r;
    m_gammaG = g;
    m_gammaB = b;

    // Gamma application requires a full repaint.
    m_fullDamage = true;
    scheduleFrame();
    return true;
}

void Output::toggleDPMS() {
    if (m_dpmsOn) {
        disable();
    } else {
        enable();
    }
}

// ---------------------------------------------------------------------------
// Damage tracking
// ---------------------------------------------------------------------------

void Output::addDamageBox(int x, int y, int w, int h) {
    // Convert from layout coordinates to output-local.
    int ox = x - m_geometry.x;
    int oy = y - m_geometry.y;

    pixman_region32_union_rect(&m_currentDamage, &m_currentDamage,
                               ox, oy, w, h);
    scheduleFrame();
}

void Output::addDamageRegion(pixman_region32_t* region) {
    if (!region) return;
    pixman_region32_union(&m_currentDamage, &m_currentDamage, region);
    scheduleFrame();
}

void Output::addFullDamage() {
    m_fullDamage = true;
    scheduleFrame();
}

void Output::scheduleFrame() {
    if (m_wlrOutput) {
        wlr_output_schedule_frame(m_wlrOutput);
    }
}

void Output::damageSurface(Surface* surface) {
    if (!surface || !surface->isMapped()) return;

    const auto& geo = surface->getGeometry();
    addDamageBox(geo.x, geo.y, geo.width, geo.height);
}

void Output::damageSurfaceMove(Surface* surface, int oldX, int oldY) {
    if (!surface) return;

    const auto& geo = surface->getGeometry();

    // Damage old position.
    addDamageBox(oldX, oldY, geo.width, geo.height);

    // Damage new position.
    addDamageBox(geo.x, geo.y, geo.width, geo.height);
}

// ---------------------------------------------------------------------------
// Workspace management
// ---------------------------------------------------------------------------

void Output::addWorkspace(Workspace* workspace) {
    if (!workspace) return;
    auto it = std::find(m_workspaces.begin(), m_workspaces.end(), workspace);
    if (it == m_workspaces.end()) {
        m_workspaces.push_back(workspace);
    }
}

void Output::removeWorkspace(Workspace* workspace) {
    m_workspaces.erase(
        std::remove(m_workspaces.begin(), m_workspaces.end(), workspace),
        m_workspaces.end());

    if (m_currentWorkspace == workspace) {
        m_currentWorkspace = m_workspaces.empty() ? nullptr : m_workspaces.front();
    }
}

// ---------------------------------------------------------------------------
// Frame rendering with damage tracking
// ---------------------------------------------------------------------------

void Output::renderFrame() {
    if (!m_wlrOutput || !m_dpmsOn) return;

    // Accumulate surface damage: collect commit damage from all mapped
    // surfaces that overlap this output.
    auto& compositor = m_server.getCompositor();
    for (auto& surf : compositor.getSurfaces()) {
        if (!surf || !surf->isMapped()) continue;

        // Check if the surface overlaps this output.
        const auto& sg = surf->getGeometry();
        int sx = sg.x - m_geometry.x;
        int sy = sg.y - m_geometry.y;

        // The surface contributes to this output if it overlaps.
        if (sx + sg.width <= 0 || sy + sg.height <= 0 ||
            sx >= m_geometry.width || sy >= m_geometry.height) {
            continue;
        }

        // Get surface-reported buffer damage from the wlr_surface.
        struct wlr_surface* wlr_surf = surf->getWlrSurface();
        if (wlr_surf && wlr_surf->current.buffer_damage.extents.x2 > 0) {
            pixman_region32_t surf_damage;
            pixman_region32_init(&surf_damage);
            pixman_region32_copy(&surf_damage,
                                 &wlr_surf->buffer_damage);

            // Translate surface damage to output-local coordinates.
            pixman_region32_translate(&surf_damage, sx, sy);

            // Clip to output bounds.
            pixman_region32_intersect_rect(&surf_damage, &surf_damage,
                                           0, 0,
                                           m_geometry.width, m_geometry.height);

            pixman_region32_union(&m_currentDamage, &m_currentDamage,
                                  &surf_damage);
            pixman_region32_fini(&surf_damage);
        }
    }

    // Feed accumulated damage into the damage ring.
    if (m_fullDamage || m_firstFrame) {
        wlr_damage_ring_add_whole(&m_damageRing);
        m_fullDamage = false;
        m_firstFrame = false;
    } else if (pixman_region32_not_empty(&m_currentDamage)) {
        wlr_damage_ring_add(&m_damageRing, &m_currentDamage);
    }
    pixman_region32_clear(&m_currentDamage);

    // Set up output state and begin the render pass.
    struct wlr_output_state output_state;
    wlr_output_state_init(&output_state);

    int buffer_age;
    struct wlr_render_pass* pass = wlr_output_begin_render_pass(
        m_wlrOutput, &output_state, &buffer_age, nullptr);
    if (!pass) {
        wlr_output_state_finish(&output_state);
        return;
    }

    // Compute the damage region for this frame using buffer age.
    pixman_region32_t frame_damage;
    pixman_region32_init(&frame_damage);
    wlr_damage_ring_get_buffer_damage(&m_damageRing, buffer_age, &frame_damage);

    // If no damage at all, we still need to submit the pass to satisfy
    // the wlr contract, but we can skip most rendering.
    bool has_damage = pixman_region32_not_empty(&frame_damage);

    if (has_damage) {
        // Clear the damaged region to the background color.
        // We scissor each rectangle in the damage region individually.
        int nrects = 0;
        pixman_box32_t* rects = pixman_region32_rectangles(&frame_damage, &nrects);

        for (int i = 0; i < nrects; i++) {
            struct wlr_render_rect_options clear_opts = {};
            clear_opts.box = {
                .x = rects[i].x1,
                .y = rects[i].y1,
                .width  = rects[i].x2 - rects[i].x1,
                .height = rects[i].y2 - rects[i].y1,
            };
            clear_opts.color = {0.1f, 0.1f, 0.12f, 1.0f};
            clear_opts.clip = &frame_damage;

            wlr_render_pass_add_rect(pass, &clear_opts);
        }

        // Render all visible surfaces within the damage region.
        renderSurfaces(pass, &frame_damage);
    }

    wlr_render_pass_submit(pass);
    wlr_output_commit_state(m_wlrOutput, &output_state);
    wlr_output_state_finish(&output_state);

    // Rotate the damage ring for the next frame.
    wlr_damage_ring_rotate(&m_damageRing);

    pixman_region32_fini(&frame_damage);
}

void Output::renderSurfaces(struct wlr_render_pass* pass,
                            pixman_region32_t* damage)
{
    auto& compositor = m_server.getCompositor();

    // Iterate surfaces in stacking order (bottom to top).
    for (auto& surface : compositor.getSurfaces()) {
        if (!surface || !surface->isMapped()) continue;

        const auto& sg = surface->getGeometry();

        // Check overlap with this output.
        int ox = sg.x - m_geometry.x;
        int oy = sg.y - m_geometry.y;

        if (ox + sg.width <= 0 || oy + sg.height <= 0 ||
            ox >= m_geometry.width || oy >= m_geometry.height) {
            continue;
        }

        // Render the main surface and its full surface tree
        // (subsurfaces and popups).
        surface->forEachRenderSurface(
            [this, pass, damage, ox, oy, &sg](
                struct wlr_surface* wlr_surf, int sx, int sy)
            {
                renderSurfaceAt(pass, wlr_surf, ox + sx, oy + sy, damage);
            });
    }
}

void Output::renderSurfaceAt(struct wlr_render_pass* pass,
                              struct wlr_surface* surface,
                              int sx, int sy,
                              pixman_region32_t* damage)
{
    if (!surface || !pass) return;

    struct wlr_texture* texture = wlr_surface_get_texture(surface);
    if (!texture) return;

    // Check if this surface rectangle intersects the damage region at all.
    pixman_region32_t surf_region;
    pixman_region32_init_rect(&surf_region,
                              sx, sy,
                              surface->current.width,
                              surface->current.height);

    pixman_region32_t visible;
    pixman_region32_init(&visible);
    pixman_region32_intersect(&visible, &surf_region, damage);

    if (!pixman_region32_not_empty(&visible)) {
        // This surface has no visible damage; skip rendering.
        pixman_region32_fini(&visible);
        pixman_region32_fini(&surf_region);
        return;
    }

    // Render the texture, clipping to the damage region.
    struct wlr_render_texture_options opts = {};
    opts.texture = texture;
    opts.dst_box = {
        .x = sx,
        .y = sy,
        .width  = surface->current.width,
        .height = surface->current.height,
    };
    opts.clip = &visible;

    // Apply per-surface transform if any.
    enum wl_output_transform transform =
        wlr_output_transform_invert(surface->current.transform);
    opts.transform = transform;

    wlr_render_pass_add_texture(pass, &opts);

    // Notify the surface that it has been presented so the client can
    // track frame callbacks properly.
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_surface_send_frame_done(surface, &now);

    pixman_region32_fini(&visible);
    pixman_region32_fini(&surf_region);
}

// ---------------------------------------------------------------------------
// Listener callbacks
// ---------------------------------------------------------------------------

void Output::onFrame(struct wl_listener* listener, void* data) {
    Output* self = wl_container_of(listener, self, m_frameListener);
    (void)data;

    self->renderFrame();
}

void Output::onRequestState(struct wl_listener* listener, void* data) {
    Output* self = wl_container_of(listener, self, m_requestStateListener);
    auto* event = static_cast<struct wlr_output_event_request_state*>(data);

    // Apply the requested state (mode change, DPMS, etc.).
    if (wlr_output_commit_state(self->m_wlrOutput, event->state)) {
        // Update cached geometry.
        self->m_geometry.width  = self->m_wlrOutput->width;
        self->m_geometry.height = self->m_wlrOutput->height;
        self->m_scale           = self->m_wlrOutput->scale;
        self->m_refreshRate     = self->m_wlrOutput->refresh;

        // Reinitialize damage ring for potential new resolution.
        wlr_damage_ring_finish(&self->m_damageRing);
        wlr_damage_ring_init(&self->m_damageRing);
        self->m_damageRing.width  = self->m_geometry.width;
        self->m_damageRing.height = self->m_geometry.height;
        self->m_fullDamage = true;

        LOG_DEBUG("Output '{}' state applied ({}x{} @ {}mHz)",
                  self->m_name, self->m_geometry.width,
                  self->m_geometry.height, self->m_refreshRate);
    }
}

void Output::onDestroy(struct wl_listener* listener, void* data) {
    Output* self = wl_container_of(listener, self, m_destroyListener);
    (void)data;

    LOG_INFO("Output '{}' disconnected", self->m_name);

    self->m_wlrOutput = nullptr;

    // Let the compositor handle cleanup and removal from the output list.
    self->m_server.getCompositor().destroyOutput(self);
}

// ---------------------------------------------------------------------------
// DPI (Task 83)
// ---------------------------------------------------------------------------

int Output::getPhysWidthMm() const {
    return m_wlrOutput ? m_wlrOutput->phys_width : 0;
}

int Output::getPhysHeightMm() const {
    return m_wlrOutput ? m_wlrOutput->phys_height : 0;
}

// ---------------------------------------------------------------------------
// DPMS control (Task 86)
// ---------------------------------------------------------------------------

void Output::setDPMS(bool on) {
    if (!m_wlrOutput) return;
    if (m_dpmsOn == on) return;

    if (on) {
        // Turning on: start fade-in.
        enable();
        m_dpmsFading = true;
        m_dpmsFadeProgress = 1.0f; // fading from black to on
        m_dpmsOn = true;
        LOG_INFO("Output '{}': DPMS on (waking)", m_name);
    } else {
        // Turning off: start fade-out animation.
        m_dpmsFading = true;
        m_dpmsFadeProgress = 0.0f; // fading from on to black
        LOG_INFO("Output '{}': DPMS off (suspending)", m_name);
        // Actual disable happens when fade completes (in updateDPMSTimer).
    }

    addFullDamage();
}

void Output::setDPMSIdleTimeout(int seconds) {
    m_dpmsIdleTimeoutSec = seconds;
    LOG_DEBUG("Output '{}': DPMS idle timeout set to {} seconds",
              m_name, seconds);
}

void Output::wakeOnInput() {
    m_lastInputTime = 0.0; // will be reset by updateDPMSTimer
    if (!m_dpmsOn) {
        setDPMS(true);
    }
    // Reset fade if we were fading to black.
    if (m_dpmsFading && m_dpmsFadeProgress > 0.0f) {
        m_dpmsFading = false;
        m_dpmsFadeProgress = 0.0f;
    }
}

void Output::updateDPMSTimer(double nowSec) {
    // Track last input time.
    if (m_lastInputTime <= 0.0) {
        m_lastInputTime = nowSec;
    }

    // Handle fade animation.
    if (m_dpmsFading) {
        double elapsed = nowSec - m_dpmsFadeStartTime;
        if (m_dpmsFadeStartTime <= 0.0) {
            m_dpmsFadeStartTime = nowSec;
            elapsed = 0.0;
        }

        float progress = static_cast<float>(elapsed / kDPMSFadeDuration);
        progress = std::clamp(progress, 0.0f, 1.0f);

        if (m_dpmsOn) {
            // Fading in: progress goes 1.0 -> 0.0 (black to normal).
            m_dpmsFadeProgress = 1.0f - progress;
        } else {
            // Fading out: progress goes 0.0 -> 1.0 (normal to black).
            m_dpmsFadeProgress = progress;
        }

        if (progress >= 1.0f) {
            m_dpmsFading = false;
            m_dpmsFadeStartTime = 0.0;

            if (!m_dpmsOn) {
                // Fade-out complete: actually disable the output.
                disable();
            }
        }

        addFullDamage();
        return;
    }

    // Check idle timeout.
    if (m_dpmsIdleTimeoutSec > 0 && m_dpmsOn) {
        double idleDuration = nowSec - m_lastInputTime;
        if (idleDuration >= static_cast<double>(m_dpmsIdleTimeoutSec)) {
            setDPMS(false);
        }
    }
}

} // namespace eternal

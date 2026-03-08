#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

extern "C" {
#include <wayland-server-core.h>
#include <pixman.h>
#include <wlr/types/wlr_damage_ring.h>

struct wlr_output;
struct wlr_output_state;
struct wlr_scene_output;
struct wlr_output_layout_output;
struct wlr_render_pass;
struct wlr_surface;
}

namespace eternal {

class Server;
class Surface;
class Renderer;
class Workspace;

/// Axis-aligned bounding box for output geometry.
struct OutputBox {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

/// Represents a physical monitor/output managed by the compositor.
class Output {
public:
    explicit Output(Server& server, wlr_output* wlrOutput);
    ~Output();

    Output(const Output&) = delete;
    Output& operator=(const Output&) = delete;
    Output(Output&&) = delete;
    Output& operator=(Output&&) = delete;

    // ── Identification ──────────────────────────────────────────────────

    /// Connector name (e.g. "DP-1", "HDMI-A-1").
    const std::string& getName() const { return m_name; }

    /// Monitor manufacturer string.
    const std::string& getMake() const { return m_make; }

    /// Monitor model string.
    const std::string& getModel() const { return m_model; }

    /// Monitor serial number.
    const std::string& getSerial() const { return m_serial; }

    // ── Geometry & display properties ───────────────────────────────────

    /// Current output geometry in layout coordinates.
    const OutputBox& getBox() const { return m_geometry; }

    /// Fractional scale factor applied to this output.
    float getScale() const { return m_scale; }

    /// wl_output transform value (rotation / flip).
    int getTransform() const { return m_transform; }

    /// Refresh rate in mHz (e.g. 60000 for 60 Hz).
    int getRefreshRate() const { return m_refreshRate; }

    /// Whether variable refresh rate (adaptive sync) is enabled.
    bool isVRREnabled() const { return m_vrrEnabled; }

    /// Whether the display power management signaling is on.
    bool isDPMSOn() const { return m_dpmsOn; }

    /// Physical size in millimeters.
    int getPhysWidthMm() const;
    int getPhysHeightMm() const;

    /// Calculated DPI based on physical size and resolution.
    double getDPI() const { return m_dpi; }

    // ── State modifiers ─────────────────────────────────────────────────

    /// Enable this output and make it part of the layout.
    bool enable();

    /// Disable this output (remove from layout, power off).
    void disable();

    /// Set the preferred mode by resolution and optional refresh rate (mHz).
    /// A refresh of 0 picks the highest available rate for the given size.
    bool setMode(int width, int height, int refreshMHz = 0);

    /// Set the fractional scale factor.
    void setScale(float scale);

    /// Set the wl_output transform (0-7).
    void setTransform(int transform);

    /// Apply a gamma look-up table.  Pass empty vectors to reset.
    bool setGamma(const std::vector<uint16_t>& r,
                  const std::vector<uint16_t>& g,
                  const std::vector<uint16_t>& b);

    /// Toggle display power management signaling (screen on/off).
    void toggleDPMS();

    // ── DPMS control (Task 86) ──────────────────────────────────────────

    /// Explicitly set DPMS on or off.
    void setDPMS(bool on);

    /// Set idle timeout in seconds for auto-suspend (0 to disable).
    void setDPMSIdleTimeout(int seconds);

    /// Get the idle timeout in seconds.
    int getDPMSIdleTimeout() const { return m_dpmsIdleTimeoutSec; }

    /// Notify that user input occurred (wake from DPMS suspend).
    void wakeOnInput();

    /// Update DPMS timer (call from event loop).
    void updateDPMSTimer(double nowSec);

    /// Whether DPMS fade animation is in progress.
    bool isDPMSFading() const { return m_dpmsFading; }

    /// Get DPMS fade progress (0.0 = fully on, 1.0 = fully off).
    float getDPMSFadeProgress() const { return m_dpmsFadeProgress; }

    // ── Damage tracking ─────────────────────────────────────────────────

    /// Add pixel-level damage for a specific region (layout coordinates).
    void addDamageBox(int x, int y, int w, int h);

    /// Add damage from a pixman region (output-local coordinates).
    void addDamageRegion(pixman_region32_t* region);

    /// Mark the entire output as damaged (full redraw next frame).
    void addFullDamage();

    /// Schedule a frame if there is pending damage.
    void scheduleFrame();

    /// Whether this output needs a full repaint (first frame, resize, etc.).
    bool needsFullDamage() const { return m_fullDamage; }

    /// Get the damage ring for direct access.
    wlr_damage_ring* getDamageRing() { return &m_damageRing; }

    // ── Surface damage helpers ──────────────────────────────────────────

    /// Damage the region occupied by a surface (for map/unmap/move/resize).
    void damageSurface(Surface* surface);

    /// Damage the old and new positions when a surface moves.
    void damageSurfaceMove(Surface* surface, int oldX, int oldY);

    // ── Workspace access ────────────────────────────────────────────────

    /// The currently visible workspace on this output.
    Workspace* getCurrentWorkspace() const { return m_currentWorkspace; }

    /// Set the active workspace on this output.
    void setCurrentWorkspace(Workspace* workspace) { m_currentWorkspace = workspace; }

    /// All workspaces assigned to this output.
    const std::vector<Workspace*>& getWorkspaces() const { return m_workspaces; }

    /// Assign a workspace to this output.
    void addWorkspace(Workspace* workspace);

    /// Remove a workspace assignment from this output.
    void removeWorkspace(Workspace* workspace);

    // ── Underlying wlroots objects ──────────────────────────────────────

    wlr_output* getWlrOutput() const { return m_wlrOutput; }
    wlr_scene_output* getSceneOutput() const { return m_sceneOutput; }
    wlr_output_layout_output* getLayoutOutput() const { return m_layoutOutput; }

    void setSceneOutput(wlr_scene_output* sceneOutput) { m_sceneOutput = sceneOutput; }
    void setLayoutOutput(wlr_output_layout_output* lo) { m_layoutOutput = lo; }

    /// Reference back to the owning server.
    Server& getServer() const { return m_server; }

private:
    // ── Listener callbacks ──────────────────────────────────────────────

    static void onFrame(struct wl_listener* listener, void* data);
    static void onRequestState(struct wl_listener* listener, void* data);
    static void onDestroy(struct wl_listener* listener, void* data);

    // ── Rendering helpers ───────────────────────────────────────────────

    /// Perform a full render pass for this output.
    void renderFrame();

    /// Render all visible surfaces onto the given render pass, applying
    /// scissor to the damage region.
    void renderSurfaces(struct wlr_render_pass* pass,
                        pixman_region32_t* damage);

    /// Render a single wlr_surface and its subsurfaces at the given position.
    void renderSurfaceAt(struct wlr_render_pass* pass,
                         Surface* owner,
                         struct wlr_surface* surface,
                         int sx, int sy,
                         pixman_region32_t* damage);

    // ── Data ────────────────────────────────────────────────────────────

    Server& m_server;
    wlr_output* m_wlrOutput = nullptr;
    wlr_scene_output* m_sceneOutput = nullptr;
    wlr_output_layout_output* m_layoutOutput = nullptr;

    std::string m_name;
    std::string m_make;
    std::string m_model;
    std::string m_serial;

    OutputBox m_geometry{};
    float m_scale = 1.0f;
    int m_transform = 0;
    int m_refreshRate = 0;
    bool m_vrrEnabled = false;
    bool m_dpmsOn = true;
    double m_dpi = 96.0;

    std::vector<uint16_t> m_gammaR;
    std::vector<uint16_t> m_gammaG;
    std::vector<uint16_t> m_gammaB;

    Workspace* m_currentWorkspace = nullptr;
    std::vector<Workspace*> m_workspaces;

    // ── Damage tracking state ────────────────────────────────────────────

    wlr_damage_ring m_damageRing{};
    pixman_region32_t m_currentDamage{};
    bool m_fullDamage = true;
    bool m_firstFrame = true;

    // ── DPMS state (Task 86) ─────────────────────────────────────────────

    int m_dpmsIdleTimeoutSec = 0;
    double m_lastInputTime = 0.0;
    bool m_dpmsFading = false;
    float m_dpmsFadeProgress = 0.0f;
    double m_dpmsFadeStartTime = 0.0;
    static constexpr double kDPMSFadeDuration = 0.5; // seconds

    // ── wl_listener wrappers ────────────────────────────────────────────

    struct wl_listener m_frameListener{};
    struct wl_listener m_requestStateListener{};
    struct wl_listener m_destroyListener{};
};

} // namespace eternal

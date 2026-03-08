#pragma once
#include <cstdint>
#include <memory>
#include <vector>

extern "C" {
#include <wayland-server-core.h>

struct wlr_output;
struct wlr_xdg_toplevel;
struct wlr_xdg_shell;
struct wlr_scene;
}

namespace eternal {

class Server;
class Output;
class Surface;

/// High-level compositor that manages outputs, surfaces, and focus.
class Compositor {
public:
    explicit Compositor(Server& server);
    ~Compositor();

    Compositor(const Compositor&) = delete;
    Compositor& operator=(const Compositor&) = delete;
    Compositor(Compositor&&) = delete;
    Compositor& operator=(Compositor&&) = delete;

    /// Initialize the compositor (create the xdg-shell, etc.).
    bool init();

    // ── Output management ───────────────────────────────────────────────

    /// Create and register a new output for the given wlr_output.
    Output* createOutput(wlr_output* wlrOutput);

    /// Destroy and unregister the given output.
    void destroyOutput(Output* output);

    /// All currently active outputs.
    const std::vector<std::unique_ptr<Output>>& getOutputs() const { return m_outputs; }

    /// The output that currently has input focus.
    Output* getActiveOutput() const { return m_activeOutput; }

    /// Set the active (focused) output.
    void setActiveOutput(Output* output) { m_activeOutput = output; }

    /// Return the output whose geometry contains the given layout point.
    Output* getOutputAt(double layoutX, double layoutY) const;

    /// Recalculate output positions in the layout.
    void arrangeOutputs();

    // ── Surface management ──────────────────────────────────────────────

    /// Create and register a new surface for the given toplevel.
    Surface* createSurface(wlr_xdg_toplevel* toplevel);

    /// Destroy and unregister the given surface.
    void destroySurface(Surface* surface);

    /// All surfaces known to the compositor (mapped or not).
    const std::vector<std::unique_ptr<Surface>>& getSurfaces() const { return m_surfaces; }

    /// Return the surface under the given layout coordinates, if any.
    /// Optionally writes the surface-local coordinates to sx/sy.
    Surface* getSurfaceAt(double layoutX, double layoutY,
                          double* sx = nullptr, double* sy = nullptr) const;

    // ── Focus ───────────────────────────────────────────────────────────

    /// The surface that currently holds keyboard focus.
    Surface* getFocusedSurface() const { return m_focusedSurface; }

    /// Set keyboard focus to the given surface (nullptr to clear).
    void setFocusedSurface(Surface* surface);

    /// Move focus to the next surface in the focus stack.
    void focusNext();

    /// Move focus to the previous surface in the focus stack.
    void focusPrev();

    // ── Window operations ───────────────────────────────────────────────

    /// Move a surface to the specified workspace index.
    void moveWindowToWorkspace(Surface* surface, int workspaceIndex);

    /// Move a surface to a different output (preserving relative position).
    void moveWindowToOutput(Surface* surface, Output* output);

    // ── Cross-monitor window movement (Task 85) ────────────────────────

    /// Move a window to the output in the given direction.
    /// Direction: "l" (left), "r" (right), "u" (up), "d" (down).
    void moveWindowToOutputDirection(Surface* surface,
                                     const std::string& direction);

    /// Move a window across monitors, adjusting geometry for scale differences
    /// and triggering focus follow + animation.
    void moveWindowCrossMonitor(Surface* surface, Output* target,
                                bool animate = true);

    /// Reference back to the owning server.
    Server& getServer() const { return m_server; }

private:
    // ── Listener callbacks ──────────────────────────────────────────────

    static void onNewOutput(struct wl_listener* listener, void* data);
    static void onNewXdgToplevel(struct wl_listener* listener, void* data);
    static void onNewXdgPopup(struct wl_listener* listener, void* data);

    // ── Data ────────────────────────────────────────────────────────────

    Server& m_server;
    wlr_xdg_shell* m_xdgShell = nullptr;

    std::vector<std::unique_ptr<Output>> m_outputs;
    std::vector<std::unique_ptr<Surface>> m_surfaces;

    Output* m_activeOutput = nullptr;
    Surface* m_focusedSurface = nullptr;

    // ── wl_listener wrappers ────────────────────────────────────────────

    struct wl_listener m_newOutputListener{};
    struct wl_listener m_newXdgToplevelListener{};
    struct wl_listener m_newXdgPopupListener{};
};

} // namespace eternal

#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

extern "C" {
#include <wayland-server-core.h>

struct wlr_xdg_toplevel;
struct wlr_xdg_surface;
struct wlr_xdg_popup;
struct wlr_subsurface;
struct wlr_scene_tree;
struct wlr_surface;
struct wlr_xdg_decoration_manager_v1;
}

namespace eternal {

class Server;
class Output;
class Surface;

/// Axis-aligned rectangle used for surface geometry.
struct SurfaceBox {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

/// Decoration rendering mode for a surface.
enum class DecorationMode : uint8_t {
    None = 0,        ///< No decorations.
    ClientSide,      ///< Client draws its own decorations.
    ServerSide,      ///< Compositor draws decorations.
};

// ---------------------------------------------------------------------------
// Popup - wraps wlr_xdg_popup for popup surfaces
// ---------------------------------------------------------------------------

class Popup {
public:
    Popup(Server& server, struct wlr_xdg_popup* popup, Surface* parent);
    ~Popup();

    Popup(const Popup&) = delete;
    Popup& operator=(const Popup&) = delete;

    /// Get the underlying wlr_xdg_popup.
    struct wlr_xdg_popup* getWlrPopup() const { return m_popup; }

    /// Get the parent surface that owns this popup.
    Surface* getParent() const { return m_parent; }

    /// Get position relative to parent surface (output-local).
    void getPosition(int& outX, int& outY) const;

    /// Reposition the popup (e.g. after parent move or constraint recalc).
    void reposition();

    /// Dismiss (close) this popup and all child popups.
    void dismiss();

    /// Get the wlr_surface for this popup's xdg_surface.
    struct wlr_surface* getWlrSurface() const;

    /// Whether this popup's surface is currently mapped.
    bool isMapped() const { return m_mapped; }

    /// Iterate this popup and its child popups, calling fn for each.
    using IterFn = std::function<void(Popup* popup)>;
    void forEachPopup(const IterFn& fn);

private:
    static void onMap(struct wl_listener* listener, void* data);
    static void onUnmap(struct wl_listener* listener, void* data);
    static void onDestroy(struct wl_listener* listener, void* data);
    static void onNewPopup(struct wl_listener* listener, void* data);
    static void onReposition(struct wl_listener* listener, void* data);

    Server& m_server;
    struct wlr_xdg_popup* m_popup = nullptr;
    Surface* m_parent = nullptr;
    bool m_mapped = false;

    std::vector<std::unique_ptr<Popup>> m_childPopups;

    struct wl_listener m_mapListener{};
    struct wl_listener m_unmapListener{};
    struct wl_listener m_destroyListener{};
    struct wl_listener m_newPopupListener{};
    struct wl_listener m_repositionListener{};
};

// ---------------------------------------------------------------------------
// SubsurfaceNode - wraps wlr_subsurface tracking
// ---------------------------------------------------------------------------

class SubsurfaceNode {
public:
    SubsurfaceNode(Server& server, struct wlr_subsurface* subsurface,
                   Surface* parentSurface);
    ~SubsurfaceNode();

    SubsurfaceNode(const SubsurfaceNode&) = delete;
    SubsurfaceNode& operator=(const SubsurfaceNode&) = delete;

    /// The underlying wlr_subsurface.
    struct wlr_subsurface* getWlrSubsurface() const { return m_subsurface; }

    /// The parent toplevel surface.
    Surface* getParentSurface() const { return m_parentSurface; }

    /// Position relative to parent surface.
    void getPosition(int& outX, int& outY) const;

    /// Whether this subsurface is in synchronized mode.
    bool isSynced() const;

    /// Whether this subsurface's surface is currently mapped.
    bool isMapped() const { return m_mapped; }

    /// Iterate this subsurface and its child subsurfaces.
    using IterFn = std::function<void(SubsurfaceNode* node)>;
    void forEachSubsurface(const IterFn& fn);

private:
    static void onMap(struct wl_listener* listener, void* data);
    static void onUnmap(struct wl_listener* listener, void* data);
    static void onDestroy(struct wl_listener* listener, void* data);
    static void onCommit(struct wl_listener* listener, void* data);
    static void onNewSubsurface(struct wl_listener* listener, void* data);

    Server& m_server;
    struct wlr_subsurface* m_subsurface = nullptr;
    Surface* m_parentSurface = nullptr;
    bool m_mapped = false;

    std::vector<std::unique_ptr<SubsurfaceNode>> m_childSubsurfaces;

    struct wl_listener m_mapListener{};
    struct wl_listener m_unmapListener{};
    struct wl_listener m_destroyListener{};
    struct wl_listener m_commitListener{};
    struct wl_listener m_newSubsurfaceListener{};
};

// ---------------------------------------------------------------------------
// Surface - the main toplevel surface wrapper
// ---------------------------------------------------------------------------

/// Represents a toplevel window / surface managed by the compositor.
class Surface {
    friend class Popup;
    friend class SubsurfaceNode;

public:
    explicit Surface(Server& server, wlr_xdg_toplevel* toplevel);
    ~Surface();

    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;
    Surface(Surface&&) = delete;
    Surface& operator=(Surface&&) = delete;

    // ── Actions ─────────────────────────────────────────────────────────

    /// Give keyboard focus to this surface.
    void focus();

    /// Request the client to close this surface.
    void close();

    /// Move the surface to absolute layout coordinates.
    void move(int x, int y);

    /// Resize the surface to the given dimensions.
    void resize(int width, int height);

    /// Set or unset fullscreen state.
    void setFullscreen(bool fullscreen);

    /// Set or unset maximized state.
    void setMaximized(bool maximized);

    /// Set or unset floating (un-tiled) state.
    void setFloating(bool floating);

    /// Set surface opacity (0.0 fully transparent, 1.0 fully opaque).
    void setOpacity(float opacity);

    /// Set activated state (window decoration hint).
    void setActivated(bool activated);

    /// Toggle real fullscreen (bypasses compositor decorations).
    void toggleFullscreen();

    /// Toggle fullscreen on a specific output.
    void fullscreenOnOutput(Output* output);

    /// Toggle maximized (fill usable area respecting bars/panels).
    void toggleMaximized();

    /// Save the current geometry for later restore.
    void saveGeometry();

    /// Restore the previously saved geometry.
    void restoreGeometry();

    /// Whether there is a saved geometry to restore.
    bool hasSavedGeometry() const { return m_hasSavedGeometry; }

    /// Get the saved geometry.
    const SurfaceBox& getSavedGeometry() const { return m_savedGeometry; }

    /// Raise this surface above all siblings.
    void toFront();

    /// Lower this surface below all siblings.
    void toBack();

    /// Pin the surface so it is visible on all workspaces.
    void pin();

    /// Unpin the surface (only visible on its assigned workspace).
    void unpin();

    // ── Configure cycle ─────────────────────────────────────────────────

    /// Send a configure event to the client with current pending state.
    /// Returns the serial of the configure event.
    uint32_t sendConfigure();

    /// Whether we are waiting for the client to ack a configure.
    bool hasPendingConfigure() const { return m_pendingConfigureSerial != 0; }

    /// Get the serial of the pending configure.
    uint32_t getPendingConfigureSerial() const { return m_pendingConfigureSerial; }

    // ── Output association ──────────────────────────────────────────────

    /// The output this surface is currently on.
    Output* getOutput() const { return m_output; }

    /// Move this surface to a different output.
    void setOutput(Output* output);

    // ── State queries ───────────────────────────────────────────────────

    bool isMapped() const { return m_mapped; }
    bool isFullscreen() const { return m_fullscreen; }
    bool isMaximized() const { return m_maximized; }
    bool isFloating() const { return m_floating; }
    bool isPinned() const { return m_pinned; }
    bool isUrgent() const { return m_urgent; }
    bool isActivated() const { return m_activated; }

    float getOpacity() const { return m_opacity; }
    DecorationMode getDecorationMode() const { return m_decorationMode; }
    void setDecorationMode(DecorationMode mode);

    int getGroupId() const { return m_groupId; }
    void setGroupId(int id) { m_groupId = id; }

    // ── Identification ──────────────────────────────────────────────────

    const std::string& getTitle() const { return m_title; }
    const std::string& getAppId() const { return m_appId; }

    // ── Geometry ────────────────────────────────────────────────────────

    const SurfaceBox& getGeometry() const { return m_geometry; }
    void setGeometry(const SurfaceBox& box);
    void setGeometry(int x, int y, int width, int height);

    /// Get the xdg_surface geometry (client-reported, excluding shadows).
    SurfaceBox getXdgGeometry() const;

    // ── Surface tree iteration ──────────────────────────────────────────

    /// Callback: (wlr_surface*, surface-local x, surface-local y)
    using SurfaceIterFn = std::function<void(struct wlr_surface* surface,
                                             int sx, int sy)>;

    /// Iterate over this surface, all subsurfaces, and all popup surfaces
    /// in bottom-to-top order suitable for rendering.
    void forEachRenderSurface(const SurfaceIterFn& fn);

    /// Iterate only subsurfaces (bottom-to-top).
    void forEachSubsurface(const SurfaceIterFn& fn);

    /// Iterate only popup surfaces (bottom-to-top).
    void forEachPopup(const SurfaceIterFn& fn);

    // ── Popup management ────────────────────────────────────────────────

    const std::vector<std::unique_ptr<Popup>>& getPopups() const { return m_popups; }

    /// Dismiss all popups belonging to this surface.
    void dismissAllPopups();

    // ── Subsurface management ───────────────────────────────────────────

    const std::vector<std::unique_ptr<SubsurfaceNode>>& getSubsurfaces() const {
        return m_subsurfaces;
    }

    // ── Underlying wlroots objects ──────────────────────────────────────

    wlr_xdg_toplevel* getToplevel() const { return m_toplevel; }
    wlr_xdg_surface* getXdgSurface() const { return m_xdgSurface; }
    struct wlr_surface* getWlrSurface() const;
    wlr_scene_tree* getSceneTree() const { return m_sceneTree; }
    void setSceneTree(wlr_scene_tree* tree) { m_sceneTree = tree; }

    /// Reference back to the owning server.
    Server& getServer() const { return m_server; }

    /// Mark the surface as urgent (e.g. requesting attention).
    void setUrgent(bool urgent) { m_urgent = urgent; }

private:
    // ── Listener callbacks ──────────────────────────────────────────────

    static void onMap(struct wl_listener* listener, void* data);
    static void onUnmap(struct wl_listener* listener, void* data);
    static void onDestroy(struct wl_listener* listener, void* data);
    static void onCommit(struct wl_listener* listener, void* data);
    static void onRequestFullscreen(struct wl_listener* listener, void* data);
    static void onRequestMaximize(struct wl_listener* listener, void* data);
    static void onRequestMove(struct wl_listener* listener, void* data);
    static void onRequestResize(struct wl_listener* listener, void* data);
    static void onSetTitle(struct wl_listener* listener, void* data);
    static void onSetAppId(struct wl_listener* listener, void* data);
    static void onNewPopup(struct wl_listener* listener, void* data);
    static void onNewSubsurface(struct wl_listener* listener, void* data);
    static void onAckConfigure(struct wl_listener* listener, void* data);

    // ── Helpers ─────────────────────────────────────────────────────────

    /// Track a new popup created under this toplevel.
    void addPopup(struct wlr_xdg_popup* popup);

    /// Track a new subsurface created under this toplevel.
    void addSubsurface(struct wlr_subsurface* subsurface);

    /// Notify the output that this surface's region is damaged.
    void damageOutput();

    // ── Data ────────────────────────────────────────────────────────────

    Server& m_server;
    wlr_xdg_toplevel* m_toplevel = nullptr;
    wlr_xdg_surface* m_xdgSurface = nullptr;
    wlr_scene_tree* m_sceneTree = nullptr;

    Output* m_output = nullptr;

    std::string m_title;
    std::string m_appId;

    SurfaceBox m_geometry{};
    SurfaceBox m_savedGeometry{};          // pre-fullscreen / pre-maximize geometry
    bool m_hasSavedGeometry = false;

    // Pending state from the last configure we sent.
    struct PendingState {
        int width = 0;
        int height = 0;
        bool fullscreen = false;
        bool maximized = false;
        bool activated = false;
    };
    PendingState m_pendingState{};
    uint32_t m_pendingConfigureSerial = 0;

    bool m_mapped = false;
    bool m_fullscreen = false;
    bool m_maximized = false;
    bool m_floating = false;
    bool m_pinned = false;
    bool m_urgent = false;
    bool m_activated = false;

    float m_opacity = 1.0f;
    DecorationMode m_decorationMode = DecorationMode::None;
    int m_groupId = 0;

    // ── Popup and subsurface tracking ────────────────────────────────────

    std::vector<std::unique_ptr<Popup>> m_popups;
    std::vector<std::unique_ptr<SubsurfaceNode>> m_subsurfaces;

    // ── wl_listener wrappers ────────────────────────────────────────────

    struct wl_listener m_mapListener{};
    struct wl_listener m_unmapListener{};
    struct wl_listener m_destroyListener{};
    struct wl_listener m_commitListener{};
    struct wl_listener m_requestFullscreenListener{};
    struct wl_listener m_requestMaximizeListener{};
    struct wl_listener m_requestMoveListener{};
    struct wl_listener m_requestResizeListener{};
    struct wl_listener m_setTitleListener{};
    struct wl_listener m_setAppIdListener{};
    struct wl_listener m_newPopupListener{};
    struct wl_listener m_newSubsurfaceListener{};
    struct wl_listener m_ackConfigureListener{};
};

// ---------------------------------------------------------------------------
// XdgDecorationManager - handles xdg-decoration-unstable-v1 protocol
// ---------------------------------------------------------------------------

class XdgDecorationManager {
public:
    explicit XdgDecorationManager(Server& server);
    ~XdgDecorationManager();

    XdgDecorationManager(const XdgDecorationManager&) = delete;
    XdgDecorationManager& operator=(const XdgDecorationManager&) = delete;

    /// Initialize and create the wlr_xdg_decoration_manager_v1.
    bool init();

    /// The preferred decoration mode the compositor advertises.
    DecorationMode getPreferredMode() const { return m_preferredMode; }
    void setPreferredMode(DecorationMode mode) { m_preferredMode = mode; }

private:
    static void onNewDecoration(struct wl_listener* listener, void* data);

    /// Per-decoration state for tracking individual toplevel decorations.
    struct DecorationState {
        XdgDecorationManager* manager = nullptr;
        struct wl_listener modeListener{};
        struct wl_listener destroyListener{};

        static void onMode(struct wl_listener* listener, void* data);
        static void onDestroy(struct wl_listener* listener, void* data);
    };

    Server& m_server;
    struct wlr_xdg_decoration_manager_v1* m_wlrManager = nullptr;
    DecorationMode m_preferredMode = DecorationMode::ServerSide;

    std::vector<std::unique_ptr<DecorationState>> m_decorations;

    struct wl_listener m_newDecorationListener{};
};

} // namespace eternal

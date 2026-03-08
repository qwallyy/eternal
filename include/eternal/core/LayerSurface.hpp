#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

extern "C" {
#include <wayland-server-core.h>
}

namespace eternal {

class Server;
class Output;
struct Surface;

// ---------------------------------------------------------------------------
// Layer shell layer identifiers (matching wlr-layer-shell-unstable-v1)
// ---------------------------------------------------------------------------

enum class Layer : uint8_t {
    Background = 0,
    Bottom     = 1,
    Top        = 2,
    Overlay    = 3,
};

// ---------------------------------------------------------------------------
// Keyboard interactivity modes
// ---------------------------------------------------------------------------

enum class KeyboardInteractivity : uint8_t {
    None      = 0,   // never receives keyboard focus
    Exclusive = 1,   // takes exclusive keyboard focus
    OnDemand  = 2,   // receives focus when clicked
};

// ---------------------------------------------------------------------------
// Edge anchors (bitfield)
// ---------------------------------------------------------------------------

enum class Anchor : uint8_t {
    None   = 0,
    Top    = 1 << 0,
    Bottom = 1 << 1,
    Left   = 1 << 2,
    Right  = 1 << 3,
};

inline Anchor operator|(Anchor a, Anchor b) {
    return static_cast<Anchor>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}

inline bool operator&(Anchor a, Anchor b) {
    return (static_cast<uint8_t>(a) & static_cast<uint8_t>(b)) != 0;
}

// ---------------------------------------------------------------------------
// Exclusive zone reservation
// ---------------------------------------------------------------------------

struct ExclusiveZone {
    int top = 0;
    int bottom = 0;
    int left = 0;
    int right = 0;
};

// ---------------------------------------------------------------------------
// Layer surface geometry
// ---------------------------------------------------------------------------

struct LayerBox {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

// ---------------------------------------------------------------------------
// LayerSurface -- represents a wlr-layer-shell surface
// ---------------------------------------------------------------------------

class LayerSurface {
public:
    LayerSurface(Server& server, Output* output);
    ~LayerSurface();

    LayerSurface(const LayerSurface&) = delete;
    LayerSurface& operator=(const LayerSurface&) = delete;

    // -- Configuration (from client commit) ---------------------------------

    void setLayer(Layer layer);
    void setAnchor(Anchor anchor);
    void setExclusiveZone(int zone);
    void setMargin(int top, int right, int bottom, int left);
    void setKeyboardInteractivity(KeyboardInteractivity mode);
    void setDesiredSize(int width, int height);
    void setNamespace(const std::string& ns);

    // -- State queries -------------------------------------------------------

    [[nodiscard]] Layer getLayer() const noexcept { return layer_; }
    [[nodiscard]] Anchor getAnchor() const noexcept { return anchor_; }
    [[nodiscard]] int getExclusiveZone() const noexcept { return exclusive_zone_; }
    [[nodiscard]] KeyboardInteractivity getKeyboardInteractivity() const noexcept {
        return keyboard_interactivity_;
    }
    [[nodiscard]] const std::string& getNamespace() const noexcept { return namespace_; }
    [[nodiscard]] Output* getOutput() const noexcept { return output_; }
    [[nodiscard]] const LayerBox& getGeometry() const noexcept { return geometry_; }
    [[nodiscard]] bool isMapped() const noexcept { return mapped_; }
    [[nodiscard]] bool wantsFocus() const noexcept;

    // -- Lifecycle -----------------------------------------------------------

    void map();
    void unmap();
    void commit();
    void destroy();

    // -- Geometry computation ------------------------------------------------

    /// Arrange this layer surface within the given output bounds,
    /// respecting anchoring, margins, and exclusive zones.
    /// Updates geometry_ in place.
    void arrange(LayerBox output_bounds, ExclusiveZone& exclusive);

    /// Compute the exclusive zone reservation this surface makes.
    [[nodiscard]] ExclusiveZone computeExclusiveReservation() const;

    /// Get the usable area after all exclusive zones on an output are applied.
    static LayerBox computeUsableArea(const LayerBox& output_bounds,
                                       const ExclusiveZone& exclusive);

    // -- Server reference ----------------------------------------------------

    Server& getServer() const { return server_; }

private:
    Server& server_;
    Output* output_ = nullptr;

    Layer layer_ = Layer::Top;
    Anchor anchor_ = Anchor::None;
    int exclusive_zone_ = 0;
    KeyboardInteractivity keyboard_interactivity_ = KeyboardInteractivity::None;

    int margin_top_ = 0;
    int margin_right_ = 0;
    int margin_bottom_ = 0;
    int margin_left_ = 0;

    int desired_width_ = 0;
    int desired_height_ = 0;

    std::string namespace_;
    LayerBox geometry_{};
    bool mapped_ = false;

    // wl_listener wrappers.
    struct wl_listener map_listener_{};
    struct wl_listener unmap_listener_{};
    struct wl_listener destroy_listener_{};
    struct wl_listener commit_listener_{};
    struct wl_listener new_popup_listener_{};
};

// ---------------------------------------------------------------------------
// LayerShellManager -- manages all layer surfaces across outputs
// ---------------------------------------------------------------------------

class LayerShellManager {
public:
    explicit LayerShellManager(Server& server);
    ~LayerShellManager();

    /// Handle a new layer surface from wlr-layer-shell.
    void addSurface(std::unique_ptr<LayerSurface> surface);

    /// Remove a layer surface.
    void removeSurface(LayerSurface* surface);

    /// Get all layer surfaces for an output and layer.
    [[nodiscard]] std::vector<LayerSurface*> getSurfaces(Output* output, Layer layer) const;

    /// Get all layer surfaces for an output across all layers.
    [[nodiscard]] std::vector<LayerSurface*> getAllSurfaces(Output* output) const;

    /// Arrange all layer surfaces on an output and return the usable area
    /// (the area remaining after exclusive zones are reserved).
    LayerBox arrangeOutput(Output* output, LayerBox output_bounds);

    /// Get the current exclusive zone reservation for an output.
    [[nodiscard]] ExclusiveZone getExclusiveZone(Output* output) const;

    /// Find the layer surface that currently holds exclusive keyboard focus.
    [[nodiscard]] LayerSurface* getExclusiveKeyboardSurface(Output* output) const;

private:
    Server& server_;
    std::vector<std::unique_ptr<LayerSurface>> surfaces_;
};

} // namespace eternal

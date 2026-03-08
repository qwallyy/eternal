#include "eternal/core/LayerSurface.hpp"
#include "eternal/utils/Logger.hpp"

#include <algorithm>

namespace eternal {

// ---------------------------------------------------------------------------
// LayerSurface implementation
// ---------------------------------------------------------------------------

LayerSurface::LayerSurface(Server& server, Output* output)
    : server_(server), output_(output) {}

LayerSurface::~LayerSurface() {
    if (mapped_) unmap();
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void LayerSurface::setLayer(Layer layer) {
    layer_ = layer;
}

void LayerSurface::setAnchor(Anchor anchor) {
    anchor_ = anchor;
}

void LayerSurface::setExclusiveZone(int zone) {
    exclusive_zone_ = zone;
}

void LayerSurface::setMargin(int top, int right, int bottom, int left) {
    margin_top_ = top;
    margin_right_ = right;
    margin_bottom_ = bottom;
    margin_left_ = left;
}

void LayerSurface::setKeyboardInteractivity(KeyboardInteractivity mode) {
    keyboard_interactivity_ = mode;
}

void LayerSurface::setDesiredSize(int width, int height) {
    desired_width_ = width;
    desired_height_ = height;
}

void LayerSurface::setNamespace(const std::string& ns) {
    namespace_ = ns;
}

bool LayerSurface::wantsFocus() const noexcept {
    return keyboard_interactivity_ != KeyboardInteractivity::None;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void LayerSurface::map() {
    mapped_ = true;
    LOG_DEBUG("LayerSurface mapped (ns='{}', layer={})", namespace_,
              static_cast<int>(layer_));
}

void LayerSurface::unmap() {
    mapped_ = false;
    LOG_DEBUG("LayerSurface unmapped (ns='{}')", namespace_);
}

void LayerSurface::commit() {
    // Re-arrange when the client commits new state.
    // In production, the manager would re-arrange all surfaces on this output.
    LOG_DEBUG("LayerSurface committed (ns='{}')", namespace_);
}

void LayerSurface::destroy() {
    if (mapped_) unmap();
    LOG_DEBUG("LayerSurface destroyed (ns='{}')", namespace_);
}

// ---------------------------------------------------------------------------
// Geometry arrangement
// ---------------------------------------------------------------------------

void LayerSurface::arrange(LayerBox output_bounds, ExclusiveZone& exclusive) {
    // Apply exclusive zone reservations from other surfaces first.
    LayerBox usable = computeUsableArea(output_bounds, exclusive);

    int x = usable.x;
    int y = usable.y;
    int w = desired_width_ > 0 ? desired_width_ : usable.width;
    int h = desired_height_ > 0 ? desired_height_ : usable.height;

    bool anchor_left   = anchor_ & Anchor::Left;
    bool anchor_right  = anchor_ & Anchor::Right;
    bool anchor_top    = anchor_ & Anchor::Top;
    bool anchor_bottom = anchor_ & Anchor::Bottom;

    // Horizontal positioning.
    if (anchor_left && anchor_right) {
        // Stretch horizontally.
        x = usable.x + margin_left_;
        w = usable.width - margin_left_ - margin_right_;
    } else if (anchor_left) {
        x = usable.x + margin_left_;
    } else if (anchor_right) {
        x = usable.x + usable.width - w - margin_right_;
    } else {
        // Center horizontally.
        x = usable.x + (usable.width - w) / 2;
    }

    // Vertical positioning.
    if (anchor_top && anchor_bottom) {
        // Stretch vertically.
        y = usable.y + margin_top_;
        h = usable.height - margin_top_ - margin_bottom_;
    } else if (anchor_top) {
        y = usable.y + margin_top_;
    } else if (anchor_bottom) {
        y = usable.y + usable.height - h - margin_bottom_;
    } else {
        // Center vertically.
        y = usable.y + (usable.height - h) / 2;
    }

    geometry_ = {x, y, std::max(1, w), std::max(1, h)};

    // Apply exclusive zone reservation.
    if (exclusive_zone_ > 0) {
        auto reservation = computeExclusiveReservation();
        exclusive.top    += reservation.top;
        exclusive.bottom += reservation.bottom;
        exclusive.left   += reservation.left;
        exclusive.right  += reservation.right;
    }
}

ExclusiveZone LayerSurface::computeExclusiveReservation() const {
    ExclusiveZone reservation{};
    if (exclusive_zone_ <= 0) return reservation;

    bool anchor_left   = anchor_ & Anchor::Left;
    bool anchor_right  = anchor_ & Anchor::Right;
    bool anchor_top    = anchor_ & Anchor::Top;
    bool anchor_bottom = anchor_ & Anchor::Bottom;

    // A surface anchored to a single edge reserves space on that edge.
    if (anchor_top && !anchor_bottom) {
        reservation.top = exclusive_zone_ + margin_top_;
    } else if (anchor_bottom && !anchor_top) {
        reservation.bottom = exclusive_zone_ + margin_bottom_;
    } else if (anchor_left && !anchor_right) {
        reservation.left = exclusive_zone_ + margin_left_;
    } else if (anchor_right && !anchor_left) {
        reservation.right = exclusive_zone_ + margin_right_;
    }
    // Surfaces anchored to opposite edges (or all edges) don't reserve.

    return reservation;
}

LayerBox LayerSurface::computeUsableArea(const LayerBox& output_bounds,
                                          const ExclusiveZone& exclusive) {
    return {
        output_bounds.x + exclusive.left,
        output_bounds.y + exclusive.top,
        std::max(1, output_bounds.width - exclusive.left - exclusive.right),
        std::max(1, output_bounds.height - exclusive.top - exclusive.bottom),
    };
}

// ---------------------------------------------------------------------------
// LayerShellManager implementation
// ---------------------------------------------------------------------------

LayerShellManager::LayerShellManager(Server& server) : server_(server) {}
LayerShellManager::~LayerShellManager() = default;

void LayerShellManager::addSurface(std::unique_ptr<LayerSurface> surface) {
    if (surface) {
        surfaces_.push_back(std::move(surface));
    }
}

void LayerShellManager::removeSurface(LayerSurface* surface) {
    std::erase_if(surfaces_, [surface](const auto& s) {
        return s.get() == surface;
    });
}

std::vector<LayerSurface*> LayerShellManager::getSurfaces(Output* output, Layer layer) const {
    std::vector<LayerSurface*> result;
    for (auto& s : surfaces_) {
        if (s->getOutput() == output && s->getLayer() == layer && s->isMapped()) {
            result.push_back(s.get());
        }
    }
    return result;
}

std::vector<LayerSurface*> LayerShellManager::getAllSurfaces(Output* output) const {
    std::vector<LayerSurface*> result;
    for (auto& s : surfaces_) {
        if (s->getOutput() == output && s->isMapped()) {
            result.push_back(s.get());
        }
    }
    return result;
}

LayerBox LayerShellManager::arrangeOutput(Output* output, LayerBox output_bounds) {
    ExclusiveZone exclusive{};

    // Arrange layers in order: background, bottom, top, overlay.
    // Exclusive zones accumulate.
    constexpr Layer layer_order[] = {
        Layer::Background,
        Layer::Bottom,
        Layer::Top,
        Layer::Overlay,
    };

    for (auto layer : layer_order) {
        auto surfaces = getSurfaces(output, layer);
        for (auto* surface : surfaces) {
            surface->arrange(output_bounds, exclusive);
        }
    }

    return LayerSurface::computeUsableArea(output_bounds, exclusive);
}

ExclusiveZone LayerShellManager::getExclusiveZone(Output* output) const {
    ExclusiveZone total{};
    for (auto& s : surfaces_) {
        if (s->getOutput() == output && s->isMapped() && s->getExclusiveZone() > 0) {
            auto reservation = s->computeExclusiveReservation();
            total.top    += reservation.top;
            total.bottom += reservation.bottom;
            total.left   += reservation.left;
            total.right  += reservation.right;
        }
    }
    return total;
}

LayerSurface* LayerShellManager::getExclusiveKeyboardSurface(Output* output) const {
    // Return the highest-layer surface with exclusive keyboard focus.
    // Check overlay first, then top, bottom, background.
    constexpr Layer layer_order[] = {
        Layer::Overlay, Layer::Top, Layer::Bottom, Layer::Background,
    };
    for (auto layer : layer_order) {
        auto surfaces = getSurfaces(output, layer);
        for (auto* s : surfaces) {
            if (s->getKeyboardInteractivity() == KeyboardInteractivity::Exclusive) {
                return s;
            }
        }
    }
    return nullptr;
}

} // namespace eternal

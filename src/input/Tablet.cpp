#include "eternal/input/Tablet.hpp"
#include "eternal/input/InputManager.hpp"
#include "eternal/utils/Logger.hpp"

#include "eternal/utils/WlrSceneCompat.h"

extern "C" {
#include <wlr/types/wlr_tablet_v2.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_output_layout.h>
}

#include <cassert>
#include <cmath>

namespace eternal {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

Tablet::Tablet(InputManager* manager)
    : manager_(manager)
{
    assert(manager_);
}

Tablet::~Tablet() {
    if (wlrTablet_) {
        wl_list_remove(&axisListener_.link);
        wl_list_remove(&proximityListener_.link);
        wl_list_remove(&tipListener_.link);
        wl_list_remove(&buttonListener_.link);
        wl_list_remove(&destroyListener_.link);
    }
}

// ---------------------------------------------------------------------------
// Task 27: wp_tablet_v2 protocol initialization
// ---------------------------------------------------------------------------

void Tablet::initTabletV2(wl_display* display) {
    if (tabletManagerV2_) return;  // Already initialized

    tabletManagerV2_ = wlr_tablet_v2_create(display);
    if (!tabletManagerV2_) {
        LOG_ERROR("Failed to create wlr_tablet_manager_v2");
        return;
    }

    LOG_INFO("wp_tablet_v2 protocol manager created");
}

// ---------------------------------------------------------------------------
// Device attachment
// ---------------------------------------------------------------------------

void Tablet::attachDevice(wlr_tablet* tablet) {
    if (wlrTablet_) {
        wl_list_remove(&axisListener_.link);
        wl_list_remove(&proximityListener_.link);
        wl_list_remove(&tipListener_.link);
        wl_list_remove(&buttonListener_.link);
        wl_list_remove(&destroyListener_.link);
    }

    wlrTablet_ = tablet;

    axisListener_.notify = handleAxisEvent;
    wl_signal_add(&wlrTablet_->events.axis, &axisListener_);

    proximityListener_.notify = handleProximityEvent;
    wl_signal_add(&wlrTablet_->events.proximity, &proximityListener_);

    tipListener_.notify = handleTipEvent;
    wl_signal_add(&wlrTablet_->events.tip, &tipListener_);

    buttonListener_.notify = handleButtonEvent;
    wl_signal_add(&wlrTablet_->events.button, &buttonListener_);

    destroyListener_.notify = handleDestroy;
    wl_signal_add(&wlrTablet_->base.events.destroy, &destroyListener_);

    // Create tablet v2 object if manager is ready
    if (tabletManagerV2_ && !tabletV2_) {
        tabletV2_ = wlr_tablet_create(tabletManagerV2_,
                                       manager_->getSeat(), &wlrTablet_->base);
        if (tabletV2_) {
            LOG_INFO("Created wp_tablet_v2 tablet object");
        }
    }

    LOG_INFO("Tablet device attached");
}

// ---------------------------------------------------------------------------
// Task 27: Get or create per-tool v2 state
// ---------------------------------------------------------------------------

TabletToolState& Tablet::getOrCreateToolState(wlr_tablet_tool* tool) {
    auto it = toolStates_.find(tool);
    if (it != toolStates_.end()) {
        return it->second;
    }

    TabletToolState state;

    // Create v2 tool if we have a tablet v2 object
    if (tabletManagerV2_ && tabletV2_) {
        state.v2Tool = wlr_tablet_tool_create(tabletManagerV2_,
                                               manager_->getSeat(), tool);
    }

    auto [inserted, _] = toolStates_.emplace(tool, state);
    return inserted->second;
}

// ---------------------------------------------------------------------------
// Task 27: Coordinate mapping
// ---------------------------------------------------------------------------

void Tablet::mapToOutput(double& x, double& y) const {
    if (!outputMapping_) return;

    // Get the output's position in the layout
    wlr_output_layout* layout = manager_->getOutputLayout();
    if (!layout) return;

    auto* layoutOutput = wlr_output_layout_get(layout, outputMapping_);
    if (!layoutOutput) return;

    int width = 0, height = 0;
    wlr_output_effective_resolution(outputMapping_, &width, &height);

    // Map from [0, 1] tablet space to output layout space
    x = layoutOutput->x + x * static_cast<double>(width);
    y = layoutOutput->y + y * static_cast<double>(height);
}

void Tablet::transformCoordinates(double& x, double& y) const {
    // Apply area mapping: map from [0,1] tablet space to the configured area
    x = areaMapping_.x + x * areaMapping_.width;
    y = areaMapping_.y + y * areaMapping_.height;

    // Apply rotation around center of mapped area
    if (std::abs(rotation_) > 0.001) {
        double centerX = areaMapping_.x + areaMapping_.width / 2.0;
        double centerY = areaMapping_.y + areaMapping_.height / 2.0;

        double rad = rotation_ * M_PI / 180.0;
        double cosR = std::cos(rad);
        double sinR = std::sin(rad);

        double relX = x - centerX;
        double relY = y - centerY;

        x = centerX + relX * cosR - relY * sinR;
        y = centerY + relX * sinR + relY * cosR;
    }
}

// ---------------------------------------------------------------------------
// Task 27: Forward axis events through tablet v2 protocol
// ---------------------------------------------------------------------------

void Tablet::forwardAxisV2(double x, double y, double pressure,
                            double tiltX, double tiltY, wlr_tablet_tool* tool) {
    if (!tabletManagerV2_ || !tabletV2_ || !tool) return;

    auto& toolState = getOrCreateToolState(tool);
    if (!toolState.v2Tool) return;

    // Find surface under the tablet position
    wlr_scene* scene = manager_->getScene();
    if (!scene) return;

    double sx = 0, sy = 0;
    wlr_scene_node* node = wlr_scene_node_at(&scene->tree.node, x, y, &sx, &sy);

    wlr_surface* surface = nullptr;
    if (node && node->type == WLR_SCENE_NODE_BUFFER) {
        auto* sceneBuf = wlr_scene_buffer_from_node(node);
        auto* sceneSurface = wlr_scene_surface_try_from_buffer(sceneBuf);
        if (sceneSurface) {
            surface = sceneSurface->surface;
        }
    }

    // Handle surface enter/leave
    if (surface != toolState.currentSurface) {
        if (toolState.currentSurface) {
            wlr_send_tablet_v2_tablet_tool_proximity_out(toolState.v2Tool);
        }
        if (surface) {
            wlr_send_tablet_v2_tablet_tool_proximity_in(toolState.v2Tool,
                                                         tabletV2_, surface);
        }
        toolState.currentSurface = surface;
    }

    if (surface) {
        wlr_send_tablet_v2_tablet_tool_motion(toolState.v2Tool, sx, sy);
        wlr_send_tablet_v2_tablet_tool_pressure(toolState.v2Tool, pressure);
        wlr_send_tablet_v2_tablet_tool_tilt(toolState.v2Tool, tiltX, tiltY);

    }

    toolState.lastX = x;
    toolState.lastY = y;
}

// ---------------------------------------------------------------------------
// Task 27: Event handlers
// ---------------------------------------------------------------------------

void Tablet::handleAxisEvent(wl_listener* listener, void* data) {
    Tablet* self = wl_container_of(listener, self, axisListener_);
    auto* event = static_cast<wlr_tablet_tool_axis_event*>(data);

    double x = event->x;
    double y = event->y;
    double pressure = event->pressure;
    double tiltX = event->tilt_x;
    double tiltY = event->tilt_y;

    // Apply area mapping and rotation
    self->transformCoordinates(x, y);

    // Map to output layout coordinates if output mapping is set
    self->mapToOutput(x, y);

    // Forward through v2 protocol
    self->forwardAxisV2(x, y, pressure, tiltX, tiltY, event->tool);

    // Internal callback
    self->handleAxis(x, y, pressure, tiltX, tiltY);
}

void Tablet::handleProximityEvent(wl_listener* listener, void* data) {
    Tablet* self = wl_container_of(listener, self, proximityListener_);
    auto* event = static_cast<wlr_tablet_tool_proximity_event*>(data);

    auto state = (event->state == WLR_TABLET_TOOL_PROXIMITY_IN)
        ? ProximityState::In : ProximityState::Out;

    double x = event->x;
    double y = event->y;
    self->transformCoordinates(x, y);
    self->mapToOutput(x, y);

    // Handle v2 proximity
    if (self->tabletManagerV2_ && event->tool) {
        auto& toolState = self->getOrCreateToolState(event->tool);
        if (state == ProximityState::Out && toolState.v2Tool) {
            if (toolState.currentSurface) {
                wlr_send_tablet_v2_tablet_tool_proximity_out(toolState.v2Tool);
                toolState.currentSurface = nullptr;
            }
        }
    }

    self->handleProximity(state, x, y, event->tool);
}

void Tablet::handleTipEvent(wl_listener* listener, void* data) {
    Tablet* self = wl_container_of(listener, self, tipListener_);
    auto* event = static_cast<wlr_tablet_tool_tip_event*>(data);

    auto state = (event->state == WLR_TABLET_TOOL_TIP_DOWN)
        ? TipState::Down : TipState::Up;

    double x = event->x;
    double y = event->y;
    self->transformCoordinates(x, y);
    self->mapToOutput(x, y);

    // Forward tip event through v2 protocol
    if (self->tabletManagerV2_ && event->tool) {
        auto& toolState = self->getOrCreateToolState(event->tool);
        if (toolState.v2Tool && toolState.currentSurface) {
            if (state == TipState::Down) {
                wlr_send_tablet_v2_tablet_tool_down(toolState.v2Tool);
            } else {
                wlr_send_tablet_v2_tablet_tool_up(toolState.v2Tool);
            }
    
        }
        toolState.tipDown = (state == TipState::Down);
    }

    self->handleTip(state, x, y, event->tool);
}

void Tablet::handleButtonEvent(wl_listener* listener, void* data) {
    Tablet* self = wl_container_of(listener, self, buttonListener_);
    auto* event = static_cast<wlr_tablet_tool_button_event*>(data);

    // Forward button through v2 protocol
    if (self->tabletManagerV2_ && event->tool) {
        auto& toolState = self->getOrCreateToolState(event->tool);
        if (toolState.v2Tool && toolState.currentSurface) {
            wlr_send_tablet_v2_tablet_tool_button(toolState.v2Tool,
                event->button, static_cast<zwp_tablet_pad_v2_button_state>(event->state));
    
        }
    }

    self->handleButton(event->button, static_cast<wlr_button_state>(event->state),
                       event->tool);
}

void Tablet::handleDestroy(wl_listener* listener, void* /*data*/) {
    Tablet* self = wl_container_of(listener, self, destroyListener_);

    // Clean up v2 tool states
    for (auto& [tool, state] : self->toolStates_) {
        if (state.currentSurface && state.v2Tool) {
            wlr_send_tablet_v2_tablet_tool_proximity_out(state.v2Tool);
        }
    }
    self->toolStates_.clear();

    wl_list_remove(&self->axisListener_.link);
    wl_list_remove(&self->proximityListener_.link);
    wl_list_remove(&self->tipListener_.link);
    wl_list_remove(&self->buttonListener_.link);
    wl_list_remove(&self->destroyListener_.link);
    self->wlrTablet_ = nullptr;
    self->tabletV2_ = nullptr;
}

// ---------------------------------------------------------------------------
// Internal event handlers (callback dispatching)
// ---------------------------------------------------------------------------

void Tablet::handleAxis(double x, double y, double pressure,
                         double tiltX, double tiltY) {
    if (axisCallback_) {
        axisCallback_(TabletAxisEvent{x, y, pressure, tiltX, tiltY, 0.0, 0.0, 0.0});
    }
}

void Tablet::handleProximity(ProximityState state, double /*x*/, double /*y*/,
                              wlr_tablet_tool* /*tool*/) {
    inProximity_ = (state == ProximityState::In);

    if (proximityCallback_) {
        proximityCallback_(state);
    }
}

void Tablet::handleTip(TipState state, double /*x*/, double /*y*/,
                        wlr_tablet_tool* /*tool*/) {
    tipDown_ = (state == TipState::Down);

    if (tipCallback_) {
        tipCallback_(state);
    }
}

void Tablet::handleButton(uint32_t button, wlr_button_state state,
                           wlr_tablet_tool* /*tool*/) {
    if (buttonCallback_) {
        buttonCallback_(button, state);
    }
}

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

void Tablet::setOutputMapping(wlr_output* output) { outputMapping_ = output; }
void Tablet::setAreaMapping(const AreaMapping& area) { areaMapping_ = area; }
void Tablet::setRotation(double degrees) { rotation_ = degrees; }

void Tablet::setAxisCallback(AxisCallback callback) { axisCallback_ = std::move(callback); }
void Tablet::setProximityCallback(ProximityCallback callback) { proximityCallback_ = std::move(callback); }
void Tablet::setTipCallback(TipCallback callback) { tipCallback_ = std::move(callback); }
void Tablet::setButtonCallback(ButtonCallback callback) { buttonCallback_ = std::move(callback); }

} // namespace eternal

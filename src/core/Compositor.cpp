#include "eternal/core/Compositor.hpp"
#include "eternal/core/Server.hpp"
#include "eternal/core/Output.hpp"
#include "eternal/core/OutputManager.hpp"
#include "eternal/core/Surface.hpp"
#include "eternal/workspace/WorkspaceManager.hpp"
#include "eternal/utils/Logger.hpp"

extern "C" {
#include <wayland-server-core.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/render/wlr_renderer.h>
}

#include <algorithm>
#include <cmath>
#include <string>

namespace eternal {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

Compositor::Compositor(Server& server)
    : m_server(server) {}

Compositor::~Compositor() = default;

bool Compositor::init() {
    LOG_INFO("Compositor initialised");
    return true;
}

// ---------------------------------------------------------------------------
// Output management
// ---------------------------------------------------------------------------

Output* Compositor::createOutput(wlr_output* wlrOutput) {
    if (!wlrOutput) return nullptr;

    auto output = std::make_unique<Output>(m_server, wlrOutput);
    Output* ptr = output.get();
    m_outputs.push_back(std::move(output));

    if (!m_activeOutput) {
        m_activeOutput = ptr;
    }

    LOG_INFO("Compositor: created output '{}'", ptr->getName());
    return ptr;
}

void Compositor::destroyOutput(Output* output) {
    if (!output) return;

    LOG_INFO("Compositor: destroying output '{}'", output->getName());

    m_server.getOutputManager().handleOutputDestroy(output);

    if (m_activeOutput == output) {
        m_activeOutput = nullptr;
        for (auto& o : m_outputs) {
            if (o.get() != output) {
                m_activeOutput = o.get();
                break;
            }
        }
    }

    for (auto& surfPtr : m_surfaces) {
        if (surfPtr->getOutput() == output) {
            surfPtr->setOutput(nullptr);
        }
    }

    std::erase_if(m_outputs, [output](const auto& ptr) {
        return ptr.get() == output;
    });
}

Output* Compositor::getOutputAt(double layoutX, double layoutY) const {
    for (auto& output : m_outputs) {
        const auto& box = output->getBox();
        if (layoutX >= box.x && layoutX < box.x + box.width &&
            layoutY >= box.y && layoutY < box.y + box.height) {
            return output.get();
        }
    }
    return nullptr;
}

void Compositor::arrangeOutputs() {
    // Simple left-to-right arrangement.
    int x = 0;
    for (auto& output : m_outputs) {
        auto* layout = m_server.getOutputLayout();
        if (layout) {
            wlr_output_layout_add(layout, output->getWlrOutput(), x, 0);
        }
        x += output->getBox().width;
    }
    LOG_DEBUG("Compositor: arranged {} outputs", m_outputs.size());
}

// ---------------------------------------------------------------------------
// Surface management
// ---------------------------------------------------------------------------

Surface* Compositor::createSurface(wlr_xdg_toplevel* toplevel) {
    if (!toplevel) return nullptr;

    auto surface = std::make_unique<Surface>(m_server, toplevel);
    Surface* ptr = surface.get();
    m_surfaces.push_back(std::move(surface));

    // Assign to active output.
    if (m_activeOutput) {
        ptr->setOutput(m_activeOutput);
    }

    return ptr;
}

void Compositor::destroySurface(Surface* surface) {
    if (!surface) return;

    if (m_focusedSurface == surface) {
        m_focusedSurface = nullptr;
    }

    auto workspaces = m_server.getWorkspaceManager().getAllWorkspaces();
    for (auto* ws : workspaces) {
        ws->removeWindow(surface);
    }

    std::erase_if(m_surfaces, [surface](const auto& ptr) {
        return ptr.get() == surface;
    });
}

Surface* Compositor::getSurfaceAt(double layoutX, double layoutY,
                                   double* sx, double* sy) const {
    for (auto it = m_surfaces.rbegin(); it != m_surfaces.rend(); ++it) {
        auto& surface = *it;
        if (!surface->isMapped()) continue;

        const auto& geo = surface->getGeometry();
        if (layoutX >= geo.x && layoutX < geo.x + geo.width &&
            layoutY >= geo.y && layoutY < geo.y + geo.height) {
            if (sx) *sx = layoutX - geo.x;
            if (sy) *sy = layoutY - geo.y;
            return surface.get();
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Focus
// ---------------------------------------------------------------------------

void Compositor::setFocusedSurface(Surface* surface) {
    if (m_focusedSurface == surface) return;

    if (m_focusedSurface) {
        m_focusedSurface->setActivated(false);
    }

    m_focusedSurface = surface;

    if (surface) {
        surface->setActivated(true);
        surface->focus();
    }
}

void Compositor::focusNext() {
    if (m_surfaces.empty()) return;

    auto it = std::find_if(m_surfaces.begin(), m_surfaces.end(),
        [this](const auto& s) { return s.get() == m_focusedSurface; });

    if (it != m_surfaces.end()) {
        ++it;
        if (it == m_surfaces.end()) it = m_surfaces.begin();
        setFocusedSurface(it->get());
    }
}

void Compositor::focusPrev() {
    if (m_surfaces.empty()) return;

    auto it = std::find_if(m_surfaces.begin(), m_surfaces.end(),
        [this](const auto& s) { return s.get() == m_focusedSurface; });

    if (it != m_surfaces.end()) {
        if (it == m_surfaces.begin()) it = m_surfaces.end();
        --it;
        setFocusedSurface(it->get());
    }
}

// ---------------------------------------------------------------------------
// Window operations
// ---------------------------------------------------------------------------

void Compositor::moveWindowToWorkspace(Surface* surface, int workspaceIndex) {
    if (!surface) return;
    auto& wsMgr = m_server.getWorkspaceManager();
    wsMgr.moveWindowTo(surface, static_cast<WorkspaceID>(workspaceIndex));
}

void Compositor::moveWindowToOutput(Surface* surface, Output* output) {
    if (!surface || !output) return;

    Output* oldOutput = surface->getOutput();
    if (oldOutput == output) return;

    // Adjust position: preserve relative position within the output.
    const auto& oldGeo = surface->getGeometry();
    const auto& oldBox = oldOutput ? oldOutput->getBox() : OutputBox{};
    const auto& newBox = output->getBox();

    // Compute relative position within old output.
    double relX = oldBox.width > 0
        ? static_cast<double>(oldGeo.x - oldBox.x) / oldBox.width
        : 0.0;
    double relY = oldBox.height > 0
        ? static_cast<double>(oldGeo.y - oldBox.y) / oldBox.height
        : 0.0;

    // Map to new output.
    int newX = newBox.x + static_cast<int>(relX * newBox.width);
    int newY = newBox.y + static_cast<int>(relY * newBox.height);

    surface->setOutput(output);
    surface->move(newX, newY);

    LOG_DEBUG("Compositor: moved window '{}' to output '{}'",
              surface->getTitle(), output->getName());
}

// ---------------------------------------------------------------------------
// Cross-monitor window movement (Task 85)
// ---------------------------------------------------------------------------

void Compositor::moveWindowToOutputDirection(Surface* surface,
                                              const std::string& direction) {
    if (!surface) return;

    Output* current = surface->getOutput();
    if (!current) return;

    const auto& box = current->getBox();
    Output* target = nullptr;

    if (direction == "l" || direction == "left") {
        // Find the output whose right edge is closest to our left edge.
        int bestDist = INT32_MAX;
        for (auto& o : m_outputs) {
            if (o.get() == current) continue;
            const auto& ob = o->getBox();
            int rightEdge = ob.x + ob.width;
            if (rightEdge <= box.x) {
                int dist = box.x - rightEdge;
                if (dist < bestDist) {
                    bestDist = dist;
                    target = o.get();
                }
            }
        }
    } else if (direction == "r" || direction == "right") {
        int bestDist = INT32_MAX;
        for (auto& o : m_outputs) {
            if (o.get() == current) continue;
            const auto& ob = o->getBox();
            if (ob.x >= box.x + box.width) {
                int dist = ob.x - (box.x + box.width);
                if (dist < bestDist) {
                    bestDist = dist;
                    target = o.get();
                }
            }
        }
    } else if (direction == "u" || direction == "up") {
        int bestDist = INT32_MAX;
        for (auto& o : m_outputs) {
            if (o.get() == current) continue;
            const auto& ob = o->getBox();
            int bottomEdge = ob.y + ob.height;
            if (bottomEdge <= box.y) {
                int dist = box.y - bottomEdge;
                if (dist < bestDist) {
                    bestDist = dist;
                    target = o.get();
                }
            }
        }
    } else if (direction == "d" || direction == "down") {
        int bestDist = INT32_MAX;
        for (auto& o : m_outputs) {
            if (o.get() == current) continue;
            const auto& ob = o->getBox();
            if (ob.y >= box.y + box.height) {
                int dist = ob.y - (box.y + box.height);
                if (dist < bestDist) {
                    bestDist = dist;
                    target = o.get();
                }
            }
        }
    }

    if (target) {
        moveWindowCrossMonitor(surface, target, true);
    }
}

void Compositor::moveWindowCrossMonitor(Surface* surface, Output* target,
                                         bool animate) {
    if (!surface || !target) return;

    Output* source = surface->getOutput();
    if (source == target) return;

    const auto& oldGeo = surface->getGeometry();
    const auto& srcBox = source ? source->getBox() : OutputBox{};
    const auto& dstBox = target->getBox();

    // Adjust window geometry for different output scales.
    float srcScale = source ? source->getScale() : 1.0f;
    float dstScale = target->getScale();
    float scaleRatio = dstScale / srcScale;

    // Compute new dimensions adjusted for scale difference.
    int newWidth  = static_cast<int>(oldGeo.width / scaleRatio);
    int newHeight = static_cast<int>(oldGeo.height / scaleRatio);

    // Compute position: center the window relative to cursor or
    // place at same relative position.
    double relX = srcBox.width > 0
        ? static_cast<double>(oldGeo.x - srcBox.x) / srcBox.width
        : 0.5;
    double relY = srcBox.height > 0
        ? static_cast<double>(oldGeo.y - srcBox.y) / srcBox.height
        : 0.5;

    int newX = dstBox.x + static_cast<int>(relX * dstBox.width);
    int newY = dstBox.y + static_cast<int>(relY * dstBox.height);

    // Clamp to target output bounds.
    newX = std::clamp(newX, dstBox.x, dstBox.x + dstBox.width - newWidth);
    newY = std::clamp(newY, dstBox.y, dstBox.y + dstBox.height - newHeight);

    // Damage old position on source output.
    if (source) {
        source->damageSurface(surface);
    }

    // Update surface state.
    surface->setOutput(target);
    surface->move(newX, newY);
    surface->resize(newWidth, newHeight);

    // Damage new position on target output.
    target->damageSurface(surface);

    // Focus follows window to new monitor.
    m_activeOutput = target;
    setFocusedSurface(surface);

    // TODO: if animate, trigger cross-monitor move animation via
    // AnimationEngine (slide from source to destination output).
    (void)animate;

    LOG_INFO("Compositor: moved window '{}' from '{}' to '{}' "
             "(scale {:.2f} -> {:.2f})",
             surface->getTitle(),
             source ? source->getName() : "none",
             target->getName(), srcScale, dstScale);
}

// ---------------------------------------------------------------------------
// Listener callbacks
// ---------------------------------------------------------------------------

void Compositor::onNewOutput(struct wl_listener* listener, void* data) {
    Compositor* self = wl_container_of(listener, self, m_newOutputListener);
    auto* wlrOutput = static_cast<wlr_output*>(data);
    self->createOutput(wlrOutput);
}

void Compositor::onNewXdgToplevel(struct wl_listener* listener, void* data) {
    Compositor* self = wl_container_of(listener, self, m_newXdgToplevelListener);
    auto* toplevel = static_cast<wlr_xdg_toplevel*>(data);
    self->createSurface(toplevel);
}

void Compositor::onNewXdgPopup(struct wl_listener* listener, void* data) {
    Compositor* self = wl_container_of(listener, self, m_newXdgPopupListener);
    (void)data;
    (void)self;
    // TODO: handle popup creation.
}

} // namespace eternal

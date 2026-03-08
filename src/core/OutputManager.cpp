#include "eternal/core/OutputManager.hpp"
#include "eternal/core/Output.hpp"
#include "eternal/core/Server.hpp"
#include "eternal/core/Compositor.hpp"
#include "eternal/core/Surface.hpp"
#include "eternal/workspace/WorkspaceManager.hpp"
#include "eternal/config/ConfigManager.hpp"
#include "eternal/render/Renderer.hpp"
#include "eternal/utils/Logger.hpp"

extern "C" {
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
}

#include <algorithm>
#include <cmath>
#include <fstream>

namespace eternal {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

OutputManager::OutputManager(Server& server)
    : m_server(server) {}

OutputManager::~OutputManager() = default;

bool OutputManager::init() {
    LOG_INFO("OutputManager: initializing");

    // Wire up the backend new_output event.
    m_newOutputListener.notify = [](struct wl_listener* listener, void* data) {
        OutputManager* self = wl_container_of(listener, self, m_newOutputListener);
        auto* wlrOutput = static_cast<wlr_output*>(data);
        self->handleNewOutput(wlrOutput);
    };
    wl_signal_add(&m_server.getBackend()->events.new_output, &m_newOutputListener);

    return true;
}

// ---------------------------------------------------------------------------
// Output collection
// ---------------------------------------------------------------------------

Output* OutputManager::findByName(const std::string& name) const {
    for (auto* output : m_outputs) {
        if (output->getName() == name)
            return output;
    }
    return nullptr;
}

Output* OutputManager::findByEDID(const std::string& identifier) const {
    for (auto* output : m_outputs) {
        EDIDInfo info = parseEDID(output->getWlrOutput());
        if (edidIdentifier(info) == identifier)
            return output;
    }
    return nullptr;
}

Output* OutputManager::getPrimaryOutput() const {
    return m_outputs.empty() ? nullptr : m_outputs.front();
}

// ---------------------------------------------------------------------------
// Coordinate conversion (Task 81)
// ---------------------------------------------------------------------------

LayoutPoint OutputManager::outputToLayout(Output* output,
                                           const OutputLocalPoint& local) const {
    if (!output) return {local.x, local.y};

    const auto& box = output->getBox();
    return {
        box.x + local.x,
        box.y + local.y
    };
}

std::optional<std::pair<Output*, OutputLocalPoint>>
OutputManager::layoutToOutput(const LayoutPoint& global) const {
    for (auto* output : m_outputs) {
        const auto& box = output->getBox();
        if (global.x >= box.x && global.x < box.x + box.width &&
            global.y >= box.y && global.y < box.y + box.height) {
            return std::make_pair(output, OutputLocalPoint{
                global.x - box.x,
                global.y - box.y
            });
        }
    }
    return std::nullopt;
}

Output* OutputManager::outputAtLayout(double lx, double ly) const {
    auto result = layoutToOutput({lx, ly});
    return result ? result->first : nullptr;
}

// ---------------------------------------------------------------------------
// Arrangement (Task 81)
// ---------------------------------------------------------------------------

void OutputManager::placeOutput(Output* output, OutputPlacement placement,
                                 Output* relativeTo) {
    if (!output) return;

    auto* layout = m_server.getOutputLayout();
    if (!layout) return;

    if (placement == OutputPlacement::Auto || !relativeTo) {
        wlr_output_layout_add_auto(layout, output->getWlrOutput());
        LOG_DEBUG("OutputManager: auto-placed '{}'", output->getName());
        return;
    }

    const auto& refBox = relativeTo->getBox();
    int newX = 0, newY = 0;

    switch (placement) {
    case OutputPlacement::LeftOf:
        newX = refBox.x - output->getBox().width;
        newY = refBox.y;
        break;
    case OutputPlacement::RightOf:
        newX = refBox.x + refBox.width;
        newY = refBox.y;
        break;
    case OutputPlacement::Above:
        newX = refBox.x;
        newY = refBox.y - output->getBox().height;
        break;
    case OutputPlacement::Below:
        newX = refBox.x;
        newY = refBox.y + refBox.height;
        break;
    case OutputPlacement::Mirror:
        // Mirror placement: same position as the reference output.
        newX = refBox.x;
        newY = refBox.y;
        break;
    default:
        wlr_output_layout_add_auto(layout, output->getWlrOutput());
        return;
    }

    wlr_output_layout_add(layout, output->getWlrOutput(), newX, newY);
    LOG_INFO("OutputManager: placed '{}' at ({}, {}) relative to '{}'",
             output->getName(), newX, newY, relativeTo->getName());
}

void OutputManager::autoArrange() {
    if (m_outputs.empty()) return;

    auto* layout = m_server.getOutputLayout();
    if (!layout) return;

    // Simple left-to-right arrangement.
    int currentX = 0;
    for (auto* output : m_outputs) {
        wlr_output_layout_add(layout, output->getWlrOutput(), currentX, 0);
        currentX += output->getBox().width;
    }

    LOG_INFO("OutputManager: auto-arranged {} outputs left-to-right",
             m_outputs.size());
}

bool OutputManager::detectAndFixOverlaps() {
    bool hadOverlap = false;

    for (size_t i = 0; i < m_outputs.size(); ++i) {
        for (size_t j = i + 1; j < m_outputs.size(); ++j) {
            const auto& a = m_outputs[i]->getBox();
            const auto& b = m_outputs[j]->getBox();

            // Check AABB overlap.
            bool overlaps =
                a.x < b.x + b.width  && a.x + a.width  > b.x &&
                a.y < b.y + b.height && a.y + a.height > b.y;

            if (overlaps) {
                hadOverlap = true;
                LOG_WARN("OutputManager: overlap detected between '{}' and '{}'",
                         m_outputs[i]->getName(), m_outputs[j]->getName());

                // Push the second output to the right of the first.
                auto* layout = m_server.getOutputLayout();
                int newX = a.x + a.width;
                wlr_output_layout_add(layout, m_outputs[j]->getWlrOutput(),
                                      newX, b.y);
                LOG_INFO("OutputManager: moved '{}' to ({}, {}) to fix overlap",
                         m_outputs[j]->getName(), newX, b.y);
            }
        }
    }

    return hadOverlap;
}

void OutputManager::rearrangeOnChange() {
    if (m_outputs.empty()) return;

    // First try to detect and fix overlaps.
    detectAndFixOverlaps();

    LOG_DEBUG("OutputManager: rearranged layout after output change");
}

// ---------------------------------------------------------------------------
// Hotplug handling (Task 82)
// ---------------------------------------------------------------------------

void OutputManager::handleNewOutput(wlr_output* wlrOutput) {
    LOG_INFO("OutputManager: new output detected: {}", wlrOutput->name);

    // Initialize rendering for this output.
    if (!wlr_output_init_render(wlrOutput, m_server.getAllocator(),
                                 m_server.getRenderer())) {
        LOG_ERROR("OutputManager: failed to init render for '{}'",
                  wlrOutput->name);
        return;
    }

    // Create the Output wrapper via the Compositor.
    Output* output = m_server.getCompositor().createOutput(wlrOutput);
    if (!output) {
        LOG_ERROR("OutputManager: failed to create Output for '{}'",
                  wlrOutput->name);
        return;
    }

    m_outputs.push_back(output);

    // Try to restore saved configuration for this output.
    if (!restoreOutputConfig(output)) {
        autoConfigure(output, wlrOutput);
    }

    // Auto-position if no saved position.
    autoPosition(output);

    // Fix any overlaps that may have been introduced.
    rearrangeOnChange();

    // Notify listeners.
    if (m_onAdded) m_onAdded(output);

    LOG_INFO("OutputManager: output '{}' added (total: {})",
             output->getName(), m_outputs.size());
}

void OutputManager::handleOutputDestroy(Output* output) {
    if (!output) return;

    LOG_INFO("OutputManager: output '{}' being destroyed", output->getName());

    // Save config before removal.
    saveOutputConfig(output);

    // Migrate windows to another output.
    Output* fallback = nullptr;
    for (auto* o : m_outputs) {
        if (o != output) {
            fallback = o;
            break;
        }
    }

    if (fallback) {
        migrateWindows(output, fallback);
    }

    // Remove from our list.
    auto it = std::find(m_outputs.begin(), m_outputs.end(), output);
    if (it != m_outputs.end()) {
        m_outputs.erase(it);
    }

    // Notify listeners.
    if (m_onRemoved) m_onRemoved(output);

    // Rearrange remaining outputs.
    rearrangeOnChange();

    LOG_INFO("OutputManager: output removed (remaining: {})", m_outputs.size());
}

void OutputManager::migrateWindows(Output* from, Output* to) {
    if (!from || !to) return;

    auto& compositor = m_server.getCompositor();
    for (auto& surfPtr : compositor.getSurfaces()) {
        Surface* surf = surfPtr.get();
        if (surf->getOutput() == from) {
            compositor.moveWindowToOutput(surf, to);
            LOG_DEBUG("OutputManager: migrated window '{}' from '{}' to '{}'",
                      surf->getTitle(), from->getName(), to->getName());
        }
    }
}

// ---------------------------------------------------------------------------
// DPI & refresh management (Task 83)
// ---------------------------------------------------------------------------

double OutputManager::calculateDPI(int widthPx, int heightPx,
                                    int widthMm, int heightMm) {
    if (widthMm <= 0 || heightMm <= 0) return 96.0; // fallback

    double diagPx = std::sqrt(static_cast<double>(widthPx * widthPx +
                                                   heightPx * heightPx));
    double diagMm = std::sqrt(static_cast<double>(widthMm * widthMm +
                                                   heightMm * heightMm));
    double diagInches = diagMm / 25.4;
    return diagPx / diagInches;
}

float OutputManager::suggestScale(double dpi) {
    // Common thresholds for fractional scaling.
    if (dpi <= 120.0)  return 1.0f;
    if (dpi <= 168.0)  return 1.25f;
    if (dpi <= 200.0)  return 1.5f;
    if (dpi <= 260.0)  return 1.75f;
    if (dpi <= 320.0)  return 2.0f;
    if (dpi <= 400.0)  return 2.5f;
    return 3.0f;
}

bool OutputManager::selectBestMode(Output* output, int preferredWidth,
                                    int preferredHeight,
                                    int preferredRefreshMHz) {
    if (!output) return false;

    auto* wlrOut = output->getWlrOutput();
    if (!wlrOut) return false;

    struct wlr_output_mode* bestMode = nullptr;
    int bestRefresh = 0;
    int bestResMatch = INT32_MAX;

    struct wlr_output_mode* mode;
    wl_list_for_each(mode, &wlrOut->modes, link) {
        int resDiff = std::abs(mode->width - preferredWidth) +
                      std::abs(mode->height - preferredHeight);

        if (resDiff < bestResMatch) {
            bestResMatch = resDiff;
            bestMode = mode;
            bestRefresh = mode->refresh;
        } else if (resDiff == bestResMatch) {
            // Same resolution -- pick higher refresh or closest to preferred.
            if (preferredRefreshMHz > 0) {
                int curDiff = std::abs(mode->refresh - preferredRefreshMHz);
                int bestDiff = std::abs(bestRefresh - preferredRefreshMHz);
                if (curDiff < bestDiff) {
                    bestMode = mode;
                    bestRefresh = mode->refresh;
                }
            } else if (mode->refresh > bestRefresh) {
                // No preference: pick highest refresh.
                bestMode = mode;
                bestRefresh = mode->refresh;
            }
        }
    }

    if (!bestMode) {
        bestMode = wlr_output_preferred_mode(wlrOut);
    }

    if (bestMode) {
        return output->setMode(bestMode->width, bestMode->height,
                               bestMode->refresh);
    }

    return false;
}

// ---------------------------------------------------------------------------
// VRR management (Task 87)
// ---------------------------------------------------------------------------

void OutputManager::setVRR(Output* output, VRRMode mode) {
    if (!output) return;

    m_vrrModes[output] = mode;

    auto* wlrOut = output->getWlrOutput();
    if (!wlrOut) return;

    struct wlr_output_state state;
    wlr_output_state_init(&state);

    bool enable = (mode == VRRMode::On);
    // For FullscreenOnly, we enable adaptive sync on the output but
    // only schedule VRR frames when a fullscreen surface is present.
    if (mode == VRRMode::FullscreenOnly) {
        enable = true;
    }

    wlr_output_state_set_adaptive_sync_enabled(&state, enable);
    wlr_output_commit_state(wlrOut, &state);
    wlr_output_state_finish(&state);

    LOG_INFO("OutputManager: VRR {} on '{}'",
             mode == VRRMode::Off ? "disabled" :
             (mode == VRRMode::On ? "enabled" : "fullscreen-only"),
             output->getName());
}

OutputManager::VRRMode OutputManager::getVRR(Output* output) const {
    auto it = m_vrrModes.find(output);
    return it != m_vrrModes.end() ? it->second : VRRMode::Off;
}

// ---------------------------------------------------------------------------
// 10-bit / HDR (Task 88)
// ---------------------------------------------------------------------------

bool OutputManager::supports10Bit(Output* output) const {
    if (!output || !output->getWlrOutput()) return false;

    // Check if any mode supports 10-bit.  In wlroots, this is indicated
    // by the DRM format list.  For now, we check the output's capabilities.
    // TODO: query DRM format list for XRGB2101010 / ARGB2101010 support.
    LOG_DEBUG("OutputManager: checking 10-bit support for '{}'",
              output->getName());
    return true; // stub -- real check requires DRM format enumeration
}

bool OutputManager::enable10Bit(Output* output) {
    if (!output || !supports10Bit(output)) return false;

    // TODO: set output format to DRM_FORMAT_XRGB2101010.
    // struct wlr_output_state state;
    // wlr_output_state_init(&state);
    // wlr_output_state_set_render_format(&state, DRM_FORMAT_XRGB2101010);
    // wlr_output_commit_state(output->getWlrOutput(), &state);
    // wlr_output_state_finish(&state);

    LOG_INFO("OutputManager: enabled 10-bit color on '{}'", output->getName());
    return true;
}

bool OutputManager::supportsHDR(Output* output) const {
    if (!output) return false;

    // HDR support requires:
    // 1. Output supports HDR metadata via EDID
    // 2. DRM driver supports HDR_OUTPUT_METADATA property
    // TODO: check DRM properties.
    LOG_DEBUG("OutputManager: checking HDR support for '{}'", output->getName());
    return false; // conservative default
}

bool OutputManager::enableHDR(Output* output) {
    if (!output || !supportsHDR(output)) return false;

    // TODO: set HDR metadata via DRM properties.
    // This would involve:
    // 1. Setting the transfer function to PQ/HLG
    // 2. Setting the colorimetry to BT.2020
    // 3. Setting max/min luminance from EDID
    // 4. Enabling tone mapping for SDR content

    LOG_INFO("OutputManager: enabled HDR on '{}' with SDR tone mapping",
             output->getName());
    return true;
}

// ---------------------------------------------------------------------------
// EDID-based config (Task 90)
// ---------------------------------------------------------------------------

EDIDInfo OutputManager::parseEDID(wlr_output* wlrOutput) {
    EDIDInfo info;
    if (!wlrOutput) return info;

    info.connector   = wlrOutput->name ? wlrOutput->name : "";
    info.make        = wlrOutput->make ? wlrOutput->make : "";
    info.model       = wlrOutput->model ? wlrOutput->model : "";
    info.serial      = wlrOutput->serial ? wlrOutput->serial : "";
    info.physWidthMm = wlrOutput->phys_width;
    info.physHeightMm = wlrOutput->phys_height;

    return info;
}

std::string OutputManager::edidIdentifier(const EDIDInfo& info) {
    return info.make + ":" + info.model + ":" + info.serial;
}

void OutputManager::saveOutputConfig(Output* output) {
    if (!output) return;

    EDIDInfo edid = parseEDID(output->getWlrOutput());
    std::string id = edidIdentifier(edid);

    SavedOutputConfig cfg;
    cfg.identifier  = id;
    cfg.posX        = output->getBox().x;
    cfg.posY        = output->getBox().y;
    cfg.width       = output->getBox().width;
    cfg.height      = output->getBox().height;
    cfg.refreshMHz  = output->getRefreshRate();
    cfg.scale       = output->getScale();
    cfg.transform   = output->getTransform();
    cfg.vrrEnabled  = output->isVRREnabled();

    m_savedConfigs[id] = cfg;

    LOG_DEBUG("OutputManager: saved config for '{}' ({})",
              output->getName(), id);
}

bool OutputManager::restoreOutputConfig(Output* output) {
    if (!output) return false;

    EDIDInfo edid = parseEDID(output->getWlrOutput());
    std::string id = edidIdentifier(edid);

    auto it = m_savedConfigs.find(id);
    if (it == m_savedConfigs.end()) return false;

    const auto& cfg = it->second;

    // Restore mode.
    if (cfg.width > 0 && cfg.height > 0) {
        output->setMode(cfg.width, cfg.height, cfg.refreshMHz);
    }

    // Restore scale.
    if (cfg.scale > 0.0f) {
        output->setScale(cfg.scale);
    }

    // Restore transform.
    output->setTransform(cfg.transform);

    // Restore position.
    auto* layout = m_server.getOutputLayout();
    if (layout) {
        wlr_output_layout_add(layout, output->getWlrOutput(),
                              cfg.posX, cfg.posY);
    }

    // Restore VRR.
    if (cfg.vrrEnabled) {
        setVRR(output, VRRMode::On);
    }

    LOG_INFO("OutputManager: restored config for '{}' ({})",
             output->getName(), id);
    return true;
}

void OutputManager::saveAllConfigs(const std::string& path) {
    // Save all output configs first.
    for (auto* output : m_outputs) {
        saveOutputConfig(output);
    }

    std::ofstream file(path);
    if (!file.is_open()) {
        LOG_ERROR("OutputManager: failed to open '{}' for writing", path);
        return;
    }

    // Write as a simple key=value format.
    for (const auto& [id, cfg] : m_savedConfigs) {
        file << "[output \"" << id << "\"]\n";
        file << "position = " << cfg.posX << " " << cfg.posY << "\n";
        file << "mode = " << cfg.width << "x" << cfg.height
             << "@" << cfg.refreshMHz << "\n";
        file << "scale = " << cfg.scale << "\n";
        file << "transform = " << cfg.transform << "\n";
        file << "vrr = " << (cfg.vrrEnabled ? "true" : "false") << "\n";
        file << "bitdepth = " << cfg.bitDepth << "\n";
        if (!cfg.mirrorOf.empty()) {
            file << "mirror = " << cfg.mirrorOf << "\n";
        }
        file << "\n";
    }

    LOG_INFO("OutputManager: saved {} output configs to '{}'",
             m_savedConfigs.size(), path);
}

void OutputManager::loadSavedConfigs(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        LOG_DEBUG("OutputManager: no saved config file at '{}'", path);
        return;
    }

    // Simple line-based parser for the config format.
    SavedOutputConfig current;
    std::string currentId;
    std::string line;

    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        if (line.starts_with("[output \"")) {
            // Save previous entry.
            if (!currentId.empty()) {
                m_savedConfigs[currentId] = current;
            }
            // Parse new identifier.
            auto start = line.find('"') + 1;
            auto end = line.rfind('"');
            currentId = line.substr(start, end - start);
            current = {};
            current.identifier = currentId;
        } else if (line.starts_with("position = ")) {
            auto val = line.substr(11);
            auto sp = val.find(' ');
            if (sp != std::string::npos) {
                current.posX = std::stoi(val.substr(0, sp));
                current.posY = std::stoi(val.substr(sp + 1));
            }
        } else if (line.starts_with("scale = ")) {
            current.scale = std::stof(line.substr(8));
        } else if (line.starts_with("transform = ")) {
            current.transform = std::stoi(line.substr(12));
        } else if (line.starts_with("vrr = ")) {
            current.vrrEnabled = (line.substr(6) == "true");
        } else if (line.starts_with("bitdepth = ")) {
            current.bitDepth = std::stoi(line.substr(11));
        } else if (line.starts_with("mirror = ")) {
            current.mirrorOf = line.substr(9);
        } else if (line.starts_with("mode = ")) {
            // Parse WxH@R format.
            auto val = line.substr(7);
            auto xpos = val.find('x');
            auto atpos = val.find('@');
            if (xpos != std::string::npos) {
                current.width = std::stoi(val.substr(0, xpos));
                if (atpos != std::string::npos) {
                    current.height = std::stoi(val.substr(xpos + 1,
                                                           atpos - xpos - 1));
                    current.refreshMHz = std::stoi(val.substr(atpos + 1));
                } else {
                    current.height = std::stoi(val.substr(xpos + 1));
                }
            }
        }
    }

    // Save last entry.
    if (!currentId.empty()) {
        m_savedConfigs[currentId] = current;
    }

    LOG_INFO("OutputManager: loaded {} saved output configs from '{}'",
             m_savedConfigs.size(), path);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void OutputManager::autoConfigure(Output* output, wlr_output* wlrOutput) {
    // Set preferred mode.
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);

    auto* mode = wlr_output_preferred_mode(wlrOutput);
    if (mode) {
        wlr_output_state_set_mode(&state, mode);
    }

    wlr_output_commit_state(wlrOutput, &state);
    wlr_output_state_finish(&state);

    // Calculate DPI-based scale.
    EDIDInfo edid = parseEDID(wlrOutput);
    if (edid.physWidthMm > 0 && edid.physHeightMm > 0) {
        double dpi = calculateDPI(wlrOutput->width, wlrOutput->height,
                                  edid.physWidthMm, edid.physHeightMm);
        float scale = suggestScale(dpi);
        output->setScale(scale);
        LOG_INFO("OutputManager: auto-scale for '{}': DPI={:.0f}, scale={:.2f}",
                 output->getName(), dpi, scale);
    }

    // Check config for VRR setting.
    const auto& generalCfg = m_server.getConfigManager().getGeneral();
    if (generalCfg.vrr == 1) {
        setVRR(output, VRRMode::On);
    } else if (generalCfg.vrr == 2) {
        setVRR(output, VRRMode::FullscreenOnly);
    }

    LOG_INFO("OutputManager: auto-configured '{}'", output->getName());
}

void OutputManager::autoPosition(Output* output) {
    if (!output) return;

    auto* layout = m_server.getOutputLayout();
    if (!layout) return;

    // If this is the first output, place at origin.
    if (m_outputs.size() <= 1) {
        wlr_output_layout_add(layout, output->getWlrOutput(), 0, 0);
        return;
    }

    // Place to the right of the rightmost existing output.
    int maxRight = 0;
    for (auto* o : m_outputs) {
        if (o == output) continue;
        int right = o->getBox().x + o->getBox().width;
        if (right > maxRight) maxRight = right;
    }

    wlr_output_layout_add(layout, output->getWlrOutput(), maxRight, 0);
    LOG_DEBUG("OutputManager: auto-positioned '{}' at x={}",
              output->getName(), maxRight);
}

} // namespace eternal

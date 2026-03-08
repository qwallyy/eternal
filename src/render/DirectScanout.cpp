// Task 108: Direct scanout implementation

#include "eternal/render/DirectScanout.hpp"
#include "eternal/render/Renderer.hpp"
#include "eternal/core/Output.hpp"
#include "eternal/core/Surface.hpp"

extern "C" {
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/render/drm_format_set.h>
}

#include <cassert>

namespace eternal {

const ScanoutStats DirectScanout::kEmptyStats{};

DirectScanout::DirectScanout() = default;
DirectScanout::~DirectScanout() = default;

void DirectScanout::init(Renderer* renderer) {
    renderer_ = renderer;
}

// ---------------------------------------------------------------------------
// Eligibility checks
// ---------------------------------------------------------------------------

bool DirectScanout::checkFormatCompatibility(wlr_output* output,
                                              wlr_surface* surface) const {
    if (!output || !surface) return false;

    // Get the surface's current buffer
    if (!surface->buffer) return false;
    wlr_buffer* buffer = &surface->buffer->base;

    // Check if the output's primary plane supports the buffer's format.
    // In wlroots 0.18, we use wlr_output_test_state to probe this.
    wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_buffer(&state, buffer);

    bool compatible = wlr_output_test_state(output, &state);
    wlr_output_state_finish(&state);

    return compatible;
}

bool DirectScanout::hasPendingChildren(Surface* surface) const {
    if (!surface) return true;

    // Check for subsurfaces
    if (!surface->getSubsurfaces().empty()) return true;

    // Check for popups
    if (!surface->getPopups().empty()) return true;

    return false;
}

bool DirectScanout::canScanout(Output* output, Surface* surface) const {
    if (!enabled_) return false;
    if (!output || !surface) return false;

    // Must be fullscreen
    if (!surface->isFullscreen()) return false;

    // Must be mapped
    if (!surface->isMapped()) return false;

    // Must not have subsurfaces or popups
    if (hasPendingChildren(surface)) return false;

    // Check format/transform compatibility
    wlr_output* wlr_out = output->getWlrOutput();
    wlr_surface* wlr_surf = surface->getWlrSurface();
    if (!wlr_out || !wlr_surf) return false;

    // Check if the surface size matches the output
    SurfaceBox geom = surface->getXdgGeometry();
    OutputBox out_box = output->getBox();
    if (geom.width != out_box.width || geom.height != out_box.height) {
        return false;
    }

    return checkFormatCompatibility(wlr_out, wlr_surf);
}

// ---------------------------------------------------------------------------
// Direct scanout attempt
// ---------------------------------------------------------------------------

ScanoutResult DirectScanout::tryDirectScanout(Output* output, Surface* surface) {
    if (!output || !surface) return ScanoutResult::Fallback;

    wlr_output* wlr_out = output->getWlrOutput();
    stats_[wlr_out].frames_attempted++;

    if (!enabled_) {
        stats_[wlr_out].last_result = ScanoutResult::Fallback;
        last_scanout_[wlr_out] = false;
        return ScanoutResult::Fallback;
    }

    // Check fullscreen
    if (!surface->isFullscreen()) {
        stats_[wlr_out].last_result = ScanoutResult::NoFullscreen;
        last_scanout_[wlr_out] = false;
        return ScanoutResult::NoFullscreen;
    }

    if (!surface->isMapped()) {
        stats_[wlr_out].last_result = ScanoutResult::Fallback;
        last_scanout_[wlr_out] = false;
        return ScanoutResult::Fallback;
    }

    // Check for subsurfaces
    if (!surface->getSubsurfaces().empty()) {
        stats_[wlr_out].last_result = ScanoutResult::SubsurfacesPresent;
        last_scanout_[wlr_out] = false;
        stats_[wlr_out].frames_composited++;
        return ScanoutResult::SubsurfacesPresent;
    }

    // Check for popups
    if (!surface->getPopups().empty()) {
        stats_[wlr_out].last_result = ScanoutResult::PopupsPresent;
        last_scanout_[wlr_out] = false;
        stats_[wlr_out].frames_composited++;
        return ScanoutResult::PopupsPresent;
    }

    // Get the wlr_surface buffer
    wlr_surface* wlr_surf = surface->getWlrSurface();
    if (!wlr_surf || !wlr_surf->buffer) {
        stats_[wlr_out].last_result = ScanoutResult::Fallback;
        last_scanout_[wlr_out] = false;
        stats_[wlr_out].frames_composited++;
        return ScanoutResult::Fallback;
    }

    // Try to commit the buffer directly to the output
    ScanoutResult result = commitDirectScanout(wlr_out, &wlr_surf->buffer->base);

    if (result == ScanoutResult::Success) {
        stats_[wlr_out].frames_scannedout++;
        last_scanout_[wlr_out] = true;
    } else {
        stats_[wlr_out].frames_composited++;
        last_scanout_[wlr_out] = false;
    }

    stats_[wlr_out].last_result = result;
    return result;
}

ScanoutResult DirectScanout::commitDirectScanout(wlr_output* output,
                                                   wlr_buffer* buffer) {
    if (!output || !buffer) return ScanoutResult::Fallback;

    // Set up the output state with the client buffer directly
    wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_buffer(&state, buffer);

    // Test if the output can accept this buffer
    if (!wlr_output_test_state(output, &state)) {
        wlr_output_state_finish(&state);
        return ScanoutResult::FormatMismatch;
    }

    // Commit the direct scanout
    if (!wlr_output_commit_state(output, &state)) {
        wlr_output_state_finish(&state);
        return ScanoutResult::PlaneUnavailable;
    }

    wlr_output_state_finish(&state);
    return ScanoutResult::Success;
}

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------

const ScanoutStats& DirectScanout::statsFor(wlr_output* output) const {
    auto it = stats_.find(output);
    if (it != stats_.end()) return it->second;
    return kEmptyStats;
}

void DirectScanout::resetStats() {
    stats_.clear();
}

bool DirectScanout::wasDirectScanout(wlr_output* output) const {
    auto it = last_scanout_.find(output);
    if (it != last_scanout_.end()) return it->second;
    return false;
}

} // namespace eternal

#pragma once

// Task 108: Input latency minimization - Direct scanout path
//
// Direct scanout bypasses compositor rendering entirely for fullscreen
// surfaces. The client's DMA-BUF is presented directly to the output
// plane, reducing input-to-display latency (important for games).

#include <cstdint>
#include <unordered_map>

extern "C" {
struct wlr_output;
struct wlr_surface;
struct wlr_buffer;
}

namespace eternal {

class Renderer;
class Surface;
class Output;

// ---------------------------------------------------------------------------
// Direct scanout state per output
// ---------------------------------------------------------------------------

enum class ScanoutResult : uint8_t {
    Success,          // Direct scanout was committed
    NoFullscreen,     // No fullscreen surface on this output
    FormatMismatch,   // Surface format incompatible with output plane
    PlaneUnavailable, // No overlay/primary plane available
    SubsurfacesPresent, // Surface has subsurfaces (can't scanout)
    PopupsPresent,    // Popups are blocking direct scanout
    TransformMismatch,// Output transform doesn't match surface
    MultiGPU,         // Surface buffer on different GPU than output
    Fallback,         // Fell back to composited path
};

struct ScanoutStats {
    uint64_t frames_scannedout = 0;
    uint64_t frames_composited = 0;
    uint64_t frames_attempted  = 0;
    ScanoutResult last_result  = ScanoutResult::Fallback;
};

// ---------------------------------------------------------------------------
// DirectScanout -- manages direct scanout path
// ---------------------------------------------------------------------------

class DirectScanout {
public:
    DirectScanout();
    ~DirectScanout();

    DirectScanout(const DirectScanout&) = delete;
    DirectScanout& operator=(const DirectScanout&) = delete;

    /// Initialize with a renderer for fallback compositing.
    void init(Renderer* renderer);

    /// Attempt direct scanout for a fullscreen surface on the given output.
    /// Returns the result code indicating success or why it fell back.
    ScanoutResult tryDirectScanout(Output* output, Surface* surface);

    /// Check if a surface is eligible for direct scanout (without committing).
    [[nodiscard]] bool canScanout(Output* output, Surface* surface) const;

    /// Enable or disable direct scanout globally.
    void setEnabled(bool enable) { enabled_ = enable; }

    /// Whether direct scanout is enabled.
    [[nodiscard]] bool isEnabled() const { return enabled_; }

    /// Get scanout statistics for an output.
    [[nodiscard]] const ScanoutStats& statsFor(wlr_output* output) const;

    /// Reset statistics for all outputs.
    void resetStats();

    /// Whether direct scanout was used for the last frame on this output.
    [[nodiscard]] bool wasDirectScanout(wlr_output* output) const;

private:
    /// Check format compatibility between surface buffer and output.
    [[nodiscard]] bool checkFormatCompatibility(wlr_output* output,
                                                 wlr_surface* surface) const;

    /// Check if the surface has any subsurfaces or popups.
    [[nodiscard]] bool hasPendingChildren(Surface* surface) const;

    /// Import the surface's DMA-BUF and present it directly.
    ScanoutResult commitDirectScanout(wlr_output* output,
                                       wlr_buffer* buffer);

    Renderer* renderer_ = nullptr;
    bool enabled_ = true;

    mutable std::unordered_map<wlr_output*, ScanoutStats> stats_;
    std::unordered_map<wlr_output*, bool> last_scanout_;

    static const ScanoutStats kEmptyStats;
};

} // namespace eternal

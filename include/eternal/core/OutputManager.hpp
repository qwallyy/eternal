#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

extern "C" {
#include <wayland-server-core.h>

struct wlr_output;
struct wlr_output_layout;
struct wlr_backend;
}

namespace eternal {

class Server;
class Output;

// ---------------------------------------------------------------------------
// Output arrangement relative placement
// ---------------------------------------------------------------------------

enum class OutputPlacement {
    Auto,
    LeftOf,
    RightOf,
    Above,
    Below,
    Mirror,
};

// ---------------------------------------------------------------------------
// Coordinate conversion helpers
// ---------------------------------------------------------------------------

struct LayoutPoint {
    double x = 0.0;
    double y = 0.0;
};

struct OutputLocalPoint {
    double x = 0.0;
    double y = 0.0;
};

// ---------------------------------------------------------------------------
// EDID identification
// ---------------------------------------------------------------------------

struct EDIDInfo {
    std::string connector;   // e.g. "DP-1"
    std::string make;
    std::string model;
    std::string serial;
    int physWidthMm  = 0;
    int physHeightMm = 0;
};

// ---------------------------------------------------------------------------
// Saved per-output config (EDID-based)
// ---------------------------------------------------------------------------

struct SavedOutputConfig {
    std::string identifier;     // "make:model:serial"
    int posX = 0;
    int posY = 0;
    int width = 0;
    int height = 0;
    int refreshMHz = 0;
    float scale = 1.0f;
    int transform = 0;
    bool vrrEnabled = false;
    int bitDepth = 8;
    std::string mirrorOf;
};

// ---------------------------------------------------------------------------
// OutputManager - tracks all outputs in layout coordinate space
// ---------------------------------------------------------------------------

class OutputManager {
public:
    explicit OutputManager(Server& server);
    ~OutputManager();

    OutputManager(const OutputManager&) = delete;
    OutputManager& operator=(const OutputManager&) = delete;

    /// Initialize the output manager (wire up backend listeners).
    bool init();

    // ── Output collection ────────────────────────────────────────────────

    /// All managed outputs.
    const std::vector<Output*>& getOutputs() const { return m_outputs; }

    /// Find an output by connector name (e.g. "DP-1").
    Output* findByName(const std::string& name) const;

    /// Find an output by EDID identifier ("make:model:serial").
    Output* findByEDID(const std::string& identifier) const;

    /// The primary (first) output, or nullptr if none.
    Output* getPrimaryOutput() const;

    // ── Coordinate conversion ────────────────────────────────────────────

    /// Convert output-local coordinates to layout-global coordinates.
    LayoutPoint outputToLayout(Output* output, const OutputLocalPoint& local) const;

    /// Convert layout-global coordinates to output-local coordinates.
    /// Returns nullopt if the point is not on any output.
    std::optional<std::pair<Output*, OutputLocalPoint>>
    layoutToOutput(const LayoutPoint& global) const;

    /// Get the output at a given layout coordinate.
    Output* outputAtLayout(double lx, double ly) const;

    // ── Arrangement ──────────────────────────────────────────────────────

    /// Place an output relative to another using the given placement.
    void placeOutput(Output* output, OutputPlacement placement,
                     Output* relativeTo = nullptr);

    /// Auto-arrange all outputs (left-to-right by default).
    void autoArrange();

    /// Detect and fix any overlapping output regions.
    bool detectAndFixOverlaps();

    /// Re-arrange layout when an output is added or removed.
    void rearrangeOnChange();

    // ── Hotplug handling (Task 82) ───────────────────────────────────────

    /// Handle a new output from the backend.
    void handleNewOutput(wlr_output* wlrOutput);

    /// Handle output destruction (migrate windows, rearrange).
    void handleOutputDestroy(Output* output);

    /// Migrate all windows from one output to another.
    void migrateWindows(Output* from, Output* to);

    // ── DPI & refresh management (Task 83) ──────────────────────────────

    /// Calculate DPI for an output based on physical dimensions.
    static double calculateDPI(int widthPx, int heightPx,
                               int widthMm, int heightMm);

    /// Suggest a fractional scale factor based on DPI.
    static float suggestScale(double dpi);

    /// Select the best mode for an output with refresh rate preference.
    bool selectBestMode(Output* output, int preferredWidth, int preferredHeight,
                        int preferredRefreshMHz = 0);

    // ── VRR management (Task 87) ────────────────────────────────────────

    enum class VRRMode { Off, On, FullscreenOnly };

    /// Set VRR mode for an output.
    void setVRR(Output* output, VRRMode mode);

    /// Get VRR mode for an output.
    VRRMode getVRR(Output* output) const;

    // ── 10-bit / HDR (Task 88) ──────────────────────────────────────────

    /// Check if an output supports 10-bit color.
    bool supports10Bit(Output* output) const;

    /// Enable 10-bit color on an output.
    bool enable10Bit(Output* output);

    /// Check if an output supports HDR metadata.
    bool supportsHDR(Output* output) const;

    /// Enable HDR with tone mapping for SDR fallback.
    bool enableHDR(Output* output);

    // ── EDID-based config (Task 90) ─────────────────────────────────────

    /// Parse EDID info from a wlr_output.
    static EDIDInfo parseEDID(wlr_output* wlrOutput);

    /// Generate a unique identifier from EDID.
    static std::string edidIdentifier(const EDIDInfo& info);

    /// Save current output config to persistent storage.
    void saveOutputConfig(Output* output);

    /// Restore saved config for a known output (by EDID).
    bool restoreOutputConfig(Output* output);

    /// Save all output configs to config file.
    void saveAllConfigs(const std::string& path);

    /// Load saved output configs from file.
    void loadSavedConfigs(const std::string& path);

    // ── Signals ──────────────────────────────────────────────────────────

    using OutputCallback = std::function<void(Output*)>;

    void onOutputAdded(OutputCallback cb)   { m_onAdded = std::move(cb); }
    void onOutputRemoved(OutputCallback cb) { m_onRemoved = std::move(cb); }

    /// Reference back to server.
    Server& getServer() const { return m_server; }

private:
    /// Auto-configure a newly connected output.
    void autoConfigure(Output* output, wlr_output* wlrOutput);

    /// Compute auto-position for a new output.
    void autoPosition(Output* output);

    Server& m_server;
    std::vector<Output*> m_outputs;

    // Saved configs by EDID identifier
    std::unordered_map<std::string, SavedOutputConfig> m_savedConfigs;

    // Per-output VRR mode
    std::unordered_map<Output*, VRRMode> m_vrrModes;

    // Callbacks
    OutputCallback m_onAdded;
    OutputCallback m_onRemoved;

    // Backend listener for new outputs
    struct wl_listener m_newOutputListener{};
};

} // namespace eternal

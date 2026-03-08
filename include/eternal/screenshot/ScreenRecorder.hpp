#pragma once

extern "C" {
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_compositor.h>
}

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>

#include "eternal/render/Renderer.hpp"

namespace eternal {

class Server;
class Surface;

/// What is being recorded.
using RecordingTarget = std::variant<
    wlr_output*,       // single output
    wlr_surface*,      // single window / surface
    Box                 // arbitrary region
>;

/// Recording options.
struct RecordingOptions {
    int  framerate       = 30;
    bool include_cursor  = true;
    bool with_audio      = false;
    bool hardware_encode = true;          // prefer VA-API / NVENC when available
    std::string encoder_name;             // override (e.g. "libx264", "h264_vaapi")
    std::filesystem::path output_path;    // empty = portal decides
    bool dmabuf_sharing  = true;          // attempt zero-copy DMA-BUF sharing
};

/// PipeWire stream state.
enum class StreamState {
    Idle,
    Connecting,
    Streaming,
    Paused,
    Error,
};

/// Represents a single PipeWire screen sharing stream.
struct ShareStream {
    uint32_t streamId = 0;
    uint32_t pipeWireNodeId = 0;
    RecordingTarget target;
    RecordingOptions options;
    StreamState state = StreamState::Idle;
    int width = 0;
    int height = 0;
    int negotiatedFps = 0;
    bool dmabufActive = false;
};

/// Screen recording and sharing through PipeWire + xdg-desktop-portal.
class ScreenRecorder {
public:
    explicit ScreenRecorder();
    ~ScreenRecorder();

    ScreenRecorder(const ScreenRecorder&) = delete;
    ScreenRecorder& operator=(const ScreenRecorder&) = delete;

    /// Initialize PipeWire connection. Returns false if PipeWire is unavailable.
    bool init(Renderer* renderer);

    /// Shutdown PipeWire connection and all streams.
    void shutdown();

    // ── Recording lifecycle (Task 92) ────────────────────────────────────

    /// Start recording with the given target and options.
    bool startRecording(const RecordingTarget& target,
                        const RecordingOptions& options = {});

    /// Stop the current recording.
    void stopRecording();

    /// True while a recording session is active.
    [[nodiscard]] bool isRecording() const { return m_recording; }

    // ── PipeWire stream management (Task 92) ─────────────────────────────

    /// Create a new PipeWire stream for screen sharing.
    /// Returns the stream ID, or 0 on failure.
    uint32_t createStream(const RecordingTarget& target,
                          const RecordingOptions& options = {});

    /// Destroy a PipeWire stream.
    void destroyStream(uint32_t streamId);

    /// Get a stream by ID.
    [[nodiscard]] ShareStream* getStream(uint32_t streamId);

    /// Get the PipeWire node ID for a stream (for portal integration).
    [[nodiscard]] uint32_t getNodeId(uint32_t streamId) const;

    /// Negotiate frame rate with PipeWire.
    bool negotiateFrameRate(uint32_t streamId, int requestedFps);

    // ── DMA-BUF zero-copy (Task 92) ─────────────────────────────────────

    /// Check if DMA-BUF sharing is available.
    [[nodiscard]] bool isDmaBufAvailable() const;

    /// Enable/disable DMA-BUF for a stream.
    void setDmaBuf(uint32_t streamId, bool enable);

    // ── Window-level sharing (Task 93) ───────────────────────────────────

    /// Share an individual window (not full output).
    uint32_t shareWindow(Surface* surface,
                         const RecordingOptions& options = {});

    /// Handle window geometry changes for an active window share.
    void onWindowGeometryChanged(Surface* surface);

    /// Handle window close during an active share.
    void onWindowClosed(Surface* surface);

    /// Export a window via the foreign toplevel protocol.
    uint32_t exportToplevel(Surface* surface);

    // ── Target management ────────────────────────────────────────────────

    /// Get the current recording target.
    [[nodiscard]] const RecordingTarget& getRecordingTarget() const { return m_target; }

    /// Dynamically switch the recording target without stopping.
    bool setTarget(const RecordingTarget& new_target);

    // ── Sensitive window blocking ────────────────────────────────────────

    /// Mark a surface as sensitive (will be blacked out in recordings).
    void addSensitiveSurface(wlr_surface* surface);
    void removeSensitiveSurface(wlr_surface* surface);
    [[nodiscard]] bool isSensitive(wlr_surface* surface) const;

    // ── Portal integration ───────────────────────────────────────────────

    /// Get the PipeWire node ID for portal clients.
    [[nodiscard]] uint32_t portalNodeId() const;

    // ── Frame submission ─────────────────────────────────────────────────

    /// Called each output frame to push a new frame into the PipeWire stream.
    void onOutputFrame(wlr_output* output);

private:
    Renderer* m_renderer = nullptr;
    bool m_recording = false;
    bool m_pipewireAvailable = false;

    RecordingTarget  m_target;
    RecordingOptions m_options;

    // PipeWire state (pimpl to avoid leaking pw headers).
    struct PipeWireState;
    std::unique_ptr<PipeWireState> m_pwState;

    // Active streams.
    std::unordered_map<uint32_t, ShareStream> m_streams;
    uint32_t m_nextStreamId = 1;

    // Window share tracking (Task 93).
    std::unordered_map<Surface*, uint32_t> m_windowStreams;

    // Sensitive surfaces set.
    std::unordered_set<wlr_surface*> m_sensitiveSurfaces;

    // Internal helpers.
    bool setupPipeWireStream(ShareStream& stream);
    void teardownPipeWireStream(ShareStream& stream);
    void submitFrame(ShareStream& stream, const void* data,
                     int width, int height, int stride);
    void submitDmaBufFrame(ShareStream& stream, int dmabufFd,
                           int width, int height, uint32_t format,
                           uint64_t modifier);
    void blackOutSensitive(void* buffer, int buf_width, int buf_height,
                           int stride);
    void updateStreamDimensions(ShareStream& stream, int w, int h);
};

} // namespace eternal

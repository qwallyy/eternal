#pragma once

// Task 109: Profiling event loop
//
// Frame timing measurement, per-stage timing, rolling average FPS counter,
// latency histogram, optional output to log or IPC, debug overlay.

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace eternal {

// ---------------------------------------------------------------------------
// High-resolution timer
// ---------------------------------------------------------------------------

using Clock = std::chrono::high_resolution_clock;
using TimePoint = Clock::time_point;
using Duration = std::chrono::duration<double, std::milli>;  // milliseconds

// ---------------------------------------------------------------------------
// Profiling stages
// ---------------------------------------------------------------------------

enum class ProfileStage : uint8_t {
    DamageCalc,       // Computing damage regions
    SceneTraversal,   // Walking the scene graph
    RenderSetup,      // GL state setup
    SurfaceRender,    // Rendering client surfaces
    EffectsRender,    // Blur, shadows, decorations
    CursorRender,     // Cursor compositing
    FrameCommit,      // Buffer swap / commit to output
    InputProcessing,  // Input event handling
    LayoutCalc,       // Layout recalculation
    AnimationTick,    // Animation engine update
    IPCProcessing,    // IPC command handling

    _Count,           // Sentinel -- must be last
};

constexpr int kStageCount = static_cast<int>(ProfileStage::_Count);

/// Human-readable name for a profile stage.
[[nodiscard]] constexpr std::string_view stageName(ProfileStage stage) {
    switch (stage) {
    case ProfileStage::DamageCalc:      return "damage_calc";
    case ProfileStage::SceneTraversal:  return "scene_traversal";
    case ProfileStage::RenderSetup:     return "render_setup";
    case ProfileStage::SurfaceRender:   return "surface_render";
    case ProfileStage::EffectsRender:   return "effects_render";
    case ProfileStage::CursorRender:    return "cursor_render";
    case ProfileStage::FrameCommit:     return "frame_commit";
    case ProfileStage::InputProcessing: return "input_processing";
    case ProfileStage::LayoutCalc:      return "layout_calc";
    case ProfileStage::AnimationTick:   return "animation_tick";
    case ProfileStage::IPCProcessing:   return "ipc_processing";
    default:                            return "unknown";
    }
}

// ---------------------------------------------------------------------------
// Latency histogram
// ---------------------------------------------------------------------------

class LatencyHistogram {
public:
    static constexpr int kBucketCount = 32;
    static constexpr double kMaxMs = 33.33;  // 2 frames at 60Hz

    LatencyHistogram();

    /// Record a latency sample in milliseconds.
    void record(double ms);

    /// Get the count in a specific bucket.
    [[nodiscard]] uint64_t bucketCount(int bucket) const;

    /// Get the bucket boundary (upper end) in ms.
    [[nodiscard]] double bucketEdge(int bucket) const;

    /// Get the total number of samples.
    [[nodiscard]] uint64_t totalSamples() const;

    /// Get the 50th percentile (median) in ms.
    [[nodiscard]] double p50() const;

    /// Get the 95th percentile in ms.
    [[nodiscard]] double p95() const;

    /// Get the 99th percentile in ms.
    [[nodiscard]] double p99() const;

    /// Reset all buckets.
    void reset();

private:
    [[nodiscard]] double percentile(double p) const;

    std::array<std::atomic<uint64_t>, kBucketCount> buckets_{};
    std::atomic<uint64_t> total_{0};
    double bucket_width_ = kMaxMs / kBucketCount;
};

// ---------------------------------------------------------------------------
// Rolling average calculator
// ---------------------------------------------------------------------------

class RollingAverage {
public:
    explicit RollingAverage(int window_size = 120);

    /// Add a sample.
    void push(double value);

    /// Get the rolling average.
    [[nodiscard]] double average() const;

    /// Get the minimum in the current window.
    [[nodiscard]] double min() const;

    /// Get the maximum in the current window.
    [[nodiscard]] double max() const;

    /// Number of samples in the window.
    [[nodiscard]] int count() const;

    /// Reset.
    void reset();

private:
    std::vector<double> samples_;
    int head_ = 0;
    int size_ = 0;
    int capacity_;
    double sum_ = 0.0;
};

// ---------------------------------------------------------------------------
// Per-stage timing accumulator
// ---------------------------------------------------------------------------

struct StageTiming {
    double last_ms = 0.0;
    double avg_ms  = 0.0;
    double max_ms  = 0.0;
    uint64_t count = 0;

    void record(double ms) {
        last_ms = ms;
        ++count;
        // Exponential moving average
        double alpha = std::min(1.0 / static_cast<double>(count), 0.05);
        avg_ms = avg_ms * (1.0 - alpha) + ms * alpha;
        if (ms > max_ms) max_ms = ms;
    }

    void reset() {
        last_ms = 0.0;
        avg_ms = 0.0;
        max_ms = 0.0;
        count = 0;
    }
};

// ---------------------------------------------------------------------------
// RAII scope timer
// ---------------------------------------------------------------------------

class ScopeTimer {
public:
    using Callback = std::function<void(double ms)>;

    explicit ScopeTimer(Callback cb)
        : callback_(std::move(cb)), start_(Clock::now()) {}

    ~ScopeTimer() {
        auto end = Clock::now();
        double ms = Duration(end - start_).count();
        if (callback_) callback_(ms);
    }

    ScopeTimer(const ScopeTimer&) = delete;
    ScopeTimer& operator=(const ScopeTimer&) = delete;

private:
    Callback callback_;
    TimePoint start_;
};

// ---------------------------------------------------------------------------
// Profiler -- main profiling interface
// ---------------------------------------------------------------------------

class Profiler {
public:
    static Profiler& instance();

    Profiler(const Profiler&) = delete;
    Profiler& operator=(const Profiler&) = delete;

    // -- Enable/disable ---------------------------------------------------

    void setEnabled(bool enable) { enabled_ = enable; }
    [[nodiscard]] bool isEnabled() const { return enabled_; }

    // -- Frame timing -----------------------------------------------------

    /// Call at the start of a frame.
    void frameBegin();

    /// Call at the end of a frame.
    void frameEnd();

    /// Get the last frame time in ms.
    [[nodiscard]] double lastFrameTimeMs() const { return last_frame_ms_; }

    /// Get rolling average FPS.
    [[nodiscard]] double fps() const;

    /// Get rolling average frame time in ms.
    [[nodiscard]] double avgFrameTimeMs() const;

    /// Get the frame time rolling average calculator.
    [[nodiscard]] const RollingAverage& frameTimeAvg() const { return frame_time_avg_; }

    /// Get the frame latency histogram.
    [[nodiscard]] const LatencyHistogram& frameHistogram() const { return frame_histogram_; }

    // -- Per-stage timing -------------------------------------------------

    /// Begin timing a stage. Returns a ScopeTimer that records on destruction.
    [[nodiscard]] ScopeTimer timeStage(ProfileStage stage);

    /// Get timing data for a stage.
    [[nodiscard]] const StageTiming& stageTiming(ProfileStage stage) const;

    // -- Output -----------------------------------------------------------

    /// Output format for profiling data.
    enum class OutputFormat {
        Log,       // Write to Logger
        JSON,      // JSON string (for IPC)
        CSV,       // CSV row
    };

    /// Dump current profiling data.
    [[nodiscard]] std::string dump(OutputFormat format = OutputFormat::Log) const;

    /// Callback for per-frame profiling output (called at frameEnd).
    using FrameCallback = std::function<void(const Profiler& profiler)>;
    void setFrameCallback(FrameCallback cb) { frame_callback_ = std::move(cb); }

    // -- Debug overlay data -----------------------------------------------

    /// Get the last N frame times for rendering a graph.
    [[nodiscard]] std::vector<double> getFrameTimeHistory(int count = 120) const;

    /// Get the last N FPS values.
    [[nodiscard]] std::vector<double> getFPSHistory(int count = 120) const;

    // -- Reset ------------------------------------------------------------

    void reset();

    /// Total frames profiled.
    [[nodiscard]] uint64_t totalFrames() const { return total_frames_; }

private:
    Profiler();
    ~Profiler() = default;

    bool enabled_ = false;

    // Frame timing
    TimePoint frame_start_;
    double last_frame_ms_ = 0.0;
    uint64_t total_frames_ = 0;
    RollingAverage frame_time_avg_{120};
    RollingAverage fps_avg_{120};
    LatencyHistogram frame_histogram_;

    // Per-stage timing
    std::array<StageTiming, kStageCount> stage_timings_{};

    // Frame history for debug overlay
    mutable std::mutex history_mutex_;
    std::vector<double> frame_time_history_;
    std::vector<double> fps_history_;
    static constexpr int kMaxHistory = 600;  // 10 seconds at 60fps

    // Callback
    FrameCallback frame_callback_;
};

// ---------------------------------------------------------------------------
// Convenience macro for scoped stage timing
// ---------------------------------------------------------------------------

#define ETERNAL_PROFILE_STAGE(stage) \
    auto _profiler_timer_##__LINE__ = \
        ::eternal::Profiler::instance().isEnabled() \
            ? ::eternal::Profiler::instance().timeStage(stage) \
            : ::eternal::ScopeTimer(nullptr)

} // namespace eternal

#pragma once

// Task 106: Multi-threading rendering
//
// Separate render thread from main event loop with a thread-safe command
// queue, frame synchronization (vsync/present fence), lock-free damage
// accumulation, and per-output render thread option.

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

extern "C" {
struct wlr_output;
}

namespace eternal {

class Renderer;

// ---------------------------------------------------------------------------
// Render command -- a unit of work submitted from main -> render thread
// ---------------------------------------------------------------------------

enum class RenderCommandType : uint8_t {
    BeginFrame,       // Begin rendering for an output
    EndFrame,         // End + commit the frame
    RenderSurface,    // Render a client surface at a given position
    RenderRect,       // Render a solid rectangle
    RenderEffect,     // Render a compositor effect (blur, shadow, etc.)
    AddDamage,        // Accumulate damage for an output
    FullDamage,       // Mark full output damage
    Shutdown,         // Graceful shutdown signal
};

struct RenderBox {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

struct RenderColor {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

struct RenderCommand {
    RenderCommandType type = RenderCommandType::BeginFrame;
    wlr_output* output = nullptr;

    // For surface rendering
    void* surface = nullptr;      // wlr_surface*, opaque to avoid header deps
    RenderBox box{};
    float alpha = 1.0f;

    // For rect rendering
    RenderColor color{};

    // For effects
    std::function<void()> effect_fn;

    // Sequence number for ordering / fence
    uint64_t sequence = 0;
};

// ---------------------------------------------------------------------------
// Lock-free SPSC ring buffer for damage rectangles
// ---------------------------------------------------------------------------

class DamageAccumulator {
public:
    static constexpr size_t kCapacity = 256;

    /// Add a damage rectangle (lock-free, producer side).
    bool push(const RenderBox& damage);

    /// Drain all accumulated damage into the output vector (consumer side).
    size_t drain(std::vector<RenderBox>& out);

    /// Mark the entire output as damaged (atomic flag).
    void markFull() { full_damage_.store(true, std::memory_order_release); }

    /// Check and clear the full-damage flag.
    bool consumeFullDamage() {
        return full_damage_.exchange(false, std::memory_order_acq_rel);
    }

private:
    struct alignas(64) Slot {
        RenderBox box;
        std::atomic<bool> ready{false};
    };

    std::array<Slot, kCapacity> ring_{};
    alignas(64) std::atomic<size_t> write_pos_{0};
    alignas(64) std::atomic<size_t> read_pos_{0};
    std::atomic<bool> full_damage_{false};
};

// ---------------------------------------------------------------------------
// Frame fence -- synchronize frame presentation
// ---------------------------------------------------------------------------

class FrameFence {
public:
    /// Signal that the frame is complete (render thread calls this).
    void signal();

    /// Wait until the current frame is done (main thread calls this).
    /// Returns false on timeout.
    bool wait(int timeout_ms = 16);

    /// Reset for next frame.
    void reset();

    /// Whether the frame is complete.
    [[nodiscard]] bool isSignaled() const {
        return signaled_.load(std::memory_order_acquire);
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> signaled_{false};
};

// ---------------------------------------------------------------------------
// Render thread -- one per output (optional) or shared
// ---------------------------------------------------------------------------

class RenderThread {
public:
    RenderThread();
    ~RenderThread();

    RenderThread(const RenderThread&) = delete;
    RenderThread& operator=(const RenderThread&) = delete;

    /// Start the render thread. Pass the renderer to use for GL calls.
    /// The renderer must be valid for the lifetime of this thread.
    bool start(Renderer* renderer);

    /// Stop the render thread (blocks until joined).
    void stop();

    /// Whether the thread is running.
    [[nodiscard]] bool isRunning() const {
        return running_.load(std::memory_order_acquire);
    }

    // -- Command submission (called from main thread) ----------------------

    /// Submit a render command to the queue.
    void submit(RenderCommand cmd);

    /// Submit a batch of render commands atomically.
    void submitBatch(std::vector<RenderCommand> cmds);

    /// Signal the start of a new frame for the given output.
    void beginFrame(wlr_output* output);

    /// Signal the end of the current frame. Blocks until present if sync.
    void endFrame(wlr_output* output, bool sync = true);

    // -- Damage accumulation (lock-free, called from main thread) ----------

    /// Get the damage accumulator for an output.
    DamageAccumulator& damageFor(wlr_output* output);

    // -- Frame synchronization ---------------------------------------------

    /// Get the frame fence for synchronization.
    FrameFence& frameFence() { return fence_; }

    /// Current frame number.
    [[nodiscard]] uint64_t frameNumber() const {
        return frame_number_.load(std::memory_order_acquire);
    }

    // -- Statistics ---------------------------------------------------------

    /// Average time spent in the render thread per frame (ms).
    [[nodiscard]] double avgRenderTimeMs() const {
        return avg_render_time_ms_.load(std::memory_order_relaxed);
    }

    /// Number of frames rendered.
    [[nodiscard]] uint64_t framesRendered() const {
        return frames_rendered_.load(std::memory_order_relaxed);
    }

    /// Number of dropped frames (missed deadline).
    [[nodiscard]] uint64_t framesDropped() const {
        return frames_dropped_.load(std::memory_order_relaxed);
    }

private:
    void threadMain();
    void processCommand(const RenderCommand& cmd);

    Renderer* renderer_ = nullptr;
    std::thread thread_;
    std::atomic<bool> running_{false};

    // Command queue (MPSC: main produces, render thread consumes)
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::queue<RenderCommand> command_queue_;

    // Frame synchronization
    FrameFence fence_;
    std::atomic<uint64_t> frame_number_{0};
    uint64_t next_sequence_ = 0;

    // Per-output damage accumulators
    std::mutex damage_mutex_;
    std::vector<std::pair<wlr_output*, std::unique_ptr<DamageAccumulator>>> damage_accumulators_;

    // Statistics
    std::atomic<double> avg_render_time_ms_{0.0};
    std::atomic<uint64_t> frames_rendered_{0};
    std::atomic<uint64_t> frames_dropped_{0};
};

// ---------------------------------------------------------------------------
// Per-output render thread manager
// ---------------------------------------------------------------------------

class RenderThreadManager {
public:
    RenderThreadManager() = default;
    ~RenderThreadManager();

    RenderThreadManager(const RenderThreadManager&) = delete;
    RenderThreadManager& operator=(const RenderThreadManager&) = delete;

    /// Initialize with a shared renderer.
    void init(Renderer* renderer);

    /// Create a dedicated render thread for an output (per-output mode).
    RenderThread* createForOutput(wlr_output* output);

    /// Remove the render thread for an output.
    void removeForOutput(wlr_output* output);

    /// Get the render thread for an output, or the shared thread.
    RenderThread* getForOutput(wlr_output* output);

    /// Get the shared render thread (single-thread mode).
    RenderThread* shared() { return &shared_thread_; }

    /// Whether per-output threading is enabled.
    [[nodiscard]] bool isPerOutput() const { return per_output_; }

    /// Enable or disable per-output threading.
    void setPerOutput(bool enable) { per_output_ = enable; }

    /// Stop all render threads.
    void stopAll();

private:
    Renderer* renderer_ = nullptr;
    RenderThread shared_thread_;
    bool per_output_ = false;

    struct OutputThread {
        wlr_output* output = nullptr;
        std::unique_ptr<RenderThread> thread;
    };
    std::vector<OutputThread> output_threads_;
};

} // namespace eternal

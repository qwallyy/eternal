// Task 106: Multi-threading rendering implementation

#include "eternal/render/RenderThread.hpp"
#include "eternal/render/Renderer.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>

namespace eternal {

// ---------------------------------------------------------------------------
// DamageAccumulator -- lock-free SPSC ring buffer
// ---------------------------------------------------------------------------

bool DamageAccumulator::push(const RenderBox& damage) {
    size_t pos = write_pos_.load(std::memory_order_relaxed);
    size_t next = (pos + 1) % kCapacity;

    // Check if the ring is full
    if (next == read_pos_.load(std::memory_order_acquire)) {
        // Ring full -- mark full damage as fallback
        markFull();
        return false;
    }

    ring_[pos].box = damage;
    ring_[pos].ready.store(true, std::memory_order_release);
    write_pos_.store(next, std::memory_order_release);
    return true;
}

size_t DamageAccumulator::drain(std::vector<RenderBox>& out) {
    size_t count = 0;
    size_t pos = read_pos_.load(std::memory_order_relaxed);

    while (pos != write_pos_.load(std::memory_order_acquire)) {
        if (ring_[pos].ready.load(std::memory_order_acquire)) {
            out.push_back(ring_[pos].box);
            ring_[pos].ready.store(false, std::memory_order_release);
            ++count;
        }
        pos = (pos + 1) % kCapacity;
    }

    read_pos_.store(pos, std::memory_order_release);
    return count;
}

// ---------------------------------------------------------------------------
// FrameFence
// ---------------------------------------------------------------------------

void FrameFence::signal() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        signaled_.store(true, std::memory_order_release);
    }
    cv_.notify_all();
}

bool FrameFence::wait(int timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (signaled_.load(std::memory_order_acquire)) return true;

    return cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] {
        return signaled_.load(std::memory_order_acquire);
    });
}

void FrameFence::reset() {
    signaled_.store(false, std::memory_order_release);
}

// ---------------------------------------------------------------------------
// RenderThread
// ---------------------------------------------------------------------------

RenderThread::RenderThread() = default;

RenderThread::~RenderThread() {
    stop();
}

bool RenderThread::start(Renderer* renderer) {
    if (!renderer) return false;
    if (running_.load(std::memory_order_acquire)) return false;

    renderer_ = renderer;
    running_.store(true, std::memory_order_release);

    thread_ = std::thread([this] { threadMain(); });
    return true;
}

void RenderThread::stop() {
    if (!running_.load(std::memory_order_acquire)) return;

    // Submit shutdown command
    submit({.type = RenderCommandType::Shutdown});

    if (thread_.joinable()) {
        thread_.join();
    }

    running_.store(false, std::memory_order_release);
}

void RenderThread::submit(RenderCommand cmd) {
    cmd.sequence = next_sequence_++;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        command_queue_.push(std::move(cmd));
    }
    queue_cv_.notify_one();
}

void RenderThread::submitBatch(std::vector<RenderCommand> cmds) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        for (auto& cmd : cmds) {
            cmd.sequence = next_sequence_++;
            command_queue_.push(std::move(cmd));
        }
    }
    queue_cv_.notify_one();
}

void RenderThread::beginFrame(wlr_output* output) {
    RenderCommand cmd;
    cmd.type = RenderCommandType::BeginFrame;
    cmd.output = output;
    submit(std::move(cmd));
}

void RenderThread::endFrame(wlr_output* output, bool sync) {
    fence_.reset();

    RenderCommand cmd;
    cmd.type = RenderCommandType::EndFrame;
    cmd.output = output;
    submit(std::move(cmd));

    if (sync) {
        // Block until the frame is presented
        fence_.wait(32);  // 2x frame budget at 60Hz
    }
}

DamageAccumulator& RenderThread::damageFor(wlr_output* output) {
    std::lock_guard<std::mutex> lock(damage_mutex_);
    for (auto& [o, acc] : damage_accumulators_) {
        if (o == output) return *acc;
    }
    damage_accumulators_.emplace_back(output, std::make_unique<DamageAccumulator>());
    return *damage_accumulators_.back().second;
}

// ---------------------------------------------------------------------------
// Thread main loop
// ---------------------------------------------------------------------------

void RenderThread::threadMain() {
    while (running_.load(std::memory_order_acquire)) {
        RenderCommand cmd;

        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] {
                return !command_queue_.empty() ||
                       !running_.load(std::memory_order_acquire);
            });

            if (command_queue_.empty()) continue;

            cmd = std::move(command_queue_.front());
            command_queue_.pop();
        }

        if (cmd.type == RenderCommandType::Shutdown) {
            running_.store(false, std::memory_order_release);
            break;
        }

        auto start = std::chrono::high_resolution_clock::now();
        processCommand(cmd);
        auto end = std::chrono::high_resolution_clock::now();

        // Update statistics for EndFrame commands
        if (cmd.type == RenderCommandType::EndFrame) {
            double ms = std::chrono::duration<double, std::milli>(end - start).count();
            uint64_t n = frames_rendered_.fetch_add(1, std::memory_order_relaxed) + 1;

            // Exponential moving average
            double prev = avg_render_time_ms_.load(std::memory_order_relaxed);
            double alpha = std::min(1.0 / static_cast<double>(n), 0.1);
            avg_render_time_ms_.store(prev * (1.0 - alpha) + ms * alpha,
                                      std::memory_order_relaxed);

            frame_number_.fetch_add(1, std::memory_order_release);
            fence_.signal();
        }
    }
}

void RenderThread::processCommand(const RenderCommand& cmd) {
    if (!renderer_) return;

    switch (cmd.type) {
    case RenderCommandType::BeginFrame: {
        if (cmd.output) {
            // Drain accumulated damage before beginning the frame
            auto& acc = damageFor(cmd.output);
            if (acc.consumeFullDamage()) {
                renderer_->addFullDamage(cmd.output);
            } else {
                std::vector<RenderBox> damage_rects;
                acc.drain(damage_rects);
                for (const auto& d : damage_rects) {
                    renderer_->addDamage(cmd.output, {d.x, d.y, d.width, d.height});
                }
            }
            renderer_->begin(cmd.output);
        }
        break;
    }

    case RenderCommandType::EndFrame:
        renderer_->end();
        break;

    case RenderCommandType::RenderSurface:
        if (cmd.surface) {
            renderer_->renderSurface(
                static_cast<wlr_surface*>(cmd.surface),
                {cmd.box.x, cmd.box.y, cmd.box.width, cmd.box.height});
        }
        break;

    case RenderCommandType::RenderRect:
        renderer_->renderRect(
            {cmd.box.x, cmd.box.y, cmd.box.width, cmd.box.height},
            {cmd.color.r, cmd.color.g, cmd.color.b, cmd.color.a});
        break;

    case RenderCommandType::RenderEffect:
        if (cmd.effect_fn) {
            cmd.effect_fn();
        }
        break;

    case RenderCommandType::AddDamage:
        if (cmd.output) {
            renderer_->addDamage(cmd.output,
                                 {cmd.box.x, cmd.box.y, cmd.box.width, cmd.box.height});
        }
        break;

    case RenderCommandType::FullDamage:
        if (cmd.output) {
            renderer_->addFullDamage(cmd.output);
        }
        break;

    case RenderCommandType::Shutdown:
        // Handled in threadMain
        break;
    }
}

// ---------------------------------------------------------------------------
// RenderThreadManager
// ---------------------------------------------------------------------------

RenderThreadManager::~RenderThreadManager() {
    stopAll();
}

void RenderThreadManager::init(Renderer* renderer) {
    renderer_ = renderer;
    shared_thread_.start(renderer);
}

RenderThread* RenderThreadManager::createForOutput(wlr_output* output) {
    if (!renderer_ || !per_output_) return &shared_thread_;

    // Check if already exists
    for (auto& ot : output_threads_) {
        if (ot.output == output) return ot.thread.get();
    }

    auto thread = std::make_unique<RenderThread>();
    thread->start(renderer_);

    output_threads_.push_back({output, std::move(thread)});
    return output_threads_.back().thread.get();
}

void RenderThreadManager::removeForOutput(wlr_output* output) {
    auto it = std::find_if(output_threads_.begin(), output_threads_.end(),
                           [output](const auto& ot) { return ot.output == output; });
    if (it != output_threads_.end()) {
        it->thread->stop();
        output_threads_.erase(it);
    }
}

RenderThread* RenderThreadManager::getForOutput(wlr_output* output) {
    if (per_output_) {
        for (auto& ot : output_threads_) {
            if (ot.output == output) return ot.thread.get();
        }
    }
    return &shared_thread_;
}

void RenderThreadManager::stopAll() {
    for (auto& ot : output_threads_) {
        ot.thread->stop();
    }
    output_threads_.clear();
    shared_thread_.stop();
}

} // namespace eternal

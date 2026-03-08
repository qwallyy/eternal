// Task 109: Profiler implementation

#include "eternal/utils/Profiler.hpp"
#include "eternal/utils/Logger.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>

namespace eternal {

// ---------------------------------------------------------------------------
// LatencyHistogram
// ---------------------------------------------------------------------------

LatencyHistogram::LatencyHistogram() {
    reset();
}

void LatencyHistogram::record(double ms) {
    int bucket = static_cast<int>(ms / bucket_width_);
    bucket = std::clamp(bucket, 0, kBucketCount - 1);
    buckets_[bucket].fetch_add(1, std::memory_order_relaxed);
    total_.fetch_add(1, std::memory_order_relaxed);
}

uint64_t LatencyHistogram::bucketCount(int bucket) const {
    if (bucket < 0 || bucket >= kBucketCount) return 0;
    return buckets_[bucket].load(std::memory_order_relaxed);
}

double LatencyHistogram::bucketEdge(int bucket) const {
    return (bucket + 1) * bucket_width_;
}

uint64_t LatencyHistogram::totalSamples() const {
    return total_.load(std::memory_order_relaxed);
}

double LatencyHistogram::percentile(double p) const {
    uint64_t total = totalSamples();
    if (total == 0) return 0.0;

    uint64_t target = static_cast<uint64_t>(std::ceil(total * p));
    uint64_t cumulative = 0;

    for (int i = 0; i < kBucketCount; ++i) {
        cumulative += bucketCount(i);
        if (cumulative >= target) {
            return bucketEdge(i);
        }
    }
    return kMaxMs;
}

double LatencyHistogram::p50() const { return percentile(0.50); }
double LatencyHistogram::p95() const { return percentile(0.95); }
double LatencyHistogram::p99() const { return percentile(0.99); }

void LatencyHistogram::reset() {
    for (auto& b : buckets_) {
        b.store(0, std::memory_order_relaxed);
    }
    total_.store(0, std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------
// RollingAverage
// ---------------------------------------------------------------------------

RollingAverage::RollingAverage(int window_size)
    : capacity_(std::max(window_size, 1)) {
    samples_.resize(capacity_, 0.0);
}

void RollingAverage::push(double value) {
    if (size_ < capacity_) {
        samples_[size_] = value;
        sum_ += value;
        ++size_;
    } else {
        sum_ -= samples_[head_];
        samples_[head_] = value;
        sum_ += value;
    }
    head_ = (head_ + 1) % capacity_;
}

double RollingAverage::average() const {
    if (size_ == 0) return 0.0;
    return sum_ / size_;
}

double RollingAverage::min() const {
    if (size_ == 0) return 0.0;
    double m = samples_[0];
    for (int i = 1; i < size_; ++i) {
        m = std::min(m, samples_[i]);
    }
    return m;
}

double RollingAverage::max() const {
    if (size_ == 0) return 0.0;
    double m = samples_[0];
    for (int i = 1; i < size_; ++i) {
        m = std::max(m, samples_[i]);
    }
    return m;
}

int RollingAverage::count() const {
    return size_;
}

void RollingAverage::reset() {
    head_ = 0;
    size_ = 0;
    sum_ = 0.0;
    std::fill(samples_.begin(), samples_.end(), 0.0);
}

// ---------------------------------------------------------------------------
// Profiler singleton
// ---------------------------------------------------------------------------

Profiler& Profiler::instance() {
    static Profiler s_instance;
    return s_instance;
}

Profiler::Profiler() = default;

// -- Frame timing ---------------------------------------------------------

void Profiler::frameBegin() {
    if (!enabled_) return;
    frame_start_ = Clock::now();
}

void Profiler::frameEnd() {
    if (!enabled_) return;

    auto now = Clock::now();
    last_frame_ms_ = Duration(now - frame_start_).count();
    ++total_frames_;

    frame_time_avg_.push(last_frame_ms_);
    frame_histogram_.record(last_frame_ms_);

    double current_fps = (last_frame_ms_ > 0.001) ? (1000.0 / last_frame_ms_) : 0.0;
    fps_avg_.push(current_fps);

    // Update history
    {
        std::lock_guard<std::mutex> lock(history_mutex_);
        frame_time_history_.push_back(last_frame_ms_);
        fps_history_.push_back(current_fps);
        if (static_cast<int>(frame_time_history_.size()) > kMaxHistory) {
            frame_time_history_.erase(frame_time_history_.begin());
        }
        if (static_cast<int>(fps_history_.size()) > kMaxHistory) {
            fps_history_.erase(fps_history_.begin());
        }
    }

    // Invoke callback
    if (frame_callback_) {
        frame_callback_(*this);
    }
}

double Profiler::fps() const {
    return fps_avg_.average();
}

double Profiler::avgFrameTimeMs() const {
    return frame_time_avg_.average();
}

// -- Per-stage timing -----------------------------------------------------

ScopeTimer Profiler::timeStage(ProfileStage stage) {
    int idx = static_cast<int>(stage);
    return ScopeTimer([this, idx](double ms) {
        if (idx >= 0 && idx < kStageCount) {
            stage_timings_[idx].record(ms);
        }
    });
}

const StageTiming& Profiler::stageTiming(ProfileStage stage) const {
    int idx = static_cast<int>(stage);
    if (idx >= 0 && idx < kStageCount) {
        return stage_timings_[idx];
    }
    static const StageTiming empty{};
    return empty;
}

// -- Output ---------------------------------------------------------------

std::string Profiler::dump(OutputFormat format) const {
    std::ostringstream ss;

    switch (format) {
    case OutputFormat::Log: {
        ss << "=== Eternal Profiler ===\n";
        ss << "Frames: " << total_frames_ << "\n";
        ss << "FPS: " << fps() << " (avg)\n";
        ss << "Frame time: " << avgFrameTimeMs() << " ms (avg), "
           << frame_time_avg_.min() << " ms (min), "
           << frame_time_avg_.max() << " ms (max)\n";
        ss << "Latency: p50=" << frame_histogram_.p50()
           << "ms p95=" << frame_histogram_.p95()
           << "ms p99=" << frame_histogram_.p99() << "ms\n";
        ss << "--- Per-stage ---\n";
        for (int i = 0; i < kStageCount; ++i) {
            auto stage = static_cast<ProfileStage>(i);
            const auto& t = stage_timings_[i];
            if (t.count > 0) {
                ss << "  " << stageName(stage) << ": "
                   << t.avg_ms << " ms (avg), "
                   << t.max_ms << " ms (max), "
                   << t.count << " samples\n";
            }
        }
        break;
    }

    case OutputFormat::JSON: {
        ss << "{";
        ss << "\"frames\":" << total_frames_;
        ss << ",\"fps\":" << fps();
        ss << ",\"frame_time_avg_ms\":" << avgFrameTimeMs();
        ss << ",\"frame_time_min_ms\":" << frame_time_avg_.min();
        ss << ",\"frame_time_max_ms\":" << frame_time_avg_.max();
        ss << ",\"latency_p50_ms\":" << frame_histogram_.p50();
        ss << ",\"latency_p95_ms\":" << frame_histogram_.p95();
        ss << ",\"latency_p99_ms\":" << frame_histogram_.p99();
        ss << ",\"stages\":{";
        bool first = true;
        for (int i = 0; i < kStageCount; ++i) {
            auto stage = static_cast<ProfileStage>(i);
            const auto& t = stage_timings_[i];
            if (t.count > 0) {
                if (!first) ss << ",";
                ss << "\"" << stageName(stage) << "\":{";
                ss << "\"avg_ms\":" << t.avg_ms;
                ss << ",\"max_ms\":" << t.max_ms;
                ss << ",\"last_ms\":" << t.last_ms;
                ss << ",\"count\":" << t.count;
                ss << "}";
                first = false;
            }
        }
        ss << "}}";
        break;
    }

    case OutputFormat::CSV: {
        // Header: frame,fps,frame_time_ms,stage1_ms,...
        ss << total_frames_ << ","
           << fps() << ","
           << last_frame_ms_;
        for (int i = 0; i < kStageCount; ++i) {
            ss << "," << stage_timings_[i].last_ms;
        }
        ss << "\n";
        break;
    }
    }

    return ss.str();
}

// -- Debug overlay data ---------------------------------------------------

std::vector<double> Profiler::getFrameTimeHistory(int count) const {
    std::lock_guard<std::mutex> lock(history_mutex_);
    int n = std::min(count, static_cast<int>(frame_time_history_.size()));
    if (n == 0) return {};
    return {frame_time_history_.end() - n, frame_time_history_.end()};
}

std::vector<double> Profiler::getFPSHistory(int count) const {
    std::lock_guard<std::mutex> lock(history_mutex_);
    int n = std::min(count, static_cast<int>(fps_history_.size()));
    if (n == 0) return {};
    return {fps_history_.end() - n, fps_history_.end()};
}

// -- Reset ----------------------------------------------------------------

void Profiler::reset() {
    last_frame_ms_ = 0.0;
    total_frames_ = 0;
    frame_time_avg_.reset();
    fps_avg_.reset();
    frame_histogram_.reset();
    for (auto& s : stage_timings_) s.reset();

    std::lock_guard<std::mutex> lock(history_mutex_);
    frame_time_history_.clear();
    fps_history_.clear();
}

} // namespace eternal

#pragma once

#include <eternal/animation/Animation.hpp>

#include <cstdint>
#include <vector>

namespace eternal {

class AnimationEngine;

enum class TimelineState {
    Stopped,
    Playing,
    Paused,
    Finished
};

struct TimelineEntry {
    AnimationID animationId = 0;
    float startTime = 0.0f;   // ms offset from timeline start
};

class Timeline {
public:
    explicit Timeline(AnimationEngine& engine);
    ~Timeline() = default;

    /// Add an animation at a specific start time (ms from timeline start).
    void add(AnimationID animId, float startTime);

    /// Add an animation to start after another one, with optional delay.
    void addAfter(AnimationID animId, AnimationID prevId, float delay = 0.0f);

    void play();
    void pause();

    /// Seek to an absolute time (ms).
    void seek(float time);

    /// Total duration of the timeline in ms.
    [[nodiscard]] float getDuration() const;

    [[nodiscard]] bool isFinished() const;
    [[nodiscard]] TimelineState getState() const { return m_state; }
    [[nodiscard]] float getCurrentTime() const { return m_currentTime; }
    [[nodiscard]] const std::vector<TimelineEntry>& getEntries() const { return m_entries; }

private:
    AnimationEngine& m_engine;
    std::vector<TimelineEntry> m_entries;
    float m_totalDuration = 0.0f;
    float m_currentTime = 0.0f;
    TimelineState m_state = TimelineState::Stopped;
};

} // namespace eternal

#include <eternal/animation/Timeline.hpp>
#include <eternal/animation/AnimationEngine.hpp>

#include <algorithm>

namespace eternal {

Timeline::Timeline(AnimationEngine& engine)
    : m_engine(engine) {}

void Timeline::add(AnimationID animId, float startTime) {
    m_entries.push_back({animId, startTime});

    // Recalculate total duration based on entry start times + animation durations
    m_totalDuration = 0.0f;
    for (const auto& entry : m_entries) {
        float entryEnd = entry.startTime;
        // Query the animation for its duration
        if (const auto* anim = m_engine.getAnimation(entry.animationId)) {
            entryEnd += anim->getDuration();
        }
        if (entryEnd > m_totalDuration)
            m_totalDuration = entryEnd;
    }
}

void Timeline::addAfter(AnimationID animId, AnimationID prevId, float delay) {
    float prevEnd = 0.0f;
    for (const auto& entry : m_entries) {
        if (entry.animationId == prevId) {
            prevEnd = entry.startTime;
            // Add the duration of the previous animation
            if (const auto* anim = m_engine.getAnimation(prevId)) {
                prevEnd += anim->getDuration();
            }
            break;
        }
    }
    add(animId, prevEnd + delay);
}

void Timeline::play() {
    m_state = TimelineState::Playing;
}

void Timeline::pause() {
    if (m_state == TimelineState::Playing)
        m_state = TimelineState::Paused;
}

void Timeline::seek(float time) {
    m_currentTime = std::clamp(time, 0.0f, m_totalDuration);
}

float Timeline::getDuration() const {
    return m_totalDuration;
}

bool Timeline::isFinished() const {
    return m_state == TimelineState::Finished;
}

} // namespace eternal

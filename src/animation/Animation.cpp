#include <eternal/animation/Animation.hpp>

#include <algorithm>

namespace eternal {

Animation::Animation(AnimationID id, float from, float to, float duration_ms,
                     const std::string& curveName, EasingFunction easing)
    : m_id(id)
    , m_from(from)
    , m_to(to)
    , m_duration(duration_ms)
    , m_elapsed(0.0f)
    , m_curveName(curveName)
    , m_state(AnimationState::Running)
    , m_easing(std::move(easing)) {}

float Animation::update(float dt) {
    if (m_state != AnimationState::Running)
        return getValue();

    m_elapsed += dt;

    if (m_elapsed >= m_duration) {
        m_elapsed = m_duration;
        m_state = AnimationState::Finished;
    }

    float value = getValue();

    if (onUpdate)
        onUpdate(value);

    if (m_state == AnimationState::Finished && onComplete)
        onComplete();

    return value;
}

void Animation::pause() {
    if (m_state == AnimationState::Running)
        m_state = AnimationState::Paused;
}

void Animation::resume() {
    if (m_state == AnimationState::Paused)
        m_state = AnimationState::Running;
}

void Animation::reverse() {
    std::swap(m_from, m_to);
    m_elapsed = m_duration - m_elapsed;
    if (m_elapsed < 0.0f)
        m_elapsed = 0.0f;
    if (m_state == AnimationState::Finished)
        m_state = AnimationState::Running;
}

void Animation::retarget(float newTo, float newDuration_ms) {
    // Current value becomes new "from", preserve continuity
    float currentVal = getValue();
    m_from = currentVal;
    m_to = newTo;
    m_elapsed = 0.0f;
    if (newDuration_ms > 0.0f) {
        m_duration = newDuration_ms;
    }
    if (m_state == AnimationState::Finished)
        m_state = AnimationState::Running;
}

void Animation::reset() {
    m_id = 0;
    m_from = 0.0f;
    m_to = 1.0f;
    m_duration = 0.0f;
    m_elapsed = 0.0f;
    m_curveName.clear();
    m_state = AnimationState::Running;
    m_easing = nullptr;
    m_priority = AnimationPriority::Normal;
    m_pooled = false;
    onUpdate = nullptr;
    onComplete = nullptr;
}

bool Animation::isFinished() const {
    return m_state == AnimationState::Finished;
}

float Animation::getValue() const {
    if (m_duration <= 0.0f)
        return m_to;

    float t = std::clamp(m_elapsed / m_duration, 0.0f, 1.0f);

    if (m_easing)
        t = m_easing(t);

    return m_from + (m_to - m_from) * t;
}

} // namespace eternal

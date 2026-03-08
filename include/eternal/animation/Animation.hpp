#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace eternal {

using AnimationID = uint64_t;

enum class AnimationState {
    Running,
    Paused,
    Finished
};

/// Priority levels for animations. Higher priority animations cannot be
/// interrupted by lower-priority ones targeting the same property.
enum class AnimationPriority : uint8_t {
    Low    = 0,
    Normal = 1,
    High   = 2,
};

using EasingFunction = std::function<float(float)>;

class Animation {
public:
    Animation() = default;
    Animation(AnimationID id, float from, float to, float duration_ms,
              const std::string& curveName, EasingFunction easing);
    ~Animation() = default;

    /// Advance the animation by dt milliseconds. Returns current value.
    float update(float dt);

    void pause();
    void resume();
    void reverse();

    /// Retarget the animation mid-flight to a new destination value.
    /// Preserves current value and remaining momentum for smooth transitions.
    void retarget(float newTo, float newDuration_ms = -1.0f);

    [[nodiscard]] bool isFinished() const;
    [[nodiscard]] float getValue() const;

    [[nodiscard]] AnimationID getId() const { return m_id; }
    [[nodiscard]] AnimationState getState() const { return m_state; }
    [[nodiscard]] float getFrom() const { return m_from; }
    [[nodiscard]] float getTo() const { return m_to; }
    [[nodiscard]] float getDuration() const { return m_duration; }
    [[nodiscard]] float getElapsed() const { return m_elapsed; }
    [[nodiscard]] const std::string& getCurveName() const { return m_curveName; }

    void setPriority(AnimationPriority p) { m_priority = p; }
    [[nodiscard]] AnimationPriority getPriority() const { return m_priority; }

    /// Mark this animation slot as available for reuse by the pool.
    void markPooled() { m_pooled = true; }
    [[nodiscard]] bool isPooled() const { return m_pooled; }
    void reset();

    /// Called every update with the current value.
    std::function<void(float)> onUpdate;

    /// Called once when the animation finishes.
    std::function<void()> onComplete;

private:
    AnimationID m_id = 0;
    float m_from = 0.0f;
    float m_to = 1.0f;
    float m_duration = 0.0f;
    float m_elapsed = 0.0f;
    std::string m_curveName;
    AnimationState m_state = AnimationState::Running;
    EasingFunction m_easing;
    AnimationPriority m_priority = AnimationPriority::Normal;
    bool m_pooled = false;
};

} // namespace eternal

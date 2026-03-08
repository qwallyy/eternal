#pragma once

namespace eternal {

class SpringAnimation {
public:
    SpringAnimation() = default;
    SpringAnimation(float stiffness, float damping, float mass);
    ~SpringAnimation() = default;

    /// Advance the spring simulation by dt milliseconds.
    void update(float dt);

    /// Whether the spring has settled within the rest threshold.
    [[nodiscard]] bool isAtRest() const;

    /// Set a new target value. The spring will begin moving toward it.
    void setTarget(float target);

    [[nodiscard]] float getPosition() const { return m_position; }
    [[nodiscard]] float getVelocity() const { return m_velocity; }
    [[nodiscard]] float getTarget() const { return m_target; }

    void setPosition(float pos) { m_position = pos; }
    void setVelocity(float vel) { m_velocity = vel; }

    void setStiffness(float s) { m_stiffness = s; }
    void setDamping(float d) { m_damping = d; }
    void setMass(float m) { m_mass = m; }
    void setRestThreshold(float t) { m_restThreshold = t; }

    [[nodiscard]] float getStiffness() const { return m_stiffness; }
    [[nodiscard]] float getDamping() const { return m_damping; }
    [[nodiscard]] float getMass() const { return m_mass; }
    [[nodiscard]] float getRestThreshold() const { return m_restThreshold; }

    /// Reset position to 0 and velocity to 0.
    void reset();

private:
    float m_stiffness = 200.0f;
    float m_damping = 10.0f;
    float m_mass = 1.0f;
    float m_target = 0.0f;
    float m_velocity = 0.0f;
    float m_position = 0.0f;
    float m_restThreshold = 0.001f;
};

} // namespace eternal

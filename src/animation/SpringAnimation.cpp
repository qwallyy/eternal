#include <eternal/animation/SpringAnimation.hpp>

#include <cmath>

namespace eternal {

SpringAnimation::SpringAnimation(float stiffness, float damping, float mass)
    : m_stiffness(stiffness), m_damping(damping), m_mass(mass) {}

void SpringAnimation::update(float dt) {
    // Convert ms to seconds for physics
    float dtSec = dt / 1000.0f;

    // Clamp dt to prevent instability from large time steps
    if (dtSec > 0.064f) dtSec = 0.064f;

    // Use 4th-order Runge-Kutta integration for accurate spring physics.
    // The ODE is:
    //   x' = v
    //   v' = (-stiffness * (x - target) - damping * v) / mass

    // Step size subdivision for stability at high stiffness values.
    // Sub-step if dt is large relative to the natural frequency.
    float omega = std::sqrt(m_stiffness / m_mass);
    float maxStep = (omega > 0.0f) ? (1.0f / (omega * 4.0f)) : dtSec;
    maxStep = std::max(maxStep, 0.0001f);

    float remaining = dtSec;
    while (remaining > 1e-6f) {
        float h = std::min(remaining, maxStep);

        float x = m_position;
        float v = m_velocity;

        // k1
        float a1 = (-m_stiffness * (x - m_target) - m_damping * v) / m_mass;
        float v1 = v;

        // k2
        float x2 = x + v1 * h * 0.5f;
        float v2 = v + a1 * h * 0.5f;
        float a2 = (-m_stiffness * (x2 - m_target) - m_damping * v2) / m_mass;

        // k3
        float x3 = x + v2 * h * 0.5f;
        float v3 = v + a2 * h * 0.5f;
        float a3 = (-m_stiffness * (x3 - m_target) - m_damping * v3) / m_mass;

        // k4
        float x4 = x + v3 * h;
        float v4 = v + a3 * h;
        float a4 = (-m_stiffness * (x4 - m_target) - m_damping * v4) / m_mass;

        // Combine
        m_position += (h / 6.0f) * (v1 + 2.0f * v2 + 2.0f * v3 + v4);
        m_velocity += (h / 6.0f) * (a1 + 2.0f * a2 + 2.0f * a3 + a4);

        remaining -= h;
    }
}

bool SpringAnimation::isAtRest() const {
    float displacement = std::fabs(m_position - m_target);
    float speed = std::fabs(m_velocity);
    return displacement < m_restThreshold && speed < m_restThreshold;
}

void SpringAnimation::setTarget(float target) {
    m_target = target;
}

void SpringAnimation::reset() {
    m_position = 0.0f;
    m_velocity = 0.0f;
}

} // namespace eternal

#include <eternal/animation/BezierCurve.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace eternal {

// ============================================================================
// BezierCurve
// ============================================================================

BezierCurve::BezierCurve(float p1x, float p1y, float p2x, float p2y)
    : m_p1x(std::clamp(p1x, 0.0f, 1.0f))
    , m_p1y(p1y)
    , m_p2x(std::clamp(p2x, 0.0f, 1.0f))
    , m_p2y(p2y)
{
    // Precompute polynomial coefficients for the cubic Bezier:
    //   B(s) = C*s + B*s^2 + A*s^3
    // where the endpoints are (0,0) and (1,1).
    m_cx = 3.0f * m_p1x;
    m_bx = 3.0f * (m_p2x - m_p1x) - m_cx;
    m_ax = 1.0f - m_cx - m_bx;

    m_cy = 3.0f * m_p1y;
    m_by = 3.0f * (m_p2y - m_p1y) - m_cy;
    m_ay = 1.0f - m_cy - m_by;
}

float BezierCurve::evaluate(float x) const {
    // Boundary conditions
    if (x <= 0.0f) return 0.0f;
    if (x >= 1.0f) return 1.0f;

    // Linear shortcut: if both control points lie on the diagonal
    if (m_p1x == m_p1y && m_p2x == m_p2y)
        return x;

    // Build LUT on first use for fast initial guesses
    if (!m_lutBuilt)
        buildLUT();

    float s = solveCurveX(x);
    return sampleCurveY(s);
}

float BezierCurve::sampleCurveX(float s) const {
    // ((ax * s + bx) * s + cx) * s
    return ((m_ax * s + m_bx) * s + m_cx) * s;
}

float BezierCurve::sampleCurveY(float s) const {
    return ((m_ay * s + m_by) * s + m_cy) * s;
}

float BezierCurve::sampleCurveDerivativeX(float s) const {
    // d/ds [ax*s^3 + bx*s^2 + cx*s] = 3*ax*s^2 + 2*bx*s + cx
    return (3.0f * m_ax * s + 2.0f * m_bx) * s + m_cx;
}

void BezierCurve::buildLUT() const {
    // Sample the x(s) function at evenly spaced s values and store
    // the mapping for binary-search initial guesses.
    for (int i = 0; i < kLUTSize; ++i) {
        float s = static_cast<float>(i) / static_cast<float>(kLUTSize - 1);
        m_lut[i] = ((m_ax * s + m_bx) * s + m_cx) * s;
    }
    m_lutBuilt = true;
}

float BezierCurve::lutGuess(float x) const {
    // Binary search through the LUT to find a good starting s for Newton.
    // The LUT stores x values at evenly spaced s; we find the bracket.
    int lo = 0;
    int hi = kLUTSize - 1;

    // Linear scan is faster for small tables
    for (int i = 1; i < kLUTSize; ++i) {
        if (m_lut[i] >= x) {
            hi = i;
            lo = i - 1;
            break;
        }
    }

    // Linearly interpolate s within the bracket
    float xLo = m_lut[lo];
    float xHi = m_lut[hi];
    float range = xHi - xLo;

    float sLo = static_cast<float>(lo) / static_cast<float>(kLUTSize - 1);
    float sHi = static_cast<float>(hi) / static_cast<float>(kLUTSize - 1);

    if (range > 1e-9f) {
        float frac = (x - xLo) / range;
        return sLo + frac * (sHi - sLo);
    }
    return (sLo + sHi) * 0.5f;
}

float BezierCurve::solveCurveX(float x) const {
    constexpr int kNewtonIterations = 8;
    constexpr float kNewtonEpsilon = 1e-7f;
    constexpr int kBisectionIterations = 32;
    constexpr float kBisectionEpsilon = 1e-7f;

    // Use LUT for initial guess
    float s = lutGuess(x);

    // Newton-Raphson iteration
    for (int i = 0; i < kNewtonIterations; ++i) {
        float err = sampleCurveX(s) - x;
        if (std::fabs(err) < kNewtonEpsilon)
            return s;

        float dx = sampleCurveDerivativeX(s);
        // If derivative is near zero, Newton can't proceed safely
        if (std::fabs(dx) < 1e-10f)
            break;

        float step = err / dx;

        // Clamp the Newton step to avoid divergence
        // (keep s in a reasonable range)
        float s_new = s - step;
        if (s_new < 0.0f) s_new = 0.0f;
        if (s_new > 1.0f) s_new = 1.0f;

        // Check for convergence before accepting
        float err_new = sampleCurveX(s_new) - x;
        if (std::fabs(err_new) < kNewtonEpsilon)
            return s_new;

        // Only accept if we actually improved
        if (std::fabs(err_new) < std::fabs(err)) {
            s = s_new;
        } else {
            // Newton failed to improve -- fall through to bisection
            break;
        }
    }

    // Bisection fallback -- robust and guaranteed to converge
    float lo = 0.0f;
    float hi = 1.0f;
    s = x;  // Reset to a safe guess

    for (int i = 0; i < kBisectionIterations; ++i) {
        float val = sampleCurveX(s);
        if (std::fabs(val - x) < kBisectionEpsilon)
            return s;
        if (val < x)
            lo = s;
        else
            hi = s;
        s = (lo + hi) * 0.5f;
    }
    return s;
}

// ============================================================================
// Static presets
// ============================================================================

BezierCurve BezierCurve::linear()    { return {0.0f,  0.0f, 1.0f, 1.0f}; }
BezierCurve BezierCurve::easeIn()    { return {0.42f, 0.0f, 1.0f, 1.0f}; }
BezierCurve BezierCurve::easeOut()   { return {0.0f,  0.0f, 0.58f, 1.0f}; }
BezierCurve BezierCurve::easeInOut() { return {0.42f, 0.0f, 0.58f, 1.0f}; }
BezierCurve BezierCurve::overshot()  { return {0.05f, 0.9f, 0.1f,  1.1f}; }
BezierCurve BezierCurve::spring()    { return {0.15f, 1.2f, 0.3f,  1.0f}; }

// ============================================================================
// BezierCurveManager
// ============================================================================

BezierCurveManager::BezierCurveManager() {
    // Register built-in presets and eagerly build their LUTs
    m_curves["linear"]    = BezierCurve::linear();
    m_curves["easeIn"]    = BezierCurve::easeIn();
    m_curves["easeOut"]   = BezierCurve::easeOut();
    m_curves["easeInOut"] = BezierCurve::easeInOut();
    m_curves["overshot"]  = BezierCurve::overshot();
    m_curves["spring"]    = BezierCurve::spring();

    // Pre-build lookup tables for all built-in curves
    for (auto& [name, curve] : m_curves) {
        curve.buildLUT();
    }
}

void BezierCurveManager::add(const std::string& name, const BezierCurve& curve) {
    m_curves[name] = curve;
}

void BezierCurveManager::add(const std::string& name,
                              float p1x, float p1y, float p2x, float p2y) {
    // Control point x values are clamped in the BezierCurve constructor
    m_curves[name] = BezierCurve(p1x, p1y, p2x, p2y);
}

BezierCurve BezierCurveManager::get(const std::string& name) const {
    auto it = m_curves.find(name);
    if (it != m_curves.end())
        return it->second;
    return BezierCurve::linear();
}

std::function<float(float)> BezierCurveManager::getEasingFunction(const std::string& name) const {
    BezierCurve curve = get(name);
    return [curve](float t) -> float {
        return curve.evaluate(t);
    };
}

bool BezierCurveManager::has(const std::string& name) const {
    return m_curves.contains(name);
}

void BezierCurveManager::remove(const std::string& name) {
    m_curves.erase(name);
}

std::vector<std::string> BezierCurveManager::getNames() const {
    std::vector<std::string> names;
    names.reserve(m_curves.size());
    for (const auto& [name, _] : m_curves) {
        names.push_back(name);
    }
    return names;
}

} // namespace eternal

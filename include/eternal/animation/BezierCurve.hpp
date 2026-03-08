#pragma once

#include <array>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace eternal {

/// Cubic Bezier curve for animation easing.
///
/// Control points (p1x, p1y) and (p2x, p2y) define the shape.
/// The curve maps a progress value x in [0,1] to an eased output y in
/// approximately [0,1] (y may exceed [0,1] for overshoot effects).
///
/// Implementation uses Newton-Raphson iteration with bisection fallback
/// for numerically stable parametric t solving, plus a lookup table cache
/// for frequently evaluated curves.
class BezierCurve {
public:
    BezierCurve() = default;

    /// Construct with control points. X coordinates are clamped to [0,1].
    BezierCurve(float p1x, float p1y, float p2x, float p2y);

    /// Evaluate the curve: given x in [0,1], returns eased y.
    /// Uses Newton-Raphson to solve the parametric x -> t mapping,
    /// with bisection fallback when Newton fails to converge.
    [[nodiscard]] float evaluate(float x) const;

    [[nodiscard]] float getP1X() const { return m_p1x; }
    [[nodiscard]] float getP1Y() const { return m_p1y; }
    [[nodiscard]] float getP2X() const { return m_p2x; }
    [[nodiscard]] float getP2Y() const { return m_p2y; }

    /// Build the internal lookup table for fast repeated evaluation.
    /// Called automatically on first evaluate(), but can be called
    /// eagerly for preloading.
    void buildLUT() const;

    /// Whether the LUT has been built.
    [[nodiscard]] bool hasLUT() const { return m_lutBuilt; }

    // ---- Static presets ----
    static BezierCurve linear();
    static BezierCurve easeIn();       // (0.42, 0, 1, 1)
    static BezierCurve easeOut();      // (0, 0, 0.58, 1)
    static BezierCurve easeInOut();    // (0.42, 0, 0.58, 1)
    static BezierCurve overshot();     // (0.05, 0.9, 0.1, 1.1)
    static BezierCurve spring();       // (0.15, 1.2, 0.3, 1.0)

private:
    /// Compute the x value of the cubic bezier at parameter s.
    [[nodiscard]] float sampleCurveX(float s) const;

    /// Compute the y value of the cubic bezier at parameter s.
    [[nodiscard]] float sampleCurveY(float s) const;

    /// First derivative of x with respect to s.
    [[nodiscard]] float sampleCurveDerivativeX(float s) const;

    /// Newton-Raphson iteration to solve for s given x, with bisection fallback.
    [[nodiscard]] float solveCurveX(float x) const;

    /// Use the LUT to get an initial guess for Newton-Raphson.
    [[nodiscard]] float lutGuess(float x) const;

    float m_p1x = 0.0f;
    float m_p1y = 0.0f;
    float m_p2x = 1.0f;
    float m_p2y = 1.0f;

    // Precomputed coefficients for Horner form evaluation
    float m_cx = 0.0f;  // 3 * p1x
    float m_bx = 0.0f;  // 3 * (p2x - p1x) - 3 * p1x
    float m_ax = 0.0f;  // 1 - 3 * p2x + 3 * p1x
    float m_cy = 0.0f;
    float m_by = 0.0f;
    float m_ay = 0.0f;

    // Lookup table for fast initial guess
    static constexpr int kLUTSize = 101;  // 0..100 inclusive
    mutable std::array<float, kLUTSize> m_lut{};
    mutable bool m_lutBuilt = false;
};

// ---------------------------------------------------------------------------
// BezierCurveManager - stores named curves for lookup
// ---------------------------------------------------------------------------

class BezierCurveManager {
public:
    BezierCurveManager();
    ~BezierCurveManager() = default;

    /// Register a named curve (validates and clamps control point x to [0,1]).
    void add(const std::string& name, const BezierCurve& curve);

    /// Register from raw control points (clamps x to [0,1]).
    void add(const std::string& name, float p1x, float p1y, float p2x, float p2y);

    /// Retrieve a curve by name. Returns linear() if not found.
    [[nodiscard]] BezierCurve get(const std::string& name) const;

    /// Get the easing function (callable) for a named curve.
    [[nodiscard]] std::function<float(float)> getEasingFunction(const std::string& name) const;

    /// Check whether a curve with the given name exists.
    [[nodiscard]] bool has(const std::string& name) const;

    /// Remove a named curve.
    void remove(const std::string& name);

    /// Get all registered curve names.
    [[nodiscard]] std::vector<std::string> getNames() const;

private:
    std::unordered_map<std::string, BezierCurve> m_curves;
};

} // namespace eternal

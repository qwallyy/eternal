#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

namespace eternal {

// ---------------------------------------------------------------------------
// Basic geometric / colour types
// ---------------------------------------------------------------------------

struct Vec2 {
    double x = 0.0;
    double y = 0.0;

    Vec2 operator+(const Vec2& o) const noexcept { return {x + o.x, y + o.y}; }
    Vec2 operator-(const Vec2& o) const noexcept { return {x - o.x, y - o.y}; }
    Vec2 operator*(double s) const noexcept { return {x * s, y * s}; }
    bool operator==(const Vec2&) const = default;
};

struct Box {
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;

    [[nodiscard]] double right()  const noexcept { return x + w; }
    [[nodiscard]] double bottom() const noexcept { return y + h; }
    bool operator==(const Box&) const = default;
};

struct Color {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;

    bool operator==(const Color&) const = default;
};

struct Mat3x3 {
    std::array<std::array<float, 3>, 3> m{};

    static Mat3x3 identity() noexcept;
    Mat3x3 operator*(const Mat3x3& o) const noexcept;
};

// ---------------------------------------------------------------------------
// Scalar utilities
// ---------------------------------------------------------------------------

[[nodiscard]] inline double lerp(double a, double b, double t) noexcept {
    return a + (b - a) * t;
}

[[nodiscard]] inline double clamp(double v, double lo, double hi) noexcept {
    return std::clamp(v, lo, hi);
}

[[nodiscard]] inline double smoothstep(double edge0, double edge1, double x) noexcept {
    double t = clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

[[nodiscard]] inline double mapRange(double value,
                                     double in_min, double in_max,
                                     double out_min, double out_max) noexcept {
    return out_min + (value - in_min) * (out_max - out_min) / (in_max - in_min);
}

// ---------------------------------------------------------------------------
// Box utilities
// ---------------------------------------------------------------------------

[[nodiscard]] bool boxIntersect(const Box& a, const Box& b) noexcept;
[[nodiscard]] bool boxContains(const Box& outer, const Box& inner) noexcept;
[[nodiscard]] Box  boxUnion(const Box& a, const Box& b) noexcept;
[[nodiscard]] bool pointInBox(const Vec2& p, const Box& b) noexcept;

// ---------------------------------------------------------------------------
// Colour utilities
// ---------------------------------------------------------------------------

[[nodiscard]] Color       colorFromHex(std::string_view hex) noexcept;
[[nodiscard]] std::string colorToHex(const Color& c);

/// Convert Oklab L,a,b -> linear sRGB r,g,b (alpha passthrough).
[[nodiscard]] Color oklabToSrgb(const Color& lab) noexcept;

/// Convert linear sRGB -> Oklab.
[[nodiscard]] Color srgbToOklab(const Color& rgb) noexcept;

/// Convert HSL (h in [0,360], s,l in [0,1]) -> sRGB.
[[nodiscard]] Color hslToRgb(float h, float s, float l) noexcept;

} // namespace eternal

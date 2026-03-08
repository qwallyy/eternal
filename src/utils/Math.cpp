#include "eternal/utils/Math.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>

namespace eternal {

// ---------------------------------------------------------------------------
// Mat3x3
// ---------------------------------------------------------------------------

Mat3x3 Mat3x3::identity() noexcept {
    Mat3x3 m{};
    m.m[0][0] = 1.0f;
    m.m[1][1] = 1.0f;
    m.m[2][2] = 1.0f;
    return m;
}

Mat3x3 Mat3x3::operator*(const Mat3x3& o) const noexcept {
    Mat3x3 r{};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 3; ++k)
                r.m[i][j] += m[i][k] * o.m[k][j];
    return r;
}

// ---------------------------------------------------------------------------
// Box utilities
// ---------------------------------------------------------------------------

bool boxIntersect(const Box& a, const Box& b) noexcept {
    return a.x < b.right() && a.right() > b.x &&
           a.y < b.bottom() && a.bottom() > b.y;
}

bool boxContains(const Box& outer, const Box& inner) noexcept {
    return inner.x >= outer.x && inner.y >= outer.y &&
           inner.right() <= outer.right() && inner.bottom() <= outer.bottom();
}

Box boxUnion(const Box& a, const Box& b) noexcept {
    double x1 = std::min(a.x, b.x);
    double y1 = std::min(a.y, b.y);
    double x2 = std::max(a.right(), b.right());
    double y2 = std::max(a.bottom(), b.bottom());
    return {x1, y1, x2 - x1, y2 - y1};
}

bool pointInBox(const Vec2& p, const Box& b) noexcept {
    return p.x >= b.x && p.x < b.right() &&
           p.y >= b.y && p.y < b.bottom();
}

// ---------------------------------------------------------------------------
// Colour utilities
// ---------------------------------------------------------------------------

static uint8_t hexCharToNibble(char c) noexcept {
    if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
    return 0;
}

Color colorFromHex(std::string_view hex) noexcept {
    if (!hex.empty() && hex[0] == '#') hex.remove_prefix(1);

    Color c{0.0f, 0.0f, 0.0f, 1.0f};

    auto byte = [&](std::size_t idx) -> float {
        if (idx + 1 >= hex.size()) return 0.0f;
        uint8_t val = static_cast<uint8_t>(hexCharToNibble(hex[idx]) << 4 |
                                           hexCharToNibble(hex[idx + 1]));
        return static_cast<float>(val) / 255.0f;
    };

    if (hex.size() >= 6) {
        c.r = byte(0);
        c.g = byte(2);
        c.b = byte(4);
    }
    if (hex.size() >= 8) {
        c.a = byte(6);
    }
    return c;
}

std::string colorToHex(const Color& c) {
    char buf[10];
    auto toByte = [](float v) -> int {
        return std::clamp(static_cast<int>(v * 255.0f + 0.5f), 0, 255);
    };
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x%02x",
                  toByte(c.r), toByte(c.g), toByte(c.b), toByte(c.a));
    return std::string(buf);
}

// ---------------------------------------------------------------------------
// Oklab conversions
// ---------------------------------------------------------------------------

Color oklabToSrgb(const Color& lab) noexcept {
    float L = lab.r, a = lab.g, b = lab.b;

    float l_ = L + 0.3963377774f * a + 0.2158037573f * b;
    float m_ = L - 0.1055613458f * a - 0.0638541728f * b;
    float s_ = L - 0.0894841775f * a - 1.2914855480f * b;

    float l = l_ * l_ * l_;
    float m = m_ * m_ * m_;
    float s = s_ * s_ * s_;

    float r = +4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s;
    float g = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s;
    float bv = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s;

    return {r, g, bv, lab.a};
}

Color srgbToOklab(const Color& rgb) noexcept {
    float r = rgb.r, g = rgb.g, b = rgb.b;

    float l = 0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b;
    float m = 0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b;
    float s = 0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b;

    float l_ = std::cbrt(l);
    float m_ = std::cbrt(m);
    float s_ = std::cbrt(s);

    float L = 0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_;
    float A = 1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_;
    float B = 0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_;

    return {L, A, B, rgb.a};
}

Color hslToRgb(float h, float s, float l) noexcept {
    auto hue2rgb = [](float p, float q, float t) -> float {
        if (t < 0.0f) t += 1.0f;
        if (t > 1.0f) t -= 1.0f;
        if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
        if (t < 1.0f / 2.0f) return q;
        if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
        return p;
    };

    h = std::fmod(h, 360.0f) / 360.0f;
    if (h < 0.0f) h += 1.0f;

    if (s <= 0.0f) {
        return {l, l, l, 1.0f};
    }

    float q = l < 0.5f ? l * (1.0f + s) : l + s - l * s;
    float p = 2.0f * l - q;

    float r = hue2rgb(p, q, h + 1.0f / 3.0f);
    float g = hue2rgb(p, q, h);
    float b = hue2rgb(p, q, h - 1.0f / 3.0f);

    return {r, g, b, 1.0f};
}

} // namespace eternal

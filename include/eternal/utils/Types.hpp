#pragma once

#include <cstdint>

namespace eternal {

/// Axis-aligned bounding box used throughout the compositor.
struct Box {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    [[nodiscard]] bool empty() const noexcept {
        return width <= 0 || height <= 0;
    }

    [[nodiscard]] bool contains(int px, int py) const noexcept {
        return px >= x && px < x + width && py >= y && py < y + height;
    }

    [[nodiscard]] Box intersect(const Box& other) const;
    [[nodiscard]] bool intersects(const Box& other) const;
    [[nodiscard]] bool fullyContains(const Box& other) const;
};

/// Color as float RGBA in [0,1].
struct Color {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

/// 2D vector for positions/offsets.
struct Vec2 {
    double x = 0.0;
    double y = 0.0;
};

/// Size delta for resize operations.
struct SizeDelta {
    int dx = 0;
    int dy = 0;
};

} // namespace eternal

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace eternal {

struct Box;
class Effects;

enum class ColorSpace {
    sRGB,
    Oklab,
    OklabLinear
};

class Border {
public:
    Border();
    ~Border() = default;

    /// Attach the GPU effects engine for rendering.
    void setEffects(Effects* effects) { m_effects = effects; }

    /// Render the border around the given box.
    void render(const Box& box, bool active, float time = 0.0f);

    /// Set active and inactive colors (ARGB packed).
    void setColors(uint32_t active, uint32_t inactive);

    /// Set border width in pixels.
    void setWidth(int width);

    /// Set gradient parameters. Supports Oklab interpolation.
    void setGradient(const std::vector<uint32_t>& colors, float angle);

    /// Set a second gradient color pair for active/inactive states.
    void setGradientColors(uint32_t activeA, uint32_t activeB,
                           uint32_t inactiveA, uint32_t inactiveB);

    /// Enable or disable gradient animation (color rotation over time).
    void setAnimated(bool animated) { m_animated = animated; }

    void setColorSpace(ColorSpace space) { m_colorSpace = space; }
    void setGradientEnabled(bool enabled) { m_gradientEnabled = enabled; }

    /// Set per-corner radii for the border shape.
    void setCornerRadii(float tl, float tr, float br, float bl);

    [[nodiscard]] int getWidth() const { return m_width; }
    [[nodiscard]] uint32_t getActiveColor() const { return m_activeColor; }
    [[nodiscard]] uint32_t getInactiveColor() const { return m_inactiveColor; }
    [[nodiscard]] bool isGradientEnabled() const { return m_gradientEnabled; }
    [[nodiscard]] bool isAnimated() const { return m_animated; }
    [[nodiscard]] float getGradientAngle() const { return m_gradientAngle; }
    [[nodiscard]] ColorSpace getColorSpace() const { return m_colorSpace; }
    [[nodiscard]] const std::vector<uint32_t>& getGradientColors() const { return m_gradientColors; }

private:
    Effects* m_effects = nullptr;
    int m_width = 2;
    uint32_t m_activeColor = 0xFF88C0D0;
    uint32_t m_inactiveColor = 0xFF4C566A;
    // Gradient colors (linear sRGB unpacked)
    uint32_t m_activeA  = 0xFF88C0D0;
    uint32_t m_activeB  = 0xFF5E81AC;
    uint32_t m_inactiveA = 0xFF4C566A;
    uint32_t m_inactiveB = 0xFF3B4252;
    bool m_gradientEnabled = false;
    bool m_animated = true;
    std::vector<uint32_t> m_gradientColors;
    float m_gradientAngle = 0.0f;
    float m_cornerTL = 10.0f;
    float m_cornerTR = 10.0f;
    float m_cornerBR = 10.0f;
    float m_cornerBL = 10.0f;
    ColorSpace m_colorSpace = ColorSpace::Oklab;
};

} // namespace eternal

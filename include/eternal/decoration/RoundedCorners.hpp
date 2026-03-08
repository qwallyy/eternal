#pragma once

extern "C" {
#include <GLES2/gl2.h>
}

namespace eternal {

struct Box;
class Effects;

struct PerCornerRadius {
    int topLeft = 10;
    int topRight = 10;
    int bottomLeft = 10;
    int bottomRight = 10;
};

class RoundedCorners {
public:
    RoundedCorners();
    ~RoundedCorners() = default;

    /// Attach the GPU effects engine for rendering.
    void setEffects(Effects* effects) { m_effects = effects; }

    /// Clip drawing to the rounded rect defined by box and current radii.
    /// Uses the SDF-based fragment shader with per-corner support.
    void clip(const Box& box);

    /// Render a solid rounded rect with per-corner radii.
    void renderSolid(const Box& box, float r, float g, float b, float a);

    /// Render a texture clipped to a rounded rect.
    void renderTexture(GLuint texture, const Box& box, float alpha = 1.0f);

    /// Get the uniform radius (top-left).
    [[nodiscard]] int getRadius() const { return m_perCorner.topLeft; }

    /// Set a uniform radius on all corners.
    void setRadius(int radius);

    /// Set per-corner radii.
    void setPerCornerRadius(const PerCornerRadius& radii);

    void setAntialiasing(bool aa) { m_antialiasing = aa; }

    [[nodiscard]] bool getAntialiasing() const { return m_antialiasing; }
    [[nodiscard]] const PerCornerRadius& getPerCornerRadius() const { return m_perCorner; }

private:
    Effects* m_effects = nullptr;
    PerCornerRadius m_perCorner;
    bool m_antialiasing = true;
};

} // namespace eternal

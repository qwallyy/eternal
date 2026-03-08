#pragma once

#include <cstdint>

namespace eternal {

struct Box;
class Effects;

struct ShadowRenderParams {
    bool enabled = true;
    int range = 20;
    float renderPower = 3.0f;
    uint32_t color = 0xEE1A1A2E;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    float scale = 1.0f;
    float spread = 0.0f;
    bool sharp = false;
    // Per-corner radii so the shadow follows rounded corners.
    float cornerTL = 0.0f;
    float cornerTR = 0.0f;
    float cornerBR = 0.0f;
    float cornerBL = 0.0f;
};

class Shadow {
public:
    Shadow();
    ~Shadow() = default;

    /// Attach the GPU effects engine for rendering.
    void setEffects(Effects* effects) { m_effects = effects; }

    /// Render a shadow for the given box.
    /// Must be called BEFORE rendering the window content.
    void render(const Box& box);

    /// Set all shadow parameters at once.
    void setParams(const ShadowRenderParams& params);

    void setEnabled(bool enabled) { m_params.enabled = enabled; }
    void setRange(int range) { m_params.range = range; }
    void setRenderPower(float power) { m_params.renderPower = power; }
    void setColor(uint32_t color) { m_params.color = color; }
    void setOffset(float x, float y) { m_params.offsetX = x; m_params.offsetY = y; }
    void setScale(float scale) { m_params.scale = scale; }
    void setSharp(bool sharp) { m_params.sharp = sharp; }
    void setSpread(float spread) { m_params.spread = spread; }
    void setCornerRadii(float tl, float tr, float br, float bl);

    [[nodiscard]] bool isEnabled() const { return m_params.enabled; }
    [[nodiscard]] const ShadowRenderParams& getParams() const { return m_params; }

private:
    Effects* m_effects = nullptr;
    ShadowRenderParams m_params;
};

} // namespace eternal

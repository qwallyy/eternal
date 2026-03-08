#include "eternal/decoration/Shadow.hpp"
#include "eternal/render/Effects.hpp"
#include "eternal/render/Renderer.hpp"

namespace eternal {

Shadow::Shadow() = default;

void Shadow::render(const Box& box) {
    if (!m_params.enabled || !m_effects) return;

    // Convert our ShadowRenderParams to the render pipeline's ShadowRenderConfig.
    ShadowRenderConfig config;

    // Unpack ARGB u32 color.
    uint32_t c = m_params.color;
    config.color = {
        .r = static_cast<float>((c >> 16) & 0xFF) / 255.0f,
        .g = static_cast<float>((c >> 8)  & 0xFF) / 255.0f,
        .b = static_cast<float>((c)       & 0xFF) / 255.0f,
        .a = static_cast<float>((c >> 24) & 0xFF) / 255.0f,
    };

    config.radius   = m_params.range;
    config.offset_x = static_cast<int>(m_params.offsetX);
    config.offset_y = static_cast<int>(m_params.offsetY);

    // Sigma derived from range and render power.
    // Higher renderPower = sharper falloff (smaller sigma relative to range).
    config.sigma = static_cast<float>(m_params.range) / m_params.renderPower;
    if (m_params.sharp) {
        config.sigma = std::max(config.sigma * 0.25f, 1.0f);
    }

    config.spread    = m_params.spread;
    config.corner_tl = m_params.cornerTL;
    config.corner_tr = m_params.cornerTR;
    config.corner_br = m_params.cornerBR;
    config.corner_bl = m_params.cornerBL;

    // Shadow is rendered BEFORE the window content (painter's algorithm).
    m_effects->renderShadow(box, config);
}

void Shadow::setParams(const ShadowRenderParams& params) {
    m_params = params;
}

void Shadow::setCornerRadii(float tl, float tr, float br, float bl) {
    m_params.cornerTL = tl;
    m_params.cornerTR = tr;
    m_params.cornerBR = br;
    m_params.cornerBL = bl;
}

} // namespace eternal

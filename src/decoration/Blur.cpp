#include "eternal/decoration/Blur.hpp"
#include "eternal/render/Effects.hpp"
#include "eternal/render/Renderer.hpp"

namespace eternal {

Blur::Blur() = default;

void Blur::render(const Box& region, float time) {
    if (!m_params.enabled || !m_effects) return;

    // Convert decoration BlurDecorParams to the render pipeline BlurRenderParams.
    BlurRenderParams rp;
    rp.iterations     = m_params.passes;
    // Map the 'size' parameter to blur offset.  Higher size = more blur.
    rp.offset         = static_cast<float>(m_params.size) / 4.0f;
    rp.scale          = 0.5f;
    rp.noise          = (m_params.noise > 0.001f);
    rp.noise_strength = m_params.noise;
    rp.contrast       = m_params.contrast;
    rp.brightness     = m_params.brightness;
    rp.saturation     = 1.0f;
    rp.vibrancy       = m_params.vibrancy;

    m_effects->applyBlur(region, rp, time);
}

void Blur::setParams(const BlurDecorParams& params) {
    m_params = params;
}

} // namespace eternal

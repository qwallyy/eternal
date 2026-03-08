#pragma once

namespace eternal {

struct Box;
class Effects;

/// Decoration-side blur parameters.
/// These map to BlurRenderParams in the render pipeline.
struct BlurDecorParams {
    bool enabled = true;
    int size = 8;            // Controls blur radius / offset
    int passes = 2;          // Number of downsample/upsample iterations
    float noise = 0.0117f;
    float contrast = 0.8916f;
    float brightness = 0.8172f;
    float vibrancy = 0.1696f;
};

class Blur {
public:
    Blur();
    ~Blur() = default;

    /// Attach the GPU effects engine for rendering.
    void setEffects(Effects* effects) { m_effects = effects; }

    /// Render Dual-Kawase blur over the given region.
    void render(const Box& region, float time = 0.0f);

    /// Set all blur parameters at once.
    void setParams(const BlurDecorParams& params);

    void setEnabled(bool enabled) { m_params.enabled = enabled; }
    void setSize(int size) { m_params.size = size; }
    void setPasses(int passes) { m_params.passes = passes; }
    void setNoise(float noise) { m_params.noise = noise; }
    void setContrast(float contrast) { m_params.contrast = contrast; }
    void setBrightness(float brightness) { m_params.brightness = brightness; }
    void setVibrancy(float vibrancy) { m_params.vibrancy = vibrancy; }

    [[nodiscard]] bool isEnabled() const { return m_params.enabled; }
    [[nodiscard]] const BlurDecorParams& getParams() const { return m_params; }

private:
    Effects* m_effects = nullptr;
    BlurDecorParams m_params;
};

} // namespace eternal

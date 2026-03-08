#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace eternal {

class Server;
class AnimationEngine;
struct Surface;
struct Box;

struct ShadowParams {
    bool enabled = true;
    int range = 20;
    float renderPower = 3.0f;
    uint32_t color = 0xEE1A1A2E;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    float scale = 1.0f;
    bool sharp = false;
};

struct GradientBorderParams {
    std::vector<uint32_t> colors;
    float angle = 0.0f;
};

class DecorationManager {
public:
    explicit DecorationManager(Server& server);
    ~DecorationManager();

    /// Set the animation engine reference for dim/fade effects.
    void setAnimationEngine(AnimationEngine* engine);

    /// Render all decorations for a surface within the given box.
    void renderDecorations(Surface* surface, const Box& box);

    /// Set the decoration theme by name.
    void setTheme(const std::string& theme);

    /// Set the border size in pixels.
    void setBorderSize(int size);

    /// Set corner rounding radius.
    void setRounding(int radius);

    /// Set gradient border parameters.
    void setGradientBorder(const std::vector<uint32_t>& colors, float angle);

    /// Set shadow parameters.
    void setShadow(const ShadowParams& params);

    /// Set how much to dim inactive windows (0.0 = none, 1.0 = full).
    void setDimInactive(float amount);

    /// Set the dim overlay color (default black).
    void setDimColor(float r, float g, float b);

    /// Called every frame to update decoration state.
    void update();

    /// Notify that keyboard focus changed to a new surface.
    void onFocusChanged(Surface* focused);

    /// Get the effective dim value for a surface (accounts for animation).
    [[nodiscard]] float getEffectiveDim(Surface* surface) const;

    [[nodiscard]] int getBorderSize() const { return m_borderSize; }
    [[nodiscard]] int getRounding() const { return m_rounding; }
    [[nodiscard]] float getDimInactive() const { return m_dimInactive; }
    [[nodiscard]] const ShadowParams& getShadowParams() const { return m_shadowParams; }

private:
    Server& m_server;
    AnimationEngine* m_animEngine = nullptr;
    std::string m_theme;
    int m_borderSize = 2;
    int m_rounding = 10;
    float m_dimInactive = 0.0f;
    float m_dimR = 0.0f;
    float m_dimG = 0.0f;
    float m_dimB = 0.0f;
    ShadowParams m_shadowParams;
    GradientBorderParams m_gradientParams;
};

} // namespace eternal

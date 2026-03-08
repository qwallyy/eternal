#include <eternal/decoration/DecorationManager.hpp>
#include <eternal/animation/AnimationEngine.hpp>

namespace eternal {

DecorationManager::DecorationManager(Server& server)
    : m_server(server) {}

DecorationManager::~DecorationManager() = default;

void DecorationManager::setAnimationEngine(AnimationEngine* engine) {
    m_animEngine = engine;
}

void DecorationManager::renderDecorations(Surface* /*surface*/, const Box& /*box*/) {
    // Rendering is handled by the Renderer using Effects.
    // This method is called per-surface during the render pass to apply
    // borders, shadows, blur, rounded corners, and dim overlays.
}

void DecorationManager::setTheme(const std::string& theme) {
    m_theme = theme;
}

void DecorationManager::setBorderSize(int size) {
    m_borderSize = size;
}

void DecorationManager::setRounding(int radius) {
    m_rounding = radius;
}

void DecorationManager::setGradientBorder(const std::vector<uint32_t>& colors, float angle) {
    m_gradientParams.colors = colors;
    m_gradientParams.angle = angle;
}

void DecorationManager::setShadow(const ShadowParams& params) {
    m_shadowParams = params;
}

void DecorationManager::setDimInactive(float amount) {
    m_dimInactive = amount;

    // Propagate to animation engine dim config
    if (m_animEngine) {
        DimInactiveConfig dimCfg;
        dimCfg.strength = amount;
        dimCfg.dimR = m_dimR;
        dimCfg.dimG = m_dimG;
        dimCfg.dimB = m_dimB;
        m_animEngine->setDimInactiveConfig(dimCfg);
    }
}

void DecorationManager::setDimColor(float r, float g, float b) {
    m_dimR = r;
    m_dimG = g;
    m_dimB = b;

    if (m_animEngine && m_dimInactive > 0.0f) {
        DimInactiveConfig dimCfg;
        dimCfg.strength = m_dimInactive;
        dimCfg.dimR = r;
        dimCfg.dimG = g;
        dimCfg.dimB = b;
        m_animEngine->setDimInactiveConfig(dimCfg);
    }
}

void DecorationManager::update() {
    // Per-frame decoration state updates happen through the AnimationEngine.
    // Ghost surface rendering, dim fades, etc. are driven by the engine tick.
}

void DecorationManager::onFocusChanged(Surface* focused) {
    if (m_animEngine && m_dimInactive > 0.0f) {
        m_animEngine->onFocusChanged(focused);
    }
}

float DecorationManager::getEffectiveDim(Surface* surface) const {
    if (m_animEngine && m_dimInactive > 0.0f) {
        return m_animEngine->getWindowDim(surface);
    }
    return 0.0f;
}

} // namespace eternal

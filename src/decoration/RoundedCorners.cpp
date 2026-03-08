#include "eternal/decoration/RoundedCorners.hpp"
#include "eternal/render/Effects.hpp"
#include "eternal/render/Renderer.hpp"

extern "C" {
#include <wlr/render/gles2.h>
}

namespace eternal {

RoundedCorners::RoundedCorners() = default;

void RoundedCorners::clip(const Box& box) {
    if (!m_effects) return;

    // Render an SDF-based rounded rect mask.  The fragment shader uses
    // per-corner radii and anti-aliased edges via smoothstep.
    float tl = static_cast<float>(m_perCorner.topLeft);
    float tr = static_cast<float>(m_perCorner.topRight);
    float br = static_cast<float>(m_perCorner.bottomRight);
    float bl = static_cast<float>(m_perCorner.bottomLeft);

    // When clipping, we render a transparent rounded rect with the inverse
    // mask to cut out the corners.  This is done in the stencil buffer or
    // by rendering with glColorMask to set up the alpha.
    //
    // For now, we use a scissor-compatible approach: render the rounded rect
    // into the alpha channel.  This works with the surface rendering pipeline
    // which tests alpha.
    glEnable(GL_STENCIL_TEST);
    glClearStencil(0);
    glClear(GL_STENCIL_BUFFER_BIT);

    // Draw the rounded rect shape into the stencil buffer.
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

    Color white = {1.0f, 1.0f, 1.0f, 1.0f};
    m_effects->renderRoundedRect(box, white, tl, tr, br, bl);

    // Now only allow rendering where stencil == 1.
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilFunc(GL_EQUAL, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
}

void RoundedCorners::renderSolid(const Box& box, float r, float g, float b, float a) {
    if (!m_effects) return;

    float tl = static_cast<float>(m_perCorner.topLeft);
    float tr = static_cast<float>(m_perCorner.topRight);
    float br = static_cast<float>(m_perCorner.bottomRight);
    float bl = static_cast<float>(m_perCorner.bottomLeft);

    Color color = {r, g, b, a};
    m_effects->renderRoundedRect(box, color, tl, tr, br, bl);
}

void RoundedCorners::renderTexture(GLuint texture, const Box& box, float alpha) {
    if (!m_effects || texture == 0) return;

    float tl = static_cast<float>(m_perCorner.topLeft);
    float tr = static_cast<float>(m_perCorner.topRight);
    float br = static_cast<float>(m_perCorner.bottomRight);
    float bl = static_cast<float>(m_perCorner.bottomLeft);

    m_effects->renderRoundedTexture(texture, box, alpha, tl, tr, br, bl);
}

void RoundedCorners::setRadius(int radius) {
    m_perCorner.topLeft = radius;
    m_perCorner.topRight = radius;
    m_perCorner.bottomLeft = radius;
    m_perCorner.bottomRight = radius;
}

void RoundedCorners::setPerCornerRadius(const PerCornerRadius& radii) {
    m_perCorner = radii;
}

} // namespace eternal

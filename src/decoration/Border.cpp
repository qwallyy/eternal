#include "eternal/decoration/Border.hpp"
#include "eternal/render/Effects.hpp"
#include "eternal/render/Renderer.hpp"

namespace eternal {

// Helper: unpack ARGB u32 to linear-ish Color.
static Color unpackColor(uint32_t argb) {
    return {
        .r = static_cast<float>((argb >> 16) & 0xFF) / 255.0f,
        .g = static_cast<float>((argb >> 8)  & 0xFF) / 255.0f,
        .b = static_cast<float>((argb)       & 0xFF) / 255.0f,
        .a = static_cast<float>((argb >> 24) & 0xFF) / 255.0f,
    };
}

Border::Border() = default;

void Border::render(const Box& box, bool active, float time) {
    if (!m_effects || m_width <= 0) return;

    if (m_gradientEnabled) {
        // Task 18: Gradient border with Oklab interpolation and animation.
        GradientBorderConfig config;
        config.color_a      = unpackColor(m_activeA);
        config.color_b      = unpackColor(m_activeB);
        config.inactive_a   = unpackColor(m_inactiveA);
        config.inactive_b   = unpackColor(m_inactiveB);
        config.border_width = static_cast<float>(m_width);
        config.corner_tl    = m_cornerTL;
        config.corner_tr    = m_cornerTR;
        config.corner_br    = m_cornerBR;
        config.corner_bl    = m_cornerBL;
        config.animated     = m_animated;
        config.active       = active;

        m_effects->renderGradientBorder(box, config, time);
    } else {
        // Solid border: render a rounded rect ring.
        Color outer_color = active ? unpackColor(m_activeColor)
                                   : unpackColor(m_inactiveColor);
        // Outer rounded rect.
        m_effects->renderRoundedRect(box, outer_color,
                                     m_cornerTL, m_cornerTR,
                                     m_cornerBR, m_cornerBL);
        // Inner cutout (transparent) to create the border ring.
        Box inner = {
            box.x + m_width,
            box.y + m_width,
            box.width  - m_width * 2,
            box.height - m_width * 2,
        };
        if (!inner.empty()) {
            Color clear = {0.0f, 0.0f, 0.0f, 0.0f};
            float itl = std::max(0.0f, m_cornerTL - static_cast<float>(m_width));
            float itr = std::max(0.0f, m_cornerTR - static_cast<float>(m_width));
            float ibr = std::max(0.0f, m_cornerBR - static_cast<float>(m_width));
            float ibl = std::max(0.0f, m_cornerBL - static_cast<float>(m_width));
            m_effects->renderRoundedRect(inner, clear, itl, itr, ibr, ibl);
        }
    }
}

void Border::setColors(uint32_t active, uint32_t inactive) {
    m_activeColor = active;
    m_inactiveColor = inactive;
}

void Border::setWidth(int width) {
    m_width = width;
}

void Border::setGradient(const std::vector<uint32_t>& colors, float angle) {
    m_gradientColors = colors;
    m_gradientAngle = angle;
    m_gradientEnabled = !colors.empty();

    // Use the first two gradient colors as the A/B pair.
    if (colors.size() >= 2) {
        m_activeA = colors[0];
        m_activeB = colors[1];
    } else if (colors.size() == 1) {
        m_activeA = colors[0];
        m_activeB = colors[0];
    }
}

void Border::setGradientColors(uint32_t activeA, uint32_t activeB,
                               uint32_t inactiveA, uint32_t inactiveB) {
    m_activeA   = activeA;
    m_activeB   = activeB;
    m_inactiveA = inactiveA;
    m_inactiveB = inactiveB;
    m_gradientEnabled = true;
}

void Border::setCornerRadii(float tl, float tr, float br, float bl) {
    m_cornerTL = tl;
    m_cornerTR = tr;
    m_cornerBR = br;
    m_cornerBL = bl;
}

} // namespace eternal

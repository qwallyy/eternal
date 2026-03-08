#pragma once

extern "C" {
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_output.h>
#include <GLES2/gl2.h>
#include <GLES3/gl3.h>
}

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "eternal/render/Renderer.hpp"

namespace eternal {

class DualKawaseBlur;

/// Parameters for blur effect (used by the render pipeline).
/// Maps to decoration::BlurParams from Blur.hpp.
struct BlurRenderParams {
    int   iterations     = 2;
    float offset         = 1.0f;
    float scale          = 0.5f;      // downscale factor
    bool  noise          = false;
    float noise_strength = 0.02f;
    float brightness     = 1.0f;
    float contrast       = 1.0f;
    float saturation     = 1.0f;
    float vibrancy       = 0.0f;
};

/// Parameters for drop shadow (used by the render pipeline).
struct ShadowRenderConfig {
    Color color      = {0.0f, 0.0f, 0.0f, 0.6f};
    int   radius     = 24;
    int   offset_x   = 0;
    int   offset_y   = 4;
    float sigma      = 12.0f;
    float spread     = 0.0f;
    float corner_tl  = 0.0f;
    float corner_tr  = 0.0f;
    float corner_br  = 0.0f;
    float corner_bl  = 0.0f;
};

/// Gradient stop.
struct GradientStop {
    float position;   // 0..1
    Color color;
};

/// Parameters for gradient border rendering.
struct GradientBorderConfig {
    Color  color_a       = {1.0f, 1.0f, 1.0f, 1.0f};
    Color  color_b       = {0.5f, 0.5f, 1.0f, 1.0f};
    Color  inactive_a    = {0.3f, 0.3f, 0.3f, 1.0f};
    Color  inactive_b    = {0.2f, 0.2f, 0.2f, 1.0f};
    float  border_width  = 2.0f;
    float  corner_tl     = 10.0f;
    float  corner_tr     = 10.0f;
    float  corner_br     = 10.0f;
    float  corner_bl     = 10.0f;
    bool   animated      = true;
    bool   active        = true;
};

/// Screen-shader context (custom post-processing pass).
struct ScreenShader {
    GLuint program = 0;
    GLint  u_texture   = -1;
    GLint  u_time      = -1;
    GLint  u_resolution = -1;
    std::string source_path;
};

/// Visual effects manager.
/// Owns the blur implementation and shader programs for shadow, gradient, etc.
class Effects {
public:
    Effects();
    ~Effects();

    Effects(const Effects&) = delete;
    Effects& operator=(const Effects&) = delete;

    /// Initialise OpenGL resources. Call after EGL context is current.
    bool init(Renderer* renderer);

    // -- Blur (Task 16) ---------------------------------------------------

    void applyBlur(const Box& region, const BlurRenderParams& params,
                   float time = 0.0f);

    // -- Shadow (Task 19) -------------------------------------------------

    void renderShadow(const Box& geometry, const ShadowRenderConfig& params);

    // -- Per-surface effects -----------------------------------------------

    void applyDimming(wlr_surface* surface, float strength);
    void applyOpacity(wlr_surface* surface, float alpha);

    // -- Gradient ----------------------------------------------------------

    /// Render a linear gradient.  `angle` is in degrees (0 = left->right).
    void renderGradient(const Box& box,
                        const std::vector<GradientStop>& stops,
                        float angle);

    // -- Gradient border (Task 18) ----------------------------------------

    void renderGradientBorder(const Box& box, const GradientBorderConfig& config,
                              float time);

    // -- Rounded rect with per-corner radii (Task 17) ---------------------

    void renderRoundedRect(const Box& box, const Color& color,
                           float tl, float tr, float br, float bl);

    /// Render a texture clipped to a rounded rect with per-corner radii.
    void renderRoundedTexture(GLuint texture, const Box& box, float alpha,
                              float tl, float tr, float br, float bl);

    // -- Software cursor compositing (Task 13) ----------------------------

    void renderCursorOverlay(wlr_texture* cursor_tex,
                             int cx, int cy,
                             int hotspot_x, int hotspot_y,
                             int output_w, int output_h);

    // -- Screen shader (full-screen post-process) --------------------------

    bool applyScreenShader(const std::filesystem::path& shader_path);
    void removeScreenShader();
    [[nodiscard]] bool hasScreenShader() const { return screen_shader_.program != 0; }
    void renderScreenShader(wlr_output* output, float time);

    /// Access the blur engine directly.
    [[nodiscard]] DualKawaseBlur* blur() const { return blur_.get(); }

private:
    Renderer* renderer_ = nullptr;
    std::unique_ptr<DualKawaseBlur> blur_;
    ScreenShader screen_shader_;

    // Shader programs
    GLuint shadow_program_      = 0;
    GLuint gradient_program_    = 0;
    GLuint dim_program_         = 0;
    GLuint opacity_program_     = 0;
    GLuint rounded_program_     = 0;
    GLuint rounded_tex_program_ = 0;
    GLuint border_grad_program_ = 0;
    GLuint cursor_program_      = 0;

    // Shared quad VAO/VBO
    GLuint quad_vao_ = 0;
    GLuint quad_vbo_ = 0;

    void initQuad();
    void drawQuad();
    bool compileEffectShaders();
    bool loadScreenShader(const std::filesystem::path& path);
};

} // namespace eternal

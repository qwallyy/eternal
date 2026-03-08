#include "eternal/render/Effects.hpp"
#include "eternal/render/DualKawaseBlur.hpp"
#include "eternal/render/Shaders.hpp"

extern "C" {
#include <wlr/render/gles2.h>
#include <wlr/util/log.h>
}

#include <cassert>
#include <cmath>
#include <fstream>
#include <sstream>

namespace eternal {

// Fullscreen quad vertices: pos(x,y) + texcoord(s,t).
static constexpr float kQuadVertices[] = {
    -1.0f, -1.0f, 0.0f, 0.0f,
     1.0f, -1.0f, 1.0f, 0.0f,
     1.0f,  1.0f, 1.0f, 1.0f,
    -1.0f, -1.0f, 0.0f, 0.0f,
     1.0f,  1.0f, 1.0f, 1.0f,
    -1.0f,  1.0f, 0.0f, 1.0f,
};

Effects::Effects() = default;

Effects::~Effects() {
    if (shadow_program_)      glDeleteProgram(shadow_program_);
    if (gradient_program_)    glDeleteProgram(gradient_program_);
    if (dim_program_)         glDeleteProgram(dim_program_);
    if (opacity_program_)     glDeleteProgram(opacity_program_);
    if (rounded_program_)     glDeleteProgram(rounded_program_);
    if (rounded_tex_program_) glDeleteProgram(rounded_tex_program_);
    if (border_grad_program_) glDeleteProgram(border_grad_program_);
    if (cursor_program_)      glDeleteProgram(cursor_program_);

    if (screen_shader_.program) glDeleteProgram(screen_shader_.program);

    if (quad_vbo_) glDeleteBuffers(1, &quad_vbo_);
    if (quad_vao_) glDeleteVertexArrays(1, &quad_vao_);
}

bool Effects::init(Renderer* renderer) {
    assert(renderer);
    renderer_ = renderer;

    blur_ = std::make_unique<DualKawaseBlur>();
    if (!blur_->init()) {
        wlr_log(WLR_ERROR, "Failed to initialise Dual-Kawase blur shaders");
        return false;
    }

    initQuad();

    if (!compileEffectShaders()) {
        wlr_log(WLR_ERROR, "Failed to compile effect shaders");
        return false;
    }

    wlr_log(WLR_INFO, "Effects engine initialized (blur, shadow, gradient border, "
            "rounded corners, cursor overlay)");
    return true;
}

// -- Blur (Task 16) --------------------------------------------------------

void Effects::applyBlur(const Box& region, const BlurRenderParams& params,
                        float time) {
    if (!blur_ || region.empty()) return;

    // Save current GL state.
    GLint prev_fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
    GLint prev_viewport[4];
    glGetIntegerv(GL_VIEWPORT, prev_viewport);

    // Copy the region from the current framebuffer into a source texture.
    GLuint source_tex = 0;
    glGenTextures(1, &source_tex);
    glBindTexture(GL_TEXTURE_2D, source_tex);
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                     region.x, region.y, region.width, region.height, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    GLuint result = blur_->render(source_tex, region.width, region.height,
                                  params, time);

    // Restore the original FBO and viewport.
    glBindFramebuffer(GL_FRAMEBUFFER, prev_fbo);
    glViewport(prev_viewport[0], prev_viewport[1],
               prev_viewport[2], prev_viewport[3]);

    // Blit the blurred result back to the framebuffer.
    if (result != 0) {
        // Use a simple textured quad to blit the result.
        // We need a trivial blit shader; reuse the opacity shader with alpha=1.
        if (opacity_program_) {
            glUseProgram(opacity_program_);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, result);
            glUniform1i(glGetUniformLocation(opacity_program_, "u_texture"), 0);
            glUniform1f(glGetUniformLocation(opacity_program_, "u_alpha"), 1.0f);

            glEnable(GL_SCISSOR_TEST);
            glScissor(region.x, region.y, region.width, region.height);
            glViewport(region.x, region.y, region.width, region.height);

            drawQuad();

            glDisable(GL_SCISSOR_TEST);
            glViewport(prev_viewport[0], prev_viewport[1],
                       prev_viewport[2], prev_viewport[3]);
            glUseProgram(0);
        }
    }

    glDeleteTextures(1, &source_tex);
}

// -- Shadow (Task 19) ------------------------------------------------------

void Effects::renderShadow(const Box& geometry, const ShadowRenderConfig& params) {
    if (shadow_program_ == 0) return;

    // Expand the geometry by the shadow radius + spread + offset.
    int expand = params.radius + static_cast<int>(params.spread);
    Box shadow_box = {
        geometry.x - expand + params.offset_x,
        geometry.y - expand + params.offset_y,
        geometry.width  + expand * 2,
        geometry.height + expand * 2,
    };

    // Save viewport.
    GLint prev_viewport[4];
    glGetIntegerv(GL_VIEWPORT, prev_viewport);

    glUseProgram(shadow_program_);
    glUniform4f(glGetUniformLocation(shadow_program_, "u_color"),
                params.color.r, params.color.g, params.color.b, params.color.a);
    glUniform2f(glGetUniformLocation(shadow_program_, "u_size"),
                static_cast<float>(geometry.width),
                static_cast<float>(geometry.height));
    glUniform2f(glGetUniformLocation(shadow_program_, "u_position"),
                static_cast<float>(geometry.x),
                static_cast<float>(geometry.y));
    glUniform1f(glGetUniformLocation(shadow_program_, "u_sigma"),
                params.sigma);
    glUniform4f(glGetUniformLocation(shadow_program_, "u_radii"),
                params.corner_tl, params.corner_tr,
                params.corner_br, params.corner_bl);
    glUniform1f(glGetUniformLocation(shadow_program_, "u_spread"),
                params.spread);
    glUniform2f(glGetUniformLocation(shadow_program_, "u_offset"),
                static_cast<float>(params.offset_x),
                static_cast<float>(params.offset_y));

    glViewport(shadow_box.x, shadow_box.y,
               shadow_box.width, shadow_box.height);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    drawQuad();

    glUseProgram(0);
    glViewport(prev_viewport[0], prev_viewport[1],
               prev_viewport[2], prev_viewport[3]);
}

// -- Surface effects --------------------------------------------------------

void Effects::applyDimming(wlr_surface* surface, float strength) {
    if (dim_program_ == 0 || !surface) return;

    wlr_texture* tex = wlr_surface_get_texture(surface);
    if (!tex) return;

    glUseProgram(dim_program_);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(glGetUniformLocation(dim_program_, "u_texture"), 0);
    glUniform1f(glGetUniformLocation(dim_program_, "u_strength"), strength);

    drawQuad();
    glUseProgram(0);
}

void Effects::applyOpacity(wlr_surface* surface, float alpha) {
    if (opacity_program_ == 0 || !surface) return;

    wlr_texture* tex = wlr_surface_get_texture(surface);
    if (!tex) return;

    glUseProgram(opacity_program_);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(glGetUniformLocation(opacity_program_, "u_texture"), 0);
    glUniform1f(glGetUniformLocation(opacity_program_, "u_alpha"), alpha);

    drawQuad();
    glUseProgram(0);
}

// -- Gradient ---------------------------------------------------------------

void Effects::renderGradient(const Box& box,
                             const std::vector<GradientStop>& stops,
                             float angle) {
    if (gradient_program_ == 0 || stops.empty() || box.empty()) return;

    constexpr int kMaxStops = 16;
    int num_stops = static_cast<int>(std::min(stops.size(),
                                               static_cast<size_t>(kMaxStops)));

    float positions[kMaxStops] = {};
    float colors[kMaxStops * 4] = {};
    for (int i = 0; i < num_stops; ++i) {
        positions[i] = stops[i].position;
        colors[i * 4 + 0] = stops[i].color.r;
        colors[i * 4 + 1] = stops[i].color.g;
        colors[i * 4 + 2] = stops[i].color.b;
        colors[i * 4 + 3] = stops[i].color.a;
    }

    float angle_rad = angle * (static_cast<float>(M_PI) / 180.0f);

    GLint prev_viewport[4];
    glGetIntegerv(GL_VIEWPORT, prev_viewport);

    glUseProgram(gradient_program_);
    glUniform2f(glGetUniformLocation(gradient_program_, "u_size"),
                static_cast<float>(box.width),
                static_cast<float>(box.height));
    glUniform2f(glGetUniformLocation(gradient_program_, "u_position"),
                static_cast<float>(box.x),
                static_cast<float>(box.y));
    glUniform1f(glGetUniformLocation(gradient_program_, "u_angle"), angle_rad);
    glUniform1i(glGetUniformLocation(gradient_program_, "u_num_stops"), num_stops);
    glUniform1fv(glGetUniformLocation(gradient_program_, "u_stop_positions"),
                 num_stops, positions);
    glUniform4fv(glGetUniformLocation(gradient_program_, "u_stop_colors"),
                 num_stops, colors);

    glViewport(box.x, box.y, box.width, box.height);
    drawQuad();
    glUseProgram(0);

    glViewport(prev_viewport[0], prev_viewport[1],
               prev_viewport[2], prev_viewport[3]);
}

// -- Gradient border (Task 18) ---------------------------------------------

void Effects::renderGradientBorder(const Box& box, const GradientBorderConfig& config,
                                   float time) {
    if (border_grad_program_ == 0 || box.empty()) return;

    GLint prev_viewport[4];
    glGetIntegerv(GL_VIEWPORT, prev_viewport);

    glUseProgram(border_grad_program_);
    glUniform2f(glGetUniformLocation(border_grad_program_, "u_size"),
                static_cast<float>(box.width),
                static_cast<float>(box.height));
    glUniform2f(glGetUniformLocation(border_grad_program_, "u_position"),
                static_cast<float>(box.x),
                static_cast<float>(box.y));
    glUniform1f(glGetUniformLocation(border_grad_program_, "u_border_width"),
                config.border_width);
    glUniform4f(glGetUniformLocation(border_grad_program_, "u_radii"),
                config.corner_tl, config.corner_tr,
                config.corner_br, config.corner_bl);
    glUniform1f(glGetUniformLocation(border_grad_program_, "u_time"), time);

    // Active colors
    glUniform4f(glGetUniformLocation(border_grad_program_, "u_color_a"),
                config.color_a.r, config.color_a.g,
                config.color_a.b, config.color_a.a);
    glUniform4f(glGetUniformLocation(border_grad_program_, "u_color_b"),
                config.color_b.r, config.color_b.g,
                config.color_b.b, config.color_b.a);
    // Inactive colors
    glUniform4f(glGetUniformLocation(border_grad_program_, "u_color_ia"),
                config.inactive_a.r, config.inactive_a.g,
                config.inactive_a.b, config.inactive_a.a);
    glUniform4f(glGetUniformLocation(border_grad_program_, "u_color_ib"),
                config.inactive_b.r, config.inactive_b.g,
                config.inactive_b.b, config.inactive_b.a);

    glUniform1i(glGetUniformLocation(border_grad_program_, "u_active"),
                config.active ? 1 : 0);
    glUniform1i(glGetUniformLocation(border_grad_program_, "u_animate"),
                config.animated ? 1 : 0);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    glViewport(box.x, box.y, box.width, box.height);
    drawQuad();
    glUseProgram(0);

    glViewport(prev_viewport[0], prev_viewport[1],
               prev_viewport[2], prev_viewport[3]);
}

// -- Rounded rect (Task 17) ------------------------------------------------

void Effects::renderRoundedRect(const Box& box, const Color& color,
                                float tl, float tr, float br, float bl) {
    if (rounded_program_ == 0 || box.empty()) return;

    GLint prev_viewport[4];
    glGetIntegerv(GL_VIEWPORT, prev_viewport);

    glUseProgram(rounded_program_);
    glUniform4f(glGetUniformLocation(rounded_program_, "u_color"),
                color.r, color.g, color.b, color.a);
    glUniform2f(glGetUniformLocation(rounded_program_, "u_size"),
                static_cast<float>(box.width),
                static_cast<float>(box.height));
    glUniform2f(glGetUniformLocation(rounded_program_, "u_position"),
                static_cast<float>(box.x),
                static_cast<float>(box.y));
    glUniform4f(glGetUniformLocation(rounded_program_, "u_radii"),
                tl, tr, br, bl);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    glViewport(box.x, box.y, box.width, box.height);
    drawQuad();
    glUseProgram(0);

    glViewport(prev_viewport[0], prev_viewport[1],
               prev_viewport[2], prev_viewport[3]);
}

void Effects::renderRoundedTexture(GLuint texture, const Box& box, float alpha,
                                   float tl, float tr, float br, float bl) {
    if (rounded_tex_program_ == 0 || box.empty() || texture == 0) return;

    GLint prev_viewport[4];
    glGetIntegerv(GL_VIEWPORT, prev_viewport);

    glUseProgram(rounded_tex_program_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(glGetUniformLocation(rounded_tex_program_, "u_texture"), 0);
    glUniform1f(glGetUniformLocation(rounded_tex_program_, "u_alpha"), alpha);
    glUniform2f(glGetUniformLocation(rounded_tex_program_, "u_size"),
                static_cast<float>(box.width),
                static_cast<float>(box.height));
    glUniform2f(glGetUniformLocation(rounded_tex_program_, "u_position"),
                static_cast<float>(box.x),
                static_cast<float>(box.y));
    glUniform4f(glGetUniformLocation(rounded_tex_program_, "u_radii"),
                tl, tr, br, bl);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    glViewport(box.x, box.y, box.width, box.height);
    drawQuad();
    glUseProgram(0);

    glViewport(prev_viewport[0], prev_viewport[1],
               prev_viewport[2], prev_viewport[3]);
}

// -- Software cursor (Task 13) --------------------------------------------

void Effects::renderCursorOverlay(wlr_texture* cursor_tex,
                                  int cx, int cy,
                                  int hotspot_x, int hotspot_y,
                                  int output_w, int output_h) {
    if (!cursor_tex || !renderer_) return;

    // Composite the cursor texture directly using the main renderer.
    Box cursor_box = {
        cx - hotspot_x,
        cy - hotspot_y,
        static_cast<int>(cursor_tex->width),
        static_cast<int>(cursor_tex->height),
    };

    // Use the opacity shader for clean alpha compositing.
    if (opacity_program_) {
        GLint prev_viewport[4];
        glGetIntegerv(GL_VIEWPORT, prev_viewport);

        // Get the GL texture from the wlr_texture.
        struct wlr_gles2_texture_attribs attribs;
        wlr_gles2_texture_get_attribs(cursor_tex, &attribs);

        glUseProgram(opacity_program_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(attribs.target, attribs.tex);
        glUniform1i(glGetUniformLocation(opacity_program_, "u_texture"), 0);
        glUniform1f(glGetUniformLocation(opacity_program_, "u_alpha"), 1.0f);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glViewport(cursor_box.x, cursor_box.y,
                   cursor_box.width, cursor_box.height);
        drawQuad();

        glUseProgram(0);
        glViewport(prev_viewport[0], prev_viewport[1],
                   prev_viewport[2], prev_viewport[3]);
    }
}

// -- Screen shader ----------------------------------------------------------

bool Effects::applyScreenShader(const std::filesystem::path& shader_path) {
    removeScreenShader();
    return loadScreenShader(shader_path);
}

void Effects::removeScreenShader() {
    if (screen_shader_.program) {
        glDeleteProgram(screen_shader_.program);
        screen_shader_.program = 0;
    }
    screen_shader_.source_path.clear();
}

void Effects::renderScreenShader(wlr_output* output, float time) {
    if (screen_shader_.program == 0 || !output) return;

    int w, h;
    wlr_output_effective_resolution(output, &w, &h);

    glUseProgram(screen_shader_.program);
    if (screen_shader_.u_texture >= 0)
        glUniform1i(screen_shader_.u_texture, 0);
    if (screen_shader_.u_time >= 0)
        glUniform1f(screen_shader_.u_time, time);
    if (screen_shader_.u_resolution >= 0)
        glUniform2f(screen_shader_.u_resolution,
                    static_cast<float>(w), static_cast<float>(h));

    glViewport(0, 0, w, h);
    drawQuad();
    glUseProgram(0);
}

// -- Private ----------------------------------------------------------------

void Effects::initQuad() {
    glGenVertexArrays(1, &quad_vao_);
    glGenBuffers(1, &quad_vbo_);

    glBindVertexArray(quad_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadVertices), kQuadVertices,
                 GL_STATIC_DRAW);

    // location 0: position (vec2)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<void*>(0));
    // location 1: texcoord (vec2)
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<void*>(2 * sizeof(float)));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void Effects::drawQuad() {
    glBindVertexArray(quad_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

bool Effects::compileEffectShaders() {
    // Shadow (Task 19) -- per-corner radii, spread, offset
    shadow_program_ = Shaders::buildProgram(Shaders::quad_vert,
                                            Shaders::shadow_frag);
    // Gradient
    gradient_program_ = Shaders::buildProgram(Shaders::quad_vert,
                                              Shaders::gradient_frag);
    // Dimming
    dim_program_ = Shaders::buildProgram(Shaders::quad_vert,
                                         Shaders::dim_frag);
    // Opacity
    opacity_program_ = Shaders::buildProgram(Shaders::quad_vert,
                                             Shaders::opacity_frag);
    // Rounded rect with per-corner radii (Task 17)
    rounded_program_ = Shaders::buildProgram(Shaders::quad_vert,
                                             Shaders::rounded_rect_frag);
    // Rounded texture clip (Task 17)
    rounded_tex_program_ = Shaders::buildProgram(Shaders::quad_vert,
                                                 Shaders::rounded_tex_frag);
    // Gradient border with Oklab interpolation (Task 18)
    border_grad_program_ = Shaders::buildProgram(Shaders::quad_vert,
                                                 Shaders::border_gradient_frag);

    bool ok = shadow_program_ && gradient_program_ && dim_program_ &&
              opacity_program_ && rounded_program_ && rounded_tex_program_ &&
              border_grad_program_;

    if (!ok) {
        wlr_log(WLR_ERROR, "One or more effect shaders failed to compile: "
                "shadow=%u gradient=%u dim=%u opacity=%u rounded=%u "
                "rounded_tex=%u border_grad=%u",
                shadow_program_, gradient_program_, dim_program_,
                opacity_program_, rounded_program_, rounded_tex_program_,
                border_grad_program_);
    }

    return ok;
}

bool Effects::loadScreenShader(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        wlr_log(WLR_ERROR, "Cannot open screen shader: %s", path.c_str());
        return false;
    }

    std::ostringstream ss;
    ss << file.rdbuf();
    std::string frag_source = ss.str();

    GLuint prog = Shaders::buildProgram(Shaders::screen_shader_vert,
                                        std::string_view{frag_source});
    if (prog == 0) {
        wlr_log(WLR_ERROR, "Failed to compile screen shader: %s", path.c_str());
        return false;
    }

    screen_shader_.program      = prog;
    screen_shader_.source_path  = path.string();
    screen_shader_.u_texture    = glGetUniformLocation(prog, "u_texture");
    screen_shader_.u_time       = glGetUniformLocation(prog, "u_time");
    screen_shader_.u_resolution = glGetUniformLocation(prog, "u_resolution");

    return true;
}

} // namespace eternal

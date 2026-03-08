#include "eternal/render/DualKawaseBlur.hpp"
#include "eternal/render/Effects.hpp"
#include "eternal/render/Shaders.hpp"

extern "C" {
#include <wlr/util/log.h>
}

#include <algorithm>
#include <cassert>

namespace eternal {

// ---------------------------------------------------------------------------
// BlurFramebuffer
// ---------------------------------------------------------------------------

void BlurFramebuffer::create(int w, int h) {
    width  = w;
    height = h;

    glGenFramebuffers(1, &fbo);
    glGenTextures(1, &texture);

    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, texture, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        wlr_log(WLR_ERROR, "BlurFramebuffer incomplete: 0x%x (size %dx%d)",
                status, w, h);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void BlurFramebuffer::destroy() {
    if (fbo)     { glDeleteFramebuffers(1, &fbo); fbo = 0; }
    if (texture) { glDeleteTextures(1, &texture); texture = 0; }
    width = height = 0;
}

void BlurFramebuffer::bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width, height);
}

// ---------------------------------------------------------------------------
// DualKawaseBlur
// ---------------------------------------------------------------------------

DualKawaseBlur::DualKawaseBlur() = default;

DualKawaseBlur::~DualKawaseBlur() {
    destroy();
}

bool DualKawaseBlur::init() {
    // Compile downsample shader.
    down_program_ = Shaders::buildProgram(Shaders::blur_down_vert,
                                          Shaders::blur_down_frag);
    // Compile upsample shader.
    up_program_   = Shaders::buildProgram(Shaders::blur_up_vert,
                                          Shaders::blur_up_frag);
    // Compile post-process (finish) shader.
    finish_program_ = Shaders::buildProgram(Shaders::quad_vert,
                                            Shaders::blur_finish_frag);

    if (!down_program_ || !up_program_ || !finish_program_) {
        wlr_log(WLR_ERROR, "Dual-Kawase blur shader compilation failed");
        return false;
    }

    // Cache uniform locations -- downsample
    down_u_texture_   = glGetUniformLocation(down_program_, "u_texture");
    down_u_halfpixel_ = glGetUniformLocation(down_program_, "u_halfpixel");
    down_u_offset_    = glGetUniformLocation(down_program_, "u_offset");

    // Cache uniform locations -- upsample
    up_u_texture_   = glGetUniformLocation(up_program_, "u_texture");
    up_u_halfpixel_ = glGetUniformLocation(up_program_, "u_halfpixel");
    up_u_offset_    = glGetUniformLocation(up_program_, "u_offset");

    // Cache uniform locations -- post-process
    finish_u_texture_    = glGetUniformLocation(finish_program_, "u_texture");
    finish_u_noise_str_  = glGetUniformLocation(finish_program_, "u_noise_strength");
    finish_u_contrast_   = glGetUniformLocation(finish_program_, "u_contrast");
    finish_u_brightness_ = glGetUniformLocation(finish_program_, "u_brightness");
    finish_u_vibrancy_   = glGetUniformLocation(finish_program_, "u_vibrancy");
    finish_u_time_       = glGetUniformLocation(finish_program_, "u_time");
    finish_u_noise_en_   = glGetUniformLocation(finish_program_, "u_noise_enabled");

    // Create shared fullscreen quad.
    static constexpr float quad[] = {
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
    };

    glGenVertexArrays(1, &quad_vao_);
    glGenBuffers(1, &quad_vbo_);

    glBindVertexArray(quad_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                          reinterpret_cast<void*>(2 * sizeof(float)));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    wlr_log(WLR_INFO, "Dual-Kawase blur initialized with post-processing");
    return true;
}

void DualKawaseBlur::downsample(GLuint source_texture,
                                 int src_width, int src_height,
                                 int iterations, float offset) {
    ensureFramebuffers(src_width, src_height, iterations);

    glUseProgram(down_program_);
    glUniform1i(down_u_texture_, 0);
    glUniform1f(down_u_offset_, offset);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, source_texture);

    int w = src_width;
    int h = src_height;

    for (int i = 0; i < iterations; ++i) {
        auto& fb = framebuffers_[i];
        fb.bind();

        glUniform2f(down_u_halfpixel_,
                    0.5f / static_cast<float>(w),
                    0.5f / static_cast<float>(h));

        drawQuad();

        // Next iteration reads from this framebuffer's texture.
        glBindTexture(GL_TEXTURE_2D, fb.texture);
        w = fb.width;
        h = fb.height;
    }

    glUseProgram(0);
}

void DualKawaseBlur::upsample(int iterations, float offset) {
    if (iterations <= 0) return;

    glUseProgram(up_program_);
    glUniform1i(up_u_texture_, 0);
    glUniform1f(up_u_offset_, offset);

    glActiveTexture(GL_TEXTURE0);

    // Walk back up the chain from smallest to largest.
    for (int i = iterations - 1; i > 0; --i) {
        auto& src = framebuffers_[i];
        auto& dst = framebuffers_[i - 1];

        dst.bind();
        glBindTexture(GL_TEXTURE_2D, src.texture);

        glUniform2f(up_u_halfpixel_,
                    0.5f / static_cast<float>(dst.width),
                    0.5f / static_cast<float>(dst.height));

        drawQuad();
    }

    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void DualKawaseBlur::postProcess(const BlurRenderParams& params, float time) {
    if (!finish_program_ || framebuffers_.empty()) return;

    // Ensure the finish FBO matches the result size.
    auto& result = framebuffers_[0];
    if (finish_fb_.width != result.width || finish_fb_.height != result.height) {
        finish_fb_.destroy();
        finish_fb_.create(result.width, result.height);
    }

    finish_fb_.bind();

    glUseProgram(finish_program_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, result.texture);

    glUniform1i(finish_u_texture_, 0);
    glUniform1f(finish_u_noise_str_, params.noise_strength);
    glUniform1f(finish_u_contrast_, params.contrast);
    glUniform1f(finish_u_brightness_, params.brightness);
    glUniform1f(finish_u_vibrancy_, params.vibrancy);
    glUniform1f(finish_u_time_, time);
    glUniform1i(finish_u_noise_en_, params.noise ? 1 : 0);

    drawQuad();

    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

GLuint DualKawaseBlur::render(GLuint source_texture,
                               int src_width, int src_height,
                               const BlurRenderParams& params, float time) {
    int iterations = params.iterations;
    if (iterations <= 0) iterations = 1;

    // The offset controls the blur radius.  Map the 'size' concept:
    // higher offset = more spread per sample.
    float offset = params.offset;

    downsample(source_texture, src_width, src_height, iterations, offset);
    upsample(iterations, offset);

    // Apply post-processing if any adjustments are needed.
    bool needs_post = params.noise ||
                      params.contrast != 1.0f ||
                      params.brightness != 1.0f ||
                      params.vibrancy != 0.0f;

    if (needs_post) {
        postProcess(params, time);
        return finish_fb_.texture;
    }

    return resultTexture();
}

void DualKawaseBlur::destroy() {
    for (auto& fb : framebuffers_) fb.destroy();
    framebuffers_.clear();
    finish_fb_.destroy();
    current_iterations_ = 0;

    if (down_program_)   { glDeleteProgram(down_program_);   down_program_   = 0; }
    if (up_program_)     { glDeleteProgram(up_program_);     up_program_     = 0; }
    if (finish_program_) { glDeleteProgram(finish_program_); finish_program_ = 0; }
    if (quad_vbo_)       { glDeleteBuffers(1, &quad_vbo_);   quad_vbo_       = 0; }
    if (quad_vao_)       { glDeleteVertexArrays(1, &quad_vao_); quad_vao_    = 0; }
}

GLuint DualKawaseBlur::resultTexture() const {
    if (framebuffers_.empty()) return 0;
    return framebuffers_[0].texture;
}

const BlurFramebuffer& DualKawaseBlur::resultFB() const {
    if (finish_fb_.texture != 0) return finish_fb_;
    static BlurFramebuffer empty;
    if (framebuffers_.empty()) return empty;
    return framebuffers_[0];
}

void DualKawaseBlur::ensureFramebuffers(int base_width, int base_height,
                                         int iterations) {
    // Only reallocate if the count or base size changed.
    if (iterations == current_iterations_ &&
        !framebuffers_.empty() &&
        framebuffers_[0].width == std::max(base_width / 2, 1) &&
        framebuffers_[0].height == std::max(base_height / 2, 1)) {
        return;
    }

    // Tear down old chain.
    for (auto& fb : framebuffers_) fb.destroy();
    framebuffers_.clear();

    framebuffers_.resize(iterations);
    int w = base_width;
    int h = base_height;

    for (int i = 0; i < iterations; ++i) {
        w = std::max(w / 2, 1);
        h = std::max(h / 2, 1);
        framebuffers_[i].create(w, h);
    }

    current_iterations_ = iterations;
}

void DualKawaseBlur::drawQuad() {
    glBindVertexArray(quad_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
}

} // namespace eternal

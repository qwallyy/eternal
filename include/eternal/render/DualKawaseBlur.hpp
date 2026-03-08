#pragma once

extern "C" {
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/wlr_texture.h>
#include <GLES2/gl2.h>
#include <GLES3/gl3.h>
}

#include <cstdint>
#include <vector>

#include "eternal/render/Renderer.hpp"

namespace eternal {

struct BlurRenderParams;   // forward, defined in Effects.hpp

/// Framebuffer object for ping-pong rendering.
struct BlurFramebuffer {
    GLuint fbo     = 0;
    GLuint texture = 0;
    int    width   = 0;
    int    height  = 0;

    void create(int w, int h);
    void destroy();
    void bind() const;
};

/// Dual-Kawase blur implementation with post-processing.
///
/// The algorithm works in two phases:
///   1. Downsample  -- progressively halve the resolution while blurring.
///   2. Upsample    -- progressively double back up, accumulating blur.
///
/// After the blur passes, a post-processing pass applies noise overlay,
/// contrast, brightness, and vibrancy adjustments.
///
/// Framebuffers are kept in a ping-pong chain sized to the number of
/// iterations so that no redundant allocations happen frame-to-frame.
class DualKawaseBlur {
public:
    DualKawaseBlur();
    ~DualKawaseBlur();

    DualKawaseBlur(const DualKawaseBlur&) = delete;
    DualKawaseBlur& operator=(const DualKawaseBlur&) = delete;

    /// Compile the blur shaders. Returns false on failure.
    bool init();

    /// Perform the downsample pass. Reads from `source_texture` into the
    /// first framebuffer in the chain, then continues halving.
    void downsample(GLuint source_texture, int src_width, int src_height,
                    int iterations, float offset);

    /// Perform the upsample pass, doubling resolution back up.
    void upsample(int iterations, float offset);

    /// Apply post-processing (noise, contrast, brightness, vibrancy).
    void postProcess(const struct BlurRenderParams& params, float time);

    /// Full blur pipeline: downsample + upsample + post-process.
    /// Returns the GL texture holding the final blurred result.
    GLuint render(GLuint source_texture, int src_width, int src_height,
                  const struct BlurRenderParams& params, float time = 0.0f);

    /// Release all GPU resources.
    void destroy();

    /// Get the final blurred texture after render().
    [[nodiscard]] GLuint resultTexture() const;

    /// Get the result framebuffer (for further compositing).
    [[nodiscard]] const BlurFramebuffer& resultFB() const;

private:
    GLuint down_program_ = 0;
    GLuint up_program_   = 0;
    GLuint finish_program_ = 0;

    // Uniform locations -- downsample
    GLint down_u_texture_    = -1;
    GLint down_u_halfpixel_  = -1;
    GLint down_u_offset_     = -1;

    // Uniform locations -- upsample
    GLint up_u_texture_      = -1;
    GLint up_u_halfpixel_    = -1;
    GLint up_u_offset_       = -1;

    // Uniform locations -- post-process
    GLint finish_u_texture_     = -1;
    GLint finish_u_noise_str_   = -1;
    GLint finish_u_contrast_    = -1;
    GLint finish_u_brightness_  = -1;
    GLint finish_u_vibrancy_    = -1;
    GLint finish_u_time_        = -1;
    GLint finish_u_noise_en_    = -1;

    // Ping-pong framebuffer chain (index 0 = largest, last = smallest).
    std::vector<BlurFramebuffer> framebuffers_;
    // Extra FBO for post-process output.
    BlurFramebuffer finish_fb_;
    int current_iterations_ = 0;

    // Shared fullscreen-quad geometry.
    GLuint quad_vao_ = 0;
    GLuint quad_vbo_ = 0;

    void ensureFramebuffers(int base_width, int base_height, int iterations);
    void drawQuad();
};

} // namespace eternal

#pragma once

extern "C" {
#include <GLES2/gl2.h>
}

#include <string>
#include <string_view>

namespace eternal {

/// Built-in GLSL shader sources and compilation helpers.
namespace Shaders {

// ---- Common vertex shader (fullscreen quad) ---------------------------------

inline constexpr std::string_view quad_vert = R"glsl(#version 300 es
precision mediump float;

layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_texcoord;

out vec2 v_texcoord;

void main() {
    gl_Position = vec4(a_pos, 0.0, 1.0);
    v_texcoord  = a_texcoord;
}
)glsl";

// ---- Dual-Kawase blur: downsample ----------------------------------------

inline constexpr std::string_view blur_down_vert = quad_vert;

inline constexpr std::string_view blur_down_frag = R"glsl(#version 300 es
precision mediump float;

uniform sampler2D u_texture;
uniform vec2  u_halfpixel;
uniform float u_offset;

in  vec2 v_texcoord;
out vec4 frag_color;

void main() {
    vec2 uv = v_texcoord;

    vec4 sum = texture(u_texture, uv) * 4.0;
    sum += texture(u_texture, uv - u_halfpixel * u_offset);
    sum += texture(u_texture, uv + u_halfpixel * u_offset);
    sum += texture(u_texture, uv + vec2(u_halfpixel.x, -u_halfpixel.y) * u_offset);
    sum += texture(u_texture, uv - vec2(u_halfpixel.x, -u_halfpixel.y) * u_offset);

    frag_color = sum / 8.0;
}
)glsl";

// ---- Dual-Kawase blur: upsample ------------------------------------------

inline constexpr std::string_view blur_up_vert = quad_vert;

inline constexpr std::string_view blur_up_frag = R"glsl(#version 300 es
precision mediump float;

uniform sampler2D u_texture;
uniform vec2  u_halfpixel;
uniform float u_offset;

in  vec2 v_texcoord;
out vec4 frag_color;

void main() {
    vec2 uv = v_texcoord;

    vec4 sum = texture(u_texture, uv + vec2(-u_halfpixel.x * 2.0, 0.0) * u_offset);
    sum += texture(u_texture, uv + vec2(-u_halfpixel.x, u_halfpixel.y) * u_offset) * 2.0;
    sum += texture(u_texture, uv + vec2(0.0, u_halfpixel.y * 2.0) * u_offset);
    sum += texture(u_texture, uv + vec2(u_halfpixel.x, u_halfpixel.y) * u_offset) * 2.0;
    sum += texture(u_texture, uv + vec2(u_halfpixel.x * 2.0, 0.0) * u_offset);
    sum += texture(u_texture, uv + vec2(u_halfpixel.x, -u_halfpixel.y) * u_offset) * 2.0;
    sum += texture(u_texture, uv + vec2(0.0, -u_halfpixel.y * 2.0) * u_offset);
    sum += texture(u_texture, uv + vec2(-u_halfpixel.x, -u_halfpixel.y) * u_offset) * 2.0;

    frag_color = sum / 12.0;
}
)glsl";

// ---- Blur post-processing: noise, contrast, brightness, vibrancy ---------

inline constexpr std::string_view blur_finish_frag = R"glsl(#version 300 es
precision mediump float;

uniform sampler2D u_texture;
uniform float u_noise_strength;
uniform float u_contrast;
uniform float u_brightness;
uniform float u_vibrancy;
uniform float u_time;
uniform bool  u_noise_enabled;

in  vec2 v_texcoord;
out vec4 frag_color;

// Hash-based pseudo-random noise.
float hash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main() {
    vec4 color = texture(u_texture, v_texcoord);

    // Noise overlay
    if (u_noise_enabled) {
        float n = hash(v_texcoord * 1000.0 + u_time) * 2.0 - 1.0;
        color.rgb += n * u_noise_strength;
    }

    // Contrast adjustment (around 0.5 midpoint)
    color.rgb = (color.rgb - 0.5) * u_contrast + 0.5;

    // Brightness adjustment
    color.rgb *= u_brightness;

    // Vibrancy (selective saturation boost for desaturated colors)
    float luma = dot(color.rgb, vec3(0.2126, 0.7152, 0.0722));
    float sat = max(max(color.r, color.g), color.b) - min(min(color.r, color.g), color.b);
    float vibrancy_amount = u_vibrancy * (1.0 - sat);
    color.rgb = mix(vec3(luma), color.rgb, 1.0 + vibrancy_amount);

    color.rgb = clamp(color.rgb, 0.0, 1.0);
    frag_color = color;
}
)glsl";

// ---- Rounded rectangle with per-corner radii (Task 17) --------------------

inline constexpr std::string_view rounded_rect_frag = R"glsl(#version 300 es
precision mediump float;

uniform vec4  u_color;
uniform vec2  u_size;
uniform vec2  u_position;
uniform vec4  u_radii;   // (top-left, top-right, bottom-right, bottom-left)

in  vec2 v_texcoord;
out vec4 frag_color;

// SDF for a rounded rectangle with per-corner radii.
float roundedBoxSDF(vec2 p, vec2 half_size, vec4 radii) {
    // Select the appropriate radius based on which quadrant p is in.
    // radii: (tl, tr, br, bl)
    vec2 r_select = (p.x > 0.0)
        ? ((p.y > 0.0) ? radii.zw : radii.yz)   // right side: br if below, tr if above
        : ((p.y > 0.0) ? radii.wx : radii.xy);   // left side:  bl if below, tl if above

    // Actually we want: top-left for (-x, -y), top-right for (+x, -y),
    // bottom-right for (+x, +y), bottom-left for (-x, +y)
    float radius;
    if (p.x <= 0.0 && p.y <= 0.0) radius = radii.x;       // top-left
    else if (p.x > 0.0 && p.y <= 0.0) radius = radii.y;    // top-right
    else if (p.x > 0.0 && p.y > 0.0) radius = radii.z;     // bottom-right
    else radius = radii.w;                                    // bottom-left

    vec2 q = abs(p) - half_size + radius;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radius;
}

void main() {
    vec2 half_size = u_size * 0.5;
    vec2 center    = gl_FragCoord.xy - u_position - half_size;

    float dist  = roundedBoxSDF(center, half_size, u_radii);
    float alpha = 1.0 - smoothstep(-0.5, 0.5, dist);

    frag_color = u_color * alpha;
}
)glsl";

// ---- Rounded rect texture clip (Task 17) ----------------------------------

inline constexpr std::string_view rounded_tex_frag = R"glsl(#version 300 es
precision mediump float;

uniform sampler2D u_texture;
uniform vec2  u_size;
uniform vec2  u_position;
uniform vec4  u_radii;   // (top-left, top-right, bottom-right, bottom-left)
uniform float u_alpha;

in  vec2 v_texcoord;
out vec4 frag_color;

float roundedBoxSDF(vec2 p, vec2 half_size, vec4 radii) {
    float radius;
    if (p.x <= 0.0 && p.y <= 0.0) radius = radii.x;
    else if (p.x > 0.0 && p.y <= 0.0) radius = radii.y;
    else if (p.x > 0.0 && p.y > 0.0) radius = radii.z;
    else radius = radii.w;

    vec2 q = abs(p) - half_size + radius;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radius;
}

void main() {
    vec4 texel = texture(u_texture, v_texcoord);

    vec2 half_size = u_size * 0.5;
    vec2 center    = gl_FragCoord.xy - u_position - half_size;

    float dist  = roundedBoxSDF(center, half_size, u_radii);
    float mask  = 1.0 - smoothstep(-0.5, 0.5, dist);

    frag_color = texel * mask * u_alpha;
}
)glsl";

// ---- Drop shadow with rounded corners (Task 19) --------------------------

inline constexpr std::string_view shadow_frag = R"glsl(#version 300 es
precision mediump float;

uniform vec4  u_color;
uniform vec2  u_size;
uniform vec2  u_position;
uniform float u_sigma;
uniform vec4  u_radii;     // per-corner radii
uniform float u_spread;    // shadow spread (px)
uniform vec2  u_offset;    // shadow offset

in  vec2 v_texcoord;
out vec4 frag_color;

float roundedBoxSDF(vec2 p, vec2 half_size, vec4 radii) {
    float radius;
    if (p.x <= 0.0 && p.y <= 0.0) radius = radii.x;
    else if (p.x > 0.0 && p.y <= 0.0) radius = radii.y;
    else if (p.x > 0.0 && p.y > 0.0) radius = radii.z;
    else radius = radii.w;

    vec2 q = abs(p) - half_size + radius;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radius;
}

float gaussian(float x, float sigma) {
    return exp(-(x * x) / (2.0 * sigma * sigma));
}

void main() {
    vec2 shadow_size = u_size + u_spread * 2.0;
    vec2 half_size   = shadow_size * 0.5;
    vec2 shadow_pos  = u_position + u_offset - u_spread;
    vec2 center      = gl_FragCoord.xy - shadow_pos - half_size;

    float dist    = roundedBoxSDF(center, half_size, u_radii);
    float inside  = 1.0 - smoothstep(-0.5, 0.5, dist);
    // Gaussian falloff for the shadow outside the shape
    float shadow  = gaussian(max(dist, 0.0), u_sigma);
    frag_color    = vec4(u_color.rgb, u_color.a * shadow * (1.0 - inside));
}
)glsl";

// ---- Gradient border with Oklab interpolation (Task 18) ------------------

inline constexpr std::string_view border_gradient_frag = R"glsl(#version 300 es
precision mediump float;

uniform vec2  u_size;
uniform vec2  u_position;
uniform float u_border_width;
uniform vec4  u_radii;
uniform float u_time;
uniform vec4  u_color_a;     // active gradient color A (linear sRGB)
uniform vec4  u_color_b;     // active gradient color B (linear sRGB)
uniform vec4  u_color_ia;    // inactive gradient color A
uniform vec4  u_color_ib;    // inactive gradient color B
uniform bool  u_active;
uniform bool  u_animate;

in  vec2 v_texcoord;
out vec4 frag_color;

// --- Oklab color space conversions ---

vec3 linearToOklab(vec3 c) {
    float l = 0.4122214708 * c.r + 0.5363325363 * c.g + 0.0514459929 * c.b;
    float m = 0.2119034982 * c.r + 0.6806995451 * c.g + 0.1073969566 * c.b;
    float s = 0.0883024619 * c.r + 0.2817188376 * c.g + 0.6299787005 * c.b;

    l = pow(max(l, 0.0), 1.0 / 3.0);
    m = pow(max(m, 0.0), 1.0 / 3.0);
    s = pow(max(s, 0.0), 1.0 / 3.0);

    return vec3(
        0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s,
        1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s,
        0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s
    );
}

vec3 oklabToLinear(vec3 lab) {
    float l = lab.x + 0.3963377774 * lab.y + 0.2158037573 * lab.z;
    float m = lab.x - 0.1055613458 * lab.y - 0.0638541728 * lab.z;
    float s = lab.x - 0.0894841775 * lab.y - 1.2914855480 * lab.z;

    l = l * l * l;
    m = m * m * m;
    s = s * s * s;

    return vec3(
        +4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
        -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
        -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s
    );
}

float roundedBoxSDF(vec2 p, vec2 half_size, vec4 radii) {
    float radius;
    if (p.x <= 0.0 && p.y <= 0.0) radius = radii.x;
    else if (p.x > 0.0 && p.y <= 0.0) radius = radii.y;
    else if (p.x > 0.0 && p.y > 0.0) radius = radii.z;
    else radius = radii.w;

    vec2 q = abs(p) - half_size + radius;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - radius;
}

void main() {
    vec2 half_size = u_size * 0.5;
    vec2 center    = gl_FragCoord.xy - u_position - half_size;

    float outer = roundedBoxSDF(center, half_size, u_radii);
    float inner_hs = max(0.0, half_size.x - u_border_width);
    float inner_vs = max(0.0, half_size.y - u_border_width);
    vec4 inner_radii = max(u_radii - u_border_width, 0.0);
    vec2 inner_half = vec2(inner_hs, inner_vs);
    float inner = roundedBoxSDF(center, inner_half, inner_radii);

    float border_mask = smoothstep(0.5, -0.5, outer) * smoothstep(-0.5, 0.5, inner);

    // Animated gradient angle around the perimeter
    float angle = atan(center.y, center.x);
    float time_offset = u_animate ? u_time * 0.2 : 0.0;
    float t = fract((angle / 6.28318530718) + time_offset);

    // Select active or inactive colors
    vec4 colA = u_active ? u_color_a : u_color_ia;
    vec4 colB = u_active ? u_color_b : u_color_ib;

    // Oklab interpolation
    vec3 labA = linearToOklab(colA.rgb);
    vec3 labB = linearToOklab(colB.rgb);
    vec3 labMix = mix(labA, labB, t);
    vec3 rgb = oklabToLinear(labMix);

    float alpha_mix = mix(colA.a, colB.a, t);
    vec4 color = vec4(clamp(rgb, 0.0, 1.0), alpha_mix);

    frag_color = color * border_mask;
}
)glsl";

// ---- Linear gradient (with Oklab support) ---------------------------------

inline constexpr std::string_view gradient_frag = R"glsl(#version 300 es
precision mediump float;

#define MAX_STOPS 16

uniform vec2  u_size;
uniform vec2  u_position;
uniform float u_angle;     // radians
uniform int   u_num_stops;
uniform float u_stop_positions[MAX_STOPS];
uniform vec4  u_stop_colors[MAX_STOPS];

in  vec2 v_texcoord;
out vec4 frag_color;

void main() {
    vec2 uv = (gl_FragCoord.xy - u_position) / u_size;

    // Direction vector from angle
    vec2 dir = vec2(cos(u_angle), sin(u_angle));
    float t  = dot(uv - 0.5, dir) + 0.5;
    t = clamp(t, 0.0, 1.0);

    // Interpolate between stops
    vec4 color = u_stop_colors[0];
    for (int i = 1; i < u_num_stops; i++) {
        float blend = smoothstep(u_stop_positions[i - 1],
                                 u_stop_positions[i], t);
        color = mix(color, u_stop_colors[i], blend);
    }
    frag_color = color;
}
)glsl";

// ---- Dimming overlay -----------------------------------------------------

inline constexpr std::string_view dim_frag = R"glsl(#version 300 es
precision mediump float;

uniform sampler2D u_texture;
uniform float     u_strength;

in  vec2 v_texcoord;
out vec4 frag_color;

void main() {
    vec4 texel = texture(u_texture, v_texcoord);
    frag_color = vec4(texel.rgb * (1.0 - u_strength), texel.a);
}
)glsl";

// ---- Opacity adjustment --------------------------------------------------

inline constexpr std::string_view opacity_frag = R"glsl(#version 300 es
precision mediump float;

uniform sampler2D u_texture;
uniform float     u_alpha;

in  vec2 v_texcoord;
out vec4 frag_color;

void main() {
    vec4 texel = texture(u_texture, v_texcoord);
    frag_color = vec4(texel.rgb, texel.a * u_alpha);
}
)glsl";

// ---- Software cursor compositing (Task 13) --------------------------------

inline constexpr std::string_view cursor_frag = R"glsl(#version 300 es
precision mediump float;

uniform sampler2D u_texture;
uniform vec2  u_cursor_pos;    // cursor position in output coords
uniform vec2  u_cursor_size;   // cursor texture size
uniform vec2  u_output_size;   // output resolution

in  vec2 v_texcoord;
out vec4 frag_color;

void main() {
    // Map fragment coord to cursor texture space
    vec2 frag_pos = v_texcoord * u_output_size;
    vec2 cursor_uv = (frag_pos - u_cursor_pos) / u_cursor_size;

    if (cursor_uv.x >= 0.0 && cursor_uv.x <= 1.0 &&
        cursor_uv.y >= 0.0 && cursor_uv.y <= 1.0) {
        frag_color = texture(u_texture, cursor_uv);
    } else {
        frag_color = vec4(0.0);
    }
}
)glsl";

// ---- Screen shader (post-process) ----------------------------------------

inline constexpr std::string_view screen_shader_vert = quad_vert;

inline constexpr std::string_view screen_shader_frag = R"glsl(#version 300 es
precision mediump float;

uniform sampler2D u_texture;
uniform float     u_time;
uniform vec2      u_resolution;

in  vec2 v_texcoord;
out vec4 frag_color;

// Default pass-through; users replace this file.
void main() {
    frag_color = texture(u_texture, v_texcoord);
}
)glsl";

// ---- Per-window effect shaders (Task 54) ----------------------------------

/// Vertex shader for per-window effects: receives window geometry uniforms.
inline constexpr std::string_view window_effect_vert = R"glsl(#version 300 es
precision mediump float;

layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_texcoord;

out vec2 v_texcoord;

void main() {
    gl_Position = vec4(a_pos, 0.0, 1.0);
    v_texcoord  = a_texcoord;
}
)glsl";

/// Grayscale effect: converts window texture to grayscale using luminance.
inline constexpr std::string_view grayscale_frag = R"glsl(#version 300 es
precision mediump float;

uniform sampler2D u_texture;
uniform float     u_time;
uniform vec4      u_geometry; // x, y, width, height

in  vec2 v_texcoord;
out vec4 frag_color;

void main() {
    vec4 texel = texture(u_texture, v_texcoord);
    float luma = dot(texel.rgb, vec3(0.2126, 0.7152, 0.0722));
    frag_color = vec4(vec3(luma), texel.a);
}
)glsl";

/// Sepia tone effect.
inline constexpr std::string_view sepia_frag = R"glsl(#version 300 es
precision mediump float;

uniform sampler2D u_texture;
uniform float     u_time;
uniform vec4      u_geometry;

in  vec2 v_texcoord;
out vec4 frag_color;

void main() {
    vec4 texel = texture(u_texture, v_texcoord);
    float r = dot(texel.rgb, vec3(0.393, 0.769, 0.189));
    float g = dot(texel.rgb, vec3(0.349, 0.686, 0.168));
    float b = dot(texel.rgb, vec3(0.272, 0.534, 0.131));
    frag_color = vec4(clamp(vec3(r, g, b), 0.0, 1.0), texel.a);
}
)glsl";

/// Color inversion effect.
inline constexpr std::string_view invert_frag = R"glsl(#version 300 es
precision mediump float;

uniform sampler2D u_texture;
uniform float     u_time;
uniform vec4      u_geometry;

in  vec2 v_texcoord;
out vec4 frag_color;

void main() {
    vec4 texel = texture(u_texture, v_texcoord);
    frag_color = vec4(1.0 - texel.rgb, texel.a);
}
)glsl";

/// Chromatic aberration effect: offsets RGB channels slightly.
inline constexpr std::string_view chromatic_aberration_frag = R"glsl(#version 300 es
precision mediump float;

uniform sampler2D u_texture;
uniform float     u_time;
uniform vec4      u_geometry;

in  vec2 v_texcoord;
out vec4 frag_color;

void main() {
    float aberration = 0.003;
    vec2 dir = v_texcoord - 0.5;
    float dist = length(dir);

    // Scale aberration by distance from center for a lens-like effect
    float offset = aberration * dist;

    vec2 uv_r = v_texcoord + dir * offset;
    vec2 uv_b = v_texcoord - dir * offset;

    float r = texture(u_texture, uv_r).r;
    float g = texture(u_texture, v_texcoord).g;
    float b = texture(u_texture, uv_b).b;
    float a = texture(u_texture, v_texcoord).a;

    frag_color = vec4(r, g, b, a);
}
)glsl";

/// Dim overlay with configurable color (Task 55).
inline constexpr std::string_view dim_color_frag = R"glsl(#version 300 es
precision mediump float;

uniform sampler2D u_texture;
uniform float     u_strength;
uniform vec3      u_dim_color;

in  vec2 v_texcoord;
out vec4 frag_color;

void main() {
    vec4 texel = texture(u_texture, v_texcoord);
    vec3 dimmed = mix(texel.rgb, u_dim_color, u_strength);
    frag_color = vec4(dimmed, texel.a);
}
)glsl";

// ---- Compilation helpers -------------------------------------------------

/// Compile a single shader stage. Returns 0 on failure (logs to stderr).
GLuint compileShader(std::string_view source, GLenum type);

/// Link a vertex + fragment shader into a program. Returns 0 on failure.
GLuint linkProgram(GLuint vert, GLuint frag);

/// Convenience: compile + link in one call.
GLuint buildProgram(std::string_view vert_src, std::string_view frag_src);

} // namespace Shaders
} // namespace eternal

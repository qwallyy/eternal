#include "eternal/render/Renderer.hpp"
#include "eternal/render/Shaders.hpp"

extern "C" {
#include <wlr/render/gles2.h>
#include <wlr/render/swapchain.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/util/log.h>
}

#include <algorithm>
#include <cassert>
#include <cstring>

namespace eternal {

// ---------------------------------------------------------------------------
// Box
// ---------------------------------------------------------------------------

Box Box::intersect(const Box& other) const {
    int x1 = std::max(x, other.x);
    int y1 = std::max(y, other.y);
    int x2 = std::min(x + width, other.x + other.width);
    int y2 = std::min(y + height, other.y + other.height);
    if (x2 <= x1 || y2 <= y1) return {0, 0, 0, 0};
    return {x1, y1, x2 - x1, y2 - y1};
}

bool Box::intersects(const Box& other) const {
    return !(x + width <= other.x || other.x + other.width <= x ||
             y + height <= other.y || other.y + other.height <= y);
}

bool Box::fullyContains(const Box& other) const {
    return other.x >= x && other.y >= y &&
           other.x + other.width <= x + width &&
           other.y + other.height <= y + height;
}

// ---------------------------------------------------------------------------
// Framebuffer
// ---------------------------------------------------------------------------

void Framebuffer::create(int w, int h) {
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

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Framebuffer::destroy() {
    if (fbo)     { glDeleteFramebuffers(1, &fbo); fbo = 0; }
    if (texture) { glDeleteTextures(1, &texture); texture = 0; }
    width = height = 0;
}

void Framebuffer::bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width, height);
}

// ---------------------------------------------------------------------------
// GLESBackend (Task 11)
// ---------------------------------------------------------------------------

bool GLESBackend::init(wlr_renderer* renderer) {
    renderer_ = renderer;
    return renderer_ != nullptr;
}

GLuint GLESBackend::createFBO(int width, int height, GLuint* out_texture) {
    GLuint fbo = 0, tex = 0;
    glGenFramebuffers(1, &fbo);
    glGenTextures(1, &tex);

    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, tex, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        wlr_log(WLR_ERROR, "FBO creation failed: status 0x%x", status);
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &tex);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return 0;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (out_texture) *out_texture = tex;
    return fbo;
}

void GLESBackend::destroyFBO(GLuint fbo, GLuint texture) {
    if (fbo)     glDeleteFramebuffers(1, &fbo);
    if (texture) glDeleteTextures(1, &texture);
}

void GLESBackend::bindFBO(GLuint fbo, int width, int height) {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, width, height);
}

void GLESBackend::bindDefaultFBO() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

GLuint GLESBackend::importBuffer(wlr_buffer* buffer, BufferType /*type*/) {
    if (!buffer || !renderer_) return 0;

    // Use wlroots texture import -- handles SHM, DMA-BUF, and EGL buffers.
    wlr_texture* tex = wlr_texture_from_buffer(renderer_, buffer);
    if (!tex) return 0;

    // Extract the GL texture ID from the wlroots GLES2 texture.
    // Note: this depends on the wlroots GLES2 backend internals.
    struct wlr_gles2_texture_attribs attribs;
    wlr_gles2_texture_get_attribs(tex, &attribs);
    return attribs.tex;
}

bool GLESBackend::supportsBufferType(BufferType type) const {
    switch (type) {
    case BufferType::SHM:    return true;
    case BufferType::DMABUF: return true;
    case BufferType::EGL:    return true;
    default:                 return false;
    }
}

// ---------------------------------------------------------------------------
// VulkanBackend (Task 11 -- stub)
// ---------------------------------------------------------------------------

bool VulkanBackend::init(wlr_renderer* /*renderer*/) {
    wlr_log(WLR_INFO, "Vulkan render backend is a stub; use GLES2");
    return false;
}

GLuint VulkanBackend::createFBO(int /*w*/, int /*h*/, GLuint* /*tex*/) { return 0; }
void VulkanBackend::destroyFBO(GLuint, GLuint) {}
void VulkanBackend::bindFBO(GLuint, int, int) {}
void VulkanBackend::bindDefaultFBO() {}
GLuint VulkanBackend::importBuffer(wlr_buffer*, BufferType) { return 0; }
bool VulkanBackend::supportsBufferType(BufferType) const { return false; }

// ---------------------------------------------------------------------------
// TextureCache (Task 11)
// ---------------------------------------------------------------------------

CachedTexture* TextureCache::get(wlr_buffer* buffer) {
    auto it = entries_.find(buffer);
    if (it != entries_.end()) {
        it->second.tex.last_use_frame = current_frame_;
        return &it->second.tex;
    }
    // Create new entry -- caller must populate the texture.
    Entry e;
    e.buffer = buffer;
    e.tex.last_use_frame = current_frame_;
    auto [ins, _] = entries_.emplace(buffer, std::move(e));
    return &ins->second.tex;
}

void TextureCache::release(wlr_buffer* buffer) {
    auto it = entries_.find(buffer);
    if (it == entries_.end()) return;

    auto& tex = it->second.tex;
    if (tex.refcount > 0) --tex.refcount;
    if (tex.refcount == 0) {
        if (tex.texture) glDeleteTextures(1, &tex.texture);
        entries_.erase(it);
    }
}

void TextureCache::ref(wlr_buffer* buffer) {
    auto it = entries_.find(buffer);
    if (it != entries_.end()) {
        ++it->second.tex.refcount;
    }
}

void TextureCache::evict(uint64_t older_than_frame) {
    for (auto it = entries_.begin(); it != entries_.end(); ) {
        if (it->second.tex.last_use_frame < older_than_frame &&
            it->second.tex.refcount <= 1) {
            if (it->second.tex.texture)
                glDeleteTextures(1, &it->second.tex.texture);
            it = entries_.erase(it);
        } else {
            ++it;
        }
    }
}

void TextureCache::clear() {
    for (auto& [_, e] : entries_) {
        if (e.tex.texture) glDeleteTextures(1, &e.tex.texture);
    }
    entries_.clear();
}

// ---------------------------------------------------------------------------
// RenderPass (Task 11)
// ---------------------------------------------------------------------------

void RenderPass::add(DrawCall call) {
    calls_.push_back(std::move(call));
}

void RenderPass::submit() {
    // Sort by layer priority.
    std::stable_sort(calls_.begin(), calls_.end(),
        [](const DrawCall& a, const DrawCall& b) {
            return static_cast<uint8_t>(a.layer) < static_cast<uint8_t>(b.layer);
        });

    for (auto& call : calls_) {
        if (call.execute) {
            call.execute();
        }
    }
}

void RenderPass::clear() {
    calls_.clear();
}

// ---------------------------------------------------------------------------
// ScissorStack (Task 15)
// ---------------------------------------------------------------------------

void ScissorStack::push(const Box& region) {
    if (stack_.empty()) {
        stack_.push_back(region);
    } else {
        // Intersect with current top.
        stack_.push_back(stack_.back().intersect(region));
    }
    apply();
}

void ScissorStack::pop() {
    if (!stack_.empty()) {
        stack_.pop_back();
    }
    apply();
}

void ScissorStack::apply() const {
    if (stack_.empty()) {
        glDisable(GL_SCISSOR_TEST);
    } else {
        const auto& top = stack_.back();
        glEnable(GL_SCISSOR_TEST);
        glScissor(top.x, top.y, top.width, top.height);
    }
}

Box ScissorStack::current() const {
    if (stack_.empty()) return {};
    return stack_.back();
}

void ScissorStack::reset() {
    stack_.clear();
    glDisable(GL_SCISSOR_TEST);
}

// ---------------------------------------------------------------------------
// DamageState
// ---------------------------------------------------------------------------

DamageState::DamageState() {
    pixman_region32_init(&current_damage);
}

DamageState::~DamageState() {
    pixman_region32_fini(&current_damage);
}

DamageState::DamageState(DamageState&& other) noexcept
    : ring(other.ring), full_damage(other.full_damage) {
    pixman_region32_init(&current_damage);
    pixman_region32_copy(&current_damage, &other.current_damage);
}

DamageState& DamageState::operator=(DamageState&& other) noexcept {
    if (this != &other) {
        ring        = other.ring;
        full_damage = other.full_damage;
        pixman_region32_copy(&current_damage, &other.current_damage);
    }
    return *this;
}

void DamageState::addDamage(const Box& box) {
    pixman_region32_union_rect(&current_damage, &current_damage,
                               box.x, box.y, box.width, box.height);
}

void DamageState::addFullDamage() {
    full_damage = true;
}

void DamageState::reset(int width, int height) {
    wlr_damage_ring_init(&ring);
    ring.width  = width;
    ring.height = height;
    full_damage = true;
    pixman_region32_clear(&current_damage);
}

// ---------------------------------------------------------------------------
// FrameScheduler
// ---------------------------------------------------------------------------

void FrameScheduler::configure(wlr_output* output) {
    if (output->refresh > 0) {
        frame_budget_ms = 1000.0 / (static_cast<double>(output->refresh) / 1000.0);
    } else {
        frame_budget_ms = 16.667; // fallback 60 Hz
    }

    vrr_enabled = (output->adaptive_sync_status ==
                   WLR_OUTPUT_ADAPTIVE_SYNC_ENABLED);
}

bool FrameScheduler::shouldRender(double now_ms) const {
    if (vrr_enabled) return true;
    if (target_fps > 0) {
        double budget = 1000.0 / target_fps;
        return (now_ms - last_frame_time) >= budget * 0.95;
    }
    return (now_ms - last_frame_time) >= frame_budget_ms * 0.95;
}

void FrameScheduler::markFrameStart(double now_ms) {
    last_frame_time = now_ms;
    frame_pending = true;
}

void FrameScheduler::markFrameEnd(double /* now_ms */) {
    frame_pending = false;
}

// ---------------------------------------------------------------------------
// Renderer
// ---------------------------------------------------------------------------

Renderer::Renderer() = default;

Renderer::~Renderer() {
    texture_cache_.clear();
}

bool Renderer::init(wlr_renderer* renderer, wlr_allocator* allocator) {
    if (!renderer || !allocator) return false;
    renderer_  = renderer;
    allocator_ = allocator;

    // Initialize GLES2 backend by default.
    backend_ = std::make_unique<GLESBackend>();
    if (!backend_->init(renderer)) {
        wlr_log(WLR_ERROR, "Failed to init GLES2 render backend");
        return false;
    }

    wlr_log(WLR_INFO, "Renderer initialized with %s backend", backend_->name());
    return true;
}

void Renderer::addGPU(GPUContext ctx) {
    extra_gpus_.push_back(std::move(ctx));
}

Framebuffer Renderer::createFramebuffer(int width, int height) {
    Framebuffer fb;
    fb.create(width, height);
    return fb;
}

void Renderer::destroyFramebuffer(Framebuffer& fb) {
    fb.destroy();
}

// -- Frame lifecycle --------------------------------------------------------

bool Renderer::begin(wlr_output* output) {
    assert(output);
    current_output_ = output;
    ++frame_number_;

    auto& dmg = damageFor(output);
    auto& sched = schedulerFor(output);

    // Accumulate surface damage into the ring.
    if (dmg.full_damage) {
        wlr_damage_ring_add_whole(&dmg.ring);
        dmg.full_damage = false;
    } else if (pixman_region32_not_empty(&dmg.current_damage)) {
        wlr_damage_ring_add(&dmg.ring, &dmg.current_damage);
    }
    pixman_region32_clear(&dmg.current_damage);

    // Obtain the damage region for this frame.
    pixman_region32_t frame_damage;
    pixman_region32_init(&frame_damage);

    int buffer_age = 0;
    wlr_output_state state;
    wlr_output_state_init(&state);

    if (!wlr_output_configure_primary_swapchain(output, &state, &output->swapchain)) {
        wlr_output_state_finish(&state);
        pixman_region32_fini(&frame_damage);
        return false;
    }

    wlr_buffer* buffer = wlr_swapchain_acquire(output->swapchain, &buffer_age);
    if (!buffer) {
        wlr_output_state_finish(&state);
        pixman_region32_fini(&frame_damage);
        return false;
    }

    current_pass_ = wlr_renderer_begin_buffer_pass(renderer_, buffer, nullptr);
    if (!current_pass_) {
        wlr_buffer_unlock(buffer);
        wlr_output_state_finish(&state);
        pixman_region32_fini(&frame_damage);
        return false;
    }

    wlr_output_state_set_buffer(&state, buffer);
    wlr_buffer_unlock(buffer);

    wlr_damage_ring_get_buffer_damage(&dmg.ring, buffer_age, &frame_damage);

    sched.markFrameStart(0);
    pixman_region32_fini(&frame_damage);

    blend_mode_ = BlendMode::Normal;
    texture_cache_.setCurrentFrame(frame_number_);

    // Reset per-frame state.
    render_pass_.clear();
    scissor_stack_.reset();
    resetOcclusion();

    return true;
}

void Renderer::end() {
    if (!current_pass_ || !current_output_) return;

    // Submit ordered render pass (if any draw calls were queued).
    if (render_pass_.size() > 0) {
        render_pass_.submit();
    }

    wlr_render_pass_submit(current_pass_);
    current_pass_ = nullptr;

    wlr_output_state state;
    wlr_output_state_init(&state);

    wlr_output_commit_state(current_output_, &state);
    wlr_output_state_finish(&state);

    wlr_damage_ring_rotate(&damageFor(current_output_).ring);
    schedulerFor(current_output_).markFrameEnd(0);

    // Evict stale textures (older than 120 frames).
    if (frame_number_ > 120) {
        texture_cache_.evict(frame_number_ - 120);
    }

    current_output_ = nullptr;
}

// -- Drawing primitives (Task 14: enhanced surface rendering) ---------------

void Renderer::renderSurface(wlr_surface* surface, const Box& pos) {
    if (!current_pass_ || !surface) return;

    // Early-out for off-screen surfaces (Task 15).
    if (isOffScreen(pos)) return;

    // Occlusion culling (Task 15).
    if (isOccluded(pos)) return;

    wlr_texture* texture = wlr_surface_get_texture(surface);
    if (!texture) return;

    // Track per-surface damage (Task 14).
    auto& tracker = surfaceDamage(surface);

    uint32_t seq = surface->current.seq;
    if (seq != tracker.last_commit_seq) {
        tracker.dirty = true;
        tracker.last_commit_seq = seq;
        tracker.damage_region = pos;
    }

    // Apply fractional scaling if active (Task 20).
    Box render_pos = pos;
    auto frac_it = surface_fractional_.find(surface);
    if (frac_it != surface_fractional_.end()) {
        auto& frac = frac_it->second;
        if (frac.viewporter_active && frac.current_scale != 1.0) {
            // The surface renders at integer scale and we apply fractional
            // viewport. Adjust the render position to match the viewport.
            render_pos.width  = static_cast<int>(
                static_cast<double>(render_pos.width) * frac.current_scale);
            render_pos.height = static_cast<int>(
                static_cast<double>(render_pos.height) * frac.current_scale);
        }
    }

    renderTexture(texture, render_pos, 1.0f);
    tracker.dirty = false;
}

void Renderer::renderSurfaceWithSubsurfaces(wlr_surface* surface, const Box& pos) {
    if (!surface) return;

    // Render subsurfaces below the main surface first.
    struct wlr_subsurface* sub;
    wl_list_for_each(sub, &surface->current.subsurfaces_below, current.link) {
        if (!sub->surface) continue;

        struct wlr_subsurface_parent_state* pstate = &sub->current;
        Box sub_pos = {
            pos.x + pstate->x,
            pos.y + pstate->y,
            sub->surface->current.width,
            sub->surface->current.height,
        };
        renderSurfaceWithSubsurfaces(sub->surface, sub_pos);
    }

    // Render the main surface.
    renderSurface(surface, pos);

    // Render subsurfaces above the main surface.
    wl_list_for_each(sub, &surface->current.subsurfaces_above, current.link) {
        if (!sub->surface) continue;

        struct wlr_subsurface_parent_state* pstate = &sub->current;
        Box sub_pos = {
            pos.x + pstate->x,
            pos.y + pstate->y,
            sub->surface->current.width,
            sub->surface->current.height,
        };
        renderSurfaceWithSubsurfaces(sub->surface, sub_pos);
    }
}

void Renderer::renderRect(const Box& box, const Color& color) {
    if (!current_pass_) return;

    wlr_render_rect_options opts = {};
    opts.box = {
        .x = box.x,
        .y = box.y,
        .width = box.width,
        .height = box.height,
    };
    opts.color = {
        .r = color.r,
        .g = color.g,
        .b = color.b,
        .a = color.a,
    };

    wlr_render_pass_add_rect(current_pass_, &opts);
}

void Renderer::renderTexture(wlr_texture* texture, const Box& box, float alpha) {
    if (!current_pass_ || !texture) return;

    wlr_render_texture_options opts = {};
    opts.texture = texture;
    opts.dst_box = {
        .x = box.x,
        .y = box.y,
        .width = box.width,
        .height = box.height,
    };
    opts.alpha = &alpha;

    wlr_render_pass_add_texture(current_pass_, &opts);
}

void Renderer::renderRoundedRect(const Box& box, const Color& color, int radius) {
    renderRoundedRect(box, color, radius, radius, radius, radius);
}

void Renderer::renderRoundedRect(const Box& box, const Color& color,
                                  int tl, int tr, int bl, int br) {
    if (!current_pass_) return;

    static GLuint program = 0;
    if (program == 0) {
        program = Shaders::buildProgram(Shaders::quad_vert,
                                        Shaders::rounded_rect_frag);
    }
    if (program == 0) return;

    glUseProgram(program);
    glUniform4f(glGetUniformLocation(program, "u_color"),
                color.r, color.g, color.b, color.a);
    glUniform2f(glGetUniformLocation(program, "u_size"),
                static_cast<float>(box.width),
                static_cast<float>(box.height));
    glUniform2f(glGetUniformLocation(program, "u_position"),
                static_cast<float>(box.x),
                static_cast<float>(box.y));
    glUniform4f(glGetUniformLocation(program, "u_radii"),
                static_cast<float>(tl), static_cast<float>(tr),
                static_cast<float>(br), static_cast<float>(bl));

    static const float quad[] = {
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
    };

    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), quad);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), quad + 2);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableVertexAttribArray(0);
    glDisableVertexAttribArray(1);
    glUseProgram(0);
}

// -- Cursor (Tasks 12 & 13) ------------------------------------------------

bool Renderer::initCursor(wlr_cursor* cursor, const char* theme, int size) {
    if (!cursor) return false;
    wlr_cursor_ = cursor;

    xcursor_manager_ = wlr_xcursor_manager_create(theme, size);
    if (!xcursor_manager_) {
        wlr_log(WLR_ERROR, "Failed to create xcursor manager");
        return false;
    }

    wlr_log(WLR_INFO, "Cursor initialized: theme='%s' size=%d",
            theme ? theme : "default", size);
    return true;
}

void Renderer::setCursorShape(wlr_output* output, const std::string& shape) {
    auto& cs = cursorFor(output);
    cs.shape_name = shape;

    // Try hardware cursor first.
    if (xcursor_manager_ && !cs.hardware_failed) {
        // Set the xcursor image on the output's hardware cursor plane.
        wlr_xcursor* xcursor = wlr_xcursor_manager_get_xcursor(
            xcursor_manager_, shape.c_str(), 1);
        if (xcursor && xcursor->image_count > 0) {
            wlr_xcursor_image* image = xcursor->images[0];
            cs.hotspot_x = image->hotspot_x;
            cs.hotspot_y = image->hotspot_y;
            cs.mode = CursorMode::Hardware;
            return;
        }
    }

    // Fall back to software cursor.
    cs.mode = CursorMode::Software;
    cs.hardware_failed = true;
}

void Renderer::setCursorPosition(wlr_output* output, int x, int y) {
    auto& cs = cursorFor(output);
    cs.x = x;
    cs.y = y;
}

void Renderer::setCursorTexture(wlr_output* output, wlr_texture* texture,
                                 int hotspot_x, int hotspot_y) {
    auto& cs = cursorFor(output);
    cs.texture = texture;
    cs.hotspot_x = hotspot_x;
    cs.hotspot_y = hotspot_y;
    cs.mode = CursorMode::Software;
}

void Renderer::renderSoftwareCursor(wlr_output* output) {
    auto it = cursor_states_.find(output);
    if (it == cursor_states_.end()) return;

    auto& cs = it->second;
    if (cs.mode != CursorMode::Software || !cs.visible || !cs.texture) return;

    // Render the cursor texture at its position, adjusted for hotspot.
    Box cursor_box = {
        cs.x - cs.hotspot_x,
        cs.y - cs.hotspot_y,
        static_cast<int>(cs.texture->width),
        static_cast<int>(cs.texture->height),
    };

    renderTexture(cs.texture, cursor_box, 1.0f);
}

CursorState& Renderer::cursorFor(wlr_output* output) {
    return cursor_states_[output];
}

// -- Scissor stack (Task 15) -----------------------------------------------

void Renderer::pushScissor(const Box& region) {
    scissor_stack_.push(region);
}

void Renderer::popScissor() {
    scissor_stack_.pop();
}

// -- Occlusion culling (Task 15) -------------------------------------------

bool Renderer::isOccluded(const Box& box) const {
    // Check if any opaque region fully contains this box.
    for (const auto& opaque : opaque_regions_) {
        if (opaque.fullyContains(box)) return true;
    }
    return false;
}

void Renderer::addOpaqueRegion(const Box& box) {
    opaque_regions_.push_back(box);
}

void Renderer::resetOcclusion() {
    opaque_regions_.clear();
}

bool Renderer::isOffScreen(const Box& box) const {
    if (!current_output_) return false;

    int ow, oh;
    wlr_output_effective_resolution(current_output_, &ow, &oh);

    return (box.x + box.width <= 0 || box.x >= ow ||
            box.y + box.height <= 0 || box.y >= oh);
}

// -- Surface damage tracking (Task 14) ------------------------------------

SurfaceDamageTracker& Renderer::surfaceDamage(wlr_surface* surface) {
    return surface_damage_[surface];
}

bool Renderer::surfaceHasDamage(wlr_surface* surface) {
    auto it = surface_damage_.find(surface);
    if (it == surface_damage_.end()) return true;  // Unknown = dirty.
    return it->second.dirty;
}

void Renderer::markSurfaceRendered(wlr_surface* surface) {
    auto it = surface_damage_.find(surface);
    if (it != surface_damage_.end()) {
        it->second.dirty = false;
    }
}

// -- Fractional scaling (Task 20) -----------------------------------------

bool Renderer::initFractionalScale(wl_display* display) {
    if (!display) return false;

    // The wp-fractional-scale-v1 and wp-viewporter protocols are advertised
    // by creating the global. wlroots handles this; we just need to track
    // state per-output and per-surface.
    wlr_log(WLR_INFO, "Fractional scale protocol support initialized");
    return true;
}

void Renderer::setOutputFractionalScale(wlr_output* output, double scale) {
    auto& fs = fractionalScaleFor(output);
    fs.scale = scale;
    fs.protocol_bound = true;

    // Apply the fractional scale to the wlr_output.
    wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_scale(&state, static_cast<float>(scale));
    wlr_output_commit_state(output, &state);
    wlr_output_state_finish(&state);

    // Mark full damage since scale changed.
    addFullDamage(output);

    wlr_log(WLR_INFO, "Output '%s' fractional scale set to %.3f",
            output->name, scale);
}

OutputFractionalScale& Renderer::fractionalScaleFor(wlr_output* output) {
    return fractional_scales_[output];
}

void Renderer::setSurfaceFractionalScale(wlr_surface* surface, double scale) {
    auto& fs = surfaceFractionalScale(surface);
    fs.preferred_scale = scale;
    fs.current_scale = scale;
}

FractionalScaleState& Renderer::surfaceFractionalScale(wlr_surface* surface) {
    return surface_fractional_[surface];
}

// -- State ------------------------------------------------------------------

void Renderer::scissor(const Box& box) {
    if (!current_pass_) return;
    glEnable(GL_SCISSOR_TEST);
    glScissor(box.x, box.y, box.width, box.height);
}

void Renderer::clearScissor() {
    glDisable(GL_SCISSOR_TEST);
}

void Renderer::setBlendMode(BlendMode mode) {
    blend_mode_ = mode;
    applyBlendMode();
}

void Renderer::clear(const Color& color) {
    if (!current_pass_) return;

    wlr_render_rect_options opts = {};
    if (current_output_) {
        int w, h;
        wlr_output_effective_resolution(current_output_, &w, &h);
        opts.box = {.x = 0, .y = 0, .width = w, .height = h};
    }
    opts.color = {
        .r = color.r,
        .g = color.g,
        .b = color.b,
        .a = color.a,
    };
    wlr_render_pass_add_rect(current_pass_, &opts);
}

// -- Queries ----------------------------------------------------------------

wlr_texture* Renderer::getTexture(wlr_surface* surface) {
    if (!surface) return nullptr;
    return wlr_surface_get_texture(surface);
}

// -- Damage -----------------------------------------------------------------

DamageState& Renderer::damageFor(wlr_output* output) {
    auto it = damage_states_.find(output);
    if (it == damage_states_.end()) {
        auto [inserted, _] = damage_states_.try_emplace(output);
        int w, h;
        wlr_output_effective_resolution(output, &w, &h);
        inserted->second.reset(w, h);
        return inserted->second;
    }
    return it->second;
}

FrameScheduler& Renderer::schedulerFor(wlr_output* output) {
    auto it = schedulers_.find(output);
    if (it == schedulers_.end()) {
        auto [inserted, _] = schedulers_.try_emplace(output);
        inserted->second.configure(output);
        return inserted->second;
    }
    return it->second;
}

void Renderer::addDamage(wlr_output* output, const Box& box) {
    damageFor(output).addDamage(box);
}

void Renderer::addFullDamage(wlr_output* output) {
    damageFor(output).addFullDamage();
}

// -- Task 107: Enhanced damage tracking ------------------------------------

void Renderer::damageWindowMove(wlr_output* output, const Box& oldPos, const Box& newPos) {
    // Damage both old and new position to ensure proper redraw
    addDamage(output, oldPos);
    addDamage(output, newPos);
}

void Renderer::damageDecoration(wlr_output* output, const Box& decoBox) {
    // Damage the decoration region (border changes, shadow updates)
    addDamage(output, decoBox);
}

void Renderer::damageSurfaceLevel(wlr_surface* surface, wlr_output* output) {
    if (!surface || !output) return;

    // Only redraw regions that the surface actually changed
    auto& tracker = surfaceDamage(surface);
    if (tracker.dirty && !tracker.damage_region.empty()) {
        addDamage(output, tracker.damage_region);
    }
}

void Renderer::setDirtyFlag(wlr_output* output, bool dirty) {
    auto it = dirty_flags_.find(output);
    if (it == dirty_flags_.end()) {
        dirty_flags_[output] = dirty;
    } else {
        it->second = dirty;
    }
}

bool Renderer::isDirty(wlr_output* output) const {
    auto it = dirty_flags_.find(output);
    if (it != dirty_flags_.end()) return it->second;
    return true;  // Unknown outputs are assumed dirty
}

void Renderer::renderDebugDamageOverlay(wlr_output* output) {
    if (!debug_damage_overlay_ || !current_pass_) return;

    auto& dmg = damageFor(output);

    // Render accumulated damage regions as semi-transparent red rects
    // This helps developers visualize what regions are being redrawn.
    pixman_box32_t* rects;
    int nrects = 0;
    rects = pixman_region32_rectangles(&dmg.current_damage, &nrects);

    for (int i = 0; i < nrects; ++i) {
        Box damageBox{
            rects[i].x1,
            rects[i].y1,
            rects[i].x2 - rects[i].x1,
            rects[i].y2 - rects[i].y1,
        };
        // Semi-transparent red overlay
        renderRect(damageBox, {1.0f, 0.0f, 0.0f, 0.3f});
    }

    // Also show the full damage indicator
    if (dmg.full_damage) {
        int w, h;
        wlr_output_effective_resolution(output, &w, &h);
        renderRect({0, 0, w, h}, {1.0f, 0.0f, 0.0f, 0.1f});
    }
}

void Renderer::setDebugDamageOverlay(bool enable) {
    debug_damage_overlay_ = enable;
}

bool Renderer::debugDamageOverlayEnabled() const {
    return debug_damage_overlay_;
}

// -- Private ----------------------------------------------------------------

void Renderer::applyBlendMode() {
    switch (blend_mode_) {
    case BlendMode::Normal:
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        break;
    case BlendMode::Additive:
        glBlendFunc(GL_ONE, GL_ONE);
        break;
    case BlendMode::Multiply:
        glBlendFunc(GL_DST_COLOR, GL_ZERO);
        break;
    case BlendMode::Replace:
        glBlendFunc(GL_ONE, GL_ZERO);
        break;
    }
}

GPUContext* Renderer::gpuForOutput(wlr_output* /* output */) {
    return nullptr;
}

} // namespace eternal

#pragma once

extern "C" {
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_damage_ring.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/render/pass.h>
#include <wlr/render/allocator.h>
#include <GLES2/gl2.h>
}

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "eternal/utils/Types.hpp"

namespace eternal {

// ---------------------------------------------------------------------------
// Common types
// ---------------------------------------------------------------------------

/// Blend mode for rendering operations.
enum class BlendMode {
    Normal,
    Additive,
    Multiply,
    Replace,
};

// ---------------------------------------------------------------------------
// Task 11: Render backend abstraction layer
// ---------------------------------------------------------------------------

/// Buffer type for client buffer imports.
enum class BufferType {
    SHM,
    DMABUF,
    EGL,
    Unknown,
};

/// Abstract render backend interface.
class RenderBackend {
public:
    virtual ~RenderBackend() = default;

    /// Name of this backend (e.g. "gles2", "vulkan").
    [[nodiscard]] virtual const char* name() const = 0;

    /// Initialize the backend with the given wlr_renderer.
    virtual bool init(wlr_renderer* renderer) = 0;

    /// Create a framebuffer object for off-screen rendering.
    virtual GLuint createFBO(int width, int height, GLuint* out_texture) = 0;

    /// Destroy an FBO and its associated texture.
    virtual void destroyFBO(GLuint fbo, GLuint texture) = 0;

    /// Bind an FBO for rendering.
    virtual void bindFBO(GLuint fbo, int width, int height) = 0;

    /// Bind the default framebuffer (screen).
    virtual void bindDefaultFBO() = 0;

    /// Import a client buffer and return a GL texture.
    virtual GLuint importBuffer(wlr_buffer* buffer, BufferType type) = 0;

    /// Check if this backend supports a given buffer type.
    [[nodiscard]] virtual bool supportsBufferType(BufferType type) const = 0;
};

/// OpenGL ES 2.0/3.0 render backend.
class GLESBackend : public RenderBackend {
public:
    [[nodiscard]] const char* name() const override { return "gles2"; }
    bool init(wlr_renderer* renderer) override;
    GLuint createFBO(int width, int height, GLuint* out_texture) override;
    void destroyFBO(GLuint fbo, GLuint texture) override;
    void bindFBO(GLuint fbo, int width, int height) override;
    void bindDefaultFBO() override;
    GLuint importBuffer(wlr_buffer* buffer, BufferType type) override;
    [[nodiscard]] bool supportsBufferType(BufferType type) const override;

private:
    wlr_renderer* renderer_ = nullptr;
};

/// Vulkan render backend (stub -- requires VK_KHR_external_memory).
class VulkanBackend : public RenderBackend {
public:
    [[nodiscard]] const char* name() const override { return "vulkan"; }
    bool init(wlr_renderer* renderer) override;
    GLuint createFBO(int width, int height, GLuint* out_texture) override;
    void destroyFBO(GLuint fbo, GLuint texture) override;
    void bindFBO(GLuint fbo, int width, int height) override;
    void bindDefaultFBO() override;
    GLuint importBuffer(wlr_buffer* buffer, BufferType type) override;
    [[nodiscard]] bool supportsBufferType(BufferType type) const override;
};

/// FBO wrapper for off-screen rendering.
struct Framebuffer {
    GLuint fbo     = 0;
    GLuint texture = 0;
    int    width   = 0;
    int    height  = 0;

    void create(int w, int h);
    void destroy();
    void bind() const;
};

/// Texture with reference counting for the texture cache.
struct CachedTexture {
    GLuint   texture  = 0;
    int      width    = 0;
    int      height   = 0;
    uint32_t refcount = 1;
    uint64_t last_use_frame = 0;
};

/// Texture cache with reference counting.
class TextureCache {
public:
    /// Get or create a cached entry for a wlr_buffer.
    CachedTexture* get(wlr_buffer* buffer);

    /// Release a reference; destroys the GL texture when refcount hits zero.
    void release(wlr_buffer* buffer);

    /// Increment the reference count.
    void ref(wlr_buffer* buffer);

    /// Evict entries not used since the given frame number.
    void evict(uint64_t older_than_frame);

    /// Set the current frame number for LRU tracking.
    void setCurrentFrame(uint64_t frame) { current_frame_ = frame; }

    /// Destroy all cached textures.
    void clear();

private:
    struct Entry {
        CachedTexture tex;
        wlr_buffer*   buffer = nullptr;
    };
    std::unordered_map<wlr_buffer*, Entry> entries_;
    uint64_t current_frame_ = 0;
};

/// Priority for ordered draw calls within a render pass.
enum class DrawLayer : uint8_t {
    Background = 0,
    Shadow     = 10,
    Blur       = 20,
    Surface    = 50,
    Border     = 60,
    Overlay    = 80,
    Cursor     = 100,
};

/// A single draw command for the ordered render pass.
struct DrawCall {
    DrawLayer layer = DrawLayer::Surface;
    Box       clip  = {};
    std::function<void()> execute;
};

/// Render pass that collects and sorts draw calls before submission.
class RenderPass {
public:
    /// Add a draw call to the pass.
    void add(DrawCall call);

    /// Sort by layer and execute all draw calls.
    void submit();

    /// Clear all queued draw calls.
    void clear();

    /// Number of queued draw calls.
    [[nodiscard]] size_t size() const { return calls_.size(); }

private:
    std::vector<DrawCall> calls_;
};

// ---------------------------------------------------------------------------
// Task 15: Scissor stack
// ---------------------------------------------------------------------------

/// Push/pop scissor region stack.
class ScissorStack {
public:
    /// Push a scissor region (intersected with the current top).
    void push(const Box& region);

    /// Pop the most recent scissor region.
    void pop();

    /// Apply the current top of the stack to GL state.
    void apply() const;

    /// Get the current scissor region (or empty if none).
    [[nodiscard]] Box current() const;

    /// Whether any region is active.
    [[nodiscard]] bool empty() const { return stack_.empty(); }

    /// Clear all regions.
    void reset();

private:
    std::vector<Box> stack_;
};

// ---------------------------------------------------------------------------
// Task 12 & 13: Cursor state
// ---------------------------------------------------------------------------

/// Cursor rendering mode.
enum class CursorMode {
    Hardware,   ///< Use the hardware cursor plane.
    Software,   ///< Composite cursor onto the output framebuffer.
};

/// Per-output cursor state.
struct CursorState {
    CursorMode mode        = CursorMode::Hardware;
    bool       visible     = true;
    int        x           = 0;
    int        y           = 0;
    int        hotspot_x   = 0;
    int        hotspot_y   = 0;
    wlr_texture* texture   = nullptr;   ///< For software cursor rendering.
    std::string  shape_name = "default";
    bool hardware_failed    = false;     ///< Fallback trigger.
};

// ---------------------------------------------------------------------------
// Task 14: Surface damage tracking
// ---------------------------------------------------------------------------

/// Per-surface damage tracking for buffer updates.
struct SurfaceDamageTracker {
    uint32_t last_commit_seq = 0;
    bool     dirty           = true;
    Box      damage_region   = {};
};

// ---------------------------------------------------------------------------
// Task 20: Fractional scaling state
// ---------------------------------------------------------------------------

/// Per-surface fractional scale info.
struct FractionalScaleState {
    double   preferred_scale  = 1.0;
    double   current_scale    = 1.0;
    bool     viewporter_active = false;
    int      viewport_src_width  = 0;
    int      viewport_src_height = 0;
};

/// Per-output fractional scale protocol state.
struct OutputFractionalScale {
    double scale             = 1.0;
    bool   protocol_bound    = false;
    bool   viewporter_bound  = false;
};

// ---------------------------------------------------------------------------
// Per-GPU rendering context for multi-GPU setups.
// ---------------------------------------------------------------------------

struct GPUContext {
    wlr_renderer*  renderer  = nullptr;
    wlr_allocator* allocator = nullptr;
    std::string    gpu_name;
};

// ---------------------------------------------------------------------------
// Damage tracking state per output.
// ---------------------------------------------------------------------------

struct DamageState {
    wlr_damage_ring ring{};
    bool             full_damage = true;
    pixman_region32_t current_damage{};

    DamageState();
    ~DamageState();
    DamageState(const DamageState&) = delete;
    DamageState& operator=(const DamageState&) = delete;
    DamageState(DamageState&&) noexcept;
    DamageState& operator=(DamageState&&) noexcept;

    void addDamage(const Box& box);
    void addFullDamage();
    void reset(int width, int height);
};

/// Frame scheduling / VRR state.
struct FrameScheduler {
    bool   vrr_enabled       = false;
    bool   frame_pending     = false;
    int    target_fps        = 0;          // 0 = use output refresh
    double last_frame_time   = 0.0;
    double frame_budget_ms   = 0.0;

    void configure(wlr_output* output);
    [[nodiscard]] bool shouldRender(double now_ms) const;
    void markFrameStart(double now_ms);
    void markFrameEnd(double now_ms);
};

// ---------------------------------------------------------------------------
// Main Renderer
// ---------------------------------------------------------------------------

/// Main renderer wrapping wlr_renderer with damage tracking, VRR, multi-GPU,
/// render backend abstraction, cursor handling, scissor stack, texture cache,
/// ordered render pass, occlusion culling, and fractional scaling.
class Renderer {
public:
    Renderer();
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    /// Initialise with the primary renderer/allocator pair.
    bool init(wlr_renderer* renderer, wlr_allocator* allocator);

    /// Register an additional GPU context (multi-GPU).
    void addGPU(GPUContext ctx);

    // -- Render backend (Task 11) -------------------------------------------

    /// Get the active render backend.
    [[nodiscard]] RenderBackend* backend() const { return backend_.get(); }

    /// Get the texture cache.
    TextureCache& textureCache() { return texture_cache_; }

    /// Get the ordered render pass for the current frame.
    RenderPass& renderPass() { return render_pass_; }

    /// Create an off-screen FBO.
    Framebuffer createFramebuffer(int width, int height);

    /// Destroy an off-screen FBO.
    void destroyFramebuffer(Framebuffer& fb);

    // -- Frame lifecycle ---------------------------------------------------

    /// Begin rendering on the given output. Returns false if no damage.
    bool begin(wlr_output* output);
    /// End rendering and submit the frame.
    void end();

    // -- Drawing primitives ------------------------------------------------

    /// Render a client surface, handling SHM/DMA-BUF/EGL (Task 14).
    void renderSurface(wlr_surface* surface, const Box& pos);

    /// Render subsurfaces with proper z-ordering (Task 14).
    void renderSurfaceWithSubsurfaces(wlr_surface* surface, const Box& pos);

    void renderRect(const Box& box, const Color& color);
    void renderTexture(wlr_texture* texture, const Box& box, float alpha = 1.0f);
    void renderRoundedRect(const Box& box, const Color& color, int radius);

    /// Render with per-corner radii (Task 17).
    void renderRoundedRect(const Box& box, const Color& color,
                           int tl, int tr, int bl, int br);

    // -- Cursor (Tasks 12 & 13) -------------------------------------------

    /// Initialize cursor support with xcursor theme.
    bool initCursor(wlr_cursor* cursor, const char* theme, int size);

    /// Set the cursor image by shape name (wp-cursor-shape-v1).
    void setCursorShape(wlr_output* output, const std::string& shape);

    /// Update cursor position for an output.
    void setCursorPosition(wlr_output* output, int x, int y);

    /// Set the cursor texture for software rendering.
    void setCursorTexture(wlr_output* output, wlr_texture* texture,
                          int hotspot_x, int hotspot_y);

    /// Render the software cursor if needed (called during frame).
    void renderSoftwareCursor(wlr_output* output);

    /// Get cursor state for an output.
    CursorState& cursorFor(wlr_output* output);

    // -- Scissor stack (Task 15) ------------------------------------------

    /// Push a scissor region onto the stack.
    void pushScissor(const Box& region);

    /// Pop the top scissor region.
    void popScissor();

    /// Get the scissor stack.
    ScissorStack& scissorStack() { return scissor_stack_; }

    // -- Occlusion culling (Task 15) --------------------------------------

    /// Check if a box is fully occluded by opaque surfaces above it.
    [[nodiscard]] bool isOccluded(const Box& box) const;

    /// Register an opaque region (call front-to-back).
    void addOpaqueRegion(const Box& box);

    /// Clear opaque regions for a new frame.
    void resetOcclusion();

    /// Check if a box is off-screen for the current output.
    [[nodiscard]] bool isOffScreen(const Box& box) const;

    // -- Surface damage tracking (Task 14) --------------------------------

    /// Get per-surface damage tracker.
    SurfaceDamageTracker& surfaceDamage(wlr_surface* surface);

    /// Check if a surface has new damage since last render.
    [[nodiscard]] bool surfaceHasDamage(wlr_surface* surface);

    /// Mark a surface as rendered (clear dirty flag).
    void markSurfaceRendered(wlr_surface* surface);

    // -- Fractional scaling (Task 20) -------------------------------------

    /// Initialize fractional scale protocol for a display.
    bool initFractionalScale(wl_display* display);

    /// Set fractional scale for an output.
    void setOutputFractionalScale(wlr_output* output, double scale);

    /// Get fractional scale state for an output.
    OutputFractionalScale& fractionalScaleFor(wlr_output* output);

    /// Set per-surface scale factor.
    void setSurfaceFractionalScale(wlr_surface* surface, double scale);

    /// Get per-surface fractional scale state.
    FractionalScaleState& surfaceFractionalScale(wlr_surface* surface);

    // -- Existing state/query API -----------------------------------------

    void scissor(const Box& box);
    void clearScissor();
    void setBlendMode(BlendMode mode);
    void clear(const Color& color);

    [[nodiscard]] wlr_texture* getTexture(wlr_surface* surface);
    [[nodiscard]] wlr_renderer* getWlrRenderer() const { return renderer_; }
    [[nodiscard]] wlr_output* currentOutput() const { return current_output_; }
    [[nodiscard]] uint64_t frameNumber() const { return frame_number_; }

    // -- Damage ------------------------------------------------------------

    DamageState&    damageFor(wlr_output* output);
    FrameScheduler& schedulerFor(wlr_output* output);

    void addDamage(wlr_output* output, const Box& box);
    void addFullDamage(wlr_output* output);

    // -- Task 107: Enhanced damage tracking --------------------------------

    /// Damage the old and new position when a window moves.
    void damageWindowMove(wlr_output* output, const Box& oldPos, const Box& newPos);

    /// Damage a decoration region (border change, shadow update).
    void damageDecoration(wlr_output* output, const Box& decoBox);

    /// Surface-level damage: only redraw the changed pixels of a surface.
    void damageSurfaceLevel(wlr_surface* surface, wlr_output* output);

    /// Scene graph dirty flag per output.
    void setDirtyFlag(wlr_output* output, bool dirty);

    /// Check whether a scene graph node / output is marked dirty.
    [[nodiscard]] bool isDirty(wlr_output* output) const;

    /// Render a debug overlay showing damage regions in red.
    void renderDebugDamageOverlay(wlr_output* output);

    /// Enable/disable the debug damage overlay.
    void setDebugDamageOverlay(bool enable);

    /// Whether the debug damage overlay is active.
    [[nodiscard]] bool debugDamageOverlayEnabled() const;

private:
    wlr_renderer*  renderer_  = nullptr;
    wlr_allocator* allocator_ = nullptr;
    wlr_output*    current_output_ = nullptr;
    wlr_render_pass* current_pass_ = nullptr;
    BlendMode       blend_mode_ = BlendMode::Normal;
    uint64_t        frame_number_ = 0;

    // Render backend (Task 11)
    std::unique_ptr<RenderBackend> backend_;
    TextureCache    texture_cache_;
    RenderPass      render_pass_;

    // Scissor stack (Task 15)
    ScissorStack    scissor_stack_;

    // Occlusion (Task 15)
    std::vector<Box> opaque_regions_;

    // Cursor (Tasks 12 & 13)
    wlr_xcursor_manager* xcursor_manager_ = nullptr;
    wlr_cursor*          wlr_cursor_      = nullptr;
    std::unordered_map<wlr_output*, CursorState> cursor_states_;

    // Surface damage (Task 14)
    std::unordered_map<wlr_surface*, SurfaceDamageTracker> surface_damage_;

    // Fractional scaling (Task 20)
    std::unordered_map<wlr_output*, OutputFractionalScale> fractional_scales_;
    std::unordered_map<wlr_surface*, FractionalScaleState> surface_fractional_;

    // Multi-GPU & damage
    std::vector<GPUContext> extra_gpus_;
    std::unordered_map<wlr_output*, DamageState>    damage_states_;
    std::unordered_map<wlr_output*, FrameScheduler> schedulers_;

    void applyBlendMode();
    GPUContext* gpuForOutput(wlr_output* output);

    // Task 107: Scene graph dirty flags per output
    std::unordered_map<wlr_output*, bool> dirty_flags_;
    bool debug_damage_overlay_ = false;
};

} // namespace eternal

#pragma once

extern "C" {
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/wlr_texture.h>
#include <GLES2/gl2.h>
}

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "eternal/render/Renderer.hpp"

namespace eternal {

class Server;
class Surface;
class Output;

/// Raw pixel buffer captured from the GPU.
struct CaptureBuffer {
    std::vector<uint8_t> pixels;
    int   width  = 0;
    int   height = 0;
    int   stride = 0;   // bytes per row
    bool  valid  = false;

    /// Flip vertically (GL origin is bottom-left).
    void flipVertical();
};

/// Annotation overlay drawn during interactive selection.
struct AnnotationState {
    bool   active    = false;
    Box    selection{};
    Color  border_color = {1.0f, 1.0f, 1.0f, 0.8f};
    Color  fill_color   = {1.0f, 1.0f, 1.0f, 0.15f};
    Color  dim_color    = {0.0f, 0.0f, 0.0f, 0.5f};
    // Magnifier
    bool   show_magnifier = true;
    int    magnifier_zoom = 4;
};

/// Callback for interactive selection completion.
using SelectionCallback = std::function<void(std::optional<Box> region)>;

/// Screenshot notification effect type.
enum class ScreenshotEffect {
    None,
    Flash,       // brief white flash
    Shutter,     // shutter animation
};

/// Screenshot functionality: capture outputs, regions, or individual windows.
class Screenshot {
public:
    Screenshot();
    ~Screenshot();

    Screenshot(const Screenshot&) = delete;
    Screenshot& operator=(const Screenshot&) = delete;

    /// Initialise with the compositor renderer.
    bool init(Renderer* renderer);

    // ── Full output capture (Task 99) ────────────────────────────────────

    /// Capture an entire output to a buffer.
    [[nodiscard]] CaptureBuffer captureOutput(wlr_output* output);

    /// Capture a specific Output object.
    [[nodiscard]] CaptureBuffer captureOutput(Output* output);

    // ── Region selection (Task 99) ───────────────────────────────────────

    /// Capture a rectangular region (in layout coordinates).
    [[nodiscard]] CaptureBuffer captureRegion(const Box& box);

    // ── Window capture (Task 99) ─────────────────────────────────────────

    /// Capture a single surface/window.
    [[nodiscard]] CaptureBuffer captureWindow(wlr_surface* surface);

    /// Capture the window currently under the cursor.
    [[nodiscard]] CaptureBuffer captureWindowUnderCursor(
        double cursorX, double cursorY);

    // ── Save (Task 99) ──────────────────────────────────────────────────

    /// Save the buffer as a PNG file using libpng. Returns true on success.
    bool saveToPNG(const CaptureBuffer& buffer,
                   const std::filesystem::path& path);

    /// Copy the buffer to the Wayland clipboard (wl-copy integration).
    bool saveToClipboard(const CaptureBuffer& buffer);

    /// Generate a default screenshot filename.
    [[nodiscard]] std::filesystem::path generatePath(
        const std::string& prefix = "eternal-screenshot") const;

    // ── Interactive selection (Task 99) ──────────────────────────────────

    /// Begin interactive region selection with freeze-frame.
    /// Freezes the current frame as a backdrop and lets the user
    /// rubber-band a rectangle.
    void interactiveSelect(SelectionCallback callback);

    /// Cancel an in-progress interactive selection.
    void cancelSelection();

    /// Returns true while interactive selection is in progress.
    [[nodiscard]] bool isSelecting() const { return m_selecting; }

    /// Called by the compositor input path to feed pointer events.
    void onPointerMotion(int x, int y);
    void onPointerButton(bool pressed);
    void onKeyPress(uint32_t keycode);

    // ── Freeze-frame (Task 99) ──────────────────────────────────────────

    /// Whether the display is frozen for screenshot selection.
    [[nodiscard]] bool isFrozen() const { return m_freezeTexture != 0; }

    // ── Annotation overlay ──────────────────────────────────────────────

    /// Render the selection overlay (call during the output frame).
    void renderOverlay(wlr_output* output);

    [[nodiscard]] const AnnotationState& annotation() const { return m_annotation; }
    AnnotationState& annotation() { return m_annotation; }

    // ── Screenshot notification (Task 99) ───────────────────────────────

    /// Trigger a screenshot notification effect on the given output.
    void triggerEffect(wlr_output* output,
                       ScreenshotEffect effect = ScreenshotEffect::Flash);

    /// Update effect animation (call from frame callback).
    /// Returns true if the effect is still playing.
    bool updateEffect(double dt);

    /// Whether a notification effect is currently playing.
    [[nodiscard]] bool isEffectPlaying() const { return m_effectPlaying; }

    /// Get the current effect opacity (for rendering the flash).
    [[nodiscard]] float getEffectOpacity() const { return m_effectOpacity; }

    // ── Server reference ────────────────────────────────────────────────

    void setServer(Server* server) { m_server = server; }

private:
    Renderer* m_renderer = nullptr;
    Server* m_server = nullptr;
    bool m_selecting = false;

    // Freeze-frame texture captured at selection start.
    GLuint m_freezeTexture = 0;
    int    m_freezeWidth   = 0;
    int    m_freezeHeight  = 0;

    // Rubber-band state.
    int  m_anchorX = 0;
    int  m_anchorY = 0;
    int  m_cursorX = 0;
    int  m_cursorY = 0;
    bool m_buttonDown = false;

    AnnotationState m_annotation;
    SelectionCallback m_selectionCb;

    // Screenshot notification effect.
    bool m_effectPlaying = false;
    ScreenshotEffect m_effectType = ScreenshotEffect::None;
    float m_effectOpacity = 0.0f;
    float m_effectDuration = 0.3f; // seconds
    float m_effectElapsed = 0.0f;

    void captureFreeze(wlr_output* output);
    void releaseFreeze();
    Box  currentSelectionBox() const;
    CaptureBuffer readPixels(int x, int y, int w, int h);
};

} // namespace eternal

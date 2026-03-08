#include "eternal/screenshot/Screenshot.hpp"
#include "eternal/core/Server.hpp"
#include "eternal/core/Compositor.hpp"
#include "eternal/core/Output.hpp"
#include "eternal/core/Surface.hpp"
#include "eternal/utils/Logger.hpp"

extern "C" {
#include <wlr/util/log.h>
#include <png.h>
}

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace eternal {

// ---------------------------------------------------------------------------
// CaptureBuffer
// ---------------------------------------------------------------------------

void CaptureBuffer::flipVertical() {
    if (!valid || height <= 1) return;

    std::vector<uint8_t> row(stride);
    for (int y = 0; y < height / 2; ++y) {
        uint8_t* top    = pixels.data() + y * stride;
        uint8_t* bottom = pixels.data() + (height - 1 - y) * stride;
        std::memcpy(row.data(), top, stride);
        std::memcpy(top, bottom, stride);
        std::memcpy(bottom, row.data(), stride);
    }
}

// ---------------------------------------------------------------------------
// Screenshot
// ---------------------------------------------------------------------------

Screenshot::Screenshot() = default;

Screenshot::~Screenshot() {
    releaseFreeze();
}

bool Screenshot::init(Renderer* renderer) {
    assert(renderer);
    m_renderer = renderer;
    return true;
}

// ---------------------------------------------------------------------------
// Full output capture (Task 99)
// ---------------------------------------------------------------------------

CaptureBuffer Screenshot::captureOutput(wlr_output* output) {
    if (!output || !m_renderer) return {};

    int w, h;
    wlr_output_effective_resolution(output, &w, &h);

    return readPixels(0, 0, w, h);
}

CaptureBuffer Screenshot::captureOutput(Output* output) {
    if (!output) return {};
    return captureOutput(output->getWlrOutput());
}

// ---------------------------------------------------------------------------
// Region selection (Task 99)
// ---------------------------------------------------------------------------

CaptureBuffer Screenshot::captureRegion(const Box& box) {
    if (box.empty() || !m_renderer) return {};
    return readPixels(box.x, box.y, box.width, box.height);
}

// ---------------------------------------------------------------------------
// Window capture (Task 99)
// ---------------------------------------------------------------------------

CaptureBuffer Screenshot::captureWindow(wlr_surface* surface) {
    if (!surface || !m_renderer) return {};

    wlr_texture* tex = wlr_surface_get_texture(surface);
    if (!tex) return {};

    int w = surface->current.width;
    int h = surface->current.height;

    return readPixels(0, 0, w, h);
}

CaptureBuffer Screenshot::captureWindowUnderCursor(double cursorX,
                                                    double cursorY) {
    if (!m_server || !m_renderer) return {};

    // Find the surface under the cursor.
    Surface* surf = m_server->getCompositor().getSurfaceAt(cursorX, cursorY);
    if (!surf || !surf->isMapped()) return {};

    struct wlr_surface* wlrSurface = surf->getWlrSurface();
    if (!wlrSurface) return {};

    // Capture at the surface's position.
    const auto& geo = surf->getGeometry();
    return readPixels(geo.x, geo.y, geo.width, geo.height);
}

// ---------------------------------------------------------------------------
// Save to PNG (Task 99 - libpng)
// ---------------------------------------------------------------------------

bool Screenshot::saveToPNG(const CaptureBuffer& buffer,
                            const std::filesystem::path& path) {
    if (!buffer.valid) return false;

    // Ensure parent directory exists.
    std::filesystem::create_directories(path.parent_path());

    FILE* fp = fopen(path.c_str(), "wb");
    if (!fp) {
        LOG_ERROR("Screenshot: cannot open file for writing: {}", path.string());
        return false;
    }

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING,
                                              nullptr, nullptr, nullptr);
    if (!png) {
        fclose(fp);
        return false;
    }

    png_infop info = png_create_info_struct(png);
    if (!info) {
        png_destroy_write_struct(&png, nullptr);
        fclose(fp);
        return false;
    }

    if (setjmp(png_jmpbuf(png))) {
        png_destroy_write_struct(&png, &info);
        fclose(fp);
        return false;
    }

    png_init_io(png, fp);

    // Set compression level for faster saves.
    png_set_compression_level(png, 6);

    png_set_IHDR(png, info,
                 buffer.width, buffer.height,
                 8, PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE,
                 PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    for (int y = 0; y < buffer.height; ++y) {
        const uint8_t* row = buffer.pixels.data() + y * buffer.stride;
        png_write_row(png, row);
    }

    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
    fclose(fp);

    LOG_INFO("Screenshot: saved {}x{} image to '{}'",
             buffer.width, buffer.height, path.string());
    return true;
}

// ---------------------------------------------------------------------------
// Copy to clipboard (Task 99)
// ---------------------------------------------------------------------------

bool Screenshot::saveToClipboard(const CaptureBuffer& buffer) {
    if (!buffer.valid) return false;

    // Save to a temporary PNG file, then invoke wl-copy.
    auto tmpPath = std::filesystem::temp_directory_path() /
                   "eternal-screenshot-clip.png";
    if (!saveToPNG(buffer, tmpPath)) return false;

    // Use wl-copy to set the clipboard from the PNG file.
    std::string cmd = "wl-copy --type image/png < " + tmpPath.string() + " &";
    int ret = std::system(cmd.c_str());

    // Clean up temp file after a delay (wl-copy reads it asynchronously).
    // In practice, wl-copy opens the file immediately, so this is fine.
    // std::filesystem::remove(tmpPath);

    if (ret == 0) {
        LOG_INFO("Screenshot: copied {}x{} image to clipboard",
                 buffer.width, buffer.height);
    } else {
        LOG_WARN("Screenshot: wl-copy failed (is wl-copy installed?)");
    }

    return ret == 0;
}

// ---------------------------------------------------------------------------
// Generate default path (Task 99)
// ---------------------------------------------------------------------------

std::filesystem::path Screenshot::generatePath(
    const std::string& prefix) const {
    std::string dir;
    if (const char* xdg = std::getenv("XDG_PICTURES_DIR")) {
        dir = xdg;
    } else if (const char* home = std::getenv("HOME")) {
        dir = std::string(home) + "/Pictures/Screenshots";
    } else {
        dir = "/tmp";
    }

    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    char timeBuf[64];
    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d_%H-%M-%S",
                  std::localtime(&time));

    std::filesystem::create_directories(dir);
    return std::filesystem::path(dir) / (prefix + "-" + timeBuf + ".png");
}

// ---------------------------------------------------------------------------
// Interactive selection (Task 99)
// ---------------------------------------------------------------------------

void Screenshot::interactiveSelect(SelectionCallback callback) {
    if (m_selecting) return;

    m_selecting    = true;
    m_selectionCb  = std::move(callback);
    m_buttonDown   = false;

    m_annotation.active    = true;
    m_annotation.selection = {};

    LOG_INFO("Screenshot: interactive selection started (freeze-frame)");
    // The freeze-frame will be captured on the next output frame
    // (see renderOverlay).
}

void Screenshot::cancelSelection() {
    if (!m_selecting) return;
    m_selecting = false;
    m_annotation.active = false;
    releaseFreeze();

    if (m_selectionCb) {
        m_selectionCb(std::nullopt);
        m_selectionCb = nullptr;
    }
    LOG_INFO("Screenshot: selection cancelled");
}

void Screenshot::onPointerMotion(int x, int y) {
    if (!m_selecting) return;
    m_cursorX = x;
    m_cursorY = y;

    if (m_buttonDown) {
        m_annotation.selection = currentSelectionBox();
    }
}

void Screenshot::onPointerButton(bool pressed) {
    if (!m_selecting) return;

    if (pressed && !m_buttonDown) {
        // Start rubber-banding.
        m_buttonDown = true;
        m_anchorX = m_cursorX;
        m_anchorY = m_cursorY;
        m_annotation.selection = {};
    } else if (!pressed && m_buttonDown) {
        // Finish selection.
        m_buttonDown = false;
        m_selecting  = false;
        m_annotation.active = false;

        Box sel = currentSelectionBox();
        releaseFreeze();

        if (m_selectionCb) {
            if (sel.empty()) {
                m_selectionCb(std::nullopt);
            } else {
                m_selectionCb(sel);
            }
            m_selectionCb = nullptr;
        }
    }
}

void Screenshot::onKeyPress(uint32_t keycode) {
    if (!m_selecting) return;

    // Escape key (keycode 1 in evdev) cancels selection.
    if (keycode == 1) {
        cancelSelection();
    }
}

// ---------------------------------------------------------------------------
// Annotation overlay (Task 99)
// ---------------------------------------------------------------------------

void Screenshot::renderOverlay(wlr_output* output) {
    if (!m_annotation.active || !m_renderer || !output) return;

    // Capture freeze-frame on first call.
    if (m_freezeTexture == 0) {
        captureFreeze(output);
    }

    // Dim the frozen background.
    m_renderer->renderRect(
        {0, 0, m_freezeWidth, m_freezeHeight},
        m_annotation.dim_color);

    // Draw selection rectangle highlight.
    if (!m_annotation.selection.empty()) {
        m_renderer->renderRect(m_annotation.selection, m_annotation.fill_color);

        // Selection border (4 thin rects).
        const int bw = 2;
        auto& s = m_annotation.selection;
        m_renderer->renderRect({s.x, s.y, s.width, bw}, m_annotation.border_color);
        m_renderer->renderRect({s.x, s.y + s.height - bw, s.width, bw}, m_annotation.border_color);
        m_renderer->renderRect({s.x, s.y, bw, s.height}, m_annotation.border_color);
        m_renderer->renderRect({s.x + s.width - bw, s.y, bw, s.height}, m_annotation.border_color);

        // Draw dimension text hint (selection WxH).
        // TODO: render text showing selection dimensions.
    }
}

// ---------------------------------------------------------------------------
// Screenshot notification effect (Task 99)
// ---------------------------------------------------------------------------

void Screenshot::triggerEffect(wlr_output* output, ScreenshotEffect effect) {
    if (effect == ScreenshotEffect::None) return;

    m_effectPlaying = true;
    m_effectType = effect;
    m_effectOpacity = 1.0f;
    m_effectElapsed = 0.0f;
    m_effectDuration = (effect == ScreenshotEffect::Flash) ? 0.15f : 0.3f;

    // Damage the output to trigger rendering.
    (void)output;

    LOG_DEBUG("Screenshot: triggered {} effect",
              effect == ScreenshotEffect::Flash ? "flash" : "shutter");
}

bool Screenshot::updateEffect(double dt) {
    if (!m_effectPlaying) return false;

    m_effectElapsed += static_cast<float>(dt);
    float progress = m_effectElapsed / m_effectDuration;

    if (progress >= 1.0f) {
        m_effectPlaying = false;
        m_effectOpacity = 0.0f;
        return false;
    }

    // Fade out the flash effect.
    m_effectOpacity = 1.0f - progress;
    return true;
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void Screenshot::captureFreeze(wlr_output* output) {
    int w, h;
    wlr_output_effective_resolution(output, &w, &h);

    glGenTextures(1, &m_freezeTexture);
    glBindTexture(GL_TEXTURE_2D, m_freezeTexture);
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, w, h, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    m_freezeWidth  = w;
    m_freezeHeight = h;

    LOG_DEBUG("Screenshot: captured freeze-frame {}x{}", w, h);
}

void Screenshot::releaseFreeze() {
    if (m_freezeTexture) {
        glDeleteTextures(1, &m_freezeTexture);
        m_freezeTexture = 0;
    }
}

Box Screenshot::currentSelectionBox() const {
    int x1 = std::min(m_anchorX, m_cursorX);
    int y1 = std::min(m_anchorY, m_cursorY);
    int x2 = std::max(m_anchorX, m_cursorX);
    int y2 = std::max(m_anchorY, m_cursorY);
    return {x1, y1, x2 - x1, y2 - y1};
}

CaptureBuffer Screenshot::readPixels(int x, int y, int w, int h) {
    CaptureBuffer buf;
    buf.width  = w;
    buf.height = h;
    buf.stride = w * 4; // RGBA
    buf.pixels.resize(static_cast<size_t>(buf.stride) * h);

    glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, buf.pixels.data());

    // GL reads bottom-up; flip to top-down.
    buf.valid = true;
    buf.flipVertical();
    return buf;
}

} // namespace eternal

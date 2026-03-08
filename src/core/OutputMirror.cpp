#include "eternal/core/OutputMirror.hpp"
#include "eternal/core/Output.hpp"
#include "eternal/core/Server.hpp"
#include "eternal/utils/Logger.hpp"

extern "C" {
#include <wayland-server-core.h>
#include <wlr/types/wlr_output.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/pass.h>
#include <GLES2/gl2.h>
}

#include <algorithm>
#include <cstring>

namespace eternal {

// ---------------------------------------------------------------------------
// OutputMirror
// ---------------------------------------------------------------------------

OutputMirror::OutputMirror(Server& server, Output* source, Output* destination)
    : m_server(server), m_source(source), m_destination(destination) {}

OutputMirror::~OutputMirror() {
    stop();
}

bool OutputMirror::start() {
    if (!m_source || !m_destination) {
        LOG_ERROR("OutputMirror: cannot start without source and destination");
        return false;
    }

    if (m_source == m_destination) {
        LOG_ERROR("OutputMirror: source and destination must differ");
        return false;
    }

    // Set up a listener on the source output's frame event to trigger copy.
    m_sourceFrameListener.notify = [](struct wl_listener* listener, void* data) {
        OutputMirror* self = wl_container_of(listener, self,
                                              m_sourceFrameListener);
        (void)data;
        self->copyAndScale();
    };
    wl_signal_add(&m_source->getWlrOutput()->events.frame,
                  &m_sourceFrameListener);

    m_active = true;

    LOG_INFO("OutputMirror: mirroring '{}' -> '{}'",
             m_source->getName(), m_destination->getName());
    return true;
}

void OutputMirror::stop() {
    if (!m_active) return;

    wl_list_remove(&m_sourceFrameListener.link);

    if (m_copyTexture) {
        glDeleteTextures(1, &m_copyTexture);
        m_copyTexture = 0;
    }

    m_active = false;
    LOG_INFO("OutputMirror: stopped mirroring");
}

void OutputMirror::setSource(Output* source) {
    if (source == m_source) return;

    bool wasActive = m_active;
    if (wasActive) stop();

    m_source = source;

    if (wasActive && m_source) {
        start();
    }
}

void OutputMirror::renderMirror() {
    if (!m_active || !m_copyTexture) return;

    // Compute scale-to-fit placement on destination.
    const auto& dstBox = m_destination->getBox();
    int outX, outY, outW, outH;
    computeScaleToFit(m_copyWidth, m_copyHeight,
                      dstBox.width, dstBox.height,
                      outX, outY, outW, outH);

    // Render the copied texture onto the destination.
    // This would be done via the renderer's texture drawing API.
    // The destination output's render pass needs to draw our texture.
    // TODO: integrate with the actual render pass pipeline.
    // renderer->renderTexture(m_copyTexture, {outX, outY, outW, outH});

    // Render independent cursor if enabled.
    if (m_independentCursor) {
        // TODO: render cursor on the mirror output at the mapped position.
    }
}

void OutputMirror::copyAndScale() {
    if (!m_active || !m_source) return;

    auto* srcWlr = m_source->getWlrOutput();
    if (!srcWlr) return;

    int w = srcWlr->width;
    int h = srcWlr->height;

    // Allocate or resize the copy texture.
    if (m_copyWidth != w || m_copyHeight != h || m_copyTexture == 0) {
        if (m_copyTexture) {
            glDeleteTextures(1, &m_copyTexture);
        }
        glGenTextures(1, &m_copyTexture);
        glBindTexture(GL_TEXTURE_2D, m_copyTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);

        m_copyWidth = w;
        m_copyHeight = h;
    }

    // Copy the current framebuffer from the source output.
    glBindTexture(GL_TEXTURE_2D, m_copyTexture);
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Schedule a frame on the destination to render the mirror.
    m_destination->scheduleFrame();
}

void OutputMirror::computeScaleToFit(int srcW, int srcH,
                                      int dstW, int dstH,
                                      int& outX, int& outY,
                                      int& outW, int& outH) const {
    float srcAspect = static_cast<float>(srcW) / srcH;
    float dstAspect = static_cast<float>(dstW) / dstH;

    if (srcAspect > dstAspect) {
        // Source is wider: fit width, letterbox top/bottom.
        outW = dstW;
        outH = static_cast<int>(dstW / srcAspect);
        outX = 0;
        outY = (dstH - outH) / 2;
    } else {
        // Source is taller: fit height, pillarbox left/right.
        outH = dstH;
        outW = static_cast<int>(dstH * srcAspect);
        outX = (dstW - outW) / 2;
        outY = 0;
    }
}

// ---------------------------------------------------------------------------
// OutputMirrorManager
// ---------------------------------------------------------------------------

OutputMirrorManager::OutputMirrorManager(Server& server)
    : m_server(server) {}

OutputMirrorManager::~OutputMirrorManager() = default;

OutputMirror* OutputMirrorManager::createMirror(Output* source,
                                                 Output* destination) {
    // Check if a mirror already exists for this destination.
    if (findByDestination(destination)) {
        LOG_WARN("OutputMirrorManager: mirror already exists for '{}'",
                 destination->getName());
        return nullptr;
    }

    auto mirror = std::make_unique<OutputMirror>(m_server, source, destination);
    if (!mirror->start()) {
        return nullptr;
    }

    OutputMirror* ptr = mirror.get();
    m_mirrors.push_back(std::move(mirror));
    return ptr;
}

void OutputMirrorManager::removeMirror(Output* destination) {
    std::erase_if(m_mirrors, [destination](const auto& m) {
        return m->getDestination() == destination;
    });
}

void OutputMirrorManager::removeAllFor(Output* output) {
    std::erase_if(m_mirrors, [output](const auto& m) {
        return m->getSource() == output || m->getDestination() == output;
    });
}

OutputMirror* OutputMirrorManager::findByDestination(Output* destination) const {
    for (auto& m : m_mirrors) {
        if (m->getDestination() == destination) return m.get();
    }
    return nullptr;
}

std::vector<OutputMirror*> OutputMirrorManager::findBySource(Output* source) const {
    std::vector<OutputMirror*> result;
    for (auto& m : m_mirrors) {
        if (m->getSource() == source) result.push_back(m.get());
    }
    return result;
}

} // namespace eternal

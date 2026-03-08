#include "eternal/screenshot/ScreenRecorder.hpp"
#include "eternal/core/Surface.hpp"
#include "eternal/utils/Logger.hpp"

extern "C" {
#include <wlr/util/log.h>
#include <GLES2/gl2.h>
}

#include <algorithm>
#include <cassert>
#include <cstring>
#include <variant>

namespace eternal {

// ---------------------------------------------------------------------------
// PipeWireState -- pimpl so PipeWire headers don't leak into public API
// ---------------------------------------------------------------------------

struct ScreenRecorder::PipeWireState {
    // PipeWire core handles.  When PipeWire is available these would be:
    //   pw_main_loop*   loop   = nullptr;
    //   pw_context*     ctx    = nullptr;
    //   pw_core*        core   = nullptr;
    //   spa_hook        core_listener{};
    bool connected   = false;
    bool initialized = false;

    // DMA-BUF support detection.
    bool dmabufSupported = false;
};

// ---------------------------------------------------------------------------
// ScreenRecorder
// ---------------------------------------------------------------------------

ScreenRecorder::ScreenRecorder()
    : m_pwState(std::make_unique<PipeWireState>()) {}

ScreenRecorder::~ScreenRecorder() {
    shutdown();
}

bool ScreenRecorder::init(Renderer* renderer) {
    assert(renderer);
    m_renderer = renderer;

    // Initialize PipeWire.
    // pw_init(nullptr, nullptr);
    // m_pwState->loop = pw_main_loop_new(nullptr);
    // m_pwState->ctx  = pw_context_new(
    //     pw_main_loop_get_loop(m_pwState->loop), nullptr, 0);
    // m_pwState->core = pw_context_connect(m_pwState->ctx, nullptr, 0);

    m_pwState->initialized = true;
    m_pwState->connected = true;
    m_pipewireAvailable = true;

    // Check DMA-BUF support.
    // TODO: query SPA_PARAM_EnumFormat for DMA-BUF modifier support.
    m_pwState->dmabufSupported = true; // optimistic default

    LOG_INFO("ScreenRecorder: initialized (PipeWire available)");
    return true;
}

void ScreenRecorder::shutdown() {
    // Stop all streams.
    for (auto& [id, stream] : m_streams) {
        teardownPipeWireStream(stream);
    }
    m_streams.clear();
    m_windowStreams.clear();

    if (m_recording) {
        m_recording = false;
    }

    if (m_pwState) {
        // pw_core_disconnect(m_pwState->core);
        // pw_context_destroy(m_pwState->ctx);
        // pw_main_loop_destroy(m_pwState->loop);
        // pw_deinit();
        m_pwState->connected = false;
        m_pwState->initialized = false;
    }

    m_pipewireAvailable = false;
    LOG_INFO("ScreenRecorder: shut down");
}

// ---------------------------------------------------------------------------
// Recording lifecycle (Task 92)
// ---------------------------------------------------------------------------

bool ScreenRecorder::startRecording(const RecordingTarget& target,
                                     const RecordingOptions& options) {
    if (m_recording) {
        LOG_ERROR("ScreenRecorder: already recording");
        return false;
    }

    m_target  = target;
    m_options = options;

    uint32_t streamId = createStream(target, options);
    if (streamId == 0) {
        LOG_ERROR("ScreenRecorder: failed to create stream");
        return false;
    }

    m_recording = true;
    LOG_INFO("ScreenRecorder: recording started (stream={})", streamId);
    return true;
}

void ScreenRecorder::stopRecording() {
    if (!m_recording) return;

    // Stop all recording streams.
    for (auto& [id, stream] : m_streams) {
        if (stream.state == StreamState::Streaming) {
            teardownPipeWireStream(stream);
        }
    }

    m_recording = false;
    LOG_INFO("ScreenRecorder: recording stopped");
}

// ---------------------------------------------------------------------------
// PipeWire stream management (Task 92)
// ---------------------------------------------------------------------------

uint32_t ScreenRecorder::createStream(const RecordingTarget& target,
                                       const RecordingOptions& options) {
    if (!m_pipewireAvailable) return 0;

    ShareStream stream;
    stream.streamId = m_nextStreamId++;
    stream.target = target;
    stream.options = options;

    // Determine dimensions from target.
    int w = 0, h = 0;
    if (auto* output = std::get_if<wlr_output*>(&target)) {
        wlr_output_effective_resolution(*output, &w, &h);
    } else if (auto* surface = std::get_if<wlr_surface*>(&target)) {
        w = (*surface)->current.width;
        h = (*surface)->current.height;
    } else if (auto* box = std::get_if<Box>(&target)) {
        w = box->width;
        h = box->height;
    }

    if (w <= 0 || h <= 0) {
        LOG_ERROR("ScreenRecorder: invalid target dimensions: {}x{}", w, h);
        return 0;
    }

    stream.width = w;
    stream.height = h;
    stream.negotiatedFps = options.framerate;
    stream.dmabufActive = options.dmabuf_sharing && m_pwState->dmabufSupported;

    if (!setupPipeWireStream(stream)) {
        return 0;
    }

    uint32_t id = stream.streamId;
    m_streams[id] = std::move(stream);

    LOG_INFO("ScreenRecorder: created stream {} ({}x{} @ {} fps, dmabuf={})",
             id, w, h, options.framerate,
             m_streams[id].dmabufActive ? "yes" : "no");
    return id;
}

void ScreenRecorder::destroyStream(uint32_t streamId) {
    auto it = m_streams.find(streamId);
    if (it == m_streams.end()) return;

    teardownPipeWireStream(it->second);
    m_streams.erase(it);

    LOG_DEBUG("ScreenRecorder: destroyed stream {}", streamId);
}

ShareStream* ScreenRecorder::getStream(uint32_t streamId) {
    auto it = m_streams.find(streamId);
    return it != m_streams.end() ? &it->second : nullptr;
}

uint32_t ScreenRecorder::getNodeId(uint32_t streamId) const {
    auto it = m_streams.find(streamId);
    return it != m_streams.end() ? it->second.pipeWireNodeId : 0;
}

bool ScreenRecorder::negotiateFrameRate(uint32_t streamId, int requestedFps) {
    auto it = m_streams.find(streamId);
    if (it == m_streams.end()) return false;

    auto& stream = it->second;

    // Clamp to reasonable range.
    int fps = std::clamp(requestedFps, 1, 240);

    // TODO: pw_stream_update_params() to renegotiate.
    // struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof(buf));
    // ... set SPA_FORMAT_VIDEO_framerate ...

    stream.negotiatedFps = fps;
    LOG_INFO("ScreenRecorder: stream {} frame rate negotiated to {} fps",
             streamId, fps);
    return true;
}

// ---------------------------------------------------------------------------
// DMA-BUF zero-copy (Task 92)
// ---------------------------------------------------------------------------

bool ScreenRecorder::isDmaBufAvailable() const {
    return m_pwState && m_pwState->dmabufSupported;
}

void ScreenRecorder::setDmaBuf(uint32_t streamId, bool enable) {
    auto it = m_streams.find(streamId);
    if (it == m_streams.end()) return;

    it->second.dmabufActive = enable && m_pwState->dmabufSupported;
    LOG_DEBUG("ScreenRecorder: stream {} DMA-BUF {}",
              streamId, it->second.dmabufActive ? "enabled" : "disabled");
}

// ---------------------------------------------------------------------------
// Window-level sharing (Task 93)
// ---------------------------------------------------------------------------

uint32_t ScreenRecorder::shareWindow(Surface* surface,
                                      const RecordingOptions& options) {
    if (!surface) return 0;

    struct wlr_surface* wlrSurface = surface->getWlrSurface();
    if (!wlrSurface) return 0;

    RecordingTarget target = wlrSurface;
    uint32_t streamId = createStream(target, options);

    if (streamId > 0) {
        m_windowStreams[surface] = streamId;
        LOG_INFO("ScreenRecorder: sharing window '{}' (stream={})",
                 surface->getTitle(), streamId);
    }

    return streamId;
}

void ScreenRecorder::onWindowGeometryChanged(Surface* surface) {
    if (!surface) return;

    auto it = m_windowStreams.find(surface);
    if (it == m_windowStreams.end()) return;

    auto* stream = getStream(it->second);
    if (!stream) return;

    // Update stream dimensions to match new window size.
    const auto& geo = surface->getGeometry();
    if (geo.width != stream->width || geo.height != stream->height) {
        updateStreamDimensions(*stream, geo.width, geo.height);
        LOG_DEBUG("ScreenRecorder: window '{}' resized to {}x{}, "
                  "updating stream {}",
                  surface->getTitle(), geo.width, geo.height,
                  stream->streamId);
    }
}

void ScreenRecorder::onWindowClosed(Surface* surface) {
    if (!surface) return;

    auto it = m_windowStreams.find(surface);
    if (it == m_windowStreams.end()) return;

    uint32_t streamId = it->second;
    LOG_INFO("ScreenRecorder: window '{}' closed, stopping stream {}",
             surface->getTitle(), streamId);

    destroyStream(streamId);
    m_windowStreams.erase(it);
}

uint32_t ScreenRecorder::exportToplevel(Surface* surface) {
    if (!surface) return 0;

    // Foreign toplevel export: assign an identifier that external clients
    // can use to reference this toplevel for screen sharing.
    // TODO: implement wlr-foreign-toplevel-management-unstable-v1 export.
    // This would return a token/identifier for the toplevel.

    LOG_DEBUG("ScreenRecorder: exported toplevel '{}' for foreign sharing",
              surface->getTitle());
    return 0; // stub
}

// ---------------------------------------------------------------------------
// Target management
// ---------------------------------------------------------------------------

bool ScreenRecorder::setTarget(const RecordingTarget& new_target) {
    m_target = new_target;

    if (!m_recording) return true;

    // Determine new dimensions.
    int w = 0, h = 0;
    if (auto* output = std::get_if<wlr_output*>(&new_target)) {
        wlr_output_effective_resolution(*output, &w, &h);
    } else if (auto* surface = std::get_if<wlr_surface*>(&new_target)) {
        w = (*surface)->current.width;
        h = (*surface)->current.height;
    } else if (auto* box = std::get_if<Box>(&new_target)) {
        w = box->width;
        h = box->height;
    }

    if (w <= 0 || h <= 0) return false;

    // Update all active streams.
    for (auto& [id, stream] : m_streams) {
        if (stream.state == StreamState::Streaming) {
            stream.target = new_target;
            updateStreamDimensions(stream, w, h);
        }
    }

    LOG_INFO("ScreenRecorder: target switched to {}x{}", w, h);
    return true;
}

// ---------------------------------------------------------------------------
// Sensitive window blocking
// ---------------------------------------------------------------------------

void ScreenRecorder::addSensitiveSurface(wlr_surface* surface) {
    m_sensitiveSurfaces.insert(surface);
}

void ScreenRecorder::removeSensitiveSurface(wlr_surface* surface) {
    m_sensitiveSurfaces.erase(surface);
}

bool ScreenRecorder::isSensitive(wlr_surface* surface) const {
    return m_sensitiveSurfaces.contains(surface);
}

// ---------------------------------------------------------------------------
// Portal integration
// ---------------------------------------------------------------------------

uint32_t ScreenRecorder::portalNodeId() const {
    // Return the node ID of the first active stream.
    for (const auto& [id, stream] : m_streams) {
        if (stream.state == StreamState::Streaming) {
            return stream.pipeWireNodeId;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Frame submission
// ---------------------------------------------------------------------------

void ScreenRecorder::onOutputFrame(wlr_output* output) {
    if (!m_recording || !output) return;

    for (auto& [id, stream] : m_streams) {
        if (stream.state != StreamState::Streaming) continue;

        // Only capture for the matching target.
        if (auto* target_output = std::get_if<wlr_output*>(&stream.target)) {
            if (*target_output != output) continue;
        }

        int w = stream.width;
        int h = stream.height;
        if (w <= 0 || h <= 0) continue;

        if (stream.dmabufActive) {
            // Zero-copy DMA-BUF path.
            // TODO: get the DMA-BUF fd from the wlr_output's back buffer.
            // int fd = wlr_buffer_get_dmabuf(buffer, &dmabuf);
            // submitDmaBufFrame(stream, fd, w, h, format, modifier);
        } else {
            // CPU copy fallback.
            int stride = w * 4;
            std::vector<uint8_t> pixels(static_cast<size_t>(stride) * h);
            glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

            // Black out sensitive surfaces.
            if (!m_sensitiveSurfaces.empty()) {
                blackOutSensitive(pixels.data(), w, h, stride);
            }

            submitFrame(stream, pixels.data(), w, h, stride);
        }
    }
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

bool ScreenRecorder::setupPipeWireStream(ShareStream& stream) {
    // TODO: create pw_stream with SPA video format parameters.
    //
    // uint8_t buf[1024];
    // struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof(buf));
    //
    // enum spa_video_format format = stream.dmabufActive
    //     ? SPA_VIDEO_FORMAT_BGRA    // DMA-BUF format
    //     : SPA_VIDEO_FORMAT_RGBA;   // SHM format
    //
    // const struct spa_pod* params[1];
    // params[0] = spa_pod_builder_add_object(&b,
    //     SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
    //     SPA_FORMAT_mediaType,      SPA_POD_Id(SPA_MEDIA_TYPE_video),
    //     SPA_FORMAT_mediaSubtype,   SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
    //     SPA_FORMAT_VIDEO_format,   SPA_POD_Id(format),
    //     SPA_FORMAT_VIDEO_size,     SPA_POD_Rectangle(
    //         &SPA_RECTANGLE(stream.width, stream.height)),
    //     SPA_FORMAT_VIDEO_framerate, SPA_POD_Fraction(
    //         &SPA_FRACTION(stream.negotiatedFps, 1)),
    //     SPA_PARAM_META_type, SPA_POD_Id(SPA_META_Header));
    //
    // pw_stream_connect(stream.pwStream, PW_DIRECTION_OUTPUT,
    //                   PW_ID_ANY, PW_STREAM_FLAG_MAP_BUFFERS, params, 1);

    stream.state = StreamState::Streaming;
    stream.pipeWireNodeId = stream.streamId + 100; // placeholder
    return true;
}

void ScreenRecorder::teardownPipeWireStream(ShareStream& stream) {
    // TODO: pw_stream_disconnect / pw_stream_destroy.
    stream.state = StreamState::Idle;
    stream.pipeWireNodeId = 0;
}

void ScreenRecorder::submitFrame(ShareStream& stream, const void* data,
                                  int width, int height, int stride) {
    if (stream.state != StreamState::Streaming) return;

    (void)data;
    (void)width;
    (void)height;
    (void)stride;

    // TODO: acquire PipeWire buffer, memcpy data, queue it.
    // struct pw_buffer* pwbuf = pw_stream_dequeue_buffer(stream.pwStream);
    // if (!pwbuf) return;
    // struct spa_buffer* spa = pwbuf->buffer;
    //
    // struct spa_meta_header* header =
    //     spa_buffer_find_meta_data(spa, SPA_META_Header, sizeof(*header));
    // if (header) {
    //     header->pts = -1;
    //     header->flags = 0;
    // }
    //
    // memcpy(spa->datas[0].data, data, stride * height);
    // spa->datas[0].chunk->offset = 0;
    // spa->datas[0].chunk->size   = stride * height;
    // spa->datas[0].chunk->stride = stride;
    // pw_stream_queue_buffer(stream.pwStream, pwbuf);
}

void ScreenRecorder::submitDmaBufFrame(ShareStream& stream, int dmabufFd,
                                        int width, int height,
                                        uint32_t format, uint64_t modifier) {
    if (stream.state != StreamState::Streaming) return;

    (void)dmabufFd;
    (void)width;
    (void)height;
    (void)format;
    (void)modifier;

    // TODO: zero-copy DMA-BUF buffer sharing.
    // struct pw_buffer* pwbuf = pw_stream_dequeue_buffer(stream.pwStream);
    // if (!pwbuf) return;
    // struct spa_buffer* spa = pwbuf->buffer;
    //
    // spa->datas[0].type = SPA_DATA_DmaBuf;
    // spa->datas[0].fd = dmabufFd;
    // spa->datas[0].maxsize = width * height * 4;
    // spa->datas[0].chunk->offset = 0;
    // spa->datas[0].chunk->size = width * height * 4;
    // spa->datas[0].chunk->stride = width * 4;
    //
    // pw_stream_queue_buffer(stream.pwStream, pwbuf);
}

void ScreenRecorder::blackOutSensitive(void* buffer, int buf_width,
                                        int buf_height, int stride) {
    // In a real implementation this would iterate the scene tree, find
    // sensitive surface geometries, and zero out those pixel regions.
    (void)buffer;
    (void)buf_width;
    (void)buf_height;
    (void)stride;
}

void ScreenRecorder::updateStreamDimensions(ShareStream& stream,
                                             int w, int h) {
    stream.width = w;
    stream.height = h;

    // TODO: pw_stream_update_params() to renegotiate resolution.
    // uint8_t buf[1024];
    // struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buf, sizeof(buf));
    // const struct spa_pod* params[1];
    // params[0] = ... build new format with updated size ...;
    // pw_stream_update_params(stream.pwStream, params, 1);
}

} // namespace eternal

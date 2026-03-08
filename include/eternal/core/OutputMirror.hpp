#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

extern "C" {
#include <wayland-server-core.h>

struct wlr_output;
struct wlr_texture;
}

namespace eternal {

class Server;
class Output;

/// Mirrors the content of a source output onto a destination output,
/// handling resolution/scale differences by scaling to fit.
class OutputMirror {
public:
    OutputMirror(Server& server, Output* source, Output* destination);
    ~OutputMirror();

    OutputMirror(const OutputMirror&) = delete;
    OutputMirror& operator=(const OutputMirror&) = delete;

    /// Start mirroring.
    bool start();

    /// Stop mirroring and restore independent rendering on destination.
    void stop();

    /// Whether mirroring is currently active.
    [[nodiscard]] bool isActive() const { return m_active; }

    /// The source output being mirrored.
    [[nodiscard]] Output* getSource() const { return m_source; }

    /// The destination output receiving the mirror.
    [[nodiscard]] Output* getDestination() const { return m_destination; }

    /// Change the source output.
    void setSource(Output* source);

    /// Called each frame on the destination output to render the mirror.
    void renderMirror();

    /// Whether to render an independent cursor on the mirror output.
    void setIndependentCursor(bool enable) { m_independentCursor = enable; }
    [[nodiscard]] bool hasIndependentCursor() const { return m_independentCursor; }

private:
    /// Copy the source framebuffer content and scale to destination.
    void copyAndScale();

    /// Compute the scale-to-fit transform.
    void computeScaleToFit(int srcW, int srcH, int dstW, int dstH,
                           int& outX, int& outY, int& outW, int& outH) const;

    Server& m_server;
    Output* m_source = nullptr;
    Output* m_destination = nullptr;
    bool m_active = false;
    bool m_independentCursor = true;

    // Frame listener on source to trigger copy.
    struct wl_listener m_sourceFrameListener{};

    // Intermediate texture for frame copy.
    uint32_t m_copyTexture = 0;
    int m_copyWidth = 0;
    int m_copyHeight = 0;
};

/// Manages all active mirror relationships.
class OutputMirrorManager {
public:
    explicit OutputMirrorManager(Server& server);
    ~OutputMirrorManager();

    /// Create a mirror from source to destination.
    OutputMirror* createMirror(Output* source, Output* destination);

    /// Remove a mirror by destination output.
    void removeMirror(Output* destination);

    /// Remove all mirrors involving an output (source or destination).
    void removeAllFor(Output* output);

    /// Find a mirror by destination output.
    [[nodiscard]] OutputMirror* findByDestination(Output* destination) const;

    /// Find all mirrors sourced from the given output.
    [[nodiscard]] std::vector<OutputMirror*> findBySource(Output* source) const;

    /// All active mirrors.
    [[nodiscard]] const std::vector<std::unique_ptr<OutputMirror>>& getMirrors() const {
        return m_mirrors;
    }

private:
    Server& m_server;
    std::vector<std::unique_ptr<OutputMirror>> m_mirrors;
};

} // namespace eternal

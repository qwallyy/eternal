#include "eternal/protocols/Accessibility.hpp"
#include "eternal/core/Server.hpp"
#include "eternal/core/Compositor.hpp"
#include "eternal/core/Output.hpp"
#include "eternal/core/Surface.hpp"
#include "eternal/utils/Logger.hpp"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace eternal {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

Accessibility::Accessibility(Server& server)
    : m_server(server) {}

Accessibility::~Accessibility() {
    shutdown();
}

bool Accessibility::init() {
    m_initialized = true;
    LOG_INFO("Accessibility: initialized");

    // Initialize AT-SPI2 if configured.
    if (m_config.atSpiEnabled) {
        initATSpi();
    }

    return true;
}

void Accessibility::shutdown() {
    if (!m_initialized) return;

    stopSpeaking();
    m_magnifier.active = false;
    m_initialized = false;

    LOG_INFO("Accessibility: shut down");
}

void Accessibility::applyConfig(const AccessibilityConfig& config) {
    bool wasReducedMotion = m_config.reducedMotionEnabled;
    bool wasLargeCursor = m_config.largeCursorEnabled;

    m_config = config;

    // Apply cursor size change.
    if (config.largeCursorEnabled != wasLargeCursor) {
        LOG_INFO("Accessibility: large cursor {}",
                 config.largeCursorEnabled ? "enabled" : "disabled");
        // TODO: update xcursor size through the renderer.
    }

    // Apply reduced motion.
    if (config.reducedMotionEnabled != wasReducedMotion) {
        LOG_INFO("Accessibility: reduced motion {}",
                 config.reducedMotionEnabled ? "enabled" : "disabled");
        // TODO: notify AnimationEngine to disable/enable animations.
    }

    // Apply magnifier state.
    m_magnifier.zoom = config.magnifierZoom;
    m_magnifier.followCursor = config.magnifierFollowCursor;
    if (!config.magnifierEnabled) {
        m_magnifier.active = false;
    }

    LOG_INFO("Accessibility: configuration applied");
}

// ---------------------------------------------------------------------------
// Text-to-speech (Task 100)
// ---------------------------------------------------------------------------

void Accessibility::setTTSEnabled(bool enabled) {
    m_config.ttsEnabled = enabled;
    LOG_INFO("Accessibility: TTS {}", enabled ? "enabled" : "disabled");

    if (!enabled) {
        stopSpeaking();
    }
}

void Accessibility::speak(const std::string& text) {
    if (!m_config.ttsEnabled || text.empty()) return;
    executeTTS(text);
}

void Accessibility::stopSpeaking() {
    if (!m_speaking) return;

    // Kill any running TTS process.
    if (m_config.ttsEngine == "espeak-ng") {
        std::system("pkill -f espeak-ng 2>/dev/null");
    } else if (m_config.ttsEngine == "speech-dispatcher") {
        std::system("spd-say --cancel 2>/dev/null");
    }

    m_speaking = false;
}

void Accessibility::announceWindowFocus(Surface* surface) {
    if (!m_config.ttsEnabled || !surface) return;

    std::string announcement;
    const auto& title = surface->getTitle();
    const auto& appId = surface->getAppId();

    if (!title.empty()) {
        announcement = title;
    } else if (!appId.empty()) {
        announcement = appId;
    } else {
        announcement = "unknown window";
    }

    speak("Focused: " + announcement);

    // Also report to AT-SPI2.
    if (m_config.atSpiEnabled) {
        atSpiReportFocus(surface);
    }
}

void Accessibility::announceWorkspaceSwitch(const std::string& workspaceName) {
    if (!m_config.ttsEnabled) return;
    speak("Workspace " + workspaceName);
}

void Accessibility::announceNotification(const std::string& message) {
    if (!m_config.ttsEnabled) return;
    speak(message);
}

// ---------------------------------------------------------------------------
// High contrast mode (Task 100)
// ---------------------------------------------------------------------------

void Accessibility::toggleHighContrast() {
    m_config.highContrastEnabled = !m_config.highContrastEnabled;

    LOG_INFO("Accessibility: high contrast {}",
             m_config.highContrastEnabled ? "enabled" : "disabled");

    // Trigger a full repaint on all outputs.
    for (auto& output : m_server.getCompositor().getOutputs()) {
        output->addFullDamage();
    }
}

// ---------------------------------------------------------------------------
// Large cursor (Task 100)
// ---------------------------------------------------------------------------

void Accessibility::toggleLargeCursor() {
    m_config.largeCursorEnabled = !m_config.largeCursorEnabled;

    LOG_INFO("Accessibility: large cursor {}",
             m_config.largeCursorEnabled ? "enabled" : "disabled");

    // TODO: reload xcursor theme with the appropriate size.
    // int size = m_config.largeCursorEnabled
    //     ? m_config.cursorScale * baseSize : baseSize;
    // renderer->initCursor(cursor, theme, size);
}

// ---------------------------------------------------------------------------
// Screen magnifier (Task 100)
// ---------------------------------------------------------------------------

void Accessibility::toggleMagnifier() {
    m_magnifier.active = !m_magnifier.active;
    m_config.magnifierEnabled = m_magnifier.active;

    if (m_magnifier.active) {
        m_magnifier.zoom = m_config.magnifierZoom;
        m_magnifier.followCursor = m_config.magnifierFollowCursor;

        // Set initial view size based on active output.
        Output* activeOutput = m_server.getCompositor().getActiveOutput();
        if (activeOutput) {
            const auto& box = activeOutput->getBox();
            m_magnifier.viewWidth = box.width;
            m_magnifier.viewHeight = box.height;
        }
    }

    LOG_INFO("Accessibility: screen magnifier {}",
             m_magnifier.active ? "enabled" : "disabled");

    // Trigger repaint.
    for (auto& output : m_server.getCompositor().getOutputs()) {
        output->addFullDamage();
    }
}

void Accessibility::setMagnifierZoom(float zoom) {
    m_magnifier.zoom = std::clamp(zoom, 1.0f, 10.0f);
    m_config.magnifierZoom = m_magnifier.zoom;

    if (m_magnifier.active) {
        for (auto& output : m_server.getCompositor().getOutputs()) {
            output->addFullDamage();
        }
    }
}

void Accessibility::updateMagnifierPosition(double cursorX, double cursorY) {
    if (!m_magnifier.active || !m_magnifier.followCursor) return;

    m_magnifier.centerX = cursorX;
    m_magnifier.centerY = cursorY;
}

void Accessibility::magnifierZoomIn() {
    setMagnifierZoom(m_magnifier.zoom * 1.25f);
}

void Accessibility::magnifierZoomOut() {
    setMagnifierZoom(m_magnifier.zoom / 1.25f);
}

// ---------------------------------------------------------------------------
// AT-SPI2 compatibility (Task 100)
// ---------------------------------------------------------------------------

bool Accessibility::initATSpi() {
    // AT-SPI2 (Assistive Technology Service Provider Interface) allows
    // screen readers like Orca to interact with applications.
    //
    // For a Wayland compositor, we would:
    // 1. Connect to the AT-SPI2 D-Bus interface
    // 2. Register as an accessibility bus provider
    // 3. Report window events (focus, property changes, etc.)
    //
    // This is a stub implementation.

    LOG_INFO("Accessibility: AT-SPI2 compatibility stub initialized");
    LOG_WARN("Accessibility: AT-SPI2 integration requires atspi2 library");
    return true;
}

void Accessibility::atSpiReportFocus(Surface* surface) {
    if (!m_config.atSpiEnabled || !surface) return;

    // TODO: send focus event via D-Bus to AT-SPI2 bus.
    // org.a11y.atspi.Event.Focus with the window title/role.

    LOG_DEBUG("Accessibility: AT-SPI2 focus report for '{}'",
              surface->getTitle());
}

void Accessibility::atSpiReportPropertyChange(Surface* surface,
                                               const std::string& property,
                                               const std::string& value) {
    if (!m_config.atSpiEnabled || !surface) return;

    // TODO: send property-change event via D-Bus.
    LOG_DEBUG("Accessibility: AT-SPI2 property change '{}' = '{}' for '{}'",
              property, value, surface->getTitle());
}

// ---------------------------------------------------------------------------
// Reduced motion mode (Task 100)
// ---------------------------------------------------------------------------

void Accessibility::toggleReducedMotion() {
    m_config.reducedMotionEnabled = !m_config.reducedMotionEnabled;

    LOG_INFO("Accessibility: reduced motion {}",
             m_config.reducedMotionEnabled ? "enabled" : "disabled");

    // Notify the animation engine.
    // In a full implementation:
    // auto& animEngine = m_server.getAnimationEngine();
    // animEngine.setEnabled(!m_config.reducedMotionEnabled);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void Accessibility::executeTTS(const std::string& text) {
    if (text.empty()) return;

    // Sanitize text to prevent command injection.
    std::string sanitized;
    for (char c : text) {
        if (c == '\'' || c == '\\' || c == '`' || c == '$') {
            sanitized += ' ';
        } else {
            sanitized += c;
        }
    }

    std::string cmd;
    if (m_config.ttsEngine == "espeak-ng") {
        std::ostringstream oss;
        oss << "espeak-ng --speed="
            << static_cast<int>(175 * m_config.ttsRate)
            << " '" << sanitized << "' &";
        cmd = oss.str();
    } else if (m_config.ttsEngine == "speech-dispatcher") {
        cmd = "spd-say '" + sanitized + "' &";
    } else {
        LOG_WARN("Accessibility: unknown TTS engine '{}'", m_config.ttsEngine);
        return;
    }

    m_speaking = true;
    int ret = std::system(cmd.c_str());
    if (ret != 0) {
        LOG_WARN("Accessibility: TTS command failed (engine='{}')",
                 m_config.ttsEngine);
        m_speaking = false;
    }
}

} // namespace eternal

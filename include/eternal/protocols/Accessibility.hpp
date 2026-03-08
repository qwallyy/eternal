#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace eternal {

class Server;
class Output;
class Surface;

// ---------------------------------------------------------------------------
// Accessibility configuration
// ---------------------------------------------------------------------------

struct AccessibilityConfig {
    // Text-to-speech.
    bool ttsEnabled = false;
    std::string ttsEngine = "espeak-ng"; // or "speech-dispatcher"
    float ttsRate = 1.0f;

    // High contrast mode.
    bool highContrastEnabled = false;
    float contrastMultiplier = 1.5f;

    // Large cursor.
    bool largeCursorEnabled = false;
    int cursorScale = 2;

    // Screen magnifier.
    bool magnifierEnabled = false;
    float magnifierZoom = 2.0f;
    bool magnifierFollowCursor = true;

    // Reduced motion.
    bool reducedMotionEnabled = false;

    // AT-SPI2 integration.
    bool atSpiEnabled = false;
};

// ---------------------------------------------------------------------------
// Screen magnifier state
// ---------------------------------------------------------------------------

struct MagnifierState {
    bool active = false;
    float zoom = 2.0f;
    double centerX = 0.0;
    double centerY = 0.0;
    int viewWidth = 0;
    int viewHeight = 0;
    bool followCursor = true;
};

// ---------------------------------------------------------------------------
// Accessibility manager
// ---------------------------------------------------------------------------

class Accessibility {
public:
    explicit Accessibility(Server& server);
    ~Accessibility();

    Accessibility(const Accessibility&) = delete;
    Accessibility& operator=(const Accessibility&) = delete;

    /// Initialize accessibility features.
    bool init();

    /// Shutdown all accessibility features.
    void shutdown();

    /// Apply configuration changes.
    void applyConfig(const AccessibilityConfig& config);

    /// Get current configuration.
    [[nodiscard]] const AccessibilityConfig& getConfig() const { return m_config; }

    // ── Text-to-speech (Task 100) ────────────────────────────────────────

    /// Enable/disable TTS.
    void setTTSEnabled(bool enabled);

    /// Speak the given text.
    void speak(const std::string& text);

    /// Stop any current speech.
    void stopSpeaking();

    /// Announce a window focus change.
    void announceWindowFocus(Surface* surface);

    /// Announce a workspace switch.
    void announceWorkspaceSwitch(const std::string& workspaceName);

    /// Announce a notification.
    void announceNotification(const std::string& message);

    // ── High contrast mode (Task 100) ────────────────────────────────────

    /// Toggle high contrast mode.
    void toggleHighContrast();

    /// Whether high contrast mode is active.
    [[nodiscard]] bool isHighContrastEnabled() const {
        return m_config.highContrastEnabled;
    }

    /// Get the contrast multiplier for rendering.
    [[nodiscard]] float getContrastMultiplier() const {
        return m_config.highContrastEnabled ? m_config.contrastMultiplier : 1.0f;
    }

    // ── Large cursor (Task 100) ──────────────────────────────────────────

    /// Toggle large cursor mode.
    void toggleLargeCursor();

    /// Whether large cursor mode is active.
    [[nodiscard]] bool isLargeCursorEnabled() const {
        return m_config.largeCursorEnabled;
    }

    /// Get the cursor scale factor.
    [[nodiscard]] int getCursorScale() const {
        return m_config.largeCursorEnabled ? m_config.cursorScale : 1;
    }

    // ── Screen magnifier (Task 100) ──────────────────────────────────────

    /// Toggle the screen magnifier.
    void toggleMagnifier();

    /// Set magnifier zoom level.
    void setMagnifierZoom(float zoom);

    /// Update magnifier position (call on cursor move).
    void updateMagnifierPosition(double cursorX, double cursorY);

    /// Get the current magnifier state for rendering.
    [[nodiscard]] const MagnifierState& getMagnifierState() const {
        return m_magnifier;
    }

    /// Whether the magnifier is currently active.
    [[nodiscard]] bool isMagnifierActive() const { return m_magnifier.active; }

    /// Zoom in the magnifier.
    void magnifierZoomIn();

    /// Zoom out the magnifier.
    void magnifierZoomOut();

    // ── AT-SPI2 compatibility (Task 100) ─────────────────────────────────

    /// Initialize AT-SPI2 bridge (stub).
    bool initATSpi();

    /// Report a window focus event to AT-SPI2.
    void atSpiReportFocus(Surface* surface);

    /// Report a window property change to AT-SPI2.
    void atSpiReportPropertyChange(Surface* surface,
                                    const std::string& property,
                                    const std::string& value);

    // ── Reduced motion mode (Task 100) ──────────────────────────────────

    /// Toggle reduced motion mode (disables animations).
    void toggleReducedMotion();

    /// Whether reduced motion mode is active.
    [[nodiscard]] bool isReducedMotionEnabled() const {
        return m_config.reducedMotionEnabled;
    }

private:
    /// Execute TTS command.
    void executeTTS(const std::string& text);

    Server& m_server;
    AccessibilityConfig m_config;
    MagnifierState m_magnifier;
    bool m_initialized = false;
    bool m_speaking = false;
};

} // namespace eternal

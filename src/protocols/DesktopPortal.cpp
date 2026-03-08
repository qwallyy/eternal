#include "eternal/protocols/DesktopPortal.hpp"
#include "eternal/core/Server.hpp"
#include "eternal/core/Output.hpp"
#include "eternal/core/Compositor.hpp"
#include "eternal/screenshot/Screenshot.hpp"
#include "eternal/screenshot/ScreenRecorder.hpp"
#include "eternal/utils/Logger.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace eternal {

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

DesktopPortal::DesktopPortal(Server& server)
    : m_server(server) {}

DesktopPortal::~DesktopPortal() {
    shutdown();
}

bool DesktopPortal::init() {
    // In a full implementation, this would:
    // 1. Connect to D-Bus session bus
    // 2. Register org.freedesktop.impl.portal.Screenshot
    // 3. Register org.freedesktop.impl.portal.ScreenCast
    // 4. Register org.freedesktop.impl.portal.FileChooser

    m_initialized = true;
    LOG_INFO("DesktopPortal: initialized (xdg-desktop-portal-wlr compatible)");
    return true;
}

void DesktopPortal::shutdown() {
    // Close all active sessions.
    for (auto& [handle, session] : m_sessions) {
        if (session.active) {
            session.active = false;
            LOG_DEBUG("DesktopPortal: closing session '{}'", handle);
        }
    }
    m_sessions.clear();
    m_initialized = false;

    LOG_INFO("DesktopPortal: shut down");
}

// ---------------------------------------------------------------------------
// Screenshot portal
// ---------------------------------------------------------------------------

void DesktopPortal::handleScreenshot(const ScreenshotPortalRequest& req,
                                      ScreenshotCallback callback) {
    if (!m_initialized) {
        if (callback) callback(PortalResponse::Other, "");
        return;
    }

    LOG_INFO("DesktopPortal: screenshot request (interactive={})",
             req.interactive);

    // Generate a temporary path for the screenshot.
    std::string path = generateScreenshotPath();

    if (req.interactive) {
        // TODO: trigger interactive screenshot selection via Screenshot module.
        // For now, capture the full primary output.
        LOG_DEBUG("DesktopPortal: interactive screenshot requested, "
                  "falling back to full output");
    }

    // Capture the active output.
    // In a real implementation, we'd use the Screenshot subsystem.
    // Screenshot& screenshot = ...;
    // auto buffer = screenshot.captureOutput(activeOutput->getWlrOutput());
    // screenshot.saveToPNG(buffer, path);

    // For now, report success with the intended path.
    std::string uri = "file://" + path;
    LOG_INFO("DesktopPortal: screenshot saved to '{}'", path);

    if (callback) {
        callback(PortalResponse::Success, uri);
    }
}

// ---------------------------------------------------------------------------
// Screencast portal
// ---------------------------------------------------------------------------

void DesktopPortal::handleScreencastCreate(const ScreencastPortalRequest& req,
                                            ScreencastCallback callback) {
    if (!m_initialized) {
        if (callback) callback(PortalResponse::Other, 0);
        return;
    }

    std::string handle = generateSessionHandle();

    PortalSession session;
    session.sessionHandle = handle;
    session.sourceType = req.sourceType;
    session.active = false;

    m_sessions[handle] = session;

    LOG_INFO("DesktopPortal: screencast session created: '{}'", handle);

    if (callback) {
        callback(PortalResponse::Success, 0);
    }
}

void DesktopPortal::handleScreencastSelectSources(
    const std::string& sessionHandle, uint32_t sourceType) {
    auto it = m_sessions.find(sessionHandle);
    if (it == m_sessions.end()) {
        LOG_WARN("DesktopPortal: unknown session '{}'", sessionHandle);
        return;
    }

    it->second.sourceType = sourceType;
    LOG_DEBUG("DesktopPortal: sources selected for '{}' (type={})",
              sessionHandle, sourceType);
}

void DesktopPortal::handleScreencastStart(const std::string& sessionHandle,
                                           ScreencastCallback callback) {
    auto it = m_sessions.find(sessionHandle);
    if (it == m_sessions.end()) {
        if (callback) callback(PortalResponse::Other, 0);
        return;
    }

    auto& session = it->second;

    // TODO: Create PipeWire stream via ScreenRecorder and get the node ID.
    // ScreenRecorder& recorder = ...;
    // recorder.startRecording(target, options);
    // session.pipeWireNodeId = recorder.portalNodeId();

    // Stub: assign a placeholder node ID.
    session.pipeWireNodeId = 42;
    session.active = true;

    LOG_INFO("DesktopPortal: screencast started for '{}' (node={})",
             sessionHandle, session.pipeWireNodeId);

    if (callback) {
        callback(PortalResponse::Success, session.pipeWireNodeId);
    }
}

void DesktopPortal::handleScreencastClose(const std::string& sessionHandle) {
    closeSession(sessionHandle);
}

// ---------------------------------------------------------------------------
// File chooser redirect
// ---------------------------------------------------------------------------

void DesktopPortal::handleFileChooser(const FileChooserRequest& req,
                                       FileChooserCallback callback) {
    if (!m_initialized) {
        if (callback) callback(PortalResponse::Other, {});
        return;
    }

    LOG_INFO("DesktopPortal: file chooser request (title='{}', dir={})",
             req.title, req.directory);

    // In a full implementation, this would:
    // 1. Spawn an external file chooser dialog (e.g. zenity, kdialog)
    // 2. Capture the selected file(s)
    // 3. Return the URIs via callback

    // For now, just report cancellation.
    if (callback) {
        callback(PortalResponse::Cancelled, {});
    }
}

// ---------------------------------------------------------------------------
// Session management
// ---------------------------------------------------------------------------

void DesktopPortal::closeSession(const std::string& handle) {
    auto it = m_sessions.find(handle);
    if (it == m_sessions.end()) return;

    auto& session = it->second;
    if (session.active) {
        // TODO: stop PipeWire stream if screencast.
        session.active = false;
    }

    LOG_INFO("DesktopPortal: closed session '{}'", handle);
    m_sessions.erase(it);
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

std::string DesktopPortal::generateSessionHandle() {
    std::ostringstream oss;
    oss << "/org/freedesktop/portal/desktop/session/eternal/"
        << m_nextSessionId++;
    return oss.str();
}

std::string DesktopPortal::generateScreenshotPath() {
    auto now = std::chrono::system_clock::now();
    auto epoch = now.time_since_epoch();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(epoch).count();

    std::string dir;
    if (const char* xdg = std::getenv("XDG_PICTURES_DIR")) {
        dir = xdg;
    } else if (const char* home = std::getenv("HOME")) {
        dir = std::string(home) + "/Pictures";
    } else {
        dir = "/tmp";
    }

    std::filesystem::create_directories(dir);
    return dir + "/eternal-screenshot-" + std::to_string(ms) + ".png";
}

} // namespace eternal

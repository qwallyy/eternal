#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace eternal {

class Server;
class Output;
class ScreenRecorder;
class Screenshot;

// ---------------------------------------------------------------------------
// Portal request/response types
// ---------------------------------------------------------------------------

enum class PortalResponse : uint32_t {
    Success   = 0,
    Cancelled = 1,
    Other     = 2,
};

struct ScreenshotPortalRequest {
    std::string handle;
    bool interactive = false;      // let user select region
    bool modal = false;
};

struct ScreencastPortalRequest {
    std::string handle;
    uint32_t sourceType = 0;       // 1=monitor, 2=window, 4=virtual
    bool multipleOutput = false;
    uint32_t cursorMode = 0;       // 0=hidden, 1=embedded, 2=metadata
};

struct FileChooserRequest {
    std::string handle;
    std::string title;
    bool multiple = false;
    bool directory = false;
    std::vector<std::string> filters;  // e.g. "*.png", "*.jpg"
    std::string currentFolder;
};

// ---------------------------------------------------------------------------
// Portal session tracking
// ---------------------------------------------------------------------------

struct PortalSession {
    std::string sessionHandle;
    std::string appId;
    uint32_t sourceType = 0;
    uint32_t pipeWireNodeId = 0;
    bool active = false;
};

// ---------------------------------------------------------------------------
// DesktopPortal - xdg-desktop-portal-wlr compatible implementation
// ---------------------------------------------------------------------------

class DesktopPortal {
public:
    explicit DesktopPortal(Server& server);
    ~DesktopPortal();

    DesktopPortal(const DesktopPortal&) = delete;
    DesktopPortal& operator=(const DesktopPortal&) = delete;

    /// Initialize portal interfaces.
    bool init();

    /// Shutdown and clean up all portal sessions.
    void shutdown();

    // ── Screenshot portal ────────────────────────────────────────────────

    /// Handle a screenshot request from the portal.
    /// Returns the portal response and the path to the captured image.
    using ScreenshotCallback = std::function<void(PortalResponse, const std::string& uri)>;
    void handleScreenshot(const ScreenshotPortalRequest& req,
                          ScreenshotCallback callback);

    // ── Screencast portal ────────────────────────────────────────────────

    /// Create a screencast session.
    using ScreencastCallback = std::function<void(PortalResponse, uint32_t nodeId)>;
    void handleScreencastCreate(const ScreencastPortalRequest& req,
                                ScreencastCallback callback);

    /// Select sources for a screencast session.
    void handleScreencastSelectSources(const std::string& sessionHandle,
                                       uint32_t sourceType);

    /// Start a screencast session.
    void handleScreencastStart(const std::string& sessionHandle,
                               ScreencastCallback callback);

    /// Close a screencast session.
    void handleScreencastClose(const std::string& sessionHandle);

    // ── File chooser redirect ────────────────────────────────────────────

    /// Redirect file chooser requests to an external file chooser.
    using FileChooserCallback = std::function<void(PortalResponse,
                                                    const std::vector<std::string>& uris)>;
    void handleFileChooser(const FileChooserRequest& req,
                           FileChooserCallback callback);

    // ── Session management ───────────────────────────────────────────────

    /// Get all active portal sessions.
    [[nodiscard]] const std::unordered_map<std::string, PortalSession>&
    getSessions() const { return m_sessions; }

    /// Close a session by handle.
    void closeSession(const std::string& handle);

private:
    /// Generate a unique session handle.
    std::string generateSessionHandle();

    /// Generate a temporary file path for screenshots.
    std::string generateScreenshotPath();

    Server& m_server;
    std::unordered_map<std::string, PortalSession> m_sessions;
    uint64_t m_nextSessionId = 1;
    bool m_initialized = false;
};

} // namespace eternal

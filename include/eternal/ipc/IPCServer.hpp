#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace eternal {

class Server;

// ---------------------------------------------------------------------------
// IPC event types for subscription -- Task 66
// ---------------------------------------------------------------------------

enum class IPCEventType : uint32_t {
    WindowOpen          = 1 << 0,
    WindowClose         = 1 << 1,
    WindowFocus         = 1 << 2,
    WindowMove          = 1 << 3,
    WorkspaceCreated    = 1 << 4,
    WorkspaceDestroyed  = 1 << 5,
    WorkspaceFocused    = 1 << 6,
    MonitorAdded        = 1 << 7,
    MonitorRemoved      = 1 << 8,
    ConfigReloaded      = 1 << 9,
    All                 = 0xFFFFFFFF,
};

inline IPCEventType operator|(IPCEventType a, IPCEventType b) {
    return static_cast<IPCEventType>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
inline IPCEventType operator&(IPCEventType a, IPCEventType b) {
    return static_cast<IPCEventType>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}
inline bool hasFlag(IPCEventType set, IPCEventType flag) {
    return (static_cast<uint32_t>(set) & static_cast<uint32_t>(flag)) != 0;
}

// ---------------------------------------------------------------------------
// IPC client connection
// ---------------------------------------------------------------------------

struct IPCClientConnection {
    int fd = -1;
    IPCEventType subscriptions = static_cast<IPCEventType>(0);
    std::string read_buffer;
    std::string write_buffer;
    bool wants_events = false;
};

// ---------------------------------------------------------------------------
// IPCServer -- Task 63, 64, 66, 67, 68, 69, 70
// ---------------------------------------------------------------------------

class IPCServer {
public:
    explicit IPCServer(Server& server);
    ~IPCServer();

    IPCServer(const IPCServer&) = delete;
    IPCServer& operator=(const IPCServer&) = delete;

    /// Create socket at $XDG_RUNTIME_DIR/eternal/$WAYLAND_DISPLAY.sock
    /// and start listening. Returns true on success.
    bool start();

    /// Start on a specific path.
    bool start(const std::string& path);

    /// Stop the server and close all connections.
    void stop();

    /// Accept new connections and process data on existing ones.
    /// Call from event loop when socket fd is readable.
    void handleEvents();

    /// Get the listening socket fd for integration with event loops.
    [[nodiscard]] int getSocketFd() const noexcept { return m_socketFd; }

    /// Process a JSON command string and return a JSON response.
    [[nodiscard]] std::string processCommand(const std::string& json);

    /// Register a handler for a specific command name.
    using CommandHandler = std::function<std::string(const std::string& args)>;
    void registerHandler(const std::string& command, CommandHandler handler);

    /// Broadcast an event to all subscribed clients -- Task 66
    void broadcastEvent(IPCEventType type, const std::string& eventName,
                        const std::string& jsonData);

    [[nodiscard]] const std::string& getSocketPath() const { return m_socketPath; }
    [[nodiscard]] bool isRunning() const { return m_running; }
    [[nodiscard]] size_t clientCount() const { return m_clients.size(); }

private:
    /// Determine default socket path from environment.
    [[nodiscard]] std::string defaultSocketPath() const;

    /// Accept pending connections (non-blocking).
    void acceptClients();

    /// Read and process data from a client.
    void processClientData(IPCClientConnection& client);

    /// Send length-prefixed response to a client fd.
    bool sendResponse(int fd, const std::string& response);

    /// Close and remove a client connection.
    void removeClient(int fd);

    /// Handle a subscribe command.
    std::string handleSubscribe(IPCClientConnection& client, const std::string& args);

    /// Built-in query handlers -- Task 67
    std::string handleMonitors();
    std::string handleWorkspaces();
    std::string handleWindows();
    std::string handleActiveWindow();
    std::string handleLayers();
    std::string handleVersion();

    /// Built-in dispatch handlers -- Task 68, 69
    std::string handleDispatch(const std::string& dispatcher, const std::string& params);
    std::string handleGetOption(const std::string& optionPath);
    std::string handleSetOption(const std::string& optionPath, const std::string& value);

    /// JSON helpers
    static std::string jsonOk(const std::string& data = "{}");
    static std::string jsonError(const std::string& message);

    Server& m_server;
    int m_socketFd = -1;
    std::string m_socketPath;
    std::vector<IPCClientConnection> m_clients;
    std::unordered_map<std::string, CommandHandler> m_handlers;
    std::atomic<bool> m_running{false};
    mutable std::mutex m_mutex;
};

} // namespace eternal

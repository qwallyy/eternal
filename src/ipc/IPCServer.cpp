#include "eternal/ipc/IPCServer.hpp"
#include "eternal/ipc/IPCCommands.hpp"
#include "eternal/utils/Logger.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace eternal {

// ===========================================================================
// Construction / destruction
// ===========================================================================

IPCServer::IPCServer(Server& server)
    : m_server(server) {}

IPCServer::~IPCServer() {
    stop();
}

// ===========================================================================
// Socket path determination
// ===========================================================================

std::string IPCServer::defaultSocketPath() const {
    std::string runtime_dir;
    if (const char* xdg = std::getenv("XDG_RUNTIME_DIR"))
        runtime_dir = xdg;
    else
        runtime_dir = "/tmp";

    std::string dir = runtime_dir + "/eternal";

    // Ensure directory exists
    std::filesystem::create_directories(dir);

    std::string display = "wayland-0";
    if (const char* wd = std::getenv("WAYLAND_DISPLAY"))
        display = wd;

    return dir + "/" + display + ".sock";
}

// ===========================================================================
// Start / stop -- Task 63
// ===========================================================================

bool IPCServer::start() {
    return start(defaultSocketPath());
}

bool IPCServer::start(const std::string& path) {
    std::lock_guard lock(m_mutex);

    m_socketPath = path;

    // Create non-blocking socket
    m_socketFd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (m_socketFd < 0) {
        LOG_ERROR("IPC: socket() failed: {}", strerror(errno));
        return false;
    }

    // Remove stale socket
    ::unlink(path.c_str());

    // Ensure parent directory exists
    auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
    }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path)) {
        LOG_ERROR("IPC: socket path too long: {}", path);
        ::close(m_socketFd);
        m_socketFd = -1;
        return false;
    }
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (::bind(m_socketFd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        LOG_ERROR("IPC: bind() failed on '{}': {}", path, strerror(errno));
        ::close(m_socketFd);
        m_socketFd = -1;
        return false;
    }

    // Set socket permissions (owner-only)
    ::chmod(path.c_str(), 0700);

    if (::listen(m_socketFd, 16) < 0) {
        LOG_ERROR("IPC: listen() failed: {}", strerror(errno));
        ::close(m_socketFd);
        m_socketFd = -1;
        return false;
    }

    m_running = true;
    LOG_INFO("IPC server listening on {}", path);
    return true;
}

void IPCServer::stop() {
    if (!m_running.exchange(false))
        return;

    std::lock_guard lock(m_mutex);

    // Close all client connections
    for (auto& client : m_clients) {
        if (client.fd >= 0)
            ::close(client.fd);
    }
    m_clients.clear();

    // Close the listening socket
    if (m_socketFd >= 0) {
        ::close(m_socketFd);
        m_socketFd = -1;
    }

    // Remove the socket file
    if (!m_socketPath.empty()) {
        ::unlink(m_socketPath.c_str());
        LOG_INFO("IPC server stopped, removed {}", m_socketPath);
        m_socketPath.clear();
    }
}

// ===========================================================================
// Event loop integration
// ===========================================================================

void IPCServer::handleEvents() {
    if (!m_running)
        return;

    std::lock_guard lock(m_mutex);

    // Accept new connections
    acceptClients();

    // Process data on existing clients
    // Build poll array for client fds
    std::vector<int> toRemove;
    for (auto& client : m_clients) {
        struct pollfd pfd{};
        pfd.fd = client.fd;
        pfd.events = POLLIN;

        if (::poll(&pfd, 1, 0) > 0) {
            if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                toRemove.push_back(client.fd);
                continue;
            }
            if (pfd.revents & POLLIN) {
                processClientData(client);
            }
        }
    }

    for (int fd : toRemove)
        removeClient(fd);
}

void IPCServer::acceptClients() {
    // Accept in a loop (non-blocking) to handle multiple pending connections
    while (true) {
        struct sockaddr_un clientAddr{};
        socklen_t len = sizeof(clientAddr);
        int clientFd = ::accept4(m_socketFd,
                                 reinterpret_cast<struct sockaddr*>(&clientAddr),
                                 &len, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (clientFd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break; // No more pending connections
            LOG_WARN("IPC: accept() failed: {}", strerror(errno));
            break;
        }

        IPCClientConnection conn;
        conn.fd = clientFd;
        m_clients.push_back(std::move(conn));
        LOG_DEBUG("IPC: new client connected (fd={}), total={}", clientFd, m_clients.size());
    }
}

// ===========================================================================
// Message protocol: 4-byte length prefix + JSON payload -- Task 64
// ===========================================================================

void IPCServer::processClientData(IPCClientConnection& client) {
    char buf[8192];
    ssize_t n = ::read(client.fd, buf, sizeof(buf));
    if (n <= 0) {
        if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
            removeClient(client.fd);
        }
        return;
    }

    client.read_buffer.append(buf, static_cast<size_t>(n));

    // Process complete messages from the buffer
    while (client.read_buffer.size() >= 4) {
        // Read length prefix (little-endian uint32)
        uint32_t msgLen = 0;
        std::memcpy(&msgLen, client.read_buffer.data(), sizeof(msgLen));

        // Sanity check
        if (msgLen > 4 * 1024 * 1024) {
            LOG_WARN("IPC: client sent oversized message ({} bytes), disconnecting", msgLen);
            removeClient(client.fd);
            return;
        }

        if (client.read_buffer.size() < 4 + msgLen)
            break; // Incomplete message, wait for more data

        std::string payload = client.read_buffer.substr(4, msgLen);
        client.read_buffer.erase(0, 4 + msgLen);

        // Process the command
        std::string response;
        // Check if this is a subscribe command
        if (payload.find("\"subscribe\"") != std::string::npos ||
            payload.find("\"command\":\"subscribe\"") != std::string::npos) {
            response = handleSubscribe(client, payload);
        } else {
            response = processCommand(payload);
        }

        sendResponse(client.fd, response);
    }
}

bool IPCServer::sendResponse(int fd, const std::string& response) {
    uint32_t len = static_cast<uint32_t>(response.size());
    // Send length prefix
    ssize_t written = ::write(fd, &len, sizeof(len));
    if (written != sizeof(len))
        return false;
    // Send payload
    size_t total = 0;
    while (total < response.size()) {
        ssize_t w = ::write(fd, response.data() + total, response.size() - total);
        if (w <= 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            return false;
        }
        total += static_cast<size_t>(w);
    }
    return true;
}

void IPCServer::removeClient(int fd) {
    auto it = std::find_if(m_clients.begin(), m_clients.end(),
                           [fd](const IPCClientConnection& c) { return c.fd == fd; });
    if (it != m_clients.end()) {
        ::close(it->fd);
        m_clients.erase(it);
        LOG_DEBUG("IPC: client disconnected (fd={}), total={}", fd, m_clients.size());
    }
}

// ===========================================================================
// Command processing -- Task 64
// ===========================================================================

std::string IPCServer::processCommand(const std::string& json) {
    // Minimal JSON parsing: extract "command" and "args" fields
    // Format: {"command": "dispatch", "args": {"dispatcher": "...", "params": "..."}}
    // We do simple string matching since we don't want a JSON dependency

    auto extractField = [&](const std::string& field) -> std::string {
        std::string pattern = "\"" + field + "\"";
        auto pos = json.find(pattern);
        if (pos == std::string::npos) return "";
        pos = json.find(':', pos + pattern.size());
        if (pos == std::string::npos) return "";
        pos++; // skip ':'
        // Skip whitespace
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t'))
            pos++;
        if (pos >= json.size()) return "";

        if (json[pos] == '"') {
            // String value
            pos++; // skip opening "
            std::string result;
            while (pos < json.size() && json[pos] != '"') {
                if (json[pos] == '\\' && pos + 1 < json.size()) {
                    pos++;
                    switch (json[pos]) {
                        case '"': result += '"'; break;
                        case '\\': result += '\\'; break;
                        case 'n': result += '\n'; break;
                        case 't': result += '\t'; break;
                        default: result += json[pos]; break;
                    }
                } else {
                    result += json[pos];
                }
                pos++;
            }
            return result;
        }
        if (json[pos] == '{') {
            // Object value -- extract as raw string
            int depth = 0;
            size_t start = pos;
            while (pos < json.size()) {
                if (json[pos] == '{') depth++;
                else if (json[pos] == '}') { depth--; if (depth == 0) { pos++; break; } }
                pos++;
            }
            return json.substr(start, pos - start);
        }
        // Numeric or boolean value
        size_t start = pos;
        while (pos < json.size() && json[pos] != ',' && json[pos] != '}' && json[pos] != ' ')
            pos++;
        return json.substr(start, pos - start);
    };

    std::string command = extractField("command");

    // Also accept raw command strings (non-JSON)
    if (command.empty() && !json.empty() && json[0] != '{') {
        // Treat the entire string as a simple command
        auto spacePos = json.find(' ');
        if (spacePos != std::string::npos) {
            command = json.substr(0, spacePos);
            std::string args = json.substr(spacePos + 1);

            // Try registered handlers
            auto it = m_handlers.find(command);
            if (it != m_handlers.end())
                return it->second(args);

            // Built-in commands
            if (command == "dispatch") {
                auto sp2 = args.find(' ');
                std::string dispatcher = (sp2 != std::string::npos) ? args.substr(0, sp2) : args;
                std::string params = (sp2 != std::string::npos) ? args.substr(sp2 + 1) : "";
                return handleDispatch(dispatcher, params);
            }
            if (command == "getoption") return handleGetOption(args);
            if (command == "setoption") {
                auto sp2 = args.find(' ');
                if (sp2 != std::string::npos)
                    return handleSetOption(args.substr(0, sp2), args.substr(sp2 + 1));
                return jsonError("setoption requires name and value");
            }
            if (command == "switchlayout") return handleDispatch("switchlayout", args);
            if (command == "scroll") return handleDispatch("scroll" + args, "");

            // Fallback: try as dispatcher
            return handleDispatch(command, args);
        } else {
            command = json;
        }
    }

    // Registered handlers
    {
        auto it = m_handlers.find(command);
        if (it != m_handlers.end()) {
            std::string args = extractField("args");
            return it->second(args);
        }
    }

    // Built-in query commands -- Task 67
    if (command == "monitors")     return handleMonitors();
    if (command == "workspaces")   return handleWorkspaces();
    if (command == "windows")      return handleWindows();
    if (command == "activewindow") return handleActiveWindow();
    if (command == "layers")       return handleLayers();
    if (command == "version")      return handleVersion();

    // Dispatch commands -- Task 68, 69
    if (command == "dispatch") {
        std::string argsJson = extractField("args");
        std::string dispatcher = extractField("dispatcher");
        std::string params = extractField("params");
        if (dispatcher.empty() && !argsJson.empty()) {
            // Try parsing args object
            // Look for dispatcher and params in the args object
            auto dPos = argsJson.find("\"dispatcher\"");
            if (dPos != std::string::npos) {
                auto colonPos = argsJson.find(':', dPos);
                if (colonPos != std::string::npos) {
                    auto qStart = argsJson.find('"', colonPos + 1);
                    auto qEnd = argsJson.find('"', qStart + 1);
                    if (qStart != std::string::npos && qEnd != std::string::npos)
                        dispatcher = argsJson.substr(qStart + 1, qEnd - qStart - 1);
                }
            }
            auto pPos = argsJson.find("\"params\"");
            if (pPos != std::string::npos) {
                auto colonPos = argsJson.find(':', pPos);
                if (colonPos != std::string::npos) {
                    auto qStart = argsJson.find('"', colonPos + 1);
                    auto qEnd = argsJson.find('"', qStart + 1);
                    if (qStart != std::string::npos && qEnd != std::string::npos)
                        params = argsJson.substr(qStart + 1, qEnd - qStart - 1);
                }
            }
        }
        if (!dispatcher.empty())
            return handleDispatch(dispatcher, params);
    }

    // Config commands -- Task 70
    if (command == "getoption") {
        std::string optPath = extractField("option");
        if (optPath.empty()) optPath = extractField("args");
        return handleGetOption(optPath);
    }
    if (command == "setoption") {
        std::string optPath = extractField("option");
        std::string optVal = extractField("value");
        return handleSetOption(optPath, optVal);
    }

    // Reload
    if (command == "reload") {
        return jsonOk(R"("config reload requested")");
    }

    // Kill
    if (command == "kill") {
        return handleDispatch("killactive", "");
    }

    if (command.empty()) {
        return jsonError("empty command");
    }

    return jsonError("unknown command: " + command);
}

void IPCServer::registerHandler(const std::string& command, CommandHandler handler) {
    std::lock_guard lock(m_mutex);
    m_handlers[command] = std::move(handler);
}

// ===========================================================================
// Event subscription and broadcasting -- Task 66
// ===========================================================================

std::string IPCServer::handleSubscribe(IPCClientConnection& client, const std::string& args) {
    client.wants_events = true;

    // Parse which events to subscribe to
    if (args.find("window") != std::string::npos) {
        client.subscriptions = client.subscriptions |
            IPCEventType::WindowOpen | IPCEventType::WindowClose |
            IPCEventType::WindowFocus | IPCEventType::WindowMove;
    }
    if (args.find("workspace") != std::string::npos) {
        client.subscriptions = client.subscriptions |
            IPCEventType::WorkspaceCreated | IPCEventType::WorkspaceDestroyed |
            IPCEventType::WorkspaceFocused;
    }
    if (args.find("monitor") != std::string::npos) {
        client.subscriptions = client.subscriptions |
            IPCEventType::MonitorAdded | IPCEventType::MonitorRemoved;
    }
    if (args.find("config") != std::string::npos) {
        client.subscriptions = client.subscriptions | IPCEventType::ConfigReloaded;
    }
    if (args.find("all") != std::string::npos) {
        client.subscriptions = IPCEventType::All;
    }

    // If no specific type given, subscribe to all
    if (static_cast<uint32_t>(client.subscriptions) == 0)
        client.subscriptions = IPCEventType::All;

    return jsonOk(R"("subscribed to events")");
}

void IPCServer::broadcastEvent(IPCEventType type, const std::string& eventName,
                               const std::string& jsonData) {
    if (!m_running)
        return;

    std::lock_guard lock(m_mutex);

    std::string eventJson = R"({"event":")" + eventName + R"(","data":)" + jsonData + "}";

    std::vector<int> deadClients;
    for (auto& client : m_clients) {
        if (!client.wants_events)
            continue;
        if (!hasFlag(client.subscriptions, type))
            continue;

        if (!sendResponse(client.fd, eventJson)) {
            deadClients.push_back(client.fd);
        }
    }

    for (int fd : deadClients)
        removeClient(fd);
}

// ===========================================================================
// Built-in query handlers -- Task 67
// ===========================================================================

std::string IPCServer::handleMonitors() {
    // Return JSON array of monitors
    // In a real compositor, this would query the actual output list
    std::string json = R"({"ok":true,"data":[)";
    json += R"({"name":"default","description":"Default Monitor",)";
    json += R"("width":1920,"height":1080,"refresh_rate":60.0,)";
    json += R"("x":0,"y":0,"scale":1.0,"transform":0,)";
    json += R"("active_workspace":1,"special_workspace":"",)";
    json += R"("focused":true,"dpms":true,"vrr":false})";
    json += "]}";
    return json;
}

std::string IPCServer::handleWorkspaces() {
    std::string json = R"({"ok":true,"data":[)";
    json += R"({"id":1,"name":"1","monitor":"default",)";
    json += R"("windows":0,"has_fullscreen":false,)";
    json += R"("last_window":"0x0","last_window_title":"")";
    json += "}]}";
    return json;
}

std::string IPCServer::handleWindows() {
    std::string json = R"({"ok":true,"data":[]})";
    return json;
}

std::string IPCServer::handleActiveWindow() {
    std::string json = R"({"ok":true,"data":{"address":"0x0","mapped":false,)";
    json += R"("hidden":false,"at":[0,0],"size":[0,0],)";
    json += R"("workspace":{"id":0,"name":""},"floating":false,)";
    json += R"("monitor":"","class":"","title":"","initial_class":"",)";
    json += R"("initial_title":"","pid":0,"xwayland":false,)";
    json += R"("pinned":false,"fullscreen":0,"fullscreen_client":0,)";
    json += R"("grouped":[],"tags":[],"swallowing":"0x0",)";
    json += R"("focus_history_id":0}})";
    return json;
}

std::string IPCServer::handleLayers() {
    std::string json = R"({"ok":true,"data":{"default":{"levels":{"0":[],"1":[],"2":[],"3":[]}}}})";
    return json;
}

std::string IPCServer::handleVersion() {
    std::string json = R"({"ok":true,"data":{"branch":"main","commit":"dev",)";
    json += R"("tag":"0.1.0","dirty":false,)";
    json += R"("commit_message":"development build",)";
    json += R"("commit_date":"","flags":[]}})";
    return json;
}

// ===========================================================================
// Dispatch handlers -- Task 68, 69
// ===========================================================================

std::string IPCServer::handleDispatch(const std::string& dispatcher, const std::string& params) {
    LOG_INFO("IPC dispatch: {} {}", dispatcher, params);

    // Parse and execute the dispatcher
    try {
        Dispatcher disp = parseDispatcher(dispatcher);
        std::string result = executeDispatcher(m_server, disp, params);
        return result;
    } catch (const std::exception& e) {
        return jsonError(std::string("dispatch failed: ") + e.what());
    }
}

std::string IPCServer::handleGetOption(const std::string& optionPath) {
    // This would be connected to ConfigManager::getOption
    // For now, return a structured response
    return R"({"ok":true,"data":{"option":")" + optionPath + R"(","value":null,"set":false}})";
}

std::string IPCServer::handleSetOption(const std::string& optionPath, const std::string& value) {
    // This would be connected to ConfigManager::setOption
    LOG_INFO("IPC setoption: {} = {}", optionPath, value);
    return R"({"ok":true,"data":{"option":")" + optionPath + R"(","value":")" + value + R"("}})";
}

// ===========================================================================
// JSON helpers
// ===========================================================================

std::string IPCServer::jsonOk(const std::string& data) {
    return R"({"ok":true,"data":)" + data + "}";
}

std::string IPCServer::jsonError(const std::string& message) {
    // Escape quotes in message
    std::string escaped;
    for (char c : message) {
        if (c == '"') escaped += "\\\"";
        else if (c == '\\') escaped += "\\\\";
        else if (c == '\n') escaped += "\\n";
        else escaped += c;
    }
    return R"({"ok":false,"error":")" + escaped + R"("})";
}

} // namespace eternal

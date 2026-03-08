#include "EternalCtl.hpp"

#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>
#include <unistd.h>

namespace eternal::cli {

// ===========================================================================
// IPCClient
// ===========================================================================

IPCClient::IPCClient() {
    detectSocketPath();
}

IPCClient::~IPCClient() {
    disconnect();
}

void IPCClient::detectSocketPath() {
    // Primary: $XDG_RUNTIME_DIR/eternal/$WAYLAND_DISPLAY.sock
    const char* runtime_dir = std::getenv("XDG_RUNTIME_DIR");
    const char* wayland_display = std::getenv("WAYLAND_DISPLAY");

    if (runtime_dir) {
        std::string dir = std::string(runtime_dir) + "/eternal";
        std::string display = wayland_display ? wayland_display : "wayland-0";
        socket_path_ = dir + "/" + display + ".sock";

        // Check if it exists
        if (std::filesystem::exists(socket_path_))
            return;

        // Fallback: legacy path format
        const char* instance_sig = std::getenv("ETERNAL_INSTANCE_SIGNATURE");
        std::string legacy = std::string(runtime_dir) + "/eternal";
        if (instance_sig) {
            legacy += ".";
            legacy += instance_sig;
        }
        legacy += ".sock";
        if (std::filesystem::exists(legacy)) {
            socket_path_ = legacy;
            return;
        }
    }

    // Fallback: /tmp/eternal/wayland-0.sock
    std::string display = wayland_display ? wayland_display : "wayland-0";
    std::string fallback = "/tmp/eternal/" + display + ".sock";
    if (std::filesystem::exists(fallback)) {
        socket_path_ = fallback;
        return;
    }
}

bool IPCClient::connect() {
    if (socket_path_.empty()) {
        std::cerr << "eternalctl: cannot determine IPC socket path\n";
        std::cerr << "  Is the eternal compositor running?\n";
        std::cerr << "  Set XDG_RUNTIME_DIR and WAYLAND_DISPLAY.\n";
        return false;
    }
    return connect(socket_path_);
}

bool IPCClient::connect(const std::string& path) {
    socket_path_ = path;

    fd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd_ < 0) {
        std::cerr << "eternalctl: socket() failed: " << strerror(errno) << "\n";
        return false;
    }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path)) {
        std::cerr << "eternalctl: socket path too long\n";
        ::close(fd_);
        fd_ = -1;
        return false;
    }
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "eternalctl: connect() to '" << path
                  << "' failed: " << strerror(errno) << "\n";
        std::cerr << "  Is the eternal compositor running?\n";
        ::close(fd_);
        fd_ = -1;
        return false;
    }
    return true;
}

std::string IPCClient::request(std::string_view message) {
    if (fd_ < 0) return {};

    // Send length-prefixed message (4-byte little-endian + payload)
    uint32_t len = static_cast<uint32_t>(message.size());
    if (::write(fd_, &len, sizeof(len)) != sizeof(len)) {
        std::cerr << "eternalctl: failed to send message length\n";
        return {};
    }
    if (::write(fd_, message.data(), len) != static_cast<ssize_t>(len)) {
        std::cerr << "eternalctl: failed to send message body\n";
        return {};
    }

    // Read length-prefixed response with timeout
    struct pollfd pfd{};
    pfd.fd = fd_;
    pfd.events = POLLIN;

    if (::poll(&pfd, 1, 5000) <= 0) {
        std::cerr << "eternalctl: timeout waiting for response\n";
        return {};
    }

    uint32_t resp_len = 0;
    if (::read(fd_, &resp_len, sizeof(resp_len)) != sizeof(resp_len)) {
        std::cerr << "eternalctl: failed to read response length\n";
        return {};
    }
    if (resp_len == 0 || resp_len > 4 * 1024 * 1024) {
        std::cerr << "eternalctl: invalid response length: " << resp_len << "\n";
        return {};
    }

    std::string response(resp_len, '\0');
    size_t total = 0;
    while (total < resp_len) {
        ssize_t n = ::read(fd_, response.data() + total, resp_len - total);
        if (n <= 0) {
            if (n == 0) break;
            if (errno == EINTR) continue;
            break;
        }
        total += static_cast<size_t>(n);
    }
    response.resize(total);
    return response;
}

void IPCClient::subscribe(const std::string& eventTypes,
                          EventCallback callback, void* userdata) {
    // Send subscribe request
    std::string req = R"({"command":"subscribe","args":")" + eventTypes + R"("})";
    auto resp = request(req);
    if (resp.empty()) return;

    // Now continuously read events
    while (true) {
        struct pollfd pfd{};
        pfd.fd = fd_;
        pfd.events = POLLIN;

        int ret = ::poll(&pfd, 1, -1); // block forever
        if (ret <= 0) break;
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) break;

        uint32_t event_len = 0;
        if (::read(fd_, &event_len, sizeof(event_len)) != sizeof(event_len))
            break;
        if (event_len == 0 || event_len > 4 * 1024 * 1024)
            break;

        std::string event(event_len, '\0');
        size_t total = 0;
        while (total < event_len) {
            ssize_t n = ::read(fd_, event.data() + total, event_len - total);
            if (n <= 0) break;
            total += static_cast<size_t>(n);
        }
        event.resize(total);

        if (!event.empty() && callback)
            callback(event, userdata);
    }
}

void IPCClient::disconnect() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

// ===========================================================================
// EternalCtl
// ===========================================================================

EternalCtl::EternalCtl() = default;

int EternalCtl::run(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage();
        return 1;
    }

    // Parse global flags
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "-j" || arg == "--json") {
            json_output_ = true;
        } else if (arg == "-q" || arg == "--quiet") {
            quiet_ = true;
        } else if (arg == "-h" || arg == "--help") {
            printUsage();
            return 0;
        } else if (arg == "-s" || arg == "--socket") {
            // Custom socket path
            if (i + 1 < argc) {
                ++i;
                // Override socket path before connect
                ipc_.disconnect();
            }
        } else {
            args.push_back(std::move(arg));
        }
    }

    if (args.empty()) {
        printUsage();
        return 1;
    }

    const std::string& cmd = args[0];

    // Commands that don't need IPC
    if (cmd == "version")      return cmdVersion();
    if (cmd == "help")         { printUsage(); return 0; }
    if (cmd == "dispatchers")  return cmdDispatchers();

    // Connect to IPC
    if (!ipc_.connect()) return 2;

    // Route commands
    if (cmd == "dispatch")     return cmdDispatch(args);
    if (cmd == "monitors")     return cmdMonitors();
    if (cmd == "workspaces")   return cmdWorkspaces();
    if (cmd == "windows")      return cmdWindows();
    if (cmd == "activewindow") return cmdActiveWindow();
    if (cmd == "layers")       return cmdLayers();
    if (cmd == "reload")       return cmdReload();
    if (cmd == "kill")         return cmdKill();
    if (cmd == "splash")       return cmdSplash();
    if (cmd == "getoption")    return cmdGetOption(args);
    if (cmd == "setoption")    return cmdSetOption(args);
    if (cmd == "switchlayout") return cmdSwitchLayout(args);
    if (cmd == "scroll")       return cmdScroll(args);
    if (cmd == "subscribe")    return cmdSubscribe(args);

    std::cerr << "eternalctl: unknown command '" << cmd << "'\n";
    std::cerr << "Run 'eternalctl help' for usage information.\n";
    return 1;
}

// ===========================================================================
// JSON request builder
// ===========================================================================

std::string EternalCtl::makeRequest(const std::string& command,
                                     const std::string& argsJson) const {
    return R"({"command":")" + command + R"(","args":)" + argsJson + "}";
}

int EternalCtl::sendAndPrint(const std::string& request) {
    auto resp = ipc_.request(request);
    if (resp.empty()) {
        if (!quiet_)
            std::cerr << "eternalctl: no response from compositor\n";
        return 1;
    }
    printResponse(resp);
    // Check if response indicates error
    if (resp.find("\"ok\":false") != std::string::npos)
        return 1;
    return 0;
}

// ===========================================================================
// Individual commands -- Task 65
// ===========================================================================

int EternalCtl::cmdDispatch(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cerr << "eternalctl dispatch: expected dispatcher name\n";
        std::cerr << "Usage: eternalctl dispatch <name> [args...]\n";
        return 1;
    }

    std::string dispatcher = args[1];
    std::string params;
    for (size_t i = 2; i < args.size(); ++i) {
        if (!params.empty()) params += " ";
        params += args[i];
    }

    std::string req = R"({"command":"dispatch","args":{"dispatcher":")" +
                      dispatcher + R"(","params":")" + params + R"("}})";
    return sendAndPrint(req);
}

int EternalCtl::cmdMonitors() {
    return sendAndPrint(makeRequest("monitors"));
}

int EternalCtl::cmdWorkspaces() {
    return sendAndPrint(makeRequest("workspaces"));
}

int EternalCtl::cmdWindows() {
    return sendAndPrint(makeRequest("windows"));
}

int EternalCtl::cmdActiveWindow() {
    return sendAndPrint(makeRequest("activewindow"));
}

int EternalCtl::cmdLayers() {
    return sendAndPrint(makeRequest("layers"));
}

int EternalCtl::cmdVersion() {
    if (ipc_.isConnected() || ipc_.connect()) {
        auto resp = ipc_.request(makeRequest("version"));
        if (!resp.empty()) {
            printResponse(resp);
            return 0;
        }
    }
    // Fallback: just print local version
    if (json_output_) {
        std::cout << R"({"ok":true,"data":{"version":"0.1.0","client":"eternalctl"}})" << "\n";
    } else {
        std::cout << "eternalctl 0.1.0\n";
    }
    return 0;
}

int EternalCtl::cmdReload() {
    auto resp = ipc_.request(makeRequest("reload"));
    if (resp.empty()) {
        std::cerr << "eternalctl: failed to send reload command\n";
        return 1;
    }
    printResponse(resp);
    return 0;
}

int EternalCtl::cmdKill() {
    return sendAndPrint(makeRequest("kill"));
}

int EternalCtl::cmdSplash() {
    return sendAndPrint(makeRequest("splash"));
}

int EternalCtl::cmdGetOption(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cerr << "eternalctl getoption: expected option name\n";
        std::cerr << "Usage: eternalctl getoption <section:name>\n";
        std::cerr << "Example: eternalctl getoption general:gaps_in\n";
        return 1;
    }
    std::string req = R"({"command":"getoption","option":")" + args[1] + R"("})";
    return sendAndPrint(req);
}

int EternalCtl::cmdSetOption(const std::vector<std::string>& args) {
    if (args.size() < 3) {
        std::cerr << "eternalctl setoption: expected option name and value\n";
        std::cerr << "Usage: eternalctl setoption <section:name> <value>\n";
        std::cerr << "Example: eternalctl setoption general:gaps_in 10\n";
        return 1;
    }
    std::string req = R"({"command":"setoption","option":")" + args[1] +
                      R"(","value":")" + args[2] + R"("})";
    return sendAndPrint(req);
}

int EternalCtl::cmdSwitchLayout(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cerr << "eternalctl switchlayout: expected layout name\n";
        std::cerr << "Usage: eternalctl switchlayout <name>\n";
        return 1;
    }
    std::string req = R"({"command":"dispatch","args":{"dispatcher":"switchlayout","params":")" +
                      args[1] + R"("}})";
    return sendAndPrint(req);
}

int EternalCtl::cmdScroll(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cerr << "eternalctl scroll: expected direction (left/right/up/down)\n";
        return 1;
    }
    std::string dir = args[1];
    std::string dispatcher;
    if (dir == "left")       dispatcher = "scrollleft";
    else if (dir == "right") dispatcher = "scrollright";
    else if (dir == "up")    dispatcher = "scrollup";
    else if (dir == "down")  dispatcher = "scrolldown";
    else {
        std::cerr << "eternalctl scroll: invalid direction '" << dir << "'\n";
        std::cerr << "Use: left, right, up, down\n";
        return 1;
    }
    std::string req = R"({"command":"dispatch","args":{"dispatcher":")" +
                      dispatcher + R"(","params":""}})";
    return sendAndPrint(req);
}

int EternalCtl::cmdSubscribe(const std::vector<std::string>& args) {
    std::string types = "all";
    if (args.size() >= 2) {
        types.clear();
        for (size_t i = 1; i < args.size(); ++i) {
            if (!types.empty()) types += ",";
            types += args[i];
        }
    }

    auto eventPrinter = [](const std::string& event, void* userdata) {
        auto* self = static_cast<EternalCtl*>(userdata);
        self->printResponse(event);
    };

    ipc_.subscribe(types, eventPrinter, this);
    return 0;
}

int EternalCtl::cmdDispatchers() {
    // List all known dispatchers
    std::vector<std::string> dispatchers = {
        "exec", "killactive", "closewindow", "movewindow", "resizewindow",
        "fullscreen", "maximize", "togglefloating", "pin", "focuswindow",
        "swapwindow", "centerwindow", "setopacity", "workspace",
        "movetoworkspace", "movetoworkspacesilent", "togglespecialworkspace",
        "focusmonitor", "cyclelayout", "switchlayout",
        "scrollleft", "scrollright", "scrollup", "scrolldown",
        "centercolumn", "setcolumnwidth", "togglegroup",
        "movewindowpixel", "resizewindowpixel", "movefocus",
        "reload", "exit", "lockscreen", "screenshot",
        "toggleoverview", "zoomin", "zoomout",
    };

    if (json_output_) {
        std::cout << R"({"ok":true,"data":[)";
        for (size_t i = 0; i < dispatchers.size(); ++i) {
            if (i > 0) std::cout << ",";
            std::cout << "\"" << dispatchers[i] << "\"";
        }
        std::cout << "]}\n";
    } else {
        std::cout << "Available dispatchers:\n";
        for (const auto& d : dispatchers)
            std::cout << "  " << d << "\n";
    }
    return 0;
}

// ===========================================================================
// Output helpers
// ===========================================================================

void EternalCtl::printUsage() const {
    std::cerr <<
        "Usage: eternalctl [flags] <command> [args...]\n"
        "\n"
        "Commands:\n"
        "  dispatch <name> [args]  Execute a dispatcher command\n"
        "  monitors                List connected monitors\n"
        "  workspaces              List workspaces\n"
        "  windows                 List all windows\n"
        "  activewindow            Show focused window info\n"
        "  layers                  List layer surfaces\n"
        "  version                 Print version info\n"
        "  reload                  Reload configuration\n"
        "  kill                    Kill focused window\n"
        "  getoption <name>        Get a config option value\n"
        "  setoption <name> <val>  Set a config option at runtime\n"
        "  switchlayout <name>     Switch to a named layout\n"
        "  scroll <direction>      Scroll workspace (left/right/up/down)\n"
        "  subscribe [types]       Subscribe to events (window,workspace,monitor,config,all)\n"
        "  dispatchers             List all available dispatchers\n"
        "  help                    Show this help\n"
        "\n"
        "Flags:\n"
        "  -j, --json              Force JSON output\n"
        "  -q, --quiet             Suppress output on success\n"
        "  -s, --socket <path>     Use a specific socket path\n"
        "  -h, --help              Show this help\n"
        "\n"
        "Examples:\n"
        "  eternalctl dispatch exec alacritty\n"
        "  eternalctl dispatch workspace 3\n"
        "  eternalctl --json monitors\n"
        "  eternalctl getoption general:gaps_in\n"
        "  eternalctl setoption general:gaps_in 10\n"
        "  eternalctl subscribe window workspace\n";
}

void EternalCtl::printResponse(const std::string& json) const {
    if (json.empty()) return;
    if (quiet_ && json.find("\"ok\":true") != std::string::npos)
        return;
    if (json_output_) {
        std::cout << json << "\n";
    } else {
        printPlainText(json);
    }
}

void EternalCtl::printPlainText(const std::string& json) const {
    // Simple plain text formatter for common responses
    // For events, just print the raw JSON
    if (json.find("\"event\"") != std::string::npos) {
        std::cout << json << "\n";
        return;
    }

    // Check for error
    if (json.find("\"ok\":false") != std::string::npos) {
        // Extract error message
        auto pos = json.find("\"error\":\"");
        if (pos != std::string::npos) {
            pos += 9;
            auto end = json.find('"', pos);
            if (end != std::string::npos) {
                std::cerr << "Error: " << json.substr(pos, end - pos) << "\n";
                return;
            }
        }
        std::cerr << json << "\n";
        return;
    }

    // For ok responses, print the data portion nicely
    // Simple extraction: find "data": and print what follows
    auto dataPos = json.find("\"data\":");
    if (dataPos != std::string::npos) {
        std::string data = json.substr(dataPos + 7);
        // Remove trailing }
        if (!data.empty() && data.back() == '}')
            data.pop_back();

        // If data is an array, format each element
        if (!data.empty() && data.front() == '[') {
            std::cout << data << "\n";
        } else if (!data.empty() && data.front() == '{') {
            // Object: print key-value pairs
            std::cout << data << "\n";
        } else if (!data.empty() && data.front() == '"') {
            // String value
            std::string val = data.substr(1);
            if (!val.empty() && val.back() == '"')
                val.pop_back();
            std::cout << val << "\n";
        } else {
            std::cout << data << "\n";
        }
        return;
    }

    // Fallback: print raw JSON
    std::cout << json << "\n";
}

} // namespace eternal::cli

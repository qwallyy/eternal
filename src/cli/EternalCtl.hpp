#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace eternal::cli {

// ---------------------------------------------------------------------------
// IPC connection wrapper -- Task 65
// ---------------------------------------------------------------------------

class IPCClient {
public:
    IPCClient();
    ~IPCClient();

    IPCClient(const IPCClient&) = delete;
    IPCClient& operator=(const IPCClient&) = delete;

    /// Connect to the compositor's IPC socket.
    /// Returns true on success.
    bool connect();

    /// Connect to a specific socket path.
    bool connect(const std::string& path);

    /// Send a JSON request and receive the JSON response (length-prefixed).
    [[nodiscard]] std::string request(std::string_view message);

    /// Send a subscribe request and then continuously read events,
    /// calling the callback for each event received.
    /// Returns when the connection is closed.
    using EventCallback = void(*)(const std::string& event, void* userdata);
    void subscribe(const std::string& eventTypes, EventCallback callback, void* userdata);

    /// Disconnect from the socket.
    void disconnect();

    [[nodiscard]] bool isConnected() const noexcept { return fd_ >= 0; }
    [[nodiscard]] const std::string& getSocketPath() const noexcept { return socket_path_; }

private:
    /// Determine socket path from environment.
    void detectSocketPath();

    int fd_ = -1;
    std::string socket_path_;
};

// ---------------------------------------------------------------------------
// EternalCtl - the command-line controller -- Task 65
// ---------------------------------------------------------------------------

class EternalCtl {
public:
    EternalCtl();
    ~EternalCtl() = default;

    /// Parse argv and execute the appropriate command.
    /// Returns the process exit code.
    int run(int argc, char* argv[]);

private:
    // Individual commands
    int cmdDispatch(const std::vector<std::string>& args);
    int cmdMonitors();
    int cmdWorkspaces();
    int cmdWindows();
    int cmdActiveWindow();
    int cmdLayers();
    int cmdVersion();
    int cmdReload();
    int cmdKill();
    int cmdSplash();
    int cmdGetOption(const std::vector<std::string>& args);
    int cmdSetOption(const std::vector<std::string>& args);
    int cmdSwitchLayout(const std::vector<std::string>& args);
    int cmdScroll(const std::vector<std::string>& args);
    int cmdSubscribe(const std::vector<std::string>& args);
    int cmdDispatchers();

    /// Build a JSON request object
    std::string makeRequest(const std::string& command,
                            const std::string& argsJson = "{}") const;

    /// Send a request and print the response
    int sendAndPrint(const std::string& request);

    void printUsage() const;
    void printResponse(const std::string& json) const;

    /// Format JSON for human-readable plain text output
    void printPlainText(const std::string& json) const;

    IPCClient ipc_;
    bool json_output_ = false;
    bool quiet_ = false;
};

} // namespace eternal::cli

#include "eternal/core/Server.hpp"
#include "eternal/utils/Logger.hpp"

extern "C" {
#include <wlr/util/log.h>
}

#include <cstring>
#include <cstdlib>
#include <iostream>
#include <sys/stat.h>
#include <string>
#include <unistd.h>

static bool envEnabled(const char* name) {
    const char* value = std::getenv(name);
    return value && std::strcmp(value, "1") == 0;
}

static bool hasEnvValue(const char* name) {
    const char* value = std::getenv(name);
    return value && value[0] != '\0';
}

static bool isUsableSeatdSocket(const char* path) {
    struct stat st {};
    if (!path || path[0] == '\0') {
        return false;
    }
    if (stat(path, &st) != 0 || !S_ISSOCK(st.st_mode)) {
        return false;
    }
    return access(path, R_OK | W_OK) == 0;
}

static void chooseSeatBackend() {
    const char* current_backend = std::getenv("LIBSEAT_BACKEND");
    const char* seatd_sock = std::getenv("SEATD_SOCK");
    if (!seatd_sock || seatd_sock[0] == '\0') {
        seatd_sock = "/run/seatd.sock";
    }

    const bool has_logind_session = hasEnvValue("XDG_SESSION_ID");
    const bool usable_seatd_socket = isUsableSeatdSocket(seatd_sock);

    if (current_backend && std::strcmp(current_backend, "seatd") == 0 &&
        !usable_seatd_socket && !envEnabled("ETERNAL_FORCE_SEATD")) {
        if (has_logind_session) {
            LOG_WARN("seatd backend requested but {} is not usable; falling back to logind",
                     seatd_sock);
            setenv("LIBSEAT_BACKEND", "logind", 1);
        } else {
            LOG_WARN("seatd backend requested but {} is not usable; clearing override",
                     seatd_sock);
            unsetenv("LIBSEAT_BACKEND");
        }
        return;
    }

    if (!current_backend && has_logind_session) {
        setenv("LIBSEAT_BACKEND", "logind", 1);
        LOG_INFO("Using logind seat backend for session {}", std::getenv("XDG_SESSION_ID"));
    }
}

static void printUsage(const char* argv0) {
    std::cerr << "Usage: " << argv0 << " [options]\n"
              << "\n"
              << "Options:\n"
              << "  -c, --config <path>   Path to configuration file\n"
              << "  -d, --debug           Enable debug logging\n"
              << "  --no-xwayland         Disable XWayland support\n"
              << "  -v, --version         Print version and exit\n"
              << "  -h, --help            Show this help\n";
}

int main(int argc, char* argv[]) {
    using namespace eternal;

    std::string config_path;
    bool debug = false;
    bool enable_xwayland = true;

    // -----------------------------------------------------------------------
    // Parse command-line arguments
    // -----------------------------------------------------------------------
    for (int i = 1; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "-c" || arg == "--config") {
            if (i + 1 < argc) config_path = argv[++i];
        } else if (arg == "-d" || arg == "--debug") {
            debug = true;
        } else if (arg == "--no-xwayland") {
            enable_xwayland = false;
        } else if (arg == "-v" || arg == "--version") {
            std::cout << "eternal 0.1.0\n";
            return 0;
        } else if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    // -----------------------------------------------------------------------
    // Configure logging
    // -----------------------------------------------------------------------
    Logger::instance().enableColors(true);
    Logger::instance().setLevel(debug ? LogLevel::Debug : LogLevel::Info);

    LOG_INFO("Starting Eternal compositor v0.1.0");

    // Avoid hard-lock behavior when users accidentally launch the compositor
    // from inside an existing X11/Wayland desktop session.
    const bool allow_nested = envEnabled("ETERNAL_ALLOW_NESTED");
    const bool has_x11 = []() {
        const char* env = std::getenv("DISPLAY");
        return env && env[0] != '\0';
    }();
    const bool has_wayland = []() {
        const char* env = std::getenv("WAYLAND_DISPLAY");
        return env && env[0] != '\0';
    }();
    if (!allow_nested && (has_x11 || has_wayland)) {
        LOG_CRIT("Refusing to start inside an existing graphical session. "
                 "Use a TTY or login manager session. "
                 "Set ETERNAL_ALLOW_NESTED=1 to override.");
        return 1;
    }

    // -----------------------------------------------------------------------
    // Initialise wlroots
    // -----------------------------------------------------------------------
    chooseSeatBackend();
    wlr_log_init(debug ? WLR_DEBUG : WLR_INFO, nullptr);

    (void)enable_xwayland; // TODO: thread this into the runtime server

    Server server;
    if (!server.init()) {
        LOG_CRIT("Failed to initialize compositor server");
        return 1;
    }

    // -----------------------------------------------------------------------
    // Load configuration
    // -----------------------------------------------------------------------
    if (!config_path.empty()) {
        LOG_INFO("Loading config from {}", config_path);
    }

    server.run();
    return 0;
}

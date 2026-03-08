#include "eternal/utils/Logger.hpp"

extern "C" {
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
}

#include <cstring>
#include <cstdlib>
#include <iostream>
#include <string>

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
    const bool allow_nested = []() {
        const char* env = std::getenv("ETERNAL_ALLOW_NESTED");
        return env && std::strcmp(env, "1") == 0;
    }();
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
    wlr_log_init(debug ? WLR_DEBUG : WLR_INFO, nullptr);

    wl_display* display = wl_display_create();
    if (!display) {
        LOG_CRIT("Failed to create wl_display");
        return 1;
    }

    wlr_backend* backend = wlr_backend_autocreate(
        wl_display_get_event_loop(display), nullptr);
    if (!backend) {
        LOG_CRIT("Failed to create wlr_backend");
        wl_display_destroy(display);
        return 1;
    }

    wlr_renderer* renderer = wlr_renderer_autocreate(backend);
    if (!renderer) {
        LOG_CRIT("Failed to create wlr_renderer");
        wl_display_destroy(display);
        return 1;
    }
    wlr_renderer_init_wl_display(renderer, display);

    wlr_allocator* allocator = wlr_allocator_autocreate(backend, renderer);
    if (!allocator) {
        LOG_CRIT("Failed to create wlr_allocator");
        wl_display_destroy(display);
        return 1;
    }

    wlr_compositor_create(display, 5, renderer);
    wlr_subcompositor_create(display);
    wlr_data_device_manager_create(display);

    wlr_output_layout* output_layout = wlr_output_layout_create(display);

    wlr_seat* seat = wlr_seat_create(display, "seat0");
    (void)seat;

    wlr_xdg_shell* xdg_shell = wlr_xdg_shell_create(display, 3);
    (void)xdg_shell;

    (void)enable_xwayland; // TODO: pass to XWaylandManager

    // -----------------------------------------------------------------------
    // Load configuration
    // -----------------------------------------------------------------------
    if (!config_path.empty()) {
        LOG_INFO("Loading config from {}", config_path);
        // TODO: parse config, populate rules, load plugins
    }

    // -----------------------------------------------------------------------
    // Open Wayland socket and run
    // -----------------------------------------------------------------------
    const char* socket = wl_display_add_socket_auto(display);
    if (!socket) {
        LOG_CRIT("Failed to open Wayland socket");
        wl_display_destroy(display);
        return 1;
    }

    if (!wlr_backend_start(backend)) {
        LOG_CRIT("Failed to start backend");
        wl_display_destroy(display);
        return 1;
    }

    setenv("WAYLAND_DISPLAY", socket, 1);
    LOG_INFO("Running on WAYLAND_DISPLAY={}", socket);

    wl_display_run(display);

    // -----------------------------------------------------------------------
    // Cleanup
    // -----------------------------------------------------------------------
    LOG_INFO("Shutting down");
    wl_display_destroy_clients(display);
    wl_display_destroy(display);

    (void)output_layout;

    return 0;
}

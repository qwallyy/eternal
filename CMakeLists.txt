cmake_minimum_required(VERSION 3.20)
project(eternal
    VERSION 0.1.0
    DESCRIPTION "Eternal - A modern Wayland compositor"
    LANGUAGES C CXX
)

# ── C++ standard ──────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Build type defaults ───────────────────────────────────────────────────────
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

# ── Options ───────────────────────────────────────────────────────────────────
option(ETERNAL_ENABLE_XWAYLAND  "Enable XWayland support"       ON)
option(ETERNAL_ENABLE_IPC       "Enable IPC server"             ON)
option(ETERNAL_ENABLE_PLUGINS   "Enable plugin system"          ON)
option(ETERNAL_ENABLE_SCREENCOPY "Enable screenshot/recording"  ON)
option(ETERNAL_BUILD_CLI        "Build eternalctl CLI tool"     ON)
option(ETERNAL_BUILD_TESTS      "Build test suite"              ON)

# ── Sanitizer configuration (Task 105) ───────────────────────────────────────
include(tests/sanitizer.cmake)

# ── Find dependencies ────────────────────────────────────────────────────────
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

pkg_check_modules(WAYLAND_SERVER   REQUIRED IMPORTED_TARGET wayland-server)
pkg_check_modules(WAYLAND_CLIENT   REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(WAYLAND_PROTOS   REQUIRED IMPORTED_TARGET wayland-protocols)
pkg_check_modules(WAYLAND_SCANNER  REQUIRED wayland-scanner)
pkg_check_modules(WLROOTS          REQUIRED IMPORTED_TARGET wlroots-0.18>=0.18.0)
pkg_check_modules(XKBCOMMON        REQUIRED IMPORTED_TARGET xkbcommon)
pkg_check_modules(LIBINPUT         REQUIRED IMPORTED_TARGET libinput)
pkg_check_modules(PIXMAN           REQUIRED IMPORTED_TARGET pixman-1)
pkg_check_modules(LIBDRM           REQUIRED IMPORTED_TARGET libdrm)
pkg_check_modules(EGL              REQUIRED IMPORTED_TARGET egl)
pkg_check_modules(GLES2            REQUIRED IMPORTED_TARGET glesv2)
pkg_check_modules(LIBPNG           REQUIRED IMPORTED_TARGET libpng)

if(ETERNAL_ENABLE_XWAYLAND)
    pkg_check_modules(XWAYLAND     IMPORTED_TARGET xcb)
    if(NOT XWAYLAND_FOUND)
        message(WARNING "XCB not found -- XWayland support disabled")
        set(ETERNAL_ENABLE_XWAYLAND OFF)
    endif()
endif()

# ── Wayland protocol code generation ─────────────────────────────────────────
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
pkg_get_variable(WAYLAND_SCANNER_BIN  wayland-scanner   wayland_scanner)

set(PROTO_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/protocols")
file(MAKE_DIRECTORY "${PROTO_GEN_DIR}")

# Helper function: generate C header + source from a Wayland XML protocol
function(eternal_wayland_protocol _name _xml)
    set(_header "${PROTO_GEN_DIR}/${_name}-protocol.h")
    set(_source "${PROTO_GEN_DIR}/${_name}-protocol.c")

    add_custom_command(
        OUTPUT "${_header}"
        COMMAND "${WAYLAND_SCANNER_BIN}" server-header "${_xml}" "${_header}"
        DEPENDS "${_xml}"
        COMMENT "Generating ${_name} server header"
        VERBATIM
    )
    add_custom_command(
        OUTPUT "${_source}"
        COMMAND "${WAYLAND_SCANNER_BIN}" private-code "${_xml}" "${_source}"
        DEPENDS "${_xml}"
        COMMENT "Generating ${_name} private code"
        VERBATIM
    )

    # Expose filenames to the caller
    set(${_name}_PROTO_HEADER "${_header}" PARENT_SCOPE)
    set(${_name}_PROTO_SOURCE "${_source}" PARENT_SCOPE)
endfunction()

# Stable protocols
eternal_wayland_protocol(xdg-shell
    "${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml")
eternal_wayland_protocol(linux-dmabuf-v1
    "${WAYLAND_PROTOCOLS_DIR}/stable/linux-dmabuf/linux-dmabuf-v1.xml")
eternal_wayland_protocol(viewporter
    "${WAYLAND_PROTOCOLS_DIR}/stable/viewporter/viewporter.xml")
eternal_wayland_protocol(presentation-time
    "${WAYLAND_PROTOCOLS_DIR}/stable/presentation-time/presentation-time.xml")

# Staging / unstable protocols
eternal_wayland_protocol(xdg-decoration-v1
    "${WAYLAND_PROTOCOLS_DIR}/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml")
eternal_wayland_protocol(ext-session-lock-v1
    "${WAYLAND_PROTOCOLS_DIR}/staging/ext-session-lock/ext-session-lock-v1.xml")
eternal_wayland_protocol(cursor-shape-v1
    "${WAYLAND_PROTOCOLS_DIR}/staging/cursor-shape/cursor-shape-v1.xml")
eternal_wayland_protocol(fractional-scale-v1
    "${WAYLAND_PROTOCOLS_DIR}/staging/fractional-scale/fractional-scale-v1.xml")
eternal_wayland_protocol(tablet-v2
    "${WAYLAND_PROTOCOLS_DIR}/stable/tablet/tablet-v2.xml")

set(PROTO_GENERATED_SOURCES
    ${xdg-shell_PROTO_HEADER}              ${xdg-shell_PROTO_SOURCE}
    ${linux-dmabuf-v1_PROTO_HEADER}        ${linux-dmabuf-v1_PROTO_SOURCE}
    ${viewporter_PROTO_HEADER}             ${viewporter_PROTO_SOURCE}
    ${presentation-time_PROTO_HEADER}      ${presentation-time_PROTO_SOURCE}
    ${xdg-decoration-v1_PROTO_HEADER}      ${xdg-decoration-v1_PROTO_SOURCE}
    ${ext-session-lock-v1_PROTO_HEADER}    ${ext-session-lock-v1_PROTO_SOURCE}
    ${cursor-shape-v1_PROTO_HEADER}        ${cursor-shape-v1_PROTO_SOURCE}
    ${fractional-scale-v1_PROTO_HEADER}    ${fractional-scale-v1_PROTO_SOURCE}
    ${tablet-v2_PROTO_HEADER}              ${tablet-v2_PROTO_SOURCE}
)

# ── Source files ──────────────────────────────────────────────────────────────
set(ETERNAL_CORE_SOURCES
    src/core/Server.cpp
    src/core/Compositor.cpp
    src/core/Output.cpp
    src/core/OutputManager.cpp
    src/core/OutputMirror.cpp
    src/core/Surface.cpp
    src/core/Seat.cpp
    src/core/LayerSurface.cpp
)

set(ETERNAL_CONFIG_SOURCES
    src/config/Config.cpp
    src/config/KDLParser.cpp
    src/config/ConfigManager.cpp
)

set(ETERNAL_INPUT_SOURCES
    src/input/InputManager.cpp
    src/input/KeybindManager.cpp
    src/input/Keyboard.cpp
    src/input/Pointer.cpp
    src/input/Touch.cpp
    src/input/Tablet.cpp
)

set(ETERNAL_LAYOUT_SOURCES
    src/layout/WindowNode.cpp
    src/layout/LayoutManager.cpp
    src/layout/ScrollableLayout.cpp
    src/layout/DwindleLayout.cpp
    src/layout/MasterLayout.cpp
    src/layout/MonocleLayout.cpp
    src/layout/FloatingLayout.cpp
    src/layout/GridLayout.cpp
    src/layout/SpiralLayout.cpp
    src/layout/ColumnsLayout.cpp
)

set(ETERNAL_RENDER_SOURCES
    src/render/Renderer.cpp
    src/render/Effects.cpp
    src/render/DualKawaseBlur.cpp
    src/render/Shaders.cpp
    src/render/RenderThread.cpp
    src/render/DirectScanout.cpp
)

set(ETERNAL_WORKSPACE_SOURCES
    src/workspace/WorkspaceManager.cpp
    src/workspace/Workspace.cpp
)

set(ETERNAL_ANIMATION_SOURCES
    src/animation/AnimationEngine.cpp
    src/animation/SpringAnimation.cpp
    src/animation/BezierCurve.cpp
    src/animation/Animation.cpp
    src/animation/Timeline.cpp
)

set(ETERNAL_DECORATION_SOURCES
    src/decoration/Border.cpp
    src/decoration/Blur.cpp
    src/decoration/Shadow.cpp
    src/decoration/DecorationManager.cpp
    src/decoration/RoundedCorners.cpp
)

set(ETERNAL_RULES_SOURCES
    src/rules/WindowRules.cpp
    src/rules/WorkspaceRules.cpp
)

set(ETERNAL_GESTURES_SOURCES
    src/gestures/GestureRecognizer.cpp
    src/gestures/TrackpadGestures.cpp
    src/gestures/TouchGestures.cpp
)

set(ETERNAL_UTILS_SOURCES
    src/utils/Logger.cpp
    src/utils/Math.cpp
    src/utils/Color.cpp
    src/utils/Signal.cpp
    src/utils/Profiler.cpp
)

set(ETERNAL_WM_SOURCES
    src/wm/WindowManager.cpp
    src/wm/View.cpp
)

set(ETERNAL_PROTOCOL_SOURCES
    src/protocols/Clipboard.cpp
    src/protocols/DesktopPortal.cpp
    src/protocols/DragDrop.cpp
    src/protocols/IdleManager.cpp
    src/protocols/SessionLock.cpp
)

# Conditional subsystems
set(ETERNAL_IPC_SOURCES "")
if(ETERNAL_ENABLE_IPC)
    set(ETERNAL_IPC_SOURCES
        src/ipc/IPCServer.cpp
        src/ipc/IPCCommands.cpp
    )
endif()

set(ETERNAL_PLUGIN_SOURCES "")
if(ETERNAL_ENABLE_PLUGINS)
    set(ETERNAL_PLUGIN_SOURCES
        src/plugins/PluginManager.cpp
        src/plugins/PluginAPI.cpp
    )
endif()

set(ETERNAL_SCREENSHOT_SOURCES "")
if(ETERNAL_ENABLE_SCREENCOPY)
    set(ETERNAL_SCREENSHOT_SOURCES
        src/screenshot/Screenshot.cpp
        src/screenshot/ScreenRecorder.cpp
    )
endif()

set(ETERNAL_XWAYLAND_SOURCES "")
if(ETERNAL_ENABLE_XWAYLAND)
    set(ETERNAL_XWAYLAND_SOURCES
        src/xwayland/XWaylandManager.cpp
    )
endif()

# ── Main compositor executable ────────────────────────────────────────────────
add_executable(eternal
    src/main.cpp
    ${ETERNAL_CORE_SOURCES}
    ${ETERNAL_CONFIG_SOURCES}
    ${ETERNAL_INPUT_SOURCES}
    ${ETERNAL_LAYOUT_SOURCES}
    ${ETERNAL_RENDER_SOURCES}
    ${ETERNAL_WORKSPACE_SOURCES}
    ${ETERNAL_ANIMATION_SOURCES}
    ${ETERNAL_DECORATION_SOURCES}
    ${ETERNAL_RULES_SOURCES}
    ${ETERNAL_GESTURES_SOURCES}
    ${ETERNAL_UTILS_SOURCES}
    ${ETERNAL_WM_SOURCES}
    ${ETERNAL_PROTOCOL_SOURCES}
    ${ETERNAL_IPC_SOURCES}
    ${ETERNAL_PLUGIN_SOURCES}
    ${ETERNAL_SCREENSHOT_SOURCES}
    ${ETERNAL_XWAYLAND_SOURCES}
    ${PROTO_GENERATED_SOURCES}
)

target_include_directories(eternal PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PROTO_GEN_DIR}
)

target_compile_definitions(eternal PRIVATE
    WLR_USE_UNSTABLE
    ETERNAL_VERSION="${PROJECT_VERSION}"
    $<$<BOOL:${ETERNAL_ENABLE_XWAYLAND}>:ETERNAL_HAS_XWAYLAND>
    $<$<BOOL:${ETERNAL_ENABLE_IPC}>:ETERNAL_HAS_IPC>
    $<$<BOOL:${ETERNAL_ENABLE_PLUGINS}>:ETERNAL_HAS_PLUGINS>
    $<$<BOOL:${ETERNAL_ENABLE_SCREENCOPY}>:ETERNAL_HAS_SCREENCOPY>
)

target_compile_options(eternal PRIVATE
    -Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-missing-field-initializers
    $<$<CONFIG:Debug>:-g -O0>
    $<$<CONFIG:Release>:-O2>
)

target_link_libraries(eternal PRIVATE
    PkgConfig::WAYLAND_SERVER
    PkgConfig::WLROOTS
    PkgConfig::XKBCOMMON
    PkgConfig::LIBINPUT
    PkgConfig::PIXMAN
    PkgConfig::LIBDRM
    PkgConfig::EGL
    PkgConfig::GLES2
    Threads::Threads
    ${CMAKE_DL_LIBS}
    PkgConfig::LIBPNG
)

if(ETERNAL_ENABLE_XWAYLAND AND XWAYLAND_FOUND)
    target_link_libraries(eternal PRIVATE PkgConfig::XWAYLAND)
endif()

# ── Apply sanitizers (Task 105) ──────────────────────────────────────────────
eternal_apply_sanitizers(eternal)

# ── eternalctl CLI tool ──────────────────────────────────────────────────────
if(ETERNAL_BUILD_CLI AND ETERNAL_ENABLE_IPC)
    add_executable(eternalctl
        src/cli/eternalctl_main.cpp
        src/cli/EternalCtl.cpp
    )

    target_include_directories(eternalctl PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_compile_options(eternalctl PRIVATE
        -Wall -Wextra -Wpedantic
    )

    set_target_properties(eternalctl PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
    )

    target_compile_definitions(eternalctl PRIVATE
        ETERNAL_VERSION="${PROJECT_VERSION}"
    )
endif()

# ── Install rules ─────────────────────────────────────────────────────────────
include(GNUInstallDirs)

install(TARGETS eternal RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(ETERNAL_BUILD_CLI AND ETERNAL_ENABLE_IPC)
    install(TARGETS eternalctl RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

install(PROGRAMS packaging/eternal-session
    DESTINATION ${CMAKE_INSTALL_BINDIR}
    OPTIONAL
)

install(FILES config/eternal.kdl
    DESTINATION ${CMAKE_INSTALL_DATADIR}/eternal
    OPTIONAL
)

# Install desktop session entries (Task 110)
install(FILES
    packaging/eternal.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/wayland-sessions
    OPTIONAL
)

# Install man page (Task 110)
install(FILES docs/eternal.1
    DESTINATION ${CMAKE_INSTALL_MANDIR}/man1
    OPTIONAL
)

# ── Testing (Task 101) ──────────────────────────────────────────────────────
if(ETERNAL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ── Summary ───────────────────────────────────────────────────────────────────
message(STATUS "")
message(STATUS "  Eternal ${PROJECT_VERSION}")
message(STATUS "  ─────────────────────────────")
message(STATUS "  Build type:       ${CMAKE_BUILD_TYPE}")
message(STATUS "  XWayland:         ${ETERNAL_ENABLE_XWAYLAND}")
message(STATUS "  IPC:              ${ETERNAL_ENABLE_IPC}")
message(STATUS "  Plugins:          ${ETERNAL_ENABLE_PLUGINS}")
message(STATUS "  Screencopy:       ${ETERNAL_ENABLE_SCREENCOPY}")
message(STATUS "  CLI (eternalctl): ${ETERNAL_BUILD_CLI}")
message(STATUS "  Tests:            ${ETERNAL_BUILD_TESTS}")
message(STATUS "  ASAN:             ${ENABLE_ASAN}")
message(STATUS "  UBSAN:            ${ENABLE_UBSAN}")
message(STATUS "  TSAN:             ${ENABLE_TSAN}")
message(STATUS "  Install prefix:   ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")

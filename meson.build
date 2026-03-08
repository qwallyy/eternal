project('eternal', ['c', 'cpp'],
    version : '0.1.0',
    license : 'MIT',
    meson_version : '>=0.60.0',
    default_options : [
        'cpp_std=c++20',
        'c_std=c11',
        'warning_level=2',
        'werror=false',
        'buildtype=debugoptimized',
    ],
)

# ── Options ───────────────────────────────────────────────────────────────────
opt_xwayland  = get_option('xwayland')
opt_ipc       = get_option('ipc')
opt_plugins   = get_option('plugins')
opt_screencopy = get_option('screencopy')
opt_cli       = get_option('cli')

# ── Dependencies ──────────────────────────────────────────────────────────────
wayland_server  = dependency('wayland-server')
wayland_client  = dependency('wayland-client')
wayland_protos  = dependency('wayland-protocols', version : '>=1.32')
wlroots         = dependency('wlroots-0.18', version : '>=0.18.0')
xkbcommon       = dependency('xkbcommon')
libinput        = dependency('libinput')
pixman          = dependency('pixman-1')
libdrm          = dependency('libdrm')
egl             = dependency('egl')
glesv2          = dependency('glesv2')
threads         = dependency('threads')

eternal_deps = [
    wayland_server,
    wayland_client,
    wlroots,
    xkbcommon,
    libinput,
    pixman,
    libdrm,
    egl,
    glesv2,
    threads,
]

conf_data = configuration_data()
conf_data.set_quoted('ETERNAL_VERSION', meson.project_version())

if opt_xwayland
    xcb = dependency('xcb', required : false)
    if xcb.found()
        eternal_deps += xcb
        conf_data.set10('ETERNAL_HAS_XWAYLAND', true)
    else
        warning('XCB not found -- XWayland support disabled')
        opt_xwayland = false
    endif
endif

if opt_ipc
    conf_data.set10('ETERNAL_HAS_IPC', true)
endif

if opt_plugins
    conf_data.set10('ETERNAL_HAS_PLUGINS', true)
endif

if opt_screencopy
    conf_data.set10('ETERNAL_HAS_SCREENCOPY', true)
endif

# ── Wayland protocol generation ──────────────────────────────────────────────
wayland_scanner = find_program('wayland-scanner')
wl_protocol_dir = wayland_protos.get_variable('pkgdatadir')

protocols = {
    # Stable
    'xdg-shell':          wl_protocol_dir / 'stable/xdg-shell/xdg-shell.xml',
    'linux-dmabuf-v1':    wl_protocol_dir / 'stable/linux-dmabuf/linux-dmabuf-v1.xml',
    'viewporter':         wl_protocol_dir / 'stable/viewporter/viewporter.xml',
    'presentation-time':  wl_protocol_dir / 'stable/presentation-time/presentation-time.xml',
    # Staging
    'xdg-decoration-v1':  wl_protocol_dir / 'staging/xdg-decoration/xdg-decoration-v1.xml',
    'ext-session-lock-v1': wl_protocol_dir / 'staging/ext-session-lock/ext-session-lock-v1.xml',
    'cursor-shape-v1':    wl_protocol_dir / 'staging/cursor-shape/cursor-shape-v1.xml',
    'fractional-scale-v1': wl_protocol_dir / 'staging/fractional-scale/fractional-scale-v1.xml',
}

proto_sources = []
proto_headers = []

foreach name, xml : protocols
    proto_headers += custom_target(
        name + '-server-header',
        output  : name + '-protocol.h',
        input   : xml,
        command : [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
    )
    proto_sources += custom_target(
        name + '-private-code',
        output  : name + '-protocol.c',
        input   : xml,
        command : [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
    )
endforeach

# ── Source files ──────────────────────────────────────────────────────────────
eternal_sources = files(
    # main
    'src/main.cpp',

    # core
    'src/core/Server.cpp',
    'src/core/Compositor.cpp',
    'src/core/Output.cpp',
    'src/core/Surface.cpp',
    'src/core/Seat.cpp',

    # config
    'src/config/Config.cpp',
    'src/config/KDLParser.cpp',
    'src/config/ConfigManager.cpp',

    # input
    'src/input/Keyboard.cpp',
    'src/input/Pointer.cpp',
    'src/input/Touch.cpp',
    'src/input/Tablet.cpp',
    'src/input/Gestures.cpp',

    # layout
    'src/layout/Scrollable.cpp',
    'src/layout/Dwindle.cpp',
    'src/layout/Master.cpp',
    'src/layout/Monocle.cpp',
    'src/layout/Floating.cpp',
    'src/layout/Grid.cpp',
    'src/layout/Spiral.cpp',
    'src/layout/Columns.cpp',

    # render
    'src/render/Renderer.cpp',
    'src/render/Effects.cpp',
    'src/render/Blur.cpp',
    'src/render/Shadows.cpp',

    # workspace
    'src/workspace/WorkspaceManager.cpp',
    'src/workspace/Workspace.cpp',

    # animation
    'src/animation/AnimationEngine.cpp',
    'src/animation/Spring.cpp',
    'src/animation/Bezier.cpp',
    'src/animation/Timeline.cpp',

    # decoration
    'src/decoration/Borders.cpp',
    'src/decoration/Titlebar.cpp',
    'src/decoration/DecorationShadows.cpp',
    'src/decoration/RoundedCorners.cpp',

    # rules
    'src/rules/WindowRules.cpp',
    'src/rules/WorkspaceRules.cpp',

    # gestures
    'src/gestures/GestureRecognizer.cpp',
    'src/gestures/Trackpad.cpp',
    'src/gestures/Touchscreen.cpp',

    # utils
    'src/utils/Logger.cpp',
    'src/utils/Math.cpp',
    'src/utils/Color.cpp',
    'src/utils/Signal.cpp',

    # window manager (legacy / bridge)
    'src/wm/WindowManager.cpp',
    'src/wm/View.cpp',
)

if opt_ipc
    eternal_sources += files(
        'src/ipc/IPCServer.cpp',
        'src/ipc/Commands.cpp',
    )
endif

if opt_plugins
    eternal_sources += files(
        'src/plugins/PluginManager.cpp',
        'src/plugins/PluginAPI.cpp',
    )
endif

if opt_screencopy
    eternal_sources += files(
        'src/screenshot/Screenshot.cpp',
        'src/screenshot/ScreenRecording.cpp',
    )
endif

if opt_xwayland
    eternal_sources += files(
        'src/xwayland/XWaylandManager.cpp',
    )
endif

# ── Compiler flags ────────────────────────────────────────────────────────────
add_project_arguments(
    '-DWLR_USE_UNSTABLE',
    language : ['c', 'cpp'],
)

eternal_inc = include_directories('src', 'include')

# ── Main executable ──────────────────────────────────────────────────────────
executable('eternal',
    eternal_sources, proto_sources, proto_headers,
    include_directories : eternal_inc,
    dependencies        : eternal_deps,
    install             : true,
    cpp_args            : ['-DETERNAL_VERSION="' + meson.project_version() + '"'],
)

# ── eternalctl CLI ───────────────────────────────────────────────────────────
if opt_cli and opt_ipc
    executable('eternalctl',
        files('src/cli/eternalctl.cpp'),
        include_directories : eternal_inc,
        install             : true,
        cpp_args            : ['-DETERNAL_VERSION="' + meson.project_version() + '"'],
    )
endif

# ── Install data ─────────────────────────────────────────────────────────────
install_data('config/eternal.kdl',
    install_dir : get_option('datadir') / 'eternal',
    install_tag : 'runtime',
)

# ── Summary ──────────────────────────────────────────────────────────────────
summary({
    'XWayland'  : opt_xwayland,
    'IPC'       : opt_ipc,
    'Plugins'   : opt_plugins,
    'Screencopy': opt_screencopy,
    'CLI'       : opt_cli,
}, section : 'Features')

# eternal

A scrollable-tiling Wayland compositor. Takes the infinite horizontal strip from [niri](https://github.com/YaLTeR/niri) and the dynamic tiling + eye candy from [Hyprland](https://github.com/hyprwm/Hyprland), mashes them together into one thing.

Built on [wlroots](https://gitlab.freedesktop.org/wlroots/wlroots) 0.18, C++20.

> **Status:** early development. It runs, renders a background, handles outputs and input devices. Window management and most features are implemented but still being wired together. Not daily-driver ready yet.

## Why

- niri's scrollable layout is great but it only does scrolling
- Hyprland has every layout and effect you'd want but no scrolling mode
- Neither lets you seamlessly switch between scrollable, dwindle, master, grid, etc. per workspace
- So here we are

## Building

Dependencies (Debian/Ubuntu):

```sh
sudo apt install build-essential cmake pkg-config libwayland-dev libwlroots-dev \
  wayland-protocols libxkbcommon-dev libinput-dev libpixman-1-dev libdrm-dev \
  libegl-dev libgles2-mesa-dev libxcb1-dev
```

Arch:

```sh
sudo pacman -S wlroots wayland wayland-protocols libinput libxkbcommon pixman libdrm mesa
```

Build:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Run (from a TTY, not inside another compositor):

```sh
./build/eternal
```

Or test with the headless backend:

```sh
WLR_BACKENDS=headless WLR_RENDERER=pixman ./build/eternal
```

## Configuration

Uses [KDL](https://kdl.dev/) format. Default config location: `~/.config/eternal/eternal.kdl`

A sample config is in `config/eternal.kdl`. Config reloads live when saved -- bad syntax won't crash the compositor, it just keeps the last working config.

```kdl
general {
    layout "scrollable"
    border-size 2
    gaps-in 5
    gaps-out 10
    focus-follows-mouse true
}

decoration {
    rounding 10
    blur {
        enabled true
        size 8
        passes 3
    }
    shadow {
        enabled true
        range 15
        color "rgba(0,0,0,0.55)"
    }
}

binds {
    bind "super" "return" "exec" "kitty"
    bind "super" "q" "killactive"
    bind "super" "space" "cyclelayout"
    bind "super" "1" "workspace" "1"
    bind "super+shift" "1" "movetoworkspace" "1"
    bindm "super" "mouse:272" "movewindow"
    bindm "super" "mouse:273" "resizewindow"
}
```

See the full sample config for all options.

## Layouts

Switch layouts per-workspace or cycle through them at runtime.

- **scrollable** -- niri-style. Windows in columns on an infinite horizontal strip. New windows never resize existing ones. Spring physics scrolling.
- **dwindle** -- BSP tree, each split alternates direction. The Hyprland classic.
- **master** -- one master area + stack. Configurable orientation (left/right/top/bottom/center).
- **monocle** -- one window fullscreen at a time, cycle through the stack.
- **floating** -- free placement with edge and window snapping.
- **grid** -- auto-sized grid based on window count.
- **spiral** -- fibonacci golden ratio splits.
- **columns** -- fixed N columns, windows stack within each.

## IPC

Unix socket at `$XDG_RUNTIME_DIR/eternal/$WAYLAND_DISPLAY.sock`. JSON protocol.

```sh
# built-in CLI
./build/eternalctl dispatch killactive
./build/eternalctl windows
./build/eternalctl monitors
./build/eternalctl workspaces
./build/eternalctl version
```

90+ dispatchers -- `exec`, `killactive`, `movewindow`, `workspace`, `fullscreen`, `togglefloating`, `cyclelayout`, `scrollleft`, `scrollright`, `centercolumn`, `toggleoverview`, `screenshot`, etc.

## Project structure

```
src/
  core/         server, output, surface, seat, layer shell
  config/       KDL parser, config manager
  layout/       all 8 layout engines + window node tree
  animation/    bezier curves, spring physics, timeline
  decoration/   borders, shadows, blur, rounded corners
  input/        keyboard, pointer, touch, tablet, keybinds
  gestures/     trackpad swipe/pinch, touch gestures
  workspace/    dynamic workspaces, per-monitor stacks
  ipc/          unix socket server, dispatchers
  render/       renderer, shaders, effects, direct scanout
  plugins/      dlopen plugin system, scripting bridge
  protocols/    clipboard, drag-drop, idle, session lock
  screenshot/   capture, pipewire recording
  xwayland/     X11 compat
include/eternal/  headers for all of the above
config/           default eternal.kdl
tests/            catch2 tests
packaging/        PKGBUILD, rpm spec, nix flake
```

## Plugins

Plugins are shared libraries loaded at runtime.

```cpp
#include <eternal/plugins/PluginAPI.hpp>

ETERNAL_PLUGIN_INFO {
    .name = "my-plugin",
    .version = "0.1.0",
    .author = "you",
};

ETERNAL_PLUGIN_INIT(api) {
    api->registerDispatcher("greet", [](const std::string& args) {
        return "{\"ok\":true}";
    });
}
```

## License

GPLv3. See [LICENSE](LICENSE).

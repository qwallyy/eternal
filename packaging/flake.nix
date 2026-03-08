{
  description = "Eternal - A modern Wayland compositor";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = import nixpkgs { inherit system; };
      in {
        packages.default = pkgs.stdenv.mkDerivation rec {
          pname = "eternal";
          version = "0.1.0";

          src = ./..;

          nativeBuildInputs = with pkgs; [
            cmake
            ninja
            pkg-config
            wayland-scanner
          ];

          buildInputs = with pkgs; [
            wayland
            wayland-protocols
            wlroots_0_18
            libxkbcommon
            libinput
            pixman
            libdrm
            libGL
            mesa
            libxcb
            xcbutil
            xcbutilwm
          ];

          cmakeFlags = [
            "-DCMAKE_BUILD_TYPE=Release"
            "-DETERNAL_ENABLE_XWAYLAND=ON"
            "-DETERNAL_ENABLE_IPC=ON"
            "-DETERNAL_ENABLE_PLUGINS=ON"
            "-DETERNAL_ENABLE_SCREENCOPY=ON"
            "-DETERNAL_BUILD_CLI=ON"
          ];

          postInstall = ''
            # Install desktop session entry
            mkdir -p $out/share/wayland-sessions
            cp ${src}/packaging/eternal.desktop $out/share/wayland-sessions/

            # Install man page
            mkdir -p $out/share/man/man1
            cp ${src}/docs/eternal.1 $out/share/man/man1/

            # Install default config
            mkdir -p $out/share/eternal
            cp ${src}/config/eternal.kdl $out/share/eternal/
          '';

          meta = with pkgs.lib; {
            description = "A modern Wayland compositor with scrollable tiling and animations";
            homepage = "https://github.com/eternal-wm/eternal";
            license = licenses.mit;
            platforms = platforms.linux;
            mainProgram = "eternal";
          };
        };

        packages.eternalctl = self.packages.${system}.default;

        devShells.default = pkgs.mkShell {
          inputsFrom = [ self.packages.${system}.default ];

          packages = with pkgs; [
            gdb
            valgrind
            clang-tools
            bear  # for compile_commands.json
          ];

          shellHook = ''
            echo "Eternal development shell"
            echo "  Build: cmake -B build -G Ninja && cmake --build build"
            echo "  Test:  cd build && ctest --output-on-failure"
          '';
        };
      }
    ) // {
      # NixOS module for installing eternal as a session
      nixosModules.default = { config, lib, pkgs, ... }:
        let
          cfg = config.programs.eternal;
        in {
          options.programs.eternal = {
            enable = lib.mkEnableOption "Eternal Wayland compositor";

            package = lib.mkOption {
              type = lib.types.package;
              default = self.packages.${pkgs.system}.default;
              description = "The eternal package to use.";
            };

            xwayland = lib.mkOption {
              type = lib.types.bool;
              default = true;
              description = "Whether to enable XWayland support.";
            };
          };

          config = lib.mkIf cfg.enable {
            environment.systemPackages = [ cfg.package ];

            services.displayManager.sessionPackages = [ cfg.package ];

            xdg.portal = {
              enable = true;
              wlr.enable = true;
            };

            programs.xwayland.enable = cfg.xwayland;
          };
        };
    };
}

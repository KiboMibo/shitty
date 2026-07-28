# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.
{
  description = "Shitty — a small, fast Wayland/Vulkan terminal emulator";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

    # Pinned to the commits recorded in .gitmodules. Bump together with the
    # submodules when either dependency moves.
    libstd = {
      url = "github:pg83/std/6ab662255eb2c459e5e69e13248c964eef5eedc1";
      flake = false;
    };
    platform = {
      url = "github:pg83/platform/89647e91af45513de60e731071401f9161404857";
      flake = false;
    };
  };

  outputs =
    {
      self,
      nixpkgs,
      libstd,
      platform,
    }:
    let
      inherit (nixpkgs) lib;

      systems = [
        "x86_64-linux"
        "aarch64-linux"
      ];

      forAllSystems = lib.genAttrs systems;

      nixpkgsFor = system: nixpkgs.legacyPackages.${system};

      # YYYY.MM.DD from the flake's lastModifiedDate, matching build.py's scheme.
      versionFromFlake =
        let
          d = self.lastModifiedDate or "19700101";
        in
        "${builtins.substring 0 4 d}.${builtins.substring 4 2 d}.${builtins.substring 6 2 d}";

      mkShitty =
        pkgs:
        let
          stdenv = pkgs.llvmPackages.stdenv;
        in
        stdenv.mkDerivation rec {
          pname = "shitty";
          version = versionFromFlake;

          src = self;

          # Flake source checkouts do not include git submodules. Populate the
          # vendored trees from the pinned flake inputs so `nix build` works
          # without `?submodules=1`.
          postUnpack = ''
            mkdir -p "$sourceRoot/third_party"
            rm -rf "$sourceRoot/third_party/libstd" "$sourceRoot/third_party/platform"
            cp -a ${libstd} "$sourceRoot/third_party/libstd"
            cp -a ${platform} "$sourceRoot/third_party/platform"
            mkdir -p "$sourceRoot/third_party/platform/third_party"
            rm -rf "$sourceRoot/third_party/platform/third_party/libstd"
            cp -a ${libstd} "$sourceRoot/third_party/platform/third_party/libstd"
            chmod -R u+w "$sourceRoot/third_party"
          '';

          # build.py stamps SHITTY_VERSION from date.today(), which is impure
          # under the Nix sandbox. Pin it to the flake revision date instead.
          postPatch = ''
            substituteInPlace build.py \
              --replace-fail 'date.today().strftime("%Y.%m.%d")' '"${version}"'
          '';

          nativeBuildInputs = with pkgs; [
            addDriverRunpath
            glslang
            makeWrapper
            pkg-config
            python3
            ragel
            wayland-scanner
          ];

          buildInputs = with pkgs; [
            brotli
            fontconfig
            freetype
            harfbuzz
            libxkbcommon
            simdutf
            utf8proc
            vulkan-headers
            vulkan-loader
            wayland
          ];

          # The project's runner honours the usual toolchain env vars. Keep the
          # build out of $src (read-only store path) via -B.
          buildPhase = ''
            runHook preBuild
            python3 ./build -B .build -j "$NIX_BUILD_CORES"
            runHook postBuild
          '';

          installPhase = ''
            runHook preInstall
            install -Dm755 .build/st "$out/bin/st"
            install -Dm644 shitty.desktop \
              "$out/share/applications/shitty.desktop"
            install -Dm644 shitty.svg \
              "$out/share/icons/hicolor/scalable/apps/shitty.svg"
            runHook postInstall
          '';

          # Vulkan ICDs live under /run/opengl-driver on NixOS; addDriverRunpath
          # puts that directory on the binary's RUNPATH.
          postFixup = ''
            addDriverRunpath "$out/bin/st"
          '';

          meta = {
            description = "Small, fast terminal emulator with a Linux Wayland/Vulkan frontend";
            homepage = "https://github.com/pg83/shitty";
            license = with lib.licenses; [
              gpl3Plus
              mit
            ];
            mainProgram = "st";
            platforms = lib.platforms.linux;
            # Wayland + Vulkan compute frontend; no X11 backend.
            badPlatforms = lib.platforms.darwin;
          };
        };

      mkDevShell =
        pkgs:
        let
          stdenv = pkgs.llvmPackages.stdenv;
          shitty = mkShitty pkgs;
        in
        pkgs.mkShell.override { inherit stdenv; } {
          inputsFrom = [ shitty ];

          packages = with pkgs; [
            clang-tools
            gdb
            # Fonts for manual runs inside the shell.
            dejavu_fonts
            ibm-plex
          ];

          FONTCONFIG_FILE = pkgs.makeFontsConf {
            fontDirectories = with pkgs; [
              dejavu_fonts
              ibm-plex
            ];
          };

          shellHook = ''
            echo "shitty dev shell — run: ./build && ./st"
          '';
        };
    in
    {
      packages = forAllSystems (
        system:
        let
          pkgs = nixpkgsFor system;
          shitty = mkShitty pkgs;
        in
        {
          default = shitty;
          shitty = shitty;
        }
      );

      devShells = forAllSystems (
        system:
        let
          pkgs = nixpkgsFor system;
        in
        {
          default = mkDevShell pkgs;
        }
      );

      formatter = forAllSystems (system: (nixpkgsFor system).nixfmt-rfc-style);

      # Overlay so consumers can `pkgs.shitty` after importing the flake.
      overlays.default = final: prev: {
        shitty = mkShitty final;
      };
    };
}

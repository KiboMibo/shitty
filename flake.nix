# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.
{
  description = "Shitty — a small, fast Wayland/Vulkan terminal emulator";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

    # Pinned to the commit recorded in .gitmodules.
    libstd = {
      url = "github:pg83/std/6ab662255eb2c459e5e69e13248c964eef5eedc1";
      flake = false;
    };
  };

  outputs =
    {
      self,
      nixpkgs,
      libstd,
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

      sanitizerCxxFlags = lib.concatStringsSep " " [
        "-fsanitize=address,undefined"
        "-fno-sanitize-recover=all"
        "-fno-omit-frame-pointer"
        "-g"
      ];

      sanitizerLdFlags = "-fsanitize=address,undefined";

      configureBuildEnvironment = sanitize: ''
        unset CPPFLAGS CFLAGS CXXFLAGS LDFLAGS
        # build intentionally passes its canonical target triple. Nix's
        # wrapper spells the equivalent native vendor field differently.
        export NIX_CC_WRAPPER_SUPPRESS_TARGET_WARNING=1
        ${lib.optionalString sanitize ''
          export CXXFLAGS=${lib.escapeShellArg sanitizerCxxFlags}
          export ASAN_OPTIONS=detect_leaks=1:abort_on_error=1
          export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
        ''}
        export LDFLAGS="${lib.optionalString sanitize "${sanitizerLdFlags} "}$(pkg-config --libs wayland-client xkbcommon) -lrt"
      '';

      mkShitty =
        pkgs:
        {
          sanitize ? false,
        }:
        let
          stdenv = pkgs.llvmPackages.stdenv;
          buildDirectory = if sanitize then ".build-asan-ubsan" else ".build";
        in
        stdenv.mkDerivation rec {
          pname = if sanitize then "shitty-asan" else "shitty";
          version = versionFromFlake;

          src = self;

          # Flake source checkouts do not include git submodules. Populate
          # libstd from its pinned input; plt is vendored in this repository.
          postUnpack = ''
            mkdir -p "$sourceRoot/third_party"
            rm -rf "$sourceRoot/third_party/libstd"
            cp -a ${libstd} "$sourceRoot/third_party/libstd"
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
            wayland-protocols
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
            ${configureBuildEnvironment sanitize}
            python3 ./build -B ${buildDirectory} -j "$NIX_BUILD_CORES"
            runHook postBuild
          '';

          installPhase = ''
            runHook preInstall
            install -Dm755 ${buildDirectory}/st "$out/bin/st"
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

      mkTestCheck =
        pkgs:
        {
          sanitize ? false,
        }:
        let
          base = mkShitty pkgs { inherit sanitize; };
          buildDirectory = if sanitize then ".build-tests-asan-ubsan" else ".build-tests";
        in
        base.overrideAttrs (old: {
          pname = if sanitize then "shitty-tests-asan" else "shitty-tests";

          nativeBuildInputs =
            old.nativeBuildInputs
            ++ [
              pkgs.ncurses
              pkgs.perl
              pkgs.vttest
            ]
            ++ lib.optionals sanitize [ pkgs.llvmPackages.llvm ];

          postPatch = old.postPatch + ''
            # Some vendored xterm scripts invoke other scripts by their
            # /usr/bin/env shebang. Rewrite those interpreters for the Nix
            # sandbox without modifying the upstream fixtures in git.
            patchShebangs \
              tests/xterm_vttests/upstream \
              tests/xterm_vttests/bin
            for script in $(grep -l "CMD='/bin/echo'" tests/xterm_vttests/upstream/*.sh); do
              substituteInPlace "$script" \
                --replace-fail "CMD='/bin/echo'" "CMD='echo'"
            done
          '';

          FONTCONFIG_FILE = pkgs.makeFontsConf {
            fontDirectories = with pkgs; [
              dejavu_fonts
              ibm-plex
            ];
          };

          buildPhase = ''
            runHook preBuild
            ${configureBuildEnvironment sanitize}
            ${lib.optionalString sanitize ''
              export ASAN_SYMBOLIZER_PATH=${lib.getExe' pkgs.llvmPackages.llvm "llvm-symbolizer"}
            ''}
            python3 ./build \
              -B ${buildDirectory} \
              -j "$NIX_BUILD_CORES" \
              -k \
              test
            runHook postBuild
          '';

          installPhase = ''
            runHook preInstall
            mkdir -p "$out"
            touch "$out/passed"
            runHook postInstall
          '';

          postFixup = "";
        });

      mkDevShell =
        pkgs:
        let
          stdenv = pkgs.llvmPackages.stdenv;
          shitty = mkShitty pkgs { };
        in
        pkgs.mkShell.override { inherit stdenv; } {
          inputsFrom = [ shitty ];
          NIX_CC_WRAPPER_SUPPRESS_TARGET_WARNING = "1";

          packages = with pkgs; [
            clang-tools
            gdb
            ncurses
            perl
            vttest
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
            # Keep ambient toolchain flags from contaminating this shell.
            unset CPPFLAGS CFLAGS CXXFLAGS LDFLAGS
            export LDFLAGS="$(pkg-config --libs wayland-client xkbcommon) -lrt"
            echo "shitty dev shell — run: ./build && ./st"
          '';
        };
    in
    {
      packages = forAllSystems (
        system:
        let
          pkgs = nixpkgsFor system;
          shitty = mkShitty pkgs { };
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

      checks = forAllSystems (
        system:
        let
          pkgs = nixpkgsFor system;
        in
        {
          build = mkShitty pkgs { };
          tests = mkTestCheck pkgs { };
          build-asan = mkShitty pkgs { sanitize = true; };
          tests-asan = mkTestCheck pkgs { sanitize = true; };
        }
      );

      formatter = forAllSystems (system: (nixpkgsFor system).nixfmt);

      # Overlay so consumers can `pkgs.shitty` after importing the flake.
      overlays.default = final: prev: {
        shitty = mkShitty final { };
      };
    };
}

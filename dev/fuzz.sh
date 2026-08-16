#!/bin/sh

set -eu

if [ "$(uname)" = Darwin ]; then
    # The flake and the legacy shell.nix only package Linux dependencies
    # (wayland), so assemble a minimal shell here. -fuse-ld=lld works around
    # a cctools ld crash when linking the sanitizer runtimes.
    nix-shell -E '{ pkgs ? import <nixpkgs> {} }: (pkgs.mkShell.override { stdenv = pkgs.llvmPackages.stdenv; }) { packages = with pkgs; [ brotli fontconfig freetype glslang harfbuzz llvmPackages.lld pkg-config python3 ragel simdutf spirv-cross ]; }' \
        --run 'CFLAGS="-fsanitize=address,fuzzer-no-link -fno-omit-frame-pointer -g" LDFLAGS="-fsanitize=address,fuzzer -fuse-ld=lld" ./build main_fuzz'
else
    env -u CPPFLAGS -u CFLAGS -u CXXFLAGS -u LDFLAGS -u CTRFLAGS -u BUILD_EXTRA_CFLAGS -u CC -u CXX -u AR -u PKG_CONFIG_PATH \
        nix-shell --run 'CFLAGS="-fsanitize=address,fuzzer-no-link -fno-omit-frame-pointer -g" LDFLAGS="-fsanitize=address,fuzzer" ./build main_fuzz'
fi

run_multi() {
    exec python3 ./dev/run_multi.py "$@"
}

# The headless VTerm intentionally persists across units; per-unit LSan scans
# dominate execution time and do not validate that process-lifetime state.
run_multi ./main_fuzz -detect_leaks=0 "$@"

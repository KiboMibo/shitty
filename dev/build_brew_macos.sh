#!/bin/sh
# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.
#
# The CI release build: Homebrew toolchain and an empty pkg-config directory.
# The optional backends (freetype, fontconfig, harfbuzz, brotli, simdutf) must
# not exist as far as the build is concerned, or the released binary picks up
# Homebrew dylibs and runs nowhere else.
set -e

PREFIX="$(brew --prefix)"
export CC="$PREFIX/opt/llvm/bin/clang"
export CXX="$PREFIX/opt/llvm/bin/clang++"
export SDKROOT="${SDKROOT:-$(xcrun --show-sdk-path)}"

WORK="$(mktemp -d)"
PC_DIR="$WORK/pkgconfig"
mkdir "$PC_DIR"
export PKG_CONFIG_LIBDIR="$PC_DIR"

./build --target=aarch64-apple-darwin -B .build-darwin st pt

# The portability guard: nothing outside the system may be dynamically
# linked, or the download is broken on machines without Homebrew.
for BINARY in st pt; do
    if otool -L ".build-darwin/$BINARY" | grep -q "$PREFIX"; then
        echo "release binary links Homebrew dylibs: $BINARY" >&2
        otool -L ".build-darwin/$BINARY" >&2
        exit 1
    fi
done

#!/bin/sh
# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.
#
# The CI release build: Homebrew toolchain and an empty pkg-config directory.
# The optional backends (freetype, fontconfig, harfbuzz, brotli, simdutf) must
# not exist as far as the build is concerned. The separate release test job
# checks the finished artifact for Homebrew dylibs.
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

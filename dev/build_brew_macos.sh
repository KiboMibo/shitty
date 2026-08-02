#!/bin/sh
# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.
#
# The CI release build: Homebrew toolchain, and a curated pkg-config
# directory whose only module is a static libutf8proc - the optional
# backends (freetype, fontconfig, harfbuzz, brotli, simdutf) must not
# exist as far as the build is concerned, or the released binary picks
# up Homebrew dylibs and runs nowhere else.
set -e

PREFIX="$(brew --prefix)"
UTF8PROC="$PREFIX/opt/utf8proc"
VERSION="$(PKG_CONFIG_LIBDIR="$UTF8PROC/lib/pkgconfig" pkg-config --modversion libutf8proc)"

PC_DIR="$(mktemp -d)"
cat > "$PC_DIR/libutf8proc.pc" <<EOF
prefix=$UTF8PROC
Name: libutf8proc
Description: utf8proc, linked statically for the release binary
Version: $VERSION
Cflags: -I\${prefix}/include
Libs: \${prefix}/lib/libutf8proc.a
EOF

export PKG_CONFIG_LIBDIR="$PC_DIR"
export CC="$PREFIX/opt/llvm/bin/clang"
export CXX="$PREFIX/opt/llvm/bin/clang++"
export SDKROOT="${SDKROOT:-$(xcrun --show-sdk-path)}"

./build --target=aarch64-apple-darwin -B .build-darwin st

# The portability guard: nothing outside the system may be dynamically
# linked, or the download is broken on machines without Homebrew.
if otool -L .build-darwin/st | grep -q "$PREFIX"; then
    echo "release binary links Homebrew dylibs:" >&2
    otool -L .build-darwin/st >&2
    exit 1
fi

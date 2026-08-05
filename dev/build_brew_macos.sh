#!/bin/sh
# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.
#
# The CI release build: Homebrew toolchain, a pinned utf8proc compiled
# statically right here (the Homebrew bottle ships no static archive),
# and a curated pkg-config directory whose only module points at it -
# the optional backends (freetype, fontconfig, harfbuzz, brotli,
# simdutf) must not exist as far as the build is concerned, or the
# released binary picks up Homebrew dylibs and runs nowhere else.
set -e

PREFIX="$(brew --prefix)"
export CC="$PREFIX/opt/llvm/bin/clang"
export CXX="$PREFIX/opt/llvm/bin/clang++"
export SDKROOT="${SDKROOT:-$(xcrun --show-sdk-path)}"

UTF8PROC_VERSION="2.10.0"
UTF8PROC_SHA256="6f4f1b639daa6dca9f80bc5db1233e9cbaa31a67790887106160b33ef743f136"

WORK="$(mktemp -d)"
curl -sL -o "$WORK/utf8proc.tar.gz" \
    "https://github.com/JuliaStrings/utf8proc/archive/refs/tags/v$UTF8PROC_VERSION.tar.gz"
echo "$UTF8PROC_SHA256  $WORK/utf8proc.tar.gz" | shasum -a 256 -c - >/dev/null
tar -xzf "$WORK/utf8proc.tar.gz" -C "$WORK"
UTF8PROC="$WORK/utf8proc-$UTF8PROC_VERSION"
"$CC" -O2 -DUTF8PROC_STATIC -c "$UTF8PROC/utf8proc.c" -o "$UTF8PROC/utf8proc.o"
ar rcs "$UTF8PROC/libutf8proc.a" "$UTF8PROC/utf8proc.o"

PC_DIR="$WORK/pkgconfig"
mkdir "$PC_DIR"
cat > "$PC_DIR/libutf8proc.pc" <<EOF
prefix=$UTF8PROC
Name: libutf8proc
Description: utf8proc, compiled statically for the release binary
Version: $UTF8PROC_VERSION
Cflags: -I\${prefix} -DUTF8PROC_STATIC
Libs: \${prefix}/libutf8proc.a
EOF
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

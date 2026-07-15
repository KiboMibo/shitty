#!/bin/sh
# Simple build script for zutty. Assumes all required headers and libraries
# (freetype2, SDL3, vulkan) are reachable via the compiler's default search
# paths and/or CPPFLAGS/CXXFLAGS/LDFLAGS from the environment.
set -eu

cd "$(dirname "$0")"

CXX="${CXX:-c++}"
BUILDDIR="${BUILDDIR:-build}"
VERSION=0.14

mkdir -p "$BUILDDIR"

# Compile the compute shader to SPIR-V and embed it as a C++ header.
glslangValidator --target-env vulkan1.1 -V -S comp \
    -o "$BUILDDIR/render.comp.spv" render.comp
python3 embed_spirv.py "$BUILDDIR/render.comp.spv" "$BUILDDIR/render_spv.h"

# shellcheck disable=SC2086 # word splitting of user-supplied flags is intended
"$CXX" -std=c++17 -O2 \
    -fno-omit-frame-pointer -fsigned-char \
    -DZUTTY_VERSION="\"$VERSION\"" \
    -I"$BUILDDIR" \
    ${CPPFLAGS:-} ${CXXFLAGS:-} \
    charvdev.cc font.cc fontpack.cc frame.cc log.cc main.cc \
    options.cc pty.cc renderer.cc vkpresenter.cc vterm.cc \
    -o "$BUILDDIR/zutty" \
    ${LDFLAGS:-} ${CTRFLAGS} -lfreetype -lSDL3 -lvulkan -lpthread

echo "Built $BUILDDIR/zutty"

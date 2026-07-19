#!/bin/sh
# Simple build script for zutty. Assumes all required headers and libraries
# (freetype2, fontconfig, GLFW, vulkan) are reachable via the compiler's default search
# paths and/or CPPFLAGS/CXXFLAGS/LDFLAGS from the environment.
set -eu

cd "$(dirname "$0")"

CXX="${CXX:-c++}"
BUILDDIR="${BUILDDIR:-build}"
VERSION=0.14

# IX exports the whole development set through LDFLAGS, including SDL3 and
# GLFW. Drop the unused SDL3 library and keep GLFW at the end of the static
# link after the application objects.
LINK_FLAGS=
for flag in ${LDFLAGS:-}; do
    case "$flag" in
        -lSDL3 | -lglfw3) ;;
        *) LINK_FLAGS="$LINK_FLAGS $flag" ;;
    esac
done

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
    charvdev.cpp font.cpp fontpack.cpp frame.cpp log.cpp main.cpp \
    options.cpp pty.cpp renderer.cpp vkpresenter.cpp vterm.cpp \
    -o "$BUILDDIR/zutty" \
    ${LINK_FLAGS} ${CTRFLAGS} -lfreetype -lfontconfig -lglfw3 -lvulkan -lpthread

echo "Built $BUILDDIR/zutty"

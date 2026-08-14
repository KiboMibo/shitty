#!/usr/bin/env bash

# Build the portable Linux binaries from source.  The host contributes build
# tools only; every library that enters st and pt is rebuilt into SYSROOT.

set -euo pipefail

project_root="$(cd "$(dirname "$0")/.." && pwd)"
cache_root="${SHITTY_LINUX_SOURCE_CACHE:-${XDG_CACHE_HOME:-$project_root/.cache}/shitty-linux-sources}"
temporary_root="${RUNNER_TEMP:-${TMPDIR:-/tmp}}"
work_parent="${SHITTY_LINUX_WORK_ROOT:-$temporary_root/shitty-linux-static-work}"
output_root="${SHITTY_LINUX_OUTPUT:-$project_root/.build-linux-static}"
jobs="${SHITTY_LINUX_JOBS:-$(nproc)}"
target=x86_64-linux-musl

if [[ "$(uname -s)" != Linux || "$(uname -m)" != x86_64 ]]; then
    echo "build_linux_static.sh currently requires an x86-64 Linux host" >&2
    exit 1
fi

tools=(
    autoconf automake autopoint bison clang clang++ cmake curl file flex
    gettextize glslangValidator gperf gzip ld.lld libtoolize llvm-ar llvm-nm llvm-ranlib
    llvm-readelf m4 make meson ninja nproc perl pkg-config python3 ragel
    sha256sum tar wayland-scanner
)
for tool in "${tools[@]}"; do
    if ! command -v "$tool" >/dev/null; then
        echo "required build tool is missing: $tool" >&2
        exit 1
    fi
done

if ! command -v rsvg-convert >/dev/null && ! command -v svg2png >/dev/null; then
    echo "required icon renderer is missing: rsvg-convert or svg2png" >&2
    exit 1
fi

if ! clang++ -std=c++26 -fsyntax-only -x c++ /dev/null; then
    echo "the host clang++ does not accept -std=c++26" >&2
    exit 1
fi

mkdir -p "$cache_root" "$work_parent" "$output_root"
work_root="$(mktemp -d "$work_parent/run.XXXXXX")"
source_root="$work_root/src"
build_root="$work_root/build"
sysroot="$work_root/sysroot"
mkdir -p "$source_root" "$build_root" "$sysroot/usr/include" "$sysroot/usr/lib"

cleanup() {
    if [[ "${SHITTY_LINUX_KEEP_WORK:-0}" == 1 ]]; then
        echo "kept Linux build tree at $work_root" >&2
    else
        rm -rf "$work_root"
    fi
}
trap cleanup EXIT

# Do not let a developer shell silently inject IX, Homebrew, Nix, or distro
# headers and libraries into the supposedly self-contained sysroot.
host_cppflags="${HOST_CPPFLAGS:-${CPPFLAGS:-}}"
host_cflags="${HOST_CFLAGS:-${CFLAGS:-}}"
host_cxxflags="${HOST_CXXFLAGS:-${CXXFLAGS:-}}"
host_ldflags="${HOST_LDFLAGS:-${LDFLAGS:-}}"
unset CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH LIBRARY_PATH SDKROOT
unset CURL_CA_BUNDLE SSL_CERT_DIR SSL_CERT_FILE
unset CPPFLAGS CFLAGS CXXFLAGS LDFLAGS PKG_CONFIG_PATH PKG_CONFIG_LIBDIR

fetch() {
    local name="$1"
    local sha="$2"
    local url="$3"
    local archive="$cache_root/$name"
    local temporary="$archive.part"

    if [[ -f "$archive" ]] && printf '%s  %s\n' "$sha" "$archive" | sha256sum --check --status; then
        printf 'SOURCE %-32s cached\n' "$name"
        return
    fi

    rm -f "$temporary"
    printf 'SOURCE %-32s %s\n' "$name" "$url"
    curl --fail --location --retry 5 --retry-all-errors --output "$temporary" "$url"
    printf '%s  %s\n' "$sha" "$temporary" | sha256sum --check --status
    mv "$temporary" "$archive"
}

unpack() {
    local name="$1"
    local archive="$2"
    local destination="$source_root/$name"

    mkdir -p "$destination"
    tar -xf "$cache_root/$archive" --strip-components=1 -C "$destination"
}

fetch linux-6.12.18.tar.xz beb902a5f69d9e57710112203db38111dad6d30556ea8ce389284c8077fe944d \
    https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.12.18.tar.xz
fetch musl-1.2.5.tar.gz a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4 \
    https://musl.libc.org/releases/musl-1.2.5.tar.gz
fetch llvm-project-15.0.7.tar.gz 42a0088f148edcf6c770dfc780a7273014a9a89b66f357c761b4ca7c8dfa10ba \
    https://github.com/llvm/llvm-project/archive/refs/tags/llvmorg-15.0.7.tar.gz
fetch zlib-ng-2.2.4.tar.gz a73343c3093e5cdc50d9377997c3815b878fd110bf6511c2c7759f2afb90f5a3 \
    https://github.com/zlib-ng/zlib-ng/archive/refs/tags/2.2.4.tar.gz
fetch bzip2-1.0.8.tar.gz ab5a03176ee106d3f0fa90e381da478ddae405918153cca248e682cd0c4a2269 \
    https://sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz
fetch brotli-1.2.0.tar.gz 816c96e8e8f193b40151dad7e8ff37b1221d019dbcb9c35cd3fadbfe6477dfec \
    https://github.com/google/brotli/archive/refs/tags/v1.2.0.tar.gz
fetch libpng-1.6.50.tar.gz 71158e53cfdf2877bc99bcab33641d78df3f48e6e0daad030afe9cb8c031aa46 \
    https://github.com/glennrp/libpng/archive/refs/tags/v1.6.50.tar.gz
fetch expat-2.7.4.tar.gz 5b356795b889d3e5b379433ece069b8781bf0727f6959ad3bbc9da0c22164f59 \
    https://github.com/libexpat/libexpat/archive/refs/tags/R_2_7_4.tar.gz
fetch libffi-3.5.2.tar.gz f3a3082a23b37c293a4fcd1053147b371f2ff91fa7ea1b2a52e335676bac82dc \
    https://github.com/libffi/libffi/releases/download/v3.5.2/libffi-3.5.2.tar.gz
fetch wayland-1.26.0.tar.bz2 ebf5fff1c8b11c24ceec74ff3047aefdb07efee8ce09bf3b856975aba3540d15 \
    https://gitlab.freedesktop.org/wayland/wayland/-/archive/1.26.0/wayland-1.26.0.tar.bz2
fetch wayland-protocols-1.49.tar.bz2 7330d2d8fbda81991548738e81da493829bc55db2daf1ae15b9d4952e4d7d83b \
    https://gitlab.freedesktop.org/wayland/wayland-protocols/-/archive/1.49/wayland-protocols-1.49.tar.bz2
fetch libxkbcommon-1.13.1.tar.gz aeb951964c2f7ecc08174cb5517962d157595e9e3f38fc4a130b91dc2f9fec18 \
    https://github.com/xkbcommon/libxkbcommon/archive/refs/tags/xkbcommon-1.13.1.tar.gz
fetch freetype-2.14.1.tar.bz2 7fea7e6fd000b6c9387374d9487d66a0822d45721f43f47e00201f62977e2b0f \
    https://gitlab.com/freetype/freetype/-/archive/VER-2-14-1/freetype-VER-2-14-1.tar.bz2
fetch harfbuzz-12.3.2.tar.gz 3ca6621821e19266710ec1a0cb6146fdc832a75828f7c55fea5502c2a810c3c8 \
    https://github.com/harfbuzz/harfbuzz/archive/refs/tags/12.3.2.tar.gz
fetch fontconfig-2.17.1.tar.bz2 bc1a90697eb8ec6c3eed118105ef9cbdfdd676e563905bf1cb571a705598300e \
    https://gitlab.freedesktop.org/fontconfig/fontconfig/-/archive/2.17.1/fontconfig-2.17.1.tar.bz2
fetch simdutf-7.7.0.tar.gz 0180de81a1dd48a87b8c0442ffa81734f3db91a7350914107a449935124e3c6f \
    https://github.com/simdutf/simdutf/archive/refs/tags/v7.7.0.tar.gz
fetch vulkan-headers-e3b1eec.tar.gz f492279345cbc10708b64fcd432b3ff6c8246a5837c4db2b649abba00cf82208 \
    https://github.com/KhronosGroup/Vulkan-Headers/archive/e3b1eec08173d6b825cd3ac88c885a63b621504a.tar.gz
fetch vulkan-loader-1.4.321.0.tar.gz 9e0315bd13d8def7d130524d0b69d0bef3e967374327ac69dd9c54cd2b716e8f \
    https://github.com/KhronosGroup/Vulkan-Loader/archive/refs/tags/vulkan-sdk-1.4.321.0.tar.gz
fetch solo-e59c516.tar.gz bf1ded63fe3a9f73bb1b8c7b6fc3110836b888b877a6c25d576be4774a5ccc0a \
    https://github.com/pg83/solo/archive/e59c51625d7eddf6c6ae0fec307009a592e503d4.tar.gz

unpack linux linux-6.12.18.tar.xz
unpack musl musl-1.2.5.tar.gz
unpack llvm llvm-project-15.0.7.tar.gz
unpack zlib zlib-ng-2.2.4.tar.gz
unpack bzip2 bzip2-1.0.8.tar.gz
unpack brotli brotli-1.2.0.tar.gz
unpack libpng libpng-1.6.50.tar.gz
unpack expat expat-2.7.4.tar.gz
unpack libffi libffi-3.5.2.tar.gz
unpack wayland wayland-1.26.0.tar.bz2
unpack wayland-protocols wayland-protocols-1.49.tar.bz2
unpack xkbcommon libxkbcommon-1.13.1.tar.gz
unpack freetype freetype-2.14.1.tar.bz2
unpack harfbuzz harfbuzz-12.3.2.tar.gz
unpack fontconfig fontconfig-2.17.1.tar.bz2
unpack simdutf simdutf-7.7.0.tar.gz
unpack vulkan-headers vulkan-headers-e3b1eec.tar.gz
unpack vulkan-loader vulkan-loader-1.4.321.0.tar.gz
unpack solo solo-e59c516.tar.gz

cc="$(command -v clang)"
cxx="$(command -v clang++)"
ar="$(command -v llvm-ar)"
ranlib="$(command -v llvm-ranlib)"
nm="$(command -v llvm-nm)"
clang_include="${SHITTY_LINUX_CLANG_INCLUDE:-$($cc -print-resource-dir)/include}"
if [[ ! -f "$clang_include/stddef.h" ]]; then
    echo "clang resource headers are missing: $clang_include" >&2
    exit 1
fi
clang_resource="$(dirname "$clang_include")"
builtins="${SHITTY_LINUX_BUILTINS:-$($cc --target="$target" -print-libgcc-file-name)}"
if [[ ! -f "$builtins" ]]; then
    echo "clang did not provide a compiler runtime archive: $builtins" >&2
    exit 1
fi
runtime_dir="$(dirname "$builtins")"
crtbegin="${SHITTY_LINUX_CRTBEGIN:-}"
crtend="${SHITTY_LINUX_CRTEND:-}"
for candidate in "$runtime_dir/crtbeginT.o" "$runtime_dir/clang_rt.crtbegin-x86_64.o"; do
    if [[ -z "$crtbegin" && -f "$candidate" ]]; then
        crtbegin="$candidate"
    fi
done
for candidate in "$runtime_dir/crtend.o" "$runtime_dir/clang_rt.crtend-x86_64.o"; do
    if [[ -z "$crtend" && -f "$candidate" ]]; then
        crtend="$candidate"
    fi
done
if [[ ! -f "$crtbegin" || ! -f "$crtend" ]]; then
    echo "clang did not provide crtbegin/crtend next to $builtins" >&2
    exit 1
fi
common_cflags="-O2 -pipe -fPIC -ffunction-sections -fdata-sections -mcx16 -resource-dir $clang_resource -ffile-prefix-map=$work_root=."
target_cc="$cc --target=$target --sysroot=$sysroot"
target_cxx="$cxx --target=$target --sysroot=$sysroot -resource-dir $clang_resource -stdlib=libc++ -nostdinc++ -I$sysroot/usr/include/c++/v1"
target_ldflags="--target=$target --sysroot=$sysroot -static -fuse-ld=lld -Wl,--gc-sections"

printf 'BUILD  %-32s\n' linux-headers
make -C "$source_root/linux" -j "$jobs" ARCH=x86 \
    HOSTCC="${HOST_CC:-$cc}" HOSTCXX="${HOST_CXX:-$cxx}" \
    HOSTCFLAGS="$host_cppflags $host_cflags" \
    HOSTCXXFLAGS="$host_cppflags $host_cflags $host_cxxflags" \
    HOSTLDFLAGS="$host_ldflags" headers
cp -a "$source_root/linux/usr/include/." "$sysroot/usr/include/"

printf 'BUILD  %-32s\n' musl
printf 'void* __dso_handle = (void*)&__dso_handle;\n' > "$source_root/musl/src/stdlib/dso_handle.c"
(
    cd "$source_root/musl"
    CC="$cc --target=$target" AR="$(command -v llvm-ar)" RANLIB="$(command -v llvm-ranlib)" \
        CFLAGS="-O2 -fno-pic -fno-pie" ./configure \
        --prefix=/usr --syslibdir=/usr/lib --target="$target" \
        --enable-static --disable-shared
    make -j "$jobs"
    make DESTDIR="$sysroot" install
)

# Clang's own runtime is part of the host toolchain.  Give its conventional
# GCC-compatible names to cross-build systems that insist on linking a probe.
install -m 0644 "$builtins" "$sysroot/usr/lib/libgcc.a"
install -m 0644 "$builtins" "$sysroot/usr/lib/libgcc_eh.a"
install -m 0644 "$crtbegin" "$sysroot/usr/lib/crtbeginT.o"
install -m 0644 "$crtend" "$sysroot/usr/lib/crtend.o"

pkg_config_real="$(command -v pkg-config)"
pkg_config_wrapper="$work_root/pkg-config"
cat > "$pkg_config_wrapper" <<EOF
#!/usr/bin/env bash
exec "$pkg_config_real" --static "\$@"
EOF
chmod +x "$pkg_config_wrapper"
export PKG_CONFIG="$pkg_config_wrapper"
export PKG_CONFIG_SYSROOT_DIR="$sysroot"
export PKG_CONFIG_LIBDIR="$sysroot/usr/lib/pkgconfig:$sysroot/usr/share/pkgconfig"

# wayland-scanner is a host tool.  Publish only that tool to Meson's native
# pkg-config lookup; target libraries remain confined to PKG_CONFIG_LIBDIR.
host_pkgconfig="$work_root/host-pkgconfig"
mkdir -p "$host_pkgconfig"
scanner="$(command -v wayland-scanner)"
scanner_version="$(wayland-scanner --version 2>&1 | awk '{print $2}')"
cat > "$host_pkgconfig/wayland-scanner.pc" <<EOF
prefix=/usr

Name: Wayland Scanner
Description: host Wayland protocol generator
Version: $scanner_version
wayland_scanner=$scanner
EOF
export PKG_CONFIG_PATH="$host_pkgconfig"
export PKG_CONFIG_PATH_FOR_BUILD="$host_pkgconfig"
export PKG_CONFIG_LIBDIR_FOR_BUILD="$host_pkgconfig"

cmake_toolchain="$work_root/toolchain.cmake"
cat > "$cmake_toolchain" <<EOF
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER "$cc")
set(CMAKE_CXX_COMPILER "$cxx")
set(CMAKE_C_COMPILER_TARGET "$target")
set(CMAKE_CXX_COMPILER_TARGET "$target")
set(CMAKE_SYSROOT "$sysroot")
set(CMAKE_AR "$ar")
set(CMAKE_RANLIB "$ranlib")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_FIND_ROOT_PATH "$sysroot")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_C_FLAGS_INIT "$common_cflags")
set(CMAKE_CXX_FLAGS_INIT "$common_cflags -stdlib=libc++ -nostdinc++ -I$sysroot/usr/include/c++/v1")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static -fuse-ld=lld -Wl,--gc-sections")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-static -fuse-ld=lld -Wl,--gc-sections")
EOF

cmake_static() {
    local name="$1"
    local source="$2"
    shift 2
    local build="$build_root/$name"

    printf 'BUILD  %-32s\n' "$name"
    cmake -S "$source" -B "$build" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$cmake_toolchain" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr \
        -DCMAKE_INSTALL_LIBDIR=lib \
        -DBUILD_SHARED_LIBS=OFF \
        "$@"
    cmake --build "$build" --parallel "$jobs"
    DESTDIR="$sysroot" cmake --install "$build"
}

printf 'BUILD  %-32s\n' llvm-runtimes
runtime_build="$build_root/llvm-runtimes"
cmake -S "$source_root/llvm/runtimes" -B "$runtime_build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$cmake_toolchain" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_INSTALL_LIBDIR=lib \
    -DLLVM_ENABLE_RUNTIMES='libunwind;libcxxabi;libcxx' \
    -DLIBUNWIND_ENABLE_SHARED=OFF \
    -DLIBUNWIND_ENABLE_STATIC=ON \
    -DLIBUNWIND_INCLUDE_TESTS=OFF \
    -DLIBUNWIND_ENABLE_THREADS=ON \
    -DLIBCXXABI_ENABLE_SHARED=OFF \
    -DLIBCXXABI_ENABLE_STATIC=ON \
    -DLIBCXXABI_INCLUDE_TESTS=OFF \
    -DLIBCXXABI_USE_LLVM_UNWINDER=ON \
    -DLIBCXXABI_ENABLE_STATIC_UNWINDER=ON \
    -DLIBCXX_ENABLE_SHARED=OFF \
    -DLIBCXX_ENABLE_STATIC=ON \
    -DLIBCXX_INCLUDE_TESTS=OFF \
    -DLIBCXX_INCLUDE_BENCHMARKS=OFF \
    -DLIBCXX_CXX_ABI=libcxxabi \
    -DLIBCXX_HAS_MUSL_LIBC=ON \
    -DLIBCXX_ENABLE_ABI_LINKER_SCRIPT=OFF
cmake --build "$runtime_build" --parallel "$jobs"
DESTDIR="$sysroot" cmake --install "$runtime_build"

# Shitty deliberately asks for -latomic on x86-64.  Clang with -mcx16 emits
# the operations inline, so the archive is an explicit empty compatibility
# provider, matching IX's lib/shim/fake(lib_name=atomic).
"$ar" rcs "$sysroot/usr/lib/libatomic.a"

cmake_static zlib "$source_root/zlib" \
    -DZLIB_COMPAT=ON -DZLIB_ENABLE_TESTS=OFF -DWITH_GTEST=OFF \
    -DWITH_OPTIM=OFF -DWITH_RUNTIME_CPU_DETECTION=OFF

printf 'BUILD  %-32s\n' bzip2
make -C "$source_root/bzip2" -j "$jobs" libbz2.a \
    CC="$target_cc" AR="$ar" RANLIB="$ranlib" CFLAGS="$common_cflags"
install -m 0644 "$source_root/bzip2/bzlib.h" "$sysroot/usr/include/bzlib.h"
install -m 0644 "$source_root/bzip2/libbz2.a" "$sysroot/usr/lib/libbz2.a"
mkdir -p "$sysroot/usr/lib/pkgconfig"
cat > "$sysroot/usr/lib/pkgconfig/bzip2.pc" <<'EOF'
prefix=/usr
libdir=${prefix}/lib
includedir=${prefix}/include

Name: bzip2
Description: lossless block-sorting compression
Version: 1.0.8
Libs: -L${libdir} -lbz2
Cflags: -I${includedir}
EOF

cmake_static brotli "$source_root/brotli" \
    -DBROTLI_DISABLE_TESTS=ON -DBROTLI_BUILD_TOOLS=OFF
cmake_static libpng "$source_root/libpng" \
    -DPNG_SHARED=OFF -DPNG_STATIC=ON -DPNG_TESTS=OFF -DPNG_TOOLS=OFF
cmake_static expat "$source_root/expat/expat" \
    -DEXPAT_SHARED_LIBS=OFF -DEXPAT_BUILD_DOCS=OFF -DEXPAT_BUILD_TESTS=OFF \
    -DEXPAT_BUILD_EXAMPLES=OFF -DEXPAT_BUILD_TOOLS=OFF

printf 'BUILD  %-32s\n' libffi
(
    cd "$source_root/libffi"
    CC="$target_cc" CXX="$target_cxx" AR="$ar" RANLIB="$ranlib" \
        CFLAGS="$common_cflags" CXXFLAGS="$common_cflags" \
        LDFLAGS="$target_ldflags" \
        ./configure --build=x86_64-pc-linux-gnu --host="$target" \
            --prefix=/usr --disable-shared --enable-static \
            --disable-docs --disable-multi-os-directory
    make -j "$jobs"
    make DESTDIR="$sysroot" install
)

meson_cross="$work_root/meson.ini"
cat > "$meson_cross" <<EOF
[binaries]
c = ['$cc', '--target=$target']
cpp = ['$cxx', '--target=$target', '-stdlib=libc++']
ar = '$ar'
strip = 'llvm-strip'
pkg-config = '$pkg_config_wrapper'

[properties]
sys_root = '$sysroot'
needs_exe_wrapper = true

[host_machine]
system = 'linux'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'

[built-in options]
prefix = '/usr'
libdir = 'lib'
default_library = 'static'
c_args = ['--sysroot=$sysroot', '-O2', '-pipe', '-fPIC', '-ffunction-sections', '-fdata-sections', '-mcx16', '-resource-dir=$clang_resource', '-ffile-prefix-map=$work_root=.']
cpp_args = ['--sysroot=$sysroot', '-O2', '-pipe', '-fPIC', '-ffunction-sections', '-fdata-sections', '-mcx16', '-resource-dir=$clang_resource', '-ffile-prefix-map=$work_root=.', '-nostdinc++', '-I$sysroot/usr/include/c++/v1']
c_link_args = ['--sysroot=$sysroot', '-static', '-fuse-ld=lld', '-Wl,--gc-sections']
cpp_link_args = ['--sysroot=$sysroot', '-static', '-fuse-ld=lld', '-Wl,--gc-sections']
EOF

meson_static() {
    local name="$1"
    local source="$2"
    shift 2
    local build="$build_root/$name"

    printf 'BUILD  %-32s\n' "$name"
    meson setup "$build" "$source" --cross-file "$meson_cross" \
        --buildtype=release --wrap-mode=nodownload "$@"
    meson compile -C "$build" -j "$jobs"
    DESTDIR="$sysroot" meson install -C "$build" --no-rebuild
}

meson_static wayland "$source_root/wayland" \
    -Ddocumentation=false -Dtests=false -Dlibraries=true -Dscanner=false

printf 'BUILD  %-32s\n' wayland-protocols
meson setup "$build_root/wayland-protocols" "$source_root/wayland-protocols" \
    --prefix=/usr --buildtype=release --wrap-mode=nodownload -Dtests=false
DESTDIR="$sysroot" meson install -C "$build_root/wayland-protocols"

printf 'BUILD  %-32s\n' xkbcommon
meson setup "$build_root/xkbcommon" "$source_root/xkbcommon" \
    --cross-file "$meson_cross" --buildtype=release --wrap-mode=nodownload \
    -Denable-x11=false -Denable-wayland=false -Denable-xkbregistry=false \
    -Denable-tools=false -Denable-docs=false \
    -Dxkb-config-root=/usr/share/X11/xkb \
    -Dxkb-config-extra-path=/etc/xkb \
    -Dxkb-config-unversioned-extensions-path=/usr/share/xkeyboard-config.d \
    -Dxkb-config-versioned-extensions-path=/usr/share/X11/xkb.d \
    -Dx-locale-root=/usr/share/X11/locale
meson compile -C "$build_root/xkbcommon" -j "$jobs" xkbcommon
DESTDIR="$sysroot" meson install -C "$build_root/xkbcommon" --no-rebuild

meson_static freetype-bootstrap "$source_root/freetype" \
    -Dharfbuzz=disabled -Dzlib=enabled -Dbzip2=enabled -Dpng=enabled \
    -Dbrotli=enabled -Dtests=disabled

meson_static harfbuzz "$source_root/harfbuzz" \
    -Dfreetype=enabled -Dglib=disabled -Dgobject=disabled -Dcairo=disabled \
    -Dicu=disabled -Dgraphite2=disabled -Dtests=disabled -Ddocs=disabled \
    -Dutilities=disabled

meson_static freetype "$source_root/freetype" \
    -Dharfbuzz=enabled -Dzlib=enabled -Dbzip2=enabled -Dpng=enabled \
    -Dbrotli=enabled -Dtests=disabled

printf 'BUILD  %-32s\n' fontconfig
(
    cd "$source_root/fontconfig"
    ./autogen.sh --noconfigure
    CC="$target_cc" CXX="$target_cxx" CC_FOR_BUILD="${HOST_CC:-$cc}" \
        AR="$ar" RANLIB="$ranlib" \
        CFLAGS="$common_cflags" CXXFLAGS="$common_cflags" \
        LDFLAGS="$target_ldflags" \
        ./configure --build=x86_64-pc-linux-gnu --host="$target" \
            --prefix=/usr --disable-shared --enable-static \
            --disable-docs --disable-docbook --disable-cache-build \
            --disable-nls --with-default-fonts=/usr/share/fonts \
            --with-add-fonts=/usr/local/share/fonts
    make -C src -j "$jobs" \
        fcalias.h fcaliastail.h fcftalias.h fcftaliastail.h \
        ../fc-case/fccase.h ../fc-lang/fclang.h fcobjshash.h
    make -C src -j "$jobs" libfontconfig.la
)
install -m 0644 "$source_root/fontconfig/src/.libs/libfontconfig.a" \
    "$sysroot/usr/lib/libfontconfig.a"
mkdir -p "$sysroot/usr/include/fontconfig"
install -m 0644 \
    "$source_root/fontconfig/fontconfig/fontconfig.h" \
    "$source_root/fontconfig/fontconfig/fcfreetype.h" \
    "$source_root/fontconfig/fontconfig/fcprivate.h" \
    "$sysroot/usr/include/fontconfig/"
install -m 0644 "$source_root/fontconfig/fontconfig.pc" \
    "$sysroot/usr/lib/pkgconfig/fontconfig.pc"

cmake_static simdutf "$source_root/simdutf" \
    -DSIMDUTF_TESTS=OFF -DSIMDUTF_TOOLS=OFF
cmake_static vulkan-headers "$source_root/vulkan-headers" \
    -DVULKAN_HEADERS_ENABLE_TESTS=OFF

printf 'BUILD  %-32s\n' solo
solo_build="$build_root/solo"
CPPFLAGS="--sysroot=$sysroot -resource-dir $clang_resource" \
CFLAGS="$common_cflags" \
CXXFLAGS="$common_cflags -stdlib=libc++ -nostdinc++ -I$sysroot/usr/include/c++/v1" \
CC="$cc" CXX="$cxx" AR="$ar" \
    "$source_root/solo/build" -B "$solo_build" --target "$target" dlfcn
install -m 0644 "$solo_build/libdlfcn.a" "$sysroot/usr/lib/libdlfcn.a"
install -m 0644 "$source_root/solo/lib/dlfcn.h" "$sysroot/usr/include/dlfcn.h"

# Upstream only exposes its static-loader switch on Apple.  The loader itself
# is platform-independent here; SoLo supplies the Linux dlopen implementation.
loader_cmake="$source_root/vulkan-loader/loader/CMakeLists.txt"
if [[ "$(grep -c '^        add_library(vulkan SHARED)$' "$loader_cmake")" != 1 ]]; then
    echo "the Vulkan Loader static patch no longer applies" >&2
    exit 1
fi
sed -i 's/^        add_library(vulkan SHARED)$/        add_library(vulkan STATIC)/' \
    "$loader_cmake"
if [[ "$(grep -c '^install(TARGETS vulkan EXPORT VulkanLoaderConfig)$' "$loader_cmake")" != 1 || \
      "$(grep -c '^install(EXPORT VulkanLoaderConfig ' "$loader_cmake")" != 1 ]]; then
    echo "the Vulkan Loader install patch no longer applies" >&2
    exit 1
fi
sed -i \
    -e 's/^install(TARGETS vulkan EXPORT VulkanLoaderConfig)$/install(TARGETS vulkan ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})/' \
    -e '/^install(EXPORT VulkanLoaderConfig /d' \
    "$loader_cmake"
cmake_static vulkan-loader "$source_root/vulkan-loader" \
    -DBUILD_TESTS=OFF -DBUILD_WSI_XCB_SUPPORT=OFF -DBUILD_WSI_XLIB_SUPPORT=OFF \
    -DBUILD_WSI_WAYLAND_SUPPORT=ON \
    -DVULKAN_HEADERS_INSTALL_DIR="$sysroot/usr"

registrar="$work_root/static_providers.cpp"
{
    printf '#include <dlfcn.h>\n\nextern "C" {\n'
    for library in wayland-client xkbcommon; do
        "$nm" --defined-only --extern-only "$sysroot/usr/lib/lib$library.a" | \
            awk 'NF >= 3 && $3 ~ /^[A-Za-z_][A-Za-z0-9_]*$/ { print $3 }' | sort -u | \
            while IFS= read -r symbol; do
                printf 'extern void* %s;\n' "$symbol"
            done
    done
    printf 'extern void* vkGetDeviceProcAddr;\n'
    printf 'extern void* vkGetInstanceProcAddr;\n'
    printf '}\n\n'
    for library in wayland-client xkbcommon; do
        printf '__attribute__((constructor)) static void register_%s() {\n' "${library//-/_}"
        "$nm" --defined-only --extern-only "$sysroot/usr/lib/lib$library.a" | \
            awk 'NF >= 3 && $3 ~ /^[A-Za-z_][A-Za-z0-9_]*$/ { print $3 }' | sort -u | \
            while IFS= read -r symbol; do
                printf '    stub_dlregister("%s", "%s", &%s);\n' "$library" "$symbol" "$symbol"
            done
        printf '}\n\n'
    done
    printf '__attribute__((constructor)) static void register_vulkan() {\n'
    printf '    stub_dlregister("vulkan", "vkGetDeviceProcAddr", &vkGetDeviceProcAddr);\n'
    printf '    stub_dlregister("vulkan", "vkGetInstanceProcAddr", &vkGetInstanceProcAddr);\n'
    printf '}\n'
} > "$registrar"

registrar_object="$work_root/static_providers.o"
"$cxx" --target="$target" --sysroot="$sysroot" -stdlib=libc++ \
    -resource-dir "$clang_resource" \
    -nostdinc++ -I"$sysroot/usr/include/c++/v1" \
    -O2 -c "$registrar" -o "$registrar_object"

runtime_group=(
    -Wl,--start-group
    -ldlfcn -lvulkan -lwayland-client -lxkbcommon
    -lfontconfig -lfreetype -lharfbuzz
    -lpng -lz -lbrotlidec -lbrotlicommon -lbrotlienc -lbz2
    -lexpat -lffi -lsimdutf
    -lc++ -lc++abi -lunwind -latomic -lc -lm
    -Wl,--end-group
)
printf -v final_ldflags '%q ' \
    -nostdlib -static -fuse-ld=lld -Wl,--gc-sections -Wl,-s \
    "$sysroot/usr/lib/crt1.o" "$sysroot/usr/lib/crti.o" \
    "$registrar_object" -L"$sysroot/usr/lib" \
    "${runtime_group[@]}" "$builtins" "$sysroot/usr/lib/crtn.o"

printf 'BUILD  %-32s\n' shitty
shitty_build="$work_root/shitty"
PKG_CONFIG="$pkg_config_wrapper" \
PKG_CONFIG_SYSROOT_DIR="$sysroot" \
PKG_CONFIG_LIBDIR="$PKG_CONFIG_LIBDIR" \
CPPFLAGS="--target=$target --sysroot=$sysroot -resource-dir $clang_resource" \
CFLAGS="$common_cflags" \
CXXFLAGS="$common_cflags -stdlib=libc++ -nostdinc++ -I$sysroot/usr/include/c++/v1 -I$sysroot/usr/include" \
LDFLAGS="$final_ldflags" \
CC="$cc" CXX="$cxx" AR="$ar" \
    "$project_root/build" -B "$shitty_build" -j "$jobs" st pt

timestamp="${SOURCE_DATE_EPOCH:-}"
if [[ -z "$timestamp" ]]; then
    if command -v git >/dev/null && git -C "$project_root" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        timestamp="$(git -C "$project_root" show -s --format=%ct HEAD)"
    else
        timestamp=0
    fi
fi
if [[ ! "$timestamp" =~ ^[0-9]+$ ]]; then
    echo "SOURCE_DATE_EPOCH must be a non-negative integer" >&2
    exit 1
fi
if tar --version 2>&1 | grep -q '^tar (GNU tar)'; then
    tar_owner=(--owner=0 --group=0 --numeric-owner)
else
    tar_owner=(--uid 0 --gid 0 --uname root --gname root)
fi

for binary in st pt; do
    install -m 0755 "$shitty_build/$binary" "$output_root/$binary"
    description="$(file -L "$output_root/$binary")"
    printf '%s\n' "$description"
    if [[ "$description" != *'ELF 64-bit LSB executable, x86-64'* ]]; then
        echo "unexpected Linux artifact: $description" >&2
        exit 1
    fi
    program_headers="$(llvm-readelf -lW "$output_root/$binary")"
    if grep -q INTERP <<< "$program_headers"; then
        echo "$binary has a PT_INTERP" >&2
        exit 1
    fi
    dynamic_section="$(llvm-readelf -dW "$output_root/$binary")"
    if grep -q NEEDED <<< "$dynamic_section"; then
        echo "$binary has a DT_NEEDED entry" >&2
        exit 1
    fi
    if grep -aFq "$work_root" "$output_root/$binary"; then
        echo "$binary contains its temporary build path" >&2
        exit 1
    fi

    archive="$output_root/$binary-linux-x86_64.tar.gz"
    tar -czf "$archive" --format=ustar --mtime="@$timestamp" \
        "${tar_owner[@]}" -C "$output_root" "$binary"
done

printf 'DONE   %s\n' "$output_root"

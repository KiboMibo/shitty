#!/usr/bin/env bash

set -euo pipefail

if [ "$#" -ne 2 ]; then
    echo "usage: $0 BUILD_DIR DIAGNOSTICS_DIR" >&2
    exit 2
fi

build_dir=$1
diagnostics_dir=$2
mkdir -p "$diagnostics_dir"

cc_path=$(command -v cc)
realm_dir=${cc_path%/bin/cc}
if [ "$realm_dir" = "$cc_path" ]; then
    echo "cc is not provided by an IX realm: $cc_path" >&2
    exit 1
fi

{
    printf 'realm=%s\n' "$realm_dir"
    printf 'cc=%s\n' "$cc_path"
    printf 'c++=%s\n' "$(command -v c++)"
    printf 'clang=%s\n' "$(command -v clang)"
    printf 'clang++=%s\n' "$(command -v clang++)"
    printf 'ld.lld=%s\n' "$(command -v ld.lld)"
    printf 'llvm-ar=%s\n' "$(command -v llvm-ar)"
    printf 'CPPFLAGS=%s\n' "${CPPFLAGS-}"
    printf 'CFLAGS=%s\n' "${CFLAGS-}"
    printf 'CXXFLAGS=%s\n' "${CXXFLAGS-}"
    printf 'LDFLAGS=%s\n' "${LDFLAGS-}"
    printf 'CTRFLAGS=%s\n' "${CTRFLAGS-}"
    cc --version
    c++ --version
    ld.lld --version
    llvm-ar --version
} > "$diagnostics_dir/toolchain.txt" 2>&1

cp -L "$realm_dir/env" "$diagnostics_dir/realm.env"
for file in libc.a libcrt.a crt1.o crti.o crtn.o; do
    source_path="$realm_dir/lib/$file"
    readlink -f "$source_path" > "$diagnostics_dir/$file.source"
    cp -L "$source_path" "$diagnostics_dir/$file"
done

llvm-ar t "$diagnostics_dir/libcrt.a" > "$diagnostics_dir/libcrt.members"
llvm-nm -A "$diagnostics_dir/libcrt.a" > "$diagnostics_dir/libcrt.symbols"
llvm-nm -A "$diagnostics_dir/libc.a" > "$diagnostics_dir/libc.symbols"
for file in crt1.o crti.o crtn.o; do
    readelf -aW "$diagnostics_dir/$file" > "$diagnostics_dir/$file.elf"
done
sha256sum "$diagnostics_dir"/libc.a \
    "$diagnostics_dir"/libcrt.a \
    "$diagnostics_dir"/crt1.o \
    "$diagnostics_dir"/crti.o \
    "$diagnostics_dir"/crtn.o \
    > "$diagnostics_dir/checksums.txt"

./build --target=x86_64-linux -B "$build_dir" st pt
./build --target=x86_64-linux -B "$build_dir" -G st pt \
    > "$diagnostics_dir/build-graph.json"

base_ldflags=${LDFLAGS-}
for binary in st pt; do
    trace_flags="$base_ldflags -Wl,--why-extract=$diagnostics_dir/$binary-why-extract.tsv"
    trace_flags="$trace_flags -Wl,--reproduce=$diagnostics_dir/$binary-link-reproduce.tar"
    LDFLAGS="$trace_flags" ./build \
        --target=x86_64-linux \
        -B "$build_dir" \
        -j 1 \
        "$binary"
    LDFLAGS="$trace_flags" ./build \
        --target=x86_64-linux \
        -B "$build_dir" \
        -G \
        "$binary" \
        > "$diagnostics_dir/$binary-build-graph.json"
done

#!/usr/bin/env bash
set -euo pipefail

source_dir="$(cd "$(dirname "$0")" && pwd)"
project_dir="$(cd "$source_dir/../.." && pwd)"
output_dir="$project_dir/.build/glibc-bridge"
musl_root="${MUSL_ROOT:-}"
compiler_rt_root="${COMPILER_RT_ROOT:-}"
vulkan_headers_root="${VULKAN_HEADERS_ROOT:-}"

if [[ -z "$musl_root" ]]; then
    for candidate in /ix/store/*-lib-musl-pure; do
        if [[ -f "$candidate/lib/libc.a" && -f "$candidate/lib/crt1.o" ]]; then
            musl_root="$candidate"
            break
        fi
    done
fi

if [[ -z "$compiler_rt_root" ]]; then
    for candidate in /ix/store/*-lib-compiler-rt-builtins-21; do
        if [[ -f "$candidate/lib/libclang_rt.builtins-x86_64.a" ]]; then
            compiler_rt_root="$candidate"
            break
        fi
    done
fi

if [[ -z "$vulkan_headers_root" ]]; then
    for candidate in /ix/store/*-lib-vulkan-headers; do
        if [[ -f "$candidate/include/vulkan/vulkan_core.h" ]] &&
           grep -q '#define VK_HEADER_VERSION 357' \
               "$candidate/include/vulkan/vulkan_core.h"; then
            vulkan_headers_root="$candidate"
            break
        fi
    done
fi

if [[ -z "$musl_root" ]]; then
    echo "MUSL_ROOT must point at an IX musl output containing lib/libc.a" >&2
    exit 1
fi
if [[ -z "$compiler_rt_root" ]]; then
    echo "COMPILER_RT_ROOT must point at an IX compiler-rt-builtins output" >&2
    exit 1
fi
if [[ -z "$vulkan_headers_root" ]]; then
    echo "VULKAN_HEADERS_ROOT must point at Vulkan headers version 357" >&2
    exit 1
fi

mkdir -p "$output_dir"
python3 "$source_dir/generate_host_symbols.py" \
    "$project_dir/../arch/root" \
    "$musl_root/lib/libc.a" \
    "$output_dir/host_symbols.c"
clang --target=x86_64-linux-musl \
    --sysroot="$musl_root" \
    -fuse-ld=lld \
    -nostdlib \
    -static \
    -std=c11 \
    -O1 -g \
    -Wall -Wextra -Werror \
    -fno-stack-protector \
    "$musl_root/lib/crt1.o" \
    "$musl_root/lib/crti.o" \
    "$source_dir/elf_loader.c" \
    "$source_dir/glibc_shim.c" \
    "$output_dir/host_symbols.c" \
    "$source_dir/tlsdesc.S" \
    "$source_dir/smoke.c" \
    -Wl,--start-group \
    "$musl_root/lib/libc.a" \
    "$compiler_rt_root/lib/libclang_rt.builtins-x86_64.a" \
    -Wl,--end-group \
    "$musl_root/lib/crtn.o" \
    -o "$output_dir/smoke"

file "$output_dir/smoke"

clang --target=x86_64-linux-musl \
    --sysroot="$musl_root" \
    -fuse-ld=lld \
    -nostdlib \
    -static \
    -std=c11 \
    -O1 -g \
    -Wall -Wextra -Werror \
    -fno-stack-protector \
    -I"$vulkan_headers_root/include" \
    "$musl_root/lib/crt1.o" \
    "$musl_root/lib/crti.o" \
    "$source_dir/elf_loader.c" \
    "$source_dir/glibc_shim.c" \
    "$output_dir/host_symbols.c" \
    "$source_dir/tlsdesc.S" \
    "$source_dir/radv_smoke.c" \
    -Wl,--start-group \
    "$musl_root/lib/libc.a" \
    "$compiler_rt_root/lib/libclang_rt.builtins-x86_64.a" \
    -Wl,--end-group \
    "$musl_root/lib/crtn.o" \
    -o "$output_dir/radv-smoke"

file "$output_dir/radv-smoke"

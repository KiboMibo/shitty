# glibc ABI bridge experiment

This experiment loads glibc-linked Linux DSOs into a fully static musl
process without mapping glibc or `ld-linux`. It is not a bundled-driver
scheme: the entry DSO and its `DT_NEEDED` closure are read from one external
`usr/lib` directory. The Arch tree is only a reproducible foreign sysroot for
development on this host.

## Status

The proof of concept works end to end on x86-64:

- `smoke` loads Arch's glibc-linked `libvulkan.so.1` and calls
  `vkEnumerateInstanceVersion`.
- `radv-smoke` recursively loads the system RADV closure, including LLVM and
  libstdc++, through the custom loader.
- RADV opens the host's `/dev/dri/renderD128`, creates a Vulkan instance,
  physical device, logical device, queue, image, memory, and command buffer.
- A GPU clear is submitted to an AMD Radeon 890M and read back through mapped
  host-visible memory.

Tested on 2026-08-14 with Arch `vulkan-radeon` 26.1.7, LLVM 22.1.8,
libdrm 2.4.134, and Vulkan loader 1.4.357. The observed result was:

```text
ICD loader interface: 7
physical device 0: AMD Radeon 890M Graphics (RADV STRIX1) (api 1.4.354)
clear readback: rgba=64,128,191,255 checksum=9b6191e8cbea2b83
```

`llvm-readelf --needed-libs .build/glibc-bridge/radv-smoke` reports no
dependencies. An `strace -f -e trace=open,openat` run shows the RADV closure
being opened from the Arch sysroot and no `libc.so.6` or `ld-linux` being
opened.

## Reproduce

Fetch the current Arch packaging closure outside the repository. The script
downloads repository databases and packages, verifies package SHA-256 hashes,
and extracts them under `../arch/root`. glibc is deliberately treated as a
virtual package and is not fetched.

```sh
dev/glibc-bridge/fetch_arch.py
```

The tested snapshot contains 81 packages (99.2 MiB compressed and 391.6 MiB
installed); its exact package versions are recorded in `../arch/manifest.json`.

Build both fully static test executables with the IX musl libc,
compiler-rt-builtins, and Vulkan headers:

```sh
dev/glibc-bridge/build.sh
```

`build.sh` discovers suitable outputs in `/ix/store`. They can instead be
selected explicitly with `MUSL_ROOT`, `COMPILER_RT_ROOT`, and
`VULKAN_HEADERS_ROOT`.

Run the Vulkan loader smoke test:

```sh
.build/glibc-bridge/smoke ../arch/root/usr/lib/libvulkan.so.1
```

Run RADV directly through the ICD interface. The two environment variables
redirect data files whose paths are normally compiled as `/usr/share/...` to
the extracted Arch sysroot:

```sh
arch_sysroot="$(cd ../arch/root && pwd)"
AMDGPU_ASIC_ID_TABLE_PATHS="$arch_sysroot/usr/share/libdrm" \
DRIRC_CONFIGDIR="$arch_sysroot/usr/share/drirc.d" \
  .build/glibc-bridge/radv-smoke \
  "$arch_sysroot/usr/lib/libvulkan_radeon.so"
```

Set `SH_GLIBC_BRIDGE_TRACE=1` to trace the pthread ABI adapters.

## How it works

`elf_loader.c` reserves anonymous memory, copies `PT_LOAD` segments with
`pread`, recursively follows `DT_NEEDED`, resolves versioned symbols with GNU
hash tables, applies relocations, establishes segment/RELRO protections, and
runs dependency constructors. `libc.so.6`, `libm.so.6`, `libpthread.so.0`,
`libdl.so.2`, `librt.so.1`, and `ld-linux-x86-64.so.2` are virtual dependencies:
they are never mapped.

`glibc_shim.c` resolves imported glibc symbols to one of three kinds of
provider:

1. explicit ABI adapters for incompatible interfaces such as pthread objects,
   glibc fortify entry points, large-file aliases, C23 functions, and dlopen;
2. generated direct bindings to ABI-compatible functions in static musl;
3. named fail-on-call traps for imports that are present in the downloaded
   sysroot but have not been implemented.

Foreign pthread mutexes, condition variables, rwlocks, barriers, once objects,
and attributes are associated with native musl objects in side tables. Dynamic
TLS uses per-thread, per-image blocks for `DTPMOD64`, `DTPOFF64`, and TLSDESC
relocations. The x86-64 TLSDESC resolver preserves every register that the
TLSDESC ABI promises to preserve while it calls the lazy TLS allocator; a
normal C-call trampoline here silently corrupts live registers.

## Deliberate limits

This is evidence that the approach is viable, not yet a production dynamic
linker or a complete glibc implementation.

- Only x86-64 `ET_DYN`, GNU hashes, `RELA`/`RELR`, the relocation kinds used by
  this closure, and the observed TLS models are implemented.
- Dependencies are searched only beside the entry DSO. `RPATH`, `RUNPATH`,
  `LD_LIBRARY_PATH`, loader namespaces, auditing, preload, and symbol
  interposition semantics are not implemented.
- ABI adapters cover this tested closure. Unexercised symbols can still hit a
  named trap; glibc internals and arbitrary third-party DSOs are not promised.
- The pthread side tables have fixed capacities and prototype-grade lifetime
  and concurrency handling. Foreign TLS blocks and loader metadata are not
  reclaimed, and DSO destructors/unloading are not implemented.
- ELF validation and overflow checks are incomplete. Loading an untrusted DSO
  is out of scope.
- Only RADV on this host has been exercised. Other Mesa drivers, NVIDIA's
  proprietary stack, display/surface paths, and process teardown need separate
  tests.

The next useful engineering step is to replace generated whole-sysroot symbol
coverage with a per-driver ABI manifest, harden pthread/TLS lifetime handling,
and integrate the loader behind the renderer's normal Vulkan dispatch path.

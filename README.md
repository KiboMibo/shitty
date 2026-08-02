# Shitty

[![codecov](https://codecov.io/gh/pg83/shitty/branch/master/graph/badge.svg)](https://app.codecov.io/gh/pg83/shitty)

**The fastest terminal emulator on Earth.**

Shitty is a hard fork and complete rewrite of **Zutty**. The original Zutty
terminal emulator was created by **Tom Szilagyi**. Shitty keeps that lineage,
but replaces the architecture, renderer, platform integration, testing
strategy, and project identity.

Shitty is built for low latency, fast startup, and predictable resource use.
It keeps terminal state on the CPU and renders cells with native compute
backends: Vulkan on Linux and Metal on macOS.

## Performance

100MB catted through the GUI on an Apple-silicon MacBook, every terminal
equalized first: Menlo 12pt, the same 14x28px cell, an 80x24 grid, 500
lines of scrollback. Best wall time of three runs.

Printable ASCII (the scroll path):

| terminal | wall | user | throughput |
|---|---|---|---|
| **shitty** | **0.81s** | 0.50s | **~118 MiB/s** |
| alacritty 0.17.0 | 0.96s | 0.78s | ~99 MiB/s |
| kitty 0.48.2 | 1.28s | 0.95s | ~75 MiB/s |
| ghostty 1.3.1 | 1.49s | 1.60s | ~64 MiB/s |

Random bytes (the parser's worst case, invalid UTF-8 throughout):

| terminal | wall | user | throughput |
|---|---|---|---|
| **shitty** | **1.88s** | 1.79s | **~51 MiB/s** |
| alacritty 0.17.0 | 3.07s | 2.92s | ~31 MiB/s |
| ghostty 1.3.1 | 4.63s | ~7.0s | ~21 MiB/s |
| kitty 0.48.2 | - | - | - |

kitty sits the random payload out: it reacts to the embedded escape junk
with title changes and bells instead of drawing. Reproduce with
[dev/compare.py](dev/compare.py), which verifies the equalized setup from
inside every terminal before measuring anything.

## Features

- VT52 through VT5xx controls and widely used xterm extensions.
- Primary and alternate screens, primary-screen scrollback, margins, tabs,
  rectangular operations, protected cells, and synchronized output.
- Reflow of primary-screen scrollback when the terminal width changes.
- Unicode grapheme clusters, combining characters, emoji sequences, and
  double-width characters.
- DEC single-width, double-width, and double-height lines.
- 16-colour, 256-colour, and 24-bit colour, including underline colour and
  extended underline styles.
- Legacy, modifyOtherKeys, and Kitty keyboard protocols.
- X10, VT200, UTF-8, SGR, SGR-pixel, and urxvt mouse protocols.
- Linear and rectangular selection, primary selection, clipboard integration,
  OSC 52 policy, and OSC 8 hyperlinks.
- Shell integration, notifications, progress reports, and in-band resize
  reporting.
- Lazy glyph rasterization, a persistent GPU glyph cache, and damage-driven
  compute rendering.

Shitty uses UTF-8 internally and exports `TERM=xterm-256color` to child
processes. The host must provide the corresponding terminfo entry.

## Requirements

Shitty is written in C++23 and built with Clang. Every build requires:

- Python 3 and `glslangValidator`;
- pkg-config;
- Brotli and utf8proc;
- POSIX threads and PTY support.

The exact `libstd` revision used by Shitty is bundled in
`third_party/libstd` and built as part of the same graph.

Linux additionally requires FreeType, HarfBuzz, Wayland client headers,
xkbcommon, `wayland-scanner`, and Vulkan headers and loader. macOS requires
SPIRV-Cross and uses CoreText, Cocoa, Metal, and IOSurface from the system SDK.

A scalar Base64 implementation is always available. If simdutf 6.5 or newer
is installed, the build uses it automatically to accelerate Base64 encoding.
Fontconfig is also optional. When present, it enables font-family lookup;
explicit font file paths are handled directly through FreeType.

Linux requires a working Vulkan driver and Wayland compositor at runtime.
macOS uses the native Metal driver. The native window and event-loop layer is
built from `third_party/plt`; the terminal does not depend on a generic
windowing toolkit.

The complete imported conformance suite additionally needs ncurses and Perl.

## Build

Build the default `install` group:

```sh
./build
```

Common build options:

```sh
./build -j 8
./build -B .build-debug
CPPFLAGS=-DDEBUG ./build
```

## Run

Start the default shell:

```sh
./st
```

Run a command:

```sh
./st -e tmux new-session
```

Choose the initial terminal size and scrollback capacity:

```sh
./st -geometry 120x36 -saveLines 5000
```

Choose fonts:

```sh
./st -font 'DejaVu Sans Mono' -fontsize 16
./st -font 'DejaVu Sans Mono' -dwfont 'Noto Sans Mono CJK JP'
```

`-font` and `-dwfont` accept explicit font file paths. When Fontconfig is
available, they also accept family queries and Shitty resolves regular, bold,
italic, and bold-italic faces automatically. An optional double-width font is
used only when its cell is exactly twice as wide as the primary font at the
same height.

Use `./st -v` to print the build version without opening a window,
`./st -help` for the main option list, and `./st -listres` for advanced
terminal, colour, clipboard, and window-policy options. Boolean flags use
`-flag` to enable and `+flag` to disable. `SHITTY_FONT_SIZE` sets the default
font size; `-fontsize` takes precedence.

During a session, `Ctrl+Shift++` increases the font size, `Ctrl+-` decreases
it, and `Ctrl+0` restores the startup size. Font resizing preserves the
terminal's rows and columns by resizing the window to the new cell dimensions.

By default, applications cannot read local selections through OSC 52 and
cannot manipulate or query the host window. These operations can be enabled
explicitly for trusted applications.

## Install

The executable is named `st`; the desktop application and icon are named
`shitty`:

```sh
install -Dm755 ./st /usr/local/bin/st
install -Dm644 shitty.desktop \
  /usr/local/share/applications/shitty.desktop
install -Dm644 shitty.svg \
  /usr/local/share/icons/hicolor/scalable/apps/shitty.svg
```

`Exec=st` is resolved through `PATH`, while `Icon=shitty` is resolved through
the active icon theme.

### Nix

A flake provides the `shitty` package and a development shell:

```sh
nix build           # ./result/bin/st
nix run             # run st directly
nix develop         # clang toolchain + build dependencies
```

Add the package to a NixOS system from the flake overlay or via:

```nix
{
  inputs.shitty.url = "github:pg83/shitty";
  # ...
  environment.systemPackages = [ inputs.shitty.packages.${system}.default ];
}
```

`shell.nix` remains available for `nix-shell` without flakes.

## Tests

Run the full native and imported conformance suite:

```sh
./build test
```

Run only the native black-box suite:

```sh
./build test_suite
```

Run the same normal and sanitizer chains as GitHub CI:

```sh
nix build -L --no-link .#checks.x86_64-linux.build &&
  nix build -L --no-link .#checks.x86_64-linux.tests
nix build -L --no-link .#checks.x86_64-linux.build-asan &&
  nix build -L --no-link .#checks.x86_64-linux.tests-asan
nix build -L --no-link .#checks.x86_64-linux.build-ubsan &&
  nix build -L --no-link .#checks.x86_64-linux.tests-ubsan
```

Build an instrumented copy of the complete suite and generate LCOV, text, and
browsable HTML reports:

```sh
nix build -L -o result-coverage .#checks.x86_64-linux.coverage
xdg-open result-coverage/html/index.html
```

The same report is attached to every GitHub coverage run and uploaded to
Codecov for per-file and pull-request coverage.

The native suite drives a dedicated headless `st_test` binary through a real
raw PTY and checks externally visible terminal snapshots and output. The
production `st` binary does not expose the test control entry point.

## Known limits

Shitty does not currently implement bidirectional text layout or inline
graphics protocols such as sixel. Some historical DEC and xterm extensions
are intentionally outside the supported profile.

## License transition and authorship

Shitty is moving from the imported GPL baseline to an MIT-only codebase. It
does not intend to retain the GPL as the final project license.

The source snapshot first imported into this repository, and code predating
that snapshot, remains licensed under GPLv3-or-later. New Shitty contributions
are dual-licensed under GPLv3-or-later and MIT. While GPL-only imported material
remains in the tree, distribution of the combined work is still subject to
the GPL.

See `LICENSE`, `LICENSE.GPL3`, `LICENSE.MIT`, and `CONTRIBUTING.md` for the
exact terms and contribution policy.

Tom Szilagyi is the original author of Zutty, from which Shitty descends.
Shitty retains his copyright notices where historical code lineage requires
them; subsequent work is copyright of the Shitty contributors.

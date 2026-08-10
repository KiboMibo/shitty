# Shitty / Pretty

[![CI](https://github.com/pg83/shitty/actions/workflows/ci.yml/badge.svg)](https://github.com/pg83/shitty/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/pg83/shitty/branch/master/graph/badge.svg)](https://app.codecov.io/gh/pg83/shitty)
[![release](https://img.shields.io/github/v/release/pg83/shitty)](https://github.com/pg83/shitty/releases/latest)
[![brew](https://img.shields.io/badge/brew-pg83%2Ftap%2Fshitty-2a6e3f?logo=homebrew)](https://github.com/pg83/homebrew-tap)
[![license](https://img.shields.io/badge/license-MIT%20%7C%20GPL--3.0-blue)](LICENSE)
[![platforms](https://img.shields.io/badge/platforms-macOS%20%7C%20Linux-8a8a8a)](#requirements)
[![speed](https://img.shields.io/badge/ascii-118%20MiB%2Fs%20%C2%B7%201.2%C3%97%20alacritty-ffb000)](#performance)

**Blazingly fast. Memory-unsafe and faster than yours.**

Shitty is built for low latency, fast startup, and predictable resource use.
It keeps terminal state on the CPU and renders cells with native compute
backends: Vulkan on Linux and Metal on macOS.

The same terminal is built with two user-facing brands. `st` is Shitty;
`pt` is Pretty, for people who prefer a polite name. They share all terminal
code and differ only in their name, application identity, config and public
environment names, help/version text, desktop entry, and icon.

## Performance

100MB catted through the GUI on an Apple-silicon MacBook, every terminal
equalized first: Menlo 12pt, the same 14x28px cell, an 80x24 grid, 500
lines of scrollback. Best wall time of three runs.

Printable ASCII (the scroll path):

| terminal | wall | user | throughput |
|---|---|---|---|
| ghostty 1.3.2-main (nightly) | 0.56s | 0.62s | ~170 MiB/s |
| **shitty** | **0.81s** | 0.50s | **~118 MiB/s** |
| alacritty 0.17.0 | 0.96s | 0.78s | ~99 MiB/s |
| kitty 0.48.2 | 1.28s | 0.95s | ~75 MiB/s |
| ghostty 1.3.1 | 1.49s | 1.60s | ~64 MiB/s |

Random bytes (the parser's worst case, invalid UTF-8 throughout):

| terminal | wall | user | throughput |
|---|---|---|---|
| **shitty** | **1.88s** | 1.79s | **~51 MiB/s** |
| alacritty 0.17.0 | 3.07s | 2.92s | ~31 MiB/s |
| ghostty 1.3.2-main (nightly) | 3.37s | 5.22s | ~28 MiB/s |
| ghostty 1.3.1 | 4.63s | ~7.0s | ~21 MiB/s |
| kitty 0.48.2 | - | - | - |

kitty sits the random payload out: it reacts to the embedded escape junk
with title changes and bells instead of drawing. The ghostty nightly row
is the official tip build (1.3.2-main+1f6e26642), measured at its
author's request - the released 1.3.1 numbers stay for comparison.
Reproduce with [dev/compare.py](dev/compare.py), which verifies the
equalized setup from inside every terminal before measuring anything.

## Why

- **Fast.** See the tables above; `dev/compare.py` reproduces them.
- **Correct.** More than 5,000 tests, harvested from over a dozen
  suites - kitty, esctest, xterm's vttests, vttest, tack, libvterm,
  libtsm, alacritty, ghostty, contour, konsole, mosh - and driven
  black-box through a real PTY.
- **Flicker-free.** Resize frames render inside the same transaction
  as the bounds change; updates are damage-driven.
- **Indestructible.** The parser state machine is total and fuzzed
  with committed corpora: `cat /dev/urandom` is a benchmark here, not
  a crash report.
- **Unicode done right.** Cells are grapheme clusters, not codepoints:
  emoji sequences, variation selectors, combining marks, wide CJK.
- **Self-contained.** One small binary, no windowing toolkit, fonts
  embedded - it starts on a machine with no fonts installed at all.
- **Locked down by default.** Applications cannot read selections or
  drive the host window unless explicitly allowed.

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

Shitty is written in C++23 and built with Clang. The bundled `libstd`
needs `-std=c++26`, which the Apple command-line-tools clang does not
know: on macOS install LLVM from Homebrew and point the build at it
(`export CC="$(brew --prefix llvm)/bin/clang"`, same for `CXX` with
`clang++`). Every build requires:

- Python 3, Ragel 6 or 7, and `glslangValidator`;
- librsvg (`rsvg-convert`), which renders the icon at build time;
- pkg-config;
- POSIX threads and PTY support.

Unicode 17 properties are generated at build time from the UCD files bundled
in `third_party/unicode`; no system Unicode library is required.

Either Ragel generation works. Ragel 7 dropped the `-x` flag that
`check_parser_totality.py` needs, so under it that check is skipped; the
generated parser is the same either way.

The exact `libstd` revision used by Shitty is bundled in
`third_party/libstd` and built as part of the same graph.

Linux additionally requires FreeType, HarfBuzz, Wayland client headers,
xkbcommon, `wayland-scanner`, and Vulkan headers and loader. macOS requires
SPIRV-Cross and uses CoreText, Cocoa, Metal, and IOSurface from the system SDK.

liburing and xxhash are optional and need no configuration: `libstd`
detects their headers and the build links whatever they turn on, giving
an io_uring reactor and a faster hash where they are installed. rapidhash
is header-only and supersedes xxhash when present.

Brotli and simdutf are optional: Brotli only satisfies FreeType's
static-link dependency chain where that applies, and simdutf 6.5 or
newer accelerates Base64 over the always-available scalar
implementation. Font families are resolved by
CoreText on macOS and by Fontconfig (optional) on Linux; explicit font
file paths work everywhere, whichever backend rasterizes them.

Linux requires a working Vulkan driver and Wayland compositor at runtime.
macOS uses the native Metal driver. The native window and event-loop layer is
built from `third_party/plt`; the terminal does not depend on a generic
windowing toolkit.

The complete imported conformance suite additionally needs ncurses, Perl,
and vttest.

## Build

Build the default `install` group:

```sh
./build
```

This builds both `st` and `pt`.

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

Use `./pt` instead for the Pretty brand; every option below is identical.

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
./st -font 'DejaVu Sans Mono' -font 'Noto Sans Mono CJK JP'
```

`-font` accepts a family name or an explicit font file path and may be
repeated: later fonts serve as fallbacks, picked per cluster by glyph
coverage. Regular, bold, italic, and bold-italic faces resolve
automatically. A vendored monospace-and-emoji trio is embedded in the
binary as the last resort, so the terminal starts even on a system with
no fonts installed at all.

Use `./st -v` to print the build version without opening a window,
`./st -help` for the main option list, and `./st -listres` for advanced
terminal, colour, clipboard, and window-policy options. Boolean flags use
`-flag` to enable and `+flag` to disable. `SHITTY_FONT_SIZE` sets the default
font size for `st`; `PRETTY_FONT_SIZE` does the same for `pt`. `-fontsize`
takes precedence.

### Config file

Every configurable option can also be set in `~/.config/shitty/shitty.toml`
(`$XDG_CONFIG_HOME` is honored), or in an explicit file passed with
`-config path.toml`. Keys are the option names from `-help` and
`-listres`; the file is TOML, parsed by a built-in parser that passes the
full `toml-test` 1.0 suite. `${NAME}` anywhere in the file expands to the
process environment variable before parsing. Command-line flags take
precedence over the file, and a broken or unknown entry prints a warning
to stderr without keeping the terminal from starting. The repository's
[`shitty.toml`](shitty.toml) is a working example that documents every
option, including the command-line-only controls. Pretty uses
`~/.config/pretty/pretty.toml` and the equivalent [`pretty.toml`](pretty.toml):

```toml
fontsize = 16
font = ["DejaVu Sans Mono", "Noto Sans Mono CJK JP"]
geometry = "120x36"
saveLines = 5000
boldColors = false
color4 = "#3465a4"
```

Send `SIGUSR1` to a running terminal to parse the same config sources again.
Command-line overrides are reapplied, and a valid result is published as one
new immutable snapshot; a syntax or value error leaves the current snapshot
active. Runtime components then reapply their own configuration (including
fonts, terminal colours and defaults, borders, and key remaps). Options used
only to create process or window state take effect on the next launch.

### Key remapping

`-remap from=to` rewrites one key chord into another before anything else
sees it, so the substitution applies equally to the bytes sent to the
application, the kitty keyboard protocol, and the terminal's own
shortcuts. Repeat the flag for more rules, or put a list into the config
file. A chord is modifiers (`ctrl`, `alt`, `shift`, `super`) joined with
`+` around a single character or a named key - every name from the input
layer works (`enter`, `f5`, `pageup`, `keypad5`, ...). The target `none`
swallows the chord. Characters match the ASCII layout of the keyboard, so
a remap keeps working under any active layout, and a remapped press keeps
its identity through repeat and release:

```toml
remap = ["ctrl+b=ctrl+d", "super+t=ctrl+shift+t", "ctrl+l=none"]
```

### Plain URIs

Ctrl-hover highlights a URI detected in plain text and Ctrl-click opens
it, but only for schemes on the `-uriScheme` list — everything else
stays ordinary text instead of being handed to an opener that has no
handler for it. The default list is `http`, `https`, `file`; a
configured list replaces it outright. Explicit OSC 8 hyperlinks are
authoritative and ignore the list. To see which schemes your desktop
actually registers handlers for:

```sh
grep -ho 'x-scheme-handler/[a-z0-9.+-]*' \
  /usr/share/applications/mimeinfo.cache \
  ~/.local/share/applications/mimeinfo.cache 2>/dev/null | sort -u | cut -d/ -f2
```

```toml
uriScheme = ["http", "https", "file", "mailto", "gemini"]
```

During a session, `Cmd+=`/`Cmd+-`/`Cmd+0` on macOS (`Ctrl+Shift+=`/
`Ctrl+-`/`Ctrl+0` on Linux) raise, lower, and restore the font size. Font
resizing preserves the terminal's rows and columns by resizing the window
to the new cell dimensions.

By default, applications cannot read local selections through OSC 52 and
cannot manipulate or query the host window. These operations can be enabled
explicitly for trusted applications.

## Install

### Homebrew (macOS, Apple silicon)

```sh
brew install pg83/tap/shitty
brew install pg83/tap/pretty
```

The [tap](https://github.com/pg83/homebrew-tap) tracks both formulae from the latest
release automatically. The same portable binaries (`st-darwin-arm64.tar.gz`
and `pt-darwin-arm64.tar.gz`, nothing dynamically linked outside the system)
are attached to every
[GitHub release](https://github.com/pg83/shitty/releases).

### Linux

Both brands are installed side by side:

```sh
install -Dm755 ./st /usr/local/bin/st
install -Dm755 ./pt /usr/local/bin/pt
install -Dm644 shitty.desktop \
  /usr/local/share/applications/shitty.desktop
install -Dm644 shitty.svg \
  /usr/local/share/icons/hicolor/scalable/apps/shitty.svg
install -Dm644 pretty.desktop \
  /usr/local/share/applications/pretty.desktop
install -Dm644 pretty.svg \
  /usr/local/share/icons/hicolor/scalable/apps/pretty.svg
```

The desktop files resolve `st`/`pt` through `PATH` and their icons through
the active icon theme.

### Nix

A flake provides the `shitty` package and a development shell:

```sh
nix build           # ./result/bin/st and ./result/bin/pt
nix run             # run st directly
nix run .#pretty    # run pt directly
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

Shitty is a hard fork and complete rewrite of **Zutty**. The original Zutty
terminal emulator was created by **Tom Szilagyi**. Shitty keeps that lineage,
but replaces the architecture, renderer, platform integration, testing
strategy, and project identity.

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

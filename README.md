# Shitty

**A small, fast, native Wayland terminal emulator with a Vulkan renderer.**

Shitty is a hard fork and complete rewrite of **Zutty**. The original Zutty
terminal emulator was created by **Tom Szilagyi**; Shitty preserves that
lineage while replacing the architecture, platform integration, renderer,
testing strategy, and project identity. The project is actively removing,
rewriting, or relicensing the remaining GPL-only imported code in order to
complete its transition to the MIT License.

The terminal core implements the commonly used VT52 through VT5xx command
families together with xterm extensions, Unicode grapheme handling, scrollback,
selection, modern keyboard protocols, mouse reporting, and 24-bit colour. GLFW
provides native Wayland window/input integration, while Vulkan presents the
terminal without uploading a CPU-rendered full-frame bitmap.

## Design

```text
 child process
      ⇅ PTY
    Vterm             parser, modes, reports, input encoding
      ⇅
    Frame             cells, damage, selection, circular scrollback
      ↓ changed cells                 Fontpack
   Renderer       ← lazy glyph rasterization ┘
      ↓ SSBO + compute shader (`render.comp`)
 persistent RGBA8 image
      ↓ blit
 Wayland swapchain    GLFW supplies window-system integration
```

The terminal model is deliberately separated from the frontend. `Vterm`
consumes PTY bytes and mutates `Frame`; `Frame` owns canonical terminal state
and damage; `Renderer` materializes full or delta cell data into its host-side
mirror, then composites cells, glyphs, cursor, selection, and decorations with
a compute shader.

Important boundaries:

- Wayland is the native window-system target.
- GLFW handles windows, input, clipboard access, and Vulkan surface creation.
- Vulkan handles all terminal image composition and presentation.
- UTF-8 is the host encoding; legacy host encodings are out of scope.
- Configuration is command-line based, without an Xresources or config-file
  compatibility layer.
- The model can run headlessly for tests, fuzzing, corpus replay, and profiling.

## Features

- VT52, VT100, VT102, VT220, VT320, VT420, and VT520-era controls.
- xterm-compatible modes, reports, title operations, OSC, and DCS handling.
- Primary and alternate screens, vertical and horizontal margins, tabs,
  autowrap, insert/delete, protected cells, and rectangular operations.
- Circular scrollback with primary-screen reflow on resize.
- Unicode grapheme clusters, combining characters, emoji sequences, and CJK
  double-width cells.
- 16-colour, 256-colour, and true-colour cells, including underline colour and
  extended underline styles.
- Legacy, modifyOtherKeys, and Kitty keyboard encoding.
- X10, VT200, UTF-8, SGR, SGR-pixel, and urxvt mouse protocols.
- Local selection, primary selection, clipboard integration, OSC 52 policy,
  hyperlinks, shell integration, notifications, and progress reports.
- GPU-resident font atlases and dirty-cell compute rendering.
- Synchronized output and in-band resize reporting.

## Requirements

Shitty is built with Clang and C++23. The build environment needs:

- Python 3 and `glslangValidator`;
- `libstd` (vendored as `third_party/libstd` or available system-wide);
- pkg-config;
- Brotli and utf8proc;
- FreeType and fontconfig;
- GLFW 3.4 or newer with Wayland support;
- Vulkan headers, loader, and a presentation-capable driver;
- POSIX threads;
- ncurses and Perl for the complete imported conformance suite.

At runtime, a Wayland session and a working Vulkan ICD are required.

## Build

The checked-in wrapper enters the project development environment and builds
the `st` target:

```sh
./build.sh
```

Additional build-runner arguments are forwarded unchanged:

```sh
./build.sh -j 8
./build.sh -B .build-debug
CPPFLAGS=-DDEBUG ./build.sh
```

The underlying command is:

```sh
$HOME/monorepo/ix/ix run bld/perl set/pg/libs -- ./build st
```

The explicit target publishes `./st` as a symlink to the
content-addressed build result.

## Run

Choose an installed monospace font when the default fontconfig alias is not
appropriate:

```sh
./st -font DejaVuSansMono -fontsize 16
```

Examples:

```sh
./st -geometry 120x36 -saveLines 5000
./st -font LiberationMono -e tmux new-session
./st -vulkanInfo -font DejaVuSansMono
./st perf tests/realworld/input
```

Shitty exports `TERM=xterm-256color` and `SHITTY_VERSION` to its child process.
The system must provide the corresponding terminfo entry.

Use `./st -help` for the authoritative option list and
`./st -listres` for advanced protocol, palette, clipboard, and window
policy options. Flags use `-flag` to enable and `+flag` to disable. The
`SHITTY_FONT_SIZE` environment variable supplies a font-size default;
`-fontsize` takes precedence.

Security-sensitive defaults include:

- `allowOsc52Read=false`, so applications cannot read local selections;
- `allowWindowOps=false`, so applications cannot manipulate or query the host
  window;
- window-operation and clipboard access can be enabled explicitly for trusted
  applications.

## Fonts

`-font` and `-dwfont` are fontconfig family queries. Shitty resolves regular,
bold, italic, and bold-italic variants and falls back to the regular face when
a style is unavailable. The optional double-width font must rasterize to
exactly twice the primary cell width at the same height.

```sh
./st -font 'DejaVu Sans Mono' -dwfont 'Noto Sans Mono CJK JP'
```

## Tests

Run the full build and conformance graph through the checked-in wrapper:

```sh
./test.sh
```

To run only the native black-box suite:

```sh
./test.sh test_suite
```

The native harness starts the dedicated `st_test` build in headless mode,
connects it to a real raw PTY, and observes canonical snapshots rather than
calling parser internals. Production `st` does not compile the control protocol
or its test-only environment hooks. The larger graph also imports or adapts
cases from Alacritty,
Contour/vttest, esctest, Ghostty, Kitty, Konsole, libvterm, Mosh, tack,
Termless, tmux, ucs-detect, VTE, WezTerm, Windows Terminal, xterm, and xterm.js,
plus recorded real-world terminal streams.

Sanitizer builds can use a separate cache:

```sh
CXXFLAGS='-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer -g' \
LDFLAGS='-fsanitize=address,undefined' \
ASAN_OPTIONS='detect_leaks=1:abort_on_error=1' \
UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1' \
./test.sh -B .build-asan-ubsan
```

## Source map

- `vterm.*` — VT parser, terminal modes, reports, keyboard, mouse, and OSC/DCS.
- `frame.*` — canonical cells, scrollback, resize/reflow, damage, and selection.
- `application.*` — Wayland/GLFW event loop, child process, clipboard, and UI.
- `vk_renderer.*` — render bridge, glyph cache, Vulkan composition, and presentation.
- `render.comp` — terminal compute compositor, embedded as SPIR-V at build time.
- `font.*`, `font_pack.*`, `font_resolver.*` — fontconfig and FreeType atlases.
- `test_mode.*`, `tests/harness.py` — test-build-only control protocol and snapshots.
- `build.py` — product, helper, and conformance-suite build graph.

## Known limits

The project does not currently implement bidirectional text layout, DEC
double-height/double-width line modes, sixel graphics, or every historical DEC
and xterm extension. Compatibility differences tracked as intentional XFAILs
remain visible in the imported suites.

## Installation

```sh
install -Dm755 ./st /usr/local/bin/st
install -Dm644 shitty.desktop \
  /usr/local/share/applications/shitty.desktop
install -Dm644 shitty.svg \
  /usr/local/share/icons/hicolor/scalable/apps/shitty.svg
```

The executable name is independent of the desktop identity. `Exec=st` resolves
the executable through `PATH`; the `shitty.desktop` filename matches the
Wayland `app_id`; and `Icon=shitty` resolves `shitty.svg` through the active
icon theme.

## License transition and authorship

Shitty does not intend to preserve GPL licensing. The project is transitioning
to the MIT License, and its target is an MIT-only codebase and release.

The current tree still contains parts of the imported Zutty baseline licensed
under the GNU General Public License, version 3 or later. While that material
remains, distribution of the combined work is subject to the GPL. New Shitty
contributions are dual-licensed under GPLv3-or-later and MIT so that they can
remain in the project after the GPL-only baseline has been removed, rewritten,
or separately relicensed. See `LICENSE`, `LICENSE.GPL3`, `LICENSE.MIT`, and
`CONTRIBUTING.md` for the exact terms and contribution policy.

Tom Szilagyi is the original author of Zutty, from which this hard fork and
complete rewrite descends. Shitty retains his copyright notices where the
historical code lineage requires them and identifies subsequent work as
copyright of the Shitty contributors.

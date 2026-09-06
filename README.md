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

- Native macOS and Linux/Wayland frontends, with Metal and Vulkan compute
  rendering, HiDPI support, and true Wayland fractional scaling.
- VT52 through VT5xx and ECMA-48 controls, ISO-2022 character sets, and the
  widely used xterm extensions.
- Primary and alternate screens, configurable primary-screen scrollback,
  horizontal and vertical margins, tab stops, rectangular operations,
  protected cells, synchronized output, and focus reporting.
- Reflow of primary-screen scrollback when the terminal width changes, while
  preserving selections, hyperlinks, shell marks, and wide glyphs.
- Unicode 17 grapheme clusters: combining characters, emoji ZWJ and variation
  sequences, regional indicators, and double-width CJK, with selectable
  historical width tables for local and remote compatibility.
- Per-cluster font fallback, regular/bold/italic/bold-italic faces,
  cross-cell ligatures, colour emoji, runtime font zoom, and optional
  unhinted subpixel rendering with stem darkening.
- Embedded monospace and emoji fallbacks, so the terminal remains usable with
  no system fonts installed; family names and explicit font files can be mixed
  into an ordered fallback chain.
- DEC single-width, double-width, and double-height lines, plus synthesized
  box drawing, block elements, scan lines, dentistry, and media symbols that
  join independently of the selected font.
- 16-colour, 256-colour, and 24-bit colour; bold, faint, italic, blink,
  inverse, conceal, strike, and overline; coloured single, double, curly,
  dotted, and dashed underlines.
- Runtime palette and default/cursor/selection colour changes and queries,
  backed by more than 1,700 named colour schemes assembled from the major
  terminal theme collections.
- Sixel inline images with colour registers, raster attributes, repetition,
  scrolling, clipping, erase semantics, and capability queries.
- Legacy, xterm `modifyOtherKeys`, and Kitty keyboard protocols, including
  press/repeat/release events, associated text, keypad modes, and arbitrary
  layout-stable chord remapping.
- X10, VT200, button-event, any-event, UTF-8, SGR, SGR-pixel, urxvt, and DEC
  locator mouse protocols, plus alternate-screen wheel-to-cursor mode.
- Native Cocoa and Wayland `text-input-v3` IME composition, including visible
  preedit text and cursor ranges.
- Character, word, line, and rectangular mouse selection; drag autoscroll;
  primary selection; system clipboard; bracketed paste; and optional
  automatic primary-to-clipboard copying.
- Explicit OSC 8 hyperlinks and detected plain URIs, with hover feedback,
  configurable allowed schemes, and native opening on click.
- OSC 52 and MIME-aware Kitty clipboard protocols, including gated clipboard
  reads and paste MIME notifications; application window operations are also
  separately gated and disabled by default.
- Multiple independent PTY tabs in one window, with keyboard and direct-index
  navigation, per-tab titles, background-session isolation, and a clickable
  title-bar tab strip on macOS. The strip can instead be a vertical list down
  the window's edge, whose rows carry what is running, the shell's working
  directory, and the checked-out git branch, with Nerd Font icons when the
  font has them.
- Optional splitting of a tab into panes, each an independent terminal with
  its own shell, size, and scrollback: split by chord, move the focus by
  click, drag the seam to resize both neighbours, and close a pane back into
  its neighbour. The seam between panes is drawn in the air the panes' own
  borders already leave, with a configurable thickness and colour.
- An optional quick-terminal window on macOS: hidden at startup, toggled by a
  global hotkey, with a remembered position and size, a fullscreen chord,
  rounded corners, and a titlebar tinted to the terminal background.
- Optional auto-hiding window chrome that reappears on hover without changing
  the terminal's row count.
- OSC 7 working-directory and OSC 133 semantic shell integration, OSC 9 and
  OSC 99 attention notifications, OSC 9;4 progress states, light/dark scheme
  reports, and in-band cell/pixel resize reports.
- `XTVERSION`, `XTGETTCAP`, primary/secondary/tertiary device attributes,
  DECRQSS state reports, iTerm2 capability reporting, and `TERM_FEATURES` for
  feature discovery without terminal-name guessing.
- Native file/URI and text drag-and-drop into the terminal on macOS and
  Wayland.
- A TOML configuration with imports, environment expansion, CLI overrides,
  colour schemes, fallback lists, and atomic `SIGUSR1` runtime reload; invalid
  reloads leave the current configuration active.
- Lazy glyph rasterization, a persistent GPU glyph cache, damage-driven
  rendering, and transactional, flicker-free resize frames.
- One self-contained binary per brand, no generic windowing toolkit, and
  conservative clipboard and host-window access policies by default.

Three of the four options in the entries above — the sidebar tab list, panes,
and auto-hiding chrome — are **on by default**; the quick-terminal window is
not, since it is a second way to run the program rather than a feature of the
first. `+tabBar`-style spellings turn each of them off: `-tabBar top`,
`+panes`, `+autoHideChrome`. Every one of them is a whole option away from
the behaviour this fork started with, and
[`bin/st/shitty.toml`](bin/st/shitty.toml) — which `st -printConfig` writes
out — carries the value of each, so there is one file to read rather than a
set of opinions to discover.

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
in `ext/unicode`; no system Unicode library is required.

Either Ragel generation works. Ragel 7 dropped the `-x` flag that
`lib/shitty/check_parser_totality.py` needs, so under it that check is skipped; the
generated parser is the same either way.

The exact `libstd` revision used by Shitty is bundled in
`ext/libstd` and built as part of the same graph.

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
built from `ext/plt`; the terminal does not depend on a generic
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
[`shitty.toml`](bin/st/shitty.toml) is a working example that documents every
option, including the command-line-only controls. Pretty uses
`~/.config/pretty/pretty.toml` and the equivalent [`pretty.toml`](bin/pt/pretty.toml):

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

### Tab bar, panes, and the quick window

These are opt-in; with none of them set the window is the one described
everywhere above.

`-tabBar sidebar` moves the tab list from the title-bar strip to a vertical
column down the window's edge, `-sidebarWidth` sets its width in points, and
`Cmd+B` hides and shows that column — hides it, rather than moving the tabs
back to the top. The chord exists only while the sidebar is the chosen
placement.

Under `-backgroundBlur glass` the active tab sits on a pill of glass, and
`-sidebarTabTint` says how opaque that pill is, `0` to `100` on the same scale
as `-backgroundOpacity`: `100` paints it the terminal background flat, `0`
leaves it clear with the desktop straight through. The default `65` keeps the
active title readable over both a dark and a light desktop. The other two
backdrops draw no pill and ignore it.

`-panes` enables splitting a tab's terminal. `Cmd+D` splits the focused pane
vertically and `Cmd+Shift+D` horizontally; a click moves the focus, `Cmd+W`
closes the focused pane and only closes the tab once its last pane is gone,
and dragging the seam resizes both neighbours, telling both shells their new
size. Without `-panes` the chords are not claimed at all and reach the
program running in the terminal.

`-paneDividerWidth` and `-paneDividerColor` control the seam. The seam is
painted into the air the panes' own borders already leave, so it takes no
space from either pane — but that also means **it has nowhere to go when
`-border` is `0`**: the two grids touch, and no thickness will make a seam
appear. With the default border of `2` there are four pixels of air and a
one-pixel seam in `#00cd00` — the same green under every colour scheme,
because a seam has to be found by the eye and aimed at by the mouse.

```sh
./st -tabBar sidebar -panes -paneDividerWidth 2
```

`-quick` starts the window hidden and binds `-quickHotkey` to toggle it, with
`-quickGeometry` for its size and place, `-quickRememberFrame` to keep a
position you set by hand across shows, `-quickFullscreenHotkey` for a
fullscreen toggle, and `-quickCornerRadius` for rounded corners.
`-transparentTitlebar` tints the title bar to the terminal background, and
`-autoHideChrome` hides the chrome until the pointer reaches it. The
quick-terminal window and both tab-bar placements are macOS-only.

### Plain URIs

Ctrl-hover highlights a URI detected in plain text and Ctrl-click opens
it, but only for schemes on the `-uriScheme` list — everything else
stays ordinary text instead of being handed to an opener that has no
handler for it. The default list is `http`, `https`, `file`, `mailto`,
`gemini`; a configured list replaces it outright. Explicit OSC 8 hyperlinks are
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

The formulae install bare binaries; `dev/make_app.sh` wraps an already-built
`st`/`pt` into a proper `Shitty.app`/`Pretty.app` bundle (Dock/Finder launch,
menu bar name, `Info.plist` icon instead of the runtime fallback):

```sh
./dev/make_app.sh
```

If another formula already owns `pt` on `PATH` (`tcl-tk` does), run
`brew link --overwrite pretty` first so the script picks up the right binary.

Prebuilt bundles (`Shitty.app.zip`, `Pretty.app.zip`) are attached to every
[GitHub release](https://github.com/pg83/shitty/releases) too, for anyone who
would rather not build from Homebrew. They are only ad-hoc signed (no Apple
Developer ID, no notarization), so macOS Gatekeeper blocks the first launch
with "Apple could not verify...". Either right-click the app and choose Open,
or clear the quarantine flag yourself: `xattr -cr Shitty.app` (or
`Pretty.app`).

### Linux

Both brands are installed side by side:

```sh
install -Dm755 ./st /usr/local/bin/st
install -Dm755 ./pt /usr/local/bin/pt
install -Dm644 bin/st/shitty.desktop \
  /usr/local/share/applications/shitty.desktop
install -Dm644 bin/st/shitty.svg \
  /usr/local/share/icons/hicolor/scalable/apps/shitty.svg
install -Dm644 bin/pt/pretty.desktop \
  /usr/local/share/applications/pretty.desktop
install -Dm644 bin/pt/pretty.svg \
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
graphics protocols such as Kitty graphics or iTerm2 inline images. Sixel is
supported. Some historical DEC and xterm extensions are intentionally outside
the supported profile.

The window features described above — the quick-terminal window, both tab-bar
placements, and auto-hiding chrome — are implemented for macOS only. On
Linux/Wayland their options parse and are accepted, and nothing appears.

`-backgroundOpacity` and `-backgroundBlur` belong to that list.

`-backgroundBlur off|blur|glass` takes a value rather than standing as a
bare flag: it says what to put behind a translucent background. `off` puts
nothing there, `blur` a blur of the desktop, and `glass` the system's glass
material, which falls back to the blur where the system has none. The
default is `glass` over an opacity of `60`. None of the three shows
anything while `-backgroundOpacity` is 100, and asking for one at 100 prints
a line saying so rather than refusing to start. A config
written when this was a flag keeps working — `backgroundBlur = true` reads
as `blur` and `false` as `off` — but a bare `-backgroundBlur` on the
command line is now an error rather than a way to switch it on.

`-backgroundOpacity` is worth spelling out because its absence is silent
rather than obviously unimplemented: on the Vulkan backend it is deliberately
not honoured, and the background stays solid at every value. Alpha only
reaches the screen through a swapchain created with a composite-alpha mode
the compositor accepts, and this chain asks for none, so the alpha channel is
discarded — a premultiplied colour written into it would render the
background *darker* rather than see-through. Half-honouring the option is
worse than not honouring it, so the Vulkan renderer reports an opacity of 100
unconditionally. If you build for Linux and see no translucency, that is this
line and not a broken driver.

**The Vulkan side of the pane and divider work has never been compiled or
run.** It was written by reading the Metal backend beside it and by reasoning
about buffer layouts and barriers, on a machine with no cross-build; no
compiler and no GPU has seen it. Treat the Linux rendering path for panes as
unverified until someone builds it, and expect to fix it rather than to find
it working. The Vulkan backend also refuses a frame carrying more than one
pane today, so even a successful build shows a single terminal per window.

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

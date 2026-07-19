# Zutty

**Zero-cost Unicode Teletype — a high-end terminal for low-end systems.**

Zutty is a compact C++ terminal emulator with a deliberately small platform
surface: GLFW handles the Wayland window, input and clipboard, while
the image is presented with raw Vulkan.

The terminal engine is the mature part of the project. It implements the
commonly used VT52 through VT5xx command families, xterm extensions, a
scrollback ring buffer, mouse protocols, selection and 256/true colour. Its
Vulkan renderer consumes the compact cell buffer and FreeType atlases in a
compute shader, writes a persistent RGBA8 storage image and blits that image
into the window-system swapchain. No full-size terminal image is rasterized or
uploaded by the CPU.

## Design boundaries

- Native Wayland. GLFW creates the window and Vulkan surface, but does not
  render the terminal.
- UTF-8 only. Legacy host encodings are intentionally outside the scope.
- Configuration is command-line only; there is no Xresources compatibility
  layer or configuration file parser.
- The terminal core and the platform/rendering frontend remain separate, so
  work on one does not require rewriting the other.

## Features

- VT52, VT100, VT102, VT220, VT320, VT420 and VT520-era control sequences,
  plus the xterm sequences used by modern terminal applications.
- UTF-8 input and Basic Multilingual Plane glyphs, including a separate
  double-width font for CJK cells.
- 16-colour, 256-colour and 24-bit colour; bold, italic, underline, inverse
  and background-colour erase.
- Primary and alternate screens, margins, origin mode, insert/delete,
  autowrap, tabs, reports and the usual DEC character sets.
- O(1) scrolling over a circular screen/scrollback store.
- VT/xterm keyboard modes and xterm-style modifier encoding.
- X10, VT200, UTF-8, SGR and urxvt mouse reporting.
- Local select-to-paste and system clipboard integration on Wayland.
- Scalable TTF, OTF and TTC fonts, plus PCF and compressed PCF bitmap fonts.
- Dirty-cell Vulkan compute rendering over GPU-resident font atlases.
- High-density windows and resize-aware Vulkan swapchain recreation.

## Requirements

Build-time requirements are:

- a C++17 compiler;
- Meson 1.2 or newer and Ninja;
- Python 3 and `glslangValidator` for embedding the compute shader;
- pkg-config;
- FreeType 2;
- GLFW 3.4 or newer, built with Wayland support;
- Vulkan headers and loader;
- POSIX threads.

At runtime Zutty needs a Wayland session, a GLFW build with its Wayland backend
and a Vulkan driver capable of presenting to Wayland. In practice that means a
working Vulkan ICD in addition to the loader.

## Build

The repository includes a Nix development shell with the required tools and
libraries:

```sh
nix-shell
meson setup build --buildtype=release -Db_lto=true
meson compile -C build
./build/zutty
```

Without Nix, install the dependencies through the system package manager and
run the same Meson commands. To install the executable, desktop entry and
scalable icon using Meson's selected prefix:

```sh
meson install -C build
```

For a development build with assertions and VT parser stepping enabled:

```sh
meson setup build-debug --buildtype=debug -Dwerror=true
meson compile -C build-debug
./build-debug/zutty -verbose
```

## Run

The built-in bitmap defaults are not installed on every system, so explicitly
choosing an available monospace font is often the best first run:

```sh
./build/zutty -font DejaVuSansMono -fontsize 16
```

Other examples:

```sh
# A larger terminal and scrollback buffer
./build/zutty -geometry 120x36 -saveLines 5000

# Run a command instead of the login shell
./build/zutty -font LiberationMono -e tmux new-session

# Inspect the selected Vulkan device
./build/zutty -vulkanInfo -font DejaVuSansMono
```

Zutty sets `TERM=xterm-256color` and exports its version as `ZUTTY_VERSION` to
the child process. The system therefore needs the corresponding terminfo entry.

## Command line

Use `zutty -help` for the authoritative option list. Options may be shortened
to an unambiguous prefix; the historical `-v` spelling still means
`-verbose`. A flag is enabled with `-flag` and disabled with `+flag`, which is
notably useful for defaults such as `+boldColors`.

| Option | Default | Meaning |
| --- | --- | --- |
| `-geometry COLSxROWS` | `80x24` | Initial terminal geometry in cells |
| `-font NAME` | `monospace` | Primary font filename prefix |
| `-dwfont NAME` | `18x18ja` | Double-width font filename prefix |
| `-fontsize PX` | `16` | Requested font height |
| `-fontpath PATHS` | `/usr/share/fonts` | Colon-separated search roots |
| `-fg RGB`, `-bg RGB` | `#fff`, `#000` | Default foreground and background |
| `-cr RGB` | foreground | Cursor colour |
| `-border PX` | `2` | Logical border width |
| `-saveLines N` | `500` | Scrollback lines, from 0 through 50000 |
| `-title TEXT` | `Zutty` | Initial window title |
| `-shell PATH` | `$SHELL` | Shell to start |
| `-e COMMAND ...` | — | Execute a command; must be the final option |
| `-login` | off | Start the selected shell as a login shell |
| `-rv` | off | Swap the default foreground and background |
| `-boldColors` | on | Use bright palette colours for bold text |
| `-altScroll` | off | Turn wheel/history movement into cursor keys on the alternate screen |
| `-autoCopy` | off | Copy every completed primary selection to the clipboard too |
| `-showWraps` | off | Mark wrapped lines at the right edge |
| `-vulkanInfo` | off | Print the chosen Vulkan device and API version |
| `-quiet`, `-verbose` | off | Adjust diagnostic output |
| `-listres` | — | Print advanced keyboard and palette options |

Colours accept `RGB` or `RRGGBB`, with an optional leading `#`.

Advanced options are regular command-line arguments despite the historical
name “resources” in `-listres` output:

```sh
zutty -altSendsEscape false -modifyOtherKeys 2 \
      -color1 cd0000 -color12 5c5cff
```

`altSendsEscape` accepts `true` or `false`; `modifyOtherKeys` accepts levels
0 through 2. `color0` through `color15` replace the base palette.

## Fonts

`-font` and `-dwfont` are primarily case-insensitive filename prefixes. Zutty
walks each root in `-fontpath` until it finds a regular face. Beside that face
it recognizes common filename suffixes for bold, italic/oblique and bold
italic variants; missing variants gracefully fall back to the regular face.

If no file under `-fontpath` matches the prefix, the name is resolved through
fontconfig as a family query instead, so names like `monospace`,
`DejaVu Sans Mono` or any installed family (including aliases) also work.
Fontconfig picks the best match, which may be a substitute family if the
requested one is not installed.

Supported files are `.ttf`, `.otf`, `.ttc`, `.pcf` and `.pcf.gz`. Good primary
font candidates commonly available on Linux include `DejaVuSansMono`,
`LiberationMono`, `FreeMono`, `Hack` and `Inconsolata`. The double-width face
must rasterize to exactly twice the primary cell width at the same height. If
it is absent or incompatible, wide characters use the missing-glyph marker.

Example with multiple search roots:

```sh
zutty -fontpath /usr/local/share/fonts:/usr/share/fonts \
      -font DejaVuSansMono -dwfont NotoSansMonoCJK
```

## Keyboard, scrolling and selection

Zutty follows the active VT cursor, keypad and function-key modes. Shift, Alt
and Ctrl combinations use the conventional xterm modifier parameters: Shift
is 2, Alt is 3, Alt+Shift is 4, Ctrl is 5, Ctrl+Shift is 6, Ctrl+Alt is 7 and
Ctrl+Alt+Shift is 8.

Local bindings are deliberately short:

| Action | Input |
| --- | --- |
| Scroll half a page | `Shift+PageUp`, `Shift+PageDown` |
| Scroll five lines | Mouse wheel |
| Begin/adjust selection | Left drag / right drag |
| Select by word or line | Double / triple click |
| Toggle rectangular selection | `Space` while selecting |
| Paste primary selection | Middle click or `Shift+Insert` |
| Copy primary selection to clipboard | `Ctrl+Shift+C` |
| Paste clipboard | `Ctrl+Shift+V` |
| Bypass application mouse tracking | Hold `Shift` |

When an application enables mouse reporting, clicks, motion and wheel events
go to the application. Holding Shift temporarily restores local selection and
scrolling.

## Terminal compatibility

The parser is a byte-at-a-time state machine descended from Zutty's original
terminal core. Historically it was exercised with Vttest across cursor
movement, margins, tabs, autowrap, DEC character sets, keyboard modes, device
reports, VT52 mode, insert/delete operations, soft and hard reset, ISO 6429
colour and xterm alternate-screen, title and mouse extensions.

This is compatibility context, not a claim of a current automated conformance
suite: the old screenshot harness was removed during the port and a frontend
regression suite has not replaced it yet.

Known limits include:

- code points outside the Unicode Basic Multilingual Plane;
- bidirectional layout and composed combining glyphs;
- DEC double-height and double-width *line* modes (`DECDHL`/`DECDWL`);
- rectangular area operations and mouse highlight tracking;
- blinking or concealed text and a blinking cursor;
- terminal-requested switching of the host window between 80 and 132 columns.

These limits are distinct from ordinary CJK double-width cells, which are
supported through `-dwfont`.

## Architecture

```text
 child process
      ⇅ PTY
    Vterm             VT parser, keyboard encoder, terminal modes
      ⇅
    Frame             cells, damage, selection, circular scrollback
      ↓ changed cells
   CharVdev           host mirror of compact 12-byte cells
      ↓ SSBO                         Fontpack
 VulkanPresenter  ← atlas/map textures ┘
      ↓ compute shader (`render.comp`)
 persistent RGBA8 storage image
      ↓ image blit
 WSI swapchain        GLFW supplies the Wayland platform integration
```

`Frame` keeps the visible screen and history in circular storage, so a scroll
normally changes an offset rather than moving every cell. `Renderer` consumes
damage deltas after the first full frame and clears dirty bits after a
successful submission. The compute shader skips clean cells while still
redrawing cursor and selection damage. Font variants occupy four layers of an
R8 atlas; a 256×256 integer lookup image maps BMP code points to atlas cells,
with an independent atlas/map pair for double-width glyphs.

`VulkanPresenter` keeps two cell buffers and command submissions in flight.
Presentation-complete semaphores belong to swapchain images rather than frame
slots, so they are not reused while the compositor still owns them. The
renderer handles RGBA/BGRA swapchain formats, preserves a GPU-side output image
between delta frames and recreates size-dependent resources when the surface
changes.

Source map:

- `vterm.*` — parser, terminal state and input encoding;
- `frame.*` — screen, history, damage and selection;
- `font.*`, `fontpack.*` — FreeType loading and glyph atlases;
- `charvdev.*` — compact cell representation and host-side video memory;
- `renderer.*` — terminal-to-presenter bridge;
- `vkpresenter.*` — Vulkan resources, compute dispatch and swapchain;
- `render.comp` — cell compositor compiled to embedded SPIR-V by Meson;
- `main.cpp` — GLFW Wayland event loop, PTY integration and clipboard;
- `options.*` — command-line configuration.

## Development notes

The code style is three-space indentation, spaces only, Allman braces and an
80-column target. Debug builds define `DEBUG`. In such a build, Print Screen
cycles the VT parser step interval through 1, 10, 100 and off. At each interval
the process logs the consumed input and stops itself with `SIGSTOP`; resume it
with `fg` or `kill -CONT PID`.

Useful references when changing parser behaviour are the xterm `ctlseqs`
documentation, Vttest, the original DEC VT100/VT102/VT220/VT420/VT520 manuals,
DEC STD 070 and Paul Williams' VT500 parser description. Changes should be
checked against real full-screen applications as well as focused escape
sequence cases, especially across resize, alternate screen and scrollback.

## Origin and license

Zutty was created by Tom Szilagyi as a lightweight X11/OpenGL terminal. This
tree retains its terminal engine and low-overhead cell model while replacing
the old frontend with GLFW and Vulkan on Wayland.

Copyright © 2020 Tom Szilagyi and subsequent Zutty contributors. Zutty is free
software under the GNU General Public License, version 3 or later. See
[`LICENSE`](LICENSE).

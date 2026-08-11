# iTerm2 upstream adaptations

## VT100Grid cases 1 through 40

The first 40 methods in `ModernTests/VT100GridTests.swift` are represented in
source order by 40 distinct executable methods in
`tests/test_iterm2_vt100_grid.py`.  The extra inventory method checks that no
source case was merged or omitted.  Thirty-eight adaptations pass and two exact
iTerm2 policy expectations are executable expected failures on both Ragel
parser backends.

### Cases 1 through 20

The source methods call private `VT100Grid` and `LineBuffer` operations.  Their
public effects were retained without adding a grid API for tests:

- appending rows to `LineBuffer` is observed by scrolling the same hard/soft
  rows into terminal history and by a shrink/grow history round trip;
- the cursor metadata cases are observed through cursor placement after that
  round trip and after reflowing a blank continuation back onto its logical
  line;
- `lengthOfLineNumber` is observed as the exact drawn-cell prefix;
- `moveCursorDownOneLineScrolling` is driven by IND with full, vertical and
  rectangular margins, including bounded history;
- `moveCursorLeft` is driven by CUB with and without DECLRMM/DECSLRM.

The rectangular case uses real VT420 controls (`CSI ? 69 h`, DECSLRM and
DECSTBM), and the DWC case uses a real width-two Unicode glyph instead of
iTerm2's private synthetic `DWC_RIGHT` cell.

### Standard

The concrete standard for this batch is Digital's *VT420 Programmer Reference
Manual*, second edition (1992).  Its character-encoding table defines IND as
moving down in the same column and scrolling at the bottom margin (page 34 of
the printed manual); pages 133 through 136 define DECSLRM, DECSTBM and
DECVSSM/DECLRMM, including a rectangular scrolling region and the disabled
default for left/right margins; pages 179 through 182 define CUB, CUF, CUU,
CUD, SU and SD, with a default count of one.  On the VT420, SU/SD pan a user
window through larger page memory rather than moving emulator grid rows, so
the manual votes on their parameter grammar but abstains on the modern
scrollback and rectangular-scroll policy.  It also does not define emulator
soft-wrap metadata, reflow, xterm private mode 45, a host-side history limit,
or renderer damage callbacks, so it abstains on those policies rather than
being used to invent them.

### Implementation vote

All eight implementations were inspected after updating their repositories.

| implementation | rows, wrap and history | horizontal/reverse movement |
| --- | --- | --- |
| Alacritty | `Grid::scroll_up` adds history when the region starts at row zero; `WRAPLINE` travels with the row | no DECSLRM or reverse CUB |
| Ghostty | `index`/`scrollUp` retain top full-width rows and `Page` retains row wrap | rectangular margins and modes 45/1045; ordinary CUB's fast path does not clamp to the left margin |
| Kitty | `INDEX_UP` adds history when `margin_top == 0`; `next_char_was_wrapped` is retained | vertical margins only; CUB never reverse-wraps |
| xterm | `xtermScroll` saves top full-width rows and line data retains `LINEWRAPPED` | DECLRMM/DECSLRM and mode 45/1045 in `CursorBack` |
| Contour | `Grid::scrollUp` retains history and `Line::wrapped()` is persistent | rectangular margins and mode 45/1045 in `Screen::moveCursorBackward` |
| iTerm2 | `scrollUpIntoLineBuffer` retains a top region when the public profile option is enabled (enabled by default); EOL marks live in the line buffer | rectangular margins; private grid CUB also crosses soft/DWC EOL without mode 45 |
| VTE | `scroll_text_up` retains a top full-width region and the ring stores `soft_wrapped` | rectangular margins and margin-clamped CUB; mode 45 is recognized but not executed |
| foot | ring rotation retains scrollback and `row.linebreak` distinguishes hard from soft | vertical margins only; reverse wrapping is a BS behavior, not CUB |

Hard/soft row identity, full-screen scrolling and bounded newest-tail history
are supported by all eight implementations.  A top-anchored full-width
partial region enters history in Alacritty, Ghostty, Kitty, xterm, Contour,
VTE and foot; iTerm2 exposes both private paths and enables the retaining path
by default.  A non-top region does not enter history in all eight.

Ghostty, xterm, Contour, iTerm2 and VTE implement rectangular margins and vote
on the rectangular IND scenario; Alacritty, Kitty and foot do not implement
DECSLRM and abstain.  All five scroll only the selected rectangle.  For CUB at
or to the right of the left margin, xterm, Contour, iTerm2 and VTE implement
the VT420-style clamp.  Ghostty supports the margins but currently moves left
past the margin in its ordinary no-wrap fast path, producing a four-to-one
vote for the clamp.  All five agree that a cursor starting to the right of the
right margin moves exactly one requested column, which exposed the product
defect fixed in this batch.

iTerm2 alone makes ordinary CUB cross a preceding soft or DWC EOL by default.
Xterm, Ghostty and Contour provide the same reverse-navigation feature only
after mode 45; foot provides it for BS; Alacritty, Kitty and VTE do not execute
it.  The VT420 manual says ordinary CUB stops at the left boundary.  The two
iTerm2 source expectations therefore remain visible as executable XFAILs;
reverse navigation itself is not omitted and is already covered through the
consensus mode-45 contract elsewhere in the suite.

### Product change

`moveCursorBackward` used to add one to CUB's count whenever `posX ==
nColsEff`.  That adjustment belonged to an older representation where the
cursor could encode a pending wrap one column beyond the right margin.  The
current model represents that state with `lastCol`, so the adjustment made a
cursor physically one column to the right of a DECSLRM region jump two
columns.  It was removed.  The eighteenth source adaptation now proves that
one CUB moves one physical column before the left margin begins to constrain
movement.

### Cases 21 through 40

The second group keeps the same private-to-public mapping, with one important
protocol distinction:

- hard-EOL CUB, CUF, CUU and CUD are driven by their CSI controls and checked
  through cursor position;
- `scrollUpIntoLineBuffer` and `scrollWholeScreenUpIntoLineBuffer` are driven
  by SU and observed through the live page plus bounded scrollback;
- horizontal and rectangular scrolling is driven by DECLRMM, DECSLRM,
  DECSTBM, SU and SD, and checks both exact cell movement and published damage
  rows;
- iTerm2's direct `scroll(rect, downBy: 0)` call has no zero-distance wire
  spelling.  `CSI 0 T` uses the protocol default count of one, as required by
  the VT420 manual and implemented by every supporting parser.  Its dedicated
  adaptation therefore proves the public zero-parameter rule instead of
  importing an unreachable private-call result.

Ordinary CUB does not cross a hard EOL in all eight implementations.  Default
CUF also stops at the page edge in all eight.  Ghostty, xterm, Contour, iTerm2
and VTE support DECSLRM and agree on CUF before, entering and at the right edge
of a horizontal region; Alacritty, Kitty and foot do not support DECSLRM and
abstain on those three cases.  With DECOM reset, the VT420 manual allows
movement outside margins: it agrees before and while entering the region, but
votes against the right-margin clamp.  The clamp therefore wins five to one.

All eight support DECSTBM and CUU/CUD.  Ghostty, Kitty, xterm, Contour, iTerm2
and VTE clamp relative vertical movement to a margin when the cursor starts
inside it, while allowing page-relative movement when it starts outside.
Alacritty and foot use page bounds while origin mode is reset.  The VT420
manual explicitly allows movement outside margins in reset DECOM and joins
those two, producing a six-to-three vote for the behavior in the source cases.
Shitty already followed that majority; no product change was needed.

All eight support full-width SU and bounded history, and retain the newest
tail when history capacity is exceeded.  For a horizontal region, the five
DECSLRM implementations scroll only the rectangle and do not create
scrollback.  Those same five implement rectangular SU/SD in both directions
and agree on one-row and multi-row movement; the other three abstain.  The
affected ASCII rows are the only presentation rows changed, matching the
iTerm2 dirty-row assertions and Shitty's public damage callback.

### Audited revisions

| implementation | relevant source | revision |
| --- | --- | --- |
| Alacritty | `alacritty_terminal/src/grid/mod.rs`, `term/mod.rs`, `term/cell.rs` | `1b2b36a64e88` |
| Ghostty | `src/terminal/Terminal.zig`, `Screen.zig`, `modes.zig` | `94d775fefc21` |
| Kitty | `kitty/screen.c`, `vt-parser.c`, `history.c` | `edc132c98b4e` |
| xterm | `cursor.c`, `util.c`, `charproc.c`, `ptyx.h` | `6380a3eaed85` |
| Contour | `Screen.cpp`, `Grid.cpp`, `Line.hpp`, `Primitives.hpp` | `c51e15ed254e` |
| iTerm2 | `VT100GridTests.swift`, `VT100Grid.m`, `VT100ScreenMutableState.m` | `3ec57866cd9b` |
| VTE | `src/vte.cc`, `vteseq.cc`, `ring.cc`, `modes.py` | `3d55bbdddb87` |
| foot | `terminal.c`, `vt.c`, `terminal.h` | `a635e0a196d9` |

# iTerm2 upstream adaptations

## VT100Grid cases 1 through 20

The first 20 methods in `ModernTests/VT100GridTests.swift` are represented in
source order by 20 distinct executable methods in
`tests/test_iterm2_vt100_grid.py`.  The extra inventory method checks that no
source case was merged or omitted.  Eighteen adaptations pass and two exact
iTerm2 policy expectations are executable expected failures on both Ragel
parser backends.

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
default for left/right margins; page 179 defines CUB as motion to the left
which stops at the left boundary.  It does not define emulator scrollback,
soft-wrap metadata, reflow, xterm private mode 45, or a host-side history
limit, so it abstains on those policies rather than being used to invent them.

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

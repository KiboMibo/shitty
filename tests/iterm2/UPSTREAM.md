# iTerm2 upstream adaptations

## VT100Grid cases 1 through 100

The first 100 methods in `ModernTests/VT100GridTests.swift` are represented in
source order by 100 distinct executable methods in
`tests/test_iterm2_vt100_grid.py`.  The extra inventory method checks that no
source case was merged or omitted.  Ninety-eight adaptations pass and two exact
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

### Cases 41 through 60

The third group covers counts at and beyond the rectangle height, split wide
cells, an empty rectangle, and hard/soft continuation metadata.  Private
`DWC_RIGHT`, `DWC_SKIP` and `EOL_DWC` sentinels are not copied into Shitty.
Their public invariant is exercised with real U+754C width-two glyphs: a
rectangle may move a complete glyph, but may not leave either half of a glyph
whose source or destination intersects a rectangle edge.  The tests cover
both directions, both horizontal edges, a region touching either page edge,
source and destination fractures, a complete glyph ending exactly at the
rectangle edge, and vertical damage scope.

The private zero-width/zero-height `VT100GridRect` has no SU/SD wire spelling.
Its no-op contract is represented by an invalid equal-top-and-bottom DECSTBM
request: the VT420 protocol rejects that empty region without changing cells
or publishing damage.  This is an executable public no-op, not an invented
test-only grid entry point.

iTerm2's last five source cases also rewrite some EOL continuation sentinels
when scrolling whole rows or a right-edge rectangle.  That detail was checked
separately from cell movement because the implementations do not agree with
iTerm2:

- Alacritty, Kitty, xterm, Contour and foot rotate full rows and keep each
  row's soft-wrap flag; they do not harden the unaffected row above the region.
- VTE moves whole rows with their hard/soft ending and hardens only the two
  boundaries where the region tears a logical line apart.
- iTerm2 does the same boundary hardening, while Ghostty clears wrap state on
  every shifted full-width row in its insert/delete-lines path.

All eight therefore retain a moved row's content as a coherent row, and seven
of eight retain its row-local wrap metadata.  Five of eight leave the
unaffected external boundary alone.  The public adaptations follow those
majorities: moved soft rows remain soft and the row above a non-top region is
not rewritten.  For a partial-width rectangle the vote is even more direct:
Ghostty, xterm, Contour and VTE keep line-ending metadata attached to the
destination row, while iTerm2 rewrites selected continuation marks.  The three
implementations without DECSLRM abstain.  The four-to-one result is tested by
preserving every destination row's soft-wrap flag during partial-width SD.

Wide-cell repair has the opposite consensus.  Ghostty's
`rowWillBeShifted()`, xterm's `scrollInMargins()`, iTerm2's
`scrollRect:downBy:` and VTE's `cleanup_fragments()` all erase an entire
multi-cell glyph cut by either horizontal edge.  Contour supports the same
rectangular scroll but its `copyColumns()` currently copies width fragments
without an edge repair, producing a four-to-one vote for cleanup.  Alacritty,
Kitty and foot do not implement DECSLRM and abstain.  The VT420 manual defines
the rectangular margins and count syntax but predates Unicode multi-cell
glyphs and host wrap metadata, so it abstains on both representation policies.

Counts equal to or larger than the three-row region blank exactly that region
in both directions and damage its three presentation rows.  All five DECSLRM
implementations clamp directly or reach the same fixed point after the region
height; Alacritty, Kitty and foot abstain.  The VT420 manual supplies the
positive/default parameter and margin grammar but, as noted above, abstains on
modern emulator row movement.  No product change was needed for cases 41
through 60.

### Cases 61 through 80

Cases 61 through 65 finish the partial-width scroll family.  They add a
two-row move in each direction and both shapes of iTerm2's synthetic
`DWC_SKIP`.  The public tests use a real U+754C at the right edge, exercise the
same source and destination fractures with SU/SD, and retain the four-to-one
line-ending and four-to-one wide-fragment decisions recorded for cases 41
through 60.  No private continuation sentinel is exposed.

`setContentsFromDVRFrame` has no terminal wire protocol: a DVR key frame is an
iTerm2 application object.  Its public analogue is replacement of the
alternate page at a new host geometry.  Alacritty, Ghostty, Kitty, xterm,
Contour, VTE and foot all resize the alternate page without primary-history
reflow; iTerm2's frame restore likewise copies physical rows rather than
joining soft lines.  The executable case checks truncation/padding, stable
cursor coordinates, and valid geometry in both directions, not iTerm2's DVR
serialization or its private choice of bottom alignment.

`setBgFgColorInRect` is represented by DECSACE rectangle extent plus DECCARA.
Kitty, Contour and VTE accept foreground/background SGR colors in DECCARA and
agree that selecting only one color leaves the other untouched.  Xterm, foot
and iTerm2 implement only the smaller DEC rendition subset, while Alacritty
and Ghostty do not implement DECCARA, so those five abstain on the color
extension.  DEC STD 070 also abstains on colors: it limits DECCARA to bold,
underline, blink and inverse.  The test records the unanimous three-supporter
extension already implemented by Shitty without calling it a DEC feature.

The two `restoreScreenFromLineBuffer` cases become primary-screen resize and
bounded-history scenarios.  Across the eight implementations, the primary
page is rebuilt from logical hard/soft rows on resize, complete wide glyphs
survive reflow, and a cursor attached to surviving content follows that
content.  Width-two storage sentinels and `LineBuffer.rawLine` offsets remain
iTerm2 internals.  The tests instead observe the restored rows, cursor and
real wide-cell pair after history pruning and a shrink/grow round trip.

`rectsForRun` is the normal autowrap path from a mid-row cursor: five cells,
one complete eight-cell row and seven cells are produced by one 20-character
write.  All eight implement this row-major graphic-character progression.
The VT420 manual's autowrap and cursor rules agree; an empty private run has no
wire event and is not turned into a test hook.

The two scroll-region accessors are exercised through their effects.  RIS
restores both margins to the page.  With DECLRMM set, SU uses the intersection
of DECSLRM and DECSTBM; after DECLRMM is reset, the same vertical region spans
the full page width.  Ghostty, xterm, Contour, iTerm2 and VTE support the
horizontal region and agree; Alacritty, Kitty and foot abstain.  The VT420
manual defines DECVSSM/DECSLRM and RIS and agrees with the public state
transition.

`eraseDwc` uses ECH on the continuation cell of a real width-two glyph.  Every
implementation that stores width fragments repairs the complete glyph rather
than leaving a drawable orphan; the VT420 predates Unicode cell width and
abstains.  `moveCursorToLeftMargin` is CR with DECLRMM disabled and enabled:
the five DECSLRM implementations use column zero in the former case and the
active left margin in the latter, matching the VT420 margin model.

The final six source methods call iTerm2's private UI-reset path, which may
move selected live rows into its `LineBuffer`.  They are not RIS semantics.
The public portable reset is RIS: Alacritty's `Grid::reset`, Ghostty's
`fullReset`, Kitty's `do_screen_reset`, xterm's `VTReset`, Contour's
`hardReset`, iTerm2's terminal-level `resetForReason`, VTE's hard reset and
foot's `term_reset(..., true)` all replace the live page, home the cursor and
clear the history backing.  Six distinct scenarios retain the source cursor
and capacity permutations while asserting that public consensus.  ECMA-48
section 8.3.105 defines reset to initial state but leaves host scrollback and
storage ownership unspecified; the all-eight history result is implementation
consensus, not a claim about ECMA-48.

No production change or test-only grid API was needed for cases 61 through
80.

### Cases 81 through 100

`moveWrappedCursorLineToTopOfGrid` is another private grid helper with no
single wire spelling.  Its public adaptation composes the standard operations
that a client can actually send: reset DECSLRM/DECSTBM, scroll the full page
with SU, then place the cursor with CUP.  The source's hard and soft row
boundaries, content order and final cursor are retained, including the fact
that stale restricted margins do not constrain the operation.  All eight
implement those controls.  The VT420 manual defines SU, CUP and margin reset;
it does not define an iTerm2 UI helper.

The empty `appendCharsAtCursor` case is an exact zero-byte no-op.  The next six
cases exercise autowrap at the bottom of a full page, inside a horizontal
region, on a one-row page, on the alternate page and in a one-row rectangular
band.  A one-row private DECSTBM range cannot be encoded because equal top and
bottom margins are invalid, so the public adaptation uses an actual one-row
terminal geometry.  This retains the behavior instead of dropping the case or
inventing a private margin API.  All eight scroll a full-width primary page
into history on autowrap and discard the corresponding alternate-page rows;
the five DECSLRM implementations scroll only the selected columns and create
no history, while Alacritty, Kitty and foot abstain on that subfeature.

The unlimited-history case retains every physical row and its hard/soft ending
while the newest two rows remain visible.  All eight preserve the ordered
newest tail under a sufficiently large history limit.  The source's
`unlimitedScrollback` boolean is an iTerm2 storage option, so the test uses a
large public `saveLines` capacity rather than exposing it in the grid API.

Cases 89 through 93 replace `DWC_RIGHT` and `DWC_SKIP` with a real U+754C.
All eight keep an intact two-cell glyph, move it to the next row when only one
cell remains, and defer ordinary narrow-character wrap until the following
character.  Ghostty, xterm, Contour, iTerm2 and VTE apply the same rule at a
DECSLRM right edge and restart at its left edge; the other three lack
DECSLRM and abstain.  The implementations use different spacer and row-ending
representations, so the oracle is the visible intact glyph and cursor, not a
copied sentinel bit.  The VT420 predates Unicode cell width and abstains on
wide repair.

Cases 94 through 100 drive real IRM (`CSI 4 h`).  Alacritty's `input`,
Ghostty's `print`, Kitty's `draw_text_loop`, xterm's `WriteText`, Contour's
`writeCharToCurrentAndAdvance`, iTerm2's `appendCharsAtCursor`, VTE's
`insert_char` and foot's `term_print` all shift existing cells right before
writing and discard cells pushed beyond the right edge.  They also agree that
DECAWM wraps a long insert stream, while reset DECAWM repeatedly replaces the
rightmost cell so the final input character wins.  Their wide-cell cleanup
paths erase a glyph cut by the shift and turn an obsolete wide-wrap spacer
back into ordinary text.

For IRM under DECSLRM, the five supporting implementations restrict both the
shift and the following wrap to the horizontal margins, preserving cells
outside them; Alacritty, Kitty and foot abstain.  The VT420 Programmer
Reference, printed page 149, defines IRM as moving existing characters right
and losing those past the page border.  Printed page 191 defines DECAWM as
placing the next character at the beginning of the next line and scrolling at
the bottom region.  Its DECSLRM definition supplies the horizontal boundary,
but it abstains on Unicode-wide storage and emulator scrollback.

No product change or test-only grid API was needed for cases 81 through 100.

### Audited revisions

| implementation | relevant source | revision |
| --- | --- | --- |
| Alacritty | `alacritty_terminal/src/grid/mod.rs`, `term/mod.rs`, `term/cell.rs` | `1b2b36a64e88` |
| Ghostty | `src/terminal/Terminal.zig`, `Screen.zig`, `modes.zig` | `94d775fefc21` |
| Kitty | `kitty/screen.c`, `vt-parser.c`, `history.c` | `1a8b11381b03` |
| xterm | `cursor.c`, `util.c`, `charproc.c`, `ptyx.h` | `6380a3eaed85` |
| Contour | `Screen.cpp`, `Grid.cpp`, `Line.hpp`, `Primitives.hpp` | `c51e15ed254e` |
| iTerm2 | `VT100GridTests.swift`, `VT100Grid.m`, `VT100ScreenMutableState.m` | `3ec57866cd9b` |
| VTE | `src/vte.cc`, `vteseq.cc`, `ring.cc`, `modes.py` | `3d55bbdddb87` |
| foot | `terminal.c`, `vt.c`, `terminal.h` | `a635e0a196d9` |

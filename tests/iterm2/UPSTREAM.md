# iTerm2 upstream adaptations

## VT100Grid cases 1 through 158

All 158 methods in `ModernTests/VT100GridTests.swift` are represented in
source order by 158 distinct executable methods in
`tests/test_iterm2_vt100_grid.py`.  The extra inventory method checks that no
source case was merged or omitted.  One hundred fifty-six adaptations pass and
two exact iTerm2 policy expectations are executable expected failures on both
Ragel parser backends.

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

### Cases 101 through 120

Cases 101 and 102 continue the IRM/overwrite boundary matrix with a real
U+754C in place of `DWC_RIGHT`.  Ghostty, xterm, Contour, iTerm2 and VTE
implement DECSLRM and therefore vote on the first case.  Ghostty's
`splitCellBoundary`, xterm's `DamagedCells`, iTerm2's
`erasePossibleDoubleWidthCharInLineNumber` and VTE's `cleanup_fragments`
erase the glyph cut at the right margin; Contour's raw `moveColumns` retains a
split pair, so the supporting vote is 4:1 for repair.  Alacritty, Kitty and
foot lack DECSLRM and abstain.  For ordinary overwrite all eight clear the
other half when the wide head is replaced.  The Unicode Standard defines the
character as one base with its display width but does not prescribe terminal
cell repair, so it abstains on this storage policy.

The two `ansi` cases expose an iTerm2 grid-coordinate choice rather than a
different wire protocol.  All eight public terminal paths use deferred
autowrap: the graphic written at the right margin remains there and the next
graphic either wraps under DECAWM or replaces that cell while DECAWM is reset.
The adaptations test both following-character outcomes instead of copying an
out-of-range private cursor coordinate.  The VT420 Programmer Reference,
printed page 191, specifies the same next-graphic trigger.  The two following
`DWC_SKIP` cases create the state with a real wide glyph that does not fit,
overwrite its pre-wrap spacer, and then distinguish the resulting ordinary
soft wrap from the obsolete wide pre-wrap marker.  All eight implement that
observable transition even though their spacer encodings differ.

`coordinateBefore` is not a cursor-motion primitive: iTerm2 calls it when a
new text chunk starts with a combining mark.  Cases 107 through 114 are
therefore represented by split writes of U+0301.  Alacritty's zero-width path,
Ghostty's grapheme predecessor lookup, Kitty's `init_prev_cell`, xterm's
`last_written` cell, Contour's `_lastCursorPosition`, iTerm2's
`coordinateBefore`, VTE's zero-column soft-wrap lookup and foot's composed-cell
path all attach a mark to the preceding base, keep it on the base side of a
wide continuation and keep a pending right-margin wrap deferred.  At a
DECSLRM edge the five supporting implementations apply the same rule to the
right-margin base; the other three abstain.

The Unicode Standard 17.0, section 3.6, says that graphical positioning of a
combining character depends on the last preceding base unless an intervening
non-combining character separates them.  It also permits an isolated combining
mark to be presented without combination.  Consequently the before-grid case
does not vote between drawing and ignoring the isolated mark; it verifies the
common invariant that the mark never attaches forward.  CR/LF is an
intervening control and all eight refuse to attach across that hard break.
Soft and wide pre-wrap states have no character separator, so their predecessor
remains eligible.  This retains every source case without adding a private
coordinate API.

Cases 115 through 120 use DCH (`CSI P`).  All eight shift the remaining tail
left, erase the vacated right cells and clamp an oversized count.  Alacritty,
Kitty, xterm, Contour, VTE and foot parse `CSI 0 P` as one.  Ghostty forwards
the explicit zero to `deleteChars(0)`, while iTerm2 preserves it through
`iTermParserSetCSIParameterIfDefault`; both perform no edit.  The implementation
vote is therefore 6:2 and the direct iTerm2 no-op is not the majority oracle.
ECMA-48 fifth edition, section 8.3.26, gives *an omitted* DCH parameter the
default one and specifies the left shift plus erased cells.  Its section 5.4
and annex F.4.2 preserve an explicit zero as zero in ZDM ZERO, with ZDM DEFAULT
providing the old zero-means-default compatibility behavior.  The standard
therefore votes with Ghostty and iTerm2; the combined consensus remains 6:3 for
the behavior tested here.

There is a real implementation split at a wide boundary.  Ghostty, Kitty,
xterm, Contour, iTerm2 and VTE repair a glyph cut at either the head or
continuation.  Alacritty and foot shift their cell arrays without a
corresponding ordinary-wide boundary repair, producing a 6:2 vote for complete
glyphs.  Whether a full-width DCH hardens a row after its wide pre-wrap marker
is shifted away is a separate 5:3 decision: Alacritty, Ghostty, Kitty, iTerm2
and VTE harden it, while xterm, Contour and foot retain row-level soft-wrap
metadata.  ECMA-48 does not define either Unicode cell fragments or emulator
soft-wrap metadata and abstains.  The head, continuation and pre-wrap cases
remain separate executable methods because they exercise different policies.

No product change or test-only grid API was needed for cases 101 through 120.

### Cases 121 through 140

Cases 121 through 127 complete the DCH matrix under horizontal margins.  The
five DECSLRM implementations — Ghostty, xterm, Contour, iTerm2 and VTE — all
limit the shift and erased tail to the active right margin and make DCH a no-op
when the cursor is outside the horizontal region.  Alacritty, Kitty and foot do
not implement DECSLRM and abstain.  Digital's *VT510 Programmer Information*
agrees: DCH moves characters only through the right scrolling margin, adds
blanks there, and has no effect outside the scrolling margins.

Those five implementations also erase a complete wide glyph when either
horizontal boundary cuts it.  For ordinary full-width DCH/ICH, Contour's
current `eraseMulticellBlocksInRange` joins Ghostty, Kitty, xterm, iTerm2 and
VTE, making the complete-glyph vote 6:2 across all eight implementations;
Alacritty and foot retain raw cell fragments.  This corrects the older 5:3
count recorded for cases 115 through 120.  The VT510 manual predates Unicode
multi-cell storage and abstains on repair policy.

The partial-region wide-pre-wrap case separates cell repair from line-ending
metadata.  Xterm, Contour and iTerm2 retain the soft boundary, while Ghostty
and VTE harden it, for a 3:2 implementation vote.  The VT510 rule that DCH has
no effect outside the scrolling margins joins the preserving side: the
combined result is 4:2.  Full-width DCH still follows the separate 5:3
hardening consensus documented above.

Cases 128 through 140 cover ICH and the equivalent public IRM path.  All eight
implement insertion of blanks, clipping at the right page edge, and IRM
insertion of non-blank graphics.  The VT510 manual likewise specifies that ICH
inserts spaces, keeps the cursor fixed, shifts text only through the right
margin and discards text beyond it; its IRM definition supplies the public
non-null insertion path used for the private grid case.

An explicit zero ICH parameter is parsed as one by Alacritty, Ghostty, Kitty,
xterm, Contour, VTE and foot.  iTerm2 preserves zero and performs no edit, so
the terminal vote is 7:1 for one.  ECMA-48 section 8.3.64 defaults only an
omitted parameter, while annex F.4.2's ZDM ZERO preserves explicit zero; with
iTerm2 that makes the combined vote 7:2 for the executable one-cell behavior.

The five DECSLRM implementations unanimously constrain ICH to the horizontal
region, preserve cells outside it and repair wide glyphs cut at either margin.
The VT510 ICH and DECSLRM rules agree on the region boundary.  Alacritty, Kitty
and foot abstain on these horizontal-margin scenarios.

Wide pre-wrap metadata has three observable subcases rather than one copied
iTerm2 sentinel rule.  Inserting one cell preserves a soft row in Alacritty,
Ghostty, xterm, Contour, iTerm2, VTE and foot; Kitty drops its cell-local
continuation flag, yielding 7:1.  When a two-cell insertion pushes the original
marker out of the row, Ghostty, xterm, Contour, VTE and foot retain row-level
soft-wrap state, while Alacritty, Kitty and iTerm2 harden it, yielding 5:3 for
preservation.  With a partial horizontal region all five supporters preserve
the untouched pre-wrap boundary.  Neither ECMA-48 nor the VT510 manual defines
host soft-wrap metadata, so the standards abstain on these representation
votes.

### Product change for cases 121 through 140

`ScreenBase::deleteCells` unconditionally cleared wrap bits at `end - 1` and
`end - 2`, even when `end` was only a horizontal scrolling margin.  It now
hardens only a full-width DCH and otherwise shifts a surviving wrap marker with
the edited cells or leaves an out-of-region marker untouched.

`ScreenBase::insertCells` previously handled only a normal marker in the last
cell.  A wide pre-wrap marker one cell earlier was lost when insertion overflow
discarded its backing cell.  The operation now records the row boundary before
moving cells and restores it at the shifted or clipped right edge.  The change
does not expose iTerm2's private `DWC_SKIP`; it preserves the public soft-row
invariant selected by the implementation votes above.

### Cases 141 through 158

Case 141 completes the ICH horizontal-margin matrix.  Ghostty, xterm,
Contour, iTerm2 and VTE implement DECSLRM and unanimously make ICH a no-op
when the cursor is outside the horizontal scrolling region.  Alacritty, Kitty
and foot do not implement DECSLRM and abstain.  Digital's *VT510 Programmer
Information* gives ICH no effect outside the scrolling margins, so the result
is 6:0.

Case 142 moves the cursor right by a deliberately excessive count.  All eight
implementations clamp CUF at the page's right edge.  ECMA-48 fifth edition,
section 8.3.20, specifies movement only up to the active presentation limit;
the combined vote is 9:0.

Case 143 covers a long autowrapped write at the bottom of DECSTBM.  All eight
implementations preserve the logical continuation when a wrapped row moves
within the scrolling region.  The VT510 wrapping and scrolling rules agree on
the visible character movement, but do not define an emulator's stored
soft-wrap bit and abstain on that representation detail.  The executable test
therefore checks both the exact public rows and their selection-visible soft
continuations.

Cases 144 through 150 convert a flat row-major range into grid coordinates,
including positive viewport offsets and partially or completely clipped
negative history rows.  The private iTerm2 arithmetic is exercised through
the public operation that consumes it: selection extraction.  Alacritty,
Ghostty, Kitty, xterm, Contour, iTerm2, VTE and foot all order a linear
selection by row and column and clip it to available viewport or history
cells.  ECMA-48 section 6.1.7 describes a selected area as an ordered string of
character positions; it supports the ordering and abstains on host viewport
clipping.  No implementation votes against the seven executable mappings.

Cases 151 through 156 cover an empty one-column logical history line and
removal of the newest regular, wrapped, empty, over-block-size or nonexistent
raw line.  Shitty has no public `LineBuffer::removeLastRawLine`; the same
observable consumer is height growth, which restores the newest complete
history line before adding blank screen rows.  Alacritty's
`Grid::grow_lines`, Ghostty's `PageList` resize, Kitty's resize recovery,
xterm's south-west resize gravity, Contour's `Grid` resize, iTerm2's screen
restoration, VTE's ring resize and foot's grid resize all follow that newest
tail policy and retain empty physical history rows.  ECMA-48 does not specify
host resize or scrollback and abstains.

Cases 157 and 158 require newly exposed cells after full-screen or rectangular
scrolling to inherit the current erase background.  Alacritty, Ghostty, xterm,
Contour, iTerm2, VTE and foot implement BCE this way.  Kitty deliberately omits
the `bce` terminfo capability and clears to zero/default cells, so the
full-screen vote is 7:1.  For the public rectangular spelling using DECSLRM,
the five implementations that support horizontal margins all use the current
background; Alacritty, Kitty and foot abstain.  ECMA-48 defines an erased
character state but not its emulator color inheritance and abstains.

No product change or test-only grid API was needed for cases 141 through 158.

## VT100Screen cases 1 through 22

The first 22 methods in `ModernTests/VT100ScreenTests.swift` are represented
in source order by 22 executable methods in
`tests/test_iterm2_vt100_screen.py`; its inventory method prevents a source
case from being silently merged or omitted.  All 22 adaptations pass on both
Ragel parser backends.

`testResizeNotes` attaches a private iTerm2 annotation to three primary-screen
cells, switches to the alternate screen, resizes, and requires the annotation
to follow the reflowed primary text.  The public adaptation uses an OSC 133
semantic range, not a test-only annotation API, and checks the same exact cell
movement from columns 0--2 to columns 1--3.  Ghostty, iTerm2, VTE and foot keep
cell-addressable semantic metadata with reflowed text.  Kitty and Contour
support OSC 133 with line-granular metadata and therefore abstain on exact
cell endpoints; Alacritty and xterm do not support OSC 133 and also abstain.
The semantic-prompts proposal defines the prompt as the exact subsequent text
up to its terminating marker.  It joins the four exact implementations for a
5:0 vote that the metadata follows that text when the inactive primary page is
reflowed.

`testSwitchingScreenBuffersRefreshesChangedKeyReportingFlags` requires the
active Kitty keyboard flags to change with the screen.  Alacritty, Ghostty,
Kitty, iTerm2 and foot keep independent main and alternate keyboard stacks.
Contour implements the protocol with one terminal-wide stack and votes
against; xterm and VTE do not implement the progressive keyboard stack and
abstain.  The current Kitty keyboard protocol explicitly requires independent
main and alternate stacks, so the combined result is 6:1.  The executable
adaptation verifies both the public state and the `CSI ? u` report after each
screen switch.

### Product change for VT100Screen cases 1 and 2

`VtermImpl::switchScreenBufferMode` used to clear the alternate Kitty flags
and stack whenever DECSET 1049 requested a cleared alternate page.  Cell
clearing and keyboard protocol state have different lifetimes: entering or
re-entering a cleared alternate page must reveal that page's existing stack.
The two resets were removed from screen switching; hard terminal reset still
clears both main and alternate stacks.

### Cases 3 through 22

Case 3 is the unchanged counterpart of case 2: switching between pages whose
active Kitty keyboard flags are equal must leave the public flags and the
`CSI ? u` report unchanged.  Alacritty, Ghostty, Kitty, iTerm2 and foot keep
independent page stacks and agree.  Contour's one terminal-wide stack reaches
the same observable result when both values are equal.  Xterm and VTE do not
implement the progressive keyboard stack and abstain.  The Kitty keyboard
protocol requires separate main and alternate stacks, so the result is 7:0.
The private iTerm2 callback count is not exposed as terminal semantics; the
adaptation checks the stable state and protocol report that consume it.

Case 4 compares a private combined line/cache read with separate reads.  Its
public consumer is repeated presentation reads across the live/history
boundary.  All eight implementations return one stable cell and line state
in the absence of intervening input, so the vote is 8:0.  ECMA-48 section
6.1.7 defines a selected area as one ordered presentation-state area; it
supports consistent public reads but does not define emulator cache identity,
on which it abstains.

Cases 5 through 7 resize an inactive primary page while an exact annotation
is partly or wholly near the history boundary.  Cases 9 through 22 serialize
and restore private iTerm2 annotations at 14 different line-boundary shapes.
Shitty has no public annotation archive API, so each remains a separate
executable scenario using OSC 133 cell metadata and the real consumers: an
inactive primary resize, bounded history traversal, and a shrink/grow reflow
round trip.  The tests prove both the exact marked characters and whether
they remain visible, split across the boundary, or move wholly into history;
they do not add a test-only serialization or grid entry point.

Ghostty, iTerm2, VTE and foot store semantic-shell metadata at cell or exact
row-and-column granularity and carry it through their reflow/history paths.
Kitty and Contour implement OSC 133 at line granularity and abstain on exact
endpoints.  Alacritty and xterm do not implement OSC 133 and abstain.  The
semantic-prompts proposal defines the semantic region as the exact subsequent
text through its terminating marker.  It joins the four exact
implementations for a 5:0 vote that the metadata follows the text.  The
proposal does not specify iTerm2's private archive format, so that format is
deliberately not imported.

Case 8 exposes a genuine iTerm2 policy difference.  On a narrower alternate
page, iTerm2 reflows physical rows and then truncates overflow.  Alacritty,
Ghostty, Kitty, xterm, Contour, VTE and foot resize the alternate page without
reflow and clip each physical row.  The executable adaptation follows that
7:1 implementation consensus and checks both the clipped cells and semantic
metadata; it does not copy iTerm2's contrary row layout.  ECMA-48 defines
neither host window resize nor alternate-page reflow and abstains on this
policy.

No additional product change was needed for cases 3 through 22.

### Cases 23 through 42

Cases 23 and 25 through 28 continue the resize matrix for annotation and
screen-mark properties: blank physical lines precede the range, the active
coordinate may be the pending-wrap column, old history may be dropped, and an
inactive primary page is both narrowed and widened.  The public adaptations
use OSC 133 prompt, input and output regions and assert their exact characters
after the real resize/history paths.  They do not expose iTerm2's
`VT100ScreenMark`, interval tree, absolute-range mutator or saved tree.

Ghostty stores OSC 133 semantic content on cells and row prompt state; iTerm2
updates exact annotation and prompt-mark ranges; VTE stores the shell region
in each cell's attributes; foot remaps its row-and-column `cmd_start` and
`cmd_end`; and Contour stores prompt/command borders as logical-line offsets
that its reflow code deliberately preserves.  All five support the exact
public boundaries and agree.  Kitty retains prompt/output marks per logical
line but not every exact prompt/input endpoint, so it abstains on those
endpoint assertions.  Alacritty and xterm have no OSC 133 semantic-region
implementation and abstain.  The semantic-prompts proposal defines A/P, B,
C and D as exact successive prompt, input and output regions and recommends
that terminals account for resize.  It makes the result 6:0.

Case 24's unresolved return-code promise is an iTerm2 ownership detail.  Its
public precondition is retained: a session may end after A and B while its
command is still open, without a C or D marker.  Ghostty, Kitty, Contour,
iTerm2, VTE and foot represent that open prefix as ordinary terminal state
and tear their terminal owner down without requiring a closing marker;
Alacritty and xterm do not implement the feature.  The semantic-prompts
proposal says only D ends the current command, so it recognizes the byte
stream as an open command prefix, but it abstains on host-language
deallocation.  The executable test closes the owning public session with
that prefix and then starts another session; no private promise API is added.

Cases 29 through 31 exercise the consumers of a command mark.  An out-of-page
row, a plain line, and a prompt with no command must respectively produce no
semantic row, no live prompt/command region, and a live prompt with an empty
command range.  Every one of the six OSC 133 implementations distinguishes
those states; unsupported Alacritty and xterm abstain.  The proposal
explicitly permits a command cancelled before C and defines a blank prompt,
so absence is not conflated with an empty but valid semantic region.

Cases 32 through 38 are iTerm2 annotation ranges on otherwise empty rows,
including the first, second, third and a late row plus one multi-line range.
Copying private interval endpoints onto undrawn cells would exclude the
implementations that represent this feature as logical-line state.  Instead,
the adaptations emit an empty OSC 133 A/B prompt and verify its row mark
through shrink and shrink/grow round trips; the multi-line case separately
checks exact marked characters.  Ghostty, Kitty, Contour, iTerm2 and foot
retain empty prompt marks across resize.  VTE records semantics on written
cells and abstains on an empty row; Alacritty and xterm are unsupported.  The
proposal explicitly says that initial indentation may be delimited as a
blank prompt and joins the five supporters for a 6:0 result.  Thus the empty
feature is tested in every supported representation rather than omitted.

Cases 39 and 40 compare iTerm2's optimized mixed-ASCII gang with its scalar
path, once with the source transcript and once over 100 deterministic random
transcripts.  The public equivalent compares one concatenated write with the
same non-empty pieces submitted separately.  Cases 41 and 42 repeat that
equivalence while ANSI IRM and DEC DECAWM are disabled and restored.  All
eight implementations have streaming parsers and implement IRM and DECAWM;
none assigns semantic meaning to host read/write boundaries.  ECMA-48 section
6.2 explicitly says that messages, records and blocks are concatenated into
one continuous stream, and section 7.2.10 defines IRM insertion.  The VT420
Programmer Reference, printed pages 149 and 191, supplies the concrete IRM
and DECAWM presentation rules.  The combined vote is 10:0 for identical
terminal state regardless of batching.

No product change or test-only screen API was needed for cases 23 through 42.

### Cases 43 through 62

Cases 43 through 54 finish the mixed-ASCII gang matrix.  The source methods
toggle private iTerm2 fast-path predicates; the adaptations retain the public
state which makes each predicate meaningful, not the predicate or the gang
implementation itself:

- case 43 uses DECANM to enter VT52 mode and `ESC <` to return to ANSI mode.
  This is only a public non-ANSI parser-mode analogue: iTerm2's `ansi` member is
  a terminal-type property and is not exposed as a Shitty mode.  Xterm and
  Contour implement DECANM and agree with Digital's VT100 definition; the
  other six implementations abstain rather than voting on an unsupported
  emulation switch;
- case 44 switches the real primary and alternate pages with mode 1049.  All
  eight implementations support it and agree that parsing continues on the
  selected page.  Xterm's control-sequence specification supplies the concrete
  1049 save/switch/restore contract;
- cases 45, 51 and 54 replace iTerm2's private command coordinate, combined
  fast-path predicates and post-trigger queue with OSC 133 command boundaries,
  once alone, once together with IRM, and once followed by more input after a
  completed command.  Ghostty, Kitty, Contour, iTerm2, VTE and foot implement
  these semantic boundaries; Alacritty and xterm abstain.  The semantic-prompts
  protocol defines the A/B/C/D state transitions and ECMA-48 section 7.2.10
  defines IRM;
- case 46 drives the DEC Special Graphics designation and then restores ASCII.
  All eight implementations agree; DEC STD 070 is the concrete character-set
  specification;
- cases 47 and 48 do not turn iTerm2's private logging and publishing switches
  into terminal modes.  Their public invariants are checked by observing parser
  events and by taking intermediate presentation snapshots while feeding the
  same stream.  All eight implementations treat host write boundaries and
  passive observers as semantically inert.  ECMA-48 section 6.2 explicitly
  specifies concatenation of messages, records and blocks into one continuous
  stream;
- cases 49 and 52 map iTerm2's private `Expect` object to its public protocol
  analogue: a DSR cursor-position request whose reply is first left pending and
  then consumed before more input.  All eight implementations answer CPR/DSR;
  ECMA-48 section 8.3.35 and the VT100 reporting rules define the exchange;
- case 50 keeps Media Copy visible as an executable expected failure.  Xterm
  and iTerm2 implement `CSI 5 i`/`CSI 4 i` controller redirection; VTE parses MC
  but deliberately executes no print action, and Alacritty, Ghostty, Kitty,
  Contour and foot do not implement it.  ECMA-48 section 8.3.82 defines Media
  Copy while xterm's control-sequence specification gives the exact private
  4/5 controller mapping.  The result is three supporters and no contrary
  implementation, but Shitty has no printer sink, so the feature is neither
  faked nor omitted;
- case 53 maps an expired private expectation to synchronized-output expiry.
  Alacritty, Ghostty, Kitty, Contour, iTerm2, VTE and foot implement mode 2026
  with bounded buffering; xterm abstains.  The synchronized-updates
  specification explicitly permits a terminal-selected premature timeout, so
  the seven implementations and the protocol give an 8:0 result.

Case 55 exercises the observable part of `dropFirstBlock`: bounded history
always drops older physical rows before newer ones.  All eight implementations
retain the newest tail, although their internal block sizes and the point at
which a partial block is reclaimed differ.  The adaptation uses the configured
six-row public capacity and checks the tail after both appends; it does not copy
iTerm2's private `LineBlock` granularity.  ECMA-48 does not define host
scrollback and abstains.

Cases 56 and 58 erase from column zero of a continuation row with EL and ED.
Alacritty, Ghostty, Kitty, xterm, Contour, iTerm2, VTE and foot all leave the
previous full row's soft-wrap boundary intact, giving 8:0.  Case 57 first
erases the last cell of the wrapped row.  Alacritty, Ghostty, Kitty, xterm,
iTerm2 and VTE harden that now-incomplete row, while Contour and foot preserve
their independent row-level wrap flag, giving 6:2.  The following EL must not
recreate a boundary that the first erase removed.  Upstream then calls
`setContinuationMarkOnLine` to manufacture the contradictory state again;
that private, publicly unreachable mutation is deliberately not exposed as a
test hook.  ECMA-48 sections 8.3.40 and 8.3.41 define the erased presentation
areas but not emulator soft-wrap metadata, so the standard abstains on the
row-flag votes.

Cases 59 through 62 cover OSC 9;4 with an omitted paused percentage.  The
ConEmu protocol makes the percentage optional but does not prescribe the
retained value, so its specification votes for accepting the sequence and
abstains on the value policy.  The implementation votes are:

| scenario | preserve/zero supporters | contrary implementations | result |
| --- | --- | --- | --- |
| pause after normal 60 | Ghostty, Kitty, Contour, iTerm2 | VTE resets to zero | 4:1 preserve |
| pause after error 25 | Ghostty, Contour, iTerm2 | Kitty and VTE reset to zero | 3:2 preserve |
| pause with no prior value | Ghostty, Kitty, Contour, VTE use zero | iTerm2 substitutes 10 | 4:1 zero |
| pause after explicit normal zero | Ghostty, Kitty, Contour, VTE keep zero | iTerm2 substitutes 10 | 4:1 zero |

Alacritty, xterm and foot do not implement OSC 9;4 and abstain in all four
rows.  The executable expectations follow these majorities rather than
iTerm2's minimum-visible-bar policy in the last two source cases.

### Product change for cases 59 through 62

The OSC parser previously dispatched progress only when a percentage was
present, making a valid `OSC 9;4;4 ST` indistinguishable from an unsupported
sequence.  `osc_PROGRESS` now carries `percentPresent` separately from the
numeric value.  `VtermImpl` retains the last percentage for an omitted paused
or error value, resets stopped/new progress as appropriate, and publishes the
consensus zero for a fresh or explicitly zero progress bar.  Explicit values
above 100 remain invalid and are still ignored.  No progress state or test-only
screen API was added.

### Audited revisions

| implementation | relevant source | revision |
| --- | --- | --- |
| Alacritty | `alacritty_terminal/src/term/mod.rs`, `event_loop.rs`, `grid/mod.rs`, `selection.rs` | `1b2b36a64e88` |
| Ghostty | `src/terminal/Terminal.zig`, `Screen.zig`, `PageList.zig`, `osc/parsers/osc9.zig`, GTK `Surface.zig` | `94d775fefc21` |
| Kitty | `kitty/screen.c`, `line-buf.c`, `vt-parser.c`, `resize.c`, `progress.py`, `window.py` | `5734bb5a587c` |
| xterm | `cursor.c`, `util.c`, `screen.c`, `charproc.c`, `ctlseqs.txt` | `6380a3eaed85` |
| Contour | `Screen.cpp`, `Grid.cpp`, `Selector.hpp`, `Terminal.cpp`, `ProgressState.cpp` | `c51e15ed254e` |
| iTerm2 | `VT100GridTests.swift`, `VT100ScreenTests.swift`, `VT100Grid.m`, `VT100ScreenMutableState.m`, `VT100Terminal.m` | `3ec57866cd9b` |
| VTE | `src/vte.cc`, `vteseq.cc`, `ring.cc`, `attr.hh` | `3d55bbdddb87` |
| foot | `terminal.c`, `grid.c`, `selection.c`, `extract.c`, `osc.c` | `a635e0a196d9` |

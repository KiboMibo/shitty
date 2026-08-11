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

### Cases 63 through 65

The final three source cases finish OSC 9;4.  An explicit paused percentage
of 0, 30 or 100 is accepted by Ghostty, Kitty, Contour, iTerm2 and VTE; the
ConEmu specification admits the same closed 0--100 range, giving 6:0.
Alacritty, xterm and foot still abstain.

Out-of-range values do not have a consensus representation.  For 101,
Ghostty, Kitty and Contour clamp to 100, iTerm2 and the ConEmu specification
reject the command, and VTE publishes 101.  Negative pause values are rejected
by iTerm2, Contour and the specification; Ghostty and Kitty treat one like an
omitted value, while VTE turns it into zero.  The tests retain Shitty's
specification-compliant rejection for both values rather than selecting one
of the incompatible implementation policies without a majority.

The missing-value votes in the last case are separate for every state:

| state | zero supporters | contrary implementations | abstentions | result |
| --- | --- | --- | --- | --- |
| normal (1) | Ghostty, Kitty, Contour, VTE | iTerm2 and the specification reject | none | 4:2 zero |
| error (2) | Kitty, VTE, iTerm2 | Contour preserves the previous value | Ghostty and the specification do not define a numeric value | 3:1 zero |
| indeterminate (3) | Kitty, VTE | Contour preserves the previous value | Ghostty, iTerm2 and the specification treat percentage as inapplicable | 2:1 zero |

All five implementations reset stopped state to an empty progress value.  A
normal state with 101 remains rejected for the same unresolved range-policy
split described above.

### Product change for cases 59 through 65

The OSC parser previously dispatched progress only when a percentage was
present, making a valid `OSC 9;4;4 ST` indistinguishable from an unsupported
sequence.  `osc_PROGRESS` now carries `percentPresent` separately from the
numeric value.  `VtermImpl` retains the last percentage for an omitted paused
value, resets an omitted error and an indeterminate state to the consensus
numeric zero, resets stopped/new progress as appropriate, and publishes zero
for a fresh or explicitly zero progress bar.  Explicit values above 100 remain
invalid and are still ignored.  No progress state or test-only screen API was
added.

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

## LineBlock cases 1 through 17

The first 17 methods in `ModernTests/LineBlockTests.swift` are represented in
`tests/test_iterm2_line_block.py`.  `LineBlock` itself is a private iTerm2
storage chunk, so Shitty does not grow a mirror API for its raw offsets,
generation counter, cache flags or destructive tail methods.  Each method is
instead attached to the public terminal transaction for which iTerm2 uses that
operation.  This preserves the feature rather than discarding cases whose
implementations use different storage layouts.

### Cases 1 through 7: append, boundaries and width-two cells

A fresh terminal has an empty history and blank page.  A CR/LF commits a hard
line boundary, while DECAWM overflow records a soft continuation which copy
and reflow treat as one logical line.  Alacritty's `WRAPLINE`, Ghostty's page
row wrap state, Kitty's `next_char_was_wrapped`, xterm's line wrap flag,
Contour's `LineFlag::Wrapped`, iTerm2's EOL values, VTE's `soft_wrapped`, and
foot's row linebreak state all make the same distinction.  The
[VT510 DECAWM definition](https://vt100.net/docs/vt510-rm/DECAWM.html) agrees
that right-margin graphic input continues at the beginning of the next row;
CR and LF remain explicit controls.  The result is 9:0 for the public hard and
soft boundary behavior.

The fixed-capacity source failure is not exposed as a terminal failure.  In
iTerm2, `LineBuffer` responds by allocating another `LineBlock`; the other
seven implementations likewise grow, rotate or replace their internal row
storage while accepting the stream.  All eight therefore vote that an input
line crossing a private allocation boundary is retained until the configured
public history limit evicts its oldest rows.  The executable case crosses
multiple chunks with 9013 cells and compares every cell.  The VT510 manual
specifies display and scrolling of received graphic characters but no host
scrollback allocator, so it abstains on chunk layout.

The sixth upstream method is only an empty placeholder.  It is kept executable
as the named ASCII segmentation case.  The seventh claims a double-width
character but its source body uses only `ABCDE`; the adaptation strengthens it
with a real U+754C at the wrap edge.  All eight implementations keep a
width-two glyph and its continuation together.  Unicode Standard Annex #11
supplies the concrete East Asian Width property, but does not specify emulator
row sentinels and therefore abstains on the atomic-storage policy.  The
implementation vote is 8:0.

### Cases 8 through 10: wrapped-line counts and cache independence

iTerm2 caches the number of physical rows by wrap width.  That cache is not a
terminal feature, but its public contract is: observing a layout does not
mutate it, a full recomputation returns the same layout, and results for one
width cannot be reused for another.  Alacritty, Ghostty, Kitty, Contour,
iTerm2, VTE and foot all implement width reflow and agree on the 7-cell layouts
at widths 3, 4 and 7.  Their mechanisms range from rebuilding a ring to
transactionally cloning pages; the same-width and width-round-trip results are
identical.  Xterm changes physical row width without reflowing prior logical
lines and abstains.  VT510 defines page autowrap, not host-window reflow or
cache lifetime, and also abstains.  The supported vote is 7:0.

The three executable cases separately check same-width observation without a
model-generation change, recomputation after a width round trip, and
width-keyed layouts.  No cache inspection command was added.

### Cases 11 through 17: removing the newest history tail

`popLastLine`, `removeLastWrappedLines` and `removeLastRawLine` are used when
iTerm2 restores newest history into a taller live page.  The public cases grow
a one-row page and separately require: one final segment of a long logical
line, one whole short line, two final wrapped segments, all segments, only the
newest of two hard lines, and a clean zero-history state after consuming the
sole line.  Soft/hard marks and all retained text are checked after every
transaction.  The last source method is observed directly as the drawn-cell
length of each newest hard line.

All eight implementations support this bottom-anchored policy: Alacritty's
`grow_lines` pulls `from_history`; Ghostty has populated-scrollback row-growth
transactions; Kitty implements it behind
`scrollback_fill_enlarged_window`; xterm's default `SouthWest` resize gravity
unsaves newest rows; Contour's `growLines` rotates saved rows back; iTerm2 pops
from `LineBuffer`; VTE repositions its ring at the bottom; and foot rebuilds
the bottom-anchored grid from oldest to newest.  Kitty's default for the option
is false, but it votes because the implementation explicitly supports the
feature being tested.  The vote is 8:0.  VT510 page memory has no emulator
scrollback or host resize restoration rule and abstains.

### Audited revisions for LineBlock cases 1 through 17

| implementation | relevant source | revision |
| --- | --- | --- |
| Alacritty | `alacritty_terminal/src/grid/resize.rs`, `grid/mod.rs`, `term/cell.rs` | `1b2b36a64e88` |
| Ghostty | `src/terminal/Screen.zig`, `PageList.zig` | `94d775fefc21` |
| Kitty | `kitty/resize.c`, `line-buf.c`, `screen.c`, `kitty_tests/screen.py` | `5734bb5a587c` |
| xterm | `screen.c`, `charproc.c`, `xterm.man`, `ptyx.h` | `6380a3eaed85` |
| Contour | `src/vtbackend/Grid.cpp`, `Screen.cpp`, `Line.cpp`, `Line.hpp` | `c51e15ed254e` |
| iTerm2 | `ModernTests/LineBlockTests.swift`, `sources/LineBuffer/LineBlock.mm`, `LineBuffer.m`, `VT100ScreenMutableState.m` | `3ec57866cd9b` |
| VTE | `src/vte.cc`, `ring.cc`, `doc/rewrap.txt` | `3d55bbdddb87` |
| foot | `grid.c`, `terminal.c` | `a635e0a196d9` |

## LineBlock cases 18 through 37

The next 20 methods in `ModernTests/LineBlockTests.swift` are represented in
`tests/test_iterm2_line_block.py`.  The source methods expose rope offsets,
entry indices, dictionaries and copy ancestry.  The executable adaptations do
not add those private storage concepts to Shitty.  They exercise the terminal
transactions that consume them: width reflow, oldest-history eviction,
hard/empty line extraction, primary/alternate preservation, independent
sessions and coordinate-based selection.

### Cases 18 through 25: wrapped lengths and oldest-history eviction

Case 18 recomputes the physical representation of `ABCDEFGHIJ` at widths 4,
5 and 20.  Alacritty, Ghostty, Kitty, Contour, iTerm2, VTE and foot reflow the
logical line and produce respectively 3, 2 and 1 physical rows.  Xterm does
not reflow existing page contents after a host resize and abstains.  The
supported implementation vote is 7:0.  Digital's
[VT510 DECAWM definition](https://vt100.net/docs/vt510-rm/DECAWM.html) defines
continuation at the page's right border, but not host-window resize reflow, so
it also abstains on the width round trip.

Cases 19 through 21 remove fewer than, exactly, and more than the available
oldest physical history rows.  All eight implementations have a bounded
scrollback structure and clamp eviction to its current population:
Alacritty shrinks the front of `Storage`, Ghostty enforces `PageList` line
limits, Kitty rotates `HistoryBuf`, xterm caps `savedlines`, Contour rotates
its bounded `Grid`, iTerm2 advances the first `LineBlock` entry, VTE advances
the `Ring` start, and foot overwrites the oldest rows of its circular grid.
The surviving result is always the newest complete physical-row tail.  A
partial logical line therefore retains precisely its surviving suffix.  The
vote is 8:0.  The executable over-drop uses repeated `CSI 3 J`; all eight
implement xterm's erase-saved-lines extension and make it idempotent.
ECMA-48 has no host scrollback store or `ED 3` value and abstains.

Cases 22 through 25 check fresh/empty storage, zero-length hard lines, ordered
hard lines and a partial drop spanning several raw lines.  All eight preserve
each CR/LF-created empty row as a real hard boundary, keep non-empty hard rows
in input order, and expose only the unevicted suffix after a bounded-history
drop.  The implementation vote is 8:0.  ECMA-48 fifth edition sections
8.3.15 and 8.3.74 define CR and LF as independent format effectors and agree
that repeated occurrences continue moving the active position even when no
graphic character intervenes.  It does not define the host's raw-line
indices, offsets or eviction accounting and abstains on those details.

### Cases 26 through 31: preservation and independent ownership

The dictionary round trip and deep/COW copies are iTerm2 persistence
mechanisms, not wire protocols.  Their public invariant is nevertheless
observable: a stored screen retains text, hard/soft boundaries and cell
metadata while another independently owned screen is mutated.

All eight implementations keep primary and alternate storage separate and
restore primary contents after a `1049` round trip.  Alacritty swaps two
grids, Ghostty's `ScreenSet` owns separate pages, Kitty has `main_linebuf` and
`alt_linebuf`, xterm has normal and alternate buffers, Contour has separate
display screens, iTerm2 owns primary and alternate grids, VTE owns normal and
alternate rings, and foot owns normal and alternate grids.  The executable
round trip additionally retains an OSC 8 hyperlink.  The vote is 8:0.
ECMA-48 does not standardize private mode 1049 or terminal persistence and
abstains.

The remaining copy cases use two independent sessions and an already
published model snapshot.  Each of the eight implementations constructs
independent screen/history ownership per terminal instance; mutation of one
instance cannot alter another, and an observer's completed snapshot cannot be
retroactively rewritten.  This architectural invariant is 8:0.  ECMA-48
defines the behavior of one presentation device and abstains on host object
ownership and snapshot lifetime.  No copy ancestry, generation counter or
serialization test command was added to Shitty.

Leading and trailing empty-line counts and `containsAnyNonEmptyLine` are
covered separately even though they share storage paths.  All eight retain
leading and interior hard empty rows, reset a trailing-empty run when a new
graphic row arrives, and make a public erase remove the final non-empty
presentation.  ECMA-48 CR/LF and ED semantics agree with those presentation
effects.  The combined vote is 9:0.

### Cases 32 through 37: physical-to-logical coordinate mapping

The six source helpers calculate rope offsets, counts of full wrapped rows and
the raw line containing a physical row.  Their public consumer is coordinate
mapping for copy/selection.  The executable cases select every ASCII segment
of `ABCDEFGHIJ`, the wide-boundary rows of `AB中DEF`, and the logical lines
`One`, `Four`, and `Hello`; empty hard lines are interspersed with `A` and
`BC` to keep the zero-cell boundaries observable.

At the original output width all eight implementations agree on the ASCII
row starts, hard-line ownership and empty-line positions.  They also keep the
U+4E2D glyph and its continuation atomic when it does not fit after `AB`:
the first physical row ends in a soft pre-wrap boundary, the second contains
the complete wide glyph plus `D`, and the last contains `EF`.  The vote is
8:0 for every public coordinate result.  Unicode Standard Annex #11 defines
U+4E2D's East Asian width property but does not prescribe a terminal's spacer
cell or selection-coordinate representation, so it votes for width two and
abstains on the internal offset.  VT510 DECAWM agrees on the soft row
continuation and likewise abstains on host selection storage.

No product change or test-only `LineBlock` API was needed for cases 18 through
37.

### Audited revisions for LineBlock cases 18 through 37

| implementation | relevant source | revision |
| --- | --- | --- |
| Alacritty | `alacritty_terminal/src/grid/mod.rs`, `grid/storage.rs`, `grid/resize.rs`, `term/mod.rs`, `selection.rs` | `1b2b36a64e88` |
| Ghostty | `src/terminal/PageList.zig`, `Screen.zig`, `Terminal.zig`, `Selection.zig` | `94d775fefc21` |
| Kitty | `kitty/history.c`, `line-buf.c`, `resize.c`, `screen.c`, `mouse.c` | `5734bb5a587c` |
| xterm | `screen.c`, `charproc.c`, `ptyx.h`, `button.c`, `ctlseqs.txt` | `6380a3eaed85` |
| Contour | `src/vtbackend/Grid.cpp`, `Screen.cpp`, `Line.cpp`, `Selector.cpp` | `c51e15ed254e` |
| iTerm2 | `ModernTests/LineBlockTests.swift`, `sources/LineBuffer/LineBlock.mm`, `LineBuffer.m`, `VT100ScreenMutableState+Resizing.m` | `3ec57866cd9b` |
| VTE | `src/vte.cc`, `vteseq.cc`, `ring.cc`, `doc/rewrap.txt` | `3d55bbdddb87` |
| foot | `grid.c`, `terminal.c`, `selection.c`, `csi.c` | `a635e0a196d9` |

## LineBlock cases 38 through 57

The next 20 active methods in `ModernTests/LineBlockTests.swift` are
represented in `tests/test_iterm2_line_block.py`.  The commented-out
`testWriteRandom` generator is not counted as an upstream case.  A
comment-aware inventory comparison confirms that `PORTED_CASES` is exactly
the first 57 of 114 active methods, leaving 57.

These methods mix public terminal behavior with iTerm2-private rope offsets,
metadata-array counts, invalidation flags and a 10,000-entry `LineBlock`
capacity.  The adaptations preserve the source scenario but observe it at the
public terminal boundary.  They do not add a `LineBlock`, search, bidi, raw
offset or invalidation API to Shitty.

### Cases 38 through 42: line positions and randomized storage churn

Cases 38 through 41 map an offset at an ordinary cell, either side of a hard
EOL, and at the start of a wrapped continuation.  The public adaptations use
selection endpoints over `Hello` and the physical rows of
`ABCDE`, empty, `ABCDE` at width two.  All eight implementations retain the
same hard boundary and soft-wrap ownership and expose stable cell-to-logical
line coordinates to their selection code.  The implementation vote is 8:0.

[VT510 DECAWM](https://vt100.net/docs/vt510-rm/DECAWM.html) specifies that a
graphic character at the right border continues on the following line when
autowrap is enabled.  ECMA-48 specifies CR/LF active-position movement and
graphic-character positions.  They therefore vote for the observable
hard/soft boundary and row origin, but abstain on iTerm2 rope offsets and its
`wrapOnEOL` argument.

Case 42 replaces iTerm2's private random block writer and golden offset table
with a deterministic 200-transaction LCG stream.  An independent model splits
hard logical lines into width-eight physical rows, applies the configured
17-row history bound, and predicts the exact 20-row public tail.  Height grow
and shrink must move four rows between history and the page without changing
that tail.  Alacritty's `Storage`, Ghostty's `PageList`, Kitty's line/history
buffers, xterm's saved-line store, Contour's bounded `Grid`, iTerm2's
`LineBuffer`, VTE's `Ring`, and foot's circular grid all implement this same
bounded newest-tail invariant.  The vote is 8:0.  ECMA-48 and VT510 do not
define emulator scrollback capacity or randomized host transactions and
abstain.

### Cases 43 through 46: searchable text and match ranges

The source cases require a forward literal match, reverse enumeration of
three matches, case-insensitive regex matching, and an explicit multiline
match.  Support was checked even where an implementation uses a different
search engine:

| implementation | observable support and vote |
| --- | --- |
| Alacritty | Forward/backward regex search, smart case, and matches spanning soft wraps; a hard line ends a search subject.  Votes for cases 43--45 and for preserving the hard separator in case 46. |
| Ghostty | Forward active-page and reverse history search, ASCII-insensitive literals, soft-line joining and explicit hard newlines in the search formatter.  Votes for all four observable results. |
| Kitty | Exports scrollback with hard newlines and soft continuations to the configured pager.  Votes for the searchable text/ranges; the delegated engine abstains on direction, regex and case policy. |
| xterm | Has selection and dabbrev-style word expansion but no general terminal-buffer search API.  Votes for exact selection ranges and retained hard separators; abstains on search-engine policy. |
| Contour | Forward/reverse literal search with smart Unicode case over `LogicalLine`; hard boundaries delimit a subject.  Votes for cases 43--45 and for preserving the case-46 separator. |
| iTerm2 | Implements the exact literal/regex, forward/backward and multiline source cases.  Votes for all four. |
| VTE | PCRE2 next/previous search with caseless and multiline flags; one hard logical line is one search subject.  Votes for cases 43--45 and for preserving the case-46 separator. |
| foot | Forward/backward smart-case literal search; regex is a separate URL feature and search input strips newlines.  Votes for cases 43--45 at the public occurrence ranges and for preserving the hard separator; abstains on regex/multiline engine policy. |

Thus forward/backward occurrence ranges and case-insensitive matching have six
native terminal-search implementations in agreement, with Kitty and xterm
abstaining only where their engine is delegated or absent.  Explicit matching
across a hard boundary is deliberately recorded as divergent: iTerm2 and
Ghostty support it, while Alacritty, Contour and VTE bound a search subject at
that boundary, foot removes newline from search input, Kitty delegates the
decision, and xterm abstains.  The feature is not discarded because the
implementations differ.

Shitty currently has no host search interface.  The executable adaptations
therefore verify the prerequisite public contract without pretending to add
one: exact forward and reverse occurrence ranges can be selected, the full
mixed-case line can be extracted for regex matching, and a hard line remains
the byte `\n` separator in extracted multiline text.  POSIX.1-2024
[`regcomp`/`regexec`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/regcomp.html)
votes for case-insensitive matching and defined newline/anchor treatment, but
abstains on terminal storage, search direction and UI policy.

### Cases 47 through 50: mutation, invalidation and capacity rollover

Case 47 requires metadata-entry accounting to follow append and pop.  The
public adaptation observes two appended history lines, grows the page by one
row, and requires the count to fall by exactly one while content remains
unchanged.  All eight storage implementations couple row metadata lifetime to
the corresponding row and agree; the vote is 8:0.

Cases 48 and 49 publish a snapshot, mutate the screen, and reuse storage after
removing its previous newest line.  Every implementation invalidates its
damage/snapshot generation on mutation and clears reused cells and wrap
metadata.  The adaptation erases `ABCDEFG`, writes the shorter `XYZ`, and
checks the old snapshot, new digest, drawn cells and wrap flag independently.
The vote is 8:0.

Case 50's failed append at 10,000 lines is a private iTerm2 `LineBlock`
allocation boundary, not a terminal input failure.  iTerm2's `LineBuffer`
allocates another block; Alacritty, Ghostty, Kitty, xterm, Contour, VTE and
foot likewise advance, allocate or rotate their respective page/ring chunks.
All eight therefore agree that the public terminal continues consuming input
and retains the configured newest tail.  The adaptation writes 10,001 hard
lines and checks rollover into a 10,000-row public history plus the final
`XYZ`.  It intentionally does not copy the private failed-return value.  The
vote is 8:0.  ECMA-48 defines neither snapshot invalidation nor host storage
capacity and abstains on cases 47--50.

### Cases 51 through 57: RTL metadata, suffix offsets and padded cells

Case 51 records that a line contains strong RTL text.  iTerm2 propagates that
metadata and VTE implements terminal bidi classification and rendering.  The
other six preserve the logical codepoint stream and selection text but do not
expose the same private flag; they abstain on the flag rather than voting
against the feature.  Unicode
[UAX #9](https://www.unicode.org/reports/tr9/) classifies Hebrew letters as
strong `R` characters and distinguishes display reordering from the logical
text stream.  The executable adaptation consequently requires `abc אבג` to
survive wrapping, width reflow and selection in logical order.  The supported
metadata vote is 2:0; the logical-text preservation vote is 8:0 plus UAX #9.

Cases 52 through 56 ask for remaining block sizes and raw-line starts at
missing, before-first, middle and exact-boundary offsets.  Public selection
over a wrapped `ABCDEFGHIJ` must expose suffixes of 10, 6 and 2 bytes;
negative coordinates clamp to the first cell, a word selection inside
`World` expands to exactly its own raw line, and the start of the second hard
line cannot include `Hello`.  Selection implementations in all eight map
these same cell/logical boundaries, for an 8:0 vote.  ECMA-48 has no host
selection or rope-offset representation and abstains.

Case 57 requires a short wrapped-line result to contain width-sized padding.
All eight grids represent `XYZ` on a six-column row followed by three blank,
undrawn cells; the adaptation checks both the visible spaces and drawn-cell
metadata.  ECMA-48's presentation space has the configured columns but does
not define a host `screen_char_t` padding array, so it votes for the visible
row and abstains on representation.  The implementation vote is 8:0.

No product change was needed for cases 38 through 57.

### Audited revisions for LineBlock cases 38 through 57

| implementation | relevant source | revision |
| --- | --- | --- |
| Alacritty | `alacritty_terminal/src/grid/resize.rs`, `grid/storage.rs`, `term/mod.rs`, `term/search.rs`, `selection.rs` | `1b2b36a64e88` |
| Ghostty | `src/terminal/PageList.zig`, `Screen.zig`, `Selection.zig`, `search/active.zig`, `search/pagelist.zig`, `search/sliding_window.zig` | `94d775fefc21` |
| Kitty | `kitty/history.c`, `line-buf.c`, `screen.c`, `options/definition.py` | `5734bb5a587c` |
| xterm | `screen.c`, `button.c`, `charproc.c`, `ptyx.h` | `6380a3eaed85` |
| Contour | `src/vtbackend/Grid.hpp`, `Line.hpp`, `CellUtil.hpp`, `Screen.cpp`, `Selector.cpp` | `c51e15ed254e` |
| iTerm2 | `ModernTests/LineBlockTests.swift`, `sources/LineBuffer/LineBlock.mm`, `LineBuffer.m`, `LineBlockMetadataArray.m` | `3ec57866cd9b` |
| VTE | `src/ring.cc`, `vte.cc`, `bidi.cc`, `app/app.cc`, `vte/vteterminal.h` | `3d55bbdddb87` |
| foot | `grid.c`, `search.c`, `selection.c`, `extract.c` | `a635e0a196d9` |

## LineBlock cases 58 through 77

The next 20 active methods in `ModernTests/LineBlockTests.swift` are
represented in `tests/test_iterm2_line_block.py`.  A source-order inventory
comparison confirms that `PORTED_CASES` is exactly the first 77 of 114 active
methods, leaving 37.

The source cases cover five independently observable contracts: mapping a
physical wrapped row back to its hard logical line and metadata, refreshing
bidi state without corrupting logical text, reverse/per-line search ranges,
double-width coordinate mapping, and mutation/COW identities used to decide
whether an append-only merge is safe.  The adaptations exercise each source
scenario through terminal input, screen snapshots, selection, hyperlinks,
shell semantic marks, reflow and model identity.  No iTerm2 `LineBlock`, bidi
cache, search engine or mutation-counter API is added to Shitty.

### Cases 58 through 61: wrapped-to-raw mapping and metadata

Alacritty's wrapped `Grid` rows and selections, Ghostty's `PageList` pins,
Kitty's `next_char_was_wrapped` history rows, xterm's `LineTstWrapped` walks,
Contour's `logicalLineHead`, iTerm2's exact source methods, VTE's `Ring` rows,
and foot's `linebreak` rows all map every soft continuation back to one hard
logical line.  Each also carries cell attributes through wrapping and reflow;
the concrete metadata representation differs (OSC 8 IDs, semantic marks,
row/cell attributes, timestamps).  The vote for logical ownership and
metadata following the cells is 8:0.  Implementations without iTerm2's
timestamp and `iTermExternalAttributeIndex` abstain on those private types.

The executable cases use `ABC` plus wrapped `DEFG`, wrapped `One`/`TwoTwo`,
two distinct OSC 8 runs, and an OSC 133 prompt spanning two physical rows.
They require exact hard/soft boundaries and require both halves of a wrapped
logical line to expose the same public metadata.

VT510 DECAWM defines continuation after the right margin and ECMA-48 defines
the hard CR/LF active-position transition.  They vote for the observable
logical boundary, but abstain on host row metadata and rope indices.

### Cases 62 through 67: bidi cache lifecycle

iTerm2 and VTE implement terminal bidi analysis and vote 2:0 for populating,
discarding, recomputing and clearing cached bidi results as content changes.
Alacritty, Ghostty, Kitty, xterm, Contour and foot do not expose an equivalent
terminal bidi cache and abstain on that implementation detail.  All eight,
however, retain the logical Unicode stream across output, erase and reflow;
their selection/extraction paths therefore vote 8:0 for the public contract.

Unicode UAX #9 is the standard vote.  It classifies Hebrew letters as strong
right-to-left characters, performs reordering only for display, and requires
characters to remain interpreted in logical order.  The six executable cases
therefore check logical selection, width reflow, complete erase, idempotent
observation, erase-then-rewrite recovery, and the absence of stale RTL state
on a later LTR replacement.  This preserves the supported bidi feature even
though only two implementations share iTerm2's cache mechanism.

### Cases 68 through 71: COW, search and double-width coordinates

Case 68's progenitor synchronization is a private COW operation.  All eight
storage implementations nevertheless keep a published view stable while
their bounded history drops an older leading row; the public vote is 8:0,
while the seven non-iTerm2 implementations abstain on `dropMirroringProgenitor`.

For cases 69 and 70 the implementation vote is:

| implementation | observable search vote |
| --- | --- |
| Alacritty | Native forward/backward regex search over wrapped terminal rows; votes for the rightmost reverse match and exact occurrence ranges. |
| Ghostty | Native active-page/history reverse search over logical text; votes for both public ranges. |
| Kitty | Delegates scrollback search to the configured pager; votes for exported hard-line text and ranges, abstains on engine direction and one-result-per-line policy. |
| xterm | Has wrapped-line selection but no general buffer-search API; votes for selectable ranges and abstains on search policy. |
| Contour | Native `Grid::searchReverse`; votes for reverse matching and per-line ranges. |
| iTerm2 | Implements the exact two source cases; votes for both. |
| VTE | PCRE2 previous/next search; votes for the rightmost reverse match and line-scoped ranges. |
| foot | Native backward search in `search.c`; votes for reverse matching and line-scoped ranges. |

Thus six native search implementations agree on reverse traversal; Kitty and
xterm abstain only on the missing/delegated engine.  The iTerm2 option that
limits output to one result per raw line remains supported rather than being
discarded: Shitty has no host search API, so the executable public adaptation
selects the independently addressable first occurrence on each hard line.
POSIX.1-2024 `regcomp`/`regexec` votes for the regular-expression match and
rightmost result chosen by reverse enumeration, and abstains on terminal UI
policy.

Case 71 maps offsets around `A中BC` to the real visible cells.  All eight
represent the wide glyph plus a continuation/spacer cell and keep selection
coordinates on the glyph boundary, for an 8:0 vote.  Unicode UAX #11 classifies
the East Asian width property used by terminal width policy; it votes for the
wide `中` character and abstains on iTerm2's private offset function.

### Cases 72 through 77: mutation identity and incremental merge

Alacritty's `TermDamageState`, Ghostty page generations/dirty rows, Kitty's
`has_dirty_text`, xterm's screen updates, Contour's dirty-line stamping,
iTerm2's mutation counter, VTE invalidation, and foot's row/view damage all
invalidate rendered state after content mutation.  They vote 8:0 that append
and erase change the published model, that a previously returned snapshot
does not mutate retroactively, and that two divergent descendants have
different observable content.  The seven non-iTerm2 implementations abstain
on the exact counter allocation strategy.

Cases 76 and 77 exercise iTerm2's append-only merge eligibility.  A partial
`abc` line may grow to `abcdef` without changing its hard-line ownership; an
append after CR/LF must remain a separate hard line.  Every implementation
has an incremental or dirty-row path for the first case and preserves the
hard boundary in the second, for an 8:0 public vote.  ECMA-48 and VT510 vote
for the resulting graphic-character stream and CR/LF boundary and abstain on
the internal merge optimization.

No product change was needed for cases 58 through 77.  All 78 public tests in
the module (77 source adaptations plus the inventory assertion) pass with
both parser backends.

### Audited revisions for LineBlock cases 58 through 77

| implementation | relevant source | revision |
| --- | --- | --- |
| Alacritty | `alacritty_terminal/src/grid/resize.rs`, `term/mod.rs`, `term/search.rs`, `selection.rs` | `1b2b36a64e88` |
| Ghostty | `src/terminal/PageList.zig`, `Screen.zig`, `Selection.zig`, `search/active.zig`, `search/pagelist.zig` | `94d775fefc21` |
| Kitty | `kitty/history.c`, `line-buf.c`, `screen.c` | `5734bb5a587c` |
| xterm | `button.c`, `screen.c`, `charproc.c`, `ptyx.h` | `6380a3eaed85` |
| Contour | `src/vtbackend/Grid.cpp`, `Grid.hpp`, `Line.hpp`, `Selector.cpp` | `c51e15ed254e` |
| iTerm2 | `ModernTests/LineBlockTests.swift`, `sources/LineBuffer/LineBlock.mm`, `LineBuffer.m` | `3ec57866cd9b` |
| VTE | `src/ring.cc`, `vte.cc`, `bidi.cc`, `app/app.cc` | `3d55bbdddb87` |
| foot | `grid.c`, `search.c`, `selection.c`, `extract.c`, `terminal.c` | `a635e0a196d9` |

## LineBlock cases 78 through 97

The next 20 active methods in `ModernTests/LineBlockTests.swift`, from
`testCanIncrementalMerge_IneligibleWhenLastLinePopped` through
`testCanIncrementalMerge_IneligibleWhenPartialLineCompleted`, are represented
in `tests/test_iterm2_line_block.py`.  The commented-out `testWriteRandom`
generator is not an active XCTest method.  Removing Swift block comments and
comparing names in source order proves that `PORTED_CASES` is exactly the
first 97 of 114 active methods, leaving 17.

These methods test an iTerm2 optimization, not a terminal protocol: a
`LineBuffer` COW copy may catch up by copying only an append-only delta from
its `LineBlock` progenitor.  A pop, reset, hard-line transition, history drop,
mutation of the copy, or any other divergence must force a full replacement.
The executable adaptations preserve every independently observable part of
that contract: old snapshots stay immutable, current text is exact, chunk
boundaries do not become line boundaries, real CR/LF boundaries do, and
recomputed wrap, rendition, bidi and double-width state describes the merged
text.  No `LineBlock`, progenitor, COW, or incremental-merge API is added to
Shitty.

### Cases 78 through 83: merge eligibility and destructive divergence

The implementation vote for independent published storage is:

| implementation | storage/copy evidence | vote |
| --- | --- | --- |
| Alacritty | `Grid<T>` and its `Storage<T>` are deep `Clone`; resize copies/reflows rows and cell flags. | A clone does not change when the live grid is later replaced. |
| Ghostty | `PageList.clone` and `Screen.clone` copy pages, cells, hyperlinks, cursor and selection pins. | A clone is independent of later page mutation or generation renewal. |
| Kitty | `create_line_copy`, `copy_line_to`, `copy_old` and rewrap destinations copy cell and line attributes. | Published line data is independent of later source writes. |
| xterm | `copyLineData` copies rows between separately allocated `ScrnBuf` storage during save/resize/alternate-screen operations. | A copied row is not an alias for a subsequently edited row. |
| Contour | `Grid` exposes full attach/resync snapshots and revision-stamped deltas; render snapshots own their cells. | A published revision remains stable and destructive edits require new state. |
| iTerm2 | The exact `cowCopy`, owner/progenitor and invalidation implementation. | Votes for the complete private optimization and public result. |
| VTE | `Ring` freezes rows into streams and replaces/regenerates those streams during rewrap. | Frozen/published row content remains stable while current storage changes. |
| foot | `grid_snapshot` deep-copies rows, cells, URI/underline ranges, cursor and damage. | A snapshot is independent of later grid mutation. |

The public snapshot-isolation vote is therefore 8:0.  The seven
non-iTerm2 implementations abstain on the private
`canIncrementalMergeFromProgenitor` predicate itself.

The six executable eligibility cases distinguish EL replacement, direct
append with no prior observer, RIS invalidation, a partial last line following
an existing hard line, append after a completed line, and observation with no
growth.  All eight implementations process EL/RIS, hard line boundaries and
unchanged state with the same observable result, for an 8:0 vote.
[ECMA-48, fifth edition](https://www.ecma-international.org/wp-content/uploads/ECMA-48_5th_edition_june_1991.pdf)
is the standard vote for CR, LF, EL, ED and RIS.  It requires their screen and
active-position effects, and abstains on host snapshots and COW eligibility.

### Cases 84 through 86 and 90 through 93: append-only content and snapshots

All eight parsers accept graphic characters as a stream rather than assigning
semantic meaning to the caller's write chunks.  Their storage paths differ:
Alacritty mutates cloned grid rows, Ghostty mutates generation-tracked pages,
Kitty marks line text dirty, xterm updates `LineData`, Contour revision-stamps
dirty rows, iTerm2 copies the delta, VTE appends/freeze-thaws ring rows, and
foot dirties the current row.  Nevertheless they agree 8:0 on all observable
results covered here:

- a differently styled suffix preserves the prefix's SGR attributes;
- one append, three appends, a 100-character append and nine merge/observe
  cycles produce the exact concatenated stream;
- a current buffer/view observes the complete line while every previously
  published snapshot remains unchanged;
- observing or reconciling a copy does not mutate the live source;
- a delta that exactly fills the right margin retains all ten cells and leaves
  wrap pending until another printable character arrives.

The VT510 Programmer Information definition of
[DECAWM](https://vt100.net/docs/vt510-rm/DECAWM.html) is the concrete terminal
standard for the last rule.  Alacritty `input_needs_wrap`, Ghostty
`pending_wrap`, Kitty `mDECAWM`, xterm `do_wrap`, Contour `wrapPending`,
iTerm2's cursor-at-width state, VTE's cursor-at-column-count state and foot's
`cursor.lcf` independently implement that deferred wrap, so the implementation
vote is 8:0.  ECMA-48 additionally votes for SGR and graphic-character stream
semantics, while abstaining on write-call boundaries and storage capacity.

### Cases 87 through 89: bidi, wrap cache and double-width propagation

The RTL append case preserves `abcאבג` in logical order and makes the whole
line selectable.  iTerm2 and VTE implement terminal bidi analysis and vote
2:0 for refreshing bidi metadata after an appended RTL suffix.  Alacritty,
Ghostty, Kitty, xterm, Contour and foot do not implement an equivalent bidi
display cache and abstain on that cache, but all six preserve the logical
Unicode stream; the public logical-text vote is 8:0.
[Unicode UAX #9](https://www.unicode.org/reports/tr9/) is the standard vote:
directional processing changes display order, not the underlying logical
character order, and is scoped to a paragraph.

For 100 `x` cells followed by 50 `y` cells at width 80, all eight keep one
soft logical line and recompute two visible rows rather than retaining an old
count.  Their mechanisms are Alacritty grid wrap flags, Ghostty page reflow,
Kitty continuation bits/rewrap destinations, xterm `LineTstWrapped`, Contour
logical-line reflow, iTerm2 cache invalidation, VTE `Ring::rewrap`, and foot
`linebreak` reflow.  The observable wrap result is 8:0; implementations
without iTerm2's cached line-count field abstain on that private cache.

Appending `中` to an ASCII prefix produces one wide cell and its continuation
cell in every implementation, for an 8:0 vote.  Alacritty explicitly carries
`WIDE_CHAR`/spacer flags through reflow, VTE inflates bidi mappings by the
cell's column width, and foot copies the wide cell plus spacer as one unit;
the other five have equivalent wide-cell pairs.  [Unicode UAX #11](https://www.unicode.org/reports/tr11/)
classifies the ideograph as Wide and votes for the wide/narrow distinction,
while explicitly leaving terminal-specific tailoring to implementations.

### Cases 94 through 97: partial, removal and history-drop invalidation

The final four cases complete a partial line without text, rebuild
the current display after ED, and evict a wrapped prefix through bounded
history, then append text and complete the partial line with CR/LF.  All eight
preserve the hard boundary and newest bounded-history tail, for an 8:0 public
vote.  ECMA-48 votes for CR/LF and ED; it abstains on the configured history
limit and iTerm2's append-only flag.  The feature is not discarded merely
because only iTerm2 exposes the optimization: every supported public
consequence remains executable.

No product change was needed for cases 78 through 97.  All 98 public tests in
the module (97 source adaptations plus the inventory assertion) pass with
both parser backends.

### Audited revisions for LineBlock cases 78 through 97

| implementation | relevant source | revision |
| --- | --- | --- |
| Alacritty | `alacritty_terminal/src/grid/{mod,resize,row,storage}.rs`, `term/{mod,cell}.rs` | `1b2b36a64e88` |
| Ghostty | `src/terminal/{PageList,Screen,Terminal}.zig` | `046b8fcc2a9a` |
| Kitty | `kitty/{history,line-buf,screen}.c` | `5734bb5a587c` |
| xterm | `screen.c`, `charproc.c`, `button.c`, `ptyx.h` | `6380a3eaed85` |
| Contour | `src/vtbackend/{Grid,Screen,Terminal}.cpp`, `Grid.hpp`, `Cursor.hpp`, `Line.hpp` | `c51e15ed254e` |
| iTerm2 | `ModernTests/LineBlockTests.swift`, `sources/LineBuffer/{LineBlock.mm,LineBlock.h,LineBuffer.m}` | `3ec57866cd9b` |
| VTE | `src/{ring.cc,ring.hh,vte.cc,vteseq.cc,bidi.cc}` | `3d55bbdddb87` |
| foot | `grid.c`, `terminal.c`, `selection.c`, `extract.c` | `a635e0a196d9` |

## LineBlock cases 98 through 114

The final 17 active methods in `ModernTests/LineBlockTests.swift`, from
`testCanIncrementalMerge_IneligibleWhenCopyMutated` through
`testImplicitDictionaryValueInjectsGeneration`, are represented separately
in `tests/test_iterm2_line_block.py`.  A comment-aware source-order comparison
now proves that `PORTED_CASES` is exactly all 114 active methods.  The module
therefore contains 115 public tests including its inventory assertion.

The source group has two distinct subjects.  Cases 98--108 finish iTerm2's
append-only COW optimization, including divergent and multi-level copy chains
and a delta larger than the default 8192-cell block.  Cases 109--114 finish
state-restoration generation handling: saved, fallback and newly allocated
generation values, copy identity, and migration metadata injected by the graph
encoder.  Neither private representation is added to Shitty, but neither is
dropped: every source method has its own executable consumer-level scenario,
and the exact implementation support and abstentions are recorded below.

### Cases 98 through 108: divergent and chained copies

The source's exact ownership graph is an iTerm2 implementation detail.  The
public invariants it protects are stronger than mere final-text equality:
mutating one branch never changes another, a leaf follows its immediate
progenitor rather than an older ancestor, hard boundaries cannot be replayed as
an append-only character delta, published snapshots remain immutable through
arbitrarily many observations, and an allocation boundary cannot truncate
input.  The eight implementations realize those invariants as follows.

| implementation | current source evidence | vote |
| --- | --- | --- |
| Alacritty | `Grid`, `Storage`, `Row` and `Cell` are deep-clonable; cursor wrap state and every cell flag are copied with the grid. | Independent branches and snapshots retain exact content and metadata. |
| Ghostty | `PageList.clone` allocates and clones every selected page while `Screen.clone` remaps its cursor, selection and tracked pins. Page serials invalidate stale references after structural changes. | Independent branches and multi-level clones retain exact state without stale coordinates. |
| Kitty | `create_line_copy_inner`, `copy_line_to`, `copy_old` and rewrap destinations copy text plus line attributes; every edited row is dirtied. | A copied line remains stable and later appends/reflows expose the complete source stream. |
| xterm | `allocScrnBuf` owns separate row storage and `copyLineData` copies complete `LineData` between edit, save, alternate and resized buffers. | A copied row is independent; input beyond one allocation continues into later rows. |
| Contour | Stable row ids are scoped by a grid generation; a rebuild returns `ResyncRequired`, while revision cursors deliver later mutations within the generation. | A consumer either receives the complete append delta or performs a full resnapshot after divergence. |
| iTerm2 | `validMutationCertificate`, `_hasDiverged`, owner/client transfer and `incrementalMergeFromProgenitor` implement the exact COW chain and copy only the appended suffix. | Votes for every private predicate, chain and efficiency condition as well as the public result. |
| VTE | Frozen `Ring` rows are immutable streams; thaw and rewrap regenerate mutable rows and preserve cell attributes and soft-wrap structure. | Published row content remains stable while the current ring continues growing. |
| foot | `grid_snapshot` deep-copies rows, cells, cursor state, ranges and damage; resize/reflow builds distinct destination rows. | Snapshots and divergent grids are independent and preserve complete input. |

The observable ownership/content result is 8:0.  Only iTerm2 exposes
`numberOfClients`, progenitor pointers, `canIncrementalMergeFromProgenitor`,
and the exact O(1)-after-first-clone optimization; the other seven abstain on
those private predicates rather than voting against them.  The executable
adaptations retain their topology with independent sessions and immutable
model snapshots: middle/root divergence, two-step and single-delta cascade,
five-level propagation, and the non-append CR/LF boundary are separate cases.

Case 108 writes `abc` plus 10,000 `x` cells after publishing the prefix.  All
eight accept the entire logical line and allocate, rotate or resize their
private storage as necessary, for an 8:0 result.  The
[VT510 DECAWM definition](https://vt100.net/docs/vt510-rm/DECAWM.html) votes
for continuing graphic input across the right margin and abstains on an
emulator's allocation size.  ECMA-48 fifth edition sections 8.3.15, 8.3.74 and
8.3.117 vote for the CR, LF and SGR stream effects used by the chain cases and
abstain on object ownership and caller write boundaries.

### Cases 109 through 114: restoration generations

There is no cross-terminal standard for archived scrollback object identity,
and the supported implementations deliberately make different internal
choices.  That is not used as a reason to omit the feature.

| implementation | restoration/identity behavior | vote |
| --- | --- | --- |
| Alacritty | With the `serde` feature, `Grid<Cell>` has a tested lossless JSON round trip; the application also writes reference grid recordings. It stores no iTerm-style generation. | Votes for restored grid equality; abstains on generation precedence. |
| Ghostty | Snapshot format 1 restores terminal-wide state, primary/alternate screens, complete history and parser continuation transactionally. `PageList.Builder` allocates fresh page serials for decoded storage. | Votes for complete restored state and safe fresh identity; abstains on iTerm's saved/fallback generation rule. |
| Kitty | Line/history buffers can be copied, exported and rewrapped, but there is no terminal-state decoder carrying a persistent content generation. | Votes for stable copied state; abstains on external restoration and generation precedence. |
| xterm | Separate screen/save buffers and `copyLineData` preserve runtime state, but no archive generation is decoded. | Votes for runtime preservation; abstains on the archive fields. |
| Contour | A generation mismatch invalidates stable row ids and requires a full attach/resync snapshot; a normal delta cursor preserves its generation until wholesale rebuild. | Votes for generation-safe consumer resync; abstains on persisted generation preference. |
| iTerm2 | The dictionary value wins over a graph-record fallback, a missing value allocates globally unique identity, the allocator advances beyond restored values, COW copies preserve identity, and migration injects the child generation. | Votes for all six exact source contracts. |
| VTE | Ring freeze/thaw streams preserve scrollback content internally but no public archive decoder restores a generation value. | Votes for runtime content preservation; abstains on archive identity. |
| foot | `grid_snapshot` is a complete deep runtime copy and carries row metadata, but it is not a persistent decoder. | Votes for snapshot preservation; abstains on archive identity. |

Thus restored visible state has three supporting implementations in agreement
(Alacritty's tested serde, Ghostty's binary snapshot, and iTerm2's state
restoration), with the other five abstaining.  The exact saved-versus-fallback
generation precedence has one supporting implementation and no contrary vote;
Ghostty and Contour use fresh/resync identity for their non-equivalent
generation domains.  ECMA-48 specifies the rich presentation state used by the
tests (graphic characters, CR/LF and SGR) but has no terminal persistence,
alternate-screen archive or host generation concept and therefore abstains.

Shitty currently has no external terminal-state archive decoder, so the six
executable adaptations exercise each public consumer invariant without
inventing a persistence command: exact rich primary state survives its owned
alternate-screen round trip; replay without saved identity reconstructs the
same model; the primary state takes precedence over alternate contents; a new
session gets independent identity; a non-mutating snapshot preserves the
model digest; and the rich snapshot carries style, wide-cell continuation and
hyperlink metadata.  This records the external-persistence gap while retaining
all six source scenarios as executable tests.

No product change was needed.  All 115 public tests pass with both parser
backends.

### Audited revisions for LineBlock cases 98 through 114

| implementation | relevant source | revision |
| --- | --- | --- |
| Alacritty | `alacritty_terminal/src/grid/{mod,row,storage,resize}.rs`, `term/{mod,cell}.rs` | `1b2b36a64e88` |
| Ghostty | `src/terminal/{PageList,Screen,Terminal}.zig`, `src/terminal/snapshot/{main,terminal,screen,snapshot}.zig` | `046b8fcc2a9a` |
| Kitty | `kitty/{history,line-buf,resize,screen}.c` | `2caa3ca16bc9` |
| xterm | `screen.c`, `charproc.c`, `ptyx.h` | `6380a3eaed85` |
| Contour | `src/vtbackend/{Grid.cpp,Grid.hpp,Screen.cpp,Terminal.cpp}` | `c51e15ed254e` |
| iTerm2 | `ModernTests/LineBlockTests.swift`, `sources/LineBuffer/{LineBlock.mm,LineBlock.h,LineBuffer.m}`, `sources/StateRestoration/iTermEncoderGraphRecord.m` | `3ec57866cd9b` |
| VTE | `src/{ring.cc,ring.hh,vte.cc,vteseq.cc}` | `3d55bbdddb87` |
| foot | `grid.c`, `grid.h`, `terminal.c` | `a635e0a196d9` |

## LineBuffer cases 1 through 20

The first 20 active methods in `ModernTests/LineBufferTests.swift`, from
`testBasic` through `testRoundTrip_wrappedHardEOLLine`, are represented in
source order by `tests/test_iterm2_line_buffer.py`.  Its inventory is checked
against the current Swift source: the source has 75 active methods, the tuple
has 20 distinct names, and it is exactly the source prefix rather than a
hand-picked subset.

`LineBuffer` is a private host object, so the port does not add a block, COW,
search-context, or `LineBufferPosition` API to Shitty.  Every source method
still has its own executable public scenario:

- cases 1 and 2 preserve the two hard logical lines and the exact eight-row
  width-four hard/soft wrap layout;
- cases 3 through 6 replay the common prefix into two or three sessions and
  mutate every requested branch independently;
- cases 7 and 8 destroy either session and then mutate the survivor, retaining
  the owner/client lifetime result without exposing iTerm2 reference counts;
- cases 9 and 10 separately pop the newest history row on height growth and
  prune only the overflowing session's oldest rows;
- case 11 partially prunes a wrapped logical line and verifies that the cursor
  remains attached to the surviving wide glyph and suffix;
- case 12 crosses more than one backing storage page and addresses both the
  oldest and newest public coordinates;
- cases 13 through 16 cover both selection endpoint roles on a short hard line
  and on later empty rows; the end-exclusive public boundary at column 10 is
  the UI equivalent of iTerm2's inclusive `width - 1` coordinate;
- cases 17 through 20 exercise every written cell, three different past-EOL
  starts, four different past-EOL ends, and all physical rows of a wrapped hard
  line.

### Consensus audit

The exact COW topology is not confused with the observable terminal contract.
Only iTerm2 supports `LineBuffer.copy`, owner/client transfer and `forceSeal`;
the other implementations abstain on those private predicates.  They do vote
on independent saved states and rows:

| implementation | source evidence | vote |
| --- | --- | --- |
| Alacritty | `Grid`, `Storage`, rows and cells are clonable; resize creates destination rows and tracks the cursor. | Independent state and stable snapshots; abstains on iTerm2 COW identity. |
| Ghostty | `PageList.clone` deep-copies pages and `Screen.clone` remaps cursor, selection and tracked pins. | Independent state and lifetime after clone; abstains on owner/client counts. |
| Kitty | line/history copy helpers allocate destination rows and copy line attributes before mutation or rewrap. | Independent copied rows; abstains on iTerm2 progenitors. |
| xterm | `allocScrnBuf` and `copyLineData` allocate and copy separate `LineData` storage. | Independent screen/save buffers; abstains on iTerm2 COW identity. |
| Contour | grid generations and revision cursors require resnapshot after structural divergence. | A consumer keeps an immutable snapshot or obtains complete new state; abstains on COW blocks. |
| iTerm2 | `LineBuffer.copy`, owner/client transfer, `popLastLine`, `dropExcessLines` and `forceSeal` are the exact tested implementation. | Votes for all private and observable branches. |
| VTE | frozen ring rows are immutable; thaw and rewrap produce mutable destination rows. | Stable published rows and independent current state; abstains on iTerm2 COW identity. |
| foot | `grid_snapshot` deep-copies rows, cells, cursors and ranges; resize builds destination rows. | Independent snapshots and divergent grids; abstains on iTerm2 COW identity. |

Thus the public independence/lifetime result is 8:0.  The exact private COW
predicate has one supporter and seven abstentions, not seven contrary votes.
ECMA-48 has no host object ownership model and abstains.

History and coordinate behavior was checked separately:

| behavior | Alacritty | Ghostty | Kitty | xterm | Contour | iTerm2 | VTE | foot | standard |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Hard lines, CR/LF and soft autowrap remain ordered | yes | yes | yes | yes | yes | yes | yes | yes | ECMA-48 CR/LF plus VT510 DECAWM: yes |
| Bounded history retains the newest rows | yes | yes | yes | yes | yes | yes | yes | yes | abstain |
| Reflow tracks a cursor through a partially retained logical line | yes | yes | yes | abstain: no reflow | yes | yes | yes | yes | abstain |
| Screen/history coordinates remain addressable across backing allocations | yes | yes | yes | yes | yes | yes | yes | yes | abstain |
| Linear selection joins soft wraps and preserves interior hard boundaries | yes | yes | yes | yes | yes | yes | yes | yes | abstain |
| Clipboard drops *implicit* right padding but preserves written spaces | yes | yes | outlier: default `strip_trailing_spaces=never` | yes | yes | yes by default | yes | yes | abstain |

The history limit is an 8:0 result.  Cursor-preserving reflow is 7:0 with xterm
abstaining because it deliberately resizes physical rows without reflow.  All
eight support a selection range over live and saved rows.  No terminal wire
standard defines scrollback allocation, host search positions, selection, or
clipboard whitespace, so ECMA-48 and the VT510 manual abstain on those rows.

The padding audit exposed a Shitty bug rather than an oracle exception.
`ScreenBase::selectedText` previously trimmed undrawn cells only for a
rectangle or when a linear endpoint was exactly at the right margin.  The same
short line therefore copied `abcde `, `abcde  ` or `abcde    ` depending on
which unused cell happened to be the endpoint.  Seven implementations discard
that implicit padding while retaining explicitly written spaces; Kitty's
configurable default is the sole outlier.  Shitty now uses `cell.drawn` for the
same distinction at every hard-line endpoint.  The older Contour adaptation
was corrected from its private `Grid::extractText` padding to current
`Terminal` clipboard behavior; its raw pointer endpoint remains separately
asserted.

The [VT510 DECAWM definition](https://vt100.net/docs/vt510-rm/DECAWM.html)
votes for continuing graphic input at the right margin.  ECMA-48 fifth edition
sections 8.3.15 and 8.3.74 vote for the CR and LF effects used to create hard
lines.  Neither document defines a clipboard, scrollback, COW or host
coordinate conversion.

All 21 public tests pass with both Ragel parser backends.

### Audited revisions for LineBuffer cases 1 through 20

| implementation | relevant source | revision |
| --- | --- | --- |
| Alacritty | `alacritty_terminal/src/grid/{mod,resize,storage}.rs`, `term/mod.rs` | `1b2b36a64e88` |
| Ghostty | `src/terminal/{PageList,Screen}.zig`, `src/terminal/formatter.zig`, `src/Surface.zig` | `426386b8579d` |
| Kitty | `kitty/{history,resize,screen}.c`, `kitty/window.py` | `2caa3ca16bc9` |
| xterm | `screen.c`, `button.c` | `6380a3eaed85` |
| Contour | `src/vtbackend/{Grid,Terminal}.{cpp,hpp}` | `c51e15ed254e` |
| iTerm2 | `ModernTests/LineBufferTests.swift`, `sources/LineBuffer/LineBuffer.{m,h}`, `sources/Selection/SelectionExtraction.swift` | `3ec57866cd9b` |
| VTE | `src/{ring.cc,vte.cc}` | `3d55bbdddb87` |
| foot | `grid.c`, `selection.c`, `extract.c` | `a635e0a196d9` |

## LineBuffer cases 21 through 40

The next 20 active methods in `ModernTests/LineBufferTests.swift`, from
`testRoundTrip_softEOL` through
`testBlockContaining_threeBlocks_atInnerBoundary_nextBlockShorter`, extend
the same source-order inventory in `tests/test_iterm2_line_buffer.py`.  The
tuple now contains the exact first 40 of 75 active methods, without duplicates.

The source batch contains four separate contracts; they were not collapsed
into one generic "resize works" test:

- cases 21 and 22 preserve a soft-wrapped logical line and two distinct empty
  hard lines;
- cases 23 through 25 keep coordinates usable across two and three mutation
  epochs, including the one-column layout;
- cases 26 and 27 map the cursor's content cell in both directions between
  widths 20 and 4;
- cases 28 through 32 cover the last-line natural/right boundary, the origin,
  an exactly full hard row, and every occupied cell of a mixed hard/soft/empty
  buffer;
- case 33 retains iTerm2's past-EOL cross-width collapse as an executable
  policy XFAIL rather than silently substituting Shitty's different anchor
  semantics;
- cases 34 and 35 keep a live position through later appends and map the first
  and last public boundaries;
- cases 36 through 40 expose the observable consequence of iTerm2's private
  fast/slow block lookup: a middle position and an exact hard-line endpoint
  stay attached to the same line across one, two and three storage epochs,
  including when the following line is shorter.

`forceSeal`, `firstBlockContainingPosition`, its fast/slow implementations,
and the returned block index/remainder are private iTerm2 storage APIs.  The
port therefore does not invent those APIs.  It does retain each source method
as its own executable public scenario and checks the line endpoint and selected
text that those private results serve.

### Consensus audit

Soft wrap and hard-line separation are universal.  Alacritty's `WRAPLINE`,
Ghostty's `wrap_continuation`, Kitty's `next_char_was_wrapped`, xterm's wrapped
line flag, Contour's `Line::wrapped`, iTerm2's `EOL_SOFT`, VTE's
`soft_wrapped`, and foot's negated `linebreak` all distinguish a continuation
from a hard boundary.  ECMA-48 fifth edition sections 8.3.15 and 8.3.74 define
CR and LF, while the VT510 DECAWM definition specifies continuation after the
right margin.  Those cases are therefore 9:0.

Column resize has to be split by capability:

| behavior | Alacritty | Ghostty | Kitty | xterm | Contour | iTerm2 | VTE | foot | standard |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Reflow logical content without joining hard lines | yes | yes | yes | abstain: physical resize | yes | yes | yes | yes | abstain |
| Map a cursor attached to a content cell across widths | yes | yes | yes | abstain | abstain: shrink path returns the old cursor (`TODO`) | yes | yes | yes | abstain |
| Preserve a past-EOL blank offset/pin instead of collapsing it to content end | yes | yes | yes | abstain | abstain | **no: collapses** | yes | yes | abstain |

Content reflow is 7:0.  Content-cell cursor mapping is 6:0, with xterm and
Contour abstaining because they do not provide that operation.  Past-EOL
mapping is a genuine 5:1 policy split: Alacritty treats blanks before the
cursor as explicit input space during reflow; Ghostty grows the reflow span to
the cursor pin; Kitty maps tracked cursors even when `x >= src_x_limit`; VTE
round-trips `eol_cells`; and foot extends `col_count` through its last tracking
point.  iTerm2 alone stores `extendsToEndOfLine` and, with `extendsRight=false`,
reinterprets it as the new natural content end.  Shitty follows the five-vote
blank-anchor policy, so the exact iTerm2 assertion remains XFAIL.  This is not
a product defect inferred from a single imported test.

All eight implementations preserve prior hard lines when later lines are
appended and expose selections whose endpoint may be at a hard-line content
boundary.  They therefore vote 8:0 for the public results of cases 23, 24 and
34 through 40.  Only iTerm2 exposes the exact backing-block identity and
fast/slow lookup predicate; the other seven and the wire standards abstain on
that private topology.

Both Ragel parser backends run 41 public tests: 40 pass and the iTerm2-only
past-EOL collapse is one expected failure.

### Audited revisions for LineBuffer cases 21 through 40

| implementation | relevant source | revision |
| --- | --- | --- |
| Alacritty | `alacritty_terminal/src/grid/{resize,tests}.rs`, `term/mod.rs` | `1b2b36a64e88` |
| Ghostty | `src/terminal/{PageList,Screen}.zig` | `8c9fd7aa79c4` |
| Kitty | `kitty/{resize,screen,history}.c` | `2caa3ca16bc9` |
| xterm | `screen.c`, `button.c` | `6380a3eaed85` |
| Contour | `src/vtbackend/{Grid,Terminal}.cpp` | `c51e15ed254e` |
| iTerm2 | `ModernTests/LineBufferTests.swift`, `sources/LineBuffer/LineBuffer.{m,h}` | `3ec57866cd9b` |
| VTE | `src/{ring.cc,vte.cc}`, `doc/rewrap.txt` | `3d55bbdddb87` |
| foot | `grid.c`, `selection.c` | `a635e0a196d9` |

## LineBuffer cases 41 through 60

The next 20 active methods in `ModernTests/LineBufferTests.swift`, from
`testBlockContaining_threeBlocks_middleOfMiddleBlock` through
`testNumberOfRawLinesInRange_VeryLongRawLines`, extend the source-order
inventory to the exact first 60 of 75 active methods.  The executable suite
has 61 tests: the inventory plus one public scenario for every source method.

Cases 41 through 49 finish the block-boundary family without exporting
iTerm2's backing-block topology.  They separately cover a middle position,
the origin, zero and positive residual row offsets, a trailing empty line, an
all-empty later storage epoch, the complete line before an inner boundary,
and an empty line between two non-empty epochs.  The public observations are
exact selection coordinates and text.  `forceSeal`, block indexes,
`absolutePosition`, and `yOffset` remain iTerm2 implementation details; no
source case is dropped merely because Shitty reaches the same result through
physical rows instead of blocks.

Case 50 is not translated into a weaker "third row contains xthird" check.
It requires forward terminal-buffer search starting at the boundary after
`second`, with the match at column 0 of row 2.  Shitty has no buffer-search
operation, so the desired `SEARCH_NEXT` transaction and selected match remain
an executable expected failure.  This records a real missing feature rather
than treating the absence of a matching private API as permission to skip it.

Case 51 retains the exact right-prompt regression: a cursor at `(70, 1)`, one
cell after the 70-byte prompt, remains there when the page shrinks from 133 to
132 columns.  The prompt and the other two hard rows remain unchanged.

Cases 52 through 60 expose `numberOfUnwrappedLines` through the wrap metadata
that the renderer and clipboard already consume.  For a non-empty range, the
count is one plus every hard boundary between adjacent physical rows; the
empty range is zero.  The tests preserve all source datasets and range
boundaries, including empty hard lines, a range beginning or ending inside a
logical line, a soft-EOL continuation, Unicode width-two cells, and a
500-character logical line.  The Japanese case strengthens upstream's
`>= 1` smoke assertion to the exact two intersected logical lines and verifies
the real lead/continuation cell pair.

### Consensus audit

The private lookup predicate still has one implementation and seven
abstentions.  Its observable result does not:

| implementation | hard/soft boundary evidence | public position result |
| --- | --- | --- |
| Alacritty | `Flags::WRAPLINE`; line search and selection walk across it. | Origin, middle cells, empty rows and hard endpoints remain distinct. |
| Ghostty | `Row.wrap_continuation`; tracked pins survive page boundaries. | The same public coordinates remain addressable across pages. |
| Kitty | `next_char_was_wrapped`; history exposes `is_continued`. | Selection distinguishes hard lines, continuations and empty rows. |
| xterm | `LINEWRAPPED`; `firstRowOfLine`/`lastRowOfLine` and selection use it. | Physical storage boundaries do not alter selection coordinates. |
| Contour | `Line::wrapped()` and `Grid::isLineWrapped`. | `CellLocation` and selection preserve hard/empty row boundaries. |
| iTerm2 | `EOL_SOFT`, `LineBufferPosition`, and the tested block lookup. | Supplies both the private predicate and public expected results. |
| VTE | ring row `soft_wrapped`. | Selection and text extraction keep hard boundaries and empty rows. |
| foot | `row.linebreak` (false means continuation). | Selection/extraction traverse the same logical boundaries. |

Thus the public results of cases 41 through 49 are 8:0.  Only iTerm2 votes on
the exact block index/remainder.  ECMA-48 fifth edition sections 8.3.15 and
8.3.74 plus the VT510 DECAWM definition vote for the CR/LF and autowrap
boundaries, but abstain on host storage blocks and selections.

Search was audited as a capability rather than assumed to be iTerm2-only:

| implementation | relevant operation | vote |
| --- | --- | --- |
| Alacritty | `Term::search_next(regex, origin, direction, ...)` | yes |
| Ghostty | `PageListSearch`/`ViewportSearch`, initialized from a tracked page position | yes |
| Kitty | `search_scrollback`, exporting wrap-marked history to the configured pager at `INPUT_LINE_NUMBER` | yes, different host implementation |
| xterm | no scrollback text-search operation | abstain |
| Contour | `Terminal::search(CellLocation)` and `searchNextMatch` | yes |
| iTerm2 | `prepareToSearchFor` plus `findSubstring` from `LineBufferPosition` | yes |
| VTE | `search_set_regex` and directional `search_find` over ring rows | yes |
| foot | `find_next` over an explicit start/end coordinate range | yes |
| ECMA-48 / VT510 | no host scrollback-search facility | abstain |

The result is 7:0, not 1:0: Kitty's pager is counted because a different
implementation boundary is not a missing feature.  Shitty's absent operation
therefore remains one explicit XFAIL.

All eight implementations retain a cursor that is still inside the new page
when a host resize removes one unused rightmost column, so case 51 is 8:0;
wire standards abstain on host resize.  Logical-line range counting is built
from the following independently audited boundary representations:

| implementation | range/count evidence | vote |
| --- | --- | --- |
| Alacritty | `WRAPLINE` drives reflow, text extraction and search line expansion. | Count a new logical line only after an unwrapped row. |
| Ghostty | `PageList` explicitly counts `wrap_continuation` rows during reflow. | Same hard/soft partition, including empty rows. |
| Kitty | `historybuf_is_line_continued` and `as_text` insert newline only when continuation is false. | Same partition. |
| xterm | `LINEWRAPPED` joins physical rows for selection and text. | Same partition; no host count method. |
| Contour | `Line::wrapped` builds logical lines for search/hints and extraction. | Same partition. |
| iTerm2 | `numberOfUnwrappedLines(in:width:)` is the exact source operation. | Exact source vote. |
| VTE | `Ring::is_soft_wrapped` governs paragraph rewrap and inserted newlines. | Same partition. |
| foot | `linebreak` governs reflow and newline insertion in `extract.c`. | Same partition. |

That partition is 8:0.  ECMA-48/VT510 supplies the hard-control and autowrap
boundaries but abstains on a host range-count API.  Unicode Standard Annex #11
revision 44 (Unicode 17.0.0) classifies the Han characters used by case 58 as
wide; all eight allocate the corresponding two-cell glyph and agree on the two
logical lines intersected by the first three physical rows.

Both Ragel parser backends run 61 public tests: 59 pass and the two documented
policy/capability gaps are expected failures.

### Audited revisions for LineBuffer cases 41 through 60

| implementation | relevant source | revision |
| --- | --- | --- |
| Alacritty | `alacritty_terminal/src/{grid/resize.rs,term/{cell.rs,search.rs}}` | `1b2b36a64e88` |
| Ghostty | `src/terminal/{PageList.zig,Terminal.zig,search/{pagelist,viewport}.zig}` | `b0b9fbc8d5b0` |
| Kitty | `kitty/{history,line,screen}.c`, `kitty/{window,boss}.py` | `2caa3ca16bc9` |
| xterm | `charproc.c`, `button.c`, `ptyx.h`, `xterm.h` | `6380a3eaed85` |
| Contour | `src/vtbackend/{Line.hpp,Grid.cpp,Terminal.cpp}` | `c51e15ed254e` |
| iTerm2 | `ModernTests/LineBufferTests.swift`, `sources/LineBuffer/LineBuffer.{m,h}` | `3ec57866cd9b` |
| VTE | `src/{ring.cc,ring.hh,vte.cc}` | `3d55bbdddb87` |
| foot | `grid.c`, `terminal.c`, `selection.c`, `extract.c`, `search.c` | `a635e0a196d9` |

## LineBuffer cases 61 through 75

The final fifteen active methods in `ModernTests/LineBufferTests.swift` are
represented in source order by `tests/test_iterm2_line_buffer.py`; the fixed
inventory now contains all 75 methods.  Cases 61 through 63 exercise the same
public hard/soft-line partition through ranges spanning many ring positions,
both used-buffer ends, and logical lines of four different lengths.  They pass
without adding an iTerm2 `LineBlock` abstraction.

Cases 64 through 73 retain every forward and backward multiline-search
contract: one storage extent, two extents, three extents, a partial first line,
the last repeated match, and a final line without a hard EOL.  Cases 74 and 75
retain iTerm2's asymmetric `stopAt` boundary: forward search excludes a match
starting exactly at the stop position, while backward search includes it.  The
fixtures and expected coordinates are executable, but Shitty has no host
terminal-buffer search operation, so these twelve cases are expected failures.
The older case 50 now uses the same absent host-operation boundary instead of
inventing a `SEARCH_NEXT` command in the test protocol.

The implementation audit separates generic terminal search from this narrower
hard-EOL multiline operation:

| implementation | hard-line search model | multiline vote |
| --- | --- | --- |
| Alacritty | `regex_search_internal` resets its DFA at every non-wrapped row boundary instead of feeding a newline. | abstain |
| Ghostty | `SlidingWindow` inserts hard newlines, and its page-boundary tests explicitly reject a match crossing one; no multiline-query mode exists. | abstain |
| Kitty | `search_scrollback` delegates per-line regex search to the configured pager; selected newlines are input to that pager, not a terminal multiline matcher. | abstain |
| xterm | no scrollback text-search operation. | abstain |
| Contour | `LogicalLine::search` crosses only the physical rows belonging to one soft-wrapped logical line. | abstain |
| iTerm2 | `FindContext` with `optMultiLine` crosses sealed `LineBlock` boundaries in both directions and accepts an explicit `stopAt`. | yes |
| VTE | `search_rows_iter` invokes PCRE separately for each hard-EOL-delimited extended line. | abstain |
| foot | the search editor removes extracted newlines, and `find_next` has no hard-EOL token in its query model. | abstain |

Generic terminal-buffer search remains supported 7:0 as documented above, but
the exact hard-EOL multiline and explicit `stopAt` contracts are 1:0 because
the other six search implementations do not expose those operations and xterm
has no search operation at all.  ECMA-48, VT510 and Unicode standards define
neither host search nor its interval endpoints and abstain.  The cases remain
explicit capability XFAILs rather than being weakened to assertions about text
already present in a snapshot.

Both Ragel parser backends run 76 public tests: 62 pass, thirteen search gaps
and the iTerm2-only past-EOL policy are expected failures.

## Grid absolute-range subtraction cases 1 through 25

The first 25 of 32 active methods in
`ModernTests/VT100GridAbsCoordRangeSubtractionTests.swift` are represented in
`tests/test_iterm2_grid_range.py`.  They cover an empty outer range, an invalid
outer range, an outer range without exclusions, an empty exclusion, and a
middle exclusion producing two same-row pieces.  The public adaptation uses
the operation that consumes this arithmetic in iTerm2: select the current OSC
133 command input while excluding PS2 and right-prompt cells.  In particular,
the fifth case requires `left`, right-prompt `RP`, and `right` to copy as
`leftright` without an inserted newline.

Cases 6 through 25 preserve the source coordinates exactly.  Positioned CUP
and OSC 133 marks install the command outer range and each excluded range after
the visible cells have been written, so the tests can retain exclusions before,
after, adjacent to, and straddling the command boundaries.  The same mechanism
retains two disjoint exclusions, deliberately unsorted input, adjacent ranges,
overlap, nesting in both orders, three duplicates, a three-range covering
union, a complete excluded middle row, and an exclusion crossing from column 5
of one row to column 5 of the next.  Expected clipboard bytes are built from
the surviving pieces in row-major order.  Same-row pieces reconnect without a
newline; pieces separated across rows do not.

iTerm2 implements this with row-major half-open
`VT100GridAbsCoordRange.subtracting(_:)` and wraps the resulting disjoint pieces
as connected subselections.  Ghostty retains prompt/input/output zones and can
highlight one contiguous input region; Kitty, Contour and foot expose command-
output actions; VTE retains semantic zones.  None of those five exposes a
select-current-command action with excluded PS2/right-prompt holes.  Alacritty
and xterm do not implement the semantic-prompt model.  They therefore all
abstain on the exact operation rather than voting for a different endpoint
representation.  The Semantic Prompts specification defines prompt, input and
output boundaries but not a GUI selection or exclusion policy, so the standards
vote also abstains.

Shitty parses OSC 133 `P` kinds and stores prompt/input cell semantics, but its
nearest public selection action still selects one complete logical line.  All
25 adaptations are therefore executable expected failures: they expose the
missing current-command action and the missing normalized disjoint exclusion
model without adding a dead range API solely for the tests.  Both Ragel parser
backends run 26 public tests for this source group: the inventory passes and all
25 behavior cases retain the capability gap.

The audited revisions for both groups are Alacritty `1b2b36a64e88`, Ghostty
`b0b9fbc8d5b0`, Kitty `2caa3ca16bc9`, xterm `6380a3eaed85`, Contour
`c51e15ed254e`, iTerm2 `3ec57866cd9b`, VTE `3d55bbdddb87`, and foot
`a635e0a196d9`.

# Alacritty reference fixtures

Source: https://github.com/alacritty/alacritty

Revision: `852e971cddfabe222d2d5bcda466e130f53af207`

Imported path: `alacritty_terminal/tests/ref`.

All 45 recording directories are unmodified. `adapter.py`, `file_names.txt`,
and `xfail.txt` are Shitty integration files. The upstream Apache-2.0 and MIT
licenses are preserved alongside the fixtures.

## terminal selection.rs

All 16 unit tests in `alacritty_terminal/src/selection.rs` at audited Alacritty
revision `1b2b36a64e88` are represented in source order by
`tests/test_alacritty_selection.py`.  The adapters exercise the real pointer,
selection extraction, snapping, scroll-region, and cell-write paths instead
of copying Alacritty's private `SelectionRange` structure.

Cases 1 through 6 and 11 through 12 define endpoints at left or right halves
of cells.  Alacritty, Ghostty, Kitty, xterm, and VTE all retain half-cell mouse
coordinates and resolve only completely covered cells.  Contour, iTerm2, and
foot use cell-level endpoints for these paths.  No terminal protocol standard
defines GUI selection, so the specification vote abstains and half-cell
geometry wins 5:3.  Shitty currently converts the pointer to a cell before the
selection component sees it; all eight affected source cases are executable
XFAILs, including the two empty-selection predicates.

Cases 7 through 10 cover line, semantic-word, simple, and block selections
while the full screen scrolls.  All eight audited implementations track a
completed selection with the moving content, using row offsets, absolute
coordinates, or tracked page pins.  The adapters verify both geometry and the
extracted bytes before and after the source's four-row rotation; all four pass.

Cases 13 through 15 move only a vertical scrolling region.  Alacritty,
Ghostty, xterm, and iTerm2 preserve the surviving range and clip the end that
leaves the region.  Kitty and foot clear a partially displaced selection;
Contour and VTE retain coordinate-based selection while the region's content
moves rather than producing Alacritty's clipped range.  With no specification
vote, clipping is the largest exact behavior group at 4:2:2.  It is retained
for linear scroll-up, linear scroll-down, and rectangular scroll-up as
executable XFAILs because Shitty currently clears all three.  Column expansion
is intentionally disabled for the block case exactly as in the source.

Case 16 maps Alacritty's private range-intersection helper to observable cell
writes: changing a row outside the selection preserves it, while changing an
intersecting row invalidates it.  Alacritty and the majority of the audited
selection implementations apply this stale-selection rule; Ghostty and
Contour keep tracked geometry across the rewrite.  The public adaptation
passes.

Both Ragel backends run 17 public tests: six pass and eleven documented
consensus-feature gaps are expected failures.  The other audited revisions are
Ghostty `b0b9fbc8d5b0`, Kitty `2caa3ca16bc9`, xterm `6380a3eaed85`, Contour
`c51e15ed254e`, iTerm2 `3ec57866cd9b`, VTE `3d55bbdddb87`, and foot
`a635e0a196d9`.

## terminal core term/mod.rs

All 23 unit tests in `alacritty_terminal/src/term/mod.rs` at Alacritty revision
`1b2b36a64e88` are represented in source order by
`tests/test_alacritty_terminal_core.py`.  Together with the inventory guard,
both parser backends run 24 public tests.  The adapters use only terminal input,
resize, scrolling, selection, title, and presentation observations; they do not
expose Alacritty's private grid, vi-cursor, damage, or version-parser types in
Shitty's API.

The two page-scroll cases retain the source invariant that page movement is
monotonic and saturates at the history boundaries.  Exact page increments are
frontend policy rather than terminal protocol: Alacritty moves ten rows while
Shitty moves five, and the audited implementations use different viewport and
configured increments.  The tests therefore drive repeated page operations to
the same top and bottom boundaries instead of treating Alacritty's increment as
a standard.

The four selection cases preserve Alacritty's extracted bytes.  Simple and
semantic selection pass.  A full-line selection includes its final hard line
break in Alacritty, xterm's default `cutNewline` mode, Contour, VTE, and foot;
Ghostty and Kitty trim it and iTerm2 defaults `CopyLastNewline` off.  With no
protocol specification for GUI selection, the 5:3 behavior is an executable
XFAIL in Shitty.  Rectangular extraction keeps physical row separators across
a soft wrap in Alacritty, Ghostty, Kitty, xterm's rectangular-selection build,
iTerm2, VTE, and foot; Contour and Shitty currently join that boundary.  The
source's three rectangle checks remain one executable XFAIL after its first two
passing checks, preserving the 7:1 consensus contract.

Alacritty's private serde round-trip has no portable wire format for the other
implementations or a specification to vote on.  Its public adaptation verifies
the corresponding lossless state invariant by switching to and from the
alternate screen.  DEC special graphics is exercised through designation and
input, and the clear-history/view cases use CSI 2 J and xterm's CSI 3 J through
their public effects.  The three vi-cursor-only assertions are represented by
the same externally observable history/view-anchor invariants using a
selection, since Shitty has no vi cursor and no test-only vi API is introduced.

Growing and shrinking the active screen and growing the inactive primary
screen pass with the source cursor/history results.  When the alternate screen
is active, Alacritty, Ghostty, Contour, iTerm2, VTE, and foot preserve the
cursor-anchored blank primary rows as the height shrinks, producing fifteen
history rows in this fixture.  xterm simply reallocates the inactive buffer,
and Kitty's rewrap drops trailing empty viewport rows.  Shitty follows the
latter result and keeps ten history rows, so Alacritty's 6:2 inactive-shrink
contract is retained as an executable XFAIL.

The three damage tests describe Alacritty's renderer-private damage topology,
for which the other renderers and terminal standards have no common data
model.  Their adapters verify the portable boundary instead: visible writes,
cursor and erase operations, whole-screen operations, viewport movement, and
resize schedule presentations while leaving the public model correct.  The
title case exercises OSC 2 and CSI 22/23 t push/pop plus RIS without asserting
Alacritty's implementation-specific stack limit.  The private Cargo version
parser is tested through its real protocol consumer, secondary DA, whose three
stable numeric fields are implementation-defined but publicly observable.

Both Ragel backends pass 21 tests and report three documented consensus-feature
gaps as expected failures.  The other audited revisions are Ghostty
`b0b9fbc8d5b0`, Kitty `2caa3ca16bc9`, xterm `6380a3eaed85`, Contour
`c51e15ed254e`, iTerm2 `3ec57866cd9b`, VTE `3d55bbdddb87`, and foot
`a635e0a196d9`.

## grid and storage

All 14 tests in `alacritty_terminal/src/grid/storage.rs` and all 12 tests in
`alacritty_terminal/src/grid/tests.rs` at Alacritty revision `1b2b36a64e88`
are represented in source order by `tests/test_alacritty_grid_storage.py`.
Together with the inventory guard, both parser backends run 27 public tests.

`Storage::inner`, `zero`, `len`, `MAX_CACHE_SIZE`, its debug indexing panic,
and the allocation cache are Alacritty-private topology.  The other terminal
implementations and terminal standards have no corresponding ABI to vote on.
Each source case is nevertheless kept as a separate executable scenario for
the public invariant its storage transition exists to provide: exact initial
page geometry, logical row order across ring rotation, bounded history access,
growth before and after rotation, shrinking at the content prefix or unused
tail, recycling inside a scrolling region, clearing invisible history,
shrink/grow reuse, later initialization, and repeated wraparound at capacity.
The eight audited implementations use rings, line buffers, page lists, or
trees, but all preserve these observable ordering and bounds invariants.

The three grid scrolling cases are driven through CSI Ps S (SU) and CSI Ps T
(SD).  Alacritty, Ghostty, Kitty, xterm, Contour, iTerm2, VTE, and foot all
implement both operations.  ECMA-48 sections 8.3.147 and 8.3.113 provide the
ninth vote: rows move inside the scrolling region and vacated rows are erased.
Ordinary SD does not pull rows from scrollback; Kitty's history-filling reverse
scroll is a distinct extension rather than the default SD path.  The tests
cover both directions, cleared rows, a bounded region with untouched edges,
and SD with populated history.

The iterator and size-hint source tests describe Rust iterator internals, so
their public adapters verify the corresponding complete row-major model and
the exact `rows * columns` snapshot cardinality at every coordinate.  No
product iterator or storage API was added for the tests.

The seven reflow cases retain Alacritty's exact logical-line results, including
direct versus two-step shrinking, an interior empty cell, and single- or
multi-row unwrapping.  Primary-screen resize reflow is implemented by
Alacritty, Ghostty, Kitty, Contour, iTerm2, VTE, and foot; xterm does not offer
this reflow and therefore abstains.  Window resize reflow is not defined by a
terminal control standard, so the specification vote also abstains.  All seven
supporting implementations preserve interior cells and logical wrap order.

Alacritty's two `resize(false, ...)` cases are exposed through the alternate
screen rather than through a test-only reflow switch.  Alacritty, Ghostty,
Kitty, xterm, Contour, VTE, and foot keep alternate-screen physical rows
separate and truncate them on width shrink; iTerm2 deliberately reflows its
alternate grid.  With the specification abstaining, the no-reflow behavior
wins 7:1 and both grow and shrink scenarios pass in Shitty.

Both Ragel backends pass all 27 public tests.  The other audited revisions are
Ghostty `b0b9fbc8d5b0`, Kitty `2caa3ca16bc9`, xterm `6380a3eaed85`, Contour
`c51e15ed254e`, iTerm2 `3ec57866cd9b`, VTE `3d55bbdddb87`, and foot
`a635e0a196d9`.

## index.rs and term/cell.rs

All 11 tests in `alacritty_terminal/src/index.rs` and all three tests in
`alacritty_terminal/src/term/cell.rs` at Alacritty revision
`1b2b36a64e88` are represented one-for-one by
`tests/test_alacritty_index_cell.py`.  With the inventory guard, both parser
backends run and pass 15 public tests.

`Point::{add,sub}` and its `Cursor`, `Grid`, and `None` boundaries are private
coordinate arithmetic rather than a terminal protocol object.  Their public
adapters keep the distinct consequences separate: ordinary CUB/CUF movement,
page-edge clamping, reverse movement across a real soft-wrap, forward
autowrap, pointer endpoints clamped to the page, and complete row-major
selection in both directions.  No point-arithmetic hook was added to Shitty.
All eight audited implementations order presentation positions by row and
then column and clamp cursor and pointer operations at their applicable page
boundary.  ECMA-48 sections 8.3.18 and 8.3.20 independently require CUB and
CUF to stop at the page boundary.  ECMA-48 does not define GUI selection or
host-side grid iteration and abstains on those adaptations.

The predecessor-row case is driven through DEC reverse-wrap mode 45 and a
real soft-wrapped line.  Xterm, Ghostty, Contour, iTerm2, and foot implement
that operation and return to the logical predecessor; Alacritty, Kitty, and
VTE do not execute mode 45 and abstain.  Forward wrapping is ordinary DECAWM
behavior supported by all eight implementations and defined by the DEC
terminal model.

Alacritty's 24-byte `Cell` cap is a Rust layout guard with no portable ABI and
no implementation or terminal-standard vote.  Its separate executable
adapter verifies the reason for the compact cell: one stored cell retains its
character and every public rendition carried by Shitty, including direct
foreground, background, and underline colors.  The two `line_length` cases
use real selection extraction.  A hard row ends after its last drawn cell,
while a soft-wrap makes the complete physical row part of the logical line;
all eight implementations preserve this distinction.  Terminal standards do
not define clipboard extraction and therefore abstain on its exact bytes.

The audited revisions are Ghostty `b0b9fbc8d5b0`, Kitty `2caa3ca16bc9`,
xterm `6380a3eaed85`, Contour `c51e15ed254e`, iTerm2 `3ec57866cd9b`, VTE
`3d55bbdddb87`, and foot `a635e0a196d9`.  No production change was made.

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

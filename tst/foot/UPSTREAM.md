# Foot unit-test adaptations

Source: https://codeberg.org/dnkl/foot

Revision: `a635e0a196d9`.

The adapters use Foot's public terminal effects rather than copying its private
row-range vectors, Wayland keyboard objects, allocator behavior, or sixel
lists into Shitty's API.  A source assertion is counted only when it has its
own executable scenario.

## URI ranges and scrollback lifetime

All 17 source cases from the two `UNITTEST` blocks in `grid.c` and the five
independent cases in the `term_erase_scrollback` block in `terminal.c` are
represented by `tst/test_foot_grid_lifetime.py`.  With the inventory guard,
both parser backends run and pass 18 public tests.

The first seven cases write, replace, join, and split a four-cell OSC 8 run.
The next five erase no run, two complete runs, the adjoining ends of two runs,
and the interior of one run; the final split repeats Foot's realloc regression
and then performs another observable edit.  The tests assert every cell's URI,
not Foot's private range count or vector address.

Alacritty stores an OSC 8 target in `CellExtra`, Ghostty and Kitty store a
hyperlink id in each cell, Contour stores a parallel per-cell hyperlink array,
iTerm2 uses per-cell external attributes, VTE uses a cell attribute index, and
Foot coalesces per-row ranges.  All seven attach the active target to newly
written cells and remove or replace it only on the overwritten/erased cells;
their different storage forms do not make the feature inapplicable.  Xterm has
no OSC 8 implementation and abstains.  The terminal-wg OSC hyperlink
specification defines the active link and its empty-URI terminator, but does
not prescribe range coalescing, so it votes for the cell association and
abstains on the private representation:
https://gitlab.freedesktop.org/terminal-wg/specifications/-/blob/master/proposals/osc-hyperlinks.md

The remaining five cases drive `CSI 3 J` after real scrollback, selection, and
sixel creation.  Alacritty, Ghostty, xterm, Contour, iTerm2, VTE, and Foot erase
saved rows while retaining the active page.  Kitty deliberately defines ED 3
as erasing both the display and scrollback, including graphics and selections.
The official xterm control-sequence definition says parameter 3 is “Erase
Saved Lines”, so page retention wins eight votes to one including the
specification.  Shitty follows that contract.  The exact GUI-selection
lifetime is not a terminal protocol object: Alacritty and Foot explicitly
discard a selection intersecting deleted history while retaining a page-only
selection, Ghostty rebases tracked pins, and Contour and Kitty clear more
aggressively; xterm, iTerm2, and VTE maintain it in their frontend/view layers.
The standard therefore abstains on selection bookkeeping.

Xterm, Contour, iTerm2, VTE, and Foot implement sixel images.  Their image
placements follow the rows they occupy: dropping saved rows releases images
that intersect those rows and does not release an image wholly on the retained
page.  Alacritty, Ghostty, and Kitty do not implement sixel and abstain.  ED 3
does not standardize renderer ownership, so the standard also abstains on that
lifetime detail.  The public adaptations verify both destruction and retention
through the rendered pixels rather than exposing an image list.

## Legacy and Kitty keyboard vectors

All 12 independent expectations in `input.c` are represented by
`tst/test_foot_keyboard.py`: the `ISO_Left_Tab` lookup, both Alt-Return
modifyOtherKeys levels, seven Swedish-layout vectors, the `de(neo)` base-layout
vector, and the `us(intl)` compose regression.  With the inventory guard, both
parser backends run and pass 13 public tests.

Alacritty, Ghostty, Kitty, Contour, iTerm2, and Foot implement the Kitty
keyboard protocol, including its alternate shifted/base-layout fields.  Their
frontend APIs obtain the fields differently, but all six encode the same
Unicode key, shifted key, PC-101 base-layout key, modifier value, and functional
key forms used by these cases.  Xterm and VTE do not implement the Kitty
progressive-enhancement flags and abstain.  The Kitty keyboard specification
is the seventh supporting vote; it explicitly defines the alternate fields,
the empty shifted subfield before a base-layout key, and `1 + modifier bits`:
https://sw.kovidgoyal.net/kitty/keyboard-protocol/

The legacy `Ctrl+Shift+ISO_Left_Tab` expectation is the one losing Foot policy.
Foot and Ghostty emit `CSI 27;6;9~` even in their ordinary legacy path.
Contour's generator, iTerm2's standard mapper, VTE's `ISO_Left_Tab` map, and
xterm's default mode emit backtab `CSI Z`; Shitty's platform boundary likewise
normalizes `Tab` and `ISO_Left_Tab` to one `InputKey::Tab`.  Alacritty has no
separate legacy mapping for this chord, while Kitty consumes it as a default
GUI tab action, so both abstain.  Xterm documents `CSI 27;6;9~` only after
`modifyOtherKeys` is enabled; its ordinary backtab behavior adds the standards
vote to the `CSI Z` majority.  The source case remains executable as the same
frontend chord, with the winning public result rather than an XFAIL for Foot's
minority default.

The two Alt-Return forms match Foot, Ghostty, xterm, Contour, iTerm2, and the
xterm modifyOtherKeys specification; Alacritty, Kitty, and VTE do not expose
that xterm mode and abstain.  No production code or test-only product API was
added.

## Parser collection and emoji variation metadata

The five assertions in Foot's `vt.c` `action_collect` unit and every entry
behind the `terminal.c` `emoji_vs` unit are represented by
`tst/test_foot_parser_unicode.py`.  The suite contains five separately
executable parser cases, one metadata inventory guard, and 371 separately
executable Unicode-base cases.  Both parser backends run and pass all 378
tests.

The parser unit is an implementation-capacity test rather than a terminal
control function.  The adapter sends real CSI streams containing one through
five intermediate bytes.  It observes the first four in order through the
existing parser trace, verifies that the unsupported fifth-byte sequence has
no terminal effect, and then proves recovery with a real `DECRQM` request and
reply.  It does not add an accessor for the parser's private byte array.

There is no cross-terminal consensus for the internal capacity.  Alacritty's
VTE parser stores two intermediates and marks longer input ignored; Ghostty
and Foot store four; Kitty has one primary and one secondary slot; xterm moves
into a control-specific state and sends unexpected following intermediates to
its ignore state; Contour appends them to a string; iTerm2's CSI dispatch key
uses one intermediate even though its DCS parser accumulates a string; and
VTE packs intermediates into its sequence key, with its generated sequence
model allowing four and named dispatch tables using at most two.  What all
eight implementations agree on is the public result used here: an unsupported
control is a null operation and the parser returns to ground for the next
character/control.  ECMA-48 section 5.4 explicitly leaves the number of
intermediate bytes unlimited, while noting that one is sufficient in
practice; it defines their syntax, not a storage ABI or an effect for an
unknown final combination:
https://ecma-international.org/publications-and-standards/standards/ecma-48/

Foot and Shitty both derive their variation metadata from Unicode 17's
`emoji-variation-sequences.txt`: 742 selector records for 371 distinct bases.
The inventory guard checks ordering, uniqueness, and the VS15/VS16 pair for
every base.  Each base is then written through the real terminal twice and
the adapter verifies that the selector remains in the grapheme and that the
result occupies the width selected by Shitty's generated Unicode metadata.
This is a public exercise of the generated table rather than a duplicate
private-table accessor.

The eight implementations split on how a selector arriving after the base
may revise an already allocated cell.  Ghostty, Kitty, xterm, and Foot narrow
a registered wide base for VS15 and widen a registered narrow base for VS16.
Contour widens for VS16 but deliberately never gives a cell back for VS15.
iTerm2 widens VS16 on its primary screen and keeps the base width for VS15.
Alacritty and VTE retain selectors for rendering but allocate width from the
base codepoint, so they do not implement late width adjustment.  The Terminal
Unicode Core specification supplies the standards vote: VS16 forces width 2,
whereas VS15 changes presentation without changing the underlying width:
https://github.com/contour-terminal/terminal-unicode-core

The Foot metadata unit itself asserts only sorted, non-overlapping records
with at least one selector flag; it does not establish Foot's unconditional
VS15 narrowing as an oracle.  The adapters therefore do not import that
private policy.  In particular, the eight registered bases whose text
presentation is itself full-width (`3030`, `303D`, `3297`, `3299`, `1F202`,
`1F21A`, `1F22F`, and `1F237`) remain two cells rather than being cropped to
one.  Every one of the 371 source bases is still executed; differing storage
and resize mechanisms are documented, not used as a reason to skip the
feature.

The implementation audit used freshly updated sources:

| implementation | revision |
| --- | --- |
| Alacritty | `1b2b36a64e88` |
| Ghostty | `fad7f854e8f9` |
| Kitty | `2caa3ca16bc9` |
| xterm | `6380a3eaed85` |
| Contour | `c51e15ed254e` |
| iTerm2 | `3ec57866cd9b` |
| VTE | `3d55bbdddb87` |
| Foot | `a635e0a196d9` |

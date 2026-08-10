# WezTerm model streams

The Rust sources in `upstream/` are copied verbatim from WezTerm revision
`76b606ec597a3c0263fa60321548637451c0a547` (2026-07-21). The graphics-only
`image.rs` is intentionally deferred with the rest of graphics testing. The
upstream project is MIT licensed; its terminal crate license is preserved as
`LICENSE.upstream`.

The stream catalog statically extracts every direct literal `term.print("...")` call
without compiling or executing WezTerm. Rust byte, Unicode, and continuation
escapes are decoded by the adapter. Each call site is an independent build
target and is compared whole versus bytewise across parser events and the full
observable terminal state. Variable-built streams and WezTerm's semantic
selection and resize assertions are handled by explicit adapters.

The screen catalog covers every executable `assert_visible_contents`
checkpoint. The often-quoted count of 74 includes the helper's own definition;
there are 73 call sites. Twenty-eight are extracted mechanically. The other 45
are explicit Python transactions for dynamic strings, resets, left/right
margins, selection side effects, and resize/reflow. The validator inventories
the Rust source by test method and rejects any missing call site, so an
untranslated operation can no longer silently discard later assertions.

Every checkpoint runs at its upstream geometry and compares visible text;
trailing default cells are normalized because Shitty stores a fixed-width grid
where WezTerm stores variable-length lines. Importing the complete set added
FinalTerm OSC 133 `L` fresh-line behavior, independently corroborated by
Ghostty and the semantic-prompts specification. All 73 currently agree without
an expected failure.

The selection catalog covers all 12 clipboard assertions. Five expectations
are deliberately adapted to the current terminal consensus: selection never
manufactures a trailing newline beyond the screen, explicitly written trailing
spaces are preserved, and soft-wrapped rows are copied as one logical line,
including triple-click line selection in both the live screen and scrollback.

The cursor catalog covers all 64 `assert_cursor_pos` call sites, including the
15 checkpoints that require explicit resize transactions. Position, visibility,
default shape, and the resize wrap-pending case are checked. WezTerm's
one-past-the-grid coordinate for oversized CUP/HVP/CHA is normalized to the
last physical cell, matching DEC/xterm behavior. Importing this catalog exposed
and fixed missing default tab stops when a customized tab table grows during
resize.

The damage catalog covers all 17 `assert_dirty_lines` call sites. We retain the
upstream stable-line oracle and explicitly translate it to the visible rows
sent to Shitty's renderer: WezTerm numbers physical history lines, while
Shitty's incremental renderer addresses the current viewport. Cursor-only
movement produces no cell damage; scrolling damages the visible coordinates
whose contents changed.

The history catalog covers all 27 `assert_all_contents` call sites and
inventories all 20 adjacent stable-row assertions. Shitty deliberately has no
stable RowId: it checks the equivalent ordered history contents, retained-row
count, and viewport origin. The history limit and every source snapshot now
match WezTerm exactly. The old, upstream-disabled selection module includes
fixed-width padding that current WezTerm model tests no longer use; only its
final implicit blank is normalized. The U+008D reverse-index input is
represented by its standard seven-bit `ESC M` form because C1 codepoints in
UTF-8 are text, not controls.

The semantic catalog covers all three semantic-zone snapshots and the one
cell-attribute assertion. It added OSC 133 `I`, whose input region ends at the
next line, and corrected OSC 133 `A` to start its prompt on a fresh line. The
latter behavior agrees with current WezTerm, Ghostty, and the semantic-prompts
specification.

The hyperlink catalog covers all three attribute assertions. It checks URI and
identity across explicit OSC 8 close/open, SGR reset, explicit identifiers,
and DECSTR. Importing it fixed soft reset to close the active OSC 8 hyperlink,
matching WezTerm, Kitty, and VTE.

The metadata catalog completes the remaining source assertions: two exact
cell-attribute/BCE oracles, four DEC double-width/double-height line-mode
checks, and the two Hangul NFC/grapheme assertions. All behavioral assertions
in the copied WezTerm test sources are now represented by an executable Shitty
adapter; Rust helper-internal checks are inventoried through their callers
rather than duplicated.

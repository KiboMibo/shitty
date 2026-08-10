# Ghostty minimized fuzz corpora

The files in `osc-cmin/`, `parser-cmin/`, and `stream-cmin/` are copied
verbatim from Ghostty revision `88b4cd047fa627cdca6781bc7e7dc8b75a2cecb9`.
They are the AFL++ edge-minimized corpora described in `README.upstream.md`.
See `LICENSE.upstream` (MIT).

The adapter preserves the upstream harness semantics: parser members are raw
VT input, stream members discard their first path-selector byte, and OSC
members use their first byte to select BEL, ST, or a missing terminator. Each
member is fed whole and across deterministic parser boundaries. The oracle
compares all externally observable streams and modes plus Shitty's rich model
state; full cell records remain available for exact mismatch diagnostics.

`upstream/stream_terminal_tests.zig` is the verbatim test section beginning at
line 907 of Ghostty's `src/terminal/stream_terminal.zig` at the same revision.
`semantic_catalog.py` extracts each test's literal `s.nextSlice()` calls as a
single logical stream while preserving the original call boundaries. Every
stream is independently compared as one write, at those upstream boundaries,
and bytewise. The five resize tests preceding those streams are accounted in
`test_ghostty_resize.py`: four execute against the product and the recoverable
allocator-failure case is inapplicable to libstd's fatal OOM contract. Dynamic
semantic prompt inputs and assertions from `Terminal.zig:13845-14320` are
ported in `test_ghostty_semantic_prompt.py`, including adjacent OSC 133 N/P
stream cases. The observable tail of `Terminal.zig:14321-EOF` is ported in
`test_ghostty_terminal_tail.py`: reset state, resize/reflow, DECCOLM,
alternate-screen modes, style preservation, and the wide-glyph delete-lines
regression. The four former skipped cases were audited again against current
Ghostty (`156bc8c8`), xterm (`6380a3ea`), VTE (`3d55bbdd`), Foot
(`a635e0a1`), Kitty (`fda3a9a2`), WezTerm (`e723cf50`), Alacritty
(`1b2b36a6`), and Windows Terminal (`b888cb7e`). Saved cursors now follow
reflowed content and normalize delayed wrap, matching Ghostty, Foot, WezTerm,
VTE, and Kitty. DECCOLM resets both margin pairs while preserving DECLRMM,
matching xterm, Ghostty, and VTE; Windows Terminal's mode reset is the
minority. Ghostty alone disables primary reflow with DECAWM, so the product
test retains the Alacritty/Kitty/Foot behavior instead of adopting that
upstream expectation.

Runtime cursor-default mutation has no corresponding public Shitty option,
tracked pins are Ghostty storage internals, status-display selection is not
implemented, and Ghostty's private glyph APC protocol is outside the terminal
protocol scope.

The first 20 accounted `Screen.zig` scenarios are executable in
`test_ghostty_screen.py`.  They cover the configured history limit, ordinary
and history-backed output, zero-history operation, complete and selective
erase, ED 3, rendition across cursor moves, and scrolling through both a
multi-row and a one-row viewport.  Ghostty's page/style-table assertions are
adapted to long public input streams and observable cell rendition; no page,
style-refcount, or cursor-copy hook is added to Shitty.

The source audit for this block used Alacritty `1b2b36a64e88`, Ghostty
`7e463bc65d43`, Kitty `0d3259f87d1c`, xterm `6380a3eaed85`, Contour
`c51e15ed254e`, iTerm2 `3ec57866cd9b`, VTE `3d55bbdddb87`, and foot
`a635e0a196d9`.  All eight keep the current rendition independently of cursor
movement and storage rotation and retain visible rows while ED 3 discards
history when they support that xterm extension.  xterm, Ghostty, Contour and
iTerm2 implement DECSCA plus protection-aware DECSED; Alacritty, Kitty, VTE
and foot do not implement that protection contract and abstain.  ECMA-48 is
the CR/LF, cursor-motion, SGR and ordinary ED reference; DEC's VT5xx control
reference supplies DECSCA/DECSED, and current xterm `ctlseqs` supplies ED 3.

The line-limit scenario exposed a Shitty storage-policy leak.  Alacritty,
Ghostty, Kitty (for this limit), xterm, Contour, iTerm2 and VTE cap retained
history at the configured logical line count.  Foot and Shitty rounded the
physical ring up to a power of two and exposed the spare slots as history.
Shitty now retains its power-of-two ring while keeping `saveLines` as a
separate logical eviction limit, so `-saveLines 123` retains exactly 123 rows.

The next 20 accounted `Screen.zig` scenarios are executable in
`test_ghostty_screen_selection.py`. They cover hyperlink lifetime and reuse,
line selection in live, wrapped and history rows, linear and rectangular
extraction, soft wraps, wide graphemes, trailing ZWJ and selection across
long-lived storage. Ghostty's page boundary is adapted to a long public
scrollback stream; no page hook is added.

Selecting only the continuation cell of a wide glyph exposed a Shitty defect:
the renderer used the whole-glyph snapped selection while clipboard extraction
went back to the raw rectangle and returned an empty string. Ghostty expands
the selection, Alacritty backs up from `WIDE_CHAR_SPACER`, xterm snaps clicks
and selection bounds away from `HIDDEN_CHAR`, Kitty expands extraction ranges
to multicell bounds, VTE resolves endpoints to character boundaries, foot
expands endpoints around `CELL_SPACER`, and iTerm2 tests clicking its
`DWC_RIGHT` placeholder. Contour guarantees that extraction emits a selected
wide glyph once but has no continuation-only contract and abstains. Shitty now
uses the already computed snapped rectangle for extraction as well as
painting. GUI selection has no terminal wire standard, so the standards vote
abstains.

Eight Ghostty assertions remain executable expected failures rather than
driving a policy change. Seven request `selectionString(..., .trim = true)`;
Shitty's public copy operation has no trim switch and preserves explicitly
selected spaces. The audited implementations do not agree on a default:
Ghostty exposes the call parameter, Kitty and xterm expose configuration,
iTerm2 has a preference, and the others distinguish written cells from screen
padding in different ways. The eighth expects selecting Ghostty's private
wide-prewrap header to copy the glyph from the next row. Only Ghostty and
Alacritty have an explicit equivalent header contract in the audited sources;
the other implementations abstain. The passing no-trim cases make Shitty's
actual public policy executable without pretending either disagreement is a
terminal-protocol rule.

The third 20-case `Screen.zig` block is executable in
`test_ghostty_screen_selection_semantic.py`. It covers select-all through the
equivalent public drag, complete logical-line iteration, line and word
selection across hard and soft boundaries, prompt/input/output boundaries,
and whole command-output selection. The source revisions audited for this
block are Alacritty `1b2b36a64e88`, Ghostty `7e463bc65d43`, Kitty
`0d3259f87d1c`, xterm `6380a3eaed85`, Contour `c51e15ed254e`, iTerm2
`3ec57866cd9b`, VTE `3d55bbdddb87`, and foot `a635e0a196d9`.

Two expected failures preserve Ghostty's default line-whitespace policy.
Ghostty trims leading and trailing whitespace and skips blank wrapped tails
unless its `whitespace` option is disabled. Kitty likewise removes blank cells,
including explicitly written spaces, from both ends before establishing its
line-selection range. Alacritty, xterm, Contour, iTerm2, VTE, and foot anchor
line selection at the complete physical or logical line start, even where
copy-time policy trims its unused tail. Shitty retains that six-to-two
behavior. ECMA-48 specifies automatic wrapping but no GUI selection policy and
abstains.

Seven semantic-selection scenarios are also expected failures, not discarded
cases. Ghostty splits line selection at per-cell prompt, input, and output
transitions and exposes `selectOutput`. Kitty independently implements
`screen_select_cmd_output`, Contour reconstructs `lastCommandBlock` from OSC
133 marks, and iTerm2 exposes prompt, command, and output ranges. They agree
that semantic command blocks are selectable, although they attach that
operation to different gestures and actions. Alacritty, xterm, VTE, and foot
have no corresponding semantic-block selection and abstain. Shitty records
OSC 133 semantics in cells but has neither semantic-aware line snapping nor a
command-output selection action, so the exact Ghostty scenarios remain
executable product gaps. The OSC 133 shell-integration convention defines the
marks, not a mandatory GUI gesture, and therefore abstains on the gesture.

Whitespace word selection across a soft wrap has no implementation consensus.
Ghostty, xterm, iTerm2, and foot group the contiguous whitespace run across
the logical line. Alacritty and VTE deliberately select one non-word cell;
Kitty declines initial word expansion for a non-word cell; Contour treats each
configured delimiter as its own word-wise selection. Shitty currently groups
whitespace only within one physical row. The four-to-four split and the lack
of a GUI-selection standard leave the Ghostty expectation as an executable
expected failure rather than a product change.

The remaining expected failure records a Ghostty-specific boundary quirk.
Clicking one of its configured word-boundary characters selects that delimiter
together with the preceding blank; Ghostty's own test calls the result
non-ideal. The other audited implementations select the delimiter alone or do
not start word expansion there, so Shitty retains the majority behavior.

The fourth 20-case `Screen.zig` block is executable in
`test_ghostty_screen_capacity_prompt_click.py`. Eleven Ghostty cases exercise
its private page allocator, style and hyperlink reference counts, explicit OOM
injection, and selection-map cleanup. Shitty has no corresponding page or
failing-allocator API, so these are adapted to observable stress contracts:
thousands of OSC 8 lifecycles, a long URI across storage growth, active style
and hyperlink state across resize/reflow and scrollback, hundreds of distinct
SGR styles, and repeated linked-text extraction. All eleven run against the
product; no Ghostty allocator hook or page-capacity hook is introduced.

The public state preserved by those adaptations is not a Ghostty policy.
Alacritty, Ghostty, Kitty, Contour, iTerm2, VTE, and foot implement OSC 8 and
attach an active hyperlink to subsequently written cells; xterm does not
implement OSC 8 and abstains. All eight retain the current SGR rendition until
another SGR changes it. The OSC 8 hyperlink specification defines the open and
empty-URI close operations, while ECMA-48 defines the persistent current
graphic rendition. Neither standard exposes allocator capacity or failure
injection, so the original internal assertions have no cross-product vote.

The other nine cases cover prompt click-to-move. Ghostty's `cl=line` counts
semantic input cells and emits cursor-left or cursor-right keys. Kitty both
implements the `click_events` form and, without it, counts prompt cells and
emits cursor keys; iTerm2's option-click feature also emits cursor keys.
Alacritty, xterm, Contour, VTE, and foot do not provide an end-to-end
click-to-move implementation and abstain. The shell-integration `click_events`
specification defines SGR mouse reports for cooperating shells, but does not
define Ghostty's `cl=line` arrow-counting policy and abstains on the exact
expectations.

Shitty already parses `cl` and `click_events`, but its pointer frontend does
not act on either mode. The three zero-movement cases therefore pass, while
the six exact movement cases remain executable expected failures. This records
the complete public feature gap rather than treating the implementations'
different activation gestures as a reason to omit it. The audit uses the same
eight source revisions listed for the preceding block.

The fifth 20-case `Screen.zig` block is executable in
`test_ghostty_screen_cursor_style_scrollback.py`. It closes the first twenty
holes in the complete source inventory rather than continuing from the file's
tail. Ghostty's private cursor-copy, page reference-count, compacted-capacity,
empty-slice, and mixed-page-width assertions are exercised through their
public consequences: modes 47/1049, SGR and OSC 8 lifetime, the first styled
linked grapheme write, invalid empty DECERA, resize followed by ED, reverse
wrap after reflow, and bounded scrollback navigation. No page or cursor-copy
test hook is added.

Current rendition across a mode-47 screen switch is the clear consensus.
Alacritty and Ghostty copy the cursor template, xterm uses one terminal-level
cursor, Contour carries its cursor, and iTerm2, VTE, and foot keep the current
SGR state outside the screen storage. Kitty resets its cursor on entry and is
the sole divergent implementation. ECMA-48 changes the current graphic
rendition only through the corresponding control functions; xterm's
47/1047/1049 specification adds no implicit SGR reset. Shitty's existing
rendition behavior therefore remains unchanged.

Active OSC 8 state is genuinely split. Alacritty, Contour, iTerm2, and foot
carry it through mode 47; Ghostty explicitly disables hyperlink copying,
Kitty zeros `active_hyperlink_id`, and VTE clears the current hyperlink because
its two screens use separate hyperlink pools. xterm has no OSC 8 support and
abstains. The OSC 8 specification keeps a hyperlink open until another OSC 8
with an empty URI and defines no alternate-screen close, so the vote is five
to three for persistence. Shitty already follows that majority. The three
exact Ghostty reset expectations remain executable expected failures, paired
with passing persistence tests; no minority policy is smuggled in as an
unconditional oracle.

All eight implementations clamp scrollback movement at the oldest retained
row and at the live-screen boundary. Scrollback navigation itself has no
terminal wire standard, so the standards vote abstains there. The source audit
uses Alacritty `1b2b36a64e88`, Ghostty `7e463bc65d43`, Kitty
`0d3259f87d1c`, xterm `6380a3eaed85`, Contour `c51e15ed254e`, iTerm2
`3ec57866cd9b`, VTE `3d55bbdddb87`, and foot `a635e0a196d9`.

The sixth 20-case `Screen.zig` block is executable in
`test_ghostty_screen_scroll_region_history.py`. It covers viewport anchoring
while output grows and is pruned, selection movement and expiry, scrolling a
bounded region with current style and background, four scroll-complete cases,
and six variants of scrolling above the cursor around storage boundaries.
Ghostty's private node-generation and page-boundary assertions are represented
by repeated `LF` at a `DECSTBM` bottom margin after different amounts of public
scrollback rotation. No page-list or generation hook is added to Shitty.

The ordinary region-scroll cases agree with Shitty on both parser backends.
Their public operation is the common `LF`-at-bottom-margin path, not a synthetic
call to Ghostty's `cursorScrollRegionUp` or `cursorScrollAbove`. The audit
followed that path through Alacritty's grid scroll, Ghostty's
`cursorDownScroll`, Kitty's index/line-buffer rotation, xterm's index and
scroll code, Contour's `linefeed`/`scrollUp`, iTerm2's `terminalLineFeed`,
VTE's `cursor_down_with_scrolling`, and foot's CSI/grid scroll implementation.

`Screen.scrollClear` is not a Ghostty-only semantic feature. Kitty and Ghostty
expose the exact `CSI 22 J`; Alacritty, Contour, iTerm2, and VTE move the
visible page into history while handling full ED; and xterm implements the same
operation with `cdXtraScroll`. Foot implements ED 0 through 3 but has no
corresponding preserve-page operation, so it abstains rather than voting
against the behavior. ECMA-48, fifth edition, section 8.3.39 defines ED only
in terms of erased character positions and has no scrollback model or mode 22,
so the standard also abstains. The supported-implementation vote is therefore
seven to zero for preserving the page in history.

Trailing blank rows have a narrower but still decisive consensus. Alacritty,
Ghostty, Kitty, and iTerm2 explicitly stop at the last non-empty row, while
xterm provides that policy as `cdXtraScroll=trim`. Contour and VTE retain a
whole page, yielding five to two for trimming; foot and ECMA-48 abstain. Shitty
currently ignores `CSI 22 J`, so the full, partial, and repeated exact upstream
expectations remain executable expected failures. The empty-screen case passes
vacuously. This records a real product gap without turning unsupported
implementations into negative votes.

This audit uses the same source revisions as the fifth block: Alacritty
`1b2b36a64e88`, Ghostty `7e463bc65d43`, Kitty `0d3259f87d1c`, xterm
`6380a3eaed85`, Contour `c51e15ed254e`, iTerm2 `3ec57866cd9b`, VTE
`3d55bbdddb87`, and foot `a635e0a196d9`.

The seventh 20-case `Screen.zig` block is executable in
`test_ghostty_screen_clone_history.py`. Two cases drive top-anchored scrolls
through rows containing ten distinct OSC 8 links after different amounts of
storage rotation, sixteen cover full and partial read-only copies, cursor
fallback, forward/reverse/rectangle selection clipping, and empty or one-line
views, and two exercise `CSI 3 J` with and without retained history.

The dense-link regressions are not reduced to plain text. Alacritty, Ghostty,
Kitty, Contour, iTerm2, VTE, and foot implement OSC 8 and keep the link attached
to a cell as that cell scrolls; xterm has no OSC 8 implementation and abstains.
The OSC 8 specification keeps the association active until an explicit close
and does not permit an internal storage boundary to discard already-written
links. The vote is therefore eight to zero including the specification. Both
fresh-destination and existing-destination adaptations verify every URI after
the public scroll operation.

Ghostty's exact `Screen.clone` is a private read-only helper. In the audited
revision its only callers are its unit tests and `ScreenClone` benchmark; the
terminal runtime does not invoke it. None of the other seven implementations
or ECMA-48 exposes a common wire operation for cloning an arbitrary row range
together with clipped selection and cursor state, so there is no meaningful
cross-terminal vote on that private API. The cases are still executable rather
than omitted: Shitty's public model snapshot is checked for independence from
later output, and its public selection and screen views cover full, partial,
out-of-bounds, reversed, rectangular, mixed-width, empty, and one-line
observations. No clone or range-snapshot hook is added to the product.

All eight implementations recognize the xterm `CSI 3 J` extension and remove
scrollback without erasing the active rows. ECMA-48 section 8.3.39 has no
scrollback model or ED parameter 3 and abstains. Both clear-history scenarios
pass in Shitty, including returning a viewport parked in deleted history to the
live screen. This block therefore has no executable expected failures.

The source revisions are Alacritty `1b2b36a64e88`, Ghostty `7e463bc65d43`,
Kitty `0d3259f87d1c`, xterm `6380a3eaed85`, Contour `c51e15ed254e`,
iTerm2 `3ec57866cd9b`, VTE `3d55bbdddb87`, and foot `a635e0a196d9`.

The eighth 20-case `Screen.zig` block is executable in
`test_ghostty_screen_resize_history.py`. It covers clearing active rows above
the cursor without touching history; height growth and shrink with empty,
populated and soft-wrapped storage; width growth and truncation without
reflow; a perfect soft-wrap merge; scrolled viewport anchoring; background-only
trailing rows; and preservation of semantic prompt rows.

Ghostty's explicit `reflow = false` argument is private to its resize API.
Shitty's public same-width primary resize already uses its copy path, while its
alternate screen uses that path for width changes as well. The corresponding
cases therefore exercise those existing public paths rather than adding a
test-only reflow switch. The ordinary primary-width growth cases still use the
normal reflow path and verify that hard line breaks, a perfect soft-wrap split,
viewport position and semantic rows survive it.

One height-growth policy has no consensus. With retained history and the
cursor above the bottom row, Ghostty, Contour and iTerm2 leave the old active
page at the top and add blank rows below; Kitty does the same by default.
Alacritty always pulls as many retained rows as fit, VTE aligns all retained
content at the top once it fits, and Foot's completed full-reflow path has the
same result. xterm pulls with its default `SouthWestGravity`. Kitty's
`scrollback_fill_enlarged_window`, xterm's `NorthWestGravity`, and Foot's
temporary interactive no-reflow path also expose the opposite policy in those
implementations. ECMA-48 defines neither host-side window resizing nor
scrollback and abstains.

The exact Ghostty expectation consequently remains an executable expected
failure, paired with a passing assertion of Shitty's retained-row pull. No
production behavior is changed and neither side of the split is presented as
a consensus oracle. The other nineteen upstream observations pass.

The source revisions are Alacritty `1b2b36a64e88`, Ghostty `7e463bc65d43`,
Kitty `0d3259f87d1c`, xterm `6380a3eaed85`, Contour `c51e15ed254e`,
iTerm2 `3ec57866cd9b`, VTE `3d55bbdddb87`, and foot `a635e0a196d9`.

The ninth 20-case `Screen.zig` block is executable in
`test_ghostty_screen_resize_reflow.py`. It covers width growth and shrink with
reflow, hard and soft line boundaries, simultaneous height and width growth,
height shrink with and without history, bounded history, cursor relocation,
and preservation of current SGR and OSC 8 state across both copy and reflow
resize paths.

Three Ghostty cases assert private storage machinery rather than a terminal
operation. The bounded `PageList` case is represented by a public mass-unwrap
while the viewport is parked in history. The allocator failpoint case uses a
rejected public zero-width resize and verifies that the complete model and the
current style and hyperlink remain intact. The two page-reference cases use
same-width and width-changing public resizes and verify the rendition applied
to the next cell. No allocator, page, reference-count, or reflow test hook is
added to Shitty.

Ghostty keeps a delayed-wrap cursor on the last printed cell after a width
increase. Alacritty, Kitty, Contour, iTerm2, VTE, and foot resolve it to the
next insertion column while reflowing; xterm does not perform the equivalent
logical-line reflow and abstains. The exact Ghostty cursor expectation is
therefore an executable expected failure paired with Shitty's passing
majority policy.

Height shrink without scrollback and with the cursor above the discarded rows
is evenly split. Alacritty, xterm, Contour, and VTE keep the cursor's top
content, as Shitty does. Ghostty, Kitty, iTerm2, and foot keep the newest
bottom content. ECMA-48 defines neither host resize nor scrollback and
abstains. The exact Ghostty expectation remains an executable expected failure
and is paired with the passing Shitty policy; neither side is called a
consensus. The other eighteen upstream observations pass. The earlier Contour
test name and comment are corrected to describe the same height-growth
behavior as a Shitty policy rather than a nonexistent consensus.

The source revisions are Alacritty `1b2b36a64e88`, Ghostty `7e463bc65d43`,
Kitty `0d3259f87d1c`, xterm `6380a3eaed85`, Contour `c51e15ed254e`,
iTerm2 `3ec57866cd9b`, VTE `3d55bbdddb87`, and foot `a635e0a196d9`.

The tenth 20-case `Screen.zig` block is executable in
`test_ghostty_screen_resize_wide_prompt.py`. Fourteen cases cover repeated
reflow, simultaneous row growth and column shrink, cursor tracking through
written and unwritten cells, and wide graphemes that are removed, wrapped or
unwrapped at a new edge. Four cover Ghostty's prompt-redraw resize option and
two cover the lifetime of replaced and cleared selection pins.

The selection-pin count is private storage state. The adaptations clear and
replace selections through the public frontend, verify that the obsolete
extent is no longer active, and then extract the replacement extent. No pin
counter or selection-lifetime hook is added to Shitty. The wide-spacer cases
likewise assert complete graphemes, continuation cells and wrap boundaries
through the public model rather than exposing Ghostty's spacer enum.

One resize case again invokes Ghostty's private `scrollClear`; its public
operation is `CSI 22 J`. As recorded for the sixth block, Ghostty, Kitty,
Alacritty, Contour, iTerm2, VTE and xterm preserve the active page in history,
while foot and ECMA-48 abstain. Shitty still ignores this extension. The exact
case therefore remains an executable expected failure, paired here with a
passing assertion that the unsupported sequence leaves the page and cursor
valid through resize.

Ghostty's `prompt_redraw` resize argument is not part of the terminal wire
protocol. Ghostty is the only audited implementation that leaves an active
OSC 133 prompt or input line cleared after resize. Kitty temporarily clears
such rows but copies them back after reflow; Contour, iTerm2, VTE and foot
preserve them through their ordinary resize paths. Alacritty and xterm do not
implement OSC 133 and abstain. The Semantic Prompts specification defines the
prompt, input and output markers but no host resize or redraw behavior and
also abstains. The three clearing expectations are executable expected
failures, paired with a passing preservation test. The completed-output case
passes because Ghostty does not clear output either.

Sixteen exact upstream observations consequently pass and four remain
expected failures. The source revisions are Alacritty `1b2b36a64e88`, Ghostty
`7e463bc65d43`, Kitty `0d3259f87d1c`, xterm `6380a3eaed85`, Contour
`c51e15ed254e`, iTerm2 `3ec57866cd9b`, VTE `3d55bbdddb87`, and foot
`a635e0a196d9`.

The final nine `Screen.zig` cases are executable in
`test_ghostty_screen_prompt_click_tail.py`. The private operation of selecting
an already tracked selection is represented by selecting the same public
extent twice, checking that its order and coordinates are unchanged, and
extracting it afterward. No tracked-pin API is exposed.

The other eight complete the `cl=line` prompt-click matrix: moving left while
skipping output cells, crossing a soft wrap, stopping at a hard break, and
clicking beyond the input on the same or a lower row from three cursor
positions. They use real pointer press/release events and inspect bytes sent
to the PTY. The audit and implementation revisions are the same as for the
fourth block: Ghostty and Kitty count semantic input cells and emit cursor
keys, and iTerm2 provides its corresponding option-click movement. Alacritty,
xterm, Contour, VTE and foot have no end-to-end click-to-move operation and
abstain. The Semantic Prompts `click_events` option specifies SGR mouse
reports for a cooperating shell, not Ghostty's `cl=line` arrow synthesis, and
also abstains on these exact counts.

Shitty parses the click option but its pointer frontend does not act on it.
The two cases in which the cursor is already at the clamped input end pass;
the six cases requiring synthesized arrows remain executable expected
failures. Together with the preceding ten blocks, all 209 tests in the current
`Screen.zig` inventory are now represented by distinct executable scenarios.

The first 20 `PageList.zig` cases are executable in
`test_ghostty_pagelist_storage.py`. They cover Builder and detached
PageAllocation validation and failure atomicity, row and wrapping movement of
pins across mixed-width pages, exact and extreme pin offsets, ownership of
incremental compression state, bounded compression traversal, and progress
after codec or allocation failure.

Builder, PageAllocation, Pin and incremental page compression are private
Ghostty storage APIs with no terminal wire equivalent. The adaptations keep
each upstream case distinct while checking its public consequence: rows
written at different widths retain order, recoverable resize rejection leaves
the complete model untouched, history limits publish no spare capacity,
viewport and selection anchors survive growth and reflow, out-of-range public
coordinates clamp to the page, two sessions keep independent navigation
state, and later history operations continue after a rejected transaction.
Style, OSC 8 linkage and selected text are checked across storage replacement.

No allocator failure, compression state, page builder or raw pin hook is added
to Shitty. ECMA-48 and the other audited terminal implementations expose no
common representation contract for these internals, so they abstain rather
than being counted against Ghostty's data-structure assertions. All 20 public
adaptations pass on both parser backends. The Ghostty source is revision
`7e463bc65d43`.

The second 20-case `PageList.zig` block is executable in
`test_ghostty_pagelist_compression.py`. It covers continuation after decommit
failure, traversal restart after replacement, reset, active-boundary resize
and prune reuse, live generations after partial erase, split and front or
middle replacement, non-restoring memory inspection, preserved reads, cold
page eligibility, lazy restoration and a viewport spanning storage pages.

These remain private representation assertions, so the tests use no guessed
compression contract. Their separate public consequences are exercised by
recoverable-error continuation, RIS and ED 3, bounded history rotation,
repeated width reflow, complete wide graphemes, persistent selection and
rendition, stable parked viewports, exact logical history limits, growing
history into the active area, and repeated model or scrollbar inspection that
does not mutate content. No compression, decommit, generation or memory-stats
API is added to the product.

All 20 adaptations pass on both parser backends. The absence of a common
storage representation in ECMA-48 and the other terminals remains an
abstention, not a vote about Ghostty's allocator and compression design. The
source revision is Ghostty `7e463bc65d43`.

The third 20-case `PageList.zig` block is executable in
`test_ghostty_pagelist_coordinates.py`. Seven scenarios finish the initial
compression group with incompressible and oversized pages, restoration by
access, reset, destruction and pruned-memory reuse. The remainder covers
initial geometry and failures, multi-page height, nonstandard width,
active/screen coordinate conversion across history pages, overflow rejection,
growth beyond a zero history budget, required pruning and scrollbar state.

Compression corruption and scratch storage remain private, so their public
adaptations use dense rendition and OSC 8 history, long auxiliary values, RIS,
session destruction and bounded storage reuse. The coordinate half uses the
existing screen model, selection frontend and scrollbar report directly,
including a two-billion-cell input coordinate that must clamp rather than
wrap. Large initial dimensions are real product instances; invalid dimensions
are exercised through the normal startup failure path.

All 20 adaptations pass on both parser backends. No raw page access or test
failure hook is introduced. The Ghostty source revision is `7e463bc65d43`;
ECMA-48 supplies no storage representation contract for the private cases.

The fourth 20-case `PageList.zig` block is executable in
`test_ghostty_pagelist_scroll.py`. It covers an empty history budget, top and
live endpoints, signed row deltas and saturation, absolute history anchors,
the active boundary, repeated navigation in both directions, and preservation
of a scrolled viewport while new rows enter history. Ghostty's `pin` and
`viewport_pin_row_offset` cache are private; their public adaptations assert
the visible rows and the absolute scrollbar tuple rather than exposing either
implementation detail through a test hook.

The public behavior agrees across the checked implementations. Alacritty
clamps `display_offset` for delta, page, top and bottom operations; Kitty
clamps `scrolled_by`; xterm clamps `topline`; Contour has direct clamp and
output-preservation tests; iTerm2 routes absolute rows through its scroll view;
VTE clamps `scroll_delta`; and foot bounds both up and down commands against
the circular grid. The checked revisions are Alacritty `1b2b36a6`, Ghostty
`7e463bc6`, Kitty `0d3259f8`, xterm `6380a3ea`, Contour `c51e15ed`, iTerm2
`3ec57866`, VTE `3d55bbdd`, and foot `a635e0a1`. ECMA-48 does not define a
frontend scrollback viewport, so it abstains on these policies.

All 20 adaptations pass on both parser backends. No production code or
test-only PageList API is added.

The fifth 20-case `PageList.zig` block is executable in
`test_ghostty_pagelist_limits.py`. It covers scroll-complete, prompt-relative
navigation, growth within and beyond the current backing storage, coordinates
beyond 16-bit row ranges, byte and row limits, pruning with two kinds of
viewport anchor, and cache invalidation after bulk or single-row erasure.

Ghostty and Kitty implement `CSI 22 J` as moving the primary screen into
scrollback and clearing it. Alacritty, xterm, Contour, iTerm2, VTE and foot
accept only their ordinary ED modes and abstain on this extension. ECMA-48
defines ED modes 0, 1 and 2, not mode 22, and likewise abstains. Shitty does
not implement the two-implementation extension, so the exact wire scenario is
an executable expected failure.

Prompt-relative viewport navigation has broader independent support: Ghostty
has `jump_to_prompt`, Kitty `scroll_to_prompt`, Contour `ScrollMarkUp` and
`ScrollMarkDown`, iTerm2 previous/next mark actions, VTE previous/next prompt
scrolling, and foot `prompt-prev`/`prompt-next`. Alacritty and xterm have no
corresponding semantic-prompt navigation and abstain. The Semantic Prompts
specification defines OSC 133 row markers but deliberately does not prescribe
a frontend navigation action. Shitty records the markers but has no prompt
navigation binding; the three nonzero prompt-navigation cases therefore
remain expected failures. The zero-delta no-op passes through the public
viewport interface.

Page allocation, tracked pins, byte accounting and the minimum internal page
size are Ghostty representation details. All eight audited terminals do,
however, expose a configured line-count bound for scrollback and discard old
history while retaining the active screen. Their allocation granularity and
runtime-reconfiguration policies differ: in particular Ghostty enforces its
limits at complete-page boundaries, while Shitty's public `saveLines` contract
is an exact logical-row cap. The adaptations therefore test Shitty's public
policy directly, including zero and non-page-sized limits, resize, independent
sessions, rich cell metadata, anchored pruning and scrollbar recomputation;
they do not add a byte-limit or page-capacity API. ECMA-48 does not specify
scrollback storage and abstains on these host policies.

Sixteen exact or public-behavior adaptations pass and four cases remain
expected failures on both parser backends. The checked revisions are
Alacritty `1b2b36a6`, Ghostty `7e463bc6`, Kitty `0d3259f8`, xterm `6380a3ea`,
Contour `c51e15ed`, iTerm2 `3ec57866`, VTE `3d55bbdd`, and foot `a635e0a1`.
No production code or test-only PageList API is added.

The sixth 20-case `PageList.zig` block is executable in
`test_ghostty_pagelist_capacity_iteration.py`. Six cases cover bounded row
erasure, trailing truncation, affected generations and viewport-cache
invalidation. Their public adaptations exercise partial and full-screen SU,
oversized region shifts, resize after trailing blank rows, and a parked
history viewport. Alacritty, Ghostty, Kitty, xterm, Contour, iTerm2, VTE and
foot all implement upward scrolling through the selected vertical region,
clamp the count to that region and leave rows outside it alone. ECMA-48 SU
supplies the applicable ordered-shift and blank-fill contract. Page
generations and Ghostty's cached pin offset remain private and are not exposed
to the tests.

Nine cases cover capacity growth for styles, graphemes, hyperlinks and their
string storage, tracked pins, exhaustion, width changes, multiple pages and
dirty state. Exact allocation limits are Ghostty-private: the other terminals
use different cell, auxiliary-string and scrollback representations, and
therefore abstain on `increaseCapacity` itself. The adaptations assert only
public consequences: old and new SGR values, Unicode grapheme clusters and
OSC 8 targets survive growth and history rotation; selection coordinates
survive unrelated metadata growth; reflowed geometry remains stable; and a
changed row is published for redraw. All eight audited implementations retain
SGR and combining-character state in their own cell models. OSC 8 is retained
by Ghostty, Alacritty, Kitty, Contour, iTerm2, VTE and foot; xterm does not
implement OSC 8 and abstains on those two scenarios. UAX #29 supplies the
grapheme-boundary reference, while neither it nor ECMA-48 prescribes backing
storage capacity.

The final five cases cover forward and reverse PageIterator traversal over
one or two active or history pages. Page chunks are again private, so the
public tests assert complete logical screen/history order and reverse
selection order, including a 300-row screen and row zero. All eight terminals
present screen and retained history in that logical order, but none shares
Ghostty's PageIterator representation; ECMA-48 also abstains on backing-page
iteration.

All 20 adaptations pass on both parser backends. The checked revisions are
Alacritty `1b2b36a6`, Ghostty `7e463bc6`, Kitty `0d3259f8`, xterm `6380a3ea`,
Contour `c51e15ed`, iTerm2 `3ec57866`, VTE `3d55bbdd`, and foot `a635e0a1`.
No production code or test-only PageList API is added.

The seventh 20-case `PageList.zig` block is executable in
`test_ghostty_pagelist_semantic_iteration.py`. Its first five cases finish the
reverse PageIterator group and cover count limits across row zero and page
boundaries plus forward and reverse cell iteration. Page chunks and pins are
Ghostty-private. The adaptations observe retained-history order, the complete
row-major cell model and reverse public selection, including a 300-row
boundary crossing. All eight audited terminals expose the same logical
screen/history order while using different backing iterators; ECMA-48 defines
screen positions but not their storage traversal.

Six cases exercise PromptIterator in both directions, continuation rows and
inclusive limits. Its direct public consequence in this block is selecting a
complete multi-line command-output range from OSC 133 markers. Ghostty and
Kitty expose selection of the output under a point, Contour and iTerm2 expose
copy/select-command-output actions, and foot exposes `pipe-command-output`.
VTE retains per-cell prompt, input and output attributes but has no matching
selection action and abstains; Alacritty and xterm do not implement OSC 133
and also abstain. Shitty retains the semantic cells but triple-click still
selects one logical line, so all six multi-line output-range adaptations are
executable expected failures rather than duplicate metadata-only checks.

The final nine cases cover prompt and input highlighting on one line, at an
output boundary, across a soft wrap and continuation prompt, at screen end,
and with either no input or no output. `highlightSemanticContent` is private
and its prompt/input variants have no direct Shitty action. Their public
adaptations therefore assert the exact per-cell OSC 133 prompt/input/output
boundaries that such an action would consume. Ghostty, Kitty, Contour, iTerm2,
VTE and foot all retain the corresponding semantic zones, although their
storage granularity ranges from cells to line marks and resilient ranges;
Alacritty and xterm abstain. The Semantic Prompts specification supplies the
`A`/`P`, `B`, `C` and continuation-marker meaning but deliberately does not
mandate a GUI selection gesture.

Fourteen adaptations pass and six command-output-selection gaps are expected
failures on both parser backends. The checked revisions are Alacritty
`1b2b36a6`, Ghostty `7e463bc6`, Kitty `0d3259f8`, xterm `6380a3ea`, Contour
`c51e15ed`, iTerm2 `3ec57866`, VTE `3d55bbdd`, and foot `a635e0a1`. No
production code or test-only PageList API is added.

The eighth 20-case `PageList.zig` block is executable in
`test_ghostty_pagelist_semantic_erase.py`. Its first eight cases finish the
input/output variants of `highlightSemanticContent`: final input, prompt-only
and output-free zones, one-line and multi-line output, the next-prompt and
screen-end bounds, and leading empty output cells. Four exact semantic-cell
adaptations pass. The four cases requiring selection of a complete multi-line
output zone remain expected failures under the same implementation consensus
as the preceding block: Ghostty, Kitty, Contour, iTerm2 and foot expose an
output extraction action; VTE, Alacritty and xterm abstain. The Semantic
Prompts specification defines the markers and zones, but not the gesture.

The remaining twelve cases cover complete history erasure and accounting,
anchors in removed and shifted rows, viewport fallback after complete or
partial pruning, automatic active-screen regrowth, a one-row screen, and
bounded row erasure at and below the top. Page counts, allocation bytes and
tracked pins are private. Their public adaptations use ED 3, ED 2, DL, SU,
the scrollbar, selection invalidation, and hyperlink metadata moving with its
row. They distinguish full history removal, later history regrowth, a pruned
viewport clamping to the new oldest row, active-screen geometry, shifted
content and erased anchors without adding a raw pin API.

All eight audited implementations support ED 3 history removal and implement
DL/SU within the selected vertical region while moving cell attributes with
the row. Their GUI anchor policies differ, so the portable invariant is only
that an erased anchor cannot keep referring to removed content; Shitty clears
such a selection. ECMA-48 defines ED 0 through 2, DL and SU. ED 3 is the xterm
extension implemented by the eight terminals and is therefore established by
implementation consensus rather than ECMA-48.

Sixteen adaptations pass and four command-output-selection gaps are expected
failures on both parser backends. The checked revisions are Alacritty
`1b2b36a6`, Ghostty `7e463bc6`, Kitty `0d3259f8`, xterm `6380a3ea`, Contour
`c51e15ed`, iTerm2 `3ec57866`, VTE `3d55bbdd`, and foot `a635e0a1`. No
production code or test-only PageList API is added.

The ninth 20-case `PageList.zig` block is executable in
`test_ghostty_pagelist_clone_resize.py`. Four erase cases cover a bounded
range reaching screen end or crossing backing pages and dense OSC 8 rows
moving through full or bounded deletion. The public adaptations use large
real screens and DL/SU regions, verify exact outside rows and damage, and
resolve every moved hyperlink URI. All eight implementations agree on the
DL/SU row movement and attribute preservation; Ghostty, Alacritty, Kitty,
Contour, iTerm2, VTE and foot retain OSC 8 targets, while xterm abstains on
the hyperlink-specific assertion. ECMA-48 supplies the DL/SU region contract
but has no hyperlink metadata.

Nine clone cases cover full and left/right bounded copies, discarded style
storage, a range shorter than the active screen, remapped and excluded pins,
and dirty rows. `PageList.clone` and its pin-remap map are Ghostty-private
read-side machinery with no terminal protocol or common implementation API.
The adaptations use the independent public model snapshot, bounded selection
copy, history pruning, selection preservation/expiry and damage publication.
They assert only that copied observations remain independent and refer to
live cells; the other terminals and ECMA-48 abstain on Ghostty's clone
representation and allocator reclamation.

Seven no-reflow height-resize cases cover growth with and without history,
shrink to five or one row, a bottom anchor, an anchor moving into scrollback,
and trimming background-only tail rows. All eight implementations preserve
the surviving row contents and keep a valid cursor and screen height, but
their choice of whether an enlarged viewport pulls retained history into view
is a host policy and differs. The adaptations execute Shitty's existing
bottom-gravity policy explicitly rather than treating Ghostty's policy as a
universal oracle. ECMA-48 does not specify host window resizing or scrollback
gravity and abstains.

All 20 adaptations pass on both parser backends. The checked revisions are
Alacritty `1b2b36a6`, Ghostty `7e463bc6`, Kitty `0d3259f8`, xterm `6380a3ea`,
Contour `c51e15ed`, iTerm2 `3ec57866`, VTE `3d55bbdd`, and foot `a635e0a1`.
No production code or test-only PageList API is added.

The tenth 20-case `PageList.zig` block is executable in
`test_ghostty_pagelist_resize_no_reflow.py`. Four height-only cases cover
trimming a background-only tail around the cursor, releasing backing rows,
extending blank rows and containing a parked viewport. Eight width-only cases
cover truncation and growth of hard rows, cursor clamping, removal of a
grapheme in discarded columns, stale wide-character spacers on both allocation
paths, preserving rows as page capacity falls, and shrink followed by growth.
The final eight cases combine both dimensions, exercise bottom and scrollback
anchors, resize beyond one standard Ghostty page, resize an empty screen, and
grow around a cursor that is not on the final content row.

Ghostty's page count, allocation capacity, tracked-pin representation and
grapheme arena are private. The public adaptations therefore assert grid
geometry, visible and historical hard-row order, cursor and selection
coordinates, model cells and wide-character integrity. They do not expose a
PageList or allocator hook. Alacritty explicitly selects its non-reflow path
for the active alternate grid; Ghostty exposes the tested non-reflow resize;
foot has a truncating interactive-resize path. Kitty, xterm, Contour, iTerm2
and VTE do not expose Ghostty's page/capacity API and abstain on those storage
assertions. Across all eight implementations, surviving hard-row cells remain
ordered, the cursor is clamped to the resized grid, and a discarded half of a
wide character cannot remain as a visible standalone glyph. Exact history
gravity and viewport anchoring remain host policy and are asserted here only
as Shitty behavior.

ECMA-48 specifies neither host window resizing, scrollback, page allocation nor
wide-cell backing metadata, so it abstains on the resize operation. Unicode
Standard Annex #29 supplies the grapheme-cluster boundary used by the one
combining-character case, but likewise says nothing about terminal resize or
storage reclamation. All 20 adaptations pass on both parser backends. The
checked revisions are Alacritty `1b2b36a6`, Ghostty `7e463bc6`, Kitty
`0d3259f8`, xterm `6380a3ea`, Contour `c51e15ed`, iTerm2 `3ec57866`, VTE
`3d55bbdd`, and foot `a635e0a1`. No production code or test-only PageList API
is added.

The eleventh 20-case `PageList.zig` block is executable in
`test_ghostty_pagelist_reflow_capacity.py`. Ten cases cover widening hard and
soft lines, invalidating a parked viewport's cached offset, reflow across
private page boundaries, and cursor anchors before, inside and after a soft
wrap. Four allocator-capacity cases move long implicit and explicit OSC 8
links, maximal combining clusters and 128 distinct styles through reflow.
Three wide-character cases distinguish removing, moving and retaining the
right-edge spacer needed before a wide glyph. The other three preserve an
OSC 133 prompt marker on blank, widened and newly split rows.

Page boundaries, cached pin offsets, string-chunk rounding and auxiliary
allocator capacities are Ghostty-private. Their separate public adaptations
use a 300-row screen, exact viewport text and offset, OSC 8 target resolution,
the complete grapheme payload, RGB cell colors and cursor coordinates. No
storage or fault-injection hook is added. All eight audited terminals have
resize paths that keep surviving hard rows ordered and clamp or remap the
cursor to valid geometry; whether soft lines are automatically reflowed is a
host option or policy, so these tests assert Shitty's enabled primary-screen
reflow rather than declaring it universal. Every implementation keeps a wide
glyph and its continuation structurally consistent through its chosen path.

Ghostty, Alacritty, Kitty, Contour, iTerm2, VTE and foot preserve OSC 8 targets
through reflow; xterm does not implement OSC 8 and abstains. All eight retain
SGR and combining-character state in their cell models. Ghostty, Kitty,
Contour, iTerm2, VTE and foot retain OSC 133 semantic zones through resize;
Alacritty and xterm abstain. The Semantic Prompts specification defines the
prompt marker but not host resize behavior. ECMA-48 likewise specifies neither
host resizing, reflow, scrollback nor backing allocation. UAX #29 supplies the
grapheme-cluster boundary but no resize policy.

All 20 adaptations pass on both parser backends. The checked revisions are
Alacritty `1b2b36a6`, Ghostty `7e463bc6`, Kitty `0d3259f8`, xterm `6380a3ea`,
Contour `c51e15ed`, iTerm2 `3ec57866`, VTE `3d55bbdd`, and foot `a635e0a1`.
No production code or test-only PageList API is added.

The twelfth 20-case `PageList.zig` block is executable in
`test_ghostty_pagelist_reflow_shrink.py`. Fifteen cases cover shrinking short
and full hard rows, OSC 133 prompt propagation, cursor and selection anchors
in content, blank cells and scrollback, blank rows before, after and between
content, and SGR or combining payload copied to new rows. Four cases cover
wide glyph elimination, right-edge wrapping and an intact ZWJ family cluster.
The final case carries Kitty's U+10EEEE placeholder stream across a row split.

Tracked pins are private Ghostty objects. In particular, its blank-cell case
clamps an anonymous pin at old column five to new column three, while Shitty's
real input cursor is normalized to the last content column. The adaptation
records that public cursor policy instead of treating the private pin result
as wire behavior. Physical `ALL_TEXT` rows, visible history, selection bytes,
cursor coordinates, model graphemes, SGR flags and wide-cell pairs cover the
other observable consequences without a PageList hook.

The eight audited terminals preserve hard-line separation, live SGR and
combining payloads, and structurally complete wide glyphs through their chosen
resize path. Automatic reflow and exact cursor gravity remain host policy.
Ghostty, Kitty, Contour, iTerm2, VTE and foot retain OSC 133 prompt zones;
Alacritty and xterm abstain. Ghostty, Kitty and iTerm2 implement Kitty Unicode
image placeholders. Alacritty, xterm, Contour, VTE and foot do not implement
that placeholder interpretation and abstain. The executable placeholder case
asserts the PageList-level prerequisite only: U+10EEEE cells survive the
split; it does not claim that Shitty renders a virtual image placement.

The Kitty graphics protocol defines U+10EEEE and its combining-diacritic
metadata, but not a terminal's resize storage. UAX #29 supplies the ZWJ
grapheme boundary. ECMA-48 does not define host resize, reflow, scrollback or
wide-cell representation. All 20 adaptations pass on both parser backends.
The checked revisions are Alacritty `1b2b36a6`, Ghostty `7e463bc6`, Kitty
`0d3259f8`, xterm `6380a3ea`, Contour `c51e15ed`, iTerm2 `3ec57866`, VTE
`3d55bbdd`, and foot `a635e0a1`. No production code or test-only PageList API
is added.

The thirteenth 20-case `PageList.zig` block is executable in
`test_ghostty_pagelist_reset_compact.py`. Two cases carry Kitty's U+10EEEE
placeholder cells through shrink/widen and into a new physical row. Five
exercise PageList reset through public RIS: blank geometry and home cursor,
repeated storage generations, a 300-row screen, invalidated selection and
viewport anchors, and discarded history. The remaining thirteen drive dense
grapheme reflow, parked-viewport clamping, bounded pruning, width-growth pin
remapping, storage growth, reset, snapshot copying and metadata reuse through
public screen operations.

Ghostty's node serials, page ownership, pool/heap distinction, capacity
dimensions, exact compaction size and linked-list topology are private. Their
adaptations therefore assert only consequences a client can observe: valid
screen geometry, ordered retained rows, independent snapshots, stable
selection content, complete grapheme/link/style payloads and genuinely blank
cells after storage reclamation. The allocation-heavy scenarios remain useful
under the suite's sanitizers without exposing an allocator hook.

Alacritty's `Grid::reset`, Ghostty's `Screen.reset`, Kitty's
`do_screen_reset`, xterm's `VTReset`, Contour's hard-reset path, iTerm2's
`resetForReason`, VTE's `RIS` and foot's `term_reset(..., true)` all restore a
blank live screen and reset its cursor/selection anchors; all eight clear or
replace the history backing used by the reset screen. They also initialize
new/reused cells rather than exposing stale rendition, grapheme or hyperlink
metadata. Exact allocation, compaction and viewport gravity are implementation
policy, so no cross-terminal claim is made for those private details.

Ghostty, Kitty and iTerm2 recognize U+10EEEE as a Kitty Unicode image
placeholder. Alacritty, xterm, Contour, VTE and foot abstain. As in the prior
block, the executable cases assert only the shared storage prerequisite that
the codepoint follows reflow; Shitty is not claimed to render a Kitty image.
ECMA-48 section 8.3.105 defines RIS as Reset to Initial State, but does not
specify scrollback, page pools, host resize/reflow, selections or allocator
reuse. UAX #29 defines the extended grapheme clusters whose codepoints must
stay together, but does not define terminal storage.

All 20 adaptations pass on both parser backends. The checked revisions are
Alacritty `1b2b36a6`, Ghostty `7e463bc6`, Kitty `0d3259f8`, xterm `6380a3ea`,
Contour `c51e15ed`, iTerm2 `3ec57866`, VTE `3d55bbdd`, and foot `a635e0a1`.
The source contains 274 PageList tests: this block accounts for cases 239–258,
leaving 16 split cases. No production code or test-only PageList API is added.

The final 16 `PageList.zig` cases are executable in
`test_ghostty_pagelist_split.py`. Four cover split positions at the middle,
first and last row and the unsplittable single-row case. Five cover a tracked
pin before, at and after a split, multiple simultaneous pins, and the viewport
pin. Three cover first/middle/last linked-list insertion. The final four retain
soft-wrap flags, styles, grapheme clusters and OSC 8 hyperlinks.

`PageList.split`, its chosen row, node identities, linked-list pointers and
`OutOfSpace` result are Ghostty-private. Its real public trigger is
`Screen.splitForCapacity`: exhausting managed style, grapheme or hyperlink
storage while the cursor and other anchors are live. The adaptations exercise
that trigger class through terminal input and assert its public consequences:
row order, active height, cursor, both selection anchors, parked viewport,
soft-wrap topology and cell metadata remain valid. Shitty need not use or
expose the same physical page split to satisfy that contract.

All eight audited terminals preserve ordered live rows and valid cursor,
selection and viewport state while their backing storage grows; none exposes
Ghostty's page-node split as terminal protocol. All eight preserve SGR and
combining payloads in live cells. Ghostty, Alacritty, Kitty, Contour, iTerm2,
VTE and foot preserve OSC 8 hyperlinks through storage movement; xterm does
not implement OSC 8 and abstains. Automatic soft-line reflow remains host
policy, so the executable wrap case records Shitty's enabled reflow behavior
rather than declaring one universal resize policy.

ECMA-48 defines character presentation, cursor and control semantics but not
terminal backing pages, allocation failure, scrollback storage, selection or
host resize. UAX #29 defines the extended grapheme clusters carried by these
cells but not their allocation. Both therefore abstain on the private split
mechanism while supporting the observable character boundaries used by the
adaptation.

All 16 adaptations pass on both parser backends. The checked revisions are
Alacritty `1b2b36a6`, Ghostty `7e463bc6`, Kitty `0d3259f8`, xterm `6380a3ea`,
Contour `c51e15ed`, iTerm2 `3ec57866`, VTE `3d55bbdd`, and foot `a635e0a1`.
All 274 `PageList.zig` tests are now accounted for, so the completed source is
removed from `dev/PLAN.md`. No production code or test-only PageList API is
added.

The first 20 `formatter.zig` cases are executable in
`test_ghostty_formatter_plain.py`. They cover one- and multi-line plain text,
wide cells selected from either half, rectangle extraction, interior and
trailing blank rows, optional trailing-space trimming, formatter state across
hard and soft row boundaries, physical versus unwrapped soft lines, and
inclusive row subsets. Selection copy is the public logical formatter;
`ALL_TEXT` supplies the separate physical-row observation.

Ghostty's byte-to-`Coordinate` point map and the `trailing_state` passed
between private page formatter instances are not public terminal behavior.
Their adaptations use selection endpoints, a 300-row screen and hard/soft
boundaries to verify the observable mapping: wide continuations snap to one
glyph, hard rows insert one newline, soft continuations insert none, and range
endpoints address the intended cells across backing-storage boundaries.

Alacritty, Ghostty, Kitty, xterm, Contour, iTerm2, VTE and foot all extract
hard rows with line separators, join soft continuations for ordinary copy,
and avoid duplicating the continuation half of a wide glyph. Rectangle and
range extraction are likewise present in all eight, although endpoint
inclusivity is API-specific. Trailing whitespace is policy rather than VT
protocol: Ghostty exposes `clipboard-trim-trailing-spaces`, Kitty takes a
`strip_trailing_whitespace` argument, iTerm2 and VTE expose trim/preserve
variants, while the ordinary Alacritty and foot copy paths trim.

Two executable cases are therefore explicit XFAILs for missing formatter
modes in Shitty. Its selection copy preserves explicitly drawn trailing
spaces and has no trim toggle; its physical `ALL_TEXT` view preserves a
trailing space on a soft row and has no `unwrap=false, trim=true` combination.
The passing no-trim and logical-unwrapping cases ensure those policies are not
silently conflated.

ECMA-48 defines the control functions that create hard line boundaries and
the presentation positions occupied by characters, but not clipboard/export
formatting, selection ranges, trimming or host soft-wrap serialization. UAX
#29 defines the character/grapheme boundary relevant to indivisible cells but
does not define grid selection. Both abstain on formatter policy.

All 20 adaptations pass on both parser backends with two documented expected
failures. The checked revisions are Alacritty `1b2b36a6`, Ghostty `7e463bc6`,
Kitty `0d3259f8`, xterm `6380a3ea`, Contour `c51e15ed`, iTerm2 `3ec57866`, VTE
`3d55bbdd`, and foot `a635e0a1`. The fixed source contains 101 formatter tests,
so 81 remain. No production code or test-only formatter API is added.

The next 20 `formatter.zig` cases are executable in
`test_ghostty_formatter_ranges_vt.py`. Twelve complete the plain-page boundary
matrix: row and column endpoints before, inside and beyond the page, partial
first and last rows, ignored prior state, and leading blank rows. Eight cover
plain extraction of styled cells and the public replay consequences of
Ghostty's VT output: plain, bold, bold plus italic, indexed foreground,
dynamic default foreground/background, independent styles across hard rows,
and a redundant SGR set.

Ghostty's `start_x`, `start_y`, `end_x`, `end_y`, `trailing_state`, point map
and exact serialized byte stream are private formatter contracts. Shitty has
no VT serializer and gains no formatter or test-only API. The boundary cases
therefore drive its public selection copy, whose endpoints clamp to the grid
and whose reverse ranges normalize as a user selection. In particular, the
private invalid `start_x > end_x` range emits nothing in Ghostty, while the
public Shitty selection deliberately copies the normalized five-cell blank
span. The executable adaptation records that distinction instead of claiming
that two unlike APIs have identical invalid-input behavior.

Ordinary plain selection in all eight audited terminals emits character data,
not the cells' SGR source sequences. All eight implement SGR bold, italic,
palette foreground and reset semantics, including idempotent repeated mode
sets and persistence until an explicit reset. All eight also implement the
xterm dynamic-default color controls used by the replay case. Styled export
is not a common terminal wire operation: implementations without a VT export
surface abstain on Ghostty's canonical reset placement and redundant-sequence
elision. The tests consequently assert the reproduced cell state rather than
inventing a Shitty byte serializer.

ECMA-48 supplies the SGR state and hard-row control semantics. xterm's control
sequence reference supplies OSC 10 and OSC 11. Neither specifies clipboard
coordinates, selection clamping, a byte-to-cell point map, or canonical VT
serialization, so those host-policy details abstain from the standards vote.

All 20 adaptations pass on both parser backends with no expected failures. The
checked revisions remain Alacritty `1b2b36a6`, Ghostty `7e463bc6`, Kitty
`0d3259f8`, xterm `6380a3ea`, Contour `c51e15ed`, iTerm2 `3ec57866`, VTE
`3d55bbdd`, and foot `a635e0a1`. Cases 1–40 of the fixed 101-test source are
now accounted for, so 61 remain. No production code is changed.

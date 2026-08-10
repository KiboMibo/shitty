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

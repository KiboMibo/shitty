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

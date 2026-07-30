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
regression. Four exposed interoperability gaps remain explicit skipped tests.
Runtime cursor-default mutation has no corresponding public Shitty option,
tracked pins are Ghostty storage internals, status-display selection is not
implemented, and Ghostty's private glyph APC protocol is outside the terminal
protocol scope.

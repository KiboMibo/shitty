# Mosh fuzz corpora

The files in `terminal_corpus/` and `terminal_parser_corpus/` are copied
verbatim from Mosh revision `decd9b705eb81626f694335b8d5940538beb06da`.
They are the seed corpora for Mosh's `Terminal::Complete` and `UTF8Parser`
libFuzzer targets. See `COPYING.upstream` (GPL-3.0).

The adapter feeds every member both as one write and byte-by-byte through two
freshly reset Shitty models. It compares PTY replies, frontend actions,
conformance modes, and the full rich model snapshot after each member.

`semantic_cases.py` is a behavior-preserving Python port of all 17
terminal-display regressions in Mosh's `src/tests/Makefile.am`: ASCII and
ISO-8859-1, the 80th-column pending-wrap state, every attribute variant,
back/forward tabs, cursor motion, insert/delete-line bounds, scrolling, wrap
across separate frames, orphan combining marks, and interactive resize.

The original tests compared Mosh output with a direct tmux session. The port
instead asserts the terminal model directly. Expectations follow ECMA-48 and
current OSC 8 practice. In particular, the old tmux-dependent 256-color XFAIL
is a normal conformance case, and the OSC 8 test uses the standard ST
terminator instead of preserving the upstream script's malformed `ESC [`.
Mosh networking, prediction, crypto, process-lifetime, and test-framework
self-tests are not terminal-emulator tests and are intentionally not ported.

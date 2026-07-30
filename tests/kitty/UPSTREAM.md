# Kitty parser fixtures

`upstream/parser.py` is copied verbatim from Kitty revision
`51116e5df35a6bd8dc96ab28de6e9b0cac8b287a` (2026-07-21).  The upstream
project is GPLv3; its license is preserved as `LICENSE.upstream`.

The catalog extracts all 426 `pb(...)` parser invocations whose input is a
literal string or byte string.  It does not import or execute Kitty.  Each
input is an independent build target and is replayed through the real Shitty
parser both whole and one byte at a time.  The adapter compares parser events,
replies, host actions, modes, renderer contract, and the rich terminal model.
Dynamic Python expressions remain for a later semantic adapter.

The screen catalog uses Python's AST to replay the statically unambiguous
`create_screen`, literal `pb(...)`, and `reset()` paths leading to literal
`str(s.line(...))` assertions. Each of the 19 imported checkpoints compares the
specified visible line at Kitty's declared test geometry. Dynamic expressions and
direct screen mutations are skipped rather than approximated. Internal line
strings containing control bytes are excluded because they are not visible
screen text.

The semantic transactions follow terminal standards and current implementation
consensus rather than treating Kitty as a specification. In particular, DEC
STD 070 limits DECCARA to bold, underline, blink, and inverse. Kitty, VTE,
Contour, and Windows Terminal implement the modern full-SGR extension, while
foot and xterm retain the DEC subset. Shitty follows the four-to-two
implementation consensus and tests full SGR, colors, and underline styles.

Kitty-specific callbacks are accounted for rather than silently copied. Its
legacy bare OSC title fallback is not dispatched because ECMA/xterm OSC syntax
requires a numeric selector. Arbitrary old OSC 99 metadata is rejected in
favour of the current structured notification protocol. `@kitty-cmd`,
`@kitty-print`, and Kitty's private DCS `=1s`/`=2s` pending-mode controls are
parsed and ignored: remote control and remote printing are deliberately outside
the terminal contract, and synchronized output uses DEC private mode 2026.
Kitty's test-only XTGETTCAP value for the synthetic name `kind` is replaced by
the normal unknown-capability reply; real declared capabilities are tested too.

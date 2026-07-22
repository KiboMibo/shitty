# Kitty parser fixtures

`upstream/parser.py` is copied verbatim from Kitty revision
`51116e5df35a6bd8dc96ab28de6e9b0cac8b287a` (2026-07-21).  The upstream
project is GPLv3; its license is preserved as `LICENSE.upstream`.

The catalog extracts all 426 `pb(...)` parser invocations whose input is a
literal string or byte string.  It does not import or execute Kitty.  Each
input is an independent build target and is replayed through the real Zutty
parser both whole and one byte at a time.  The adapter compares parser events,
replies, host actions, modes, renderer contract, and the rich terminal model.
Dynamic Python expressions remain for a later semantic adapter.

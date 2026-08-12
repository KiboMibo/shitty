# libtsm semantic tests

These tests are Python ports of all 32 named assertion tests in libtsm's
`test_screen.c`, `test_selection.c`, `test_vte.c`, and `test_vte_mouse.c`.
The source snapshot is in `monorepo/tmp/terminal-repos/libtsm`.

The original tests are MIT licensed. Copyright belongs to their respective
libtsm contributors.

`test_screen_null` and `test_vte_null` exercise libtsm's nullable C ABI, which
Shitty does not expose. Their ports cover the corresponding public object
lifecycle and harmless empty operations; they do not claim C ABI compatibility.

An unfinished drag in `test_screen_copy_incomplete` copied its anchor cell in
libtsm. Shitty follows desktop selection behavior: a press without a non-empty
drag does not create clipboard text.

libtsm mirrors BEL/ST from OSC color queries. Shitty emits the canonical ST
terminator for replies, independent of the request terminator.

libtsm also ignored extra parameters to OSC 10/11. Current xterm dynamic-color
syntax assigns successive parameters to successive dynamic colors, so the port
checks two consecutive queries instead.

The old `test_vte_osc4` expected color-setting requests to be ignored. OSC 4
palette mutation is established current terminal behavior, so the port verifies
that a set is accepted and observable by a following query.

# Contour vtconformance / vttest

The 143 `golden/*.dump` files and scenario design come from Contour revision
`ff1da74de2c2cded7216eda4835ec1fa8400d7b3`, under Apache-2.0; see
`CONTOUR-LICENSE.txt`.

The test helper sources under `vttest/` come from vttest revision
`0229d7171a8574a2bf406c6ce14549f65d810e51` (version 2.7, 2025-12-05); see
`VTTEST-COPYING`. `config.h` was produced by that revision's configure script
for the Linux/musl build environment used by Shitty.

`scenarios.json` is a direct transcription of Contour's scripted menu paths.
The Python adapter replaces Contour's terminal-engine harness with Shitty's
PTY/control harness while retaining the upstream dump format and one build
target per scenario.

`test_contour_checksum.py` rewrites all 12 test cases from
`src/vtbackend/RectangularAreaChecksum_test.cpp` at the same Contour revision.
It retains every xterm-406-derived checksum oracle: negation and overflow,
written versus undrawn blanks, the six DEC video-attribute weights, DEC
charset mapping, combining marks, and all five composable XTCHECKSUM flags.
The one upstream pure-algorithm case with an empty rectangle is retained as a
native Screen unit test because no valid DECRQCRA wire request denotes an
empty rectangle.

`test_contour_input_generator.py` rewrites the terminal-observable core of all
122 cases in `src/vtbackend/InputGenerator_test.cpp`.  Cases 1 and 4 through
61 are exercised through Shitty's real `plt::InputSink` path: C0 aliases,
legacy modifier combinations, lock filtering, modifyOtherKeys, DECNKM,
DECBKM, Kitty keyboard flags and event types, alternate keys, keypad text,
and reset.  The two `InputBinding.match` cases are covered by
`input_bindings_ut.cpp`; focus, wheel, and reset cases are also cross-checked
by `test_modes.py`, `test_mouse_frontend_scroll.py`, and `test_reset_matrix.py`.
Contour's assertion that a pure Shift chord never engages
`modifyOtherKeys=2` is intentionally replaced with the established
xterm-compatible result already covered by Shitty's exhaustive ASCII matrix;
xterm, Kitty's current F3 specification, and the Kitty/Foot/Alacritty
implementations are likewise used to replace the obsolete modified-F3
`CSI 1;modifier R` spelling with unambiguous `CSI 13;modifier~`.

The remaining assertions are retained as explicit capability boundaries.
Contour's modifier-name table and private pending-sequence `consume()` ABI do
not exist in Shitty.  Three wheel cases configure Contour's internal
`MouseWheelMode` directly, with no terminal sequence capable of requesting
that state.  The 50 `Win32InputMode` cases test private ConPTY DECSET 9001;
Shitty has no ConPTY frontend and deliberately reports mode 9001 unsupported,
which is pinned by `test_windows_terminal_adapter.py`.  They are therefore
not silently approximated with the generic Wayland/Cocoa input contract.

`test_contour_grid.py` and `screen_ut.cpp` account for all 32 cases in
`src/vtbackend/Grid_test.cpp`.  The 25 terminal-observable cases are rewritten
through the PTY harness: logical-line iteration in both directions,
height/width resize, reversible reflow, hard-line boundaries, finite
scrollback, sparse and blank rows, viewport history, long lines, and semantic
OSC 133 regions.  Seven storage/rendering cases use native Screen tests:
blank history remains unmaterialized across a large resize, blank capture
uses the shared sparse row, and partial horizontal scrolling preserves
distinct blank-cell attributes without materializing equal blank rows.

Two implementation choices intentionally follow Shitty's public contract
rather than Contour internals.  Shitty has bounded scrollback (including an
explicit zero-history mode), so Contour's `Infinite` history case is exercised
at the largest useful finite boundary rather than pretending to offer
unbounded storage.  When height grows while the cursor is above the bottom,
Shitty restores the newest history rows, matching Foot and Alacritty; Contour
instead leaves history untouched and appends a blank row.  Non-normal DEC
lines are tested natively to ensure they are clipped rather than reflowed.

`test_contour_shell_integration.py` inventories all 31 cases in
`src/vtbackend/ShellIntegration_test.cpp` and imports the terminal-observable
protocol core.  OSC 133 prompt/input/output boundaries are checked across
multi-line prompts and reversible reflow, and Contour's `CSI > M` SETMARK is
parsed directly.  Primary and alternate screens now retain independent
semantic state, so an alternate-screen application cannot inherit a live
primary prompt and returning to the primary screen restores its input region.

The inventory also makes two remaining boundaries explicit.  Twelve cases
exercise Contour GUI extraction APIs (`lastCommandBlock()` and
`livePromptSpan()`), for which Shitty does not yet expose an equivalent
product or test API; their underlying semantic cells and reflow invariants are
covered, but the extraction API remains a separate task.  Another twelve
cases exercise Contour-private DEC mode 2034, authenticated DCS queries,
random session tokens, and JSON replies.  No independent terminal in the
local Foot, Alacritty, Kitty, Ghostty, VTE, xterm, or WezTerm sources
implements that protocol, so it is recorded as an intentional capability
boundary rather than silently approximated.  The `LineFlags` formatter case
is likewise a private Contour value-object assertion with no wire behavior.

`test_contour_kitty_clipboard.py` inventories all 19 cases from
`src/vtbackend/KittyClipboard_test.cpp`. The packet parser cases are also
covered at the native `ParserIface` boundary, while the Python suite exercises
the complete OSC 5522 exchange through the real asynchronous clipboard and PTY
output paths: bounded/chunked writes and reads, errors, permissions, MIME
aliases, sanitized `id` echoing, TARGETS and paste-event mode 5522.

Two adaptations intentionally follow the protocol and Shitty's capabilities
instead of Contour internals. Shitty implements primary selection on both
supported platforms, so `loc=primary` is tested as a distinct successful
target rather than forced to `ENOSYS`. Shitty has no DEC status-line screen;
the transmission-lifetime invariant is tested across a primary/alternate
screen switch instead. TARGETS replies use Kitty's current wire shape:
`mime=.` with the available MIME names in the payload, rather than Contour's
`mime=text/plain` packet with an empty payload.

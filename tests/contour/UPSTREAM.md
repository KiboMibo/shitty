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

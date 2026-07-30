# Windows Terminal parser streams

`upstream/StateMachineTest.cpp` and `upstream/OutputEngineTest.cpp` are copied
from Windows Terminal revision with CRLF line endings normalized to LF
`a198a6cac075da15d4b6c21eea65f32caa77f0d8` (2026-07-21). The upstream
project is MIT licensed; its license is preserved as `LICENSE.upstream`.

The catalog statically extracts every literal `ProcessString(L"...")` call,
including adjacent C++ string literals, without compiling or executing the
Windows test framework. C1 wide characters are mapped to the raw 8-bit control
bytes accepted by Shitty. Each call site is an independent build target and is
compared whole versus bytewise across parser events and the full observable
terminal state. Variable-built streams and Windows Terminal's semantic screen
assertions remain for a later adapter.

`../test_windows_terminal_mouse.py` translates all five methods and every
data-source row from
`src/terminal/adapter/ut_adapter/MouseInputTest.cpp` at the same revision:
1,155 default/UTF-8/SGR button cases, 55 SGR motion cases, 220 wheel cases,
and the alternate-scroll transaction. Windows Terminal limits the old default
encoding to coordinate 95 and extends UTF-8 encoding to `SHORT_MAX - 32`.
Shitty instead follows xterm's protocol bounds: 223 for the byte encoding and
2015 for UTF-8, clamping larger coordinates. SGR remains unbounded.

`../test_windows_terminal_input.py` accounts for all nine methods in
`src/terminal/adapter/ut_adapter/inputTest.cpp`. It exercises the portable
terminal boundary rather than Windows `INPUT_RECORD` details: named keys,
focus, modifiers, control characters, DECBKM, DECARM, and S7C1T/S8C1T.
Windows Terminal's Ctrl+Backspace policy is host-input behavior, and its
unconditional modified editing keys and raw Ctrl+number results disagree with
the xterm modifier-resource protocol already covered by Shitty's exhaustive
keyboard matrices. Those rows therefore use the existing xterm-compatible
policy. The import found that S8C1T was not applied to keyboard-generated
CSI/SS3 and added the required `ESC Fe` to C1 folding.

`../test_windows_terminal_kitty_keyboard.py` translates all four methods in
`src/terminal/adapter/ut_adapter/kittyKeyboardProtocol.cpp`: every one of the
129 table rows and the three repeat transactions. The adapter statically reads
the copied C++ table and drives Shitty through its generic `plt::InputKey`
boundary and direct protocol test hooks. The import removed the duplicate
terminal-specific key enum and extended the platform key set through F35,
media/volume keys, and semantic keypad keys.

Expected sequences follow the current Kitty protocol rather than preserving
obsolete encodings in older tests: F3 is `CSI 13 ~`, default modifier value 1
and default press event type 1 are omitted, and an omitted modifier parameter
remains an empty field before associated text. These forms agree with current
Kitty, Foot, Ghostty, Alacritty, WezTerm, and Windows Terminal. Associated text
is not synthesized for Control or Super key events, because platform text
input suppresses those modifiers and the protocol forbids C0/C1 control codes
in that field.

`../test_windows_terminal_selection.py` translates all 21 methods in
`src/cascadia/UnitTests_TerminalCore/SelectionTest.cpp`, copied as
`upstream/SelectionTest.cpp` at the same revision. The test-mode display
exposes both the raw drag rectangle and the snapped rectangle already supplied
to renderers, so the coordinate assertions do not depend on copied text alone.
The corpus covers clamping, history/view coordinates, block selection, wide
glyph boundaries, word and logical-line expansion, direction changes, and the
selection pivot. Windows' per-row expansion of a rectangular highlight around
a partially covered wide glyph is implemented in both GPU and reference
renderers and covered by the reference-renderer unit test. It intentionally
does not alter rectangular clipboard extraction: like Konsole, Shitty omits a
wide glyph when only half of it lies inside the copied column range.

Windows Terminal allows callers to replace Char/Word/Line expansion explicitly
on each Shift+click. Shitty's platform-neutral mouse input has no Windows
`SelectionExpansion` argument and follows Foot and Kitty: extending an existing
word- or line-wise selection preserves that mode. The corresponding upstream
transaction is adapted to that consensus while retaining separate char, word,
line, and persistent-drag assertions. Selection coordinates are half-open, as
in the rest of Shitty's selection API.

The 25 methods in `src/terminal/parser/ut_parser/InputEngineTest.cpp` test the
opposite, Windows-only boundary: decoding a VT input byte stream into Win32
`INPUT_RECORD`, cursor API calls, and `CSI _` Win32 input records. Shitty is a
terminal emulator and has no conhost input decoder. Its observable producer
side is covered by the input and mouse matrices above; the remaining
`INPUT_RECORD` field assertions are not applicable.

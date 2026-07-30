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

`../test_windows_terminal_buffer.py` translates all 10 methods in
`src/cascadia/UnitTests_TerminalCore/TerminalBufferTests.cpp`, copied as
`upstream/TerminalBufferTests.cpp` at the same revision. It covers basic and
wrapped writes, viewport anchoring while output advances and the history ring
wraps, the complete tab-stop mutation and traversal surface, and implicit URL
detection across soft wraps, scrollback, and viewport coordinates. The
upstream helper discovers tab stops by moving forward from column zero, so its
expected list intentionally excludes the otherwise valid stop at column zero.

Windows Terminal allocates exactly the requested 100 history rows. Shitty
rounds its shared screen ring to a power of two and consequently retains more
rows. The anchoring test first discovers that effective capacity, then
preserves every upstream transition: output does not snap a scrolled viewport,
the viewport follows its rows until they are overwritten, and finally remains
pinned to the oldest retained row.

`../test_windows_terminal_reflow.py` translates the complete parameterized
`src/buffer/out/ut_textbuffer/ReflowTests.cpp`, copied as
`upstream/ReflowTests.cpp` at the same revision: 15 cases and 42 buffer states.
The Python adapter parses the authoritative table, constructs each initial
screen, performs every resize in sequence, and compares every cell, wide-cell
continuation, forced-wrap marker, and cursor coordinate. A test-only
`SET_WRAPPED` hook is necessary because the upstream fixture directly creates
synthetic padded rows whose wrap metadata cannot always be produced by a VT
stream. Resize itself uses the normal product path.

Thirty-four states use the upstream result verbatim. Eight expected states are
adapted where Windows' fixed-height `TextBuffer::Reflow` deliberately circles
the completed buffer or applies its documented `REFLOW_JANK_CURSOR_WRAP`.
Shitty keeps the cursor-anchored viewport rather than discarding its beginning.
When a cursor maps exactly one cell beyond the new right edge, Shitty retains
it as pending wrap in the last cell, matching Alacritty and Ghostty, instead of
creating a forced blank continuation row. The adapted states still compare the
complete resulting grid and explicitly verify pending wrap.

`../test_windows_terminal_screen_buffer.py` starts the translation of all 113
methods in `src/host/ut_host/ScreenBufferTests.cpp`, copied as
`upstream/ScreenBufferTests.cpp` at the same revision. The source inventory is
checked statically so later upstream methods cannot disappear unnoticed. The
first 13 methods cover alternate-screen lifetime and cursor state, reverse
index, every tab-stop transition, ED2, and all 24 inactive C0 values. They run
through the observable VT boundary. The original private Win32 buffer-pointer
and moving-viewport assertions have no terminal protocol counterpart; their
observable lifetime and screen contracts are retained instead. Windows'
processed-output newline is expressed explicitly as CRLF, while raw terminal
LF remains ECMA-48 line feed.

The first block found a product gap. DECST8C (`CSI ? 5 W` and its omitted default)
now restores the standard stops every eight columns. Windows enumerates stops
by moving forward and therefore does not observe column zero; Shitty keeps
VTE's equivalent internal bitmap convention in which zero is set. The
next 12 methods cover cell resize and DECCOLM, pen preservation, DECSTR cursor
state on both screens, newline behavior around margins and scrollback, erase
colors, OSC 4 parsing, and the complete DECRSTS color-table transaction.

The second block found that DCS `2 $ p` color-table restore was ignored.
Ragel now parses every slash-separated definition directly into semantic HLS
or RGB callbacks, including omitted and clamped components; there is no
secondary string parser in Vterm. RIS restores the initial palette. Windows
treats `CSI 8;0;0t` as a no-op, while Shitty retains xterm's current behavior
of substituting the screen dimensions. Windows rejects `rgbi:`, but Shitty
retains the XParseColor model already shared with current color parsers.

The next 12 methods cover shrinking without lifetime corruption, cursor style
preservation through primary and alternate resize, active alternate geometry,
ED 2 cursor and erase-color behavior, word selection, active-screen VT
dispatch, and RIS from the alternate screen. Win32 buffer pointer identity and
`GetConsoleScreenBufferInfoEx` are translated to the observable active-screen
contract. `GetWordBoundaryTrimZerosOn` and `GetWordBoundaryTrimZerosOff` are
classified rather than emulated: they test the private host setting
`SetTrimLeadingZeros`, not a terminal protocol, and conflict with the project's
selection policy in which punctuation and whitespace remain independently
selectable classes.

The following 11 methods cover default-color sources, SGR reset and reverse,
backspace and delete-character attribute preservation, palette changes shared
by both screens, three-digit OSC 4 indices, and OSC 10/11 validation. The
Win32-only `WriteCharsLegacy` entry point is represented by equivalent whole
and chunked terminal byte streams. This block exposed missing VT525 DECAC
(`CSI Ps;Pf;Pb , |`). Shitty now parses its text and frame items into separate
semantic callbacks, assigns or resets the normal-text defaults, accepts the
256-color extension shared by Windows Terminal and Contour, and resets the
assignment on RIS. The frame item is deliberately a no-op, matching xterm:
Shitty does not own compositor or Cocoa window furniture.

The next five methods retain the complete 64-case near-end-of-line DCH matrix,
both original minimal regressions, the history-color lifetime invariant, and
all 15 SU/SD/IL/DL/RI combinations. Windows moves a private Win32 viewport
inside a larger console buffer; Shitty expresses the portable part through an
ECMA-48/DEC scrolling region. Content outside that region remains unchanged,
IL/DL move the cursor to the left margin, and newly revealed cells retain the
current foreground/background while clearing rendition metadata. The
`DontResetColorsAboveVirtualBottom` regression is observed at the terminal
boundary by viewing a colored history row while a write changes the live
screen.

The following five methods cover insert/replace mode, the complete centered
ICH/DCH matrices, DECIC/DECDC/DECFI/DECBI over a rectangular scrolling region,
and one-cell movement of wide glyphs through ICH, DCH, and DECCRA. The import
found that ICH checked both vertical and horizontal margins while DCH checked
only the horizontal range. They now use the same horizontal boundary rule.
Windows expands ICH/DCH to the full line when the cursor is outside separately
configured vertical margins. That result is adapted: VT510 defines these as
horizontal operations, and xterm and Ghostty continue using the configured
left/right margins independently of the cursor row. Both original vertical
margin branches remain exercised.

The next three methods cover ED3, all 12 combinations of EL/ED,
to-end/from-beginning/all, and regular/selective erasure, plus every upstream
DECSCA parameter case. The Win32 console-storage tail after its movable
viewport has no terminal equivalent; ED3 is checked through its observable
contract: history is removed, a scrolled view returns to the live screen, and
live cells, colors, and cursor remain unchanged. Two upstream policies are
adapted to current terminal behavior. Windows selective erase preserves the
old colors of erased cells, while xterm, Ghostty, and Shitty create blanks
using current erase colors. DECSCA has one parameter in the DEC grammar;
Windows applies the last parameter of a malformed list, xterm applies the
first, and Ghostty rejects the list. Both malformed cases remain covered with
xterm-compatible first-parameter behavior.

The next five methods retain all 12 margin-scrolling branches. SU and SD move
the vertical region with and without horizontal margins; IL and DL cover
vertical margins, the full screen, and the rectangular region; RI covers a
nonzero top margin, a top margin at the first row, and the rectangular region.
Every branch compares the complete six-row grid, and every cursor assertion
made by the upstream test is retained.

The following seven methods cover IND and NEL at the top and bottom of the
screen and both kinds of scrolling region; the nine IL/DL/RI and
16/256/direct-color combinations; LNM, DECSCNM, DECOM with DECLRMM, DECAWM
including both wide-glyph edge cases, and RIS before and after filling
history. The Win32 movable viewport is translated to the terminal's
screen-plus-history behavior. Its private render-settings color lookup is
represented by the published renderer reverse-screen state while the
underlying cell colors are verified to remain unchanged. RIS additionally
checks that observable history is removed.

The next five methods contribute three portable tests and two explicit host
classifications. Alternate-screen clearing is performed through ED2 and CUP
and verifies that primary contents and cursor survive. The complete 256
extended-attribute matrix and all 4096 attribute/foreground/background
combinations are retained, including every applicable individual reset after
each combination. This exposed a Shitty bug where changing bold/faint state
after a direct RGB foreground reconstructed it as the default color; bold
changes now leave direct colors intact while retaining the configured
bright-ANSI behavior. `RestoreDownAltBufferWithTerminalScrolling` and
`SnapCursorWithTerminalScrolling` manipulate `_virtualBottom`, movable Win32
viewports, and console APIs with no terminal protocol equivalent. Their
portable alternate-resize, scrollback-follow, and screen-lifetime invariants
are already covered by the translated resize and alternate-screen tests. The
next 11 methods cover vertical and horizontal cursor movement from inside,
outside, and exactly on rectangular margins; CNL/CPL; HPR/VPR; DECSC/DECRC
position, pending-wrap, rendition, charset, DECOM, changed-origin, and
clamping state; DECALN; and cursor visibility/blink modes. The import found
that printing to the right of horizontal margins was incorrectly clamped
back into the rectangle. It also found that DECSC stored absolute coordinates
in origin mode, so DECRC could not apply the saved relative position to
changed margins. Windows moves CNL/CPL to column zero after leaving vertical
margins; xterm's esctest, Ghostty, and WezTerm retain the configured left
margin, while Foot and Kitty agree with Windows. Shitty retains its existing
xterm-compatible carriage-return rule and tests both vertical outcomes.
Windows also starts with cursor blinking enabled; Shitty retains its
non-blinking product default while testing every DECSET/DECRST transition.
The next three methods retain all OSC 8 state transitions and add observable
cell assertions: an implicit link stays active across text and closes, an
explicit identity reuses the same link, and the same explicit `id` with a
different URI creates a distinct link without changing the earlier cell.
Nine methods are explicitly classified as Win32 host policy. They manipulate
the private `_virtualBottom`, pan a console viewport horizontally inside a
larger screen buffer, or invoke `SetConsoleCursorPosition` and
`MakeCurrentCursorVisible`. A terminal emulator has a live screen, scrollback,
and a user-controlled view instead of this virtual Win32 viewport. The
portable scrollback, resize/reflow, cursor, and alternate-screen invariants
from those methods are already exercised independently.
The remaining 12 methods stay explicitly listed in `PLAN.md`.

The 25 methods in `src/terminal/parser/ut_parser/InputEngineTest.cpp` test the
opposite, Windows-only boundary: decoding a VT input byte stream into Win32
`INPUT_RECORD`, cursor API calls, and `CSI _` Win32 input records. Shitty is a
terminal emulator and has no conhost input decoder. Its observable producer
side is covered by the input and mouse matrices above; the remaining
`INPUT_RECORD` field assertions are not applicable.

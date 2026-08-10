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

`test_contour_screen.py` starts the direct `Screen_test.cpp` transfer at
Contour revision `9f2b296f51770d6fb9a6c9614561594443fea864`. It rewrites the
first 12 cases: all `writeText.bulk.*` variants, the scalar vttest autowrap
pattern, and `AppendChar`. The assertions retain bulk versus scalar writes,
deferred wrap, right-edge overwrite with DECAWM disabled, full-page scrolling,
and bounded history. Contour's `LineCount(1)` is not copied as an exact public
history capacity: Shitty rounds nonzero row storage to a power of two, so the
case preserves and checks the resulting additional history row. Contour does
not assert its private delayed-wrap bit while DECAWM is disabled, and the port
does not invent that assertion.

The following 23 cases, from `AppendChar_CR_LF` through
`AppendChar_AutoWrap_LF`, are inventoried by the same suite. CR/LF, mutable
mode 2027, and copying a cluster written under an older width policy are
exercised directly. Their cell, wide-tail, ZWJ, VS16, right-edge, and autowrap
assertions are also covered by `test_cells.py`, `test_ghostty_grapheme.py`, and
`test_ghostty_terminal_input.py`.

Two expectations required an independent oracle. Contour no longer narrows a
wide emoji when a valid VS15 arrives, but Ghostty, Kitty, and Foot all do, so
Shitty retains the narrowing behavior. Conversely, Contour, Ghostty, and Foot
make DEC mode 2027 mutable and gate late cluster-width revision on it; WezTerm
and Windows Terminal report the mode permanently enabled. Shitty now follows
the former consensus: DECRST 2027 keeps codepoints in one grapheme but freezes
its width at the first codepoint, DECSET reenables revision, and RIS/DECSTR
restore the enabled default.

The next 12 cases, from `Screen.isLineVisible` through `InsertColumns`, are
also inventoried. Viewport visibility is covered by `test_contour_grid.py` and
`test_scrollback.py`; Backspace and Linefeed by `test_cursor.py` and the
scroll-region matrix; ED and EL by the Ghostty erase suites; and DECFI/DECIC by
the complete esctest matrices and the Windows Terminal screen-buffer port.
Those suites include margins, protection, wide cells, history and cursor
invariants beyond the individual Contour assertions.

`DSR.Unsolicited_ColorPaletteUpdated` exposed the one missing observable
contract. After an application enables private mode 2031, reapplying the
configured palette now emits `CSI ? 997;1 n` for a dark scheme or
`CSI ? 997;2 n` for a light scheme; with the mode reset it remains silent.
Contour reports palette resets, Ghostty reports configuration and system-theme
changes, and Kitty and Foot report corresponding configured theme changes.
Application-originated OSC 10/11 changes remain excluded because they do not
represent the user's configured color preference.

The following 12 cases, from `InsertCharacters.NoMargins` through
`DECSED-0`, are accounted by existing suites with broader matrices. ICH, IRM
and IL are covered by `test_editing.py`, the Ghostty blank-insertion and line
editing ports, and the Windows Terminal editing methods; together they retain
default and clamped counts, right-edge loss, wide cells, current erase colors,
both margin pairs and metadata movement. DECSCA save/restore and rendition
independence are covered by `test_ghostty_saved_cursor.py`, `test_cursor.py`
and the DECALN state test. Every DECSEL direction and the first DECSED
direction are covered by the Ghostty selective EL/ED suites, including the
independent DEC and ISO protection flags. Contour, xterm, Ghostty and Windows
Terminal agree on the behavior exercised by this block, so no alternative
oracle was needed.

The next 12 cases, from `DECSED-1` through
`DECSCA: selective erase still respects DEC protection after the ISO split`,
complete Contour's DECSED directions and its initial ISO guarded-area block.
Existing Ghostty erase ports and `test_editing.py` already cover DECSED 1/2,
empty unprotected rows, ED/EL/ECH, 7-bit and 8-bit SPA/EPA, coalesced parser
input, and the separation between ISO protection for ordinary erase and DEC
protection for selective erase.

The soft-reset case found one missing state transition. SPA now activates the
ISO-aware ordinary-erase model, EPA stops marking new cells without disabling
that model, and DECSTR/RIS disable it. Thus cells guarded before a reset become
erasable without rewriting their cell metadata. This follows xterm, whose
`ReallyReset` clears `protected_mode`, and Contour, whose soft reset calls
`resetProtection`; Ghostty uses the same persistent erase-mode split but does
not currently implement DECSTR. VTE and Foot do not implement enough SPA/EPA
semantics to act as contrary oracles, while Windows Terminal only provides the
DEC selective-protection half.

The following 12 cases, from `VT52: enter, cursor movement, and leave`
through `DECFRA.Full`, cover VT52 dispatch and the first rectangle-editing
block. The direct Vterm suite now checks VT52 direct addressing, home, all
four relative cursor commands, ED/EL, identify, and the return to ANSI mode.
xterm and Windows Terminal independently dispatch the same VT52 commands and
agree with Contour's final cursor and erase semantics.

DECSERA and DECFRA are already exercised more broadly by the complete esctest
rectangle matrices, the Windows Terminal rectangular-area port,
`test_defaults.py`, `test_editing_matrix.py`, and the checksum suite. In
particular, omitted or zero rectangle edges select the page boundaries;
xterm's `xtermParseRect` and Windows Terminal's `_CalculateRectArea` agree, so
Contour's `DECFRA.Invalid` label describes a valid all-page default rather
than an invalid rectangle. Delete-lines in-range and clamping behavior is
covered by the Ghostty, WezTerm, and Windows Terminal ports. Contour's direct
`deleteLines(0)` section has no terminal-stream equivalent because CSI `0 M`
uses the specified default count of one, so no private Screen API was exposed
just to reproduce that internal no-op.

The next 12 cases, from `DeleteColumns` through `MoveCursorBackward`, have
direct executable adaptations in `test_contour_screen.py`. They retain the
Contour setup and count/clamping matrices for DECDC, DCH, ECH, SU, SD, CUU,
CUD, CUF and CUB, plus ED 3 dropping history without changing the live page.
The complete esctest matrices and the Ghostty, WezTerm and Windows Terminal
ports remain independent cross-checks for both margin pairs, wide-cell repair,
erase attributes, metadata movement and exact damage.

Contour's `Unscroll` case calls a private Screen operation rather than a wire
sequence. Its observable behavior is covered by the Contour grid resize tests:
growing restores the newest available history rows in order, consumes only
those rows, and fills any remaining growth with blanks. When the cursor is not
at the bottom, Contour keeps history off-screen, while Foot and Alacritty
restore it; Shitty retains the latter consensus documented by the grid suite.
The zero-count sections that directly call Contour Screen methods likewise do
not override the terminal protocol, where omitted and zero CSI counts mean
one; the direct adaptations therefore exercise the wire semantics.

The following 12 cases, from `HorizontalPositionAbsolute` through
`CNL_CPL_clamp_to_scroll_region_and_left_margin`, are also direct executable
adaptations. They cover HPA, HPR, CHA, VPA, CR and NEL positioning; SD and IL
inside combined margins; autowrap at the right margin; DECBI movement and
horizontal scrolling; and CNL/CPL clamping inside and outside the vertical
region. xterm's cursor and column-index implementations and Windows
Terminal's `AdaptDispatch` agree on the command decomposition and margin
rules. Foot independently agrees on the non-horizontal-margin forms.

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

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
pattern, and `AppendChar`. Every case has a distinct executable scenario; the
ports retain the original boundaries between scalar and bulk writes,
deferred wrap, right-edge overwrite with DECAWM disabled, full-page scrolling,
and bounded history. Contour's `LineCount(1)` is not copied as an exact public
history capacity: Shitty rounds nonzero row storage to a power of two, so the
case preserves and checks the resulting additional history row. Contour does
not assert its private delayed-wrap bit while DECAWM is disabled, and the port
does not invent that assertion.

The following 23 cases, from `AppendChar_CR_LF` through
`AppendChar_AutoWrap_LF`, have direct executable adaptations retaining the
upstream per-codepoint and bulk-scanner boundaries. They cover CR/LF, emoji
presentation backgrounds, VS15/VS16 width changes, ASCII-base combining,
mode 2027 gating, both right-edge width revisions, copy without
remeasurement, single and ten-codepoint ZWJ clusters, wide-tail overwrite,
and both autowrap paths. `test_cells.py`, `test_ghostty_grapheme.py`, and
`test_ghostty_terminal_input.py` remain independent broader cross-checks.

Two expectations required an independent oracle. Contour no longer narrows a
wide emoji when a valid VS15 arrives, but Ghostty, Kitty, and Foot all do, so
Shitty retains the narrowing behavior. Conversely, Contour, Ghostty, and Foot
make DEC mode 2027 mutable and gate late cluster-width revision on it; WezTerm
and Windows Terminal report the mode permanently enabled. Shitty now follows
the former consensus: DECRST 2027 keeps codepoints in one grapheme but freezes
its width at the first codepoint, DECSET reenables revision, and RIS/DECSTR
restore the enabled default.

`AppendChar.abandoned_width_revision_restores_the_head_cell` is adapted with a
different expected result. Contour leaves the newly wide cluster narrow in the
old last-column cell, but Ghostty's exact right-edge VS16 test and Kitty's
widened-character path move the whole cluster to the next row. Shitty keeps
that consensus behavior; the exact Contour write sequence now guards the
move, old-cell cleanup, continuation cell and cursor state.

The next 12 cases, from `Screen.isLineVisible` through `InsertColumns`, are
direct executable adaptations. They retain each one-row viewport offset,
Backspace clamping, Linefeed scrolling, all ED/EL directions, the complete
repeated DECFI state progression, and DECIC outside/inside/repeated margin
cases. `test_contour_grid.py`, `test_scrollback.py`, `test_cursor.py`, the
Ghostty erase suites, the complete esctest matrices and the Windows Terminal
screen-buffer port remain broader cross-checks for protection, wide cells,
history and cursor invariants.

The direct public adaptation for `DSR.Unsolicited_ColorPaletteUpdated` lives
in `test_color_scheme.py` because palette replacement is a configuration
operation. It exposed the one missing observable contract. After an
application enables private mode 2031, reapplying the configured palette now
emits `CSI ? 997;1 n` for a dark scheme or `CSI ? 997;2 n` for a light scheme;
with the mode reset it remains silent.
Contour reports palette resets, Ghostty reports configuration and system-theme
changes, and Kitty and Foot report corresponding configured theme changes.
Application-originated OSC 10/11 changes remain excluded because they do not
represent the user's configured color preference.

The following 12 cases, from `InsertCharacters.NoMargins` through
`DECSED-0`, have direct executable adaptations. They preserve Contour's ICH
counts and horizontal-margin boundaries, IRM right-edge loss, sequential IL,
DECSCA cell metadata and saved state, every DECSEL direction, and both default
forms of DECSED 0. `test_editing.py`, the Ghostty blank-insertion, saved-cursor
and selective-erase ports, and the Windows Terminal editing methods remain
broader independent cross-checks for wide cells, erase colors and metadata
movement. Contour, xterm, Ghostty and Windows Terminal agree on this block, so
no alternative oracle was needed.

The next 12 cases, from `DECSED-1` through
`DECSCA: selective erase still respects DEC protection after the ISO split`,
have direct executable adaptations. They cover DECSED 1/2, empty unprotected
rows, ED/EL/ECH, 7-bit and raw 8-bit SPA/EPA, coalesced parser input, and the
separation between ISO protection for ordinary erase and DEC protection for
selective erase. The Ghostty erase ports and `test_editing.py` remain broader
independent cross-checks.

The exact raw-C1 Contour inputs intentionally use the consensus result instead
of Contour's expectation. In a UTF-8 stream xterm ignores decoded C1 controls,
Foot ignores raw bytes `0x80..0x9f`, libvterm enables them only when UTF-8 is
off, and VTE requires valid UTF-8 decoding; Contour and Ghostty accept the raw
bytes. Shitty therefore leaves raw `0x96`/`0x97` inert in UTF-8 mode. A
separate adaptation switches to the single-byte data path with `ESC %@` and
verifies that the same bytes then execute SPA/EPA, including inside a
coalesced input run.

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
through `DECFRA.Full`, have direct executable adaptations. They cover VT52
direct addressing, home, all four relative cursor commands, erase, identify,
and the return to ANSI mode; selective rectangular erase; delete-lines count
and clamping; and every Contour fill-rectangle shape/default scenario. xterm
and Windows Terminal independently dispatch the same VT52 commands and agree
with Contour's final cursor and erase semantics.

The complete esctest rectangle matrices, the Windows Terminal rectangular-area
port, `test_defaults.py`, `test_editing_matrix.py`, and the checksum suite are
independent cross-checks. In particular, omitted or zero rectangle edges
select the page boundaries; xterm's `xtermParseRect` and Windows Terminal's
`_CalculateRectArea` agree, so Contour's `DECFRA.Invalid` label describes a
valid all-page default rather than an invalid rectangle. The Ghostty, WezTerm,
and Windows Terminal delete-lines ports provide the same cross-check there.
Contour's direct `deleteLines(0)` section has no terminal-stream equivalent
because CSI `0 M` uses the specified default count of one, so the adaptation
exercises the wire behavior instead of exposing a private Screen API.

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

The next 12 cases, from `MoveCursorTo` through
`DECSTR.resets_left_right_margin_mode`, have distinct executable adaptations.
Contour's private cursor helpers are exercised through CUP, HT and the public
save/restore controls; the remaining scenarios retain all three alternate
screen modes, DCH and ED outside scrolling regions, both CBT margin cases,
VT/FF under LNM, DECSCL feature gating, and the DECSTR mode transition.

Two results use an independent oracle. Contour lets CBT cross the left margin
under origin mode, but current xterm, VTE and Windows Terminal clamp it to the
margin; Shitty keeps that consensus result for the exact Contour input.
Conversely, the direct DECSTR case exposed a Shitty bug: DECLRMM remained set
after its margins were reset. Current xterm, Contour, Windows Terminal,
WezTerm and libvterm all reset the mode, so Shitty now does as well. The old
esctest case requiring DECLRMM to survive DECSTR is retained as an explicitly
documented XFAIL; its companion DECSTR case and current xterm source agree
with the adopted behavior. Alacritty implements neither DECSTR nor DECLRMM;
Ghostty implements DECLRMM but not DECSTR; Kitty implements DECSTR but not
DECLRMM/DECSLRM. They therefore abstain on this specific transition rather
than count as contrary implementations.

The following 12 cases, from `DECRQCRA.honors_origin_mode` through
`DECRQSS reports the scroll-region margins`, are direct executable scenarios.
They preserve the origin-relative and absolute checksum requests; every IND
position outside, inside and at the bottom margin; all intermediate RI states
with zero, one and two margin pairs; the complete outside-horizontal-band
control matrix; DECALN margin reset; CNL/CPL clamping; and both DECRQSS margin
reports. Private Contour cursor and index calls are expressed through their
public CUP, IND, RI, CNL and CPL controls. The existing Ghostty index suite,
Windows Terminal cursor tests, checksum matrix and DECRQSS protocol test remain
independent broader cross-checks; no expectation diverged in this block.

The next 12 cases, from `DECRQSS reports the current SGR` through `DECARM`,
also have distinct executable scenarios. They cover exact SGR and DECSACE
status strings, all four VT525-keyboard-setting requests, raw C1 bytes and a
valid C1-range UTF-8 continuation, S7C1T/S8C1T replies, DECID versus DA1,
ordinary and extended CPR under origin mode, every in-band resize section,
ANSI/private DECRQM states, DECNKM and DECARM.

The DECSACE query exposed a missing observable operation. Shitty now reports
the already implemented stream/rectangle state through a dedicated parser
callback; xterm, VTE, Windows Terminal and Contour agree on values 0 and 2.
Three other Contour expectations do not match the broader oracle. Windows
Terminal and Ghostty, like Shitty, serialize extended SGR colors in colon
form, so indexed underline color is reported as `58:5:1`. VTE deliberately
does not implement DECELF, DECLFKC or DECSMKR behavior, while xterm, Windows
Terminal, Kitty, Ghostty and WezTerm do not provide those DECRQSS settings;
the exact requests therefore retain the valid unsupported reply
`DCS 0 $ r ST` instead of inventing inert state. Finally, raw C1-range bytes
in a UTF-8 stream become U+FFFD and remain non-controls, matching Ghostty,
Kitty, VTE and Foot; the same C1 controls are exercised separately in Shitty's
single-byte mode.

The following 12 cases, from `DECBKM` through `findMarkerDownwards`, are kept
as distinct executable scenarios even where broader tests already existed.
They replay the exact history-producing writes, every capture length, every
scroll offset, tab clearing/setting, fixed and manual backward-tab counts,
the reverse-search buffer including its wrapped match, the non-ASCII
smart-case source text, and marked/unmarked history rows. `DECBKM` is also
queried in each state.

Two Contour-internal APIs have no Shitty product surface: selecting a suffix
with `captureBuffer()` and searching the buffer. Those scenarios therefore
verify the complete retained source buffer and all requested suffixes, but do
not pretend that Shitty implements a capture or search command. Likewise,
Shitty exposes semantic prompt metadata but no next/previous-marker action;
the marker scenario verifies the exact metadata across live rows and history.
It exposed a real `CSI > M` defect: the Contour extension started coloring
subsequent cells as prompt text without marking the current row. It now starts
the prompt at the current row, matching Contour's deprecated SETMARK behavior
and its OSC 133 replacement.

Contour's tab tests differ at two private boundaries. Its default stops are at
zero-based columns 7 and 15, while xterm specifies column 9 and every eight
columns thereafter and Windows Terminal and VTE use zero-based 8 and 16;
Shitty retains that consensus. Contour's direct C++ call also treats a count
of zero as a no-op, whereas public `CSI 0 Z` uses CBT's default count of one,
as specified by xterm and implemented by Kitty. The executable wire scenario
therefore checks the latter behavior.

The next 12 cases, from `findMarkerUpwards` through
`DECCRA.Right.intersecting`, have separate executable scenarios. They cover
marked and unmarked live/history rows, DECTABSR defaults, clearing and custom
stops, DECSC/DECRC mode restoration, Unicode OSC 2, all OSC 4 and dynamic
OSC 10--19 forms, XTGETTCAP, zero-history retention, the full resize matrix,
and the three overlapping/trailing-parameter DECCRA copies.

Three private Contour APIs remain deliberately observable rather than copied:
marker navigation has no Shitty action, so the exact row metadata is checked;
`setMaxHistoryLineCount()` is represented by a zero-history session; and
Contour disables reflow for its resize test.  The latter exposed a Shitty data
loss defect: a column shrink with the cursor near the top retained that cursor
at the cost of rows below it. Ghostty's `PageList` keeps the active area
bottom-anchored and resets a cursor moved into history to top-left; Windows
Terminal retains a reflowed virtual bottom at least through the last nonblank
row; Foot and Alacritty retain reflowed material in their scrollback grids.
Shitty takes Ghostty's cursor recovery rule for its likewise one-sided
scrollback model: the test verifies that rows survive in history and reappear
on regrow, while both current and saved cursors moved into history restore at
top-left. The Windows Terminal source cases remain executable too, with their
different cursor-preserving virtual-buffer expectation recorded explicitly as
an adaptation rather than silently omitted. Contour's visible `AB`/`CD`
truncation remains a valid no-reflow result, not the oracle for Shitty's
reflowing product.

The dynamic-color cases found another product defect. Selection foreground and
background inherit OSC 10/11 defaults until explicitly set by OSC 19/17; they
were not being updated when those defaults changed. Both inherited paths now
follow their defaults, while explicit selection colors remain independent.
For XTGETTCAP `RGB`, Contour/WezTerm's `8/8/8` and xterm/Shitty's `8` encode
the same equal channel width, so the wire adaptation retains the established
xterm form. `am` and unknown capabilities correctly use the unsupported
reply because Shitty does not advertise them.

The next 12 cases, from `DECCRA.Left.intersecting` through the 96-character
SCS designation, likewise each have an executable scenario. They cover the
remaining overlapping DECCRA direction, the HPA XTGETTCAP query, the 100x100
Sixel checkerboard at normal and scrolling page heights, DECSTR, DECTST,
SGR save/restore, and every GL/GR locking-shift and 96-character designation.

Two source-private boundaries are made explicit. Contour exposes HPA from its
private termcap table, while Shitty does not advertise that capability and
returns the prescribed `0+r` result. Contour also enables a host status-line
API for its Sixel status case; Shitty has neither a status-line object nor a
wire control, so the public fallback verifies the ordinary page has no hidden
reserved row. The Contour fixture configures 10x10 image cells, whereas
Shitty's documented sixel patches are fixed at 6x12 pixels: the exact 100x100
checkerboard therefore occupies 17x9 Shitty cells and ends at `(0,8)`.

Contour alone treats DECTST's power-up test as reset. xterm 410, Kitty, and
Ghostty have no DECTST dispatch, so Shitty follows their common observable
no-op with no reply. This is retained as a source-named executable scenario,
not discarded. The other charset scenarios use their public glyph output to
check what Contour checked through private charset-table state.

The following 12 cases retain all DECAUPSS/DECRQUPSS transitions and the first
three tab layouts as separate scenarios.  Invalid size/designator pairs leave
UPSS unchanged; both DECSTR and RIS restore `%5`.  Contour's VT500-only Greek
expectation is adapted to actual xterm-410, which accepts it at VT320.  The
`<` scenario checks its public resolved glyph rather than Contour's private
designation flag.

The next 12 retain two more tab boundaries and ten independent DECCIR reports.
The reports are checked byte-for-byte on the wire: cursor position, every
reported rendition bit, protection, origin and pending-wrap flags, and G0/G1
designation identity.
The final three DECCIR cases independently retain GL and GR locking shifts
and the `Scss` 96-character-set bit.

Contour's eight MultiPage cases are retained as public xterm-compatible
scenarios: Shitty, like xterm-410, has no multi-page display memory and
ignores navigation/coupling controls, while DECRQDE and extended CPR report
the sole page as page 1.

All seven REP cases are now separate public scenarios: default and zero
counts, bulk input, both margin kinds, ordinary wrap/scroll, and no preceding
graphic character.

The following eleven DECSCL cases are separate public scenarios. xterm's
`CASE_DECSCL` resets, selects levels 61--65 and sets the C1 transmission
framing; its DECRQSS implementation reports the selected level. Shitty has
the same observable level, reset, and 7-/8-bit framing transitions. Contour
instead rewrites its DA1 optional-extension list after a level change, while
WezTerm's DECRQSS handler unconditionally returns `65;1\"p`. Those DA1 bits
therefore have no cross-implementation dynamic oracle: Shitty keeps DA1 as a
static description of its implemented device capabilities (level 64), and
uses DECRQSS as the public report of the selected conformance level.

The next five source cases are represented through their wire-observable
effects. The private C1-folding helper is covered by CSI, DCS and OSC replies;
the S8C1T/VT52 round trip follows xterm's transition back to VT100 and hence
back to seven-bit controls. The DECSCL reset and the VT100 DECRQCRA request
retain their independently observable effects.

XTSMTITLE/XTRMTITLE is retained through enabled window-operation title queries:
hex and UTF-8 set/query combinations, icon/window independence, bare reset,
and RIS are all exercised through the PTY.

The remaining MultiPage cases are likewise retained as public page-1
scenarios: DECSC/DECRC, DECCRA, alternate screen, reset, content continuity,
margin isolation, resize, and RIS all run against Shitty's one real screen
instead of a fabricated inaccessible page store.

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

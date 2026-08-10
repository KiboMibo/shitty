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

DECSET 41 is xterm-specific (no corresponding implementation is present in
the local Ghostty, WezTerm, or Kitty sources), but its public pending-wrap
behavior is retained in its own on/off scenario.

OSC 52 is retained as a write/read PTY round trip. Contour and Ghostty silence
denied reads; Kitty requires user permission. Shitty now follows that secure
common boundary rather than emitting an empty reply when `allowOsc52Read` is
disabled.

OSC 110 and 111 have their own foreground/background reset scenario, matching
xterm's dynamic-color reset and Ghostty's explicit parser coverage.

Contour alone implements DECDMAC/DECINVM: the local xterm, Ghostty, WezTerm,
and Kitty sources have no dispatch for either control. All eleven source cases
therefore remain distinct public no-op/recovery scenarios, including their
definition, overwrite, range, nesting and recursion streams.  A macro body
which contains `ESC [` is a deliberately visible parser boundary: xterm, VTE,
Ghostty, and Alacritty abort the DCS and parse the CSI, while Kitty keeps it as
payload until ST. Shitty follows the former, majority behaviour. It also does
not advertise extension 32 before or after a DECSCL transition.

DECUDK is different: Shitty and xterm program the physical function-key input
path, while VTE rejects it for security and Kitty/Ghostty have no DECUDK
handler. The transferred scenarios therefore press the corresponding F keys,
instead of inspecting Contour's private key table. Contour clears UDKs on
DECSTR; xterm clears them only on RIS, so the Shitty scenario records the
preserving behaviour. Similarly, Shitty's fixed VT420 DA1 advertises extension
8 before and after DECSCL, as xterm's configured DA1 does.

The NRCS tests explicitly enable DECNRCM before observing a replacement:
xterm's own NRCS test does the same, and the mode is off by default. Contour's
German and French checks omit that prerequisite. Its DEC Technical `A` check
also expects Greek capital alpha, but xterm's table and VTE's DEC Technical
designation map `A` to U+221D PROPORTIONAL TO; the transferred case asserts
that consensus value. As with extension 8, the fixed Shitty DA1 keeps extension
15 visible across DECSCL.

DEC Locator cases use only wire-observable `DECLRP` replies and frontend
pointer events. This retains the upstream distinctions between enable,
disable, one-shot, cell/pixel coordinates, event selection, unavailable
queries, reset, and DA1 advertisement without exposing a locator-state test
API.

DECDLD is a capability split rather than a product contract in the other
terminals: xterm's implementation is compiled only for tracing, VTE explicitly
declines it, and Ghostty, Kitty, and Alacritty do not dispatch it. The seven
scenarios therefore preserve each distinct DCS input stream and assert the
shared safe result for an unsupported DRCS: no screen leak, no false DA1
extension 7, and a designated DRCS character falls back to its plain cell.

DECALN has two dedicated scenarios: ordinary page fill and the same fill after
the history ring has wrapped. xterm, Ghostty, and the independent esctest
suite all treat it as the VT100 alignment pattern rather than ordinary text.

VPR is retained as one case with all three upstream sections: omitted and
explicit counts move vertically without changing the column, and an excessive
count clamps to the page. xterm and VTE implement the same cursor operation.

The following three DECRQCRA/XTCHECKSUM source cases are now separate
byte-level PTY scenarios. xterm accepts `CSI Pi * y` with all four rectangle
coordinates omitted, interprets it as the whole page, and echoes `Pi` in the
DCS reply; Shitty's parser had incorrectly required all six parameters, so
this transfer fixed that protocol bug. The exact checksum values and the
five XTCHECKSUM flags are independently cross-checked by the xterm-derived
checksum matrix. VTE recognises the same requests and framing, but deliberately
returns a dummy checksum outside its test mode because this query reads screen
contents. For the final Contour case, its non-wire `Settings::checksumExtension`
fixture has no Shitty counterpart: the scenario observes the public configured
default (zero) after both DECSTR and RIS, which is the default-resource case of
xterm's reset rule.

DECSNLS is a capability boundary, not a request to resize Shitty's PTY behind
the application. xterm performs it only with its window-operation permission;
VTE leaves the VT525 operation unimplemented, and Ghostty, Kitty, and
Alacritty have no handler. Its one source case is retained with all of its
parameter boundaries as a no-op scenario, and DECRQSS returns the standard
unsupported `0$r` response.

The following LF-below-margin case is retained with its single and repeated
bottom-edge inputs, ordinary advance below a smaller region, and the actual
bottom-margin scroll. VTE's `line_feed()` and Ghostty's `index` implementation
make the same distinction, so it is a public cursor invariant rather than a
Contour-internal guard.

The two omitted/zero one-based cases retain CUP/HVP, CHA/VPA, HPA/HPR, all
four cursor directions, and ICH/DCH/ECH as separate parameter boundaries.
xterm's `one_if_default`, VTE's one-based coordinate conversion, and
Ghostty's explicit zero-count-to-one normalization agree with these
observable results.

The next three rectangular-area cases retain omitted source coordinates,
bottom/right clamping for fill and both erase variants, and DECOM-relative
coordinates. xterm's rectangle parser and VTE's `collect_rect()` use the same
defaults, clamp and origin model; the scenarios exercise Shitty through the
actual DECCRA/DECFRA/DECERA/DECSERA streams.

The following DECCRA edge case retains the partial copy at the bottom-right
corner. xterm snapshots the source and bounds every target cell; Shitty clips
the copy dimensions before writing, so the scenario checks both copied cells
and every untouched preceding row.

DECDC is retained as an xterm/VT420 compatibility and sparse-row safety case:
the default count deletes one column from each row of the vertical region,
including rows that have not yet been materialized by text output.

The next three title cases use their PTY-visible `OSC L`/`OSC l` reports:
OSC 0/1/2 title independence, the two title queries, and every optional-pair
stack boundary of XTPUSHTITLE/XTPOPTITLE (including bounded depth). This is an
xterm extension, not a VT/ECMA sequence. Its complete icon/window-pair stack
semantics agree in xterm and Contour, which are the implementations supporting
that full feature. Alacritty, Ghostty, Kitty, VTE and foot maintain only a
window-title stack; iTerm2 keeps separate icon and window stacks rather than
one stack of optional pairs. Those subsets do not vote on the pair semantics.
The scenarios use enabled window operations rather than inspecting Shitty's
internal title state.

`CSI 8 ; height ; width t` and DECSLPP are tested through their independent
`CSI 18 t` grid-size report. They are an xterm extension rather than an
ECMA-48/VT standard. Contour, xterm, VTE and iTerm2 implement a resize with
both dimensions and agree on its result; Kitty rejects it, Ghostty implements
only size *reports*, foot logs it as unimplemented, and Alacritty has no
terminal-side handler. The test therefore retains only the shared `8;h;w`
form and the shared DECSLPP dispatch, not Contour's omitted-dimension rule.

The two special-color cases use OSC 5/105 queries and the OSC 4;256 alias.
This is also an xterm extension, with Contour and xterm as the supporting
implementations and matching wire behaviour. Ghostty and VTE recognise it
but intentionally leave special colors unsupported; Kitty explicitly ignores
it; foot and iTerm2 implement OSC 4/104 only; Alacritty has no OSC 5/105
handler. Thus their non-support is not treated as a contrary vote. A bare
OSC 104 is nevertheless excluded: VTE and Ghostty define it as a palette-only
reset, matching Shitty's public behaviour.

`DECXCPR` and `DECCKSR` have a separate supporting group: xterm, Contour,
VTE and iTerm2. They agree on page 1 in the extended cursor report and on
echoing the checksum request id with `0000` when no macro memory exists.
Alacritty, Ghostty, Kitty and foot do not implement these private VT420
reports, so do not vote on their response shape.

The DECSLRM backspace boundary is shared by xterm, Contour, Ghostty, VTE and
iTerm2. Reverse-wrap mode 45 is an xterm extension implemented by xterm,
Contour, Ghostty, iTerm2 and foot; its extended mode 1045 is implemented by
xterm, Contour and Ghostty. Alacritty and Kitty implement neither extension,
and VTE does not implement reverse wrap, so the CPR scenarios assert only the
forms on which the supporting implementations agree.

The image-placement, DECDWL and hyperlink delta cases are mapped to Shitty's
renderer update boundary rather than Contour's private `GridDelta` cursor.
DEC's Sixel specification defines the raster dimensions, while the cell-row
coverage follows the terminal's cell geometry. xterm, Contour, iTerm2, VTE and
foot implement Sixel; Alacritty, Ghostty and Kitty do not and therefore do not
vote. Contour's 100x100-pixel fixture covers ten rows with its configured
10-pixel cells. The same fixture covers exactly nine rows in Shitty's 12-pixel
Sixel patch geometry, and the scenario verifies that precisely those nine rows
reach the renderer.

The sized-text delta case remains an executable expected-failure oracle in
`test_kitty_text_sizing.py`: Kitty's OSC 66 protocol specification, Kitty and
Contour agree that `s=2:w=2` creates a two-row block and therefore changes both
its head and continuation row. Ghostty parses OSC 66 but labels its terminal
callback unimplemented; foot supports only `w` and explicitly rejects `s`;
Alacritty, xterm, iTerm2 and VTE have no OSC 66 implementation. Those partial
or absent implementations do not vote on the vertical-scale semantics. Shitty
does not yet have the parser/grid/rendering representation, so this source case
is preserved as a failing target rather than legitimising the current no-op.

DECDWL is defined
by the VT100 specification; xterm, Contour and iTerm2 implement the same
double-width line attribute. VTE recognises it but deliberately leaves it
unimplemented, while Alacritty, Ghostty, Kitty and foot have no DECDWL
operation, so those implementations do not vote. The DECDWL scenario verifies
that changing an otherwise empty line still publishes that line and its new
attribute to the renderer.

The OSC 8 protocol specification
defines an active hyperlink spanning subsequently printed cells and an empty
URI as its terminator.  Alacritty, Ghostty, Kitty, Contour, iTerm2, VTE and foot
all implement that cell association; xterm has no OSC 8 implementation and
therefore does not vote.  The scenario verifies that the single damaged row
reaches the renderer and that both rendered cells retain the same nonzero
hyperlink id and resolve to the original URI after the update cycle.

The final delta case crosses a private boundary. Contour's `OSC 133;B` stores a
`PromptEnd` offset on the logical line head and its daemon-facing `GridDelta`
must publish the changed history row. No such object or history-delta stream
exists in Shitty. The semantic-prompts specification defines the public effect
of `B` as ending the prompt and starting user input; it does not prescribe a
retroactive line-head marker. Ghostty and VTE implement that transition as the
attribute for subsequently printed cells. Kitty and foot accept `B` without a
separate row mutation. iTerm2 updates a prompt mark created earlier by `A`,
while Contour also creates its `PromptEnd` metadata from `B` alone. Alacritty
and xterm do not implement OSC 133 and do not vote. The transferred scenario
therefore uses the protocol-valid `A` ... `B` sequence: the wrapped prompt head
is already in scrollback when `B` arrives, the next cell is classified as user
input, and revealing the history shows that both prompt rows retained their
semantic cells. It does not invent a renderer event for Contour's private
off-screen metadata mutation.

The remaining MultiPage cases are likewise retained as public page-1
scenarios: DECSC/DECRC, DECCRA, alternate screen, reset, content continuity,
margin isolation, resize, and RIS all run against Shitty's one real screen
instead of a fabricated inaccessible page store.

`test_contour_terminal.py` starts the 144-case `Terminal_test.cpp` inventory
with its first three cases. The blinking scenario retains the common public
timer invariant: a blinking cursor advances through both visible and hidden
phases. Contour, Alacritty, Kitty and VTE additionally make raw keyboard input
restart the visible phase. Ghostty and foot restart it on subsequent PTY
output, iTerm2 temporarily makes the cursor solid after terminal output or
cursor movement, and xterm has no corresponding raw-input reset. DEC mode 12
and DECSCUSR select blinking but do not specify timer phase or input-triggered
restart. The Contour-only raw-key assertion is therefore not treated as an
oracle where the implementations do not agree.

The IME scenario keeps the observable failure boundary rather than Contour's
private optional render-cursor object: preedit text remains rendered when the
ordinary blinking cursor is in its hidden phase. Alacritty suspends ordinary
cursor blinking while preedit exists; Ghostty and Kitty render independent
preedit state; VTE explicitly invalidates the cursor during preedit even when
that cursor is otherwise invisible; foot renders both preedit cells and its
preedit cursor; and iTerm2 replaces the ordinary cursor with dedicated marked
text and IME-cursor rendering. xterm's XIM path delegates composition through
its cursor-positioned preedit support. There is no terminal wire standard for
frontend IME composition, so all eight implementations were checked at their
frontend/rendering boundary.

The modifier-key scenario exposed a Shitty regression: emitting a reported
Kitty modifier packet used the same user-input path as Enter and reset a
scrolled viewport. Contour, Alacritty, Ghostty, Kitty and foot all implement
report-all modifier events and preserve the viewport for left/right
Shift/Control/Alt/Super while still writing the packet. VTE filters modifier
events before its encoder, while xterm and iTerm2 do not implement this
report-all behaviour, so those three do not vote on the viewport effect.
Contour, Kitty and foot also preserve the viewport for CapsLock and NumLock;
Alacritty and Ghostty classify only the eight momentary keys for this UI
policy. The Kitty protocol itself classifies CapsLock and NumLock as modifier
state and requires modifier-key events in report-all mode, although it does
not prescribe viewport policy. Shitty follows the supporting majority for
those two lock keys. ScrollLock is not included: the protocol does not classify
it as modifier state and the implementations have no consensus. Ordinary
non-modifier input still resets the viewport.

The next `Terminal_test.cpp` block adds three more accounted cases. Contour's
`localPathAtMousePosition()` resolves relative matches against the OSC 7
working directory, accepts existing absolute paths (including `~` inside a
component), and rejects nonexistent files. Ghostty has the same built-in path
matcher and CWD-relative resolution, while iTerm2's Semantic History performs
the same existence-checked resolution. Alacritty exposes configurable hints,
Kitty exposes its separate path-hints kitten, and VTE lets embedders install
match regexes, but none of those three provides this terminal-owned
mouse-point lookup. foot and xterm detect URIs only. No terminal protocol
standard defines local-path discovery. The supporting implementations agree,
so the scenario is retained as an executable expected failure: Shitty's
current pointer API recognizes scheme URIs but does not yet retain OSC 7 CWD
for bare-path resolution.

`AutoScrollOnUpdate` is adapted to the independently observable policy shared
by Alacritty, Ghostty, Kitty, VTE, foot and iTerm2, and obtainable from Contour's
disabled output-autoscroll setting: new PTY output keeps the historical view
anchored, while typed key or text input returns to the live bottom. xterm
offers the two policies as independent resources with opposite defaults.
There is no applicable wire standard. Contour's direct
`bufferChanged()` callback is private and is not simulated. Its reported key
release rule is also excluded: Contour and Kitty preserve the viewport,
Alacritty, Ghostty and foot reset it after an encoded non-modifier release,
VTE does not forward such releases, and xterm/iTerm2 do not implement that
Kitty report-event path. There is no consensus to use as an oracle.

The DECCARA scenario explicitly sends `DECSACE 2` before asserting a rectangle.
DEC defines the initial DECSACE value as stream extent; xterm, VTE, Kitty and
iTerm2 agree, while Contour initializes its field to rectangle despite its own
handler documenting the stream default. The transferred assertion therefore
does not preserve that Contour bug. xterm, Contour, Kitty, VTE, iTerm2 and foot
all support DECCARA's standard bold/underline changes and agree that cell text
is untouched; Alacritty and Ghostty do not implement it and do not vote. The
Contour fixture's truecolor parameter is deliberately omitted from this
cross-terminal oracle: Contour, Kitty and VTE accept full SGR there, while
xterm, foot, iTerm2 and the DEC standard retain the smaller rendition subset.

`Terminal.CaptureScreenBuffer` uses Contour's private `XTCAPTURE`,
`CSI > Ps ; Ps , t`, and returns APC payloads tagged 314. The Contour source
itself labels that wire format an extension. No matching request or APC reply
exists in Alacritty, Ghostty, Kitty, xterm, iTerm2, VTE or foot, and no DEC or
xterm control-sequence specification defines it. Since there are no other
supporting implementations, Shitty does not acquire a remote screen-extraction
protocol. The executable scenario verifies the intentional public boundary:
the unknown request changes neither screen nor viewport and emits no PTY reply.

Both following RIS cases are represented by one end-to-end encoder scenario.
DEC defines RIS as reset to initial state, and Alacritty, Ghostty, Kitty, xterm,
iTerm2, VTE, foot and Contour all implement a hard/full reset. Shitty first
emits application-cursor `SS3 A` under DECCKM, then RIS clears both the screen
and the mode, and the same frontend key emits normal `CSI A`. This checks the
mode register and its input-generator mirror without an internal getter.
Contour's status-display assertion is not fabricated because Shitty has no
status-display screen. Its `frozenModes` replay is likewise a Contour settings
facility, absent from the other seven terminal cores and from DEC; the normal
unfrozen reset is the portable contract.

The next transfer accounts for `Terminal_test.cpp` cases 10 through 29 as one
20-case block. DEC private mode 2029 is Contour's passive-mouse extension. No
matching mode or report shape exists in Alacritty, Ghostty, Kitty, xterm,
iTerm2, VTE or foot, and neither DEC nor xterm specifies it. The executable
boundary therefore verifies that Shitty reports the mode as permanently
unsupported both before and after RIS; it does not add a one-implementation
input protocol.

Mouse coordinate modes use one active encoding. xterm documents 1005, 1006,
1015 and 1016 as mutually exclusive and its `extend_coords` reset only clears
the matching active value. Contour, Ghostty, Kitty and foot implement the same
single-enum rule. Alacritty implements the 1005/1006 subset, iTerm2 implements
1005/1006/1015, and VTE implements its supported subset through one mouse
tracking mode; those implementations agree for the modes they support. The
xterm control-sequence specification is the applicable standard. The imported
case exposed and fixes Shitty's unconditional-reset bug: the existing parser
callback now carries the set/reset bit, and Vterm clears an encoding only when
that encoding is active.

`forceRedraw()` and `clampedTotalPageSize()` are Contour C++ APIs, not terminal
protocols. Alacritty, Ghostty, Kitty, xterm, iTerm2, VTE and foot have no such
call. Their frontends all derive the PTY pixel size from page cells and the
current cell metrics, and none gives an application a wire request for
Contour's transient one-column widening. The public scenarios retain the two
portable invariants: a widen/restore round trip keeps a 9x18 cell, and a pixel
resize floors the page at one cell. Shitty has no DEC status-display screen, so
the status-line branch of Contour's clamp is not fabricated.

The four column-mode cases separate their real wire contracts from Contour's
status-line bookkeeping. DEC/xterm DECCOLM support is shared by all eight
implementations, and the transferred scenarios require a column change to
leave the row count alone, take effect before following output, and return to
80 columns on RIS. DECSCPP is specified by the VT340/VT525 manuals and
implemented by Contour and xterm; iTerm2 parses it behind its resize-security
policy, while VTE deliberately registers it as a NOP and Alacritty, Ghostty,
Kitty and foot do not implement it. The supporting implementations and the DEC
standard agree that `CSI 132 $ |` selects 132 columns without adding a row, so
Shitty's missing handler is an executable expected failure. The extra
Contour-only assertion about double-counting an indicator row is represented
by the unchanged row-count contract rather than a synthetic status screen.

DECNCSM is a VT500-level DEC mode: after `DECSCL 65`, setting mode 95 preserves
page memory across DECCOLM and its reset/default clears it. Contour, xterm and
the DEC specification agree; the other implementations either follow that
observable rule where they expose mode 95 or do not support the mode and do not
vote. The scenario explicitly enters VT500 compatibility instead of relying on
Contour's test fixture default. Synchronized output mode 2026 is implemented
by Alacritty, Ghostty, Kitty, Contour, VTE and foot, with compatible support in
iTerm2; xterm does not provide the same frame-transaction contract and does not
vote. The test observes Shitty's rendered snapshot: intermediate writes remain
hidden until the matching reset publishes the complete frame.

XTPUSHCOLORS/XTPOPCOLORS are implemented by xterm, Contour, Kitty, foot and
iTerm2 and specified by xterm. VTE recognizes them as explicit NOPs;
Alacritty and Ghostty do not implement them. The supporting implementations
agree that a pop restores the palette captured by the push, so the missing
Shitty color stack is kept as an expected failure using OSC 4 and a rendered
indexed-color cell.

DECAC, DECATC and DECSTGLT come from the VT525 specification. Contour and xterm
implement all three. VTE parses all three but leaves them unimplemented;
Alacritty, Ghostty, Kitty, iTerm2 and foot do not expose the VT525 alternate
text-color table and therefore do not vote. Shitty already implements DECAC
item 1, so its scenario verifies assigned default foreground/background and a
bare reset. DECATC and DECSTGLT remain separate expected failures: alternate
mode must map bold text through its assigned palette pair and switching back
to ANSI mode must stop doing so. Contour's additional status-line test concerns
host-owned chrome. Shitty has one application screen and no DEC status line;
the executable boundary verifies that the private sequences neither create a
row nor disturb existing page text.

The final five cases in this block are frontend/model behavior rather than
vendor protocols. ISO-8613-style colon SGR underline variants are implemented
by Alacritty, Ghostty, Kitty, Contour, iTerm2, VTE and foot; current xterm also
recognizes the extended underline styles. The test verifies that single,
double and curly underline replace one another and that SGR 24/reset clears
the active style. All eight implementations model a wide glyph as one copied
grapheme plus a continuation cell and their selection extractors preserve
selected line boundaries. No wire standard governs selection, but the checked
implementations agree on these UI semantics. The three selection scenarios
therefore require no padding after a wide character, newlines in a one-column
rectangular selection, and leading selected blank lines. The curly-underline
case also pins that style 3 does not imply italic and resets independently.

The next transfer accounts for `Terminal_test.cpp` cases 30 through 49 as one
20-case block. The two selection cases are exercised through actual frontend
pointer press, motion and release events, including the half-cell endpoint
rule: dragging from row 2 into row 3 extracts `7890\nABC`, release preserves
the extracted text, a plain click clears it, and a soft-wrapped boundary does
not insert a newline. Contour, Alacritty, Ghostty, Kitty, xterm, iTerm2, VTE
and foot all join soft-wrapped rows when copying a linear selection. No wire
standard specifies a GUI selection, but the eight implementations agree on
the observable result.

`ParsingBuffer` and `TrivialLineBufferIntegrity` name private Contour storage
objects, so their portable failure boundary is the byte stream rather than an
invented Shitty buffer getter. The tests split UTF-8 and CSI across independent
PTY chunks, then verify the decoded box-drawing scalar and the following SGR
cell; the separate ASCII fast-path case verifies every byte of the line. The
box-drawing regression sends the original three-byte UTF-8 characters. All
eight checked parsers are incremental byte-stream parsers and preserve these
results across input chunking; UTF-8 itself is the applicable standard.

The remaining fifteen cases include two inert animation-progress getters and
the precision-scrolling and momentum block. The scrolling feature is
deliberately retained even though the implementations divide its work
differently. Contour and current Kitty render a sub-cell viewport and
run their own inertia; Kitty's `kitty/mouse.c` and
`glfw/momentum-scroll.c` use a different sampler, thresholds and decay from
Contour. VTE keeps a fractional `scroll_delta` and delegates kinetic motion to
`GtkScrolledWindow`. iTerm2 consumes precise `NSEvent.scrollingDelta` and
`momentumPhase` through `iTermScrollAccumulator`. Ghostty carries precision
scroll state through `Surface.zig`, and its Cocoa frontend receives native
momentum while GTK supplies kinetic scrolling. Alacritty consumes Winit
`PixelDelta` plus `TouchPhase` and accumulates it into terminal rows. foot
accumulates Wayland `value120` into line steps, while xterm has no comparable
precision-viewport input path; those two do not vote on pixel rendering or
inertia. There is no terminal wire standard for this frontend behavior. The
Wayland core protocol is an applicable frontend standard: it identifies
`finger` as a smooth source with possible kinetic scrolling and defines
`axis_stop` specifically so a client can begin that kinetic motion. It does
not prescribe the renderer, sampler or decay constants.

Shitty's frontend event had previously discarded exactly the information
needed to represent the imported cases. `ScrollInput` now carries phase,
precision, native-momentum and timestamp fields. Cocoa maps `NSEvent.phase`
and `momentumPhase`; Wayland maps finger `axis_source`, event time and
`axis_stop`. The fiber input sink and Composer router tests pin lossless
delivery. Native momentum packets already traverse Vterm and are tested as a
decelerating, non-reversing sequence ending at an explicit `End` packet.
This covers the user-visible feature when the window system owns inertia; it
does not pretend that Contour's velocity formula is universal.

Pixel viewport rendering is still missing. Executable expected failures
require a fractional event to alter the rendered frame without first changing
the integer history row, preserve a remainder after a full-cell step, clear
that remainder at the history boundary, and keep a fractionally anchored view
stable when PTY output appends rows. The alternate-screen case passes and pins
the supporting Contour/Kitty rule that local pixel history scrolling is
disabled there. A separate expected failure sends `Begin`, rapid `Update`
samples and `End`, then advances the animation system: Shitty does not yet
synthesize inertia for a platform such as Wayland which supplies the gesture
end but no native momentum packets.

Contour's exact `sum(delta)/duration` velocity, its 50 px/s start threshold,
the requirement for two samples, and its 0.05-per-second friction are not used
as cross-terminal constants. Kitty permits a qualifying single sample and
uses weighted samples with 0.96 friction; Cocoa and GTK own those parameters
outside the terminal core. The imported cases therefore retain the functional
contracts—fast gesture continuation, slow-gesture rejection, progressive
deceleration and an eventual stop—while native momentum packet tests avoid
choosing one vendor's private numerical model. The no-transition and
no-cursor-animation cases likewise assert stable complete public state instead
of exporting Contour's private progress getters.

The next transfer accounts for `Terminal_test.cpp` cases 50 through 69 as one
20-case block. The source revisions checked for this block were Contour
`9f2b296`, Alacritty `1b2b36a`, Ghostty `156bc8c`, Kitty `fda3a9a`, xterm
`6380a3e`, iTerm2 `3ec5786`, VTE `3d55bbd` and foot `a635e0a`.

The discrete-wheel glide is not misrepresented as an eight-terminal
consensus. Contour's `injectWheelMomentum()` is the only checked backend that
turns a low-resolution wheel notch into its own decaying animation. Kitty's
`glfw/momentum-scroll.c` explicitly passes non-finger and non-high-resolution
events through, while `kitty/mouse.c` applies wheel V120 input immediately;
its own inertia is for finger-based high-resolution input. Alacritty applies
`LineDelta` immediately and accumulates Winit `PixelDelta` into rows. Ghostty
normalizes both precision and nonprecision input in `Surface.zig` and carries
native momentum metadata from the platform. iTerm2's
`iTermScrollAccumulator` distinguishes an immediate mouse wheel from AppKit
trackpad phases. VTE keeps a fractional adjustment and lets
`GtkScrolledWindow` provide optional kinetic motion. foot accumulates
continuous/value120 axes into immediate lines and clears its remainder on
`axis_stop`; xterm maps buttons 4 and 5 directly to line scrolling.

There is no terminal protocol for this GUI policy. The Wayland pointer
specification is the applicable concrete standard: a finger source lives in a
continuous coordinate space and `axis_stop` lets a client begin kinetic
scrolling, while wheel sources need not send a stop. It neither requires
inertia for a wheel nor specifies a decay curve. Even though Contour alone
implements this exact wheel animation, the ten wheel scenarios remain
executable rather than being discarded: one-notch progression, accumulated
notches, signed reversal, native-momentum independence and frame scheduling
are expected failures; history clamping, alternate-screen fallback,
scroll-to-bottom reset,
zero-sum input and caller fallthrough pin Shitty's current public branches.
This retains an implemented feature whose mechanisms differ elsewhere without
promoting Contour's private constants to a standard.

Gesture cancellation has independent support beyond that wheel policy.
Contour cancels momentum on a new begin, a first stray update, reset and
resize. Kitty cancels its current synthetic momentum when a new physical
event arrives and when the owning window/input context changes. AppKit exposes
separate physical and momentum phase streams to iTerm2 and Ghostty; VTE
explicitly toggles GTK kinetic scrolling off before programmatic adjustment.
The transferred scenarios therefore require stale native-momentum packets to
be rejected after a new physical begin or resize. Shitty carries the phase and
source bits but does not yet enforce stream ownership, so both are expected
failures. An explicitly cancelled physical stream and an unphased legacy
stream are also tested to remain stopped. The latter is the public observable
result of Contour's private `momentumScrolling=false` and
`smoothScrolling=false` branches; no Shitty configuration key is invented to
mirror another application's internal settings object.

All eight implementations keep terminal grids nonempty. Kitty's `screen.c`
clamps lines and columns with `MAX(1u, ...)`; iTerm2's window-size helper
clamps both dimensions to one; Alacritty uses nonzero display sizes; Ghostty
has explicit one-row/one-column page storage; VTE, foot and xterm all rely on
positive row and column counts at their grid boundary. The plain 1x1 resize
scenario consequently passes and also verifies growth back to 20x10.

The status-line variant has a narrower but concrete oracle. DEC VT320/VT525
defines DECSSDT and DECSASD, and Contour and status-line-enabled xterm
implement a separate indicator/host-writable row. Ghostty parses the commands
only to black-hole status-directed output, VTE deliberately leaves both as
NOPs, and Alacritty, Kitty, iTerm2 and foot do not implement the DEC display;
those six do not vote for its layout. The supporting implementations and the
DEC specification require an indicator status display to coexist with a
nonempty main page. The transferred expected failure resizes the main page to
1x1 and queries DECSSDT through DECRQSS, expecting `1$r1$~`. This records the
real missing protocol instead of fabricating a private status-row getter.

Finally, the native packet scenarios keep platform-owned inertia distinct
from synthesized inertia. A complete native Begin/Update/End stream is
accepted, two streams can run independently, and an unphased precision stream
never arms continuation. A fast physical gesture ending without native
momentum is still expected to schedule an animation frame on platforms such
as Wayland; Shitty does not yet synthesize that continuation, so the frame
request remains an expected failure.

The next transfer accounts for `Terminal_test.cpp` cases 70 through 89 as one
20-case block. The source revisions checked for this block were Contour
`9f2b296`, Alacritty `1b2b36a`, Ghostty `156bc8c`, Kitty `fda3a9a`, xterm
`6380a3e`, iTerm2 `3ec5786`, VTE `3d55bbd` and foot `a635e0a`.

The three remaining momentum cases retain gesture lifetime rather than
Contour's numerical formula. Contour synthesizes motion after a sufficiently
fast precision gesture, restarts it for each rapid gesture and cancels it on
an alternate-screen switch. Kitty independently synthesizes inertia for
finger/high-resolution input and ties the active stream to its window and
main-screen identity, cancelling it when a new physical stream or a different
screen owner takes over. iTerm2 and Ghostty consume AppKit's distinct physical
and momentum phases; VTE uses GTK kinetic scrolling. Alacritty and foot
accumulate precision input without a terminal-owned decay, and xterm has no
precision gesture stream, so those three do not vote for synthesis. The
Wayland pointer specification again supplies the frontend lifetime rule:
finger axes may be followed by kinetic motion after `axis_stop`, but it does
not define velocity or friction. The imported expected failures consequently
require rapid gestures to replace—not stack with—the previous continuation,
require one final continuation to advance and settle, and reject a stale
native momentum update after an alternate-screen round trip.

Cursor motion animation has independent implementations with deliberately
different visuals. Contour interpolates the cell position for a configurable
duration; Kitty's current renderer implements `cursor_trail` with configurable
decay and thresholds; iTerm2 implements both smooth cursor sliding and smear
animation. Alacritty, Ghostty, xterm, VTE and foot render the new cursor
position immediately and do not vote. No terminal wire standard controls this
presentation. The two scenarios therefore test the shared public behavior—a
cursor move schedules later frames, and a second move while those frames are
active retargets the animation—without importing any duration, curve or
private progress getter. Both remain expected failures in Shitty.

Primary/alternate fade is narrower. Contour alone among the eight checked
terminals implements a transition specifically for a DEC primary/alternate
screen switch, with configurable style and duration. Kitty has a custom
renderer `tab-change` transition, but that animates a tab switch rather than
DECSET/DECRST 1049 and therefore does not vote for this operation. Alacritty,
Ghostty, xterm, iTerm2, VTE and foot switch the DEC screens without such a
transition. No DEC or ECMA-48 rule prescribes how this GUI change is drawn.
The five Contour scenarios are nevertheless kept as executable expected
failures instead of dropping a one-vendor implemented feature: switching must
schedule frames, fade-out must change the presented image and move cell color
toward the background, the sequence must terminate, and a distinct fade-in
frame must be reached. The assertions use images and frame scheduling only;
they do not expose Contour's private progress values or its 200 ms test
constant.

Selection clearing, endpoint extension and autoscroll have a broad GUI
oracle, although no terminal wire standard specifies them. Contour, Ghostty,
Kitty, xterm, iTerm2 and VTE support extending an existing selection from a
modified click and choose an endpoint according to the click position. foot
implements the same nearest-end extension under its default right-click
binding rather than Shift-click. Alacritty starts a fresh selection for its
corresponding modified left click and does not vote on extension. All eight
implement selection dragging across scrollback and clear completed selection
state safely; their event bindings and timer placement differ, but active
drag autoscroll, history clamping and completed-selection stability agree.
The imported tests exercise these results through frontend pointer and scroll
events. Direct clear/availability checks use only `TestApi` commands wired to
Shitty's existing `VtermImpl::selectionClear()` and `hasSelection()` helpers;
the production `Vterm` interface is unchanged.

Contour's private passive mouse tracking mode 2029 has no consensus oracle.
VTE names 2029 as `CONTOUR_MOUSE_PASSIVE_TRACKING` in its mode table but does
not implement the forwarding behavior; Alacritty, Ghostty, Kitty, xterm,
iTerm2 and foot do not implement the mode at all, and no DEC/xterm standard
defines it. The scenario therefore records the intentional capability
boundary: DECRQM reports `?2029;0$y`, while the independently supported Shift
override still creates a local selection without emitting a fabricated SGR
application packet. It does not approximate Contour's simultaneous
handled-result and mouse-report semantics.

The final three cases are governed by the Kitty keyboard protocol itself.
Kitty, Contour, Alacritty, Ghostty, iTerm2 and foot implement meaningful Kitty
keyboard event-type reporting and agree that an Up press under flags 3 is
`CSI A`, its release is `CSI 1;1:3 A`, and Ctrl+A uses `CSI 97;5 u` followed
by `CSI 97;5:3 u`. They also suppress release output when the Report Event
Types flag is absent. VTE only recognizes an older private mode name without
an encoder path, and xterm does not implement the protocol, so neither votes.
All three imported scenarios pass on both Shitty parser backends.

The next transfer accounts for `Terminal_test.cpp` cases 90 through 109 as a
single 20-case block. The source revisions checked for this block were Contour
`9f2b296`, Alacritty `1b2b36a`, Ghostty `156bc8c`, Kitty `fda3a9a`, xterm
`6380a3e`, iTerm2 `3ec5786`, VTE `3d55bbd` and foot `a635e0a`.

The Kitty repeat case follows the protocol rather than a Contour-private
encoder detail. With Disambiguate Escape Codes and Report Event Types active,
Kitty, Contour, Alacritty, Ghostty, iTerm2 and foot encode an Up repeat as
`CSI 1;1:2 A`. VTE and xterm have no Kitty event-type encoder and abstain. The
new scenario complements the immediately preceding press/release cases and
passes on both Shitty parser backends.

The three top-anchored region regressions separate grid scrolling from history
creation. ECMA-48 SU scrolls the active area without moving the active cursor;
the checked implementations likewise use a partial-region path that does not
manufacture a full-page history line. Contour and Alacritty additionally keep
their normal/Vi cursor independent from a region that does not contain it.
Shitty has no normal-mode cursor, so that private coordinate is represented by
an existing frontend selection endpoint outside the region; it remains fixed.
The two viewport-count cases expose the actual Shitty defect: both SU and IND
over a top-anchored partial region increase an already scrolled `view_offset`
by one even though no history line was added. They are retained as expected
failures, including the bounded-history variant that rules out spare-capacity
effects.

Cursor-line highlighting is not discarded just because its activation differs.
Contour enables a full-row guide in normal mode; iTerm2 exposes the same visual
feature as Cursor Guide and accepts the public
`OSC 1337;HighlightCursorLine=0/1` control. Ghostty recognizes that iTerm2 key
but deliberately leaves it unimplemented, Alacritty only styles the Vi cursor
cell, and Kitty, xterm, VTE and foot provide no full-row guide; those six do not
vote on the enabled feature. Shitty ignores the public control, so the two
enabled plain-line scenarios are expected failures, while explicit disable in
insert mode passes. Contour's yank range is adapted to the common public
selection operation: a configured selection background recolors the selected
plain line and leaves its sibling unchanged. All eight implementations provide
that observable selection behavior.

The four grapheme scenarios retain both Unicode clustering and rendered cell
geometry. Unicode Standard Annex #29 supplies the cluster boundaries. Contour,
Alacritty, Ghostty, Kitty, VTE, foot and iTerm2 preserve the tested
emoji/VS/ZWJ and decomposed alpha clusters as a single one- or two-column cell;
xterm has no comparable grapheme-cell representation and does not provide a
contrary oracle. The model-side layouts are already correct in Shitty, but
selecting any of the five lines changes the actual rasterized glyph geometry
even when selected and unselected foreground and background colors are made
identical. The pixel-level comparison is therefore an expected failure for
every upstream subcase, rather than a private `RenderPath` assertion.

Contour's last-column wide-cell expectation is the outlier. Alacritty inserts
a leading wide spacer and wraps, Ghostty and foot pad then wrap before writing
the two cells, Kitty wraps whenever `cursor + width` exceeds the page, VTE
wraps before a glyph crossing the right edge, and iTerm2 records `EOL_DWC` plus
`DWC_SKIP` before placing the glyph on the next row. xterm is the compatibility
target explicitly cited by the Ghostty and iTerm2 implementations. The port
uses that consensus: the old row has a soft-wrap marker, the complete glyph
and continuation occupy the next row, and selecting it returns the glyph with
no synthetic padding. The following fallback test compares public pixels after
a cursor on a wide versus narrow final cell and confirms that cursor colors do
not leak into the next selected line. The final cluster test checks the public
model cell and continuation instead of Contour's private batched-line flag.

All eight implementations provide alternate-screen wheel-to-cursor behavior,
although policy and defaults differ. xterm mode 1007 is the protocol control;
Contour, Ghostty, VTE, foot, Alacritty and iTerm2 expose corresponding policy,
while Kitty performs the same alternate-screen fallback whenever application
mouse tracking is inactive. The ports exercise both Shitty activation routes:
its existing `-altScroll` setting for the no-protocol policy and DECSET 1007
for application control. DECCKM alone selects the encoding. Current xterm,
Ghostty, VTE, foot and Kitty consequently emit `CSI B` in normal cursor mode
and `SS3 B` after `DECSET 1`; Contour's test-specific expectation that 1007 by
itself selects SS3 is not adopted. Application SGR mouse tracking takes
priority in every implementation, and a primary-screen wheel remains local.

Two modal frontend branches remain missing. Contour and Alacritty let Shift
bypass alternate-scroll injection, and Contour's normal mode, Alacritty's Vi
mode and iTerm2's copy mode consume scrolling locally rather than typing into
the child. The public adaptation uses an active frontend selection as Shitty's
available modal state. Shitty currently emits cursor keys in both situations,
so both scenarios are expected failures. Finally, Contour, Kitty, Ghostty,
foot and VTE all repeat generated keys according to the normalized wheel
amount. Contour stores its multiplier inside `Terminal`; Shitty receives the
already scaled delta from its frontend, so a magnitude-three event verifies
the same three-key output without inventing a new configuration option.

The next transfer accounts for `Terminal_test.cpp` cases 110 through 129 as
one 20-case block. The source revisions checked for this block were Contour
`9f2b296`, Alacritty `1b2b36a`, Ghostty `156bc8c`, Kitty `fda3a9a`, xterm
`6380a3e`, iTerm2 `3ec5786`, VTE `3d55bbd` and foot `a635e0a`.

Lock modifiers have two deliberately different paths. Ghostty and foot strip
CapsLock and NumLock before legacy binding and key encoding, while retaining
them for the Kitty encoder; Kitty's protocol assigns bits 64 and 128 to the
two locks. Contour follows the same split. Kitty and iTerm2 implement the
positive Kitty reporting path, and xterm, VTE and foot independently preserve
NumLock for numeric-versus-application keypad choice. Alacritty's frontend
does not expose CapsLock to its Kitty encoder and therefore abstains on that
positive packet, but its legacy bindings are likewise independent of lock
state. xterm and Contour implement DECUDK; VTE recognizes the sequence as a
NOP and the remaining terminals abstain. The transferred sweep covers every
printable ASCII character and every frontend F/keypad key in both legacy and
modifyOtherKeys mode 2, then separately proves NumLock keypad selection,
DECUDK dispatch and exact Kitty lock packets. Shitty has semantic URL
selection and a platform copy chord rather than labelled hint input; the
corresponding UI-action regression therefore verifies that the copy action is
consumed under all lock combinations and emits no child input.

DEC private mode 9001 is a ConPTY protocol rather than a DEC mode. Contour is
the only one of the eight checked implementations with its Win32 input-record
encoder; the other seven abstain. The Microsoft ConPTY packet definition is
the external specification, and it requires both Escape's Unicode value and
the NumLock keypad character to be carried. Shitty does not implement 9001,
so the two exact records remain one executable expected failure.

All eight terminals implement linear selection, and Contour, Ghostty, xterm,
iTerm2 and VTE expose a direct select-all operation. xterm's selection range,
VTE's `select_all`, and iTerm2's `PTYTextView::selectAll` include saved lines;
Kitty exposes the same screen-plus-scrollback extent through extraction and
selection operations. Alacritty and foot have no direct select-all command
and abstain on the command itself. Shitty likewise has no select-all API, so
the port drives the same range through its public drag operation: the anchor
is placed at the top of scrollback and the completed endpoint at the bottom
of the live page. Separate tests retain completion in insert mode, the
pointer endpoint on a blank same-row suffix, the full first row of a
multi-row selection, and per-cell selection rendering on a scrolled trivial
line. No GUI selection rule is specified by ECMA-48; agreement among the
supporting implementations is the oracle here.

Contour's internal DECMode map is represented at Shitty's wire boundary.
DECSET/DECRST and DECRQM are the two directions defined by the DEC controls,
and every mutable mode exposed at Shitty's default compatibility level is
set, reset and queried in an isolated terminal. DECANM is queried in its
default state because resetting it enters VT52 mode, where the ANSI query is
no longer syntax. Numbers 38 and 44 remain explicitly unrecognized. Mode 95
is intentionally omitted from the mutable sweep at the default VT400 level:
Shitty exposes it only at VT500 compatibility and correctly reports unknown
at the lower level.

Passive mode 2029 and Contour's interactive buffering trace are both
one-implementation features among the eight. VTE lists 2029 but does not
implement its simultaneous report-and-decline behavior; the other six do not
recognize it. Likewise, ordinary parser traces exist elsewhere, but none
provides Contour's user-stepped sequence queue. Both features are kept as
expected failures: passive mouse input must both emit an SGR report and leave
the frontend free to select, while an APC must remain owned and queued behind
the preceding CUP instead of executing immediately.

Focus reporting has the opposite consensus. xterm defines DECSET 1004 and all
eight implementations gate `CSI O`/`CSI I` on it while still tracking local
focus. The port checks both the silent disabled state and the exact enabled
packets. The adjacent bounds regression hardens the public snapshot page in
the same way as the underlying grid: right and bottom are exclusive and
negative page coordinates never alias Python's last row or cell.

Fatal PTY output follows transport lifetime, not backpressure. Contour drops
the pending buffer on a fatal device error; Kitty explicitly discards it,
VTE clears its outgoing buffer and disconnects the write source, and
Alacritty, Ghostty and foot terminate or disarm the failed writer. xterm may
retain its buffer until session teardown and is the lone outlier. POSIX
distinguishes retryable `EAGAIN`/`EINTR` from errors such as `EPIPE`; it does
not make a dead stream writable again. Shitty production already followed
the consensus, but its scripted TestPty treated every error as EAGAIN. The
harness and its older inverse test now match production: EAGAIN retains the
bytes, EPIPE consumes them and a later flush has nothing to retry.

Contour's IME test needs a state mutex because its GUI query and terminal
parser run on different threads. Alacritty, Kitty, VTE and iTerm2 likewise
synchronize their split frontend/model access. foot and xterm serialize it
inside one event thread, while Shitty uses one cooperative scheduler thread;
those implementations vote for safe state lifetime, not for one locking
mechanism. The transferred test therefore interleaves composition queries,
wide and combining output, and alternating resizes on Shitty's actual
scheduler, checking cursor and cell addressability after each reallocation.

Finally, Contour, Alacritty, Kitty and foot implement visible labelled URL
hints; Ghostty and iTerm2 implement semantic URL selection/history through a
different frontend, while xterm and VTE abstain. Their visible modes do not
offer off-screen history and their logical-line scanners preserve a URI over
a soft wrap. Shitty's public semantic-selection route proves those same two
results: a wrapped URI is returned whole, and a history URI cannot be selected
until that row is scrolled into the viewport.

The next transfer is one 20-case block: the final 15 cases 130 through 144 of
`Terminal_test.cpp`, followed by cases 304 through 308 of `Screen_test.cpp`.
The source revisions checked were Contour `9f2b296`, Alacritty `1b2b36a`,
Ghostty `156bc8c`, Kitty `fda3a9a`, xterm `6380a3e`, iTerm2 `3ec5786`, VTE
`3d55bbd` and foot `a635e0a`.

The four implementations with a labelled keyboard hint UI are Contour,
Alacritty, Kitty and foot. Alacritty's `HintState` rebuilds visible matches,
assigns labels and dispatches open, copy, paste, select and external-command
actions. Foot constructs column-aligned logical lines across soft wraps,
keeps a byte-to-cell map and generates key combinations. Kitty's hints kitten
has multi-character labels, URL and path matching, clipboard, paste and open
programs. Ghostty has automatic plain-URI matching and opening, iTerm2 has
smart selection and semantic history, VTE exposes regex matches to its
frontend, and xterm has regex selection; those four provide useful matching
and geometry oracles but abstain on label assignment itself. No terminal wire
standard defines a hint overlay.

Shitty's corresponding public operation is semantic URI hover, selection and
opening rather than a modal label alphabet. The ports therefore retain every
observable boundary without adding a Contour-only API: discovery after a
history row becomes visible, stable selected content while the viewport or
grid moves, a selected overlay on cursor and non-cursor lines, wrapped match
highlighting, wide-cell column mapping, and a match beginning in the last
column. The five actions are exercised through Shitty's actual frontend
composition: select, copy, control-click open, paste, and copy followed by
paste. A one-row viewport deliberately checks both edges of a wrapped URI.
Contour suppresses a tail-only match because its label would be off-screen;
Shitty has no label to place, and its semantic matcher, like the non-labelled
frontends, can follow the visible tail backward and returns the whole URI.

Contour alone among the four labelled implementations has the new explicit
scrollback-wide scope and negative per-hint scan limit. Shitty exposes saved
rows through the viewport rather than a second hint scope, so the exact
history rows are scrolled into view before querying them. The invalid negative
limit is represented at Shitty's real saved-line configuration boundary and
must be rejected cleanly instead of reaching unsigned row arithmetic. Bare
path validation against OSC 7 is not approximated: both occurrences of an
existing `Makefile`, its absolute result, and rejection of a missing name are
an executable expected failure until Shitty implements that product feature.

Contour and xterm are the two checked implementations with a configurable DEC
terminal identity. The other six use a fixed identity or do not implement the
same constructor setting, so they abstain on that API. At the wire boundary,
DEC's conformance model is unambiguous: DECSCL selects an operating level,
DECRQM becomes available at VT300, and VT420 introduced DECFRA. The VT340
adaptation therefore proves the VT300 report boundary. The VT220 adaptation
proves that DECSCL remains available to raise the level while DECFRA is
ignored, then that the same DECFRA executes after selecting VT500. This found
that Shitty dispatched DECFRA at VT220 despite already tracking the selected
compatibility level. The parser now applies the same VT400 gate used by the
other rectangular VT420 controls.

The five `Screen_test.cpp` cases cover ANSI modes LNM, KAM and SRM. ECMA-48
defines all three contracts; xterm and Contour implement all of them. Ghostty
implements LNM and optionally permits KAM, while storing SRM without local
echo. Alacritty and Kitty implement the received-data half of LNM. VTE keeps
the normative LNM/KAM/SRM descriptions in its mode table but deliberately
does not make these modes writable (and removed SRM local echo); foot and
iTerm2 abstain. The ports retain both halves of LNM, keyboard locking, and all
three SRM states, always checking that local echo never suppresses bytes sent
to the host.

The horizontal-margin LNM case exposed an ordering defect. Shitty performed
LNM's carriage return before deciding whether LF could scroll, moving a cursor
which had been right of the horizontal band into it. xterm's index path tests
the old column first, and Contour has the same rule. Bare LF, VT and FF now
index from the original column and only then return the carriage; an explicit
CR LF remains an explicit two-control sequence and therefore keeps its normal
ordering.

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

The final 12 `Screen_test.cpp` cases are now separate executable scenarios.
The source revisions checked for this block were Alacritty `1b2b36a`, Ghostty
`7e463bc`, Kitty `49ee9c4`, xterm `6380a3e`, Contour `c51e15e`, iTerm2
`3ec5786`, VTE `3d55bbd`, and foot `a635e0a`.  ECMA-48 5th edition is the
mode/SRM standard; the VT3xx/VT5xx programmer references are the DEC mode and
device-report standard; current xterm `ctlseqs` is the XTWINOPS reference.

SRM is deliberately not implemented by copying Contour's buffering internals.
Contour and xterm parse locally entered raw control bytes when SRM is reset;
both can consequently generate and then locally parse a reply. ECMA-48 says
locally entered data is imaged in monitor mode, but does not require a
terminal-generated protocol reply to be echoed. Ghostty records SRM but has
no local-echo action, VTE records the normative mode but removed local echo,
iTerm2 marks its SRM property unused, and Alacritty, Kitty and foot do not
implement it. The raw-query and generated-reply scenarios therefore remain
executable expected failures: they preserve the two supporting
implementations' behavior and the reentrancy regression without introducing
a second PTY output/capture pipeline as if it were settled consensus. RIS
restoring the default send/receive state is independently passing.

The hard-reset scenario calls the existing terminal reset through a test-only
entry point; it does not add a production reset API. Contour and xterm restore
ANSI parsing and the configured operating personality. Most of the remaining
six have no VT52 mode and abstain. Shitty already leaves VT52, but currently
falls back from a configured level 65 to level 64, so the complete scenario is
an expected failure. xterm also recognizes RIS in its VT52 escape table;
Contour's assertion that RIS cannot leave VT52 is not imported as an oracle.

The fixed ANSI DECRQM distinctions, permanently-reset DECHCCM,
DECREQTPARM, XTWINOPS reports/zero dimensions, and the standard DECDSR matrix
all pass as direct wire scenarios. For DECREQTPARM, Contour and xterm report
baud code 128 while VTE uses 120; Shitty retains the two-implementation value.
For locator status the DEC VT340 spelling is request `CSI ? 55 n`; reply
`CSI ? 53 n` means no locator. Contour and iTerm2 additionally accept 53 as a
request, but xterm and VTE do not, so that alias was not added.

Two DEC-mode groups remain executable expected failures rather than inert
bits in `VtermImpl`. xterm implements DECPFF against its printer state and
Contour remembers it without a printer; VTE and iTerm2 only recognize a fixed
state, while the other four abstain. For modes 34, 36, 61, 100 and 106,
Contour is the only implementation in the eight that makes every mode a
mutable no-effect toggle. xterm reports the applicable modes permanently
reset, VTE reports non-writable defaults, iTerm2 falls back to permanently
reset, and the others abstain. The VT525 reference defines the real modes,
but it does not make Contour's placeholder storage a cross-implementation
terminal contract.

The first eight current `TextSizing_test.cpp` cases are also imported as
expected failures. Kitty's OSC 66 specification is the standard and Kitty
itself the defining implementation. Contour implements the full metadata set
and foot implements the width subset. Current Ghostty has a complete metadata
parser, but its stream dispatcher explicitly classifies the resulting
`kitty_text_sizing` command as an unimplemented OSC callback; it therefore
abstains on screen semantics along with Alacritty, xterm, iTerm2 and VTE.
Defaults, colon separation, the first-semicolon payload boundary and column
multiplication agree across the implementations that act on the command.
Range failure handling differs: Contour rejects the request, Ghostty's parser
ignores an invalid field, foot falls back to ordinary text for an invalid
width, and Kitty clamps values in its screen path. Improper fractions likewise
split between rejection and non-fractional fallback. Those tests preserve the
valid boundary and the upstream rejection case without promoting one recovery
strategy.

Unknown OSC 66 keys are a genuine protocol split. Contour, Ghostty and foot
ignore unsupported keys (including non-numeric values); Kitty's generated
parser rejects an unknown key before reading its value, and the published
spec does not settle forward-compatible extension keys. Both Contour cases
remain executable and documented as that majority behavior, but expected
failure status is retained with the rest of OSC 66 until the parser/grid/
renderer representation is designed.

The following 20 `TextSizing_test.cpp` cases, through
`overwriting_a_block_destroys_all_of_it`, are separate executable scenarios,
bringing the source inventory to 28 of 60. Nineteen remain expected failures:
they cover fixed and scaled geometry, whole-block wrapping and clamping,
oversized-block rejection, atomic overwrite, ICH/DCH/DECSERA interaction,
horizontal margins, malformed requests, and the invariant that no operation
may leave an orphan continuation. A capability assertion accompanies the two
otherwise-vacuous rejection cases, so ignoring every OSC 66 command cannot be
reported as success.

The implementation revisions checked for this block were Alacritty
`1b2b36a64`, Ghostty `7e463bc65`, Kitty `0d3259f87`, xterm `6380a3eae`,
Contour `c51e15ed2`, iTerm2 `3ec57866c`, VTE `3d55bbddd`, and foot
`a635e0a19`. The OSC 66 specification requires blocks larger than the page to
be discarded, whole-block wrapping with DECAWM, right-edge placement without
DECAWM, atomic overwrite, cursor advance by `s*w`, and the stated ICH/DCH
editing rules. Kitty's screen implementation performs those operations
directly; Contour agrees. Foot participates only in unscaled forced-width
geometry, and the other five abstain on grid behavior.

Contour's `a_run_too_long_to_store_is_refused_not_shortened` expectation is
not copied literally. Its 16-codepoint private cell cap rejects the 19-byte
ASCII payload. The specification permits up to 4096 payload bytes but leaves
render fitting, including truncation, to implementations. Current Kitty keeps
this 19-codepoint payload under its 24-codepoint cell cap and Foot stores it in
full, so the adapted scenario follows that 2:1 implementation result and
requires the protocol payload to survive.

The one passing scenario in this block does not depend on OSC 66: selectively
erasing only the head of an ordinary wide character must also remove its
continuation. xterm repairs both boundary cells, VTE calls
`cleanup_fragments`, and Contour explicitly removes the orphan. iTerm2's
rectangle mutator appears to rewrite only the requested cell, while Kitty,
Foot, Ghostty and Alacritty do not implement DECSERA. Shitty follows the 3:1
result and now pins its already-correct behavior with the exact Contour input.

The next 20 cases, from `selection_yields_the_text_once` through
`insert_mode_does_not_orphan_a_neighbouring_block`, bring the executable
inventory to 48 of 60. Eighteen OSC 66 scenarios remain expected failures.
They cover selection and extraction from every band, vertical cell ownership,
scrolling for block height, atomic overlap with tall and ordinary-wide cells,
adjacent-block identity, complete fractional metadata, and insert-mode shifts
on every claimed row. The two rejection scenarios again include a positive
capability probe where ignoring OSC 66 would otherwise pass vacuously.

The published protocol defines indivisible block wrapping and overwrite,
cursor advance, page-size rejection, and ICH/DCH interactions. It does not
define selection extraction or IRM. For those areas current Kitty and Contour
agree that a block is one selectable payload and that IRM shifts every row a
new block claims; Foot's forced-width composed cell agrees for the unscaled
single-row subset. Ghostty parses but does not dispatch OSC 66, while
Alacritty, xterm, iTerm2 and VTE do not parse it, so those five abstain. The
eight revisions are unchanged from the preceding block.

Contour's two private `multicellBlockAt` cases are adapted through public
selection behavior rather than a new product API. An ordinary one-column cell
does not expand into its neighbour, while starting a selection on the trailing
column of an ordinary wide character snaps to the complete two-column glyph.
Both scenarios already pass. Alacritty's selection predicate includes a wide
head when its spacer is selected; xterm adjusts endpoints around
`HIDDEN_CHAR`; Kitty uses its unified multicell selection path; Contour exposes
the same block lookup; Foot expands endpoints across `CELL_SPACER`; and iTerm2
extends selection painting into `DWC_RIGHT`. Ghostty and VTE do not provide an
equally direct endpoint rule in the inspected terminal-core sources and abstain
on that exact normalization. The six explicit implementations agree, and the
behavior is independent of OSC 66.

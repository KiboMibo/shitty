# tmux OSS-Fuzz input corpus

The 4,166 files in `corpus/` reproduce the official OSS-Fuzz tmux seed
corpus recipe at OSS-Fuzz revision
`ea7eeb3ee4de363d97c5067b9b72379423f63da2`. The recipe concatenates and
splits each source into at most 512-byte members, matching
`input-fuzzer.options`:

- `24-bit-color.sh`, `256colors.pl`, and `UTF-8-demo.txt` from tmux revision
  `7abb9af06236eb9def862bb88a82792f6c846bef`;
- Alacritty, esctest, and iTerm2 streams from `tmux-fuzzing-corpus` revision
  `73b1e642654f90279f62bfbf91aa6eb0b3b98646`.

The relevant upstream harness, options, dictionary, and generator inputs are
preserved in `upstream/`. The tmux and source-corpus licenses are preserved
beside this file. The adapter feeds every member whole and across
deterministic parser boundaries, comparing PTY output, frontend actions,
printer output, protocol modes, render state, hyperlink targets, and Shitty's
full rich model state.

## Current regress cursor and editing cases

Fourteen independent `start_pane` cases from tmux revision
`851c5a933d4838c32ad06c248b2ba975d106149c` are represented one-to-one by
`tests/test_tmux_regress_cursor_edit.py`: all seven cases in
`regress/input-cursor.sh`, the `dch`, `ich`, `erase`, `el1`, `ech`, and `irm`
cases in `regress/input-edit.sh`, and `wrap` from
`regress/input-scroll.sh`.  The inventory guard checks fourteen distinct
source identities and fourteen executable methods.  Both parser backends pass
all fourteen plus the inventory test.

The streams preserve tmux's exact control bytes.  Its source `printf` runs
through an ordinary PTY, whose `ONLCR` processing changes the final LF to
CR LF before tmux's parser sees it; the adaptation performs that same transport
step explicitly.  It then checks complete padded rows, every source cursor
coordinate, and the soft-wrap marker, rather than comparing trimmed strings.

The expected terminal behavior was audited in all eight primary
implementations, not inferred from tmux:

| implementation | cursor/tabs | DCH, ICH, EL, ECH, IRM | delayed autowrap |
| --- | --- | --- | --- |
| Alacritty | `term/mod.rs` cursor and tab handlers | `insert_blank`, `delete_chars`, line erasure and insert mode | `input_needs_wrap` |
| Ghostty | `Terminal.zig` cursor and `tabClear` handlers | `insertBlanks`, `deleteChars`, erase and insert mode | `Screen.Cursor.pending_wrap` |
| Kitty | `screen_cursor_*` and `screen_clear_tab_stop` | `screen_insert_characters`, `screen_delete_characters`, erase and IRM | `mDECAWM` plus wrap-pending state |
| xterm | `CursorForward`, `CursorBack`, `TabClear` | `InsertChar`, `DeleteChar`, erase and `INSERT` mode | `WRAPAROUND` plus `do_wrap` |
| Contour | `Screen` cursor/tab operations | `insertChars`, `deleteChars`, erase and insert mode | cursor wrap-pending state and `AutoWrap` |
| iTerm2 | mutable-screen cursor and tab delegate operations | `terminalInsertEmptyCharsAtCursor`, `terminalDeleteCharactersAtCursor`, erase and insert mode | grid cursor past the last column plus `wraparoundMode` |
| VTE | `set_cursor_column`, `move_cursor_*`, tab-stop set | `insert_blank_characters`, `delete_characters`, erase and insert mode | off-right-edge cursor represents pending autowrap |
| foot | `term_cursor_left/right`, CHA and tab-stop handlers | `csi.c` cases `@`, `P`, `K`, `X` and mode 4 | last-column flag plus DECAWM |

The result is 8:0 for every selected case.  The implementations use different
cell stores and wrap flags, but all produce the same public rows and cursor
positions.  ECMA-48 fifth edition independently defines CUB, CUF, HPA/CHA,
HVP, HTS, TBC, CBT, DCH, ICH, EL, ECH and insertion/replacement mode; in
particular sections 8.3.26, 8.3.38 and 8.3.64 define DCH, ECH and ICH as shifts
or erased positions.  DEC's autowrap model supplies the independent standard
vote for the final right-margin character followed by a delayed wrap on the
next printable character.

No tmux server, private grid API, terminal-specific output serializer or
test-only product hook is used by these adaptations.

### Audited revisions

| implementation | relevant source | revision |
| --- | --- | --- |
| Alacritty | `alacritty_terminal/src/term/mod.rs`, `grid/mod.rs` | `1b2b36a64e88` |
| Ghostty | `src/terminal/Terminal.zig`, `Screen.zig` | `fad7f854e8f9` |
| Kitty | `kitty/screen.c`, `vt-parser.c` | `2caa3ca16bc9` |
| xterm | `charproc.c`, `cursor.c`, `tabs.c`, `util.c` | `6380a3eaed85` |
| Contour | `src/vtbackend/Screen.cpp`, `Screen.hpp` | `c51e15ed254e` |
| iTerm2 | `VT100ScreenMutableState*.m`, `VT100Grid.m` | `3ec57866cd9b` |
| VTE | `src/vteseq.cc`, `vte.cc` | `3d55bbdddb87` |
| foot | `csi.c`, `terminal.c`, `vt.c` | `a635e0a196d9` |

## Remaining edit, scroll, history and SGR cases

The remaining seven cases in `regress/input-edit.sh`, the remaining nine in
`regress/input-scroll.sh`, and all five in `regress/input-sgr.sh` at tmux
revision `851c5a933d4838c32ad06c248b2ba975d106149c` are represented one-to-one
by `tests/test_tmux_regress_edit_scroll_sgr.py`.  Its inventory guard checks 21
distinct source identities and 21 executable methods.  Both parser backends
pass all 21 plus the inventory test.

As in the preceding batch, source LF bytes are converted to CR LF to preserve
the normal PTY `ONLCR` transport used by `start_pane`.  The adaptations assert
complete cell rows, cursors, wrap topology, history contents and each SGR cell
attribute.  They do not reduce the tmux `capture-pane -e` exercises to visible
text alone.

### Editing and scrolling vote

Cases `ed`, `ed1`, `ed2`, `il`, `dl`, `rep` and `decaln` retain the complete
tmux streams for ED 0/1/2, IL, DL, REP and DECALN.  Cases `wraplast`, `nowrap`,
`origin`, `scrollup`, `scrolldown`, `ri` and `nel` retain delayed DECAWM,
disabled DECAWM, DECSTBM plus DECOM, region scrolling, RI and NEL.  Alacritty,
Ghostty, Kitty, xterm, Contour, iTerm2, VTE and foot all implement every one
of these operations and produce the exact source rows: the implementation
vote is 8:0.

ECMA-48 defines ED, IL, DL, REP and NEL.  The DEC VT100/VT220 control model is
the independent standards vote for DECALN, DECSTBM, DECOM, RI and delayed
autowrap.  These standards specify cell/page effects rather than the eight
implementations' private ring, page-list or line-array representation.

The source's `history-limit` case configures three saved rows behind a
three-row live page and captures all six.  The adaptation uses Shitty's real
three-row history capacity and checks the same six logical rows.  Every
audited implementation exposes finite configurable scrollback and keeps the
newest tail; terminal control standards do not prescribe host capacity and
abstain on the number three.

`clear-history` is a tmux server command, so pretending to execute it inside a
terminal parser would test an adapter.  Its public terminal effect is exercised
through `CSI 3 J`: discard saved rows while preserving the three live rows.
All eight implement that effect.  XTerm Control Sequences defines ED 3 as
erasing saved lines and supplies the concrete specification vote.  This keeps
the feature despite its different component boundary.

### SGR vote

`sgr-basic`, `sgr-colour` and `sgr-reset` preserve the exact attribute set and
reset list, base and bright palette colors, indexed colors, RGB colors and
default foreground/background resets.  All eight implementations support the
complete result, giving 8:0.  ECMA-48 defines the basic renditions and resets;
ISO 8613-6-compatible SGR color forms and XTerm Control Sequences supply the
color-format vote.

`sgr-underline` retains styles single, double, curly, dotted, dashed and off.
`sgr-uscolour` retains indexed underline color, the colon RGB spelling with an
empty color-space field, and SGR 59 reset.  Alacritty, Ghostty, Kitty, Contour,
iTerm2, VTE and foot all store and render these styles and colors.  Current
xterm has ordinary underline and an application resource for a fixed
underline color, but does not implement SGR `4:2`--`4:5` or `58/59`; it
therefore abstains on these two cases rather than voting to reinterpret them.
The supported-implementation result is 7:0.  ECMA-48 does not define these
subparameter styles or the underline-color extension and also abstains.

### Audited revisions

| implementation | relevant source | revision |
| --- | --- | --- |
| Alacritty | `term/mod.rs`, `grid/mod.rs`; pinned `vte/src/ansi.rs` | `1b2b36a64e88`; parser `3b3da71c34cc` |
| Ghostty | `Terminal.zig`, `Screen.zig`, `sgr.zig` | `fad7f854e8f9` |
| Kitty | `screen.c`, `vt-parser.c`, `line.c`, `data-types.h` | `2caa3ca16bc9` |
| xterm | `charproc.c`, `util.c`, `screen.c`, `ctlseqs.txt` | `6380a3eaed85` |
| Contour | `Screen.cpp`, `Grid.cpp`, `SgrWriter.hpp` | `c51e15ed254e` |
| iTerm2 | `VT100Terminal.m`, `VT100GraphicRendition.m`, `VT100ScreenMutableState*.m` | `3ec57866cd9b` |
| VTE | `vteseq.cc`, `vte.cc`, `parser-sgr.hh` | `3d55bbdddb87` |
| foot | `csi.c`, `terminal.c`, `render.c` | `a635e0a196d9` |

## Malformed input, modes, OSC and first Unicode case

All nine cases in `regress/input-malformed.sh`, both cases in
`regress/input-modes.sh`, all eight cases in `regress/input-osc.sh`, and the
`wide` case from `regress/input-unicode.sh` at tmux revision
`851c5a933d4838c32ad06c248b2ba975d106149c` are represented one-to-one by
`tests/test_tmux_regress_malformed_modes_osc_unicode.py`.  Its inventory guard
checks 20 distinct source identities and 20 executable methods.  Both parser
backends pass all 20 plus the inventory test and the related shell-integration
tests.

The adaptations retain the complete externally observable operation: parser
recovery and following text, protocol replies, title and clipboard effects,
frontend progress actions, hyperlink targets, complete rows and per-cell
semantic, width and hyperlink metadata.  The 1,100,000-byte OSC and APC
payloads are preserved exactly and merely split into transport writes.

### Malformed input vote

Alacritty, Ghostty, Kitty, xterm, Contour, iTerm2, VTE and foot all recover at
CAN or ST and ignore unknown CSI and OSC functions, giving 8:0 for the public
recovery result.  ECMA-48 supplies the independent parser-state and
control-string vote.  Ghostty, Kitty, xterm, Contour, iTerm2 and VTE also emit
the DECRQSS failure form for an unknown request, giving 6:0 among supporting
implementations; Alacritty and foot abstain.  DEC VT420 and XTerm Control
Sequences specify `DCS 0 $ r ST` as the failure reply.

tmux replaces each complete malformed UTF-8 group once.  The adaptation uses
Unicode maximal-subpart replacement instead: four replacements for
`F0 80 80 80` and three for `ED A0 80`.  Ghostty, Kitty, iTerm2 and VTE produce
that exact result.  Default Alacritty and xterm consume each group as one
error, while Contour and foot have other legacy decoding behavior.  Unicode
Core section 3.9 and Table 3-9 require the maximal-subpart result, so the vote
is 5:2 with the two other implementations abstaining from either exact result.

The malformed OSC 8 stream also intentionally differs from tmux.  Alacritty,
Ghostty, Kitty, iTerm2, VTE and foot accept the duplicate `id` parameter and
leave that hyperlink active when the following OSC 8 has no URI separator.
Contour closes it at the malformed second command and current xterm does not
implement OSC 8.  The implementation vote is therefore 6:1, and the OSC 8
specification's colon-separated parameter grammar supplies the seventh vote
for acceptance.  Invalid color, progress and OSC 52 commands have no partial
public effect.

### Modes and OSC vote

All eight implementations support alternate-screen restoration, dynamic and
palette colors, safe title-stack processing and wide-cell overwrite cleanup.
The exact public results are 8:0.  XTerm Control Sequences specifies alternate
screen, OSC colors and the title stack; Unicode UAX 11 supplies the width
classification behind the wide-cell invariant.

All implementations except current xterm support basic OSC 8 hyperlinks,
giving 7:0 among supporting implementations.  OSC 52 clipboard writes have
the same 7:0 supported result, with VTE abstaining instead of xterm.  Ghostty,
Contour and iTerm2 implement the exact BEL-terminated ConEmu OSC 9;4 progress
stream, giving 3:0; the other five either implement a different OSC 9
protocol, require ST for this subcommand, or abstain.  The OSC 8, XTerm OSC 52
and ConEmu OSC 9;4 specifications supply the respective independent votes.

tmux treats Screen's `ESC k ... ST` and an arbitrary APC as title strings.
Current VT-mode terminals do not agree: seven implementations expose
`renamedX` with the title unchanged for the Screen extension, while iTerm2
implements it; all eight suppress arbitrary APC content without changing the
title.  These tests therefore retain the source streams but assert the
terminal consensus rather than tmux's server convention.

For OSC 133 `A`, Kitty, Contour, VTE and foot mark the current position without
moving it.  Ghostty and iTerm2 first move to a fresh line; Alacritty and xterm
abstain.  The Semantic Prompts proposal specifies the fresh-line behavior, so
the exact vote is 4:3 for no implicit cursor movement.  Explicit OSC 133 `L`
remains the fresh-line operation.  The complete tmux stream still verifies
the `A`, `B`, `C`, `D` and `P` partition as per-cell prompt, command, output
and idle semantics.

### Audited revisions

| implementation | relevant source | revision |
| --- | --- | --- |
| Alacritty | `term/mod.rs`; pinned `vte/src/ansi.rs` | `1b2b36a64e88`; parser `3b3da71c34cc` |
| Ghostty | `Terminal.zig`, OSC parsers and UTF-8 tests | `fad7f854e8f9` |
| Kitty | `screen.c`, `vt-parser.c`, parser tests | `2caa3ca16bc9` |
| xterm | `charproc.c`, `osc.c`, `ctlseqs.txt` | `6380a3eaed85` |
| Contour | `Screen.cpp`, parser and shell-integration handlers | `c51e15ed254e` |
| iTerm2 | `VT100Terminal.m`, `VT100XtermParser.m`, UTF-8 helpers | `3ec57866cd9b` |
| VTE | `vteseq.cc`, OSC handlers and `utf8-test.cc` | `3d55bbdddb87` |
| foot | `csi.c`, `osc.c`, `vt.c` | `a635e0a196d9` |

## Remaining Unicode and first reply cases

The remaining nine cases in `regress/input-unicode.sh` and the first eleven
cases in `regress/input-replies.sh` at tmux revision
`851c5a933d4838c32ad06c248b2ba975d106149c` are represented one-to-one by
`tests/test_tmux_regress_unicode_replies.py`.  The reply subset runs from
`dsr-ok` through `decrqm-origin-set`: DSR and CPR, primary and secondary DA,
and the reset/set reports for IRM, DECCKM and DECOM plus the initial DECCOLM
report.  Its inventory guard checks 20 distinct source identities and 20
executable methods.  Both parser backends pass all 20 plus the inventory and
the two directly related leading-combining regressions.

As in the preceding input batches, the Unicode streams retain the source
bytes and reproduce the normal PTY's LF-to-CR-LF transport.  Assertions cover
complete rows and cursor positions as well as wide heads/tails, soft-wrap
topology and full grapheme payloads.  Replies retain their exact requests and
setup sequences; only terminal identity and behavior on which tmux is not a
terminal consensus are adapted.

### Unicode vote

All eight implementations clear both halves when either half of a wide cell
is overwritten, move a wide glyph wholly to the next row when it cannot fit,
and preserve a combining mark on narrow and wide bases.  The implementation
result for `widepad`, `wideedge`, `wideeol`, `combine` and `combinewide` is
8:0.  The Unicode width and grapheme rules supply the independent standard
vote.

VS16 on U+2714 is wide by default in Ghostty, Kitty, Contour, iTerm2 and foot.
Alacritty and VTE retain a narrow cell, and xterm's capable implementation has
its `emojiWidth` resource disabled by default.  The Terminal Unicode Core
rules require VS16 emoji presentation to occupy two cells, so the exact vote
is 6:3 for the wide cluster.

Ghostty, Kitty, Contour and foot store the regional-indicator pair as one
cluster.  iTerm2 stores adjacent indicator cells but explicitly pairs them in
the renderer; VTE likewise shapes the adjacent run.  Alacritty and xterm do
not implement paired flag shaping and abstain.  Thus the feature vote is 6:0,
and UAX 29 plus the Unicode emoji rules supply the seventh vote for one
two-cell flag.  The Shitty test checks its own complete-cluster cell model,
not another terminal's private representation.

The leading combining mark exposes three representations.  Ghostty, Kitty,
VTE and foot discard it; Alacritty and xterm retain it in the current blank
cell without advancing, so the following `A` overwrites it; Contour and iTerm2
retain it and advance one cell.  The exact source-stream result is therefore
6:2 for `A` at column zero.  UAX 29 recognizes the isolated mark as a
degenerate cluster and UAX 11 gives nonspacing marks no advance width, so
Shitty retains it without advancing rather than dropping the feature.  This
also corrected an older xterm.js adaptation that accidentally inspected
cursor visibility and blink as if they were cursor coordinates.

tmux caps the final cluster at 16 codepoints.  Alacritty, Ghostty, Kitty,
iTerm2 and foot retain all 17 codepoints in the source stream; Contour, VTE
and xterm truncate at different implementation limits.  UAX 29 defines no
shorter cluster for this sequence, giving a 6:3 vote for retaining all 16
combining marks.  Shitty's existing 24-codepoint safety bound already admits
the consensus result.

### Reply vote

All eight implementations answer operating-status DSR with `CSI 0 n` and CPR
at the home position with `CSI 1 ; 1 R`, giving 8:0.  They all implement DA1
and DA2 but correctly report different terminal families, capabilities and
versions.  The DEC DA specifications define those fields as terminal
descriptors rather than universal constants.  The adaptations therefore
retain the exact tmux queries while checking Shitty's own VT420 capability
list and VT420-family secondary identity instead of copying tmux's `screen`
identity.

Ghostty, Kitty, xterm, Contour, iTerm2, VTE and foot implement DECRQM for IRM,
DECCKM and DECOM and agree on reset `2` and set `1`; Alacritty parses DECRQM
but its terminal handler deliberately emits no reply and abstains.  These six
cases are 7:0.  For DECCOLM, Ghostty, Kitty, xterm, Contour, iTerm2 and VTE all
report the initial mutable reset state `2`; foot does not recognize mode 3 and
Alacritty again abstains.  DEC's DECRQM/DECRPM and DECCOLM definitions supply
the seventh vote.  tmux's permanently-reset `4` is its own fixed-pane policy,
not the terminal consensus, so Shitty continues to report `CSI ? 3 ; 2 $ y`.

### Audited revisions

| implementation | relevant source | revision |
| --- | --- | --- |
| Alacritty | `term/mod.rs`, `term/cell.rs`; pinned `vte/src/ansi.rs` | `1b2b36a64e88`; parser `3b3da71c34cc` |
| Ghostty | `Terminal.zig`, `modes.zig`, `stream_terminal.zig`, Unicode API | `fad7f854e8f9` |
| Kitty | `screen.c`, `line.h`, Unicode property tables | `2caa3ca16bc9` |
| xterm | `charproc.c`, `util.c`, `misc.c`, `ctlseqs.ms` | `6380a3eaed85` |
| Contour | `Screen.cpp`, `LineSoA.cpp`, Unicode cluster tests | `c51e15ed254e` |
| iTerm2 | `VT100Terminal.m`, `VT100Output.m`, `ScreenChar.m` | `3ec57866cd9b` |
| VTE | `vte.cc`, `vteunistr.cc`, `vteseq.cc` | `3d55bbdddb87` |
| foot | `terminal.c`, `csi.c`, `composed.c` | `a635e0a196d9` |

## Remaining replies and terminal requests

The final 19 calls in `regress/input-replies.sh`, from
`decrqm-wrap-set` through `osc-52-query`, and both wire round trips in
`regress/input-requests.sh` at tmux revision
`851c5a933d4838c32ad06c248b2ba975d106149c` are represented one-to-one by
`tests/test_tmux_regress_reply_requests.py`.  Its inventory guard checks 21
distinct source identities and 21 executable methods.

The DECRQM cases retain every source setup and request byte.  The DECRQSS
case retains the source request but first selects a steady bar, so the reply
checks live cursor state instead of copying tmux's implementation-specific
default.  Color cases retain the BEL-terminated source setup and queries;
Shitty replies with its established canonical ST terminator.  The two
`input-requests.sh` cases exercise the public terminal side of the protocol:
an OSC 4 palette request and an allowed OSC 52 clipboard request complete a
real request/reply round trip.  Tmux's pane-to-client proxy and synthetic
terminal replies are not terminal-emulator APIs and are not reproduced.

### Mode and status votes

Alacritty, Ghostty, Kitty, xterm, Contour, iTerm2, VTE and foot all implement
DECAWM, DECTCEM, normal/button/any-event mouse tracking, focus events, SGR
mouse transport and bracketed paste.  Each corresponding set/reset report is
therefore 8:0.  DEC STD 070 and the VT420/VT525 control definitions supply one
independent vote for DECRQM/DECRPM state reporting; the XTerm Control
Sequences specification supplies one for the adopted mouse, focus and
bracketed-paste modes.

Alacritty, Ghostty, Kitty, xterm, Contour and iTerm2 implement UTF-8 mouse
transport and report mode 1005 as set after DECSET.  VTE and foot recognize
the number but deliberately keep the unsupported transport permanently
reset, so they abstain on the enabled behavior.  The supported vote is 6:0,
plus the XTerm Control Sequences definition of mode 1005.

Ghostty, Kitty, Contour, iTerm2, VTE and foot implement color-scheme update
mode 2031 and report its mutable state.  Alacritty and xterm do not implement
the extension and abstain, giving 6:0.  Contour's
`color-palette-update-notifications.md` is the concrete protocol specification
and supplies the independent specification vote.

Ghostty, Kitty, xterm, Contour, iTerm2, VTE and foot implement DECRQSS for
DECSCUSR and return the currently selected cursor shape and blink state.
Alacritty does not implement DECRQSS and abstains, giving 7:0.  DEC's
DECRQSS/DECRPSS and DECSCUSR definitions provide the independent standard
vote.  The test selects mode 6 and requires `DCS 1 $ r 6 SP q ST`; it does not
adopt tmux's unusual `SP q 0 SP q` payload.

### Color and clipboard votes

All eight implementations set and query indexed OSC 4 colors and dynamic OSC
10, 11 and 12 colors.  All eight also make an indexed color queryable again
after OSC 104 restores it.  XTerm Control Sequences specifies these set,
query and reset operations and supplies the standard vote.  Tmux expects no
reply after its OSC 104 cache invalidation because it no longer knows the
underlying color; a terminal does know its configured palette.  The adapted
case therefore captures the configured color, changes and resets the entry,
then requires the query to return that original color.

Alacritty, Ghostty, Kitty, xterm, Contour, iTerm2 and foot provide gated OSC
52 clipboard reads and base64 replies.  VTE parses selector 52 but has no
clipboard handler and abstains, giving 7:0.  XTerm Control Sequences specifies
the read request and reply and supplies the standard vote.  Shitty's tests
explicitly enable its existing read permission; the default security denial
is not weakened.

The two terminal-request scenarios carry no additional tmux semantic oracle.
They prove that the same 8:0 OSC 4 and 7:0 OSC 52 features work when the exact
application request from `input-requests.sh` is presented directly to the
terminal.  No synthetic client attachment, response injection or test-only
product API is added.

All 21 adaptations plus their inventory pass on both parser backends.  No
production code changes were needed.

### Audited revisions

| implementation | relevant source | revision |
| --- | --- | --- |
| Alacritty | `ansi.rs`, `term/mod.rs` | `1b2b36a64e88`; parser `3b3da71c34cc` |
| Ghostty | `modes.zig`, `dcs.zig`, `stream_terminal.zig`, `Surface.zig` | `fad7f854e8f9` |
| Kitty | `modes.h`, `screen.c`, `window.py`, `clipboard.py` | `2caa3ca16bc9` |
| xterm | `misc.c`, `charproc.c`, `ctlseqs.txt` | `6380a3eaed85` |
| Contour | `Primitives.hpp`, `Screen.cpp`, `Terminal.cpp` | `c51e15ed254e` |
| iTerm2 | `VT100Terminal.m`, `VT100Output.m`, `VT100XtermParser.m` | `3ec57866cd9b` |
| VTE | `modes.py`, `parser-osc.hh`, `vteseq.cc` | `3d55bbdddb87` |
| foot | `csi.c`, `dcs.c`, `osc.c` | `a635e0a196d9` |

## Legacy control input-key cases

The first 63 terminal-facing cases in `regress/input-keys.sh` at tmux revision
`851c5a933d4838c32ad06c248b2ba975d106149c` are represented one-to-one by
`tests/test_tmux_regress_input_keys_control.py` and
`tests/test_tmux_regress_input_keys_control_mid.py` and
`tests/test_tmux_regress_input_keys_control_tail.py`: `C-Space`, the control
and meta-control forms of `a` through `z`, Escape and Meta-Escape, and the
control/meta-control forms of backslash, right bracket, caret and underscore.
Their inventory guards check 21, 22 and 20 distinct source identities and
executable methods.  The adaptation drives Shitty's public frontend paths and
compares the raw bytes written to the PTY.  In particular, the two `C-j` cases
assert LF and ESC+LF directly; they do not copy the empty and ESC-only visual
strings produced by tmux's line-oriented capture helper.

Caret and underscore are not modeled as invented standalone physical keys.
The tests send `Ctrl-Shift-6` and `Ctrl-Shift--` through the layout-aware key
path, including the shifted codepoint supplied by the platform.  Escape uses
the named-key path; the other punctuation uses its unshifted layout identity.

### Encoding vote

For control-space and letters other than `i` and `m`, all eight implementations
map the chord to its traditional C0 byte and support the meta/Alt form as an
ESC prefix before that byte.  Alacritty accepts the platform-produced C0 text
and prefixes it in `alt_send_esc`; Kitty and Contour perform the mapping
explicitly; VTE and foot combine their keymap/XKB C0 result with their enabled
meta-escape mode.  xterm exposes the same prefix through `metaSendsEscape` or
`altSendsEscape`, and iTerm2 through the profile's Esc+ option-key behavior.
Those two configurable alternatives do not abstain: both implementations
support the exact tested wire behavior.

All eight also agree on the newly covered Escape, backslash, right bracket,
caret and underscore wire results.  Escape is ESC and Meta-Escape is ESC ESC;
the four control chords are FS, GS, RS and US, with Meta adding one leading
ESC.  These 20 new cases therefore vote 8:0.

Ghostty is a supporting dissent for `Ctrl-i` and `Ctrl-m`, not an abstention.
Its current legacy encoder deliberately emits `CSI 105;5u` and `CSI 109;5u`
for those two ambiguous chords, and modifier 7 for their Alt forms.  The other
seven implementations emit Tab and CR respectively, with an ESC prefix for
Alt.  The exact implementation vote is therefore 8:0 for the other 39 cases
and 7:1 for these four wire results.

Kitty's keyboard protocol is the concrete independent protocol vote.  Its
legacy algorithm says to emit ESC first when Alt is held, then apply its
control mapping; the published tables map space to 0, `a` through `z` to 1
through 26, Escape to 27, and backslash/right bracket/caret/underscore to
28/29/30/31.  They specify the same Alt prefix and cite the VT100 keyboard
table as the historical base.  The protocol vote agrees with the
seven-implementation result for the two disputed chords rather than dropping
the implemented feature.

All 63 adaptations and their three inventory guards pass on both parser
backends; no production code change was needed.

### Audited revisions

| implementation | relevant source | revision |
| --- | --- | --- |
| Alacritty | `alacritty/src/input/keyboard.rs` | `1b2b36a64e88` |
| Ghostty | `src/input/key_encode.zig` | `fad7f854e8f9` |
| Kitty | `kitty/key_encoding.c`, `docs/keyboard-protocol.rst` | `2caa3ca16bc9` |
| xterm | `input.c`, `charproc.c`, `ctlseqs.txt` | `6380a3eaed85` |
| Contour | `src/vtbackend/InputGenerator.cpp` and tests | `c51e15ed254e` |
| iTerm2 | `sources/Keyboard/iTermStandardKeyMapper.m` | `3ec57866cd9b` |
| VTE | `src/keymap.cc`, `src/vte.cc`, `src/modes.py` | `3d55bbdddb87` |
| foot | `input.c`, `terminal.c` | `a635e0a196d9` |

## Printable input-key cases

The next 190 terminal-facing cases in `regress/input-keys.sh` at tmux revision
`851c5a933d4838c32ad06c248b2ba975d106149c` are represented one-to-one by
`tests/test_tmux_regress_input_keys_printable_head.py` and
`tests/test_tmux_regress_input_keys_printable_mid.py` and
`tests/test_tmux_regress_input_keys_printable_more.py` and
`tests/test_tmux_regress_input_keys_printable_upper_head.py` and
`tests/test_tmux_regress_input_keys_printable_upper_mid.py` and
`tests/test_tmux_regress_input_keys_printable_upper_tail.py` and
`tests/test_tmux_regress_input_keys_printable_lower_head.py` and
`tests/test_tmux_regress_input_keys_printable_lower_mid.py` and
`tests/test_tmux_regress_input_keys_printable_lower_more.py` and the first ten
cases in `tests/test_tmux_regress_input_keys_printable_tail_function_head.py`:
all 95 printable ASCII characters from Space through tilde, each in plain and
Meta form.  The mixed file's inventory guard covers its ten printable cases
together with the ten functional cases below.

The adaptations use the platform event sequence rather than injecting the
result byte.  Each scenario sends the physical layout key, including Shift
and its shifted codepoint where applicable, followed by the text event that
frontends generate for a printable press.  Thus `!` is `Shift-1`, `"` is
`Shift-'`, `*` is `Shift-8`, and `+` is `Shift-=`.  The assertion observes the
complete raw PTY write and would catch either a missing byte or a duplicate
from mishandling both stages.

### Encoding vote

Alacritty, Ghostty, Kitty, xterm, Contour, iTerm2, VTE and foot all send the
selected ASCII layout character unchanged in their legacy path and support
Meta/Alt as one leading ESC before that character.  For xterm and iTerm2 the
ESC behavior is the same configurable mode already described above; both
support the exact wire result and therefore vote rather than abstain.  The
exact implementation vote is 8:0 for all 190 cases.

Kitty's keyboard protocol supplies the independent protocol vote.  Its legacy
text algorithm first emits ESC for Alt, then emits the Shift-selected
character; its examples explicitly include shifted number-row punctuation.

## Tab, backspace and first function-key cases

The remaining ten cases in the mixed file adapt `Tab`, `M-Tab`, `BSpace`,
`M-BSpace` and `F1` through `F6`.  They use the platform named-key path, not
`writeKittyKey`, and observe the complete raw PTY output.

Alacritty, Ghostty, Kitty, xterm, Contour, iTerm2, VTE and foot agree 8:0 on
Tab, Alt-Tab and the six function-key sequences.  Their legacy tables emit HT,
ESC HT, SS3 P through SS3 S, CSI 15 tilde and CSI 17 tilde respectively.

Backspace is the one mode-dependent case.  Seven implementations start on the
tmux expectation, DEL and ESC DEL; xterm's compiled resource starts with
DECBKM set and therefore chooses BS instead.  All eight implement the exact
tested DEL branch when DECBKM is reset, so the default-state vote is 7:1 and
the mode-capability vote is 8:0.  The adaptation deliberately exercises
Shitty's reset DECBKM state rather than hiding this distinction.

Kitty's keyboard protocol is the independent protocol vote.  Its legacy C0
table specifies HT and ESC HT for Tab, DEL and ESC DEL for Backspace while
noting the historical BS swap, and its legacy functional table gives exactly
the six function-key sequences above.

All 200 adaptations in the printable and mixed batches, plus their ten
inventory guards, pass on both parser backends; no production code change was
needed.

## Function and navigation-key cases

`tests/test_tmux_regress_input_keys_navigation_head.py` represents the next 20
source identities: `F8` through `F12`; the `IC`/`Insert`, `DC`/`Delete`,
page-down and page-up aliases; `Home`, `End`, `BTab`, `C-S-Tab` and `Up`.
Every case enters through the platform named-key path.  The inventory guard
requires 20 distinct source names and 20 executable methods.

Seventeen cases agree with the tmux expected bytes and have an 8:0
implementation vote.  The Kitty legacy functional table independently gives
the same function, editing, paging, BackTab and arrow sequences.

The adaptation intentionally does not copy two stale tmux expectations:

- Current Alacritty, Ghostty, Kitty, xterm, Contour, iTerm2, VTE and foot all
  emit CSI H and CSI F for unmodified Home and End in normal cursor mode.  The
  source file instead expects CSI 1 tilde and CSI 4 tilde.  The implementation
  vote is 8:0 for H/F, also matching Kitty's legacy table.
- No audited implementation supports tmux's HT expectation for `C-S-Tab`.
  Kitty, xterm, Contour, iTerm2 and VTE reduce it to BackTab (CSI Z); Ghostty
  and foot preserve Ctrl with CSI 27;6;9 tilde; Alacritty has no matching
  legacy binding.  Kitty's protocol C0 table specifies CSI Z for
  Ctrl-Shift-Tab, so the adaptation follows the five-implementation plurality
  plus the protocol vote.

All 20 adaptations and the inventory guard pass on both parser backends; no
production code change was needed.

## Cursor and numeric-keypad cases

`tests/test_tmux_regress_input_keys_keypad.py` carries the next complete block
of 33 source identities: `Down`, `Right`, `Left`, and all 15 numeric keypad
keys from multiply through digit 9, with a separate Meta identity for every
keypad key.  The adapter sends real platform key events in reset numeric
keypad mode; it never injects the resulting character or substitute NumLock
policy for the tmux source semantics.  Its inventory guard requires 33
distinct source identities and methods.

The three cursor keys and all 15 unmodified keypad cases have an 8:0
implementation vote.  Reset numeric mode emits the literal character.

For Meta keypad, Alacritty, Ghostty, Contour and foot prefix the numeric
literal with ESC.  xterm, iTerm2, VTE and Kitty's current frontend numeric
path emit only the literal, ignoring Alt for this special-key route.  Kitty's
protocol says that keypad keys are equivalent to non-keypad keys and therefore
supplies the deciding vote for the ESC prefix.  The combined vote is 5:4 for
the tmux expectation.

This exposed a Shitty split: NumLock already routed these literals through
the character encoder and preserved Alt, while reset numeric mode routed the
same keys through the special-key table and silently discarded every modifier
because its literals contain no modifier marker.  The common legacy frontend
dispatch now sends normal numeric-keypad literals through the character
encoder as well.  Application keypad and Kitty protocol encoding remain
separate.  All 33 adaptations and the inventory guard pass on both parser
backends.

## First extended function-key cases

`tests/test_tmux_regress_input_keys_extended_function_head.py` represents the
first 21 identities after `extended-keys always`: all seven nonempty
Shift/Alt/Control combinations for F1, F2 and F3.  Each identity is a real
platform named-key event and the inventory guard requires 21 distinct source
names and executable methods.

F1 and F2 have an 8:0 implementation vote for `CSI 1 ; modifier P/Q`.
Alacritty, Ghostty, Kitty, xterm, Contour, iTerm2, VTE and foot all preserve
the complete three-modifier bitset in the usual xterm modifier parameter.
Kitty's keyboard protocol independently specifies the same functional-key
codes and modifier parameter.

Modified F3 has two live encodings.  Alacritty, xterm, iTerm2, VTE and foot
emit `CSI 1 ; modifier R`; Ghostty, Kitty and Contour emit
`CSI 13 ; modifier ~` to avoid collision with a cursor-position report.
Kitty's current keyboard protocol permits only the latter F3 base.  Thus the
exact vote is 5:4 for the tmux `CSI 1 ; modifier R` expectation.  The
adaptation retains that narrow consensus and records the fully implemented
alternative instead of treating it as unsupported or dropping F3.

All 21 adaptations and the inventory guard pass on both parser backends; no
production code change was needed.

## More extended function-key cases

`tests/test_tmux_regress_input_keys_extended_function_mid.py` carries the next
21 source identities: all seven nonempty Shift/Alt/Control combinations for
F4, F5 and F6.  The inventory guard requires a separate executable method for
every source identity and every method sends a platform named-key event.

Alacritty, Ghostty, Kitty, xterm, Contour, iTerm2, VTE and foot agree 8:0 on
all three bases and every modifier combination: `CSI 1 ; modifier S` for F4,
`CSI 15 ; modifier ~` for F5 and `CSI 17 ; modifier ~` for F6.  Kitty's
keyboard protocol supplies the independent protocol vote for the same bases
and modifier encoding.  All 21 adaptations and the inventory guard pass on
both parser backends; no production code change was needed.

## Extended F8 through F10 cases

`tests/test_tmux_regress_input_keys_extended_function_more.py` represents the
next 21 source identities: all seven nonempty Shift/Alt/Control combinations
for F8, F9 and F10.  Current tmux has no F7 assertion in this source file, so
the adaptation neither invents an identity nor hides the gap in its inventory.

All eight audited implementations emit `CSI 19 ; modifier ~`,
`CSI 20 ; modifier ~` and `CSI 21 ; modifier ~` respectively, giving an 8:0
vote for every case.  Kitty's functional-key table independently specifies
the same bases and modifier encoding.  All 21 adaptations and the inventory
guard pass on both parser backends; no production code change was needed.

## Extended F11, F12 and Up cases

`tests/test_tmux_regress_input_keys_extended_function_tail.py` carries the
next 21 source identities: every nonempty Shift/Alt/Control combination for
F11, F12 and Up.  The first fourteen finish the function-key block; the final
seven begin the cursor-key block without reducing the requested batch size.

Alacritty, Ghostty, Kitty, xterm, Contour, iTerm2, VTE and foot agree 8:0 on
`CSI 23 ; modifier ~`, `CSI 24 ; modifier ~` and
`CSI 1 ; modifier A`.  Kitty's keyboard protocol independently defines the
same three functional bases and modifier field.  All 21 adaptations and the
inventory guard pass on both parser backends; no production code change was
needed.

## Extended Down, Right and Left cases

`tests/test_tmux_regress_input_keys_extended_cursor_head.py` represents the
next 21 source identities: every nonempty Shift/Alt/Control combination for
Down, Right and Left.  Together with Up in the preceding batch this completes
the source's extended cursor-key block.

All eight audited implementations preserve the complete modifier bitset and
emit `CSI 1 ; modifier B/C/D`, giving an 8:0 vote.  Kitty's keyboard protocol
defines the same cursor-key bases and modifier field.  All 21 adaptations and
the inventory guard pass on both parser backends; no production code change
was needed.

## Extended Home, End and PPage cases

`tests/test_tmux_regress_input_keys_extended_navigation_head.py` represents
the next 21 source identities: every nonempty Shift/Alt/Control combination
for Home, End and the first PageUp alias, `PPage`.  `PageUp` and `PgUp` remain
separate upstream identities even though they enter the same platform key.

The underlying encoders in all eight implementations support
`CSI 1 ; modifier H/F` and `CSI 5 ; modifier ~`, but their default frontend
bindings are part of the public operation being adapted.  For plain
Shift+Home/End, Alacritty, Ghostty, Contour and VTE scroll locally while
Kitty, xterm, iTerm2 and foot forward the encoded key.  Kitty's protocol vote
breaks the 4:4 tie in favour of forwarding `CSI 1 ; 2 H/F`, which is Shitty's
public result.

Plain Shift+PageUp has a different consensus: Alacritty, Ghostty, xterm,
Contour, VTE and foot scroll local history; Kitty and iTerm2 forward
`CSI 5 ; 2 ~`, and the protocol describes that forwarded branch.  The exact
vote is therefore 6:3 for local scrolling.  That source identity asserts both
an increased view offset and no PTY bytes.  The other six modifier identities
are forwarded by the implementation plurality and retain the source wire
assertions.  All 21 adaptations and the inventory guard pass on both parser
backends; no production code change was needed.

## Extended paging aliases

`tests/test_tmux_regress_input_keys_extended_paging.py` carries the next 21
source identities: every nonempty Shift/Alt/Control combination for the
`PageUp` and `PgUp` aliases and the first PageDown alias, `NPage`.  Aliases
remain separate executable scenarios even though the platform maps them to
the same physical key.

The same frontend/encoder split applies to all three names.  Plain Shift with
PageUp or PageDown scrolls local history in Alacritty, Ghostty, xterm,
Contour, VTE and foot; Kitty and iTerm2 forward the key, while Kitty's
protocol describes the forwarded branch.  The 6:3 public-operation vote is
tested with both the view offset and absence of PTY bytes.  The remaining six
modifier combinations retain their consensus xterm encoding,
`CSI 5/6 ; modifier ~`.  All 21 adaptations and the inventory guard pass on
both parser backends; no production code change was needed.

## Extended PageDown aliases and IC

`tests/test_tmux_regress_input_keys_extended_paging_insert.py` represents the
next 21 source identities: all modifier combinations for the `PageDown` and
`PgDn` aliases and the first Insert alias, `IC`.  The two paging Shift cases
repeat the consensus local-scroll assertion for their own upstream identities.

Plain Shift+Insert is also intercepted above the encoder.  Alacritty,
Ghostty, Kitty, xterm, VTE and foot paste a selection; Contour and iTerm2
forward modified Insert, and Kitty's protocol describes the forwarded form.
The public-operation vote is 6:3 for paste.  Because implementations differ
between primary and clipboard selection, the test gives both the same payload
and verifies the resulting PTY bytes.  The other six IC combinations use the
consensus `CSI 2 ; modifier ~` encoding.  All 21 adaptations and the inventory
guard pass on both parser backends; no production code change was needed.

## Extended Insert and Delete aliases

`tests/test_tmux_regress_input_keys_extended_editing.py` represents the next
21 identities: all modifier combinations for the `Insert`, `DC` and `Delete`
source names.  `Insert` repeats the Shift-paste public-operation consensus for
its separate alias; its other combinations use `CSI 2 ; modifier ~`.

The Delete encoder has no competing default binding in the audited normal
terminal state.  All eight implementations and Kitty's protocol agree on
`CSI 3 ; modifier ~`, giving a 9:0 vote for both `DC` and `Delete`.  All 21
adaptations and the inventory guard pass on both parser backends; no production
code change was needed.

## Extended Tab tail and tty key decoder head

`tests/test_tmux_regress_input_keys_tail_tty_keys_head.py` finishes the two
post-`extended-keys always` identities in `regress/input-keys.sh` and carries
the first 20 executable identities from `regress/tty-keys.sh`, from NUL as
`C-Space` through LF as `C-j`.  The inventory guard requires all 22 source
identities to have distinct executable scenarios.

`C-Tab` has an 8:0 implementation vote for `CSI 27 ; 5 ; 9 ~` in the audited
legacy encoders and agrees with tmux.  For `C-S-Tab`, tmux's extended mode
expects `CSI 27 ; 6 ; 9 ~`, but the terminal's public operation remains the
one audited above: Kitty, xterm, Contour, iTerm2 and VTE produce BackTab,
Ghostty and foot preserve Control, and Alacritty has no legacy binding.
Kitty's legacy protocol table votes for BackTab, so the 6:2 supported vote
keeps Shitty's `CSI Z` behavior.

`tty-keys.sh` exercises tmux's input key decoder, which a terminal emulator
does not have.  Each source identity is therefore adapted to the corresponding
public terminal operation: the frontend key event is encoded to the same wire
byte that tmux recognizes.  The first 20 cases repeat the already audited 8:0
C0 and Alt-prefix consensus for Space, `a` through `h`, Tab and `j`; Kitty's
legacy keyboard protocol supplies the independent protocol vote.  All 22
adaptations pass on both parser backends; no production code change was
needed.

## Tty control key decoder continuation

`tests/test_tmux_regress_tty_keys_control_mid.py` carries the next 21
executable identities from `regress/tty-keys.sh`: Meta-Control-J, both
plain/Meta forms of Control-K and Control-L, Enter and Meta-Enter, and both
forms of Control-N through Control-T.  As with the preceding decoder cases,
each identity is adapted to the inverse public terminal operation and verifies
the bytes emitted to the PTY.

The audited legacy encoders agree 8:0 on all 21 results: letter chords produce
their traditional C0 bytes, Enter produces CR, and Meta/Alt adds one leading
ESC.  Enter is driven through the named-key frontend path, so this does not
conflate it with the dissenting legacy handling of the distinct `Ctrl-m`
chord.  Kitty's legacy keyboard protocol supplies the independent protocol
vote.  All 21 adaptations and the inventory guard pass on both parser
backends; no production code change was needed.

## Tty C0 decoder tail

`tests/test_tmux_regress_tty_keys_control_tail.py` carries all 22 source
identities for bytes `0x15` through `0x1f` in `regress/tty-keys.sh`: plain and
Meta forms of Control-U through Control-Z, Escape, Control-backslash,
Control-right-bracket, Control-caret and Control-underscore.  Each decoder
identity again executes the inverse public frontend operation and verifies the
PTY bytes.

The result is the previously audited 8:0 implementation consensus plus the
Kitty legacy protocol vote: the chords encode to NAK through US, with Meta
adding one ESC prefix.  Caret and underscore use layout-aware `Shift-6` and
`Shift--` events; Escape uses the named-key frontend path.  All 22 adaptations
and the inventory guard pass on both parser backends; no production code
change was needed.

## Tty printable decoder head

`tests/test_tmux_regress_tty_keys_printable_head.py` carries the next 22
source identities from `regress/tty-keys.sh`: plain and Meta forms of ASCII
Space through asterisk.  Each source decoder identity executes the inverse
public frontend operation using the physical layout key, its Shift-selected
codepoint where needed, and the matching text event.

The audited terminal encoders agree 8:0 that the plain operation emits the
selected ASCII byte and Meta/Alt adds one leading ESC; Kitty's legacy keyboard
protocol supplies the independent protocol vote.  This also avoids inheriting
the stray shell `=` tokens on four Meta assertions in the tmux script as if
they described a terminal operation.  All 22 adaptations and the inventory
guard pass on both parser backends; no production code change was needed.

## Tty printable decoder continuation

`tests/test_tmux_regress_tty_keys_printable_mid.py` carries the next 22 source
identities from `regress/tty-keys.sh`, plain and Meta forms of ASCII plus
through `5`.  Each uses the same inverse public frontend adaptation and
layout/text event pair as the preceding printable batch.

All eight audited terminal encoders and Kitty's legacy protocol agree on the
selected ASCII byte and one leading ESC for Meta.  All 22 adaptations and the
inventory guard pass on both parser backends; no production code change was
needed.

`tests/test_tmux_regress_tty_keys_printable_more.py` continues that adaptation
with the next 22 source identities, plain and Meta forms of ASCII `6` through
`@`.  Shifted punctuation is driven through its physical number-row or
punctuation key.  The vote remains 8:0 plus Kitty's legacy protocol; all 22
adaptations and the inventory guard pass on both parser backends without a
production code change.

`tests/test_tmux_regress_tty_keys_printable_upper_head.py` carries the next 22
source identities, plain and Meta forms of uppercase `A` through `K`.  Every
scenario uses a Shift-modified physical letter plus its uppercase text event.
The vote remains 8:0 plus Kitty's legacy protocol; all 22 adaptations and the
inventory guard pass on both parser backends without a production code change.

`tests/test_tmux_regress_tty_keys_printable_upper_mid.py` carries the following
22 identities, plain and Meta forms of uppercase `L` through `V`, through the
same layout/text frontend path.  The 8:0 implementation vote and Kitty
protocol vote are unchanged; both parser backends pass all adaptations and the
inventory guard without a production code change.

`tests/test_tmux_regress_tty_keys_printable_upper_tail.py` carries the next 22
identities, plain and Meta forms of `W` through lowercase `a`, including the
intervening ASCII punctuation.  Caret and underscore use their shifted
physical keys.  The 8:0 implementation vote and Kitty protocol vote remain
unchanged; both parser backends pass every adaptation and the inventory guard
without a production code change.

`tests/test_tmux_regress_tty_keys_printable_lower_head.py` carries the next 22
source identities, plain and Meta forms of lowercase `b` through `l`, through
the layout/text frontend path.  The 8:0 implementation vote and Kitty protocol
vote remain unchanged; both parser backends pass all adaptations and the
inventory guard without a production code change.

`tests/test_tmux_regress_tty_keys_printable_lower_mid.py` carries the following
22 identities, plain and Meta forms of lowercase `m` through `w`, through the
same public frontend path.  The 8:0 implementation vote and Kitty protocol
vote are unchanged; both parser backends pass every adaptation and the
inventory guard without a production code change.

## Tty printable tail and application keypad head

`tests/test_tmux_regress_tty_keys_printable_tail_keypad.py` carries the final
16 printable identities in `regress/tty-keys.sh`, lowercase `x` through DEL,
and the next four application-keypad identities: plain/Meta `KPEnter` and
`KP*`.  Printable characters retain the layout/text frontend adaptation;
Backspace uses the named-key path in reset DECBKM state.  The printable and
Backspace votes are the 8:0 consensuses already documented above.

For a modified application keypad key, the tmux decoder fixture recognizes an
extra ESC before an unmodified SS3 sequence.  That is not the current terminal
encoder consensus.  Ghostty, xterm, iTerm2, VTE and foot encode Alt inside the
SS3 sequence as modifier 3 (`SS3 3 M/j`); Contour uses the extra ESC prefix.
Alacritty and Kitty provide no applicable conventional DECKPAM encoder branch
and abstain, giving a 5:1 implementation vote.  Xterm's modified-key
specification supplies the independent protocol vote for modified SS3.

Shitty already had the complete modified application-keypad table, but its
initial `modifyKeypadKeys` resource was incorrectly zero, making the table
unreachable until an application changed the resource.  Initializing it to
xterm's `mfkOriginal` value 1 selects the existing table without adding state
or API.  All 20 adaptations and the inventory guard pass on both parser
backends.

`tests/test_tmux_regress_tty_keys_keypad_mid.py` carries the next 20 decoder
identities: plain and Meta application-keypad `KP+`, `KP-`, `KP.`, `KP/` and
`KP0` through `KP5`.  Plain keys retain their DEC SS3 sequences.  Meta keys
apply the same 5:1 implementation consensus plus xterm specification vote for
modifier 3 inside SS3.  All 20 adaptations and the inventory guard pass on
both parser backends without another production change.

## Tty keypad tail and cursor decoder head

`tests/test_tmux_regress_tty_keys_keypad_cursor.py` carries the next 20 source
identities: plain and Meta application-keypad `KP6` through `KP9`, all eight
SS3 cursor identities, and the first four normal-CSI cursor identities.  The
keypad cases complete the 5:1 plus specification modified-SS3 adaptation.

For cursor keys, the tmux fixture's external-ESC Meta form is again a decoder
input, not the current encoder result.  All eight audited terminals encode a
modified cursor key as `CSI 1 ; modifier final` in both normal and application
cursor modes, while unmodified application cursor keys use SS3.  Xterm's
modified-key specification supplies the independent protocol vote.  The tests
therefore enter DECCKM for the SS3 identities and assert `CSI 1 ; 3 A/B/C/D`
for their Meta operations.  All 20 adaptations and the inventory guard pass
on both parser backends without a production change.

## Tty cursor, Home/End and rxvt arrows

`tests/test_tmux_regress_tty_keys_cursor_home_rxvt.py` carries the next 20
source identities: the remaining normal cursor keys, Home and End in normal
and application cursor forms, and all eight rxvt arrow decoder cases.
Unmodified application cursor operations retain SS3; modified normal or
application operations use the already audited `CSI 1 ; modifier final`
encoding.

The rxvt lowercase-final sequences are a legacy decoder dialect rather than a
current encoder consensus.  Their corresponding public Control/Shift+Arrow
operations have an 8:0 implementation vote for modifier parameters 5 and 2,
respectively, and xterm's modified-key specification supplies the protocol
vote.  All 20 adaptations and the inventory guard pass on both parser backends
without a production change.

### Audited revisions

| implementation | relevant source | revision |
| --- | --- | --- |
| Alacritty | `alacritty/src/input/keyboard.rs` | `1b2b36a64e88` |
| Ghostty | `src/input/key_encode.zig`, `src/input/function_keys.zig` | `fad7f854e8f9` |
| Kitty | `kitty/key_encoding.c`, `docs/keyboard-protocol.rst` | `2caa3ca16bc9` |
| xterm | `input.c`, `charproc.c`, `terminfo`, `ctlseqs.txt` | `6380a3eaed85` |
| Contour | `src/vtbackend/InputGenerator.cpp` | `c51e15ed254e` |
| iTerm2 | `sources/Keyboard/iTermStandardKeyMapper.m`, `sources/VT100/VT100Output.m`, `sources/PTYSession/PTYSession.m` | `3ec57866cd9b` |
| VTE | `src/keymap.cc`, `src/vte.cc` | `3d55bbdddb87` |
| foot | `input.c`, `keymap.h` | `a635e0a196d9` |

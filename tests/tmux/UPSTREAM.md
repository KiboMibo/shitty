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

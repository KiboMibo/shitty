# xterm.js upstream tests

Source: https://github.com/xtermjs/xterm.js

Revision: `699f5537b0232e444cb98261b8b3991c3cfecb5e`

Imported path: `test/fixtures/escape_sequence_files/*.in` and matching
`*.text` files.

The data is unmodified. `adapter.py`, `file_names.txt`, and `xfail.txt` are
Shitty integration files. The upstream MIT license is preserved in
`LICENSE.upstream`.

## InputHandler semantic tests

Source revision: `6ccafd97791ed8c6bf05662708a0745b1d085023`.

The first 20 cases from `src/common/InputHandler.test.ts` are represented as
20 distinct public terminal scenarios in
`tests/test_xtermjs_input_handler_core.py`. Private `InputHandler` and buffer
field mutations were translated to their wire-visible consequences: CSI/ESC
input, mode and DECRQSS replies, cursor/cell state, scrollback size, and
soft-wrap topology. Existing broader Shitty tests did not replace any source
case.

Fifteen scenarios pass on both parser backends. Five remain executable
expected failures:

- xterm.js mode 45 follows only soft-wrapped rows and exposes a cursor at
  `x == cols`; Shitty's pending-wrap representation does not reproduce the
  same repeated `BS SP BS` traversal at the vertical margins;
- xterm.js saves DECAWM in DECSC state, while Shitty deliberately leaves the
  terminal mode unchanged on DECRC;
- xterm.js can disable mode 2031 with its private
  `vtExtensions.colorSchemeQuery` option; Shitty has no equivalent policy
  option and always exposes the supported mode;
- xterm.js stores the soft-wrap link on the continuation row and clears that
  link when EL erases the whole continuation. Shitty stores it on the
  predecessor and currently retains it in that case;
- xterm.js has a non-standard `scrollOnEraseInDisplay` option that turns ED 2
  into a scrollback-producing operation. Shitty implements ordinary ECMA-48
  erase and has no such option.

The behavior audit used these pinned implementations:

| implementation | revision |
| --- | --- |
| Alacritty | `1b2b36a64e88` |
| Ghostty | `7e463bc65d43` |
| Kitty | `0d3259f87d1c` |
| xterm | `6380a3eaed85` |
| Contour | `c51e15ed254e` |
| iTerm2 | `3ec57866cd9b` |
| VTE | `3d55bbdddb87` |
| foot | `a635e0a196d9` |

### BufferReflow case 7 and SelectionService cases 1 through 19

This batch accounts for 20 source cases. The final BufferReflow case is added
to `tests/test_xtermjs_buffer_reflow.py`, and the first 19 SelectionService
cases are represented one-for-one in `tests/test_xtermjs_selection_service.py`.
Seventeen pass on both parser backends. The selection cases use real
double-click, triple-click and drag gestures instead of exposing xterm.js's
private selection model.

The final reflow case grows a wrapped block while the cursor is outside it.
Alacritty, Ghostty, Kitty, Contour, iTerm2, VTE and foot reflow such content;
xterm keeps physical rows on resize. There is no terminal standard for resize
reflow, so Shitty retains the 7-to-1 supporting consensus and passes the case.

Three exact xterm.js word-selection policies remain executable expected
failures:

- when the click lands on an opening or closing path delimiter, xterm.js
  includes that delimiter and continues through the adjacent word. Contour is
  the only audited implementation with this exact asymmetric scan. Xterm and
  VTE select individual punctuation; Ghostty and foot select delimiter runs;
  Alacritty applies bracket matching; Kitty does not begin ordinary word
  selection on punctuation; and iTerm2 uses its separate ICU `other` class.
  With no implementation consensus and no applicable terminal standard,
  Shitty keeps its Unicode-class selection;
- xterm.js also treats emoji and adjacent letters as one word because emoji
  are absent from its configurable separator list. Alacritty, Ghostty,
  Contour and foot do the same. Kitty, xterm, iTerm2 and VTE classify symbols
  separately from letters and numbers. This 4-to-4 split has no standard
  tiebreaker, so both the ordinary and tag-sequence emoji source cases remain
  policy XFAIL rather than changing Shitty's existing behavior.

The audit used freshly updated repositories:

| implementation | revision |
| --- | --- |
| xterm.js | `29a738423349` |
| Alacritty | `1b2b36a64e88` |
| Ghostty | `951a03b58bf6` |
| Kitty | `e95da80fdbbf` |
| xterm | `6380a3eaed85` |
| Contour | `c51e15ed254e` |
| iTerm2 | `3ec57866cd9b` |
| VTE | `3d55bbdddb87` |
| foot | `a635e0a196d9` |

No production change was made in this batch.

### UnicodeV6, UnicodeService, CharsetService, and XParseColor

All 17 terminal-relevant cases in `src/common/input/UnicodeV6.test.ts`,
`src/common/services/UnicodeService.test.ts`,
`src/common/services/CharsetService.test.ts`, and
`src/common/input/XParseColor.test.ts` at xterm.js revision
`29a738423349` are represented one-for-one in
`tests/test_xtermjs_unicode_charset_color.py`.  With the inventory guard, both
parser backends run 18 public tests: 12 pass and six exact source-policy or
product gaps remain executable expected failures.

The browser-only `src/common/Color.test.ts` tests CSS alpha, blending,
contrast, luminance, and canvas-facing conversion helpers.  They are not
terminal color parsing and have no component in Shitty.  They were not
counted among the 17 cases and no CSS/alpha product API was invented for
them.

The source's frozen Unicode-6 default is an xterm.js provider policy, not an
implementation consensus.  Alacritty, Ghostty, Kitty, xterm, Contour, VTE,
and foot use their compiled or host width tables; iTerm2 defaults its explicit
selector to Unicode 8.  The Unicode standard defines versioned properties but
does not prescribe an emulator's default version.  Shitty therefore keeps its
host-libc default and the exact `Uw6` source expectation is an XFAIL.

Historical selection itself exposes two concrete gaps.  Xterm.js's V6
provider treats U+1F923, which was not assigned until Unicode 9, as one cell.
iTerm2 is the only audited main implementation with a selectable pre-9 table;
its `iTermCharacterWidth` has separate supplementary-plane tables for Unicode
8 and 9 and likewise leaves U+1F923 narrow in version 8.  The other seven do
not expose historical tables and abstain.  Unicode 8's East Asian Width data
also does not assign U+1F923 a Wide or Fullwidth value.  Shitty's
`-unicodeWidths 8` currently undoes width changes for older assigned emoji but
still applies the current Wide property to this then-unassigned codepoint.
Both the exhaustive-provider adaptation and the explicit ten-emoji source
case retain that gap as XFAILs.

Runtime provider replacement is observable in Shitty through configuration
reload: its advertised `Uw` changes and the notification adaptation passes.
However, a codepoint measured before reload remains in Vterm's
`unicodeProperties` cache, so reloading 8 to 17 does not change that
codepoint's cell width.  Xterm.js replaces its provider, while iTerm2's
runtime version setter switches the width table; the other implementations
do not offer this operation and abstain.  The stale-cache result is the fourth
Unicode XFAIL.  Xterm.js and iTerm2 also reject versions outside their
registered/supported sets, whereas Shitty accepts any numeric cutoff from 0
through 99 and advertises it.  With the other seven implementations and the
standard abstaining on such a selector, the exact unknown-provider case is
kept as a policy XFAIL rather than silently discarded.

The four charset cases pass through real G1 designation, SO invocation, and
DECSTR/RIS reset.  Alacritty, Ghostty, Kitty, xterm, Contour, iTerm2, VTE, and
foot all keep designation separate from invocation and restore the default GL
state on reset; ECMA-35 supplies the same independent standard vote.

Six of seven XParseColor cases pass through OSC 12 set/query round trips for
every 4-, 8-, 12-, and 16-bit source vector, upper-case input, 16-bit replies,
and reduction to the terminal's eight-bit RGB storage.  The remaining source
case calls a standalone xterm.js helper narrower than XParseColor itself and
rejects mixed component widths and `rgbi:`.  Xlib's XParseColor grammar permits
independent one-to-four-digit components; xterm, Alacritty, Ghostty, Kitty,
Contour, iTerm2, and foot accept the audited mixed-width form, while VTE
rejects it.  Xterm and Ghostty also explicitly implement `rgbi:`.  Shitty
keeps its broader grammar under the 7-to-1 vote, so that source case remains
the sixth XFAIL.

The audit used these revisions:

| implementation | revision |
| --- | --- |
| xterm.js | `29a738423349` |
| Alacritty | `1b2b36a64e88` |
| Ghostty | `b0b9fbc8d5b0` |
| Kitty | `2caa3ca16bc9` |
| xterm | `6380a3eaed85` |
| Contour | `c51e15ed254e` |
| iTerm2 | `3ec57866cd9b` |
| VTE | `3d55bbdddb87` |
| foot | `a635e0a196d9` |

No production change was made in this batch.

### All 20 Params cases

All 20 cases from `src/common/parser/Params.test.ts` are represented in their
source order by 20 separate executable methods in
`tests/test_xtermjs_params.py`; an inventory method proves that no source name
was merged or lost. All 21 public tests pass on both Ragel parser backends.

`Params` is a private xterm.js callback container, not a terminal protocol
object. Its constructor-selected typed-array capacities, `fromArray`, `clone`
and borrowed `getSubParams` views therefore were not copied into Shitty's API.
Their distinct observable contracts are still exercised: ordinary and grouped
parameters round-trip through parser trace, a completed rendition remains on
its cell after later parser reuse, every grouped SGR consumer receives its own
values, aborted and completed sequences clear independently, and a colon
digit accumulator survives an arbitrary input chunk boundary.

ECMA-48 fifth edition sections 5.4.1 through 5.4.3 are the protocol oracle.
They define digits and `:` (03/10) inside a parameter substring, `;` (03/11)
between substrings, and leading, trailing or adjacent semicolons as empty
parameters whose value is control-function-specific. They do not define a
maximum for a variable parameter list. Annex F.4.2 also says ZDM is deprecated
and distinguishes an omitted parameter from an explicit zero; the public test
therefore checks bare SGR's specified default effect instead of requiring the
xterm.js container's internal zero sentinel. A minus sign is an Intermediate
Byte, not a numeric Parameter Byte, so the two private negative-value guard
cases become public malformed-CSI recovery cases.

All eight implementations were inspected after updating them. Their data
structures differ without making grouped parameters unsupported:

| implementation | storage and limit | numeric/overflow policy |
| --- | --- | --- |
| Alacritty | 32 combined values, grouped slices | `u16` saturation; an extra value marks the sequence ignored |
| Ghostty | 24 values plus colon-separator bits | `u16` saturation; an extra value drops the sequence |
| Kitty | 256 values plus subparameter flags | 17-digit accumulator dispatched as `int`; also accepts a private signed-number extension |
| xterm | 30 values plus subparameter ordinals | clamps at 65535; excess separators stop advancing and later digits can reach the last slot |
| Contour | 16 values plus a subparameter mask | clamps at 65535; excess digits remain on the last slot |
| iTerm2 | 16 main and 16 separately indexed subparameters | omitted is `-1`; numeric overflow is unrecognized and excess groups are discarded |
| VTE | 32 combined final/nonfinal arguments | clamps at 65535; an extra value moves to CSI ignore |
| foot | 16 main values and 16 subparameters per main value | unsigned accumulation; overflow groups use detached dummy storage |

Alacritty, xterm, Contour, iTerm2, VTE and foot accept a leading colon,
although iTerm2 represents the first following number as the main parameter;
Ghostty and Kitty reject that form. The six supporting implementations make
the accepted public form the oracle, while Shitty retains the empty leading
component in its richer trace. All implementations support ordinary and SGR
colon grouping, defaults, reset and chunked parsing. Kitty alone interprets
`-` as a signed-parameter extension; the other seven follow the ECMA byte
classes and reject a digit after that intermediate.

There is deliberately no invented consensus for capacity or integer width.
Limits range from 16 to 256; Alacritty, Ghostty and VTE drop an overflowing
sequence, while xterm, Contour, iTerm2 and foot retain a bounded prefix and
Kitty has not reached its cap at xterm.js's 33-value boundary. Exact clamps
range from 65535 through `INT32_MAX` to Shitty's `UINT32_MAX`. Accordingly the
ported overflow cases assert common public safety properties--no negative
cursor arithmetic, no poisoning of the next SGR/CSI transaction and complete
recovery--rather than xterm.js's private bit width. The existing 32/33 Shitty
boundary remains an explicitly named product-policy regression, not a claim
that ECMA-48 or the implementation vote mandates 32.

The audit used freshly updated repositories:

| implementation | parser source | revision |
| --- | --- | --- |
| xterm.js | `src/common/parser/Params.ts` and `Params.test.ts` | `29a738423349` |
| Alacritty | `alacritty-vte/src/params.rs`, `src/lib.rs` | `1b2b36a64e88` / `3b3da71c34cc` |
| Ghostty | `src/terminal/Parser.zig`, `parse_table.zig` | `94d775fefc21` |
| Kitty | `kitty/vt-parser.c` | `edc132c98b4e` |
| xterm | `charproc.c`, `ptyx.h` | `6380a3eaed85` |
| Contour | `Sequence.hpp`, `SequenceBuilder.hpp` | `c51e15ed254e` |
| iTerm2 | `iTermParser.h`, `VT100CSIParser.m` | `3ec57866cd9b` |
| VTE | `parser.hh`, `parser-arg.hh` | `3d55bbdddb87` |
| foot | `vt.c`, `terminal.h`, `vt.h` | `a635e0a196d9` |

No production change was needed in this batch.

### EscapeSequenceParser cases 4 through 24

These 21 state-transition cases from
`src/common/parser/EscapeSequenceParser.test.ts` are represented one-for-one
in `tests/test_xtermjs_escape_sequence_parser.py`; an additional inventory
test proves that their 21 source names are distinct. They cover the complete
C0 execute and ASCII print ranges, anywhere cancellation/restart and 8-bit C1
transitions, ESC and ESC
intermediate collection/dispatch, and CSI entry/parameter collection and
dispatch.

The private xterm.js parser state and callback arrays were not copied into the
product. Each state is reached with a real terminal byte prefix, then observed
through parser trace, terminal actions, text and protocol recovery. Raw C1 is
tested after `ESC %@`, Shitty's existing ISO single-byte selector; UTF-8 input
continues to treat continuation bytes as part of their code point. APC's three
private parser phases collapse to the one public APC control-string state, but
all three source paths remain separate executable inputs.

ECMA-48 5th edition section 5.4 defines CSI parameter bytes as 03/00 through
03/15, intermediate bytes as 02/00 through 02/15, and final bytes as 04/00
through 07/14. Its sections 5.2 and 5.3 define the C0 and C1 sets and the
7-bit/8-bit introducer forms. Those byte classes agree with the Alacritty,
Ghostty, xterm, Contour, VTE and foot state machines. iTerm2 separately
collects embedded C0 incidentals and enables C1 transitions in ASCII and
Latin-1 modes. Kitty implements the applicable C0, ESC and CSI operations but
explicitly supports only C0 controls, so it abstains on raw C1 behavior rather
than voting against the supported implementations.

The audit also exposed four parser defects in Shitty: C0 inside ESC lost the
pending sequence; the specialized `ESC SP`, `ESC #` and `ESC %` states let
C0/DEL overlap their final-byte fallback; reserved C1 bytes in ESC/CSI were
misclassified as sequence finals or invalid parameters; and `ESC` followed by
a non-ST byte while ignoring DCS remained in DCS ignore instead of starting a
fresh escape. The grammar now uses disjoint ECMA-48 byte classes, preserves
the pending ESC across C0 execution, and applies C1-anywhere recovery without
changing invalid UTF-8 handling in control strings.

The audit used freshly updated repositories:

| implementation | revision |
| --- | --- |
| xterm.js | `29a738423349` |
| Alacritty | `1b2b36a64e88` |
| Ghostty | `94d775fefc21` |
| Kitty | `cf136a233ccc` |
| xterm | `6380a3eaed85` |
| Contour | `c51e15ed254e` |
| iTerm2 | `3ec57866cd9b` |
| VTE | `3d55bbdddb87` |
| foot | `a635e0a196d9` |

### EscapeSequenceParser cases 25 through 47

The next 23 source cases, from the first CSI-intermediate transition through
`state DCS_PARAM param action`, are represented by 23 separate executable
methods in `tests/test_xtermjs_escape_sequence_parser.py`. Upstream itself
contains `trans CSI_PARAM --> CSI_IGNORE` twice; the inventory therefore
records 23 entries but 22 distinct source names in this batch.

The CSI cases exercise every intermediate byte, every final byte, all C0 and
DEL positions, both routes into CSI ignore, and its final-byte recovery. The
leading-colon case is retained rather than dropped because implementations
differ: Alacritty, xterm, Contour, iTerm2, VTE and foot accept it as the first
subparameter separator, while Ghostty and Kitty reject it. The supporting
majority and ECMA-48 section 5.4.2's parameter-substring grammar make the
accepted form the oracle. All eight reject a private marker after ordinary
parameters and a parameter byte after an intermediate, although Kitty and
iTerm2 express rejection without a dedicated ignore state.

SOS and PM expose a representation difference rather than an unsupported
feature. Alacritty, xterm, VTE and foot discard their bodies; Contour captures
PM but discards SOS; Ghostty and Kitty collect the strings before ignoring
unknown commands. Shitty keeps its existing parser-trace payload for both, and
the tests assert the common public rule: the payload is inert until ST and the
next graphic is processed in ground. This deliberately tests Shitty's richer
representation instead of omitting the cases merely because the callback
shape differs from xterm.js.

For OSC, Alacritty, Ghostty, Contour, VTE and foot discard embedded C0 bytes,
whereas xterm, iTerm2 and Kitty use tolerant whole-string accumulators. The
five state-machine implementations form the consensus for xterm.js's C0
ignore cases; all eight agree that those C0 bytes have no independent terminal
side effect. Every ASCII payload byte is tested separately. Both 7-bit and
raw-C1 OSC, SOS, PM and DCS introducers are also driven from every reachable
public parser state.

The DCS header audit found a real Shitty defect. Alacritty, Ghostty, VTE and
foot explicitly ignore C0 in DCS entry and parameter states; iTerm2, Kitty and
xterm do not execute those bytes while accumulating the string. Contour agrees
in DCS entry but executes them in DCS parameter state, making it the sole
dissent there. Shitty used the generic executing `sequenceC0` rule in all
three DCS header states, so BEL rang. The header now consumes C0 without
execution. A second defect was the generic SOS/PM/APC string-data action
executing C0 while tracing it; every implementation agrees such string content
must be terminal-inert, so that side effect was removed without discarding the
trace payload.

ECMA-48 5th edition section 5.4 supplies the CSI parameter, intermediate and
final byte classes. Section 5.6 defines a control string as an opening
delimiter, a command or character string, and ST; it permits arbitrary
character-string bit combinations except SOS and ST, but does not turn their
C0 values into independent presentation actions. The implementation audit
used these concrete sources after updating every repository:

| implementation | parser source | revision |
| --- | --- | --- |
| Alacritty | `alacritty-vte/src/lib.rs` (`vte` 0.15.0) | `1b2b36a64e88` / `3b3da71c34cc` |
| Ghostty | `src/terminal/parse_table.zig` | `94d775fefc21` |
| Kitty | `kitty/vt-parser.c` | `3705a7fcd155` |
| xterm | `VTPrsTbl.c`, `charproc.c` | `6380a3eaed85` |
| Contour | `src/vtparser/Parser-impl.hpp` | `c51e15ed254e` |
| iTerm2 | `VT100CSIParser.m`, `VT100DCSParser.m`, `VT100XtermParser.m` | `3ec57866cd9b` |
| VTE | `src/parser.hh` | `3d55bbdddb87` |
| foot | `vt.c` | `a635e0a196d9` |
| xterm.js source cases | `src/common/parser/EscapeSequenceParser.test.ts` | `29a738423349` |

### EscapeSequenceParser cases 48 through 70

The next 23 source cases, from the DCS leading-colon transition through APC
termination, are represented by 23 further executable methods in
`tests/test_xtermjs_escape_sequence_parser.py`. Upstream repeats
`trans DCS_INTERMEDIATE --> DCS_IGNORE`, just as it previously repeated a CSI
ignore transition. The batch therefore has 23 entries and 22 distinct names.

The DCS tests drive every parameter, intermediate, final, payload and ignored
byte through real terminal input. The source expectation that a leading colon
enters `DCS_PARAM` is not used as the oracle. Alacritty and VTE accept DCS
subparameters; Ghostty, xterm, Contour, iTerm2 and foot reject a colon in DCS
entry or parameter state. Kitty collects an opaque DCS string and therefore
does not vote on the header-state distinction. The 5-to-2 supporting consensus
makes the sequence invalid in Shitty and keeps it ignored through ST.

All eight implementations agree that C0 after a DCS final is not executed as
an independent terminal command. Alacritty, Ghostty, Contour, VTE and foot
explicitly send those bytes to the DCS payload handler; Kitty, xterm and iTerm2
retain the command string for later dispatch. Shitty previously called its
ground C0 executor from generic and structured DCS payload states, so BEL could
ring while parsing a DCS. Payload C0 is now delivered to the DCS command parser
and trace without a terminal side effect. DEL remains ignored.

ECMA-48 5th edition sections 5.6 and 8.3.27 define a DCS command string more
narrowly as 00/08 through 00/13 and 02/00 through 07/14. That standard vote
supports the ordinary command-byte subset but rejects the extra C0 values that
the eight terminal implementations tolerate. The implementation consensus is
kept for those extension bytes because it is unanimous and preserves payload
instead of turning it into unrelated presentation actions.

xterm.js splits APC into entry, intermediate and passthrough callbacks, while
ECMA-48 section 8.3.2 defines only one APC command string. Shitty likewise has
one public APC string and exposes its complete inert payload through parser
trace. Every xterm.js state path remains a separate executable input: both
introducers from every parser state, every intermediate and start byte, both
C0 classes, DEL, ST, CAN, SUB and ESC recovery. Ghostty, Kitty and iTerm2 also
collect APC bodies; the implementations that discard unsupported APC commands
abstain on payload representation. Shitty preserves embedded C0 in its richer
trace but, like every implementation, gives it no terminal side effect; DEL is
ignored. On ESC followed by a non-ST byte, Shitty follows the shared control-
string recovery rule: abort the APC and process a fresh escape sequence rather
than emitting xterm.js's private successful-end callback.

The audit used freshly updated repositories:

| implementation | parser source | revision |
| --- | --- | --- |
| Alacritty | `alacritty-vte/src/lib.rs` (`vte` 0.15.0) | `1b2b36a64e88` / `3b3da71c34cc` |
| Ghostty | `src/terminal/parse_table.zig` | `94d775fefc21` |
| Kitty | `kitty/vt-parser.c` | `edc132c98b4e` |
| xterm | `VTPrsTbl.c`, `charproc.c`, `misc.c` | `6380a3eaed85` |
| Contour | `src/vtparser/Parser-impl.hpp` | `c51e15ed254e` |
| iTerm2 | `VT100DCSParser.m`, `VT100XtermParser.m` | `3ec57866cd9b` |
| VTE | `src/parser.hh` | `3d55bbdddb87` |
| foot | `vt.c` | `a635e0a196d9` |
| xterm.js source cases | `src/common/parser/EscapeSequenceParser.test.ts` | `29a738423349` |

### EscapeSequenceParser cases 71 through 91

The complete 21-case `escape sequence examples` block is represented by 21
new executable methods in `tests/test_xtermjs_escape_sequence_parser.py`.
The examples exercise
mixed text/control dispatch, OSC terminated by BEL, DCS and APC split across
input calls, 7-bit and 8-bit introducers and terminators, CSI subparameters,
malformed-sequence recovery, and CAN/SUB aborts.

The source names are retained verbatim even where they are factually wrong:
`print + PM(C1) + print` sends `0x98`, which ECMA-48 defines as SOS, and the
second DCS-labelled example sends `0x9f`, which is APC. Shitty's public trace
therefore records SOS and APC. This keeps both implemented control-string
features visible instead of dropping them merely because xterm.js exposes a
different callback representation.

Every audited parser retains an unfinished control-string state across input
calls. Alacritty-vte, Ghostty, xterm, Contour, iTerm2, VTE, foot and Kitty all
keep parser or saved-buffer state until a terminator arrives. Raw C1
introducers are configurable in xterm and iTerm2 and directly supported by
Ghostty and VTE; Alacritty, Kitty, Contour and foot abstain where their UTF-8
input path does not accept that representation. For strings started in a
supported form, Ghostty, xterm, Contour and iTerm2 accept either ST encoding;
Alacritty also accepts C1 ST for DCS. VTE deliberately rejects a DCS or OSC
whose introducer and terminator use different control sets. The supporting
majority and ECMA-48 section 9's equivalence of 7-bit and 8-bit control
representations make mixed ST valid in Shitty.

BEL termination of OSC is an xterm extension rather than the ECMA-48 rule, but
all eight implementations accept it. ECMA-48 sections 5.6 and 8.3.89 specify
ST; the unanimous implementation extension is the oracle for the BEL example.
APC, PM and SOS payloads are ignored by some implementations, parsed by
others, and dispatched to feature handlers by Ghostty, Kitty, Contour or
iTerm2. Those differences do not remove the cases: the common public effect is
that the payload does not leak into terminal text, while Shitty's existing
trace preserves the correctly typed string.

All eight parsers support colon inside a CSI parameter string. Their internal
representations differ between separator bits, flat subparameter flags and
nested values; ECMA-48 section 5.4.2 explicitly permits `03/10` as a separator
inside a parameter substring. The test therefore checks Shitty's normalized
public trace, including zeroes for empty fields. DCS has a different grammar:
Ghostty, xterm, Contour, iTerm2 and foot reject colon in its parameter state,
Alacritty and VTE accept it, and Kitty's opaque string collector abstains. The
5-to-2 consensus keeps the colon-DCS example ignored. The DCS abort examples
use a valid semicolon header so they reach an active DCS rather than merely
leaving its ignore state.

CAN or SUB aborts an active DCS, OSC and APC in Alacritty-vte, Ghostty, xterm,
Contour, iTerm2, VTE and foot. Kitty's whole-string scanner waits for BEL or
ST and is the sole dissent. ECMA-48 section 8.3.6 says CAN makes preceding
erroneous data ignored but leaves its precise application meaning to the
participants; section 8.3.148 defines SUB as an error replacement rather than
a control-string terminator. The 7-to-1 terminal consensus supplies the wire
behavior for both bytes. An aborted string produces no completed string trace,
the control remains observable, and following text is parsed in ground.

The Unicode recovery example also required a product correction. None of the
eight parsers reprints the non-ASCII graphic that invalidated an unfinished
CSI. They differ on whether the next ASCII byte terminates the old CSI or is
the first ground graphic, but decoded-codepoint parsers including VTE discard
the invalidating codepoint and recover immediately. Shitty previously used
`fhold` and reprocessed it as terminal text. For a valid UTF-8 lead byte it now
discards that codepoint, including continuation bytes split across `feed`
calls, then parses the following input in ground. Standalone malformed high
bytes, single-byte input and C1 cancellation keep their existing reprocessing
semantics.

The audit used freshly updated repositories:

| implementation | parser source | revision |
| --- | --- | --- |
| Alacritty | `alacritty-vte/src/lib.rs` (`vte` 0.15.0) | `1b2b36a64e88` / `3b3da71c34cc` |
| Ghostty | `src/terminal/parse_table.zig`, `Parser.zig` | `94d775fefc21` |
| Kitty | `kitty/vt-parser.c` | `edc132c98b4e` |
| xterm | `VTPrsTbl.c`, `charproc.c` | `6380a3eaed85` |
| Contour | `src/vtparser/Parser-impl.hpp` | `c51e15ed254e` |
| iTerm2 | `VT100CSIParser.m`, `VT100DCSParser.m`, `VT100XtermParser.m` | `3ec57866cd9b` |
| VTE | `src/parser.hh` | `3d55bbdddb87` |
| foot | `vt.c` | `a635e0a196d9` |
| xterm.js source cases | `src/common/parser/EscapeSequenceParser.test.ts` | `29a738423349` |

### EscapeSequenceParser cases 92 through 112

The next 21 source cases are represented by 21 new executable methods in
`tests/test_xtermjs_escape_sequence_parser.py`: the four error-coverage cases,
the print and ESC handler cases, seven ESC custom-handler lifecycle cases, the
CSI handler case, and seven CSI custom-handler lifecycle cases. All pass on
both Ragel parser backends.

xterm.js's runtime handler stacks, boolean fallback chain and disposable
registrations are private embedding APIs. None of the eight audited terminals
has a corresponding wire operation: Alacritty uses a fixed `Perform`
interface, Ghostty fixed parser actions, Kitty and xterm direct dispatch,
Contour a template listener, iTerm2 fixed parser tokens, VTE generated fixed
sequence commands, and foot fixed action functions. Those implementations
therefore abstain on stack ordering and disposal as object-lifetime policies.
The tests retain every named case as a separate public scenario: known ESC and
CSI dispatch use NEL and SGR, unknown dispatch verifies no fallback effect,
wire order verifies sequential effects, and repeated unknown sequences verify
that later standard dispatch remains intact. No handler-registration hook was
added to Shitty or its harness.

NEL and SGR themselves are implemented by all eight terminals. ECMA-48 fifth
edition sections 8.3.86 and 8.3.117 define their cursor and rendition effects,
so those standard operations provide the oracle for the public adaptations.
The print-handler source input is likewise exercised end to end: text runs,
UTF-8 selection through `ESC % G`, NEL, SGR, CR/LF and OSC all remain visible
through their normal public effects and parser trace.

The CSI error case exposes a genuine recovery-policy split after a non-grammar
Unicode codepoint. Xterm, Kitty, VTE and foot recover to ground immediately;
Alacritty-vte, Ghostty, Contour and iTerm2 remain in CSI ignore until an ASCII
final byte. ECMA-48 section 5.4 excludes that codepoint from the CSI grammar
but does not define resynchronization for it. With no implementation majority
or standard tie-break, the test records Shitty's existing decoded-input policy:
discard the invalidating codepoint and resume in ground. It does not claim the
xterm.js recovery choice as a consensus requirement.

Malformed DCS headers are ignored through ST without leaking their payload in
Alacritty-vte, Ghostty, Kitty, xterm, iTerm2 and VTE. Contour leaks selected
high bytes from its DCS-ignore state and foot can leave that state when a UTF-8
continuation resembles C1 ST; they are the two dissenters. ECMA-48 sections
5.6 and 8.3.27 define DCS as a control string terminated by ST, reinforcing
the 6-to-2 no-leak behavior.

The upstream DCS passthrough callback accepts arbitrary Unicode codepoints,
but that is not a terminal-protocol requirement. Alacritty-vte, Ghostty,
Contour, iTerm2 and foot retain only the permitted low-byte payload; xterm's
ordinary DCS path also supplies no wide-codepoint payload. Kitty's opaque
collector and VTE preserve the original bytes. ECMA-48 section 8.3.27 is
decisive: a DCS command string is restricted to selected C0 bytes and
`02/00` through `07/14`, excluding non-ASCII Unicode. Shitty's generic unknown
DCS collector was corrected to discard a complete UTF-8 codepoint while
remaining inside DCS. In particular, a `0x9c` continuation byte is not
mistaken for ST. Specialized DCS handlers are unchanged.

A standalone C1 ST in ground remains observable in Shitty's parser trace but
has no screen effect. Alacritty-vte, xterm, iTerm2 and VTE also expose it to a
fixed parser callback or token; Ghostty treats it as no action, while Kitty,
Contour and foot do not provide a comparable raw-C1 input vote. This preserves
the distinction between parser observability and terminal presentation.

The audit used freshly updated repositories:

| implementation | parser/dispatch source | revision |
| --- | --- | --- |
| Alacritty | `alacritty-vte/src/ansi.rs` (`vte` 0.15.0) | `1b2b36a64e88` / `3b3da71c34cc` |
| Ghostty | `src/terminal/stream.zig`, `stream_terminal.zig` | `94d775fefc21` |
| Kitty | `kitty/vt-parser.c` | `edc132c98b4e` |
| xterm | `VTPrsTbl.c`, `charproc.c` | `6380a3eaed85` |
| Contour | `src/vtparser/Functions.hpp`, `Screen.cpp` | `c51e15ed254e` |
| iTerm2 | `VT100OtherParser.m`, `VT100CSIParser.m`, `VT100Terminal.m` | `3ec57866cd9b` |
| VTE | `src/parser-seq.py`, `vteseq.cc` | `3d55bbdddb87` |
| foot | `vt.c` | `a635e0a196d9` |
| xterm.js source cases | `src/common/parser/EscapeSequenceParser.test.ts` | `29a738423349` |

### EscapeSequenceParser cases 113 through 137

This batch accounts for the complete synchronous EXECUTE, OSC, DCS and APC
handler block: one EXECUTE case and eight cases for each string protocol. The
25 source cases have 4 new distinct names because the seven stack-lifecycle
names recur across handler classes. All pass on both Ragel parser backends.

The implementations expose parser handling at different layers rather than
sharing xterm.js's runtime stack API. Alacritty-vte calls one `Perform` object;
Ghostty emits typed stream actions into one handler; Kitty has fixed terminal
dispatch plus one callback per string class in its input parser; xterm buffers
strings and enters a fixed switch; Contour calls one `ParserEvents` listener;
iTerm2 produces tokens and installs protocol-specific DCS hooks; VTE returns a
typed `Sequence`; and foot calls fixed `action_hook`, `action_put` and
`action_unhook` functions. None supplies xterm.js's last-registered-first,
boolean-fallback, disposable stack. They abstain on that object-lifetime API,
but its source cases are not omitted: every lifecycle name remains a separate
executable public scenario for single dispatch, supported fallback, inert
unknown dispatch, ordering, and parser integrity after one or two discarded
sequences. No registration or disposal hook was added to Shitty or the test
harness.

The EXECUTE case uses CR followed by LF, exactly as its source input does.
Alacritty, Ghostty, Kitty, xterm, Contour, iTerm2, VTE and foot all dispatch
both controls to cursor movement. ECMA-48 fifth edition sections 8.3.15 and
8.3.74 define CR as movement to line home and LF as movement to the
corresponding position on the following line. The adaptation verifies callback
order, both screen effects, and a second CR/LF pair after the source's handler
clear point; clearing a parser callback itself has no terminal-wire operation.

OSC 1 has a real public effect among implementations that support it. Kitty,
xterm, Contour and iTerm2 set the icon label and keep it separate from the
window title. Alacritty and Ghostty implement only OSC 0/2 title changes, VTE
explicitly treats OSC 1 as a no-op because it has no icon-title property, and
foot parses but ignores OSC 1; those four abstain instead of voting against the
feature. The supporting 4-to-0 consensus keeps Shitty's icon-title event and
state. The base case checks the generic listener event and queries the stored
icon title through CSI 20 t. ECMA-48 section 8.3.89 supplies OSC framing but
leaves command interpretation to the operating system; xterm's control
sequence document supplies the OSC 1 selector meaning.

All eight implementations recognize the DCS header/body/ST structure, but
surface it differently. Alacritty, Ghostty and Contour expose streaming
hook/put/unhook callbacks; Kitty and xterm buffer before fixed dispatch; iTerm2
uses a state machine with hooks for tmux, SSH and sixel; VTE constructs a typed
DCS sequence and supports unripe dispatch; foot selects one fixed put handler
at the hook. The source's syntactically valid but unsupported `1;2;3+p`
identifier is consumed without text leakage or a reply by all eight. The base
test preserves its two input chunks and exact header and payload. The fallback
scenarios use DECRQSS as the supported fixed handler and require exactly one
reply; the no-fallback scenarios retain the unknown DCS and require none.
ECMA-48 section 8.3.27 defines the command string and ST boundary while
deliberately assigning command meaning to the device or IDCS, matching this
split between generic parsing and fixed protocol support.

APC likewise has implementations even though `+p` itself names no common
application protocol. Ghostty exposes start/feed/end and bounded unknown
capture, Kitty exposes an APC callback and implements Kitty graphics, Contour
exposes start/put/dispatch and forwards complete APC bodies, and iTerm2 emits a
generic APC token before handling tmux title or Kitty graphics forms.
Alacritty, xterm, VTE and foot recognize and consume APC framing but have no
applicable `+p` command and abstain on application semantics. Every supporting
implementation keeps a complete APC atomic and returns to ground at ST. The
tests therefore verify exact payload across chunk boundaries, one completed
trace event, inert unknown content, sequence order, and correct dispatch after
one or two preceding APCs. ECMA-48 section 8.3.2 defines the APC command string
and ST boundary and leaves its interpretation to the relevant application.

The audit used freshly updated repositories:

| implementation | relevant source | revision |
| --- | --- | --- |
| Alacritty | `alacritty-vte/src/lib.rs`, `src/ansi.rs` | `1b2b36a64e88` / `3b3da71c34cc` |
| Ghostty | `src/terminal/Parser.zig`, `stream.zig`, `stream_terminal.zig`, `apc.zig` | `94d775fefc21` |
| Kitty | `kitty/vt-parser.c`, `kitty/kittens.c` | `edc132c98b4e` |
| xterm | `VTPrsTbl.c`, `charproc.c`, `ctlseqs.ms` | `6380a3eaed85` |
| Contour | `src/vtparser/Parser-impl.hpp`, `ParserEvents.hpp`, `src/vtbackend/Functions.hpp` | `c51e15ed254e` |
| iTerm2 | `VT100XtermParser.m`, `VT100DCSParser.m`, `VT100Terminal.m` | `3ec57866cd9b` |
| VTE | `src/parser.hh`, `parser-seq.py`, `vteseq.cc` | `3d55bbdddb87` |
| foot | `vt.c`, `dcs.c`, `osc.c` | `a635e0a196d9` |
| xterm.js source cases | `src/common/parser/EscapeSequenceParser.test.ts` | `29a738423349` |

No production change was needed in this batch.

### EscapeSequenceParser cases 1 through 3 and 138 through 161

The final reconciliation compares the `PORTED_CASES` tuple directly with all
161 upstream `it(...)` names in source order. It found that the earlier batches
started at source case 4: `constructor`, `initial states`, and `reset states`
had been omitted. Those three cases and the final 24 cases are now represented
by 27 separate executable methods. The inventory is an exact 161-entry match,
including all repeated names; it has 133 distinct names and the module has 162
public tests including the inventory assertion. All pass on both Ragel parser
backends.

The three initialization cases do not justify exposing xterm.js's transition
table or mutable parser fields. A fresh terminal instead drives ESC, CSI, OSC,
DCS and APC through the parser constructed by the product, verifies that plain
text begins in ground with no stale parameter or string state, and resets from
five different unfinished states. Alacritty-vte's `Processor::new/reset`,
Ghostty's `Parser.init/reset`, Kitty's `reset_csi` and normal-state reset,
xterm's `ResetState`, Contour's `Parser::reset`, iTerm2's protocol parser state
entry, VTE's ground transition, and foot's `action_clear` all clear parser
state and accumulators. ECMA-48 fifth edition section 8.3.105 says RIS returns
the device to the state in which it became operational. Shitty's terminal reset
previously reset only the model, leaving the Ragel machine inside an unfinished
ESC, CSI or control string. `Parser::reset()` now reconstructs its protocol
state, reapplies the generated initial state, preserves the OSC 52 policy, and
cancels any unfinished trace event. The same implementation serves both parser
backends and is safe when RIS or DECSCL invokes it from inside `feed()`.

The ERROR callback itself is a private xterm.js embedding API. None of the
eight terminals publishes the same position/state/parameter record, so all
abstain on that callback shape. They do have observable recovery after a
non-grammar Unicode codepoint in CSI: xterm, Kitty, VTE and foot return to
ground immediately, while Alacritty-vte, Ghostty, Contour and iTerm2 ignore
through the next ASCII final. ECMA-48 section 5.4 excludes the codepoint but
does not specify malformed-stream resynchronization. With a 4-to-4 split and no
standard tie-break, the distinct ERROR case records Shitty's existing immediate
ground recovery: the euro sign is discarded and the following `;3m` is plain
text. It does not promote xterm.js's private callback record to a protocol
oracle.

The five identifier-limit cases separate wire grammar from callback-key
packing. ECMA-48 section 5.4 defines CSI parameter bytes `03/00..03/15`,
intermediates `02/00..02/15`, finals `04/00..07/14`, and explicitly places no
limit on the number of intermediates. Alacritty stores two intermediates;
Ghostty, VTE and foot store four; Contour uses an unbounded string. Kitty and
iTerm2's CSI dispatchers retain one generic intermediate, while xterm selects
named intermediate tables rather than exposing a generic identifier. The five
implementations with a generic two-byte path support the source operation;
the other three abstain on that representation. Tests enumerate every byte in
the source ranges and every one-byte private prefix, but do not copy xterm.js's
private two-byte ceiling: Shitty's existing three- and four-intermediate paths
remain executable.

For ESC, an intermediate is used so every `03/00..07/14` final can be driven on
the wire without turning `P`, `[`, `]`, `X`, `^`, or `_` into a control-string
introducer. For APC, xterm.js treats the first body byte as a callback
identifier with a `03/00..07/14` final. ECMA-48 sections 5.6 and 8.3.2 instead
define one application command string containing `02/00..07/14`; there is no
APC final field. Shitty therefore preserves `/` (`02/15`) as valid APC data and
ignores DEL rather than adopting the source API's rejection of `/`. Separate
ESC, CSI, DCS and APC invocation cases verify exact prefix/intermediate/final
order and distinguish every source identifier through public trace.

The final fourteen cases exercise xterm.js's Promise-returning handler
continuations. None of the eight terminal parsers implements that embedding
contract: Alacritty invokes one synchronous `Perform`, Ghostty emits a typed
action synchronously, Kitty and xterm dispatch directly, Contour calls one
`ParserEvents` listener, iTerm2 emits parser tokens, VTE returns one generated
`Sequence`, and foot invokes fixed action functions. They abstain on Promise
result, saved handler position and poisoned-continuation errors; ECMA-48 also
defines only the byte stream and effects. No async callback API was added to
Shitty. Every source case remains distinct and executable through its public
postcondition: the complete mixed stream stays ordered in one write, across
logical chunks and byte-at-a-time input; arbitrary splits cannot poison later
input; reset resumes at the next delivered codepoint; and repeated SGR, ESC,
OSC, DCS and APC operations dispatch exactly once in wire order.

The audit used freshly updated repositories:

| implementation | relevant source | revision |
| --- | --- | --- |
| Alacritty | `alacritty-vte/src/lib.rs`, `alacritty_terminal/src/ansi.rs` | `1b2b36a64e88` / `3b3da71c34cc` |
| Ghostty | `src/terminal/Parser.zig`, `parse_table.zig`, `stream.zig` | `94d775fefc21` |
| Kitty | `kitty/vt-parser.c` | `edc132c98b4e` |
| xterm | `VTPrsTbl.c`, `charproc.c` | `6380a3eaed85` |
| Contour | `src/vtparser/Parser-impl.hpp`, `ParserEvents.hpp`, `src/vtbackend/SequenceBuilder.hpp` | `c51e15ed254e` |
| iTerm2 | `VT100CSIParser.m`, `VT100DCSParser.m`, `VT100XtermParser.m` | `3ec57866cd9b` |
| VTE | `src/parser.hh`, `parser-glue.hh`, `parser-seq.py` | `3d55bbdddb87` |
| foot | `vt.c` | `a635e0a196d9` |
| xterm.js source cases | `src/common/parser/EscapeSequenceParser.test.ts` | `29a738423349` |

This batch required the parser-reset production fix described above; no
Promise handler or identifier-registration API was introduced.

### OscParser, DcsParser and ApcParser

All 63 current source cases are represented one-for-one and in source order in
`tests/test_xtermjs_control_string_parsers.py`: 23 `OscParser` cases, 20
`DcsParser` cases and 20 `ApcParser` cases. Their tuples retain repeated names
from the sync/async blocks and contain respectively 17, 14 and 14 distinct
names. The module has 64 public tests including the inventory assertion; all
pass on both Ragel parser backends.

The three xterm.js classes are embedding helpers, not wire protocols. Their
runtime handler stacks, disposable registrations, boolean fallback chain and
Promise-returning `end`/`unhook` continuations are private API. None of the
eight audited terminals exposes that contract:

| implementation | OSC | DCS | APC |
| --- | --- | --- | --- |
| Alacritty | one synchronous `Perform::osc_dispatch` | synchronous `hook`/`put`/`unhook` | syntactically consumed and ignored |
| Ghostty | one typed `osc.Command` | streamed `dcs_hook`/`dcs_put`/`dcs_unhook` actions | streamed `apc_start`/`apc_put`/`apc_end` actions |
| Kitty | one complete buffered dispatch | one complete buffered dispatch | one complete buffered graphics dispatch |
| xterm | one complete fixed dispatcher | one complete fixed dispatcher | syntactically consumed and ignored |
| Contour | one `startOSC`/`putOSC`/`dispatchOSC` listener | one hooked payload parser | one `startAPC`/`putAPC`/`dispatchAPC` listener |
| iTerm2 | one `VT100XtermParser` token | one token or protocol-specific hook | one `VT100_APC` token |
| VTE | one completed `Sequence` | one completed, optionally unripe `Sequence` | syntactically consumed and ignored |
| foot | one fixed `osc_dispatch` | fixed `dcs_hook`/`dcs_put`/`dcs_unhook` | syntactically consumed and ignored |

All eight therefore abstain on stack registration, disposal order, boolean
bubbling and Promise continuation state. The cases are not omitted: each is a
separate executable public scenario preserving the corresponding complete
string, fallback, repetition, chunking, cancellation or ordering
postcondition. Fixed dispatchers still exercise the implemented feature even
though they represent it without xterm.js's registration API. DCS parameters,
intermediates and payload, OSC identifier and payload, and the complete APC
body are verified through public parser trace; supported OSC 2 and DECRQSS
operations additionally verify their public effects or replies.

The source factory tests temporarily replace an internal payload cap with 100
bytes. That number is not a protocol boundary. ECMA-48 fifth edition sections
5.6, 8.3.2, 8.3.27, 8.3.89 and 8.3.143 define APC, DCS and OSC as control
strings closed by ST and specify their command-string byte range, but no
maximum length. The implementations deliberately choose different resource
policies:

| implementation | relevant retained payload policy |
| --- | --- |
| Alacritty | dynamically growing OSC buffer in the normal `std` build; streamed DCS |
| Ghostty | 2048-byte normal OSC capture, up to 8 MiB for allocating protocols; streamed DCS/APC |
| Kitty | 256 KiB general escape-code threshold, with chunked handling for large OSC 52 |
| xterm | configurable string maximum, normally 20,000 bytes or 600,000 with graphics |
| Contour | 50 KiB OSC/APC buffers; streamed DCS payload parsers |
| iTerm2 | 1 MiB general OSC/APC and DCS bounds, with protocol-specific exceptions |
| VTE | 4096 Unicode codepoints for completed OSC/DCS sequences |
| foot | dynamically grown OSC storage and streamed DCS |

Every implementation that semantically accepts the corresponding string
accepts both 100 and 101 ASCII payload bytes. The three `limit + 1` tests thus
assert successful 101-byte wire strings instead of copying the rejected
xterm.js wrapper expectation. Shitty's independent 1 MiB OSC and 4095-byte DCS
resource boundaries remain covered in `test_parser_states.py`; this batch does
not weaken or relabel them as consensus protocol limits.

The source `end(false)`/`unhook(false)` flag also has no single direct wire
encoding. CAN cancellation differs in practice: Alacritty, Ghostty, Contour
and foot finish an accumulated OSC on the transition; xterm and iTerm2 discard
it; Kitty retains CAN in its buffered body; VTE ignores CAN while staying in
OSC. ECMA-48 section 8.3.6 says preceding erroneous data shall be ignored but
leaves the exact application scope to sender and recipient. There is no
terminal consensus that could justify changing Shitty's existing discard
policy. The cancellation cases record that policy explicitly, while reset
cases verify the stronger common invariant: abandoning a partial OSC, DCS or
APC cannot leak its bytes or parser state into the next complete string.

The audit used freshly updated repositories:

| implementation | relevant source | revision |
| --- | --- | --- |
| Alacritty | `alacritty-vte/src/lib.rs`, `alacritty_terminal/src/ansi.rs` | `1b2b36a64e88` / `3b3da71c34cc` |
| Ghostty | `src/terminal/Parser.zig`, `src/terminal/osc.zig` | `94d775fefc21` |
| Kitty | `kitty/vt-parser.c` | `edc132c98b4e` |
| xterm | `VTPrsTbl.c`, `charproc.c`, `main.h` | `6380a3eaed85` |
| Contour | `src/vtparser/Parser-impl.hpp`, `src/vtbackend/SequenceBuilder.hpp` | `c51e15ed254e` |
| iTerm2 | `VT100XtermParser.m`, `VT100DCSParser.m` | `3ec57866cd9b` |
| VTE | `src/parser.hh`, `src/parser-string.hh` | `3d55bbdddb87` |
| foot | `vt.c`, `osc.c`, `dcs.c` | `a635e0a196d9` |
| xterm.js source cases | `OscParser.test.ts`, `DcsParser.test.ts`, `ApcParser.test.ts` | `29a738423349` |

No production change was required in this batch.

### Remaining SelectionService and SelectionModel cases

This batch accounts for the remaining 25 selection cases: the final seven
`SelectionService` cases and all 18 `SelectionModel` cases. They are represented
one-for-one in `tests/test_xtermjs_selection_tail.py`; 24 pass on both parser
backends and one remains an executable policy expected failure. Together with
the preceding batch, all 44 selection cases in the current upstream are now
accounted for.

The private `SelectionModel` API was not reproduced in the product or test
harness. Its externally observable invariants are exercised through real
selection operations: forward and reverse drags, word snapping across rows,
rectangular copying, clearing, scrollback trimming and copied text at a
physical line edge. The earlier `selectAll` adaptation was also corrected to
drag from the oldest scrollback row to the live screen; it now demonstrably
selects content beyond the viewport.

The sole mismatch is xterm.js's optional `mouseEventsRequireAlt` policy. With
that option enabled, an unmodified click bypasses application mouse reporting
and starts a local selection, while an Alt-click is sent to the application.
The exact policy has no implementation consensus:

- Alacritty hard-codes Shift as the mouse-reporting bypass in
  `alacritty/src/input/mod.rs`;
- Ghostty's `mouse-shift-capture` and XTSHIFTESCAPE state only choose whether
  Shift is reported or starts local selection;
- Kitty can reproduce the exact policy by mapping an unmodified left press in
  grabbed mode to `mouse_selection normal` and leaving Alt-click unhandled;
- xterm's `ShiftOverride` and XTSHIFTESCAPE operate only on Shift;
- Contour accepts a configurable but nonempty
  `bypass_mouse_protocol_modifier`; `None` disables bypass rather than making
  an unmodified click the bypass;
- iTerm2 uses Option in the opposite direction: Option suppresses application
  reports and forces a local selection;
- VTE starts local selection with Shift while mouse tracking is active and
  explicitly leaves XTSHIFTESCAPE unimplemented;
- foot has configurable `selection-override-modifiers`, but an empty modifier
  mask matches every modifier combination, not only an unmodified click.

No terminal standard specifies GUI ownership of mouse events. With only Kitty
among the main implementations supporting the exact xterm.js policy, Shitty
keeps its established Shift bypass and records the source behavior as an
executable XFAIL. The second xterm.js precedence case passes observably because
Alt remains owned by the application while mouse reporting is active.

The audit used freshly updated repositories:

| implementation | revision |
| --- | --- |
| xterm.js | `29a738423349` |
| Alacritty | `1b2b36a64e88` |
| Ghostty | `951a03b58bf6` |
| Kitty | `e95da80fdbbf` |
| xterm | `6380a3eaed85` |
| Contour | `c51e15ed254e` |
| iTerm2 | `3ec57866cd9b` |
| VTE | `3d55bbdddb87` |
| foot | `a635e0a196d9` |

No production change was made in this batch.

### KittyKeyboard protocol state, modifiers and C0 keys

The first 20 cases from `src/common/input/KittyKeyboard.test.ts` are represented
one-for-one in `tests/test_xtermjs_kitty_keyboard.py`. They cover the inactive
and active protocol states, every combination in the source modifier prefix,
and the first eight C0-key cases through Alt+Escape. All 20 pass on both parser
backends.

The private xterm.js `KittyKeyboard.evaluate()` helper was not copied. Each
scenario drives Shitty's public input path after negotiating the Kitty keyboard
flags on the terminal wire. Printable keys use separate physical/layout and
text events, exactly as the Wayland and Cocoa frontends do; Escape, Enter, Tab
and Backspace use frontend key events. The two private `shouldUseProtocol`
cases are observed through the public flags query and legacy-versus-Kitty
Escape encoding.

There is no behavioral mismatch in this batch, so no consensus decision or
policy XFAIL is needed. The modifier arithmetic and encodings also agree with
the Kitty keyboard protocol specification, but that agreement is corroborating
evidence rather than a reason to replace the executable source scenarios.

The audit used freshly updated repositories:

| implementation | revision |
| --- | --- |
| xterm.js | `29a738423349` |
| Alacritty | `1b2b36a64e88` |
| Ghostty | `d929e6a34a09` |
| Kitty | `e95da80fdbbf` |
| xterm | `6380a3eaed85` |
| Contour | `c51e15ed254e` |
| iTerm2 | `3ec57866cd9b` |
| VTE | `3d55bbdddb87` |
| foot | `a635e0a196d9` |

No production change was made in this batch.

### KittyKeyboard C0, navigation, arrow and initial function-key cases

The next 20 cases from `src/common/input/KittyKeyboard.test.ts` are represented
one-for-one in `tests/test_xtermjs_kitty_keyboard.py`. They complete the source
C0-key group, cover Insert through End, PageUp/PageDown, the four arrows and
their first modifier combinations, and add F1 and F2. Seventeen pass on both
parser backends. Three exact source expectations remain executable expected
failures:

- xterm.js emits the legacy `SS3 P` and `SS3 Q` forms for unmodified F1 and F2
  even after enabling the Kitty disambiguation flag. Alacritty, Ghostty, Kitty,
  Contour and foot emit `CSI P` and `CSI Q`; iTerm2 also leaves the legacy SS3
  form but chooses the protocol's permitted `CSI 11~` and `CSI 12~`
  alternatives. xterm and VTE do not implement the Kitty keyboard protocol and
  abstain. The Kitty specification explicitly permits the `CSI P/Q` form and
  counts as one further vote, so Shitty keeps its existing encoding;
- xterm.js's private evaluator returns `CSI 5;2~` for Shift+PageUp. On their
  public input paths Alacritty, Ghostty, xterm, Contour, iTerm2, VTE and foot
  claim this chord for local scrollback and send no PTY bytes; only Kitty sends
  `CSI 5;2~` by default. No terminal standard specifies GUI shortcut ownership,
  so Shitty keeps the 7-to-1 implementation consensus.

The audit used freshly updated repositories:

| implementation | revision |
| --- | --- |
| xterm.js | `29a738423349` |
| Alacritty | `1b2b36a64e88` |
| Ghostty | `d929e6a34a09` |
| Kitty | `e95da80fdbbf` |
| xterm | `6380a3eaed85` |
| Contour | `c51e15ed254e` |
| iTerm2 | `3ec57866cd9b` |
| VTE | `3d55bbdddb87` |
| foot | `a635e0a196d9` |

No production change was made in this batch.

### KittyKeyboard remaining function keys and initial numpad cases

The next 20 cases from `src/common/input/KittyKeyboard.test.ts` are represented
one-for-one in `tests/test_xtermjs_kitty_keyboard.py`. They cover F3 through
F24, the first modified function keys, and numpad 0, 1, 9 and decimal. Eighteen
pass on both parser backends. Two exact xterm.js expectations remain executable
expected failures:

- with Kitty disambiguation enabled, xterm.js still emits legacy `SS3 R` for
  F3. Alacritty, Ghostty, Kitty, Contour, iTerm2 and foot all emit `CSI 13~`;
- in the same mode xterm.js emits legacy `SS3 S` for F4. Alacritty, Ghostty,
  Kitty, Contour and foot emit `CSI S`, while iTerm2 uses the equivalent
  `CSI 14~` form.

xterm and VTE do not implement the Kitty keyboard protocol and abstain on both
cases. The Kitty specification requires `CSI 13~` for F3 and permits either
`CSI S` or `CSI 14~` for F4, so it adds one vote against both legacy SS3 source
expectations. Shitty keeps its existing `CSI 13~` and `CSI S` encodings.

The F24 case exposed a test frontend defect rather than a terminal mismatch:
`TestInputImpl` translated GLFW function-key values only through F20 even
though GLFW's contiguous public range ends at F25. Extending that test bridge
to the actual GLFW boundary makes the existing Shitty F24 implementation
observable as `CSI 57387 u`; the production Wayland and Cocoa frontends already
map their extended function-key ranges.

The audit used freshly updated repositories:

| implementation | revision |
| --- | --- |
| xterm.js | `29a738423349` |
| Alacritty | `1b2b36a64e88` |
| Ghostty | `d929e6a34a09` |
| Kitty | `e95da80fdbbf` |
| xterm | `6380a3eaed85` |
| Contour | `c51e15ed254e` |
| iTerm2 | `3ec57866cd9b` |
| VTE | `3d55bbdddb87` |
| foot | `a635e0a196d9` |

No production terminal behavior was changed in this batch.

### KittyKeyboard numpad, modifier and initial event-type cases

The next 20 cases from `src/common/input/KittyKeyboard.test.ts` are represented
one-for-one in `tests/test_xtermjs_kitty_keyboard.py`. They cover the remaining
numpad arithmetic keys, keypad Enter and Equal, modified keypad 5, all sided
Shift/Control/Alt/Super keys, the three lock keys, and the first plain-text and
Escape press events. All 20 pass on both parser backends.

The Right Alt case initially exposed a test frontend defect. `TestInputImpl`
treated every GLFW Right Alt event as AltGraph, so `Alt+RightAlt` lost the Kitty
Alt modifier and produced `CSI 57449u` instead of `CSI 57449;3u`. GLFW reports
Right Alt as the regular Alt modifier; the bridge already has a separate
explicit AltGraph bit. The two inputs are now preserved independently and the
bridge unit test covers both paths.

Alacritty, Ghostty, Kitty, Contour, iTerm2 and foot all encode a regular pressed
Right Alt as functional key 57449 with the active Alt bit. xterm and VTE do not
implement Kitty keyboard output and abstain. The Kitty specification likewise
requires a modifier key's own bit to reflect the current event and assigns
ISO Level 3 Shift, the AltGraph key distinguished by foot, its own functional
code 57453. The resulting consensus supports `CSI 57449;3u` without conflating
Right Alt and AltGraph.

The audit used freshly updated repositories:

| implementation | revision |
| --- | --- |
| xterm.js | `29a738423349` |
| Alacritty | `1b2b36a64e88` |
| Ghostty | `d929e6a34a09` |
| Kitty | `e95da80fdbbf` |
| xterm | `6380a3eaed85` |
| Contour | `c51e15ed254e` |
| iTerm2 | `3ec57866cd9b` |
| VTE | `3d55bbdddb87` |
| foot | `a635e0a196d9` |

No production terminal behavior was changed in this batch.

### KittyKeyboard event-type cases

The next 20 cases from `src/common/input/KittyKeyboard.test.ts` are represented
one-for-one in `tests/test_xtermjs_kitty_keyboard.py`. They complete the
press/repeat/release group for printable, recovery, modified, functional and
modifier keys, then cover the two modified-key cases with
`REPORT_EVENT_TYPES` enabled on its own. All 20 pass on both parser backends.

One case exposed a product defect. With flags 3, an unmodified printable repeat
must remain plain UTF-8 text; Shitty instead let `REPORT_EVENT_TYPES` force it
to `CSI 97;1:2u`. The source expectation is supported by Alacritty, Ghostty,
Kitty, iTerm2 and foot. Contour deliberately makes the opposite choice and has
an explicit `CSI 97;1:2u` unit test. xterm and VTE do not implement Kitty
keyboard output and abstain.

The Kitty keyboard protocol specification is the standard vote: it states that
text-producing events stay plain UTF-8 and therefore cannot expose event types
unless `REPORT_ALL_KEYS` is requested. The resulting six-to-one consensus
supports xterm.js. `VtermInput` now forces a printable key into a CSI report for
an event-only release, which has no following text event, but leaves an
unmodified repeat on the normal text path. Modified repeats and report-all mode
continue to carry the `:2` event suffix.

The audit used freshly updated repositories:

| implementation | revision |
| --- | --- |
| xterm.js | `29a738423349` |
| Alacritty | `1b2b36a64e88` |
| Ghostty | `09557e91dc33` |
| Kitty | `e95da80fdbbf` |
| xterm | `6380a3eaed85` |
| Contour | `c51e15ed254e` |
| iTerm2 | `3ec57866cd9b` |
| VTE | `3d55bbdddb87` |
| foot | `a635e0a196d9` |

### KittyKeyboard report-all and ordinary lock-key cases

The next 20 cases from `src/common/input/KittyKeyboard.test.ts` are represented
one-for-one in `tests/test_xtermjs_kitty_keyboard.py`. They complete the
event-only modified-key cases, exercise modifier and lock keys without
`REPORT_ALL_KEYS`, and cover report-all encoding for printable ASCII,
Enter, Tab and Backspace across press, repeat and release. Nineteen pass on
both parser backends. One exact xterm.js expectation remains an executable
expected failure.

The batch exposed a product defect for CapsLock and NumLock. Shitty encoded
them whenever any Kitty enhancement was active, although lock-key events are
supposed to require `REPORT_ALL_KEYS`. Kitty suppresses all three lock keys;
Ghostty, Contour and foot suppress CapsLock and NumLock; iTerm2 suppresses its
CapsLock `flagsChanged` event and has no physical NumLock event; Alacritty
encodes both. The Kitty protocol's report-all rule and its reference
implementation count as the standard vote. Shitty now follows the supporting
consensus and treats CapsLock and NumLock as modifier keys for this gate.

ScrollLock does not have that consensus. Ghostty, Contour, foot and Alacritty
classify it as an ordinary functional key and encode it without report-all.
Kitty classifies it as a modifier and suppresses it; iTerm2 has no virtual
ScrollLock keycode. Although the protocol prose and reference implementation
support xterm.js, the implementation vote is four-to-one in the opposite
direction. Shitty therefore keeps its existing `CSI 57359 u` behavior and the
xterm.js suppression case remains a policy XFAIL.

The audit used freshly updated repositories:

| implementation | revision |
| --- | --- |
| xterm.js | `29a738423349` |
| Alacritty | `1b2b36a64e88` |
| Ghostty | `09557e91dc33` |
| Kitty | `e95da80fdbbf` |
| xterm | `6380a3eaed85` |
| Contour | `c51e15ed254e` |
| iTerm2 | `3ec57866cd9b` |
| VTE | `3d55bbdddb87` |
| foot | `a635e0a196d9` |

### KittyKeyboard text and alternate-key cases

The next 20 cases from `src/common/input/KittyKeyboard.test.ts` are represented
one-for-one in `tests/test_xtermjs_kitty_keyboard.py`. They finish report-all
release coverage, then cover associated text, alternate shifted keys, combined
alternate-and-text fields, release suppression, dead keys and unidentified
keys. All 20 pass on both parser backends, bringing the imported set to 134
passes and 6 documented policy XFAILs.

The shifted-alternate release case exposed a missing frontend identity. A key
release has no following text event, but its Kitty packet still has to retain
the shifted codepoint: `Shift+A` release under flags 30 is
`CSI 97:65;2:3u`. Kitty, Ghostty, Alacritty, Contour, iTerm2 and foot all retain
that alternate on release. xterm and VTE do not implement Kitty keyboard
output and abstain. The Kitty protocol specification also applies alternate
keys to every event that is already escape-encoded and does not exclude
release, so the implementation and standard votes are unanimous.

`plt::KeyInput` therefore carries the active layout's shifted codepoint in
addition to its existing unshifted active- and base-layout identities. Wayland
and Cocoa populate it directly from their native keyboard translation, the
test frontend exposes the same input shape, and `VtermInput` uses it for both
deferred press packets and immediate release packets. The field is appended to
the aggregate so all existing positional initializers keep their meaning.

The audit used freshly updated repositories:

| implementation | revision |
| --- | --- |
| xterm.js | `29a738423349` |
| Alacritty | `1b2b36a64e88` |
| Ghostty | `09557e91dc33` |
| Kitty | `e95da80fdbbf` |
| xterm | `6380a3eaed85` |
| Contour | `c51e15ed254e` |
| iTerm2 | `3ec57866cd9b` |
| VTE | `3d55bbdddb87` |
| foot | `a635e0a196d9` |

### KittyKeyboard functional, media and macOS Option cases

The next 20 cases from `src/common/input/KittyKeyboard.test.ts` are represented
one-for-one in `tests/test_xtermjs_kitty_keyboard.py`. The first ten cover
PrintScreen, Pause, ContextMenu and seven media/volume keys. The remaining ten
cover Option-as-Alt letters, digit and dead keys, Shift and Control
combinations, and the distinct Linux Alt path. All 20 pass on both parser
backends, bringing the imported set to 154 passes and 6 documented policy
XFAILs.

The browser source receives composed macOS values such as `ƒ`, `∞`, or `Dead`
and reconstructs the unmodified key when its `macOptionAsAlt` policy is active.
Shitty has the same policy at a different boundary: its Cocoa frontend always
exposes Option as terminal Alt. That frontend was incorrectly forwarding the
composed value as the Kitty key identity, so Option+F became codepoint 402
instead of `CSI 102;3u`, and an Option dead key had no identity at all. Cocoa
now translates a printable event without Option while retaining `InputAlt` in
the reported modifiers. A native unit test starts from the raw composed and
dead-key NSEvents; the portable cases then exercise the resulting public
terminal input path.

Kitty, Ghostty, Alacritty through winit, and iTerm2 all implement this same
policy split: when Option acts as Alt it is removed from keyboard-layout
translation but retained as the terminal modifier; when native Option text is
selected, the composed text is left intact. Contour and foot have no Cocoa
input implementation, while xterm and VTE do not generate Kitty keyboard
output, so those four abstain. The Kitty keyboard specification independently
requires the primary codepoint to be the unshifted key in the active layout
and identifies macOS Option as Alt, adding the standard vote to the unanimous
supporting implementations.

The final case deliberately keeps the non-macOS rule separate: Linux Alt is a
modifier chord over the already translated active-layout key. No generic
physical-key fallback was added to `Vterm`.

The audit used freshly updated repositories:

| implementation | revision |
| --- | --- |
| xterm.js | `29a738423349` |
| Alacritty | `1b2b36a64e88` |
| Ghostty | `09557e91dc33` |
| Kitty | `e95da80fdbbf` |
| xterm | `6380a3eaed85` |
| Contour | `c51e15ed254e` |
| iTerm2 | `3ec57866cd9b` |
| VTE | `3d55bbdddb87` |
| foot | `a635e0a196d9` |

### KittyKeyboard final Option and release cases

The final five cases from `src/common/input/KittyKeyboard.test.ts` are
represented one-for-one in `tests/test_xtermjs_kitty_keyboard.py`. They cover
Linux AZERTY, native Option composition with Option-as-Alt disabled, composed
text without Alt, Option punctuation, and an Option-key release event. Four
pass on both parser backends. Together with the earlier batches, all 165
current upstream cases are accounted for: 158 pass and 7 are executable,
documented policy XFAILs.

The punctuation case is the one new XFAIL. xterm.js only reconstructs the
unmodified key from DOM `code` values beginning with `Key` or `Digit`; for
Option+`;`, whose DOM code is `Semicolon`, it therefore reports the composed
ellipsis as `CSI 8230;3u`. That is a browser implementation fallback, not the
consensus terminal behavior. Ghostty removes Option from the modifier set used
for all macOS key translation while preserving Alt in the reported event.
Kitty applies its Cocoa Option-as-Alt filter to the complete printable-key
path. iTerm2 subtracts Option before deriving both its primary and shifted
Unicode key codes. Alacritty delegates the same whole-window policy to winit.
All four supporting implementations therefore produce the unmodified active
layout punctuation. xterm and VTE do not generate Kitty keyboard events, and
Contour and foot have no Cocoa Option translation path, so they abstain.

The Kitty keyboard specification independently requires the primary key code
to be the unshifted key in the active layout and defines macOS Option as Alt.
It therefore votes for semicolon (`CSI 59;3u`), making the supporting consensus
four implementations plus the standard against the xterm.js expectation. The
source expectation remains executable as an XFAIL instead of being silently
discarded. A Cocoa unit test starts with the raw `…`/`;` NSEvent and verifies
that Shitty's Option-as-Alt frontend yields layout/base `;` while retaining
`InputAlt`.

The audit used the repositories refreshed for this batch:

| implementation | revision |
| --- | --- |
| xterm.js | `29a738423349` |
| Alacritty | `1b2b36a64e88` |
| Ghostty | `09557e91dc33` |
| Kitty | `e95da80fdbbf` |
| xterm | `6380a3eaed85` |
| Contour | `c51e15ed254e` |
| iTerm2 | `3ec57866cd9b` |
| VTE | `3d55bbdddb87` |
| foot | `a635e0a196d9` |

For DECSC/DECRC, xterm, Contour, Ghostty, VTE, foot, and Alacritty do not save
DECAWM; Kitty and iTerm2 agree with xterm.js. This matches DEC STD 070's cursor
state description, while later DEC manuals have contradictory wording about a
"wrap flag". The xterm implementation explicitly distinguishes the saved
last-column flag from DECAWM. Shitty therefore keeps its existing behavior.

Mode 45, mode 2031, reflow metadata, and `scrollOnEraseInDisplay` are outside
ECMA-48's portable contract. Their failures record exact xterm.js policies,
not consensus requirements. A standalone U+200B is now ignored without
advancing: Alacritty and VTE attach width-zero characters only to an existing
cell, Ghostty and foot discard a grapheme-breaking zero-width character,
Kitty has no previous cell to attach it to, and xterm classifies U+200B as an
ignored formatter. Contour's width-one storage and iTerm2's compatibility
policy are the two contrary implementations, producing a 7:2 vote. UAX #29
confirms that U+200B is a standalone Control grapheme break; Terminal Unicode
Core requires that segmentation in mode 2027 but does not define the cell
advance of a zero-width-only cluster, so the applicable standard abstains on
the disputed width policy.

### InputHandler cases 21 through 40

The next 20 source cases are represented one-for-one in
`tests/test_xtermjs_input_handler_text.py`. Thirteen pass on both parser
backends. The seven executable expected failures are:

- xterm.js repeats the complete preceding grapheme for REP; Shitty, xterm,
  Ghostty, and Contour retain only the preceding codepoint (foot can retain a
  composed-cell handle);
- xterm.js clears its REP join state on an intervening SGR, while Shitty,
  xterm, Contour, and foot retain the preceding graphic character through
  control sequences. ECMA-48 leaves REP undefined only when no graphic
  character precedes it and does not require xterm.js's reset policy;
- when a double-width character cannot fit in the final column, xterm.js
  clears stale content in that column before wrapping. Shitty leaves the old
  cell visible. Unicode width and early-wrap cleanup are outside ECMA-48;
- xterm.js discards U+00AD. Shitty renders it as a width-one compatibility
  character; Kitty preserves it and other implementations apply different
  default-ignorable/width policies;
- xterm.js initializes the 1049 alternate page with the current erase
  background. Shitty clears the page with canonical blank cells instead;
- Kitty's non-standard SGR 221 and 222 independently reset bold and faint.
  xterm.js implements both; Shitty and the other audited terminals ignore
  them.

The charset, split-grapheme, ordinary REP, modes 47/1047/1048/1049, saved
alternate cursor, and standard SGR cases all pass. The audit used the same
eight pinned implementation revisions listed above. No production change was
made.

### InputHandler cases 41 through 60

The next 20 source cases are represented one-for-one in
`tests/test_xtermjs_input_handler_sgr.py`. Eighteen pass on both parser
backends, including all ordinary attributes, all 256 palette indices, RGB and
palette source transitions, missing colon components, and three mixed
semicolon/colon forms. Default-color restoration is checked through public
DECRQSS rather than the resolved test-only RGB value.

Two permissive xterm.js parsing policies remain expected failures:

- `CSI 38;2;5 m` zero-fills the missing green and blue components in
  xterm.js; Shitty rejects the incomplete semicolon RGB clause and preserves
  the previous foreground;
- `CSI 38;2::50:100:150 m` is normalized by xterm.js to RGB 50/100/150;
  Shitty treats the empty colon subparameter as the first component and
  obtains 0/50/100.

The eight-repository audit found agreement on the canonical
`38;2;r;g;b` and `38:2::r:g:b` forms, but no portable contract for either
permissive form above. ISO 8613-6-style colon notation assigns component
positions explicitly; it does not define arbitrary mixing after a semicolon
RGB selector. These failures therefore preserve xterm.js compatibility
oracles without changing Shitty's parser policy. No production change was
made.

### InputHandler cases 61 through 80

The next 20 source cases are represented one-for-one in
`tests/test_xtermjs_input_handler_cursor.py`. Fifteen pass on both parser
backends: the three complete indexed-color forms, two complete RGB forms with
surrounding attributes, and all ten CUF/CUB/CUD/CUU/CNL/CPL/CHA/CUP/DECOM/HPA
positioning scenarios. Private xterm.js cursor assignments were replaced with
public CUP input before exercising the source command.

Five executable expected failures retain xterm.js's permissive completion of
truncated color clauses:

- `CSI 38:2 m` and `CSI 38:5 m` replace the current color with RGB black and
  palette index zero in xterm.js; Shitty rejects each incomplete clause and
  preserves the previous color;
- in `CSI 1;38:2::50:100;4 m`, xterm.js supplies a zero blue component and
  then applies underline. Shitty interprets the shorter colon group as RGB
  0/50/100;
- `CSI 1;38:2::;4 m` supplies all three missing components as zero in
  xterm.js, while Shitty leaves the default foreground unchanged;
- `CSI 1;38;2::;4 m` additionally mixes a semicolon color selector with an
  incomplete colon group. xterm.js produces black and still applies
  underline; Shitty does not recognize the same parameter boundary.

The same eight pinned implementations were audited. Alacritty and VTE require
all three RGB components; Ghostty accepts only its complete three-component
or color-space forms; Contour accepts only its defined four/five-subparameter
shapes; iTerm2 requires at least an index or a complete RGB tuple; foot checks
the complete arity before applying a color; Kitty's color state machine
passes only complete color groups to its SGR implementation; and xterm
rejects absent components after `get_subparam()` returns its `DEFAULT` value.
They do not establish xterm.js's zero-fill policy as a consensus requirement.
No production change was made.

### InputHandler cases 81 through 100

The next 20 source cases are represented one-for-one in
`tests/test_xtermjs_input_handler_cursor_bounds.py`. All pass on both parser
backends. HPR, VPA, and VPR retain their complete default, explicit-count,
clamping, and orthogonal-coordinate checks.

The remaining source cases inject `x = y = +/-10000` directly into xterm.js's
private buffer before dispatching CUF/CUB/CUD/CUU/CNL/CPL/CHA/CUP/HPA/HPR/VPA,
VPR, DCH, ECH, or ICH. No terminal byte stream can create that corrupt state.
Each adaptation instead drives the cursor to both publicly reachable page
boundaries and checks the same command's clamping consequence. The DCH and ECH
tests after a full-width write preserve the source's exact final-cell oracle;
the ICH boundary scenario remains separate because its source case has a
separate identity.

The same eight pinned implementations agree on the reachable cursor and page
boundary behavior exercised here. ECMA-48 defines the movement/editing
functions but has no contract for mutation of an emulator's private cursor
fields outside the presentation component. No product or test-only API was
added to manufacture that state.

### InputHandler cases 101 through 120

The next 20 source cases are represented one-for-one in
`tests/test_xtermjs_input_handler_margins.py`. Eighteen pass on both parser
backends. The scenarios cover the pending-wrap ICH edge, DECSTBM defaults and
validation, SU/SD/IL/DL clipping to vertical margins, processing all bytes in
the source's 5/10000/200000/300000-byte input calls, default window-operation
denial, character-grid reporting, all three title-stack selectors, and
DECCOLM permission and resize behavior.

Two executable expected failures preserve the exact xterm.js fixture state.
Its per-operation pixel and cell queries are enabled while no renderer exists,
so CSI 14 t and CSI 16 t remain silent. Shitty's headless platform is itself a
complete window backend with deterministic pixel and cell metrics; once its
single `allowWindowOps` policy is enabled, both queries correctly report those
metrics. Shitty cannot represent an enabled query whose required platform
service is absent.

This batch exposed two consensus defects that were fixed without adding API:

- DECSTBM now clamps an oversized bottom parameter to the page bottom before
  validating the region. Alacritty, Ghostty, Kitty, xterm, Contour, iTerm2,
  VTE, and foot all apply that behavior;
- a forced DECCOLM transition now requests 80 or 132 columns whenever the
  actual window width differs, even when the stored DECCOLM mode bit already
  names the requested width. Its clear, home, and permission semantics remain
  unchanged.

### InputHandler cases 121 through 140

The next 20 source cases are represented one-for-one in
`tests/test_xtermjs_input_handler_wide.py`. All pass on both parser backends.
They cover XTVERSION selectors, wide-cell repair by print/EL/ICH/DCH/ECH,
ordinary and reverse-wrap canonical erase, a wide glyph crossing an early
wrap boundary, SGR 0 with and without an active OSC 8 hyperlink, and the
single/double underline reset forms.

This batch exposed two product defects:

- XTVERSION answered `CSI > 1 q`. Xterm's control-sequence document defines
  only an omitted or zero selector; xterm, Kitty, VTE, and foot reject a
  nonzero selector. Ghostty and Contour currently answer it, while Alacritty
  and iTerm2 do not implement the query. Shitty now follows the 5-to-2
  supporting consensus and tests both the accepted zero and rejected nonzero
  forms at the parser boundary.
- reverse-wrap looked only at the physical right edge for a soft-wrap marker.
  A wide glyph which moves early leaves Shitty's marker on the final occupied
  cell instead. Xterm, Ghostty, Contour, iTerm2, and foot implement mode 45 and
  return to the logical wrap boundary; their split-wide/spacer representation
  differs, but overwriting that boundary removes the whole wide glyph.
  Alacritty, Kitty, and VTE do not execute mode 45 and do not vote. Shitty now
  finds its existing per-cell wrap marker and lands there; mode 1045 retains
  its physical-edge fallback for a line with no marker.

The source audit used freshly updated repositories:

| implementation | revision |
| --- | --- |
| xterm.js | `29a738423349` |
| Alacritty | `1b2b36a64e88` |
| Ghostty | `951a03b58bf6` |
| Kitty | `e95da80fdbbf` |
| xterm | `6380a3eaed85` |
| Contour | `c51e15ed254e` |
| iTerm2 | `3ec57866cd9b` |
| VTE | `3d55bbdddb87` |
| foot | `a635e0a196d9` |

### InputHandler cases 141 through 160

The next 20 source cases are represented one-for-one in
`tests/test_xtermjs_input_handler_styles_osc.py`. Seventeen pass on both
parser backends. They cover all extended underline styles, indexed and RGB
underline-color state, DECSTR resets, OSC 4 palette query/set, OSC 8 link
parameters and URI delimiters, and targeted or complete OSC 104 restoration.

Three exact xterm.js policies remain executable expected failures:

- `CSI 58;2::1:2:3 m` mixes a semicolon color selector with colon
  subparameters. It is the underline-color counterpart of the already
  recorded `CSI 38;2::...` policy; the audited implementations agree only on
  the canonical semicolon and colon forms.
- xterm.js makes `SGR 4:0` reset the current underline color as a side effect
  of disabling the underline. Alacritty, Ghostty, Kitty, Contour, iTerm2,
  VTE, and foot keep underline style and underline color as independent
  state; resets use SGR 4/24 and SGR 59 respectively. Xterm does not implement
  programmable SGR 58 underline color and does not vote.
- xterm.js rejects `rgb:1/22/333` in OSC 4. Xlib's XParseColor grammar
  explicitly permits one through four hex digits independently for each
  channel. Xterm delegates OSC color parsing to XParseColor, and Alacritty,
  Ghostty, Kitty, Contour, iTerm2, and foot also accept this exact mixed-width
  value. VTE rejects it. Shitty therefore keeps the standard and 7-to-1
  supporting consensus behavior.

The audit used the freshly updated revisions in the table above; Alacritty's
color grammar was also checked in its pinned `vte 0.15.0` parser dependency
at `3b3da71c34cc`. No production change was made.

### InputHandler cases 161 through 180

The next 20 source cases are represented one-for-one in
`tests/test_xtermjs_input_handler_colors_erase.py`. Fourteen pass on both
parser backends. They cover OSC 10/11/12 set, query and successive-value
processing, OSC 110/111/112 restoration, cursor bounds after all 29 source
editing and movement sequences, both ED 3 viewport regressions, DECSCA with
DECSEL/DECSED, and DECRQSS reporting of DECSCA.

All eight implementations support setting, querying and restoring the three
dynamic colors. Xterm, Alacritty, Ghostty, Kitty, and Contour also implement
the xterm successive-parameter rule; foot, VTE, and iTerm2 consume only one
dynamic-color specification. Xterm's control-sequence specification states
that each successive parameter addresses the next dynamic color. Shitty
keeps that 6-to-3 contract while stopping after the three dynamic colors it
models, as Alacritty, Ghostty, and Kitty do.

Six exact xterm.js delayed-wrap expectations remain executable expected
failures:

- after a full-width write, EL 0 and ED 0 leave the last cell untouched in
  xterm.js. ECMA-48 and the DEC manuals define erase relative to the active
  presentation position, inclusively. Xterm, Ghostty, Contour, VTE, and foot
  erase that last cell for EL 0; seven implementations do so for ED 0. Shitty
  therefore does not adopt xterm.js's out-of-range cursor representation;
- EL 1/2 and ED 1/2 erase the same cells in Shitty and xterm.js, but xterm.js
  retains its delayed-wrap latch. Alacritty, Kitty, Contour, and iTerm2 retain
  it, while xterm, Ghostty, VTE, and foot cancel it. Neither ECMA-48 nor the
  DEC erase definitions specify this emulator-internal latch, so the 4-to-4
  split provides no consensus for changing Shitty's existing policy.

ED 3 is different: every audited implementation preserves delayed-wrap while
erasing scrollback. Seven of eight also leave the visible page untouched;
Kitty clears it together with history. Shitty incorrectly normalized the
cursor before dropping history, so `eraseScrollback()` now preserves its
existing `lastCol` state. Erasing normal-screen history still releases a
scrolled viewport. When ED 3 is received on the alternate screen, Alacritty,
Ghostty, Kitty, Contour, and foot leave primary history and its viewport
alone; xterm, iTerm2, and VTE address primary history. Shitty follows the
5-to-3 behavior and its separate alternate-screen storage remains a no-op.

DECSCA and the DEC selective erases are implemented by Ghostty, xterm,
Contour, and iTerm2; Alacritty, Kitty, VTE, and foot do not implement the
protected-cell semantics and abstain. The four implementations and the DEC
definition unanimously agree that parameters 0/2 select erasable cells and
parameter 1 protects subsequently written cells. Xterm, Contour, and iTerm2
also report this state through DECRQSS exactly as the DEC request-status
definition requires. Shitty already matched both contracts.

The source audit used the freshly updated revisions in the table above. The
only production change in this batch is the ED 3 delayed-wrap fix.

### InputHandler cases 181 through 194

The final 14 source cases are represented one-for-one in
`tests/test_xtermjs_input_handler_modes_async.py`. Ten pass on both parser
backends. They cover mutable and unknown DECRQM modes, the Kitty keyboard
stack limit and screen-local state, and ordering of cursor reports, OSC and
DCS operations. The callback-only ordering cases are translated to their
public wire effects instead of introducing xterm.js parser internals into the
product API.

Four exact xterm.js policies remain executable expected failures:

- xterm.js reports ANSI KAM 2 as permanently reset and SRM 12 as permanently
  set. ECMA-48 defines both as mutable. Ghostty, xterm and Contour implement
  mutable KAM, while xterm and Contour implement mutable SRM; implementations
  which only recognize or store an unused bit abstain. Shitty retains its
  mutable state.
- xterm.js deliberately reports DEC private mode 12 as reset after setting it
  unless a frontend cursor-blink option is enabled. Alacritty, Ghostty, xterm,
  Contour, iTerm2 and foot expose it as mutable, so Shitty keeps the live mode
  value.
- xterm.js groups modes 3, 8, 67, 1005 and 1015 under backend-specific
  permanent or unknown answers. The DEC definitions and the supporting
  implementations instead treat each of these modes as mutable; exact support
  differs, and non-supporting implementations abstain.
- the same xterm.js group reports mode 1048 as initially set. Reportable
  implementations split 3-to-2 in favor of initially reset, and there is no
  terminal standard for this xterm extension. Shitty previously implemented
  save/restore but incorrectly answered DECRQM as unknown. It now reports the
  existing saved-cursor state: reset initially and set after `CSI ? 1048 h`.
  A separate strict regression locks that product fix while the exact
  xterm.js aggregate remains an expected failure.

The audit used the freshly updated revisions in the table above. The only
production change in this batch is the live DECRQM 1048 response.

### Buffer cases 1 through 20

The first 20 cases from `src/common/buffer/Buffer.test.ts` are represented
one-for-one in `tests/test_xtermjs_buffer.py`. All 20 pass on both parser
backends. The first three cover bounded line storage, the initial full-page
scrolling region, and the erased viewport. The next ten exercise
`getWrappedRangeForLine` through the public line-selection behavior at first,
middle, and last-row boundaries. The final seven cover blank-row width
changes, height shrink with and without history, cursor anchoring, and height
growth pulling backing rows into the viewport.

No private `CircularList`, `BufferLine`, or wrapped-range hook was added.
Capacity is observed through retained scrollback and its oldest reachable
row; wrapped ranges are observed by selecting a complete logical line; resize
state is observed through visible cells, cursor position, history size, total
rows, `ybase`, and `ydisp`. Existing broader resize and selection tests do not
replace any of the 20 source scenarios.

All eight implementation sources were checked even though this batch has no
mismatch. Alacritty's `Grid`, Ghostty's `PageList`, Kitty's
`Screen`/`HistoryBuf`, xterm's `Screen` and wrapped-line selection helpers,
Contour's `Grid`, iTerm2's `LineBuffer`/`VT100Screen`, VTE's `Ring`, and foot's
`grid` all retain bounded history, distinguish hard from soft logical-line
boundaries, and implement the corresponding height-resize anchoring. Their
column-resize reflow policies differ, but these first 20 cases resize only
blank rows horizontally and therefore do not collapse that difference.
ECMA-48 section 6.1.1 independently specifies that every character position
starts in the erased state, matching the initial-viewport case. It does not
specify emulator scrollback, GUI line selection, or resize transformation and
therefore abstains on those implementation-local contracts.

The audit used freshly updated repositories:

| implementation | revision |
| --- | --- |
| xterm.js | `29a738423349` |
| Alacritty | `1b2b36a64e88` |
| Ghostty | `09557e91dc33` |
| Kitty | `e95da80fdbbf` |
| xterm | `6380a3eaed85` |
| Contour | `c51e15ed254e` |
| iTerm2 | `3ec57866cd9b` |
| VTE | `3d55bbdddb87` |
| foot | `a635e0a196d9` |

No production change was needed in this batch.

### Buffer cases 21 through 40

The next 20 cases from `src/common/buffer/Buffer.test.ts` are represented
one-for-one in `tests/test_xtermjs_buffer.py`. Seventeen pass on both parser
backends and three remain executable policy expected failures. The passing
cases cover a viewport parked at the oldest history row, simultaneous row and
column changes, empty-row storage, hard-row wrap/unwrap, bounded reflow
truncation, successive logical rows, combining graphemes, marker relocation
and disposal, zero-space tails, wide cells, and cursor movement when width
growth compacts several soft rows.

The source marker objects are private xterm.js storage. They are represented
by three real OSC 133 prompt anchors: a width shrink moves their logical heads
from rows 0/1/2 to 0/5/10, width growth restores them, and bounded history
removes the first head while preserving the surviving continuation. This
exercises independently distinguishable public metadata rather than adding a
generic marker hook. The two “via tab char” cases do not send HT upstream;
they manually construct `ab` followed by two empty cells and a soft-linked
`cd` row. The adaptation constructs the same observable six-cell logical line
on the wire and verifies its exact physical rows through both growth and
shrink.

Two expected failures preserve xterm.js's old-ConPTY compatibility switches.
Among the audited implementations only Alacritty and Contour provide a native
ConPTY backend. Both keep PTY resize separate from their terminal grid resize:
Alacritty always calls the same `Term::resize`/`Grid::resize` after its
`OnResize`, and Contour applies `Grid::resize` before its `ConPty::resizeScreen`
call. Neither passes a Windows build number into the grid or disables reflow
at build 21375. Ghostty, Kitty, xterm, iTerm2, VTE and foot have no native
ConPTY backend and abstain. Thus no main implementation supports xterm.js's
exact “append rows without moving ybase” or build-21376 gate, and Shitty does
not couple its terminal component to PTY backend identity to reproduce them.

The third expected failure is the source default
`reflowCursorLine: false`. Alacritty explicitly relocates the live grid cursor
while joining and splitting rows; Ghostty passes a tracked cursor pin through
`Screen` and `PageList` resize; Kitty includes the active cursor in the
`TrackCursor` array consumed by `resize_screen_buffers`; Contour passes the
cursor into `Grid::resize`; iTerm2 converts screen positions through its
`LineBuffer`; VTE supplies the live cursor as a `Ring::rewrap` marker; and foot
adds the live and saved cursors to its reflow tracking points. Xterm reallocates
physical rows and does not reflow logical lines. The supporting vote is
therefore 7-to-1 for reflowing the active cursor block, matching Shitty and the
earlier BufferReflow audit.

ECMA-48, fifth edition, defines coded control functions and its device model;
it defines no host-window resize, scrollback, logical-line reflow, PTY backend
policy, or operating-system build gate, so it abstains on all three
differences. The audit used freshly updated repositories:

| implementation | revision |
| --- | --- |
| xterm.js | `29a738423349` |
| Alacritty | `1b2b36a64e88` |
| Ghostty | `44f06d4e4fd0` |
| Kitty | `e95da80fdbbf` |
| xterm | `6380a3eaed85` |
| Contour | `c51e15ed254e` |
| iTerm2 | `3ec57866cd9b` |
| VTE | `3d55bbdddb87` |
| foot | `a635e0a196d9` |

No production change was needed in this batch.

### Buffer cases 41 through 63

The remaining 23 cases from `src/common/buffer/Buffer.test.ts` are represented
one-for-one in `tests/test_xtermjs_buffer.py`. All 23 pass on both parser
backends. Together with the preceding batches, all 63 cases in the current
upstream file are now accounted for; 60 pass and the three previously audited
ConPTY/cursor-line policy differences remain executable expected failures.

The first eleven cases complete the `reflowLarger` and `reflowSmaller`
matrix. The private source fixture's injected rows, `ybase`, `ydisp`, and
cursor fields are constructed with public terminal operations: hard and soft
rows, real bounded scrollback, a viewport parked with wheel input, and a real
window resize. The full-history parked case deliberately verifies the visible
anchor rather than copying xterm.js's impossible hand-written `y = 13` state
for a ten-row viewport. Reflow trims three physical rows and rebases Shitty's
numeric view offset from 5 to 2 while leaving the same logical top row visible;
that is the observable invariant the source's private `ydisp` assertion is
trying to protect.

The no-scrollback case uses the real alternate screen and checks the invariant
through both row growth and shrink. The three private marker cases use a real
OSC 133 prompt mark: it follows its logical row, disappears with a trimmed row,
and leaves no semantic state in a recycled row. The five line-to-string cases
use real range selections. The source manually assigns width one to the emoji
U+1F601; a wire client cannot override a terminal's width table, and Unicode
17.0 classifies U+1F601 as Wide. Its one-cell supplementary-codepoint case is
therefore represented by the Neutral U+1D11E, while U+1F601 exercises the
double-cell case. Both verify that extraction emits one complete Unicode
scalar and never emits a continuation cell.

The final three source cases inspect xterm.js's private cache timer and typed
array capacity. Those implementation objects have no wire-visible identity.
Their distinct public postconditions remain separate executable scenarios:
line extraction stays stable across loop turns, clear and resize invalidate no
text, and the same contents survive the deferred work following a greater
than twofold column shrink. No cache, timer, allocator, or memory-capacity hook
was added to the product or harness.

All eight implementations were checked. Alacritty's `Grid::resize`, Ghostty's
tracked `PageList` viewport pin, Kitty's rewrap cursors, Contour's `Grid`,
iTerm2's `LineBuffer` coordinate conversion, VTE's `Ring::rewrap` markers, and
foot's explicit viewport tracking all preserve cursor/view anchors through
reflow. Xterm reallocates physical rows without logical-line reflow and
therefore abstains on the reflow topology. Every implementation represents
wide trailing cells as non-text placeholders and backs up or extends selection
at a wide boundary. All provide a no-history alternate grid by default;
iTerm2 additionally offers an opt-in policy to append alternate-screen output
to primary history. OSC 133 row metadata is supported by Ghostty, Kitty,
Contour, iTerm2, VTE, and foot; Alacritty and xterm lack this metadata and
abstain on marker lifetime.

Unicode Standard Annex #11 revision 44 supplies the width vote for the two
supplementary characters, and UAX #29 revision 47 identifies grapheme clusters
as the default unit for text selection. ECMA-48 defines neither host-window
resize and reflow nor scrollback, GUI selection, alternate-screen history, or
cache lifetime, so it abstains on those terminal-emulator policies.

The audit used freshly updated repositories:

| implementation | revision |
| --- | --- |
| xterm.js | `29a738423349` |
| Alacritty | `1b2b36a64e88` |
| Ghostty | `44f06d4e4fd0` |
| Kitty | `e95da80fdbbf` |
| xterm | `6380a3eaed85` |
| Contour | `c51e15ed254e` |
| iTerm2 | `3ec57866cd9b` |
| VTE | `3d55bbdddb87` |
| foot | `a635e0a196d9` |

No production change was needed in this batch.

### BufferReflow cases 1 through 6

The first six source cases are represented one-for-one in
`tests/test_xtermjs_buffer_reflow.py`. Five pass on both parser backends and
exercise narrow/wide mixtures, existing soft wraps and unused cells at a line
end without exposing Shitty's internal wide-cell representation.

The sixth case is an executable expected failure. It selects xterm.js's
`reflowCursorLine: false` policy and expects growth not to reflow the wrapped
block containing the cursor. Alacritty, Ghostty, Kitty, Contour, iTerm2, VTE
and foot reflow that active block and track the cursor through it. Xterm does
not reflow on resize. No terminal standard specifies resize reflow, so the
7-to-1 implementation consensus keeps Shitty's existing behavior rather than
adding an xterm.js-only configuration switch.

The source audit used freshly updated revisions:

| implementation | revision |
| --- | --- |
| xterm.js | `29a738423349` |
| Alacritty | `1b2b36a64e88` |
| Ghostty | `951a03b58bf6` |
| Kitty | `e95da80fdbbf` |
| xterm | `6380a3eaed85` |
| Contour | `c51e15ed254e` |
| iTerm2 | `3ec57866cd9b` |
| VTE | `3d55bbdddb87` |
| foot | `a635e0a196d9` |

### All 51 BufferLine cases

All 51 cases from `src/common/buffer/BufferLine.test.ts` are accounted for in
`tests/test_xtermjs_buffer_line.py`. They are exercised through the
public terminal model rather than by exposing xterm.js's private line storage:
underline attributes, grapheme and wide-cell representation, ICH/DCH/ECH,
background-colored erasure, resize/reflow copies, combining-data replacement
and trimmed and ranged line extraction. The final 26 cases add exact
supplementary, combining and fullwidth extraction ranges, leading and appended
combining marks, all five wide-cell edit sequences, eight extended-attribute
mutations, and the observable stability/invalidation contract of both string
cache cases. All 52 executable tests pass on both parser backends.

Two source details have no public terminal operation of their own:
`underlineVariantOffset` has no runtime producer in current xterm.js, and a
zero-column `BufferLine` is not a valid terminal page. Their public portions
(the five runtime underline variants and the minimum terminal grid) are still
covered, and both details remain named in the executable inventory. No
test-only BufferLine API was added to Shitty.

ECMA-48 5th edition sections 8.3.26, 8.3.38 and 8.3.64 define DCH, ECH and ICH
as shifts and fills with erased character positions. Alacritty, Ghostty,
Kitty, xterm, Contour, iTerm2, VTE and foot all implement that base behavior.
At a split wide-cell boundary, Ghostty, Kitty, xterm, iTerm2 and VTE explicitly
erase the damaged pair; Alacritty, Contour and foot shift their raw cell arrays
without equivalent boundary repair. UAX #11 revision 44 says that East Asian
Width is not a ready-made terminal-width algorithm, but also says nonspacing
marks have no advance width. Shitty keeps the 5-to-3 implementation consensus:
wide text is never exposed as an orphan continuation, while a combining mark
joins its predecessor.

A leading combining mark is a genuine policy split. Ghostty, Kitty, VTE and
foot discard it when there is no predecessor. Xterm, Contour and iTerm2 retain
it as a one-cell cluster. Alacritty attaches it to the blank cell at the cursor
without advancing. UAX #29 revision 47 calls an isolated combining mark a
degenerate combining-character sequence and places a grapheme boundary after
start-of-text. That standard vote breaks the 4-to-4 retain/discard split, so
Shitty retains the mark; its existing one-cell representation is kept because
the standard does not prescribe terminal cursor advancement.

Colored/style underline extensions are implemented by all audited terminals
except xterm, which therefore does not vote on those cases. The seven
supporting implementations move the complete rendition or its parallel
attribute record during ICH/DCH and copies. The two xterm.js cache objects have
no public counterpart in any implementation; their observable contract is
tested by repeated extraction before and after every corresponding public line
mutation, without adding a cache hook.

The audit used freshly updated repositories:

| implementation | revision |
| --- | --- |
| xterm.js | `29a738423349` |
| Alacritty | `1b2b36a64e88` |
| Ghostty | `94d775fefc21` |
| Kitty | `cf136a233ccc` |
| xterm | `6380a3eaed85` |
| Contour | `c51e15ed254e` |
| iTerm2 | `3ec57866cd9b` |
| VTE | `3d55bbdddb87` |
| foot | `a635e0a196d9` |

No production change was made in this batch.

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

Fourteen scenarios pass on both parser backends. Six remain executable
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
  erase and has no such option;
- xterm.js ignores a standalone U+200B without advancing. Shitty follows the
  iTerm2-default policy and gives a leading U+200B its own cell.

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
not consensus requirements. U+200B is zero-width/default-ignorable in the
Unicode data used by xterm, Alacritty, Ghostty, Kitty, Contour, VTE, and foot;
iTerm2 intentionally defaults to a cursor-advancing compatibility policy, as
does Shitty today.

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

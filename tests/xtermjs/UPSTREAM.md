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

No production API or implementation was added for this batch.

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

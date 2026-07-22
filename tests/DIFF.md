## Verdict

The audit started with 116 expected failures:

- **3 are genuine candidates for fixing in Shitty**: Delete Character outside the vertical margins, Back Index at the full-screen boundary, and Forward Index at the full-screen boundary.
- **23 come from an outdated `ucs-detect` profile**. They describe capabilities that Shitty already implements, not defects.
- **90 are acceptable profile differences, errors in external golden results, behavior left undefined by a standard, or comparisons between private application programming interfaces**.

Therefore, an expected failure does not currently mean that Shitty has 116 known bugs. I found three actual protocol differences where Shitty disagrees with both xterm and the majority of the principal terminal emulators.

The three protocol differences have since been fixed, the 23 outdated
`ucs-detect` expectations have been regenerated, and resize reflow has been
implemented. The imported suites now contain 87 expected failures, all in the
acceptable third category above.

The comparison covered xterm, Ghostty, Kitty, VTE, WezTerm, Alacritty, Konsole, Contour, foot, xterm.js, and Windows Terminal. I also reran all seven Contour expected failures without masking their differences.

## Behavior fixed after the audit

### 1. Delete Character outside the top and bottom scrolling region

`DCHTests.test_DCH_WorksOutsideTopBottomMargin`

Shitty ignores Delete Character when the cursor is outside the vertical margins. This agrees with an older Digital Equipment Corporation description, but the practical `xterm-256color` profile behaves differently:

- xterm restricts Delete Character only by the horizontal margins; it does not check the cursor row: `xterm/util.c`, function `DeleteChar`.
- VTE deliberately constructs a one-row scrolling region independently of Set Top and Bottom Margins: `vte/src/vteseq.cc`, function `Terminal::DCH`.
- Contour explicitly reproduces the behavior introduced in xterm patch 316.
- Ghostty, Alacritty, and Windows Terminal also apply Delete Character to the current row independently of the vertical scrolling region.

Conclusion: **fix Shitty and remove the expected failure**. The current comment in `tests/esctest/xfail.txt` defends the Digital Equipment Corporation behavior, but that behavior conflicts with both the xterm profile selected by Shitty and the behavior of the principal terminal emulators.

### 2 and 3. Back Index and Forward Index at the full-screen boundary

- `DECBITests.test_DECBI_WholeScreenScrolls`
- `DECFITests.test_DECFI_WholeScreenScrolls`

Shitty follows the literal Digital Equipment Corporation rule that the operation is ignored at the page boundary. Modern implementations shift the contents when the cursor is at an active margin, including when that margin coincides with the screen boundary:

- xterm performs horizontal scrolling at the left or right margin, including a margin at the screen boundary: `xterm/util.c`, function `xtermColIndex`.
- Windows Terminal does the same: `windows-terminal/src/terminal/adapter/adaptDispatch.cpp`, functions `AdaptDispatch::BackIndex` and `AdaptDispatch::ForwardIndex`.
- VTE calls its cursor movement operations with horizontal scrolling enabled.
- Contour reproduces xterm behavior.
- Despite its contradictory documentation string, the esctest assertion also expects scrolling.

Conclusion: **fix both operations to follow xterm and remove both expected failures**.

## Alacritty: 16 expected failures

None of these sixteen cases identifies a Shitty bug.

- `decaln_reset`: Ghostty, VTE, WezTerm, and xterm reset both pairs of margins, Origin Mode, and the cursor. Ghostty does this explicitly in `ghostty/src/terminal/Terminal.zig`, function `decaln`. The older Alacritty implementation only fills the screen.
- `deccolm_reset`: 132 Column Mode must have no effect unless xterm private mode 40 has first allowed 80 and 132 column switching. xterm, VTE, and Windows Terminal behave this way. Windows Terminal explicitly fixes this behavior in `windows-terminal/src/host/ut_host/ScreenBufferTests.cpp`.
- `vttest_origin_mode_1`, `vttest_origin_mode_2`, and `vttest_scroll`: Alacritty does not change the physical width for 132 Column Mode. Ghostty, VTE, Konsole, Contour, and Windows Terminal change it. WezTerm and Alacritty belong to the group that does not request a host-window resize. Shitty provides the complete width-switching behavior.
- `erase_in_line`: xterm and Ghostty clear pending wrap when processing Erase in Line. The older Alacritty implementation preserves pending wrap and does not perform Erase Right in this state.
- `selective_erasure`: xterm, Ghostty, Contour, xterm.js, and Windows Terminal support Select Character Protection Attribute, Selective Erase in Display, and Selective Erase in Line. Alacritty, Kitty, VTE, and WezTerm either do not support them or support them only partially. Shitty implements the actual feature.
- `underline`: Select Graphic Rendition parameter 21 means double underline in xterm and most modern engines. The historical Alacritty interpretation of parameter 21 as bold off is a legacy choice.
- Eight Back Color Erase and cell-reset cases: the rendered screens are identical. Only the invisible foreground color of an empty cell differs.

The terminal ecosystem is divided over the internal attributes of Back Color Erase cells:

- xterm, Kitty, and, to a large extent, WezTerm copy the complete current color pair or current Select Graphic Rendition state. Kitty applies the complete cursor attributes in `kitty/kitty/line.c`, function `line_apply_cursor`.
- Alacritty, Ghostty, VTE, foot, and xterm.js primarily preserve only the background. foot explicitly creates a cell with only the current background in `foot/terminal.c`, function `erase_cell_range`. xterm.js behaves similarly in `xterm.js/src/common/InputHandler.ts`, function `_eraseAttrData`.

For an xterm profile, Shitty behavior is correct. These expected failures may remain, although a future adapter should preferably compare rendered behavior instead of invisible internal cell fields.

## Contour: 7 expected failures

The expected-failure comments are split by the actual cause of each checkpoint.

- `vttest.07.vt52-mode`: Delete must be ignored. There is no consensus for position `0x5f` in the Digital Equipment Corporation Special Graphics character set: xterm, VTE, and Shitty produce a blank; Kitty produces a no-break space; Ghostty, Contour, and xterm.js retain the underscore. The golden result is not portable.
- `vttest.09.known-bugs`: the principal differences are checkpoint placement and the user-configurable Auto Repeat Mode state. Neither is a portable protocol oracle.
- `vttest.11.6.6.vt102-with-bce`: two checkpoints were recorded in a different execution phase. Two more keep the cursor column after Insert Line or Delete Line even though vttest itself states that the cursor should move to the first column. Shitty and xterm follow that stated behavior.
- `vttest.11.6.7.ecma48-misc-with-bce` and `vttest.11.7.iso6429-other`: these execute Repeat after a previous Repeat control function. The fifth edition of the European Computer Manufacturers Association standard ECMA-48 leaves Repeat behavior undefined when the preceding item is a control function. Contour stops after the first repetition; Shitty, Kitty, Ghostty, and Konsole continue repeating the last graphic character. Windows Terminal and xterm.js clear Repeat state after a control function. Both policies are permitted.
- `vttest.11.6.7.1.protected-area-with-bce` and `vttest.11.7.1.protected-area`: the test enables Erasure Mode number 6. In the set state, ECMA-48 requires ordinary erase operations to erase guarded cells as well. Shitty does so, while the Contour golden result retains the box. The golden result is incorrect.

Conclusion: **do not change Shitty for these cases**. The comments should instead be split by individual checkpoint.

## esctest: the remaining 25 expected failures

### Reverse wrap: 5 expected failures

Modern xterm separates two behaviors:

- private mode 45 crosses only a line that was soft-wrapped;
- private mode 1045 crosses any line and permits cyclic movement from the top to the bottom.

Ghostty and Contour implement the same distinction in `ghostty/src/terminal/Terminal.zig`, function `cursorLeft`, and `contour/src/vtbackend/Screen.cpp`, in the backward cursor movement implementation.

xterm.js and VTE provide only the restricted private mode 45 behavior. WezTerm and foot still assign the older, less restricted behavior to private mode 45. The old esctest cases expect that behavior without enabling private mode 1045. **Shitty agrees with modern xterm, so these expected failures should remain.**

### Request Mode hardware modes: 19 expected failures

Backarrow Key Usage Mode, Keyboard Position Mode, Page Cursor Coupling Mode, Vertical Cursor Coupling Mode, Transmit Rate Linking Mode, and the fourteen VT500-series modes in this group concern physical keyboards, communication lines, page memory, or bidirectional terminal hardware.

The principal graphical terminal emulators either:

- report these modes as permanently reset or permanently set;
- report them as unknown;
- send no response;
- or recognize the numeric mode without implementing modifiable functionality.

VTE recognizes some of the mode numbers but represents them as immutable declarative modes. xterm reports them as permanently reset. Almost no graphical terminal emulator implements them as modifiable functional modes, which is what the imported esctest cases demand. **Keep these expected failures.**

### Reset to Initial State and the alternate screen: 1 expected failure

xterm, Ghostty, VTE, and Contour clear terminal pages and return to the primary screen during a hard reset. Old alternate-screen contents must not reappear after a later switch to alternate-screen mode 47. **The esctest golden result is incorrect, so keep the expected failure.**

## Kitty: 2 expected failures

Both cases test the internal Unicode representation of the visually empty Digital Equipment Corporation Special Graphics character at position `0x5f`:

- Kitty and the Linux virtual terminal use the no-break space character, Unicode code point U+00A0.
- xterm, VTE, and Shitty use the space character, Unicode code point U+0020, or an undefined blank.
- Ghostty, Contour, and xterm.js retain the underscore.

No choice changes normal visible application output. **Keep these as internal-model compatibility differences.**

## libvterm: 16 expected failures

Almost all of these failures result from comparing different application programming interfaces or different terminal profiles.

- Parser trace: libvterm canonicalizes a seven-bit escape representation of a C1 control into a C1 callback. Shitty preserves the actual parser grammar production. User-visible terminal behavior is identical.
- Unicode Transformation Format 8: every modern terminal rejects code points above U+10FFFF. Only the number of replacement characters emitted for a malformed sequence differs. The old libvterm golden result must not accept U+1FFFFF.
- Combining marks: libvterm truncates after five marks. Terminals use different defensive limits: xterm has a configurable limit, Contour stores 16 code points, Kitty stores 24 code points, and Ghostty uses dynamic storage. Unicode does not define a fixed limit.
- Insert Line cursor column and Request Status String for Select Character Protection Attribute: xterm and Shitty return the cursor to the left margin and canonicalize the erasable state as `0"q`. libvterm preserves the cursor column and the original `2"q` spelling.
- Character-set reset: Shitty follows the Digital Equipment Corporation and xterm arrangement with G0 and G1 as United States American Standard Code for Information Interchange, and G2 and G3 as supplemental sets. Unicode-only engines often reset every set to American Standard Code for Information Interchange.
- Pending wrap during resize: terminal policies differ. Shitty now remaps the
  logical cursor during reflow and recomputes whether it remains at the new
  right edge.
- Default cursor blinking: this is a user-interface setting, not a terminal protocol rule. xterm and Shitty default to no blinking; the libvterm callback reports blinking enabled.
- Save Cursor cursor shape: xterm, Ghostty, and Shitty do not save cursor shape or cursor blinking. Ghostty's saved cursor structure in `ghostty/src/terminal/Terminal.zig` contains no such fields.
- Input fixture: this tests `vterm_keyboard_unichar()`, not an output byte stream from a terminal frontend. Unicode key encoding, xterm Modify Other Keys, and C0 control policy are not required to produce identical bytes.
- Primary Device Attributes, xterm version reporting, and Request Status String: terminal identity and canonical reply serialization necessarily differ between terminal profiles.
- Repeat: xterm and Shitty do not repeat a combining mark and allow a long Repeat operation to continue through automatic wrapping. Kitty and Contour also process Repeat through their normal text-printing paths. VTE instead limits Repeat to the remaining columns in the current row.
- Select Graphic Rendition parameters 73 and 74: libvterm implements superscript and subscript. Almost all principal terminal emulators ignore these parameters.
- Reflow was the only significant product feature in this group. Shitty now
  reflows primary-screen logical lines and passes the complete imported
  libvterm reflow fixture.

## ucs-detect: 0 expected failures

All 23 entries should be treated as differences from a stale expected profile:

- color-scheme query;
- Request Status String for Select Character Protection Attribute, Set Cursor Style, Select Conformance Level, margins, Select Graphic Rendition, and true color;
- expanded Primary Device Attributes;
- Kitty keyboard protocol query;
- Request Mode for modern private modes;
- xterm termcap capability queries for `Co`, `colors`, `RGB`, and `TN`.

Support is not universal across terminal emulators, but Shitty replies truthfully and uses valid protocol forms. For example, the Kitty keyboard protocol is supported by Kitty, Ghostty, foot, WezTerm, and Contour, while xterm does not support it. Request Status String and xterm termcap querying are strongest in xterm, Kitty, Contour, and WezTerm.

`data/shitty.yaml` has been regenerated from Shitty, all 23 false expected
failures have been removed, and the profile is tested as the declared Shitty
contract.

## VTE: 14 expected failures

- `escape_nf` and `escape_fpes`: VTE interprets Escape followed by `Z` as the European Computer Manufacturers Association Single Character Introducer and waits for a following character. xterm, Ghostty, Kitty, Windows Terminal, and Shitty recognize the complete VT100 Identify Terminal command. VTE is the outlier for this terminal profile.
- Twelve mixed Operating System Command cases: VTE requires the introducer and terminator to use matching seven-bit or eight-bit forms. xterm, Shitty, and most modern parsers accept a seven-bit Operating System Command followed by an eight-bit String Terminator, and the reverse combination. The European Computer Manufacturers Association standard does not prescribe malformed-stream recovery here.
- Oversized Operating System Command: maximum lengths are implementation policy:

  - Ghostty allows 2,048 bytes for an ordinary Operating System Command and permits selected commands to allocate more storage.
  - VTE allows 4,096 code points.
  - Kitty allows an escape sequence of approximately 256 kibibytes.
  - Shitty allows one mebibyte.
  - xterm.js allows ten million bytes.
  - foot grows the buffer dynamically.

The Shitty limit is reasonable. **Keep all 14 expected failures.**

## wraptest: 1 expected failure

Horizontal Tab while automatic wrap is pending:

- the Digital Equipment Corporation specification requires clearing the Last Column Flag;
- xterm normally preserves the flag, while private mode 41 enables the Digital Equipment Corporation and curses correction;
- Ghostty, VTE, and xterm.js effectively do nothing at the pending-wrap position;
- Alacritty immediately wraps to the next row.

Shitty follows default xterm behavior. **Keep the expected failure.**

## xterm.js: 8 expected failures

- Vertical Position Backward: xterm.js ignores the standardized `Control Sequence Introducer Ps k` operation. Shitty, VTE, and Contour implement the movement. Shitty is correct.
- Repeat after Line Feed or Cursor Backward: the standard leaves the result undefined, and terminal implementations are divided. Keep the selected Shitty behavior.
- Save Cursor and Save Current Cursor Position with pending wrap: xterm, Ghostty, Alacritty, and Shitty preserve the wrap flag. The xterm.js golden result loses it.
- Set Top and Bottom Margins quirks: Ghostty and Shitty reject excess parameters. Kitty and xterm.js generally use the first two parameters. The Digital Equipment Corporation definition specifies exactly two parameters, so Shitty is correct.
- Cursor Backward Tabulation with pending wrap: the standard requires movement to the previous tab stop. xterm.js ignores the operation because its internal cursor is represented as being one column beyond the screen. Shitty is correct.
- Reverse wrap: this is the same outdated expectation that assigns private mode 1045 behavior to private mode 45.

**Keep all eight expected failures.**

## Remaining expected failures

Keep the remaining 87 expected failures. They document a terminal profile
choice, a disputed internal representation, behavior left undefined by a
standard, or an error in an imported oracle.

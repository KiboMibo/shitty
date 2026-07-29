# Konsole tokenizer streams

`upstream/Vt102EmulationTest.cpp` is copied verbatim from Konsole revision
`fd49b8fb1af78721157d34667a19915f2f51223a` (2026-07-21). The file is
GPL-2.0-or-later; its license is preserved as `LICENSE.upstream`.

The catalog statically extracts all 146 declarative ANSI and VT52 tokenizer
rows, including their expected `ProcessToken` constructors, without compiling
or executing Qt or Konsole. Every row is an independent build target. The
upstream packed-token ABI is translated to Shitty's control, ESC, CSI, or VT52
parser event and normalized payload; both whole and bytewise feeds must match
that oracle as well as the full observable terminal state. VT52 rows first
apply DECANM, clear the setup trace, and then test the original input.

Standalone NUL is the one intentional modernization: ECMA-48 permits it to be
ignored, so Shitty's zero-run fast path emits no parser event. Konsole emits an
internal control token and ignores it during dispatch. `testTokenFunctions`
only proves that Konsole's copied legacy `TY_*` macros equal its replacement
constexpr functions; Shitty has no equivalent packed-token ABI. The catalog
instead recognizes all 13 constructors used by the corpus and rejects any
unknown constructor.

`semantic_cases.py` additionally ports all six named `ScreenTest.cpp` tests and
all seven named `HistoryTest.cpp` tests. Konsole's file-backed unlimited and
runtime-polymorphic history types have no Shitty counterpart; those assertions
are represented by the public disabled/finite `saveLines` policies rather than
by test-only storage classes.

Two selection details intentionally follow current desktop behavior instead of
Konsole's internal API: selecting a single blank row yields empty text rather
than a bare newline, and rectangular rows are newline-delimited rather than
space-delimited.

`vt_cases.py` ports all 11 semantic methods from `Vt102EmulationTest.cpp`:
parser output, buffered/synchronized updates, and nine Kitty keyboard
transactions. Tertiary DA reports Shitty's identifier instead of Konsole's.
Keyboard events use Shitty's split platform key/text input and current Kitty
protocol encodings; in particular, report-all and functional keys retain the
explicit default modifier field emitted by the product input path.

`upstream/CharacterWidthTest.cpp` is copied from the same Konsole revision and
retains its GPL-2.0-or-later notice. All 25 data rows are extracted into
independent Python-driven targets which query Shitty's internal codepoint-width
primitive. Konsole's `-1` sentinel for DEL is represented as the observable
zero advance. Importing these rows also aligned Cf format controls and trailing
Hangul Jamo with the current terminal consensus.

`upstream/KeyboardTranslatorTest.cpp` is likewise an exact source copy. Its 24
modifier-wildcard rows are independent targets. The adapter obtains the actual
xterm modifier parameter from Shitty's generic frontend-input path and F12
encoding, then applies the upstream wildcard oracle. Konsole's `testHexKeys`
exercises its private user-keytab parser rather than terminal protocol: Shitty
has no keytab DSL. Its standard default assertion, Backspace producing DEL, is
already covered by `tests/test_keyboard.py`; the Delete-to-BS and Space-to-NUL
rows are deliberate settings of the imported test keytab (and the latter is
an upstream expected failure), not portable terminal defaults.

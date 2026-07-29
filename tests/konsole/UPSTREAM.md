# Konsole tokenizer streams

`upstream/Vt102EmulationTest.cpp` is copied verbatim from Konsole revision
`fd49b8fb1af78721157d34667a19915f2f51223a` (2026-07-21). The file is
GPL-2.0-or-later; its license is preserved as `LICENSE.upstream`.

The catalog statically extracts all 146 declarative ANSI and VT52 tokenizer
input rows without compiling or executing Qt or Konsole. Every row is an
independent build target and is compared whole versus bytewise across parser
events and the full observable terminal state. VT52 rows include the DECANM
reset needed to put Shitty in the upstream test's mode. Konsole's expected
internal token values, Vt102Emulation semantic methods, and width assertions
remain for later adapters.

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

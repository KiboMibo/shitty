# Konsole tokenizer streams

`upstream/Vt102EmulationTest.cpp` is copied verbatim from Konsole revision
`fd49b8fb1af78721157d34667a19915f2f51223a` (2026-07-21). The file is
GPL-2.0-or-later; its license is preserved as `LICENSE.upstream`.

The catalog statically extracts all 146 declarative ANSI and VT52 tokenizer
input rows without compiling or executing Qt or Konsole. Every row is an
independent build target and is compared whole versus bytewise across parser
events and the full observable terminal state. VT52 rows include the DECANM
reset needed to put Zutty in the upstream test's mode. Konsole's expected
internal token values and semantic screen/history/width assertions remain for
a later adapter.

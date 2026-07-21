# ucs-detect Unicode width corpus

This directory imports the complete generated Unicode width tables from
`ucs-detect` 2.3.4, revision
`ea4510a4bc6e99df2af500d454ac34f66c0245b3` (2026-07-14).

The upstream tables and documentation are kept unmodified.  `catalog.py` and
`adapter.py` are Zutty integration code.  Upstream is MIT licensed; its license
is preserved as `LICENSE.upstream`.

The adapter embeds the suite into Zutty's headless model tier.  Like upstream,
it writes each Unicode sequence through the real terminal parser and measures
the resulting cursor position using CPR.  `MEASURE_WIDTHS` batches many fresh
RIS/sequence/CPR probes into one control transaction; it does not call Zutty's
width implementation directly.  VS16 also retains upstream's separate check
that each base character is narrow before the selector is applied.

These targets validate parser/model cell width and grapheme effects.  They do
not claim that a configured font contains a glyph or that the rasterized shape
fits its cells; the upstream interactive renderer probe remains a later test
tier.

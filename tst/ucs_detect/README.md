# ucs-detect Unicode width corpus

This directory imports the complete generated Unicode width tables from
`ucs-detect` 2.3.4, revision
`ea4510a4bc6e99df2af500d454ac34f66c0245b3` (2026-07-14).

The upstream tables and documentation are kept unmodified.  `catalog.py` and
`adapter.py` are Shitty integration code.  Upstream is MIT licensed; its license
is preserved as `LICENSE.upstream`.

The adapter embeds the suite into Shitty's headless model tier.  Like upstream,
it writes each Unicode sequence through the real terminal parser and measures
the resulting cursor position using CPR.  `MEASURE_WIDTHS` batches many fresh
RIS/sequence/CPR probes into one control transaction; it does not call Shitty's
width implementation directly.  VS16 also retains upstream's separate check
that each base character is narrow before the selector is applied.

These targets validate parser/model cell width and grapheme effects.  They do
not claim that a configured font contains a glyph or that the rasterized shape
fits its cells; the upstream interactive renderer probe remains a later test
tier.

The live terminal detector is imported separately as 87 offline capability
targets.  `upstream/terminal.py` and `upstream/table_xtgettcap.py` are verbatim
upstream inputs.  `probe_adapter.py` projects their PTY probes onto the headless
control interface without importing Blessed and without network access.  Each
target compares one normalized result against the regenerated Shitty profile in
`probe_cases.py`; `upstream/data/shitty.yaml` contains the corresponding updated
capability section while retaining the imported Unicode measurements.  Exact
differences are tracked in `probe_xfail.txt`; screen-leak probes additionally
preserve the visible model.

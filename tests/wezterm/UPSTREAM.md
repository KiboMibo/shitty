# WezTerm model streams

The Rust sources in `upstream/` are copied verbatim from WezTerm revision
`76b606ec597a3c0263fa60321548637451c0a547` (2026-07-21). The graphics-only
`image.rs` is intentionally deferred with the rest of graphics testing. The
upstream project is MIT licensed; its terminal crate license is preserved as
`LICENSE.upstream`.

The stream catalog statically extracts every direct literal `term.print("...")` call
without compiling or executing WezTerm. Rust byte, Unicode, and continuation
escapes are decoded by the adapter. Each call site is an independent build
target and is compared whole versus bytewise across parser events and the full
observable terminal state. Variable-built streams and WezTerm's semantic
selection, resize, and dirty-line assertions remain for a later adapter.

The screen catalog statically interprets the unambiguous subset of those same
Rust tests: literal `print`, cursor placement, erase, mode, and line deletion
operations followed by literal `assert_visible_contents` checkpoints. It skips
unknown operations and dynamic expressions instead of guessing. Each of the 28
imported checkpoints runs at its upstream geometry and compares visible text;
trailing default cells are normalized because Shitty stores a fixed-width grid
where WezTerm stores variable-length lines.

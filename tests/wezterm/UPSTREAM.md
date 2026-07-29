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

The screen catalog covers every executable `assert_visible_contents`
checkpoint. The often-quoted count of 74 includes the helper's own definition;
there are 73 call sites. Twenty-eight are extracted mechanically. The other 45
are explicit Python transactions for dynamic strings, resets, left/right
margins, selection side effects, and resize/reflow. The validator inventories
the Rust source by test method and rejects any missing call site, so an
untranslated operation can no longer silently discard later assertions.

Every checkpoint runs at its upstream geometry and compares visible text;
trailing default cells are normalized because Shitty stores a fixed-width grid
where WezTerm stores variable-length lines. Importing the complete set added
FinalTerm OSC 133 `L` fresh-line behavior, independently corroborated by
Ghostty and the semantic-prompts specification. All 73 currently agree without
an expected failure.

Non-visible upstream oracles remain separate work: cursor and dirty-line
metadata, all-lines/history and stable-row assertions, semantic zones and cell
attributes, hyperlink identity, and clipboard selection results. They are
listed explicitly in `PLAN.md` rather than being treated as part of the now
complete visible-screen catalog.

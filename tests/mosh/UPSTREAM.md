# Mosh fuzz corpora

The files in `terminal_corpus/` and `terminal_parser_corpus/` are copied
verbatim from Mosh revision `decd9b705eb81626f694335b8d5940538beb06da`.
They are the seed corpora for Mosh's `Terminal::Complete` and `UTF8Parser`
libFuzzer targets. See `COPYING.upstream` (GPL-3.0).

The adapter feeds every member both as one write and byte-by-byte through two
freshly reset Zutty models. It compares PTY replies, frontend actions,
conformance modes, and the full rich model snapshot after each member.

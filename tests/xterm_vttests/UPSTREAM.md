# xterm vttests

Source: https://invisible-island.net/xterm/

Revision: `6380a3eaed857c182ea6cfa78cd706966b2628d0` (`xterm-410`).

License: X Consortium style; see `COPYING.upstream` and the headers retained
in every upstream script.

The complete upstream `vttests/` directory is preserved verbatim.  The first
adapter layer executes the finite shell generators.  Local command shims only
replace unavailable host tools (`tput`, Perl/Text::CharWidth and `sleep`); the
escape streams are still produced by the upstream scripts.  Interactive,
infinite and query/reply scripts remain preserved for the later PTY adapter.

# xterm vttests

Source: https://invisible-island.net/xterm/

Revision: `6380a3eaed857c182ea6cfa78cd706966b2628d0` (`xterm-410`).

License: X Consortium style; see `COPYING.upstream` and the headers retained
in every upstream script.

The complete upstream `vttests/` directory is preserved verbatim.  The adapter
executes all shell scenarios and every Perl scenario that uses only core
modules.  Finite generators are compared whole versus chunked, query/reply and
interactive scripts run on a real PTY, and infinite color generators
contribute deterministic 256-KiB stream prefixes.  Local command shims replace
only unavailable host tools (`tput`, `tabs`, Perl/Text::CharWidth and `sleep`);
escape streams still come from upstream.  The remaining Perl scenarios depend
on `Term::ReadKey` or `Text::CharWidth` and stay preserved for a later adapter.

The Perl targets require `bld/perl` in the IX run environment.

# xterm vttests

Source: https://invisible-island.net/xterm/

Revision: `6380a3eaed857c182ea6cfa78cd706966b2628d0` (`xterm-410`).

License: X Consortium style; see `COPYING.upstream` and the headers retained
in every upstream script.

The complete upstream `vttests/` directory is preserved verbatim.  The adapter
executes all 60 shell and Perl scenarios.  Finite generators are compared whole
versus chunked, query/reply and interactive scripts run on a real PTY, and
infinite color generators contribute deterministic 256-KiB stream prefixes.
Local command shims and small compatibility modules replace only unavailable
host tools (`tput`, `tabs`, `Term::ReadKey`, `Text::CharWidth`, and `sleep`);
escape streams still come from upstream.

The Perl targets require `bld/perl` in the IX run environment.

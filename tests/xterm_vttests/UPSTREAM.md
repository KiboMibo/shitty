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

`query-xres.pl` is retained and executed as an expected inapplicable case:
it queries xterm's X resource database through XTGETXRES. Shitty has no X
resource database on either Wayland or Cocoa, so there is no meaningful value
or portable terminal behavior to expose. `cursor.pl` is given its own source
as display input; upstream otherwise only clears an already-empty screen and
provides no observable oracle.

The alien shell harnesses for `resize.sh`, `title.sh`, and `version.sh` are
translated to finite Python scenarios. The original scripts remain verbatim.
The translations preserve their protocol substance: geometry queries and a
resize request, title query/update/restore, and the version query. They replace
only infinite visual loops, shell/TTY parsing, sleeps, and output intended for
a human.

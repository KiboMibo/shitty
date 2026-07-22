# tack

Source: https://github.com/ThomasDickey/tack-snapshots

Revision: `4c0a14b599b3b8161f952f3ff5a5a9eb189e3c27`.

License: GNU GPL version 2; see `upstream/COPYING` and the headers retained in
the upstream files.

The complete upstream source distribution is preserved in `upstream/`.
`build_tack.sh` builds it without source changes.  Its `--enable-leaks` option
disables optional permanent-memory cleanup; despite the historical name this
is the normal build mode needed by the current upstream snapshot.

The manifest contains every literal terminfo capability referenced by an
upstream `TestList`: 118 independent build targets.  `validate.py` checks the
manifest directly against those C initializers.  The adapter starts the
real tack executable under Shitty's real PTY, enters tack's capability-search
path, and lets the matching upstream test procedures consume several manual
acknowledgements.  Since tack is primarily a visual suite, this import tier
checks process safety, liveness, menu routing, and observable terminal-state
changes; it does not invent golden visual answers that upstream does not have.

These targets require `lib/ncurses` in the IX run environment.

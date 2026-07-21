# vtebench workloads

This directory imports all 12 upstream workloads from vtebench revision
`ead80032e57dee2e75f0b51f2ea67528647d9944`.  The upstream benchmark files,
including the two Vim recordings and the Unicode symbol corpus, are preserved
under `benchmarks/`.  vtebench is dual MIT/Apache-2.0 licensed; both licenses
are included.

The adapter reproduces one 80x24 iteration of each benchmark and its setup.
Dimension-dependent shell snippets are generated directly so the test does not
depend on `tput` or a host terminfo database.  Each workload is fed both as one
burst and across large non-aligned chunks.  PTY replies, protocol/action state,
renderer state, hyperlinks and the rich model digest must agree.  The one-burst
parse rate is printed as a baseline; a generous 30 second alarm detects hangs
without pretending that a shared CI host can enforce a precise benchmark.

# VTE parser matrices

`upstream/parser-test.cc` is copied verbatim from VTE revision
`3d55bbdddb87d3341c9e9e87fa6a085192612668` (2026-07-21). The source is
LGPLv3-or-later; its license and VTE's license note are preserved here.

The adapter recreates VTE's generated ESC, CSI, DCS, parameter, recovery, and
OSC matrices and feeds them to the real Zutty parser. Small axes are exhaustive;
the largest Cartesian products retain every value on each axis with pairwise
and boundary combinations so routine tests stay bounded. Streams are drained
in bounded batches and every generated sequence is checked. Each
upstream `g_test_add_func` family is exposed as a separate build target; the
OSC control introducer/terminator cross-product is split further so failures
remain local.

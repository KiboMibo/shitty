# VTE parser matrices

`upstream/parser-test.cc` is copied verbatim from VTE revision
`3d55bbdddb87d3341c9e9e87fa6a085192612668` (2026-07-21). The source is
LGPLv3-or-later; its license and VTE's license note are preserved here.

The adapter recreates VTE's generated ESC, CSI, DCS, parameter, recovery, and
OSC matrices and feeds them to the real Shitty parser. Small axes are exhaustive;
the largest Cartesian products retain every value on each axis with pairwise
and boundary combinations so routine tests stay bounded. Streams are drained
in bounded batches and every generated sequence is checked. Each
upstream `g_test_add_func` family is exposed as a separate build target; the
OSC control introducer/terminator cross-product is split further so failures
remain local.

`upstream/unicode-width-test.cc` is copied verbatim from the same revision.
The width adapter extracts its explicit Unicode ranges and points for ambiguous
width 1, splits large ranges at 256-codepoint boundaries, and verifies 930
codepoints through the terminal's standard CPR reply. VTE's GLib comparison
and ambiguous-width-2 assertions are deferred because Shitty does not expose a
CJK ambiguous-width setting.

`upstream/tabstops-test.cc` is copied verbatim from the same revision. All
seven registered families are translated by `tabstop_adapter.py`. The adapter
uses real HTS, TBC, CHT, CBT, RIS, and terminal resize operations, plus a
read-only test API that exposes the resulting stop table. VTE's configurable
tab widths are represented by explicitly setting the same stop positions.
VTE's internal `resize(fill=false)` policy and the synthetic `endpos` argument
have no terminal protocol equivalent: Shitty follows its public rule of
filling newly addressable columns, and CHT/CBT clamp a missing stop to the
applicable screen or margin edge.

The two tests from VTE `src/utf8-test.cc` are translated into
`utf8_adapter.py` and `utf8_cases.py`. The decode target checks every one of
the 1,112,064 Unicode scalar values. The replacement target retains all 108
encoding_rs vectors, including NUL, truncated sequences, overlong encodings,
surrogates, out-of-range values, and obsolete five/six-byte forms. The oracle
uses the modern maximal-subpart replacement behavior shared by VTE,
encoding_rs, WHATWG Encoding, and current Unicode recommendations.

`upstream/modes-test.cc` is copied verbatim from the same revision. Its two
registered tests are translated through the public protocol: SM/RM/RIS and
DECRQM cover IRM; DECAWM, meta-sends-escape and focus-event defaults are
queried with DECRQM; XTSAVE/XTRESTORE and RIS cover saved focus state.
VTE's independent BDSM bit and C++ bitset copy operations are implementation
details with no terminal-protocol observable and are therefore inapplicable.

`upstream/color-test.cc` and its generated `color-names-tests.hh` are copied
verbatim from the same revision. All five registered families are translated
through OSC 12 set/query operations. This includes all 782 X11 color-name
vectors. The oracle follows the terminal contract documented by xterm:
`name or RGB specification as per XParseColor`. Consequently, old-style
`#RGB` components are left-justified exactly like Xlib, while `rgb:` component
precision is scaled; this intentionally differs from VTE's value-object test,
which replicates low bits for both forms. `rgbi:` remains valid as specified
by Xcms and implemented by xterm, Ghostty and Shitty. CSS-only rgba/rgb/hsl,
alpha serialization and the opposite-parser-mode assertions are retained as
inputs but adapted to their OSC meaning: non-XParseColor forms are rejected
without changing the current color.

`upstream/pastify-test.cc` is copied verbatim from the same revision. Its 71
registered cases are fully classified. The 70 cases reachable through VTE's
real paste path are translated through Shitty's real Clipboard, generic input
and PTY output queue, both whole and one byte per clipboard callback. This
covers the C0/DEL and UTF-8 C1 matrices, all eight placements per control,
idempotence, CR/LF folding, and C0 bracket markers. The import fixed unsafe
control forwarding and chunk-boundary handling in Shitty's streaming paste
adapter.

There is no standard mandating how a terminal visualizes unsafe paste
controls. Current VTE uses Unicode control pictures, Ghostty replaces a
security-oriented set with spaces, foot strips controls, and
Alacritty/WezTerm/Kitty remove dangerous subsets. The interoperable consensus
is not to forward terminal-control injection unchanged; Shitty adopts VTE's
lossless visible representation. VTE's C1 bracket-marker test is the sole
inapplicable case: it tests a parameter of the internal helper that VTE's own
production `widget_paste()` always calls with C1 disabled.

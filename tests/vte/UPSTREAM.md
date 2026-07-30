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

The complete product-observable recovery families are also translated:

- all 52 controls listed by VTE, with raw C1 enabled explicitly;
- all 32 invalid `ESC 0/n` and `ESC 1/n` finals, both contiguous and split at
  the input boundary;
- all 1,184 combinations from `test_seq_csi_clear`, covering every prefix of
  the 74-byte maximum-argument CSI followed by zero through fifteen fresh
  arguments.

VTE exposes NUL as an internal no-op token when its tests are built with
`PARSER_INCLUDE_NOP`. Shitty instead verifies that NUL produces no parser
event. This is the terminal-observable ECMA-48 behaviour and agrees with
xterm.js and the existing Konsole adaptation.

VTE's two SCI families are intentionally not adopted as terminal behaviour.
Its generic ECMA parser interprets `ESC Z` as the 7-bit form of SCI, while DEC
terminals and current Foot, Ghostty, WezTerm, Konsole, and xterm-compatible
applications use `ESC Z` as DECID, the obsolete form of primary DA. Shitty
keeps DECID; its 7-bit and 8-bit behaviour is already covered by esctest.

`upstream/parser-charset-tables.hh` is also copied verbatim. All six charset
families from `parser-test.cc` are translated, preserving all 9,246
designators: 2,528 single-byte 94-set cases, 1,659 single-byte 96-set cases,
2,531 multibyte 94-set cases, 2,133 multibyte 96-set cases, 158 control-set
cases, and 237 other-coding-system cases. A read-only test API exposes the
four designated graphic slots, so the tests check semantic state rather than
only accepting the escape bytes.

The observable oracle follows the terminal implementation layer, not VTE's
larger internal ECMA-35 token enum. Supported DEC, ISO Latin-1, UK and NRC
sets are designated exactly. Unknown single-byte sets select the default
graphic mapping. Unsupported multibyte, C0/C1 designation and non-UTF
coding-system sequences are consumed without changing graphic slots; the
widely implemented `ESC %@` and `ESC %G` encoding selectors remain active.
The import fixed Shitty accidentally interpreting multibyte slot selectors
as G0 single-byte designations and accepting unrelated modified or 96-set
final bytes as DEC/NRC sets.

The three generated known-sequence tables, `parser-esc.hh`,
`parser-csi.hh`, and `parser-dcs.hh`, are copied byte-for-byte from the same
revision. Their 274 signatures comprise 48 ESC, 204 CSI, and 22 DCS
commands, including all 174 VTE NOP entries. Python transactions verify every
signature as one stream and one byte at a time. A native ParserIface matrix
then checks semantic dispatch for all 103 signatures implemented by Shitty
and verifies that the other 171 produce no product callback.

Representative parameters and DCS payloads are supplied only to the semantic
matrix, so parameter-dependent handlers are actually reached; the Python
matrix retains VTE's exact parameterless bytes. Modern conflicts follow
current terminal practice: `CSI ? u` is Kitty keyboard-state query rather
than old DECRQUPSS, and `CSI ? m` is the xterm modify-key query rather than
old DECSGR. Sixel/ReGIS and other graphics, printer, page-presentation, and
obsolete session commands remain consumed no-ops.

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

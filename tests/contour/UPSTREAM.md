# Contour vtconformance / vttest

The 143 `golden/*.dump` files and scenario design come from Contour revision
`ff1da74de2c2cded7216eda4835ec1fa8400d7b3`, under Apache-2.0; see
`CONTOUR-LICENSE.txt`.

The test helper sources under `vttest/` come from vttest revision
`0229d7171a8574a2bf406c6ce14549f65d810e51` (version 2.7, 2025-12-05); see
`VTTEST-COPYING`. `config.h` was produced by that revision's configure script
for the Linux/musl build environment used by Shitty.

`scenarios.json` is a direct transcription of Contour's scripted menu paths.
The Python adapter replaces Contour's terminal-engine harness with Shitty's
PTY/control harness while retaining the upstream dump format and one build
target per scenario.

`test_contour_checksum.py` rewrites all 12 test cases from
`src/vtbackend/RectangularAreaChecksum_test.cpp` at the same Contour revision.
It retains every xterm-406-derived checksum oracle: negation and overflow,
written versus undrawn blanks, the six DEC video-attribute weights, DEC
charset mapping, combining marks, and all five composable XTCHECKSUM flags.
The one upstream pure-algorithm case with an empty rectangle is retained as a
native Screen unit test because no valid DECRQCRA wire request denotes an
empty rectangle.

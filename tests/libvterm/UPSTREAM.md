# libvterm test DSL

Source: https://github.com/neovim/libvterm

Revision: `934bc2fbf21800ac3458a499df8820ca5fb45fd3`

License: MIT; see `LICENSE.upstream`.

The complete upstream `t/*.test` DSL corpus is preserved under `upstream/`.
The adapter runs the UTF-8 decoder, raw parser, fallback, screen, resize,
reflow, state, input, mouse, selection and historical vttest fixtures against
Shitty's control interface. Cell, pen, cursor, title, mode, PTY-reply and
normalized parser callback expectations are checked directly.

## Damage topology

All 48 renderer callbacks in `62screen_damage.test` are now executable
assertions: 39 `damage` rectangles and nine `moverect` operations.  Libvterm's
callback ABI is rectangle-granular and can optionally report a copy operation;
Shitty's renderer ABI is deliberately row-granular because shaping makes an
edit invalidate its whole row.  The adapter therefore translates every source
rectangle to its exact half-open row set.  A `moverect` contributes every
destination row, while the accompanying `damage` callback contributes the
vacated or newly cleared rows.  It does not discard a move merely because the
two renderers encode it differently.

Libvterm's CELL, ROW, SCREEN and SCROLL merge modes delay and fragment
callbacks differently.  The adapter accumulates the rows from one source
mutation group through `DAMAGEFLUSH` and compares them with the rows published
by Shitty for the same mutation group.  This preserves exact affected-row
topology without adding libvterm's private merge policy or a test-only
rectangle API to the product.  RIS damage is captured immediately after RIS;
the subsequent OSC palette and encoding writes are fixture alignment and are
not allowed to overwrite that renderer observation.  The seven still-reported
pending callbacks in this fixture are `sb_pushline`, libvterm's external
scrollback-owner ABI, and are unrelated to damage topology.

The consensus audit used each implementation's current renderer boundary.
Alacritty uses per-line bounds plus an explicit full-damage flag; Ghostty uses
cell/page dirtiness and `dirty.clear` for a full reset; Kitty uses dirty lines
and a screen-wide `is_dirty`; xterm copies retained pixels and clears exposed
areas directly; Contour stamps every mutated line revision; iTerm2 marks cells
or whole grids dirty; VTE combines update rectangles with `invalidate_all`;
and Foot records row damage with `term_damage_all` for RIS.  All eight include
the destination rows of insert/scroll operations and the vacated rows that
must be cleared.  All eight also force a full visible-screen invalidation for
RIS, even when the old page is already blank.  A distinct renderer-level move
opcode is only an optimization in implementations that expose one; the others
still vote for the same affected rows rather than abstaining from the
operation.

ECMA-48 defines the resulting cells for RIS, insertion, deletion and scrolling
inside margins, so it supplies the standards vote for which rows change.  It
does not define renderer damage callbacks, merge modes, copy opcodes, or the
xterm alternate-screen extension, and abstains on those representation
details: https://ecma-international.org/publications-and-standards/standards/ecma-48/

The implementation audit used freshly updated sources:

| implementation | revision |
| --- | --- |
| Alacritty | `1b2b36a64e88` |
| Ghostty | `fad7f854e8f9` |
| Kitty | `2caa3ca16bc9` |
| xterm | `6380a3eaed85` |
| Contour | `c51e15ed254e` |
| iTerm2 | `3ec57866cd9b` |
| VTE | `3d55bbdddb87` |
| Foot | `a635e0a196d9` |

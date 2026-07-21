# libvterm test DSL

Source: https://github.com/neovim/libvterm

Revision: `934bc2fbf21800ac3458a499df8820ca5fb45fd3`

License: MIT; see `LICENSE.upstream`.

The complete upstream `t/*.test` DSL corpus is preserved under `upstream/`.
The adapter runs the screen, resize, reflow, state, input, mouse, selection and
historical vttest fixtures against Zutty's control interface. Cell, pen,
cursor, title, mode and PTY-reply expectations are checked directly. Raw
parser callbacks and damage callback topology remain preserved for the next
adapter layer and are reported as pending rather than mistaken for checks.

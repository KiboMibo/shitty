# libvterm test DSL

Source: https://github.com/neovim/libvterm

Revision: `934bc2fbf21800ac3458a499df8820ca5fb45fd3`

License: MIT; see `LICENSE.upstream`.

The complete upstream `t/*.test` DSL corpus is preserved under `upstream/`.
The adapter runs the UTF-8 decoder, raw parser, fallback, screen, resize,
reflow, state, input, mouse, selection and historical vttest fixtures against
Zutty's control interface. Cell, pen, cursor, title, mode, PTY-reply and
normalized parser callback expectations are checked directly. Damage callback
topology remains preserved for a later renderer-facing adapter layer and is
reported as pending rather than mistaken for a check.

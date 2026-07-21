# libvterm test DSL

Source: https://github.com/neovim/libvterm

Revision: `934bc2fbf21800ac3458a499df8820ca5fb45fd3`

License: MIT; see `LICENSE.upstream`.

The complete upstream `t/*.test` DSL corpus is preserved under `upstream/`.
The first adapter layer runs the screen, resize, reflow and historical vttest
fixtures that have direct `?screen_*`, `?cursor` or `?lineinfo` goldens against
Zutty's rich `MODEL_SNAPSHOT`.  Callback-only parser/state expectations remain
preserved but are deliberately reported as pending instead of being mistaken
for checked assertions.

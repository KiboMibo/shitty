# Project style settings

Per-project settings that the shared [STYLE.md](STYLE.md) delegates here.
`ext/` contains vendored projects with their own copies of these files.

- **Macro prefix.** Project-owned macros use a `SHITTY_` prefix.
- **Namespace.** Shitty is a program, not a library: no project namespace.
  The vendored trees under `ext/` keep their own namespaces.
- **Formatter.** `./style.py` formats every tracked C++ source; include
  reordering skips `ext/`. `lib/shitty/render.comp` is intentionally excluded.

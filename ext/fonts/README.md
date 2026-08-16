# Embedded fallback fonts

These fonts are compiled into the binary (see `lib/shitty/generate_font_data.py`) and used
as the last font resolver and the tail of the glyph fallback chain, so the
terminal always has a usable font. All of them are licensed under the SIL Open
Font License 1.1 (see OFL.txt):

- JetBrainsMonoNerdFont-Regular.ttf — JetBrains Mono patched with Nerd Font
  symbols. Copyright 2020 The JetBrains Mono Project Authors
  (https://github.com/JetBrains/JetBrainsMono), Nerd Fonts patch
  (https://github.com/ryanoasis/nerd-fonts).
- NotoColorEmoji.ttf — color emoji. Copyright 2013 Google LLC
  (https://github.com/googlefonts/noto-emoji).
- NotoEmoji-Regular.ttf — monochrome emoji. Copyright 2013 Google LLC
  (https://github.com/googlefonts/noto-emoji).

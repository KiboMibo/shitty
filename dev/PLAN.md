Короткий вывод: готовые fixtures, goldens и fuzz-corpora собраны хорошо, но у
нескольких терминалов ещё не перенесена существенная часть независимого
semantic oracle.

Учёт upstream case означает отдельный исполняемый scenario в нашем suite.
Существующее более широкое покрытие используется для сверки, но само по себе
не заменяет перенос. Для приватного API без wire-эквивалента переносится
наблюдаемое поведение публичной операции, а расхождение документируется.

## Незакрытые источники

### libvterm

- Перенести точную топологию damage callbacks.

### Ghostty

- `PageList.zig` — 260 тестов.
- `Screen.zig` — 69 тестов остаются; первые 20 storage/screen scenarios
  учтены в `tests/test_ghostty_screen.py`, следующие 20 selection/copy
  scenarios — в `tests/test_ghostty_screen_selection.py`, ещё 20 сценариев
  line/word/semantic selection — в
  `tests/test_ghostty_screen_selection_semantic.py`, ещё 20 hyperlink/capacity/
  prompt-click scenarios — в
  `tests/test_ghostty_screen_capacity_prompt_click.py`. Полный current-upstream
  inventory уточнён: в `Screen.zig` 209, а не 208 тестов. Первые
  20 ранее пропущенных cursor/style/scrollback scenarios учтены в
  `tests/test_ghostty_screen_cursor_style_scrollback.py`, следующие 20
  viewport/scroll-region/scroll-above scenarios — в
  `tests/test_ghostty_screen_scroll_region_history.py`, ещё 20 hyperlink-dense/
  clone/clear-history scenarios — в
  `tests/test_ghostty_screen_clone_history.py`.
- `formatter.zig` — 100 тестов.
- key encoding — 90 тестов.
- bindings — 83 теста.
- selection — 55 тестов.
- parser — 24 теста.

94 initial fuzz seeds не взяты, но имеют низкую ценность рядом с полным cmin.

Основные источники: [Terminal.zig](/home/pg/monorepo/tmp/terminal-repos/ghostty/src/terminal/Terminal.zig), [Screen.zig](/home/pg/monorepo/tmp/terminal-repos/ghostty/src/terminal/Screen.zig).

### Contour unit tests

- Screen — все 349 scenario текущего upstream учтены в
  `tests/test_contour_screen.py`; последние 12 включают 5 явно обоснованных
  executable XFAIL вместо Contour-only placeholder state.
- Terminal local-path lookup сохранён как expected failure: Shitty пока не
  распознаёт существующие bare/relative paths относительно OSC 7 CWD.
- TextSizing — все 60 scenario текущего upstream учтены в
  `tests/test_kitty_text_sizing.py`. Три общих wide-cell/selection/DECSERA
  regressions проходят, остальные OSC 66 scenarios остаются executable
  expected failures. Последние 12 проверяют renderer bands, history/damage,
  selection и DECCRA только через существующие экранные операции; OSC 66
  representation и product API ради тестов не добавлялись.
- ShellIntegration — все 31 scenario учтены отдельными исполняемыми тестами.
  Шесть `lastCommandBlock()` cases проверяют точные prompt/output boundaries,
  reflow и отсутствие ложного блока через сохранённую semantic-разметку; шесть
  `livePromptSpan()` cases — через row geometry и существующий
  `cursorIsAtPrompt()`. Отдельной Contour-подобной GUI extraction API у Shitty
  по-прежнему нет, но это product gap, а не неперенесённый upstream oracle.

Это самый крупный готовый источник terminal semantics после Ghostty: [Screen_test.cpp](/home/pg/monorepo/tmp/terminal-repos/contour/src/vtbackend/Screen_test.cpp).

### xterm.js modern tests

- InputHandler — 192 теста.
- EscapeSequenceParser — 185 тестов.
- KittyKeyboard — 165 тестов.
- Buffer — 63 теста.
- Keyboard — 61 тест.
- BufferLine — 51 тест.
- selection — 44 теста.
- OSC/DCS/APC parsers — около 60 тестов.
- BufferReflow — 7 тестов.
- Unicode/charset/color parsing.

Самая ценная часть — InputHandler, BufferReflow, selection и keyboard. Чистые
callback tests парсера менее приоритетны: Ragel parser уже тестируется напрямую.

### iTerm2

- VT100Grid — 158 тестов.
- VT100Screen — 53 теста.
- LineBlock — 115 тестов.
- LineBuffer — 75 тестов.
- grid range arithmetic — 32 теста.
- TerminalHardRules — 20 тестов.
- legacy Screen — 108 тестов.
- legacy Grid — 64 теста.
- DCS parser — 34 теста.
- CSI parser — 30 тестов.
- Xterm parser — 27 тестов.
- semantic history — 54 теста.

Это сильный oracle для grid/history/resize, но самый дорогой для адаптации из-за
Swift/Objective-C модели: [VT100GridTests.swift](/home/pg/monorepo/tmp/terminal-repos/iterm2/ModernTests/VT100GridTests.swift).

### Alacritty units

- terminal core — 23 теста.
- selection — 16 тестов.
- grid/storage — 26 тестов.
- index/cell invariants.

Search и vi-mode пока не нужны.

### Foot

- scrollback erase и selection lifetime;
- URI range edits;
- Kitty/legacy key encoding;
- parser private bytes;
- width/variation-selector metadata.

Копировать внутренние grid structures смысла нет.

### tmux

Около 18 потенциально полезных regress scripts: `tty-keys`, `tty-draw-line`,
`input-osc`, `input-sgr`, mouse, UTF-8, theme report, window ops. Это в основном
oracle tmux, а не терминала; использовать их лучше как real-world streams.

## Рекомендуемый порядок

1. Contour Screen/Terminal.
2. Ghostty Screen/PageList и оставшиеся semantic assertions.
3. xterm.js InputHandler/reflow/keyboard/selection.
4. iTerm2 Grid/Screen/LineBuffer.

Самые важные области для независимого внешнего oracle: resize/reflow/history,
selection lifetime, input encoding, OSC replies/effects и damage semantics.

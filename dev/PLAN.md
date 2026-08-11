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

- InputHandler — все 194 теста текущего upstream учтены отдельными executable
  scenarios в `tests/test_xtermjs_input_handler_core.py` и
  `tests/test_xtermjs_input_handler_text.py`, а также
  `tests/test_xtermjs_input_handler_sgr.py` и
  `tests/test_xtermjs_input_handler_cursor.py` и
  `tests/test_xtermjs_input_handler_cursor_bounds.py` и
  `tests/test_xtermjs_input_handler_margins.py` и
  `tests/test_xtermjs_input_handler_wide.py` и
  `tests/test_xtermjs_input_handler_styles_osc.py` и
  `tests/test_xtermjs_input_handler_colors_erase.py` и
  `tests/test_xtermjs_input_handler_modes_async.py` (159 проходят, 35
  документированных policy XFAIL).
- EscapeSequenceParser — 185 тестов.
- KittyKeyboard — все 165 тестов текущего upstream учтены в
  `tests/test_xtermjs_kitty_keyboard.py` (158 проходят, 7 документированных
  policy XFAIL на обоих parser backend).
- Buffer — 23 ещё не перенесённых теста; первые 40 cases учтены в
  `tests/test_xtermjs_buffer.py` (37 проходят, 3 документированных policy
  XFAIL на обоих parser backend).
- Keyboard — 61 тест.
- BufferLine — 51 тест.
- selection — все 44 теста текущего upstream учтены: первые 19
  SelectionService cases находятся в `tests/test_xtermjs_selection_service.py`,
  оставшиеся 7 SelectionService и 18 SelectionModel cases — в
  `tests/test_xtermjs_selection_tail.py` (40 проходят, 4 документированных
  policy XFAIL).
- OSC/DCS/APC parsers — около 60 тестов.
- BufferReflow — все 7 тестов текущего upstream учтены в
  `tests/test_xtermjs_buffer_reflow.py` (6 проходят, 1 документированный
  policy XFAIL).
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

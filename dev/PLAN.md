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

### iTerm2

- legacy Screen — 108 тестов;
- legacy Grid — 64 теста;
- semantic history — 54 теста.

Это сильный oracle для grid/history/resize, но дорогой для адаптации из-за
Swift/Objective-C модели.

### tmux

Около 18 потенциально полезных regress scripts: `tty-keys`, `tty-draw-line`,
`input-osc`, `input-sgr`, mouse, UTF-8, theme report, window ops. Это в основном
oracle tmux, а не терминала; использовать их лучше как real-world streams.

Самые важные области для независимого внешнего oracle: resize/reflow/history,
selection lifetime, input encoding, OSC replies/effects и damage semantics.

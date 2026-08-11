Короткий вывод: готовые fixtures, goldens и fuzz-corpora собраны хорошо, но у
нескольких терминалов ещё не перенесена существенная часть независимого
semantic oracle.

Учёт upstream case означает отдельный исполняемый scenario в нашем suite.
Существующее более широкое покрытие используется для сверки, но само по себе
не заменяет перенос. Для приватного API без wire-эквивалента переносится
наблюдаемое поведение публичной операции, а расхождение документируется.

## Незакрытые источники

### tmux

Остались независимые cases из `input-edit` и `input-scroll`, а также
`tty-keys`, `tty-draw-line`, `input-keys`, `input-malformed`, `input-modes`,
`input-osc`, `input-sgr`, `input-unicode`, replies/requests, mouse, theme
report и window ops. Это в основном oracle tmux, а не терминала; использовать
их лучше как real-world streams.

Самые важные области для независимого внешнего oracle: resize/reflow/history,
selection lifetime, input encoding, OSC replies/effects и damage semantics.

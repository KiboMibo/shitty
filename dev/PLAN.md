Короткий вывод: готовые fixtures, goldens и fuzz-corpora собраны хорошо, но у
нескольких терминалов ещё не перенесена существенная часть независимого
semantic oracle.

Учёт upstream case означает отдельный исполняемый scenario в нашем suite.
Существующее более широкое покрытие используется для сверки, но само по себе
не заменяет перенос. Для приватного API без wire-эквивалента переносится
наблюдаемое поведение публичной операции, а расхождение документируется.

## Незакрытые источники

### tmux

Остались mouse, theme report и window ops. В
основном это oracle tmux, а не терминала;
использовать их лучше как real-world streams.

Самые важные области для независимого внешнего oracle: resize/reflow/history,
selection lifetime, input encoding, OSC replies/effects и damage semantics.

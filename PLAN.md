## Вердикт

Текущая архитектура тестов zutty выбрана правильно: настоящий бинарь, реальный PTY, headless control socket и снимок публично наблюдаемого состояния. Заменять это набором C++-тестов, вызывающих internals, не надо. Такие unit-тесты полезны для parser primitives, но хуже фиксируют поведение терминала как системы.

По объёму suite уже серьёзная: 61 файл, 628 Python-тестов, около 8 880 строк. Главный дефицит теперь не в количестве ручных сценариев, а в четырёх вещах:

- независимые источники ожидаемого поведения;
- replay реальных приложений;
- настоящий coverage-guided fuzzing;
- проверки renderer/frontend boundary.

Нашёл также две конкретные проблемы в декларациях текущего покрытия:

1. [vttest.py](/home/pg/monorepo/zutty/tests/vttest.py) не проверяет картинку. Он лишь доходит до завершения, отвечает `0` во вложенных меню и ищет `That's all, folks!`. Это smoke-тест PTY и lifecycle, а не «полный vttest conformance».
2. [differential.py](/home/pg/monorepo/zutty/tests/differential.py) фактически не differential: он посылает всего шесть запросов и отдельно проверяет формат ответов. Состояние экранов разных терминалов не сравнивается.

## Что делают другие терминалы

| Проект | Сильная сторона | Что взять для zutty |
|---|---|---|
| Ghostty | Огромное количество inline unit-тестов; отдельные AFL++ targets для parser, OSC и полного terminal stream; initial и coverage-minimized corpora. В текущем срезе 3 241 `test` declarations и 4 002 corpus-файла, хотя initial/cmin частично дублируются. [Fuzzer README](/home/pg/monorepo/tmp/terminal-repos/ghostty/test/fuzz-libghostty/README.md), [upstream](https://github.com/ghostty-org/ghostty) | Три отдельных native fuzz target: tokenizer, control strings, полный Vterm. Корпус сохранять и минимизировать по edge coverage. |
| Kitty | 351 Python-тест и 198 Go-тестов: parser events, screen, scrollback, resize, keyboard, mouse, clipboard, notifications, graphics, fonts; включён Unicode `GraphemeBreakTest`. [kitty_tests](/home/pg/monorepo/tmp/terminal-repos/kitty/kitty_tests), [upstream](https://github.com/kovidgoyal/kitty/tree/master/kitty_tests) | Отдельно тестировать parser dispatch и screen effects; использовать официальные generated Unicode vectors и полную матрицу клавиатуры. |
| Alacritty | 45 записанных terminal streams, включая `vim`, `tmux`, `htop`, shell completion и vttest; после replay сравнивается полная сериализованная grid. [ref.rs](/home/pg/monorepo/tmp/terminal-repos/alacritty/alacritty_terminal/tests/ref.rs), [fixtures](/home/pg/monorepo/tmp/terminal-repos/alacritty/alacritty_terminal/tests/ref) | Формат «сырой PTY stream + geometry/config + ожидаемый snapshot». Это лучший seed для регрессий Codex/mc. |
| VTE | Особенно сильное тестирование самого ECMA-48 parser FSM: valid/invalid CSI, OSC, DCS, C0/C1, параметры, subparameters, пределы чисел; отдельно Unicode width, tabstops, sixel parser. 115 зарегистрированных тестов. [parser-test.cc](/home/pg/monorepo/tmp/terminal-repos/vte/src/parser-test.cc), [upstream](https://gitlab.gnome.org/GNOME/vte) | Исчерпывающая таблица переходов parser state, включая запрещённые байты и recovery. |
| GNOME Terminal | Практически не тестирует эмуляцию сам: это GUI-обвязка над VTE. [meson.build](/home/pg/monorepo/tmp/terminal-repos/gnome-terminal/meson.build) | Рассматривать GNOME Terminal и VTE раздельно: frontend integration против terminal engine. |
| Konsole | 119 Qt-тестов parser/screen/history/PTY/width плюс ручные scripts с Unicode, wrap и цветами. [autotests](/home/pg/monorepo/tmp/terminal-repos/konsole/src/autotests) | Полезные вариации history, decoder и tokenization, но harness сильно привязан к internals. |
| WezTerm | Unit-тесты модели терминала, отдельного escape parser, изображений и Unicode BiDi conformance. [term tests](/home/pg/monorepo/tmp/terminal-repos/wezterm/term/src/test) | Generated conformance data и проверка dirty lines/атрибутов наряду с текстом. |
| Windows Terminal | 135 parser-тестов, 60 TerminalCore, 17 text-buffer, directed fuzz target и generated Unicode grapheme table. [parser](/home/pg/monorepo/tmp/terminal-repos/windows-terminal/src/terminal/parser) | Фаззить не только байты, но и последовательности абстрактных VT-команд и изменения размеров/state. |
| xterm.js | 57 unit-файлов и более 1 700 test calls; 76 старых `.in → .text` golden fixtures, browser/Playwright и WebGL stress tests. [Terminal2.test.ts](/home/pg/monorepo/tmp/terminal-repos/xterm.js/src/browser/Terminal2.test.ts) | Один corpus прогонять и через headless model, и через настоящий renderer. |
| libvterm | Очень компактный DSL: 43 файла, примерно 3 464 строки с командами `PUSH`, ожидаемыми parser callbacks, cells, cursor и screen damage. [fixtures](/home/pg/monorepo/tmp/terminal-repos/libvterm/t) | Идея хороша для table-driven Python-тестов, не обязательно копировать Perl/C harness. |
| Mosh | Два libFuzzer target и E2E harness: выполняет сценарий напрямую и через Mosh, затем сравнивает `tmux capture-pane`. [README](/home/pg/monorepo/tmp/terminal-repos/mosh/src/tests/README.md) | Сравнивать реальное приложение через эталонный и тестируемый путь; фиксировать resize и Unicode corner cases. |
| foot | В текущем дереве нашёл только большой тест config parser; автоматических тестов terminal core/protocol не обнаружил. [tests](/home/pg/monorepo/tmp/terminal-repos/foot/tests) | Как источник поведения полезен только differential/manual, не как готовая suite. |

Ghostty формулирует разумный порядок авторитетов: спецификация, затем xterm, затем поведение распространённых терминалов. Это важно: терминалы сознательно расходятся, поэтому слепое «zutty должен совпасть с Ghostty» неправильно. [Ghostty описывает свой xterm audit именно так](https://github.com/ghostty-org/ghostty).

## Внешние conformance suites

### esctest

Это самый ценный немедленный источник новых тестов: 543 теста в 80 файлах, около 9 015 строк. Он проверяет VT100–VT520 и xterm-поведение автоматически.

Главный трюк — `DECRQCRA`: esctest запрашивает checksum каждой отдельной клетки, поэтому может проверить экран настоящего терминала без доступа к internals. Также проверяются cursor reports и window operations. Есть маркировка VT level, known bugs и intentional deviations.

Минусы:

- старый Python 2;
- profiles только xterm/iTerm2;
- часть визуальных свойств недоступна через checksum;
- собственный «ideal» местами является мнением автора.

Но для zutty это не мешает: наш snapshot богаче DECRQCRA. Полезно и запускать upstream suite целиком, и переносить её интересные вариации в native Python tests. [Локальная копия](/home/pg/monorepo/tmp/terminal-repos/esctest-fdo), [upstream](https://gitlab.freedesktop.org/terminal-wg/esctest).

### vttest

Современный vttest давно не ограничен VT100: там VT220/320/420/520, character sets, reports, keyboard, margins, protected areas, colors и xterm extensions. Но большая часть результата рассчитана на визуальную оценку человеком. [Текущий manpage](https://xterm.dev/manpage-vttest/), [исходники](/home/pg/monorepo/tmp/terminal-repos/vttest).

Contour построил поверх него то, что нам нужно:

- scripted menu input;
- snapshots после известных шагов;
- 143 golden dumps;
- список известных gaps как ratchet: новый gap ломает build, исчезнувший старый gap тоже требует удалить запись;
- отдельно запускается esctest.

[Contour vtconformance](/home/pg/monorepo/tmp/terminal-repos/contour/src/vtconformance) — наиболее непосредственно переносимый дизайн.

### Termless и Terminfo.dev

[Termless](https://termless.dev/) предоставляет единый headless interface к xterm.js, Ghostty, Alacritty, WezTerm, libvterm, Kitty и другим engines: cells, attributes, cursor, modes, scrollback, replies, PTY и recordings. В suite заявлено 120+ cross-backend conformance scenarios.

Это хороший готовый differential layer, но не oracle: если пять engines согласились, они всё равно могут одинаково расходиться с DEC/xterm. Различия надо классифицировать, а не автоматически объявлять багом.

[Terminfo.dev](https://www.terminfo.dev/) поверх него строит большую матрицу feature probes для реальных терминалов, headless engines и multiplexers. Это отличный machine-readable inventory поддерживаемых протоколов.

### Unicode

[ucs-detect](https://ucs-detect.readthedocs.io/intro.html) проверяет реальные терминалы через движение cursor: wide/narrow, combining, ZWJ, regional indicators, VS15/VS16 и реальные тексты сотен языков. Он также probes Kitty keyboard, synchronized output, graphics и XTGETTCAP.

Для model layer надо импортировать официальный [Unicode GraphemeBreakTest](https://www.unicode.org/Public/UCD/latest/ucd/auxiliary/GraphemeBreakTest.txt) и правила [UAX #29](https://www.unicode.org/reports/tr29/). `ucs-detect` нужен отдельно, потому что корректная внутренняя ширина ещё не гарантирует корректный font shaping/raster output.

### wraptest

[wraptest](/home/pg/monorepo/tmp/terminal-repos/wraptest) — маленький, но очень содержательный тест deferred wrap/Last Column Flag. Он проверяет, какие операции сохраняют или сбрасывают скрытое wrap-state: TAB, LF, RI, SGR, CUP, EL, ED, DCH, ICH, ECH, CPR, DECSC/DECRC.

Его надо перенести одним из первых. Он проверяет именно те комбинации состояния, которые часто не покрываются тестом одной escape sequence. [Upstream](https://github.com/mattiase/wraptest).

### terminfo и производительность

[tack](https://invisible-island.net/ncurses/tack.html) проверяет соответствие реального терминала его terminfo description. Это отдельный слой: можно идеально реализовать протокол, но сломать приложения неверным `TERM`/terminfo.

[vtebench](https://github.com/alacritty/vtebench) измеряет только скорость чтения PTY. Сам проект честно не измеряет latency, frame consistency или responsiveness. Для zutty нужен расширенный вариант.

## Оценка текущего zutty

Сильные стороны:

- black-box boundary вместо вызова `Vterm` internals;
- настоящий PTY и реальные replies;
- богатый canonical snapshot: cells, graphemes, attributes, links, selection, scrollback;
- очень хорошее покрытие resize/history/selection;
- проверка fragmented input;
- уже есть метаморфизм whole stream против chunked stream;
- UI-sensitive Codex regressions уже превратились в тесты.

Слабые места:

1. `vttest` сейчас smoke-only.
2. `differential.py` сравнивает не поведение, а только валидность шести replies.
3. `fuzz_parser.py` — детерминированный random test, не coverage-guided fuzzer. Он почти не знает грамматику и редко добирается до глубоких валидных состояний.
4. Нет corpus replay реальных приложений.
5. Unicode проверяется хорошими вручную выбранными случаями, но не полным generated corpus.
6. Почти нет внешней проверки framebuffer/raster/GLFW/Wayland/X11.
7. Нет системной проверки throughput, event-loop fairness, frame count, памяти и latency под потоком в сотни мегабайт.
8. Нет machine-readable соответствия «поддерживаемая sequence → тесты → спецификация». Нынешний `COVERAGE.md` полезен человеку, но не обнаружит новую handler branch без теста.
9. Fuzzing пока не покрывает сочетания `write/resize/scroll/select/reset/switch-screen`, хотя именно stateful sequences обычно ломают terminal model.

## Рекомендуемая система тестирования

Нужны семь независимых слоёв:

1. Parser conformance  
   Все состояния ECMA-48, invalid bytes, CAN/SUB/ESC recovery, C0 внутри последовательностей, 7/8-bit forms, numeric overflow, OSC/DCS quotas.

2. Screen semantics  
   Текущие Python black-box tests плюс порт esctest и wraptest.

3. External conformance  
   Настоящий esctest; автоматизированный vttest с командами и snapshots; known-gap ratchet.

4. Replay corpus  
   Записи `bash/zsh`, `mc`, Codex, vim/neovim, tmux, htop, less/man, fzf. Формат должен хранить PTY bytes, geometry/resize events и snapshots в точках-barrier. Timing не должен определять correctness.

5. Differential  
   Один stream прогоняется через zutty, Ghostty, xterm.js/libvterm/Alacritty. Сравниваются normalized cells, cursor, modes, replies и scrollback. Различия попадают в явный allowlist с объяснением и выбранным oracle.

6. Fuzz/property testing  
   Native persistent targets для parser, OSC/DCS и полного Vterm. Инварианты:

   - cursor и margins всегда в допустимых пределах;
   - wide lead/continuation согласованы;
   - grapheme не теряется при chunking;
   - история и viewport не ссылаются на удалённые строки;
   - whole, bytewise и произвольное chunking дают одинаковый результат;
   - reset идемпотентен;
   - query не меняет состояние;
   - отсутствие crash/hang/unbounded allocation.

   ASan на этом musl пока не закладываю как обязательный gate. AFL++ edge coverage всё равно полезен; sanitizers можно добавить позднее в другом окружении.

7. Renderer/frontend  
   Offscreen Vulkan golden tests для glyph placement, clipping, cursor, selection, blink phases, HiDPI и damage. Отдельный минимальный Wayland/X11 tier проверяет GLFW events, clipboard и window resizing.

## Очерёдность с максимальной отдачей

1. Перенести весь `wraptest` как matrix test.
2. Подключить upstream esctest с ratchet известных отклонений.
3. Переделать vttest integration по схеме Contour: scripted paths и snapshots, а не только clean exit.
4. Ввести replay format и записать `mc`, Codex `/resume + scroll`, vim, tmux и htop.
5. Сделать настоящий screen-state differential runner.
6. Прогонять каждый существующий protocol fixture целиком, bytewise и по всем значимым границам.
7. Добавить AFL++ targets и corpus из Ghostty, tmux, xterm.js, libvterm и собственных regressions.
8. Импортировать Unicode generated data и запустить `ucs-detect` как renderer integration.
9. Затем offscreen renderer и performance/fairness suite.

## Скачанные исходники

Из-за того что `/home/pg/repos` здесь является symlink на read-only `/home/pg/Downloads`, по согласованию всё лежит в [terminal-repos](/home/pg/monorepo/tmp/terminal-repos).

Скачаны Ghostty, Kitty, Konsole, VTE, GNOME Terminal, Alacritty, foot, WezTerm, Contour, xterm, vttest, libvterm, esctest, Termless, iTerm2, xterm.js, Windows Terminal, Mosh, tmux, libtsm, ucs-detect, vtebench, wraptest и tack — каждый отдельным git checkout. Никаких изменений в zutty при исследовании не делал.

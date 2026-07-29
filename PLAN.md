Короткий вывод: мы хорошо собрали готовые fixtures, goldens и fuzz-corpora, но у ряда терминалов перенесли только входные байты, не их assertions. Поэтому формально тестов много, а значительная часть независимого semantic oracle ещё не взята.

## Что взято полноценно

Здесь upstream-ожидания действительно проверяются:

- esctest — все 543 метода, развёрнутые в 549 случаев.
- libvterm — все 43 `.test`, кроме точной топологии damage callbacks.
- Alacritty — все 45 reference recordings.
- Contour vttest — 18 сценариев, использующих все 143 golden dumps.
- xterm.js legacy fixtures — все 76 пар `.in/.text`.
- xterm `vttests` — все 60 исполняемых сценариев.
- Termless — все 46 релевантных cross-backend случаев.
- tack — 118 случаев.
- vtebench — все 12 workload.
- wraptest, ucs-detect — полностью.
- Ghostty cmin — 20 OSC + 616 parser + 3271 stream.
- Mosh fuzz corpora — все 27 файлов.
- tmux fuzz corpus — 4166 элементов.

Это хорошая база: classic VT conformance, реальные записи и crash coverage представлены хорошо.

## Где взяты байты, но не upstream oracle

Это главная находка аудита.

### Ghostty

Наши 70 «semantic» cases берут только литеральные `nextSlice()` и проверяют:

- whole input;
- исходное разбиение Ghostty;
- побайтовую подачу.

Assertions Ghostty не исполняются. Это прямо отражено в [Ghostty UPSTREAM.md](/home/pg/monorepo/shitty/tests/ghostty/UPSTREAM.md).

Из 75 тестов `stream_terminal.zig` вообще пропущены пять resize-тестов:

- synchronized output при resize;
- mode 2048 geometry reports;
- suppress reports;
- atomicity при неудачном resize;
- canonical state после resize effects.

Но основной хвост значительно больше:

- `Terminal.zig` — 404 теста;
- `PageList.zig` — 260;
- `Screen.zig` — 208;
- `formatter.zig` — 100;
- key encoding — 90;
- bindings — 83;
- semantic prompt OSC — 64;
- selection — 55;
- parser — 24.

То есть fuzz-corpus взят полностью, а semantic model Ghostty — почти не взят. Основные источники: [Terminal.zig](/home/pg/monorepo/tmp/terminal-repos/ghostty/src/terminal/Terminal.zig), [Screen.zig](/home/pg/monorepo/tmp/terminal-repos/ghostty/src/terminal/Screen.zig).

Не взяты также 94 initial fuzz seeds, но они имеют низкую ценность рядом с полным cmin.

### Kitty

Взяты 426 из 434 вызовов `pb()`, но каждый вызов превращён в отдельный тест. Из-за этого теряются:

- состояние между вызовами;
- структура исходного test method;
- upstream callback expectations;
- последовательности reset/setup;
- assertions после OSC/DCS/CSI.

Из 28 проверок строк перенесено 19. Всего в файле 15 полноценных тестовых методов. Следующий шаг здесь — импортировать тест как транзакцию, а не ещё больше отдельных `pb()`.

### Konsole

~~Все 146 declarative tokenizer rows теперь assertion-based: из upstream
`ProcessToken` извлекается ожидаемое семейство control/ESC/CSI/VT52 и
нормализованный payload, после чего проверяются whole и bytewise feeds.
Единственная современная адаптация — standalone NUL игнорируется согласно
ECMA-48, тогда как Konsole создаёт внутренний token и игнорирует его позже.~~

~~Все 11 semantic methods из `Vt102EmulationTest.cpp` перенесены как
транзакции: parser output/replies, buffered/synchronized updates и 9 семейств
Kitty keyboard, включая stack/set/reset, event types, legacy, Ctrl и text
keys.~~

~~Screen — все 6 именованных тестов и все внутренние assertions; History — все
7, включая reflow, finite/disabled policy и вытеснение, перенесены в Python.~~

Осталось:

- ~~`testTokenFunctions` неприменим: это self-test числового ABI Konsole,
  сравнивающий новые constexpr с продублированными старыми `TY_*` macros.
  Наш catalog вместо этого проверяет все 13 встречающихся token constructors и
  отклоняет неизвестные, а продуктовый parser не имеет этого packed ABI.~~
- ~~CharacterWidth — все 25 data rows перенесены; внутренний width primitive
  доступен batch test API. Импорт исправил ширину Cf format controls и
  U+1160..U+11FF по консенсусу Konsole/Ghostty/Kitty/Foot/VTE/WezTerm/xterm.js.~~
- ~~KeyboardTranslator — все 24 modifier-wildcard data rows проверяются через
  реальный generic frontend input и F12 encoding. `testHexKeys` проверяет
  Konsole-specific parser пользовательских keytab; он неприменим, поскольку
  Shitty намеренно не имеет keytab DSL. Его единственный стандартный default,
  Backspace → DEL, уже покрыт продуктовым тестом.~~
- ~~PtyTest — применимые `testWindowSize` и `testRunProgram` перенесены.
  `testWindowSize` нашёл отсутствие `ws_xpixel/ws_ypixel`; теперь PTY получает
  точный pixel extent клеточной области при старте и resize. Три оставшихся
  метода являются setter/getter tests внутренней конфигурации Konsole
  (flow-control, erase char, utmp) и классифицированы как неприменимые.
  Отдельного mouse autotest в текущем Konsole нет, а все selection/copy
  assertions уже входят в шесть перенесённых Screen tests.~~

### Windows Terminal

128 `ProcessString()` перенесены как whole-versus-bytewise streams. Upstream assertions не перенесены.

Остались особенно полезные:

- adapter — 53 теста;
- InputEngine — 25;
- input adapter — 9;
- mouse — 5;
- Kitty keyboard — 4;
- selection — 21;
- terminal buffer — 10;
- большая параметрическая Reflow suite;
- `ScreenBufferTests` — 113, но часть из них привязана к Win32 console model.

### WezTerm

Из пяти выбранных файлов:

- 53 исходных теста;
- 157 `term.print()` sites;
- 74 `assert_visible_contents`.

У нас сейчас:

- 136 отдельных stream cases;
- 28 видимых screen checkpoints.

Пропущено то, что адаптер пока не умеет выразить:

- resize/reflow;
- history и scrollback;
- dirty-line/damage assertions;
- semantic zones;
- left/right margins;
- hyperlinks;
- richer selection;
- динамически построенные streams.

Файлы уже лежат у нас — выгоднее расширить DSL адаптера, чем добавлять новые источники.

### VTE

В upstream `parser-test.cc` зарегистрировано 36 семейств. Мы покрываем девять семейств, развёрнутых в 34 targets.

Не взяты:

- invalid ESC;
- все charset 94/96 варианты;
- known ESC/CSI/SCI/DCS tables;
- CSI clear/recovery;
- SCI;
- часть control handling;
- внутренние sequence-builder/glue tests.

Последние не особо полезны, зато отдельно стоит взять:

- tab stops и resize — 7;
- bracketed paste sanitization — 6;
- UTF-8 replacement/error behavior — 2;
- modes/color — небольшой остаток.

## Крупные полностью неиспользованные источники

### Contour unit tests

Мы взяли только vtconformance goldens. Не взяты:

- Screen — 343;
- Terminal — 119;
- InputGenerator — 122;
- TextSizing — 59;
- Grid — 32;
- ShellIntegration — 31;
- KittyClipboard — 19;
- RectangularAreaChecksum — 12.

Это, вероятно, самый большой готовый источник terminal semantics после Ghostty. [Screen_test.cpp](/home/pg/monorepo/tmp/terminal-repos/contour/src/vtbackend/Screen_test.cpp)

Graphics/ReGIS/Sixel/Kitty Graphics пока брать не нужно.

### xterm.js modern tests

Legacy fixtures взяты, современные unit tests — нет:

- InputHandler — 192;
- EscapeSequenceParser — 185;
- KittyKeyboard — 165;
- Buffer — 63;
- Keyboard — 61;
- BufferLine — 51;
- selection — 44;
- OSC/DCS/APC parsers — около 60;
- BufferReflow — 7;
- Unicode/charset/color parsing.

Самая ценная часть — InputHandler, BufferReflow, selection и keyboard. Чистые callback tests парсера менее приоритетны, потому что Ragel parser у нас уже тестируется непосредственно.

### iTerm2

Не взято ничего. Полезное ядро:

- VT100Grid — 158;
- VT100Screen — 53;
- LineBlock — 115;
- LineBuffer — 75;
- grid range arithmetic — 32;
- TerminalHardRules — 20;
- legacy Screen — 108;
- legacy Grid — 64;
- DCS parser — 34;
- CSI parser — 30;
- Xterm parser — 27;
- semantic history — 54.

Это очень сильный oracle для grid/history/resize, но самый дорогой для адаптации из-за Swift/Objective-C модели. [VT100GridTests.swift](/home/pg/monorepo/tmp/terminal-repos/iterm2/ModernTests/VT100GridTests.swift)

### ~~libtsm~~ — готово

~~Все 32 assertion-based теста перенесены в Python: screen — 4, selection —
12, VTE — 9, mouse — 7. Импорт нашёл и исправил lifetime selection при
прокрутке видимой строки, частичное клипование selection при вытеснении из
history и кодирование release в SGR-pixel mouse. Специфичные для nullable C
ABI проверки отмечены как неприменимые; устаревшее ожидание игнорировать OSC 4
set заменено современной проверкой set+query.~~

### ~~Mosh semantic display tests~~ — готово

~~Все 17 terminal-display regressions перенесены в Python с прямым oracle по
ячейкам, цветам, OSC 8, wrap-флагам, курсору и resize. Старый upstream XFAIL
для первых восьми indexed colors заменён современной SGR-проверкой.~~

### Alacritty units

Recordings взяты полностью, но остаются:

- terminal core — 23;
- selection — 16;
- grid/storage — 26;
- index/cell invariants.

Search и vi-mode нам пока не нужны.

### Foot

Большого скрытого VT-suite там нет. Имеются небольшие встроенные tests, из которых полезны только поведенческие аналоги:

- scrollback erase и selection lifetime;
- URI range edits;
- Kitty/legacy key encoding;
- parser private bytes;
- width/variation-selector metadata.

Копировать их внутренние grid structures смысла нет.

### tmux

Кроме fuzz corpus есть около 18 потенциально полезных regress scripts: `tty-keys`, `tty-draw-line`, `input-osc`, `input-sgr`, mouse, UTF-8, theme report, window ops. Но это преимущественно oracle tmux, а не терминала. Их лучше использовать как real-world streams, не как эталон состояния.

## Рекомендуемый порядок

Я бы импортировал так:

1. ~~Mosh semantic display tests.~~
2. ~~libtsm — все 32.~~
3. ~~Konsole Screen/History, selection/copy, tokenizer, width, keyboard и
   применимые PTY methods.~~
4. Расширить существующий WezTerm screen adapter до всех 74 checkpoints.
5. Windows Terminal adapter/input/mouse/selection/reflow.
6. VTE tabstops/paste/UTF-8 и known-sequence matrices.
7. Kitty — сохранить исходные test transactions и callbacks.
8. Contour Screen/Terminal/InputGenerator.
9. Ghostty Terminal/Screen/PageList и OSC assertions.
10. xterm.js InputHandler/reflow/keyboard/selection.
11. iTerm2 Grid/Screen/LineBuffer.

Самые важные поведенческие области, где нам нужен именно независимый внешний oracle: resize/reflow/history, selection lifetime, input encoding, OSC replies/effects и семантика damage. Собственных тестов на это у нас много, но они подтверждают нашу модель нашей же моделью.

Репозиторий не изменял; рабочее дерево чистое.

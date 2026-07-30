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

~~Все 14 полноценных test methods учтены build-time validator. Девять
семантических методов перенесены как цельные product transactions с состоянием,
parser callbacks, replies, host actions, setup/reset и assertions после
OSC/DCS/CSI. Base64 покрыт native unit tests; все 313 UTF-8 oracle rows и 24
split sequences проходят через product decoder; DECCARA имеет отдельную
product transaction. Внутренние Kitty SIMD/backend, fixed producer-buffer и
`find_either_of_two_bytes` не соответствуют нашему API. Kitty Graphics
исключён из плана.~~

~~Все 28 screen observations учтены: 19 статически извлечённых checkpoints
остаются независимыми tests, оставшиеся девять проверяются в stateful
transactions (шесть empty-row, CSI, REP и DCS). Все 426 статических `pb()`
по-прежнему независимо проверяются whole/bytewise; восемь динамических путей
покрыты транзакциями методов.~~

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
- 73 исполняемых `assert_visible_contents` (прежние 74 включали определение
  самого helper).

У нас сейчас:

- 136 отдельных stream cases;
- ~~все 73 visible-screen checkpoints: 28 извлекаются статически, ещё 45
  переписаны как явные Python transactions для dynamic strings, reset,
  left/right margins, selection side effects и resize/reflow. Catalog
  инвентаризирует каждый call site, все проходят без XFAIL. Импорт добавил
  подтверждённую WezTerm/Ghostty/spec семантику OSC 133 `L` fresh-line.~~

Остались non-visible oracles из тех же файлов:

- ~~все 64 cursor assertions перенесены, включая 15 resize checkpoints,
  visibility/default shape и wrap-pending. Три one-past-grid ожидания WezTerm
  нормализованы к последней физической клетке по DEC/xterm; импорт исправил
  расширение default tab stops после resize кастомизированной таблицы;~~
- ~~все 17 dirty-line/damage assertions перенесены. Исходные stable-line
  ожидания сохранены как oracle и явно переведены в visible renderer rows:
  WezTerm индексирует физические строки вместе с history, Shitty передаёт
  renderer координаты текущего viewport;~~
- ~~все 27 `assert_all_contents` и 20 соседних stable-row assertions
  перенесены. Stable RowId в Shitty намеренно отсутствует, поэтому проверяются
  эквивалентные ordered history contents, retained-row count и viewport
  origin. Четыре snapshots адаптированы к нашему power-of-two ring, который
  хранит больше запрошенного минимума; отключённый upstream selection test
  нормализует неявный padding последней строки;~~
- ~~все 3 semantic-zone snapshots и единственный semantic cell-attribute
  assertion перенесены. Импорт добавил OSC 133 `I` (input до конца строки) и
  исправил OSC 133 `A`: prompt начинается с новой строки по согласованному
  поведению WezTerm, Ghostty и semantic-prompts specification;~~
- ~~все 3 hyperlink attribute assertions перенесены. Проверяются URI,
  стабильность identity между OSC 8 и SGR reset, смена identity и отключение
  ссылки через DECSTR. Импорт исправил soft reset: активная OSC 8 ссылка
  теперь сбрасывается по согласованному поведению WezTerm, Kitty и VTE;~~
- ~~все 12 selection clipboard assertions перенесены. Современные адаптации:
  не создаём trailing newline за границей экрана, сохраняем явно записанный
  trailing blank и соединяем soft-wrapped строки в одну логическую;~~
- ~~все оставшиеся metadata assertions перенесены: 2 cell/BCE snapshots,
  4 line-mode assertions и 2 Unicode/NFC/grapheme assertions. Проверяются
  background after DCH, ED background-color erase, double-width и обе половины
  double-height строк, single-width reset и исходный Hangul cluster.~~

Файлы уже лежат у нас — выгоднее расширить DSL адаптера, чем добавлять новые источники.

### VTE

В upstream `parser-test.cc` зарегистрировано 36 семейств. Все
terminal-observable семейства учтены: 21 перенесено в 46 targets, два SCI
семейства намеренно отклонены, а 13 внутренних helper families явно
классифицированы как неприменимые.

Перенесено и классифицировано:

- ~~tab stops и resize — все 7 зарегистрированных семейств перенесены через
  реальные HTS/TBC/CHT/CBT/RIS/resize и read-only oracle полной таблицы;~~
- VTE-внутренние `Tabstops::resize(fill=false)` и synthetic `endpos` не имеют
  terminal protocol эквивалента. Исходные assertions сохранены у нас, но
  помечены неприменимыми: Shitty всегда заполняет новые доступные колонки, а
  отсутствие следующего/предыдущего stop зажимает в границу экрана или margin;
- ~~bracketed paste sanitization — полный `pastify-test.cc`: 71
  зарегистрированный случай классифицирован, 70 production-reachable cases
  перенесены через настоящий Clipboard/input/PTY path. Проверяются 29 C0/DEL,
  32 C1, восемь размещений каждого control, idempotence, CR/LF и побайтовое
  разбиение. Импорт исправил небезопасный passthrough controls и состояние на
  границах chunks. Единственный C1-bracket case неприменим: это параметр
  внутреннего VTE helper, который сам VTE в production всегда выключает;~~
- ~~control handling — все 52 записи VTE проверяются через parser trace,
  включая raw C1 mode. Внутренний VTE NOP-token для NUL адаптирован к
  observable ECMA-48 поведению: NUL игнорируется, как также делают xterm.js и
  уже перенесённый Konsole corpus;~~
- ~~invalid ESC — все 32 `ESC 0/n`/`ESC 1/n` проверяются целиком и с разрывом
  перед final: ни один не может завершиться как escape sequence;~~
- ~~CSI clear/recovery — полный upstream matrix: все 74 префикса
  maximum-argument CSI × 16 размеров следующего CSI, итого 1 184 случая,
  подтверждают очистку параметров и состояния после abort;~~
- ~~SCI и SCI known не переносятся как поведение: VTE generic ECMA parser
  занимает `ESC Z` под SCI, но DEC/xterm и современные Foot, Ghostty, WezTerm
  и Konsole используют его как DECID. По правилу majority-over-obsolete-spec
  Shitty сохраняет DECID; его 7/8-bit варианты уже покрыты esctest. Исходный
  VTE corpus сохранён verbatim;~~
- ~~charset designation — все шесть upstream families и все 9 246
  designators перенесены с семантическим oracle по четырём G-slots: 2 528
  single-byte 94, 1 659 single-byte 96, 2 531 multibyte 94, 2 133 multibyte
  96, 158 control и 237 DOCS. Поддерживаемые DEC/ISO/NRC sets проверяются
  точно; неизвестные single-byte sets выбирают default mapping, а
  неподдерживаемые multibyte/control/DOCS не меняют G-slots. Импорт исправил
  ошибочную запись multibyte designation в G0 и ложное распознавание
  modified/96 finals как DEC/NRC;~~
- ~~known ESC/CSI/DCS — все три сгенерированные upstream tables перенесены
  byte-for-byte: 48 ESC, 204 CSI и 22 DCS signatures, включая все 174 VTE
  NOP entries. Все 274 проверяются whole и bytewise через настоящий parser
  trace; 103 поддерживаемых Shitty signatures дополнительно проверяются
  напрямую на конкретный ParserIface callback, а остальные 171 — на
  отсутствие product dispatch. Современные конфликты `CSI ? u` и `CSI ? m`
  разрешены в пользу Kitty keyboard и xterm modify-key protocols;~~
- ~~UTF-8 replacement/error behavior — оба теста перенесены полностью:
  1 112 064 допустимых scalar values и все 108 encoding_rs malformed
  vectors. Импорт исправил maximal-subpart replacement для E0/ED/F0/F4;~~
- ~~modes/color — все 2 mode и 5 color families перенесены. Mode tests идут
  через SM/RM/RIS, DECRQM и XTSAVE/XTRESTORE; все 782 X11 names проверяются
  через OSC 12. Импорт добавил standard XParseColor named colors. Hash-form
  следует настоящему Xlib: компоненты left-justified, а не low-bit replicated,
  как во внутреннем value-object parser VTE; `rgbi:` оставлен валидным по
  Xcms/xterm/Ghostty. CSS/alpha/stringify inputs сохранены и адаптированы к
  OSC: непротокольные формы не меняют цвет.~~
- VTE C++ bitset copy/BDSM и CSS rgba/hsl/alpha serialization не имеют
  terminal-protocol observable; исходные assertions сохранены verbatim и
  помечены неприменимыми к Shitty.
- ~~13 `arg`, `string` и `glue/*` families из `parser-test.cc` неприменимы:
  они тестируют packed `vte_seq_arg_t`, VTE string container, numeric/string
  conversion helpers, tokeniser и C++ sequence builder. У Shitty этих ABI и
  типов нет; их terminal-observable границы уже исчерпывающе покрыты
  parameter/max/recovery, OSC/DCS length и whole/bytewise matrices.~~

## Крупные полностью неиспользованные источники

Отдельная неприменимая проверка уже импортированного xterm `vttests`:
`query-xres.pl` требует xterm X resource database через XTGETXRES. У Shitty
на Wayland/Cocoa такого database нет; сценарий сохранён и исполняется как
ожидаемо неприменимый. Чужая shell/TTY обвязка `resize.sh`, `title.sh` и
`version.sh` переведена в конечные Python-сценарии: проверяются geometry
query/resize request, title query/update/restore и version query; оригиналы
сохранены verbatim.

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
4. ~~Расширить существующий WezTerm screen adapter до всех 73 исполняемых
   visible checkpoints.~~
5. ~~Все WezTerm non-visible oracles: cursor, selection, damage,
   history/stable-row, semantic, hyperlink и line/cell metadata.~~
6. Windows Terminal adapter/input/mouse/selection/reflow.
7. ~~VTE tabstops/paste/UTF-8 и known-sequence matrices.~~
8. ~~Kitty — сохранить исходные test transactions и callbacks.~~
9. Contour Screen/Terminal/InputGenerator.
10. Ghostty Terminal/Screen/PageList и OSC assertions.
11. xterm.js InputHandler/reflow/keyboard/selection.
11. iTerm2 Grid/Screen/LineBuffer.

Самые важные поведенческие области, где нам нужен именно независимый внешний oracle: resize/reflow/history, selection lifetime, input encoding, OSC replies/effects и семантика damage. Собственных тестов на это у нас много, но они подтверждают нашу модель нашей же моделью.

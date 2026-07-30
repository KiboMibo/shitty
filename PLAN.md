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

~~Все пять resize-тестов `stream_terminal.zig` учтены. Четыре перенесены
product-level тестами: resize с неизменной сеткой сбрасывает synchronized
output по консенсусу Ghostty/Foot/Kitty; mode 2048 сообщает точную геометрию
клеточной области и молчит в выключенном состоянии; чтение resize reply не
меняет canonical state. Ghostty-ветви без pixel geometry и без write callback
не существуют в Shitty: `Composer` всегда хранит полную pixel geometry, а
`ptyOutput` является обязательной зависимостью Vterm. Upstream OOM-тест
классифицирован как неприменимый: `libstd::allocateMemory()` имеет намеренно
fatal OOM contract (`STD_INSIST`), поэтому recoverable allocator failure,
которую моделирует Zig `FailingAllocator`, в продукте отсутствует. Screen
resize при обычном исключении уже строит замену в новом `ObjPool` и меняет
указатели только после успешного построения.~~

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

~~Первые девять resize-тестов `Terminal.zig` учтены. Reject-zero до
мутации перенесён буквально; reset synchronized output, pixel-only resize и
геометрия уже проверяются `test_ghostty_resize.py`. Optional pixel dimensions
не соответствуют нашему контракту: `Composer` получает cell и pixel geometry
одной транзакцией. Saturating `u32` multiplication неприменим, поскольку
публичная геометрия Composer и POSIX `winsize.ws_xpixel/ws_ypixel` намеренно
`u16`. Пять allocator/failpoint ветвей, включая замену alternate, относятся к
recoverable Zig allocator; libstd OOM является fatal и эти состояния в
продукте недостижимы.~~

~~Блок `Terminal.zig:4358-4596` перенесён в
`test_ghostty_terminal_input.py`: saturation cursor position, plain/chunked
input, wrap и forced scroll, unique style, pathological grapheme growth,
zero-width start/pending-wrap, long line и все right-edge/one-column wide-char
варианты. Отдельный upstream `dirty` вариант покрывается более строгими
инкрементальными Screen-тестами, которые сверяют каждый damage update с полным
рендером. Для leading ZWJ принят общий результат Ghostty/Kitty/Foot — joiner
без базы игнорируется; standalone combining marks сохранены ради Mosh и
существующего терминального fallback.~~

~~Блок `Terminal.zig:4615-5172` учтён. Пять wide-cell overwrite regressions
перенесены в `test_ghostty_terminal_input.py`: очистка обеих половин, отсутствие
порчи хвоста предыдущей строки и удаление bold/direct-background metadata.
Применимая Unicode-семантика перенесена в `test_ghostty_grapheme.py` через
побуквенную подачу: family/pirate ZWJ, VS15/VS16 и valid emoji modifier.
Disabled/toggled mode 2027 неприменимы, поскольку Shitty намеренно сообщает
permanently set и всегда хранит extended grapheme clusters. `graphemeWidth`
parity полнее проверяется официальными Unicode 17 vectors и полным
ucs-detect. Ghostty-разрыв quote + emoji modifier сохраняет удалённое из
Unicode 11 правило GB10; актуальный UAX #29 GB9 и Kitty/utf8proc объединяют
modifier как Extend, поэтому зафиксирован современный результат. Из этого же
следуют неприменимость четырёх disabled-mode VS tests и отсутствие их dirty
transition.~~

~~Блок `Terminal.zig:5173-6072` учтён. В `test_ghostty_grapheme.py`
перенесены damage каждого кодпоинта строящейся графемы; сужение VS15 со
снятием pending-wrap; расширение VS16 в последних клетках, перенос на следующую
строку, damage обеих строк и сохранение hyperlink; повторные VS16-кластеры;
атомарный перенос Devanagari-кластера через нижнюю границу со scrollback; очистка
grapheme payload при перезаписи lead и tail. Обычная ширина combining/Indic и
right-edge варианты уже строже покрыты Unicode 17 matrix. Внутренняя граница
Ghostty PageList заменена наблюдаемой продуктовой границей live
screen/scrollback. Три Ghostty-теста отбрасывают «невалидный» VS15/VS16 из
payload; актуальный UAX #29 относит variation selectors к Extend и требует
GB9 без разрыва, поэтому наши тесты фиксируют сохранение селектора, включая
последующий combining mark. Намеренно неверный Ghostty fast path
Prepend+ASCII отвергнут в пользу UAX #29 GB9b, уже проверяемого whole/chunked.
Запись при открытом scrollback уже покрыта более строгим
`test_write_while_scrolled_changes_only_live_screen`.~~

~~Блок `Terminal.zig:6073-6899` учтён. Charset designation, locking/single
shifts, GL/GR, полный DEC/NRC mapping и отсутствие damage от одной смены
charset проверяются `test_unicode_charset_matrix.py`; non-ASCII после
designation остаётся Unicode. Kitty unicode placeholder относится к
исключённому Kitty Graphics protocol. Soft wrap и продолжение semantic prompt
перенесены как наблюдаемые cell semantics. Right-margin wrap, выход за margin,
перенос wide glyph целиком и точные damage rows проверяются вместе с
горизонтальными margins. Найден и исправлен grid-инвариант DECAWM=off:
двухколоночный glyph больше не записывается усечённой одноколоночной клеткой,
а VS16 не превращает последнюю клетку в половину wide pair. Это соответствует
большинству Ghostty/Alacritty/xterm.js; отличающиеся Kitty и Foot допускают
clip/overwrite. Hyperlink start/reuse/end/change/overwrite, wide-edge
hyperlink и GC identity уже строже покрыты OSC matrix и protocol tests.
LF/CR/LNM, pending-wrap reset, origin/margins, BS, HT/CBT и tab stops покрыты
cursor/mode matrices и импортированными esctest cases.~~

~~Блок `Terminal.zig:6900-7845` учтён. Все cursorPos cases — clipping,
pending-wrap reset, origin относительно vertical и horizontal margins и
нулевые/default coordinates — уже покрыты cursor/DEC matrices через реальные
CUP/HVP. DECSTBM/DECSLRM defaults, single parameter, home side effect и
сохранение предыдущего region после invalid/equal bounds проверяются
наблюдаемыми scroll/edit transactions; для equal bounds добавлены отдельные
Ghostty regressions. Все protocol-observable IL cases покрывают full и partial
regions, cursor home, count clamping, operation outside region, erase colors,
точный damage каждой сдвинутой строки, сброс pending-wrap/wrap metadata,
grapheme и hyperlink metadata и обе пары margins. Ghostty PageList
page-boundary invalidation, style-map refcounts и capacity retry относятся к
удалённой storage architecture; эквивалентный продуктовый инвариант проверяет
перенос rich `CellExtra` через строку и native incremental-render oracle.
Прямой `insertLines(0)` не является terminal protocol case: ECMA-48 default
для `CSI 0 L` равен одной строке, а внутреннего zero-count API у Shitty нет.~~

~~Блок `Terminal.zig:7846-8659` учтён. CSI SU/SD проверяются для full screen,
vertical region и partial-width region: содержимое, неизменный cursor, erase
attributes, точные damage rows, large-count clipping и отсутствие history у
неполного region уже покрыты editing/scrollback/Windows Terminal matrices.
Добавлены Ghostty-oracles для сохранения pending-wrap до следующей печати и
для побитового перемещения/очистки hyperlink metadata внутри horizontal
margins при сохранении клеток снаружи. Full-width SU на primary сохраняет
вытесненную строку в scrollback, а `save_lines=0` и regions её не сохраняют —
это уже проверяется отдельными scrollback tests. Внутренние Ghostty
PageList viewport coordinates и `page_row.dirty` заменены наблюдаемыми
history snapshots и полным incremental-render oracle Screen.~~

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

- ~~adapter — 46 из 53 методов перенесены и ещё 7 классифицированы: cursor,
  SGR, device/status replies, DA1/DA2/DA3, DECRQDE, DECREQTPARM, DECRQSS,
  mode/report, palette, dynamic-color, keyboard/keypad, title и line-feed
  blocks, checksum, C1 output, window geometry, DECRQTSR color-table report и
  DECTABSR tab-stop save/restore; импорт добавил
  отсутствовавшие 10-entry xterm SGR ring stack, printer DSR,
  displayed-extent/terminal-parameter reports, permanent grapheme-mode report
  и корректный default пустых RGB subparameters. DEC macro/DRCS/page-memory
  subsystems и экспериментальный VS Code completions host UI классифицированы
  как неприменимые; все 53 upstream methods учтены;~~
- ~~InputEngine — все 25 методов проверяют отсутствующий у терминала слой
  VT input stream → Win32 `INPUT_RECORD`. Обратная, наблюдаемая сторона
  протокола покрыта keyboard/mouse matrices; Win32 ABI неприменим;~~
- ~~input adapter — все 9 методов перенесены на generic input boundary.
  Сохранена xterm-compatible политика modifier resources вместо Windows
  host-specific Ctrl+Backspace/Ctrl+number; импорт исправил S8C1T для
  keyboard-generated CSI/SS3;~~
- ~~mouse — все 5 methods и 1 430 data assertions перенесены. Для legacy
  coordinates сохранены bounds xterm (223 byte, 2015 UTF-8), а не
  Windows Terminal (95 и `SHORT_MAX - 32`). Импорт добавил отсутствовавший
  horizontal alternate-scroll: wheel теперь порождает Left/Right, включая
  application cursor mode;~~
- ~~Kitty keyboard — все 4 methods перенесены: полная таблица из 129
  `INPUT_RECORD` rows и три repeat transactions. Тесты идут через generic
  `plt::InputKey` boundary; отдельный `VtKey` удалён. Импорт добавил F21–F35,
  media/volume и полную keypad семантику, исправил F3 по актуальному Kitty
  protocol, canonical omission default modifiers/press event и фильтрацию
  control-code associated text;~~
- ~~selection — все 21 methods перенесены с координатными assertions raw и
  snapped selection. Импорт исправил pivot при extension, selection wide-cell
  boundaries, rectangular highlight через wide glyph и triple-click по целой
  soft-wrapped logical line. Windows-only смена expansion mode при каждом
  Shift+click адаптирована к консенсусу Foot/Kitty: drag сохраняет исходный
  word/line mode;~~
- ~~terminal buffer — все 10 methods перенесены: базовая запись, посимвольный
  и bulk wrap, удержание viewport при выводе и полном scrollback ring, reset/
  add/clear/forward/reverse tab stops, implicit URL detection через soft wrap,
  scrollback и viewport-relative interval. Тест ёмкости использует фактический
  power-of-two history budget Shitty вместо Windows-специфичных 100 строк;~~
- ~~большая параметрическая Reflow suite — все 15 cases / 42 buffer states
  перенесены с поклеточной проверкой text, wide continuations, wrap и cursor
  после каждого resize. 34 состояния совпадают дословно; восемь
  Windows-specific fixed-buffer/cursor-wrap результатов адаптированы к
  cursor-anchored screen и pending-wrap политике Shitty/Alacritty/Ghostty;~~
- ~~`ScreenBufferTests`, initial block — первые 13 методов перенесены через
  observable terminal boundary: alternate-buffer lifetime/cursor, RI, полный
  tab-stop transition matrix, ED2 и все 24 inactive C0. Импорт нашёл и
  исправил отсутствующий DECST8C.
  Приватные Win32 pointer/viewport assertions заменены соответствующим
  протокольным контрактом;~~
- ~~`ScreenBufferTests`, resize/reset/newline/color block — ещё 12 методов:
  клеточный resize и DECCOLM, сохранение pen state, DECSTR на primary/alternate,
  LF с margins/scrollback и erase colors, OSC 4 parser matrix, DECRSTS color
  table report с HLS/RGB/omitted/clamped компонентами и RIS palette reset.
  `CSI 8;0;0t` следует xterm и подставляет размер экрана вместо Windows no-op;
  `rgbi:` сохраняется как поддерживаемая XParseColor-модель;~~
- ~~`ScreenBufferTests`, resize/erase/alternate/word block — ещё 12 методов:
  shrink lifetime, cursor style через resize, resize активного alternate,
  ED 2 cursor/erase colors, word selection и active-screen dispatch/RIS.
  Две проверки `GetWordBoundaryTrimZeros*` классифицированы как неприменимая
  Win32 host policy (`SetTrimLeadingZeros`), а не terminal protocol/selection
  consensus; punctuation и whitespace остаются отдельными selection-классами;~~
- ~~`ScreenBufferTests`, default/palette/backspace block — ещё 11 методов:
  default color sources и reset SGR, reverse, BS/DCH с default attributes,
  global и трёхзначный OSC 4 palette index, OSC 10/11 validation, VT525 DECAC.
  Win32 `WriteCharsLegacy` сохранён как whole/chunked byte-stream invariant.
  DECAC normal-text реализован с 256-color extension и RIS reset; window-frame
  item семантически разбирается, но остаётся no-op как в xterm, поскольку
  terminal window frame принадлежит host compositor;~~
- ~~`ScreenBufferTests`, DCH/scroll block — ещё 5 методов: полная матрица
  near-EOL DCH, две исходные минимальные регрессии, сохранение цветов history
  при записи в live screen и SU/SD/IL/DL/RI с тремя величинами, scrolling
  region, cursor и erase attributes. Win32 movable viewport заменён
  стандартным VT scrolling region;~~
- ~~`ScreenBufferTests`, horizontal editing block — ещё 5 методов:
  insert/replace, полные ICH/DCH matrices, DECIC/DECDC/DECFI/DECBI и
  wide-cell ICH/DCH/DECCRA. Импорт исправил различавшуюся проверку margins в
  ICH и DCH. Windows разворачивает horizontal operation на всю строку вне
  vertical margins; адаптировано к VT510 и consensus xterm/Ghostty, где
  заданные horizontal margins продолжают действовать;~~
- ~~`ScreenBufferTests`, erase/protection block — ещё 3 метода: ED3,
  полная 3×line/display×regular/selective erase matrix и DECSCA. Win32
  storage tail заменён observable history contract. Windows selective erase
  сохраняет старые colors, а malformed multi-parameter DECSCA применяет
  последний параметр; адаптировано к xterm/Ghostty erase colors и
  xterm-compatible first parameter;~~
- ~~`ScreenBufferTests`, margin scrolling block — ещё 5 методов:
  SU/SD/IL/DL/RI внутри vertical margins, без margins и внутри совместного
  vertical/horizontal rectangular region. Перенесены все 12 upstream-ветвей
  с полным grid и cursor assertions;~~
- ~~`ScreenBufferTests`, line-feed/mode/reset block — ещё 7 методов:
  IND/NEL на page edges и в rectangular margins, 3×3 IL/DL/RI erase-color
  matrix, LNM, DECSCNM, DECOM вместе с DECLRMM, DECAWM с wide glyph и RIS
  после заполнения history. Win32 movable viewport адаптирован к terminal
  screen+history, а private render-settings lookup проверяется через
  опубликованный renderer state и сохранённые cell colors;~~
- ~~`ScreenBufferTests`, alternate/extended-attributes block — ещё 5 методов:
  alternate clear с сохранением primary, все 256 комбинаций восьми extended
  attributes и все 4096 attribute×foreground×background комбинаций вместе с
  последовательными resets. Импорт исправил потерю direct RGB foreground при
  `SGR 22`. `RestoreDownAltBufferWithTerminalScrolling` и
  `SnapCursorWithTerminalScrolling` классифицированы как Win32-only
  `_virtualBottom`/movable-viewport/console-API policy; переносимые resize,
  scrollback-follow и alternate lifetime уже покрыты;~~
- ~~`ScreenBufferTests`, cursor block — ещё 11 методов: CUU/CUD/CUF/CUB
  внутри, снаружи и точно на margins, CNL/CPL, HPR/VPR, полный
  DECSC/DECRC state вместе с DECOM и сменой margins, DECALN и
  DECTCEM/cursor-blink. Импорт исправил печать справа от horizontal margins
  и сохранение относительных координат DECSC при DECOM. Windows CNL/CPL
  снаружи vertical margins адаптирован к xterm/Ghostty/WezTerm: carriage
  return сохраняет действующий left margin;~~
- ~~`ScreenBufferTests`, hyperlink/virtual-viewport block — ещё 12 методов:
  три OSC 8 lifecycle/identity/URI transaction перенесены с поклеточной
  проверкой. Девять методов `_virtualBottom`, movable viewport, horizontal
  console-buffer panning и `SetConsoleCursorPosition` классифицированы как
  Win32 host policy; terminal-side scrollback, reflow, resize и link lifetime
  уже независимо покрыты.~~
- ~~`ScreenBufferTests`, final block — последние 12 методов разобраны:
  три color-preserving reflow transaction, все шесть rectangular operations,
  DECCRA из double-width source, 36 delayed-wrap reset controls и multiline
  wrap перенесены. Импорт исправил pending-wrap у line-rendition/DECAWM и
  redundant DECCOLM, а rectangular copy теперь ограничивается физической
  шириной double-width строки. `TestDeferredMainBufferResize` проверяет
  Win32-внутреннюю отложенную оптимизацию уже покрытого observable alt resize;
  DECECM игнорируется consensus xterm/VTE/основных терминалов; три scrollbar
  mark/command-history метода являются Windows host UI поверх уже покрытого
  OSC 133 semantic protocol. Все 113 методов теперь ported или явно
  classified.~~

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
- ~~InputGenerator — all 122 cases accounted: the 59 terminal-observable
  keyboard cases are rewritten against the real `plt::InputSink` path;
  `InputBinding`, focus, wheel, and reset assertions are mapped to native and
  Python tests. The two Contour container/table ABI cases, three internal-only
  wheel policies, and 50 private ConPTY DECSET 9001 cases are explicitly
  inapplicable; Shitty has no ConPTY frontend and continues to report 9001
  unsupported.~~
- TextSizing — 59. OSC 66 support was reverted: the first implementation
  spread Kitty-specific multicell state through Parser, CellExtra, Screen,
  Vterm, Font and every renderer before the parser/grid/rendering ownership
  model had been agreed. Keeping that design would make the compact cell and
  generic rendering interfaces depend on an unfinished extension. The
  imported behavioral tests remain in `tests/test_kitty_text_sizing.py` as
  expected failures; they are the oracle for a later implementation after its
  representation and component boundaries are designed explicitly.
- ~~Grid — all 32 cases accounted: 25 terminal-observable resize, reflow,
  history, viewport, sparse-row and semantic-region cases are rewritten in
  Python; seven private storage/rendering cases are mapped to native Screen
  tests for lazy blank rows, sparse capture and partial-horizontal
  fill-attribute preservation. Infinite history is adapted to Shitty's
  explicit finite scrollback contract, and non-bottom height growth follows
  the Foot/Alacritty bottom-anchored consensus instead of Contour's blank-row
  behavior.~~
- ~~ShellIntegration protocol core — all 31 cases inventoried; OSC 133 and
  `CSI > M` SETMARK semantics are imported, including prompt/input/output
  boundaries, multi-line prompts, reversible reflow, and primary/alternate
  semantic-state isolation. Contour's private `LineFlags` formatter has no
  terminal-protocol observable.~~
- Contour ShellIntegration GUI extraction — 12 remaining cases for
  `lastCommandBlock()` and `livePromptSpan()` need an explicit Shitty
  product/test API. Their underlying semantic-cell and reflow invariants are
  already covered, but string/span extraction is not.
- Contour private semantic-block protocol — 12 mode-2034 cases cover
  authenticated DCS queries, random tokens and JSON replies. No independent
  implementation exists in the checked Foot, Alacritty, Kitty, Ghostty, VTE,
  xterm or WezTerm sources; treat as intentionally inapplicable unless Shitty
  deliberately adopts this Contour protocol.
- ~~KittyClipboard — all 19 cases accounted. OSC 5522 parsing, bounded
  multi-packet writes, MIME validation, asynchronous reads, 4096-byte read
  chunks, permission errors, sanitized multiplexing ids, TARGETS probes and
  private mode 5522 paste notifications are covered by native parser tests and
  Python protocol tests. The Contour-only refusal of `loc=primary` is replaced
  by real primary-selection reads/writes because Shitty supports that
  platform capability. Its status-line lifetime test is exercised across the
  primary/alternate screen boundary; Shitty has no DEC status-line surface.~~
- ~~RectangularAreaChecksum — все 12 test cases перенесены: базовая сумма,
  written/undrawn blanks, DEC video-attribute weights, DEC charset mapping,
  combining marks и все пять composable XTCHECKSUM flags. Импорт добавил
  XTCHECKSUM и заменил Windows-specific checksum oracle на измеренное
  xterm-406 поведение, которое также реализует Contour.~~

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

# M6. Ядро VT получает поверхность вложения, а счёт сетки остаётся у эмбеддера

**Дата:** 2026-09-04 · **Задача:** `M6`, волна 6 плана `docs/plans/2026-08-29-upstream-merge.md` · **Ветка:** `wave/upstream-merge-w6` · **Вердикт:** `DONE, дерево зелёное`

Шестой мерж-шаг и самый крупный: 45 файлов у апстрима, 22 конфликтующих файла, **196 конфликтных хунков** (в `vterm.cpp` — 91). Апстримная правка при этом почти целиком механическая: её `-U0`-дифф по `vterm.cpp` — это 199 парных подстановок строк плюс шесть включений, и ни одного содержательного хунка сверх шести именованных мест.

**Три решения, которые этот мерж принял сознательно и которые отличают дерево от дословного взятия апстрима:** `resize()`, `borderPixels()`/`scaledPixels()` остаются у `Composer`; `Vterm::create()` и `VtermHeadless::create()` продолжают брать `Composer&`; `CellExtraClient` становится наследником `Listener`. Каждое разобрано в §3, каждое измерено, а не обосновано словами.

---

## 1. Коммиты

| Коммит | Что |
|---|---|
| `fe2bf2dd` | `merge wave 6 step M6: the VT core gets its embedding surface, and the grid stays behind` — сам мерж, родители `3e7a31a9` + `25dbda61` |
| `6a24cc39` | `The window's grid is spelled composer.vt.columns now, and the guard could not see it` — словарь `pane_grid_names` в `build.py` |
| `5395ce14` | `includes: let style.py order the eight blocks upstream rewrote in this merge` |

Предмержевая голова — `3e7a31a9`. Слит один апстримный коммит:

```
25dbda61 Give the VT core its own embedding surface
```

Следующие коммиты диапазона (`f3de9de6`, `bd86ed38`, `9bf5e497`, `7e2a8e3e`) **не тянулись**: это отдельные шаги `M6b`–`M6e` по решению `Р1` плана.

---

## 2. Что принёс апстрим

`VtState` в `lib/vterm/vt_state.{h,cpp}` — «всё, что машина состояний читает вокруг себя, и геометрия сетки, которую она отдаёт обратно»: `config`, `pool`, `cellExtras`, `smallObjects`, `platform`, `window`, `brandName`, восемь полей геометрии и пять списков слушателей (`resized`, `fontChanged`, `cellExtrasChanged`, `titleChanged`, `configChanged`). `Composer` **встраивает** его полем `vt` и превращается в то, чем он и был вокруг ядра: шрифты, рендереры, шейпер, привязки ввода, бренд, сессии.

Сверх переезда полей:

| Что | Где |
|---|---|
| `Composer::setOptions(const Options*)` — единственная точка публикации снимка: держит `opts`, `vt.config` и `vt.baseBorder` в шаге | `composer.{h,cpp}` |
| `AnsiPalette` уезжает из `Options` в `VtConfig::palette` — это семантика терминала, её читают и пишут OSC | `options.h`, `vt_config.h`, `options.cpp` |
| `lib/shitty/ansi_palette.{h,cpp,_ut.cpp}` → `lib/vterm/` | переименование, конфликта нет |
| `Vterm`, `Screen`, `VtermHeadless`, `CellExtraStore` принимают `VtState` вместо `Composer` | шесть файлов |
| Снятие мёртвых включений (`options.h`, `brand.h`, `fonts` в `screen.cpp`, `application.h` в `vterm.cpp`) | «мертвы со времён раскола шейпера» |

Число узлов цели `st`: **225 → 226**. Ровно `+1` — добавился `vt_state.cpp`; `ansi_palette.cpp` переехал внутри одного и того же глоба (`build.py:765` собирает `lib/shitty/*.cpp` и `lib/vterm/*.cpp` в одну библиотеку), поэтому переезд узел не создал и не убрал.

---

## 3. Конфликты: как разрешён каждый

### 3.0. Метод

Апстримный дифф по каждому конфликтному файлу разбирался на **пары строк** (`-`/`+`) и проверялся на объяснимость фиксированным набором подстановок. Для `vterm.cpp`: 199 пар из 199 объяснились семью правилами (`composer.opts->vt.` → `vt.config->`, `terminal->composer` → `terminal->vt`, `Composer& composer` → `VtState& vt`, `composer.` → `vt.` и три мельче) плюс **шесть** строк, где апстрим менял смысл, а не имя. Поэтому разрешение строилось не из маркеров, а так: взять **нашу** сторону файла, применить ту же подстановку, затем руками внести шесть содержательных мест. Тот же приём — для остальных 21 файла, с набором правил «имя, уехавшее в `VtState`, получает `.vt.` перед собой».

Правильность проверена в обе стороны, машинно, и результат — §4.3.

### 3.1. `resize()` — **наша сторона**, и это измеренное решение, а не осторожность

**Конфликт.** Апстрим переносит `Composer::resize()` в `VtState::resize()`. Его тело:

```cpp
const u32 borders = 2u * borderPixels();
const u32 contentWidth = pixelWidth_ > borders ? pixelWidth_ - borders : 0;
```

Наше тело считает сетку из `contentInsets()` — это граница **плюс то, что хром резервирует на каждой из четырёх сторон** (`A1`, `A10`), и печатает трассу `F4`/`Q2` через `brand->identifierCString()`.

**Взята наша.** `VtState::resize()` из апстримного файла **удалён** — не оставлен рядом, потому что две функции, считающие одну сетку по-разному, это второй источник правды в чистом виде. `Composer::resize()` коммитит те же четыре поля (`vt.columns`, `vt.rows`, `vt.pixelWidth`, `vt.pixelHeight`) и обходит `vt.resizedListeners`, то есть снаружи ничего не изменилось.

**Почему это не вкусовщина — подстановка.** Взятие апстримной формулы означает «сетка считается из симметричной границы, резервы хрома забыты». Проба (`insets = paneInsets()` вместо `contentInsets()` — ровно то, что делает апстримное тело, без искажений вроде деления на ноль):

```
OK: 940, ERR: 10
- Composer::EveryGridChangePrintsAndNothingElseDoes
- Composer::EveryLegalReserveIsWorthItsOwnWidthInPixels
- Composer::EveryPixelOfAComposersContentBoxAnswersFromItsOwnSide
- Composer::PointerStopsAtTheSidebarsEdgeNotTheWindows
- Composer::ResizeGivesTheSidebarItsColumnsBackWhenItIsHidden
- Composer::ResizeTakesTheStripOutOfTheRowsAndLeavesTheColumnsAlone
- CsdTabsUi::TheChordHidesTheTabsInsteadOfMovingThemToTheOtherEdge
- SidebarTabsUi::AReloadThatDropsTheOptionDropsTheReserve
- SidebarTabsUi::CmdBGivesTheColumnsBackAndTakesThemAgain
- SidebarTabsUi::ReservesItsWidthBeforeTheGridIsFirstCounted
```

Десять красных, все — про сайдбар и cmd+b. То есть здесь дословное взятие апстрима **ловится тестами**, в отличие от мины `M5`. Но ловится только потому, что волна 4 эти тесты написала; гварды на это молчат.

### 3.2. `borderPixels()` / `scaledPixels()` — **наша сторона**, и первое разрешение было неверным

**Конфликт.** Апстрим кладёт `VtState::borderPixels()` в `lib/vterm`.

**Первое разрешение (отброшено):** взять апстримное место, поправив только потолок насыщения `3000` → `30000` (наша правка `R4-qa, Q1`) и добавив `VtState::scaledPixels()`, чтобы правило округления не размножилось. Собралось, тесты зелёные — и `border_pixels_guard`, запущенный напрямую, **покраснел**:

```
borderPixels()/scaledPixels() are the border option and its scale, not the layout (A1): contentInsets() is what layout reads.
Unallowed uses:
  lib/vterm/vt_state.cpp:43
  lib/vterm/vt_state.cpp:54
  lib/vterm/vt_state.cpp:55
  lib/vterm/vt_state.h:47
  lib/vterm/vt_state.h:48
```

Это ровно ловушка, названная планом заранее (`T5.1`, «Побочная ловушка»): «метод `borderPixels()` в ядре покраснеет в `lib/vterm`, и появится соблазн расширить разрешение — **запрещённый ход** по `T2.1`». Разрешение не расширялось.

**Итоговое разрешение — взята наша сторона.** `VtState` несёт `baseBorder` и `contentScale` **полями**; ни `borderPixels()`, ни `scaledPixels()` в ядре нет. `Composer::scaledPixels()` — наша реализация с потолком 30000, `Composer::borderPixels()` = `scaledPixels(vt.baseBorder)`. Гвард зелёный без единой правки разрешения, и `A1` остаётся там, где его поставила волна 4.

Проверено, что ядру это сегодня и не нужно: единственным читателем `borderPixels()` внутри `lib/vterm` был `VtState::resize()`, которого по §3.1 там нет.

**Почему `vt.baseBorder`, а не `opts->border`.** Апстримный контракт — «`Options` меняется только через `setOptions()`». Чтение опубликованного поля, а не снимка, делает `setOptions()` единственным местом, где эти двое могут разойтись.

### 3.3. `CellExtraClient` — **обе стороны**, и это мина класса `M5`, пришедшая с другой стороны

**Конфликт.** Апстримный `VtState::setCellExtras()` обходит `cellExtrasChangedListeners`, приводя узлы к `Listener*` и вызывая `onListen()`. У нас в этом списке лежат `CellExtraClient` — отдельный базовый тип с `collectExtras()` и `extrasCollected()`, заведённый нашим `R7`.

Обе базы — `stl::IntrusiveNode`. Значит `static_cast` в **любую** сторону компилируется, линкуется и попадает в чужой vtable-слот. На `M5` эта мина была направлена в одну сторону (апстрим клал `Listener`, наш код кастовал к `CellExtraClient`); здесь она направлена в обратную.

**Взяты обе.** `struct CellExtraClient: Listener` с `void onListen(void*) override { extrasCollected(); }`. После этого:

- апстримный обход как `Listener*` **истинен по типу**, а не по удаче — `CellExtraClient` *является* `Listener`;
- наш второй обход того же списка (в `CellExtraStoreImpl::collect()`, приведение к `CellExtraClient*` ради `collectExtras()`) продолжает опираться на инвариант «в этом списке только клиенты» — это тот же инвариант, на который наш код опирался и до мержа, и он не ослаб;
- `~CellExtraClient()` снят: `Listener` отвязывает узел сам, а это ровно то, что комментарий типа и предписывал.

Собственный `Composer::setCellExtras()` удалён — взято апстримное место в `VtState`, и `cell_extra_store.cpp` целиком лёг на апстримный текст плюс наш блок `R7` (см. §5, файл идёт «побайтово апстримный плюс наша вставка»).

### 3.4. `Vterm::create()` и `VtermHeadless::create()` — **наша сторона**, с именованным остатком

**Конфликт.** Апстрим меняет сигнатуру на `VtState&`: ядро больше не называет GUI-половину.

**Взята наша** (`Composer&` плюс наш параметр `const PaneGeometry&`). Причина измерена, а не предположена: после механической подстановки в `vterm.cpp` осталось **четыре** места, которым нужен эмбеддер, и ни одно не выражается через `VtState`:

| Место | Что читает |
|---|---|
| `columnsForPixelWidth()`, `rowsForPixelHeight()`, XTWINOPS `CSI 8` | `composer.contentInsets()` — граница плюс резервы хрома |
| `updateExtraCellCount()` | `composer.sessions->cellCapacityExcept(this)` |
| XTWINOPS «изменить размер окна» | `composer.resize()` (см. §3.1) |
| `mouseCell()`, `mouseSelectionCell()` | `mouseGeometry(composer, …)` — наша перегрузка |

`VtermImpl` теперь несёт **оба**: `Composer& composer` и `VtState& vt` (алиас на `composer.vt`, не копия). Это позволило принять апстримные 199 переименованных строк дословно, а остаток свести к четырём местам, перечисленным в комментарии у поля — их можно пересчитать одним `grep`, и они и есть входной материал `T5.1`.

`VtermHeadless::create()` осталась на `Composer&` по той же причине: она считает безголовую сетку через `contentInsets()` и строит `PaneGeometry` через `windowPane(composer)`. Заголовок `vterm_headless.h` git слил на апстримную сторону автоматически — возвращён вручную, это тот случай, когда автослияние прошло молча и неверно.

Внутренние структуры `OutputPtyHandle` и `VtermHeadlessImpl` при этом **взяты апстримные** (`VtState& state`): им хватает `state.platform`.

### 3.5. `options.h` — **апстримная сторона**, как предписано планом

`Options::palette` снят, палитра живёт в `VtConfig::palette`. Наши два поля рядом (`paneDividerColor`, `sidebarColor`) сохранены. Два читателя починены: `options.cpp:1118` (`paneDividerColor = vt.palette[8]`) и `options_ut.cpp` (5 обращений). Ни один из них не был конфликтным файлом — их нашла сборка.

### 3.6. `composer.h` / `composer.cpp` — обе стороны

Взято апстримное: поле `VtState vt`, `setOptions()`, снятие уехавших полей и методов, `setContentScale()` через `vt.contentScale`, `CellExtraStore::create(vt, 0)` в конструкторе.

Взято наше: `Insets`, `ChromeSide`, `PixelRect`, `PaneUpdate`, `scaledPixels`/`borderPixels`/`chromeInsets`/`paneInsets`/`contentInsets`/`chromeReserve`/`setChromeReserve`/`resize`, поле `chromeReserves[]`, три списка слушателей (`splitVertical`, `splitHorizontal`, `toggleSidebar`) и их регистрация в конструкторе.

### 3.7. `vterm_headless_ut.cpp` — трёхсторонний мерж на переименованных сторонах

Апстрим переписал здесь **корпусы двух фаззинг-регрессий** (+434 строки, почти целиком байты) — содержательная правка, теряться она не должна. Наш форк добавил в тот же файл +1142 строки панельных тестов. Прямой мерж дал бы месиво.

Сделано так: подстановка применена к **базе** и к **нашей** стороне, после чего `git merge-file` свёл `renamed(base)` / `renamed(ours)` / `theirs`. Проверка, что подстановка точна: `diff renamed(base) theirs` даёт ровно апстримные содержательные правки — переупорядочение включений и два корпуса, и ничего больше. Конфликтов осталось два, оба в блоке включений, оба разрешены в нашу пользу (наш набор — надмножество).

### 3.8. Остальные 15 конфликтных файлов

Механическая подстановка, наша сторона там, где наш форк переписал участок (`A1`-инсеты в `application.cpp` и `main_fuzz`, панельный кадр в `render_*`), апстримная — везде, где наш форк участок не трогал. Результат сверен машинно, §4.3.

### 3.9. Работа сверх конфликтов, без которой дерево не собирается

Мерж затронул 22 файла, но переезд полей `Composer` ломает всех читателей. Механическая подстановка применена ещё к **11 файлам вне мержа**: `pane_layout.cpp`, `mouse_frontend.{cpp,_ut.cpp}`, `font_freetype.cpp`, `font_coretext.cpp`, `render.h`, `ui_quick_hotkey.{h,mm}`, `ui_csd_tabs_ut.cpp`, `ui_sidebar_tabs.mm`, `ui_sidebar_tabs_ut.cpp`, плюс `options_ut.cpp` и `screen_ut.cpp`, которые git слил без конфликта и оставил недоехавшими.

Отдельно: **все 18 присваиваний `X.opts = Y` заменены на `X.setOptions(Y)`**, включая девять тестовых, которых апстрим не касался. Это не косметика: `vt.baseBorder` — копия `opts->border`, и прямая запись оставляет её протухшей. Найдено не рассуждением, а прогоном: первый прогон юнит-тестов после мержа дал `OK: 928, ERR: 22`, и 21 из 22 отказов лечился этой заменой.

Двадцать второй — `Composer::ChromeReservesAreZeroUntilSomethingClaimsASide`: тест правил `options.border` **в уже опубликованном снимке**. Переписан на публикацию второго снимка, что и делает перезагрузка конфига. Это единственное изменение смысла теста в этом мерже, и оно взято из апстримного контракта дословно.

---

## 4. Таблица критериев

| № | Критерий | Результат |
|---|---|---|
| 1 | `./build st --clear` зелёная | **226 узлов**, `EXIT=0` |
| 2 | `./build unit_tests pty_test_helper` + прогон | `EXIT=0`, **`OK: 950`** |
| 3 | Совпадение диффов | 71 + 43 расхождения, **все три — именованные решения §3**, четвёртой причины нет |
| 4 | Питоновский набор поимённо | `Ran=6399`, красные совпадают с предмержевой головой |
| 5 | Пять гвардов | все `EXIT=0`, запуск напрямую; три покраснели на пробах |
| 6 | `A1`, `A8`, `A9`, `A10` | не нарушены, §4.6 |

### 4.1. `./build st --clear`

```
$ ./build st --clear -j 10
EXIT=0
[CC] {223/226} $(B)/obj/libshitty/lib/shitty/font_embedded.cpp.o
[CC] {224/226} $(B)/obj/libshitty/lib/shitty/parser.cpp.o
[AR] {225/226} $(B)/libshitty_prod.a
[LD] {226/226} $(B)/st
```

Эталон до мержа, снятый на `3e7a31a9` в этом же дереве: `[LD] {225/225} $(B)/st`.

**Свежесть бинарника, не через систему сборки.** Строка, которой до мержа не было и быть не могло:

```
$ strings -a .build/st | grep -c "lib/vterm/vt_state.cpp"   → 1
```

**И то, что в бинарник попало именно моё разрешение, а не апстримный текст** — пара символов, различающая их:

```
$ nm -a .build/st | grep -E "8Composer6resizeEtt|8Composer12scaledPixelsEt|15CellExtraClient8onListenEPv|7VtState13setCellExtras"
0000000100003398 T __ZN15CellExtraClient8onListenEPv
00000001000a74c0 T __ZN7VtState13setCellExtrasEP14CellExtraStore
000000010000540c T __ZN8Composer10setOptionsEPK7Options
00000001000058fc T __ZN8Composer6resizeEtt
0000000100005554 T __ZNK8Composer12scaledPixelsEt

$ nm -a .build/st | grep -E "7VtState6resizeEtt|7VtState13borderPixelsEv"
(пусто)
```

`Composer::resize` и `Composer::scaledPixels` присутствуют, `VtState::resize` и `VtState::borderPixels` отсутствуют — это и есть §3.1 и §3.2, отпечатавшиеся в бинарнике. `CellExtraClient::onListen` — §3.3. `Composer::setOptions` — апстримная половина.

### 4.2. Юнит-тесты

```
$ ./build unit_tests pty_test_helper -j 10 ;  EXIT=0
$ SHITTY_PTY_TEST_HELPER="$PWD/.build/pty_test_helper" ./.build/unit_tests --threads=1
EXIT=0
OK: 950
```

Имена сверены поимённо с предмержевой головой: **950 и 950, `diff` пуст**. Ни один тест не появился и не исчез — что важно, потому что 22 отказа первого прогона (§3.9) чинились правкой тестового кода, и «зелёное за счёт выброшенного теста» здесь исключено списком.

### 4.3. Совпадение диффов

Сравнение построчное и машинное, в обе стороны. `A` — строки, которые апстрим **удалил**, а в смерженном дереве они остались (то есть апстримная правка не применена). `B` — строки, которые апстрим **добавил** взамен строки, ещё присутствовавшей в нашей предмержевой голове (то есть замена не выполнена там, где выполнять было что).

```
A. upstream-removed lines still in the merged tree, by cause:
    30  create() keeps the Composer (T5.1/T5.9 residue)
    23  OTHER
    14  resize() stays on Composer
     4  borderPixels()/scaledPixels() stay on Composer
   TOTAL 71
B. upstream-added lines not applied where our tree still had the base line, by cause:
    25  create() keeps the Composer (T5.1/T5.9 residue)
    12  resize() stays on Composer
     3  borderPixels()/scaledPixels() stay on Composer
     3  OTHER
   TOTAL 43
```

`A OTHER` — 23 строки вида `}`, `return;`, `node = node->next;` из `composer.cpp`: это тела `Composer::setCellExtras()` и `Composer::resize()`, которые апстрим удалил, а их закрывающие скобки совпали с такими же скобками в оставшихся функциях того же файла. Артефакт построчного сравнения, не расхождение. `B OTHER` — три строки, все из §3.4 (список инициализации `VtermImpl` и `struct VtState;` в `vterm_headless.h`).

**Четвёртой причины нет.** Всё, чем дерево отличается от дословного взятия апстрима, — три решения §3.1, §3.2, §3.4.

В обратную сторону: каждый апстримный `-`-хунк, не попавший в эти три категории, применён; каждый апстримный `+`-хунк, чья база у нас ещё была, применён. `cell_extra_store.cpp` — показательный: его дифф к апстримному файлу состоит **ровно** из нашего блока `R7` и ничего больше.

### 4.4. Питоновский набор

Собран и прогнан на самой предмержевой голове (`3e7a31a9`), в этом же дереве, до мержа: свои `st_test`/`pt_test`/`toml_dump`, 20 групп с полным окружением из `build.py`.

```
before (3e7a31a9): Ran=6399 failures=6 errors=15 skipped=17 expected_failures=549   16 именованных красных
after  (5395ce14): Ran=6399 failures=6 errors=15 skipped=17 expected_failures=549   16 именованных красных
```

Число тестов — эталонные 6399 (6390 уникальных, девять `FontResolverTest` исполняются дважды, `G14`).

**Одно расхождение в именах, и оно разобрано до конца.** Набор прогонялся после мержа четыре раза. Три прогона дали список, побайтово совпадающий с предмержевым. Один прогон — вместо `test_pty.PtyTest.test_child_exit_report_includes_output_flushed_at_exit` показал `test_term_features.TermFeaturesTest.test_children_get_term_features_in_the_environment`, при тех же 6 `failures` и 15 `errors`.

Это не регрессия, и это показано, а не заявлено:

- обе — тесты, порождающие процесс через pty; текст отказа один и тот же, из `tst/harness.py:298` — `RuntimeError: test child tty has no path`, то есть pty не выделился;
- каждая в одиночку на смерженном дереве проходит **12 из 12**;
- отказ **воспроизведён под нагрузкой на смерженном дереве**: 24 параллельные копии `test_children_get_term_features_in_the_environment` дали 1 отказ из 24 с тем же `RuntimeError`.

То есть набор из 20 групп, идущих параллельно на одной машине, изредка исчерпывает pty, и какой именно из тестов-«порождателей» это заденет — лотерея прогона. Новых отказов ноль; в двух прогонах из четырёх красных было на один **меньше**, чем до мержа.

### 4.5. Гварды

Запуск **напрямую**, минуя `./build`: программы четырёх сканирующих гвардов извлечены из `build.py` разбором AST (вместе с их словарями и разрешениями), `vterm_boundary` — это отдельный скрипт.

```
border_pixels_guard   EXIT=0   output bytes=0
mouse_geometry_guard  EXIT=0   output bytes=0
pane_grid_guard       EXIT=0   output bytes=0
darwin_call_guard     EXIT=0   output bytes=0
vterm_boundary        EXIT=0   output bytes=0
```

**Зелёное без пробы ничего не значит**, поэтому каждый гвард проверен подстановкой нарушения:

| Гвард | Проба | Результат |
|---|---|---|
| `border_pixels` | `c.borderPixels()` в `pane_layout.cpp` | `EXIT=1`, назвал файл и строку |
| `mouse_geometry` | `mouseGeometry(c)` в `pane_layout.cpp` | `EXIT=1` |
| `pane_grid` | `composer.columns` в `render_reference.cpp` | `EXIT=1` |
| `pane_grid` | `composer.vt.columns` там же — **до** правки словаря | **`EXIT=0`, пустой отчёт** |
| `darwin_call` | незащищённый `createMetalRenderer(composer)` | `EXIT=1` |
| `vterm_boundary` | `#include <lib/shitty/composer.h>` в `vt_state.h` | `EXIT=1` |

Четвёртая строка — находка, и ей посвящён §5.1.

### 4.6. Инварианты

| Инвариант | Чем проверен |
|---|---|
| `A1` — `contentInsets()` единственный источник геометрии для раскладки | `border_pixels_guard` зелёный **без правки разрешения** (§3.2); `Composer::resize()` по-прежнему считает сетку из `contentInsets()`; зелены `Composer::ResizeCountsTheGridOutOfTheContentInsets`, `ContentInsetsCarryTheBorderOnEverySide`, `ContentInsetsReserveTheSidebarOnTheRightInBackingPixels`, `ContentInsetsReserveTheTitleBarStripOnTopInBackingPixels`, `ReferenceRenderer::PlacesTheGridAtTheContentInsets`, `PlacesTheGridAtInsetsThatDifferOnEveryAxis`. Подстановка §3.1 роняет десять из них |
| `A8` — геометрия панели передаётся, а не читается из окна | `mouse_geometry_guard` зелёный; `Vterm::create()` по-прежнему принимает `PaneGeometry` (§3.4); зелены `VtermHeadless::TakesItsGridFromThePaneItWasGiven`, `PointerReportsCountFromTheOriginTheVtermWasGiven`, `MovingThePaneMovesWhereItsPointerReportsCountFrom`, `SelectionStartsInTheCellThePaneOwnsAndNotTheWindows`, `AutoscrollMeasuresFromThePanesTopEdgeAndNotTheWindows` |
| `A9` — рендерер берёт сетку из `TerminalUpdate`, не из окна | `pane_grid_guard` зелёный **после** того, как научен новому написанию (§5.1), и краснеет на пробе в обоих написаниях; зелены `ReferenceRenderer::AFrameWithoutAGridIsRefused`, `DrawsTwoPanesOfDifferentGrids`, `MetalPanes::AZeroGridInOnePaneRefusesTheWholeFrame` |
| `A10` — `chromeInsets` и `paneInsets` названы по отдельности, `contentInsets` их композиция | все три метода на месте и не размножились; зелены `Composer::EachChromeSideKeepsItsOwnReserveOnItsOwnEdge`, `EveryPixelOfAComposersContentBoxAnswersFromItsOwnSide`, `SessionSet::PanesDivideTheContentBoxAndNotTheWindow` |

Дополнительно, вне списка: `render_vk.cpp` и `font_freetype.cpp` на macOS не компилируются штатной сборкой, а мерж их изменил. Проверены отдельно:

```
c++ -std=c++26 -O2 -DHAVE_VULKAN_WAYLAND=1 -I. -Ilib/shitty -Iext/libstd -Iext \
    -I<render_spv.h> -I<Vulkan-Headers>/include -c lib/shitty/render_vk.cpp
VK_EXIT=0        # без предупреждений

c++ -std=c++26 -O2 -DHAVE_FREETYPE=1 -DHAVE_HARFBUZZ=1 … -fsyntax-only lib/shitty/font_freetype.cpp
FT_EXIT=0
```

---

## 5. Обнаружено

### 5.1. `pane_grid_guard` ослеп на этом мерже, и это чинилось словарём, а не разрешением

`T2.1` научил гвард ожидаемому написанию перехода: план читал апстрим как «`columns`/`rows` уезжают в `VtGeometry`», и в `pane_grid_names` попали `composer.geometry.columns`, `composer_.geometry.rows` и ещё два. Коммит, который переход действительно сделал, назвал поверхность `VtState`, и сетка окна теперь пишется `composer.vt.columns` — написание, которого в списке из восьми имён нет.

Измерено, а не предположено (таблица §4.5): проба `composer.vt.columns` в `render_reference.cpp` до правки даёт `rc=0` и пустой отчёт, та же проба как `composer.columns` — краснеет. Это `A9` без охраны за зелёным гвардом, то есть в точности тот отказ, ради закрытия которого писалась `T2.1`, — только через написание, которого ещё никто не видел.

Правка (`6a24cc39`): четыре имени **добавлены**, ни одного не убрано. Гвард видит строго больше, чем видел; ни одно разрешение не расширено; `geometry.*` оставлены, потому что туда идёт `bd86ed38`.

Это тот пункт, который `docs/plans/reviews/upstream-merge-invariants.md` §3.3 записал как обязательный для `T2.1`/`T6.1` («без него `T2.1` закроется зелёной пустотой»), и он не был выполнен на волне 3 просто потому, что угаданное написание оказалось не тем.

### 5.2. Мина `M5` существует в обе стороны, и здесь она была направлена в нас

На `M5` апстрим клал в слушателей `Listener`, а наш код кастовал к `CellExtraClient`. Здесь ровно наоборот. Общая форма: **два базовых типа, оба наследники `stl::IntrusiveNode`, один список**. `static_cast` в любую сторону компилируется, линкуется, вызывает чужой слот, и ни один из пяти гвардов такого не видит.

Разница с `M5` в том, что тут её можно было закрыть, а не обойти: `CellExtraClient: Listener` делает апстримный обход истинным по типу. После этой правки список `cellExtrasChangedListeners` можно обходить как `Listener` **всегда**, независимо от того, кто в него что положит.

Остаётся один необеспеченный инвариант: второй обход того же списка (в `collect()`, приведение к `CellExtraClient*`) требует, чтобы в списке были только клиенты. Он существовал и до мержа, и мерж его не ухудшил, но он ничем не проверяется — заметка для `T5.3`.

### 5.3. `vt.baseBorder` — второй источник правды, заведённый апстримом

`Composer::opts->border` и `VtState::baseBorder` — одно значение в двух местах, синхронизируемых только через `setOptions()`. В продуктовом коде это безопасно: `Composer::opts` объявлен `const Options*`, снимок неизменяем. В тестах — нет: девять мест писали `composer.opts = &options` напрямую, и все девять оставляли `baseBorder` протухшим. Все переведены на `setOptions()`; один тест, правивший опубликованный снимок, переписан на публикацию второго.

Заметка на будущее: пока `border` живёт в `Options`, а не в `VtConfig`, каждая новая точка публикации опций обязана идти через `setOptions()`, и ничто, кроме code review, этого не требует.

### 5.4. Автослияние git прошло молча и неверно в двух файлах

`vterm_headless.h` git слил на апстримную сторону целиком (`Composer&` → `VtState&`), потому что наш форк его не трогал, — и это ломало наш `VtermHeadless::create()`, который считает безголовую сетку через `contentInsets()`. Аналогично `lib/shitty/main.cpp` получил апстримный вызов `VtermHeadless::create(composer.vt, …)`. Оба поймала сборка, но оба — пример того, что **список конфликтных файлов не совпадает со списком файлов, которые мерж решает**.

### 5.5. Апстримный дифф этого коммита механичен на 97%

199 из 205 изменённых строк `vterm.cpp` — семь текстовых подстановок. Это делает разрешение проверяемым машинно (§3.0, §4.3), но и означает, что обычный `git merge` производит 196 конфликтных хунков там, где содержательных решений шесть. Приём «подстановка на нашу сторону, затем шесть мест руками» стоит записать в метод: он превращает 91 хунк `vterm.cpp` в один просмотр списка остатков.

### 5.6. Питоновский набор на этой машине имеет флак-класс, не связанный с мержем

Тесты, порождающие процесс через pty, под 20-групповой параллельной нагрузкой изредка получают `RuntimeError: test child tty has no path`. Воспроизведено на смерженном дереве 24-кратным параллельным запуском одного теста: 1 отказ из 24. Это объясняет, почему списки красных между прогонами гуляют на одно имя, и стоит того, чтобы попасть в брифы: сравнение списков «поимённо» надо делать не по одному прогону.

---

## 6. Что осталось задачам `T5.1`–`T5.11` — оценено после мержа

Оценка снята **по смерженному дереву**, а не по таблице плана.

### 6.1. Главное: ни одна из одиннадцати задач не может начаться на `M6`

Каждая из них владеет файлом, которого после `25dbda61` ещё нет. Множество снято командой, не из таблицы:

```
$ git ls-tree -r --name-only <commit> lib/vterm | grep -E "pty.h|vterm.cpp|cell_extra|vt_geometry|vt_headless|mouse_frontend"

25dbda61  (M6)   — ничего из перечисленного
f3de9de6  (M6b)  cell_extra_store.{h,cpp}  mouse_frontend.{h,cpp}  pty.h  vterm.cpp
bd86ed38  (M6c)  + vt_geometry.{h,cpp}
7e2a8e3e  (M6e)  + vt_headless.{h,cpp}
```

Следствие для командира — **в плане две неверные зависимости**:

| Задача | В плане | На самом деле |
|---|---|---|
| `T5.7` (`childPid()` в `lib/vterm/pty.h`) | зависит от `M6` | `lib/vterm/pty.h` появляется в `M6b` |
| `T5.9` (взять `vt_headless` целиком) | зависит от `M6` | `vt_headless.*` появляются в `M6e` |

Обе были помечены планом как «можно начинать сразу после `M6`». Начать их сейчас нельзя.

### 6.2. Что `M6` уже сделал за задачи волны

| Задача | Было по плану | Стало после `M6` |
|---|---|---|
| `T5.3` `CellExtraClient` на список ядра | перевести клиента на `VtCellExtras::changedListeners`, сохранить дедуп корней и `cellCapacity()` | **Половина сделана.** Список уже `VtState::cellExtrasChangedListeners`; `CellExtraStore::create()` уже принимает `VtState&`; `CellExtraClient` уже `Listener`, то есть тип, ради которого задача и заводилась, согласован. Дедуп корней и `cellCapacity()` сохранены и зелены. Осталось: переезд файлов в `lib/vterm` на `M6b` и переименование `VtState` → `VtCellExtras` на `M6c` |
| `T5.8` сосуществование нашего `A10` с апстримными `setOptions()`/`installVtHost()`/`debugFd` | три апстримных приобретения против нашей пятёрки методов | **Треть сделана.** `setOptions()` принят и разведён с нашим `A10`: `chromeInsets`/`paneInsets`/`contentInsets`/`setChromeReserve`/`scaledPixels` живы, все 18 писателей `opts` переведены. Осталось `installVtHost()`/`debugFd` — они приходят позже `M6` |
| `T5.11` новая сигнатура в `main_fuzz` | переписать под новый API | **Половина сделана**: файл уже переведён на `composer->vt.*` и на наши `contentInsets()`/`gridPixelWidth`. Осталась только смена сигнатуры `VtermHeadless::create`, которая приходит с `M6e` |
| `T5.1` origin и четырёхсторонние инсеты | проектное решение + `VtGeometry` | **Не изменилась по объёму, но получила точный входной материал.** Четыре места в `vterm.cpp`, которым нужен эмбеддер, теперь перечислены поимённо у поля `VtermImpl::composer` (§3.4) — это и есть то, чему `T5.1` должна найти место. Плюс §3.2 показал, что ловушка `border_pixels_guard` реальна: она сработала уже на `M6`, на один коммит раньше, чем ожидал план |
| `T5.2` расщепить `grid_geometry.h` | арифметика в `lib/vterm`, обёртки в `lib/shitty` | без изменений; `grid_geometry.h` мерж не тронул |
| `T5.4` `Vterm::create` с `VtGeometry` на панель | + предупреждение про `A5` и `refocus()` | без изменений. Апстримный дифф `session.cpp` в этом коммите чисто механический (26 строк на 26), воспроизведения состояния окна при активации он не приносил — предупреждение `A5` остаётся в силе для `M6b`–`M6c` |
| `T5.5`, `T5.6` | 65 хунков + апстримные приобретения в `lib/vterm/vterm.cpp` | без изменений; файла ещё нет |
| `T5.10` перенести +858 строк панельных тестов | на новый API | без изменений, но приём из §3.7 (`merge-file` на переименованных сторонах) применим и там |

### 6.3. Одной строкой

Из одиннадцати задач волны **три сократились** (`T5.3` наполовину, `T5.8` на треть, `T5.11` наполовину), **у двух исправлена зависимость** (`T5.7` → `M6b`, `T5.9` → `M6e`), остальные не изменились; **начинать сейчас нельзя ни одну** — все ждут `M6b` и позже.

---

## 7. Воспроизведение

```sh
cd <worktree на wave/upstream-merge-w6>

./build st --clear -j 10                       # 226 узлов
./build unit_tests pty_test_helper -j 10
SHITTY_PTY_TEST_HELPER="$PWD/.build/pty_test_helper" ./.build/unit_tests --threads=1   # OK: 950

# гварды — напрямую, минуя ./build: программы извлекаются из build.py разбором AST
#   (border_pixels/mouse_geometry/pane_grid/darwin_call — строки *_guard_program)
python3 lib/vterm/check_includes.py lib/vterm /tmp/vb.stamp

# питоновский набор, полное окружение из build.py
./build st_test pt_test toml_dump -j 10
for g in $(seq 0 19); do
  SHITTY_TEST_BINARY="$PWD/.build/st_test" \
  SHITTY_PRETTY_TEST_BINARY="$PWD/.build/pt_test" \
  SHITTY_TOML_DUMP_BINARY="$PWD/.build/toml_dump" \
  SHITTY_TEST_FONTCONFIG=0 SHITTY_TEST_PLATFORM=cocoa \
  SHITTY_TEST_VERSION="$(python3 -c 'from datetime import date; print(date.today().strftime("%Y.%m.%d"))')" \
  python3 tst/run_unittest_group.py --group=$g --group-count=20 &
done; wait

# render_vk.cpp — компилируется локально, но не линкуется
git clone --depth 1 https://github.com/KhronosGroup/Vulkan-Headers <vkh>
./build render_spv && cp -L .build/render_spv.h <inc>/
c++ -std=c++26 -O2 -DHAVE_VULKAN_WAYLAND=1 -I. -Ilib/shitty -Iext/libstd -Iext \
    -I<inc> -I<vkh>/include -c lib/shitty/render_vk.cpp -o /dev/null
```

Сравнение диффов §4.3 — сопоставление множеств `+`/`-` строк по каждому файлу между `git diff 3e7a31a9 HEAD` и `git diff 25dbda61~1 25dbda61`, с разбором каждого расхождения по причине; ожидаемый результат — таблица §4.3 без строки `OTHER`, кроме артефактов скобок.

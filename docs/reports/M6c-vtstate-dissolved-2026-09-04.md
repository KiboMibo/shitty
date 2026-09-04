# M6c. `VtState` растворяется, и ядро получает семь имён вместо одного

**Дата:** 2026-09-04 · **Задача:** `M6c`, волна 6 плана `docs/plans/2026-08-29-upstream-merge.md` · **Ветка:** `wave/upstream-merge-w6c` · **Коммит мержа:** `49864f1a`, родители `eb016aec` + `bd86ed38` · **Вердикт:** `DONE с одним унаследованным красным` — сборка, юнит-тесты и питоновский набор зелены и совпадают с предмержевой головой, четыре сканирующих гварда зелены и **доказаны пробой в новом написании**, `vterm_boundary` красный на тех же восьми строках, что и до шага.

Восьмой мерж-шаг и первый содержательный за волну: `M6` дал ядру поверхность, `M6b` перевёз ядро, `M6c` **разбирает поверхность на части**. `VtState` исчезает; `Vterm::create` получает каждую часть отдельным параметром. Это самый крупный конфликт всей серии — **194 конфликтных места в 28 файлах**, из них 91 хунк в одном `vterm.cpp`.

---

## 1. Коммит

| Коммит | Что |
|---|---|
| `49864f1a` | `merge wave 6 step M6c: VtState dissolves into explicit embedding pieces` — сам мерж, родители `eb016aec` + `bd86ed38` |

Предмержевая голова — `eb016aec`. Слит один апстримный коммит:

```
bd86ed38 Dissolve VtState into explicit embedding pieces
```

`9bf5e497` и `7e2a8e3e` **не тянулись**: это шаги `M6d`–`M6e`.

---

## 2. Что принёс апстрим

`VtState` был «мешком»: конфигурация, пулы, платформа, окно, сетка, граница, масштаб и пять списков слушателей в одной структуре. Апстрим разбирает его на семь именованных частей и передаёт каждую в `Vterm::create` явно.

| Часть | Файл | Что несёт |
|---|---|---|
| `VtGeometry` | `lib/vterm/vt_geometry.{h,cpp}` (новые, 38 + 48 строк) | сетка, размер ячейки, пиксели поверхности, `borderPixels`, `resize()` |
| `VtConfigSlot` | `lib/vterm/vt_config.h` (+ поле `VtConfig::brandName`) | точка монтирования перезагружаемого снимка опций |
| `VtCellExtras` | `lib/vterm/cell_extra_store.h` | слот стора «дополнений» плюс его список слушателей |
| `VtHost` | `lib/vterm/vt_host.h` (новый, 44 строки) | всё, чем терминал дотягивается до окна: буферы обмена, `XTWINOPS`, кадр, заголовок, эхо ресайза |
| `stl::SmallObjAllocator&` | — | общий на эмбеддера |
| `plt::Scheduler&` | — | вместо `vt.platform->scheduler()` |
| `PtyHandle&` | `lib/vterm/pty.h` | как раньше |

Плюс два раскола:

- **`pty.h` расщепляется.** Байтовый канал (`PtyHandle`, `PtySize`) остаётся в ядре; фабрика `forkpty` (`Pty`, `createPty`) уезжает в новый `lib/shitty/pty.h`. Ядро больше не объявляет `LaunchCommand` и `plt::Platform`.
- **`Vterm::configChanged()` становится публичным виртуальным.** Раньше терминал сам подписывался на список эмбеддера; теперь перезагрузку разносит владелец времени жизни — `SessionSet::everyTerminalConfigChanged()`.

Четыре файла ядра приехали **побайтово апстримными**: `vt_geometry.h`, `vt_geometry.cpp`, `vt_host.h`, `vt_config.h`, а также `screen.h` и `screen.cpp` (проверено `diff` против `bd86ed38`, вывод пуст).

Побочно апстрим сменил владельца аренны у пяти объектов терминала: `UnicodeMap`, `Parser`, `notifications`, `savedPrivModes`, `userDefinedKeys` строились в процессном пуле (`vt.pool`), теперь — в арене сессии (`owner`). Это принято как есть: арена умирает вместе с сессией, процессный пул не умирал никогда.

Число узлов цели `st`: **226 → 226**. `vt_geometry.cpp` пришёл, `vt_state.cpp` ушёл, глоб `lib/vterm/*.cpp` в `build.py:765` их не различает.

---

## 3. Конфликты: как разрешён каждый

**194 конфликтных места в 28 файлах**: 192 содержательных хунка в 26 файлах плюс два `modify/delete` (`vt_state.h`, `vt_state.cpp`).

```
vterm.cpp 91 · render_reference.cpp 16 · vterm_headless_ut.cpp 13 · test_mode.cpp 8
application.cpp 7 · render_metal.mm 6 · session.cpp 6 · render_vk.cpp 5
vterm_headless.cpp 5 · main_fuzz/main.cpp 4 · composer_ut.cpp 4 · session_ut.cpp 4
pty_ut.cpp 3 · render_reference_ut.cpp 3 · vterm.h 3 · composer.h 2
vterm_headless.h 2 · cell_extra_store_ut.cpp 2 · и восемь файлов по одному
```

### 3.0. Метод: доказать эквивалентность, а не читать маркеры

`M6b` показал `-U0`-фильтром, что переезд не имеет содержательной части. Здесь фильтр бесполезен — коммит содержательный целиком. Вместо него применён **обратный ренейм**: апстримная сторона каждого хунка переписывалась в наше довомержевое написание (`geometry.columns` → `columns_`, `config().` → `vt.config->`, `extras_.store` → `vt.cellExtras`, `host.` → `vt.window->`, `smallObjects_` → `vt.smallObjects`, `scheduler_` → `vt.platform->scheduler()`), и если результат **посимвольно совпадал с нашей стороной**, хунк закрывался нашей стороной автоматически.

```
$ python3 resolve_vterm.py            # первый проход, только доказанные
total 91 auto-ours 70 manual 21
```

**70 из 91 конфликта `vterm.cpp` — один и тот же конфликт**: апстрим переименовал `vt.columns` в `geometry.columns` там, где наш форк уже переименовал то же самое в `columns_` (грид панели, `A8`). Ни один из них не читался глазами: каждый закрыт доказательством равенства. Оставшийся 21 разобран поимённо ниже.

### 3.1. `vterm.cpp` — двадцать один хунк, который не свёлся

| # | Что | Чья сторона | Почему |
|---|---|---|---|
| 1 | `CallVtermConfigChanged` + `CallVtermCollectExtras` | **смешанная** | `CallVtermConfigChanged` **удалён**: апстрим поднял `configChanged()` в публичный интерфейс, и `SessionSet` теперь разносит перезагрузку сам. Оставить нашу подписку — значит выполнить `configChanged()` **дважды на каждую перезагрузку**. `CallVtermCollectExtras` (`R7`) остаётся: у апстрима такого нет |
| 2, 3, 15, 19, 21 | сигнатуры и поля `VtermImpl` | **обе** | апстримный список параметров дословно, плюс два наших с краёв — §3.2 |
| 4, 5, 8, 18 | `mouseGeometry(composer, originX_, …)` против `geometry.borderPixels` | **наша** | `A8`: пиксельная мышь, хит-тест ссылок, выделение и автоскролл считают от начала **панели**, а не окна. Семь именованных тестов держат это (`T5.1` §6.1) |
| 6, 7 | `Screen::createPrimary/createAlternate` | **обе** | апстримный первый аргумент (`extras_` вместо `vt`), наши `columns_`/`rows_` |
| 9 | `updateExtraCellCount()` | **наша** | `A11`: стор один на окно и считается суммой по живым панелям (`R5-qa`, Q2) |
| 10 | `collectCellExtras()` | **наша** | `R7`: терминал — такой же клиент, как все; сам себе ничего не передаёт |
| 11 | отчёт `XTSMGRAPHICS` | **наша** | наше написание грида панели |
| 12 | `columnsForPixelWidth` / `rowsForPixelHeight` | **наша** | **прямое указание `T5.1` §2.7.** Эти функции делят **ширину окна**; инсеты панели хрома не содержат. Замена на `2u * geometry.borderPixels` компилируется, ничего не роняет и при ненулевом сайдбаре заставляет `CSI 18t` отчитаться о лишних колонках. Остаются у эмбеддера до `M6d` |
| 13 | внутриполосный ресайз | **наша** | `T5.1` §2.5: раскладку считает `Composer::resize()`, потому что резервы хрома — его. Апстримный `geometry.resize(…, &host)` считает из симметричной границы |
| 14 | `XTWINOPS` операция 8 | **наша** | та же причина, что 12 |
| 16 | палитра и `outputRows` | **обе** | апстримный `config()`, наши `rows_` |
| 17 | регистрация слушателей | **смешанная** | остаётся только `extras_.changedListeners.pushBack(CallVtermCollectExtras)` |
| 20 | посев `setCellCount` в `create()` | **наша** | `R5-qa` Q2: грид панели — неверная база для стора всего окна, и посев избыточен: `resetTerminal()` доходит до `updateExtraCellCount()` до возврата из `create()` |

### 3.2. `Vterm::create` — апстримный список плюс два наших параметра

```c++
static Vterm* create(stl::ObjPool& owner,
                     Composer& composer,             // наш, T5.4/M6d его снимут
                     VtGeometry& windowGeometry,     // апстримный geometry
                     const VtConfigSlot& config,
                     VtCellExtras& extras,
                     stl::SmallObjAllocator& smallObjects,
                     plt::Scheduler& scheduler,
                     VtHost& host,
                     const PaneGeometry& geometry,   // наш, A8
                     PtyHandle& pty,
                     VtermTraceFactory* traceFactory);
```

**Чья сторона и почему.** Апстримная — по всем семи частям, в апстримном порядке. Альтернатива (оставить нашу сигнатуру `create(owner, composer, geometry, pty, traceFactory)` и вытаскивать части из `composer` внутри конструктора) выглядела дешевле, и её пришлось бы **полностью переделать на `M6d`**: как только `Composer&` уходит, все семь инициализаций надо переписать с `composer.X` на параметр. Взяв апстримный список сразу, мы оставляем `M6d` и `T5.4` ровно одно удаление — параметр `Composer&`, — а `T5.1` ровно одно слияние: `windowGeometry` и `geometry` становятся одним `VtGeometry` на панель.

Имена выбраны в ту же сторону: **пане́льная геометрия называется `geometry`**, оконная — `windowGeometry`. После `T5.1` оконная исчезает, и `geometry` уже значит то, что должно.

Реальных вызовов `Vterm::create` в дереве два (`session.cpp`, `vterm_headless.cpp`) плюс тринадцать в `vterm_headless_ut.cpp`.

### 3.3. `cell_extra_store.h` — обе структуры рядом

Апстрим ввёл `VtCellExtras` (слот стора плюс список слушателей). У нас в том же месте живёт `CellExtraClient` (`R7`). Взяты **обе**: `VtCellExtras` дословно апстримный, `CellExtraClient` наш, комментарий в нём перенацелен с `VtState::setCellExtras()` на `VtCellExtras::replace()` — имя изменилось, смысл нет.

### 3.4. `pty.h` — апстримный раскол, наша сигнатура

Раскол принят: `Pty`/`createPty` уезжают в `lib/shitty/pty.h`, ядро остаётся с байтовым каналом. В апстримном новом файле восстановлена **наша** трёхаргументная `spawn(owner, command, size)` вместе с её комментарием: размер ставится на слейв **до** форка, иначе ребёнок, читающий `TIOCGWINSZ` первой операцией, видит 0x0 (`T5`, initial winsize). `lib/vterm/pty.h` расходится с апстримным ровно на наш `childPid()`.

Известный сигнал волны 3 сработал в третий раз: `lib/shitty/ui_sidebar_tabs_ut.cpp` включал `<lib/vterm/pty.h>` ради `Pty`, апстрим этого файла не знает и не поправил. Найден не по диффу, а сканом дерева на включения переехавшего имени.

### 3.5. `vterm_headless.{h,cpp}` — единственное место, где апстримный код отброшен целиком

Апстримный безголовый хост строит себе платформу, окно, геометрию, слот конфигурации, стор и **собственный адаптер `HeadlessVtHost`**, потому что `Composer` вокруг него нет. Наш — эмбеддер `Composer`, и адаптер у него уже есть.

**Отброшено:** `HeadlessVtHost` целиком, 71 строка (объявление плюс 17 определений-переадресаций). **Взято:** пять апстримных аксессоров (`platform()`, `window()`, `host()`, `geometry()`, `extras()`) и поля под них — они отвечают из `Composer`. **Сохранено наше:** `create(Composer&, …)`, `CallHeadlessResize`, `CallHeadlessFontChanged` и подсчёт безголовой поверхности через `contentInsets()`, а не через `2 * borderPixels`.

Причина, по которой второй адаптер не оставлен рядом: `HeadlessVtHost::resized()` вызывает `terminal->windowResized()`, которого у нашего `Vterm` нет (его заменил `paneResized(const PaneGeometry&)`), а чтобы вызвать наш, ему понадобился бы тот же `Composer` — и получился бы второй путь к окну, живущий рядом с первым и никогда не исполняемый. `T5.9` заменяет этот файл апстримным `vt_headless.*` целиком, и `HeadlessVtHost` вернётся оттуда.

Побочно: `HeadlessVtHost::titleChanged()` у апстрима пуст. Наш безголовый хост берёт `ComposerVtHost`, и заголовок доходит до `composer.titleChangedListeners` — то есть **ровно туда же, куда доходил до мержа**.

### 3.6. Ренейм по дереву: 1088 обращений в 37 файлах

`Composer::vt` исчез; каждое поле получило дом. Ренейм выполнен машинно, с сохранением получателя — иначе под ту же форму попал бы `Options::vt` (это `VtConfig`, и он не менялся ни на символ):

| Было | Стало | Обращений |
|---|---|---:|
| `.vt.window` | `.window` | 169 |
| `.vt.config` | `.vtConfig.config` | 117 |
| `.vt.glyphWidth` / `.vt.glyphHeight` | `.geometry.cellPixelWidth` / `cellPixelHeight` | 175 |
| `.vt.pixelWidth` / `.vt.pixelHeight` | `.geometry.pixelWidth` / `pixelHeight` | 154 |
| `.vt.platform` | `.platform` | 78 |
| `.vt.cellExtras` | `.extras.store` | 70 |
| `.vt.columns` / `.vt.rows` | `.geometry.columns` / `rows` | 132 |
| `.vt.setCellExtras(` | `.extras.replace(` | 52 |
| `.vt.smallObjects` | `.smallObjects` | 25 |
| `.vt.setGlyphSize(` | `.geometry.setCellPixelSize(` | 24 |
| `.vt.configChangedListeners` | `.configChangedListeners` | 20 |
| `.vt.cellExtrasChangedListeners` | `.extras.changedListeners` | 16 |
| `.vt.fontChangedListeners` | `.fontChangedListeners` | 15 |
| `.vt.contentScale` | `.contentScale` | 13 |
| `.vt.pool` | `.pool` | 10 |
| `.vt.resizedListeners` | `.resizedListeners` | 10 |
| `.vt.titleChangedListeners` | `.titleChangedListeners` | 3 |
| `.vt.baseBorder` | `opts->border` (§6.1) | 3 |
| `.vt.brandName` | `config().brandName` (§4) | 2 |

Скрипт падал, встретив неизвестное поле, — чтобы «почти подошло» не прошло молча. Три формы записаны вручную, потому что менялась не только левая часть: `Screen::create*(composer.vt, …)` → `(composer.extras, …)`, `CellExtraStore::create(composer.vt, n)` → `(composer.extras, *composer.pool, n)`, и одиннадцать аргументов `Vterm::create`.

### 3.7. `installVtHost()` — то, что не поймал ни один компилятор

Адаптер `ComposerVtHost` строится отдельным вызовом `Composer::installVtHost()`, «когда окно уже есть». Наша сборка компилировалась и линковалась, `st` собирался, а **все 65 тестов `SessionSet` падали с SIGSEGV**: `composer.host` оставался нулевым, и `*composer.host` в `Vterm::create` разыменовывал ноль.

Отказ был бы невидим, если бы юнит-тесты не гонялись: продуктовый путь (`application.cpp`) вызов уже содержал — он приехал апстримным автомержем. Не хватало его девятнадцати тестовым фикстурам. Вставлено по одному вызову после каждого создания окна в `session_ut.cpp`, `test_mode.cpp`, `application_ut.cpp`, `ui_csd_tabs_ut.cpp`, `ui_sidebar_tabs_ut.cpp`.

**Мораль для следующих шагов:** «дерево собралось» о таком не говорит ничего. Отказ поймал только прогон `unit_tests`, и поймал его на 767-м тесте из 950.

---

## 4. `VtConfig::brandName` и наш `Brand`

План предупреждал о втором источнике правды. **Расхождения нет — и вот почему, с числами.**

`VtConfig::brandName` не заводит новое имя. Это `stl::StringView` на результат `Brand::displayName()`, и он ставится в двух местах, оба апстримные:

```
lib/shitty/composer.cpp:51   defaults->vt.brandName = brand->displayName();   // зашитые умолчания
lib/shitty/options.cpp:858   vt.brandName = brand.displayName();             // каждый разобранный снимок
```

Читается **ровно один раз**, в ядре: `lib/vterm/vterm.cpp:5940`, ответ на `XTVERSION` (`DCS > | <имя> <версия>`). До мержа то же самое читалось из `VtState::brandName`, который заполнялся из того же `brand->displayName()`. Поле переехало из `VtState` в `VtConfig` — это и есть весь эффект.

**Чем два имени различаются.** У `Brand` их два, и они разные по смыслу:

| Метод | Значение в `st` / `pt` | Кто читает |
|---|---|---|
| `displayName()` | `Shitty` / `Pretty` | `VtConfig::brandName` → `XTVERSION`; заголовок окна; баннер `-version` |
| `identifierCString()` | `shitty` / `pretty` | префикс диагностики в 15 местах эмбеддера |

Ядро после `M6c` знает **только первое**, и знает его снимком: значение копируется в `VtConfig` в момент публикации опций. Эмбеддер читает второе живьём, через указатель `composer.brand`.

**Единственный способ их развести** — подменить `Brand` после публикации опций. Ни одно место этого не делает: бренд выбирается в `main()` (`bin/st/main.cpp`, `bin/pt/main.cpp`), передаётся в конструктор `Composer` и больше не меняется; `GenericBrand` (`Terminal`) обслуживает безголовые адаптеры. Проверено: `git grep 'composer.brand ='` пуст, а `Composer::brand` присваивается только в списке инициализации.

Проверка на брендирование `pt` пройдена отдельно — `python3 tst/pretty_binary_branding.py .build/pt`, `EXIT=0`: подстрока `shitty` в `pt` после `strip` не появилась.

---

## 5. Гварды: что проверено пробой

Все пять запускались **напрямую**, извлечением программы из `build.py` разбором AST; через `./build` доказательства не было бы (`CLAUDE.md`: кеш адресуется содержимым).

```
border_pixels_guard    EXIT=0   output bytes=0
mouse_geometry_guard   EXIT=0   output bytes=0
pane_grid_guard        EXIT=0   output bytes=0
darwin_call_guard      EXIT=0   output bytes=0
vterm_boundary         EXIT=1   8 строк, 3 файла
```

**Зелёное без пробы ничего не значит.** Для каждого проверялось не «зелёный ли», а **видит ли он изменившийся код** — проба ставилась в файл, который этот шаг переписал, и в написании, которое этот шаг ввёл.

| Гвард | Проба | Результат |
|---|---|---|
| `pane_grid` | `return composer.geometry.columns + composer.geometry.rows;` в **`render_reference.cpp`** | `EXIT=1`, две строки `render_reference.cpp:1161` |
| `pane_grid` | `return composer.vt.columns;` (написание `M6`, теперь мёртвое) | `EXIT=1` — старая половина словаря ещё жива |
| `pane_grid` | самопроверка: спрятан `render_vk.cpp` | `EXIT=1`, «Unreachable: render_vk.cpp» |
| `border_pixels` | `return composer.borderPixels();` в **`lib/vterm/vterm_headless.cpp`** | `EXIT=1`, `lib/vterm/vterm_headless.cpp:261` |
| `border_pixels` | `composer.geometry.borderPixels + composer.geometry.borderPixels` там же | `EXIT=0` — **поле не считается, форма вызова считается**, ровно как записано в решении `T5.1` §2.2 |
| `mouse_geometry` | `mouseGeometry(composer)` в **`lib/vterm/vterm_headless.cpp`** | `EXIT=1`, `lib/vterm/vterm_headless.cpp:261` |
| `mouse_geometry` | `mouseGeometry(composer, 1, 2, 3, 4)` там же | `EXIT=0` — форма с origin не считается |
| `mouse_geometry` | самопроверка: спрятан `lib/vterm/mouse_frontend.cpp` | `EXIT=1`, «re-key the allowance onto where these live now» |
| `darwin_call` | `createCsdTabsUi(composer);` вне гварда в **`application.cpp`** | `EXIT=1`, `application.cpp:1210` |
| `darwin_call` | тот же вызов под `#if defined(__APPLE__)` | `EXIT=0` |

Первая проба каждой пары ставилась **настоящим кодом, не комментарием**: `blanked()` заменяет тела комментариев пробелами, и проба в комментарии зеленеет у всех четырёх гвардов сразу — на первом заходе так и вышло, и это стоило одного ложного «гварды слепы».

**Ловушка `T2.1` больше не открыта.** `pane_grid_names` уже содержал `composer.geometry.columns`/`rows` — написание, которое `T2.1` угадала «на один мерж-шаг вперёд»: `M6` увёл поверхность в `composer.vt.columns` и на этом написании гвард ослеп, а `M6c` вернул то, что `T2.1` предполагала. Словарь **не менялся**; изменён только комментарий над ним, чтобы следующий читатель не гадал, зачем в нём четыре мёртвых имени. Разрешения, счётчики, корни сканирования, `border_pixels_allowance`, `pane_grid_backends`, `darwin_guard_macros` — **не тронуты ни на символ**:

```
$ git diff eb016aec HEAD -- build.py | grep -E '^[+-]' | grep -v '^[+-]#' | grep -vE '^(\+\+\+|---)'
(пусто)
```

### 5.1. `vterm_boundary` — список не вырос

```
$ python3 lib/vterm/check_includes.py lib/vterm /tmp/vb.stamp ; EXIT=1
mouse_frontend.h:9:    "composer.h"        does not resolve inside lib/vterm
vterm.cpp:22:          "session.h"
vterm.cpp:23:          "composer.h"
vterm.cpp:27:          "grid_geometry.h"
vterm_headless.cpp:11: "options.h"
vterm_headless.cpp:12: "composer.h"
vterm_headless.cpp:13: "pane_layout.h"
vterm_headless.cpp:14: "grid_geometry.h"
```

**Восемь строк в трёх файлах, побайтово те же, что после `M6b`.** Новых нет.

Внутри `vterm.cpp:23` шаг сделал ровно то, что предсказало решение `T5.1` §7: обращений к эмбеддеру было одиннадцать, стало **десять** — `composer.vt` ушло. Оставшиеся:

```
composer.contentInsets  ×3   (6534, 6541, 6612)      — T5.1 вариант «А», с оговоркой §2.7
composer.sessions       ×2   (3298, 3299)            — T5.4
composer.resize         ×1   (6570)                  — M6d
mouseGeometry(composer) ×4   (1648, 2053, 2485, 9778) — T5.1 вариант «А»
```

Тип `Composer&` в четырёх сигнатурах (`vterm.cpp:428, 993, 8856, 9925`) — `T5.4`.

---

## 6. Что сделано ради варианта «А», и что оставлено `T5.1`

Знание решения понадобилось четыре раза, и каждый раз развилка была настоящей.

### 6.1. Сделано

1. **`geometry.borderPixels` не заполняется.** Апстрим в `Composer::setOptions()` и `setContentScale()` вычисляет `scaledBorder(options->border, contentScale)` и кладёт в поле ядра. Это принято **не было**. Причина в двух словах: `A1` кладёт преобразование «точки → пиксели» на эмбеддера, `contentInsets()` — единственный источник геометрии раскладки, и заполненное поле было бы **второй масштабированной границей**, о которой `T5.1` и существует. Проверено, что поле мёртвое в обе стороны:

   ```
   $ git grep -n 'geometry\.borderPixels' -- '*.cpp' '*.h' '*.mm'
   lib/shitty/composer.h:150:    // meters. geometry.borderPixels is left at its default rather than
   ```

   Одно упоминание, и то комментарий. Вариант «А» удаляет поле; заполнить его сейчас значило бы написать код, который `T5.1` первым делом сотрёт, и оставить в дереве расхождение с `contentInsets()` до тех пор.

2. **`VtGeometry::resize()` не получает вызывающего.** Апстрим зовёт его из `ApplicationImpl::updateWindowInfo()`, `showWindow()`, внутриполосного `XTWINOPS` и безголового `create()`. Все четыре взяты нашей стороной: считает `Composer::resize()`, из `contentInsets()`. `T5.1` §2.5 говорит ровно это. Функция скомпилирована и не вызывается — как и `borderPixels`, это апстримный файл, который `T5.1` перепишет.

3. **`Composer::borderPixels()` перестал читать копию.** `VtState::baseBorder` был копией `opts->border`, снятой в `setOptions()`. `VtState` растворился, копии не стало, и метод читает опубликованный снимок там, где он публикуется:

   ```c++
   u16 Composer::borderPixels() const {
       return scaledPixels(opts->border);
   }
   ```

   Это то самое «двух представлений одной величины не заводить», ради которого `T5.1` отвергла вариант «Б». Счётчик `border_pixels_allowance["lib/shitty/composer.cpp"] = 8` при этом **не изменился** — гвард считает формы вызова, а их столько же.

4. **Пане́льная геометрия называется `geometry`, оконная — `windowGeometry`.** После `T5.1` оконная исчезает; имя, которое останется, уже правильное.

### 6.2. Оставлено `T5.1`

- Замена `borderPixels` на четырёхсторонний `VtInsets` и добавление `originX`/`originY`/`width`/`height` в `VtGeometry`. `vt_geometry.{h,cpp}` лежат **побайтово апстримными** — ровно тот вход, который решение описывает.
- Закрытие `PaneGeometry` (`vterm.h:52-65`) и слияние её с `VtGeometry`. Обе структуры сейчас живут рядом, и это признанная временная цена: §2.4 решения называет две структуры одной формы дефектом, но снимает его та же задача, что их сливает.
- Перевод `mouseGeometry(const Composer&)` на `mouseGeometry(const VtGeometry&)` — четыре вызова, и с ними строка `mouse_frontend.h:9` из красного списка.
- Три обращения `composer.contentInsets()` в `vterm.cpp`, **с оговоркой §2.7**: строки 6537, 6544, 6615 отвечают на вопросы **про окно**, и механический перевод на инсеты панели их сломает молча.

### 6.3. Подойти к «А» вплотную не пришлось

Ни один конфликт не потребовал реализовать «А», чтобы дерево собралось. Ближе всего было место, где `Composer::setOptions()` перестал заполнять `baseBorder`: там пришлось решить, откуда `borderPixels()` берёт число, — и оба ответа (`opts->border` и `geometry.borderPixels`) компилируются. Взят первый, потому что второй «А» мешает.

---

## 7. Таблица критериев

| № | Критерий | Результат |
|---|---|---|
| 1 | `./build st --clear` зелёная | **226 узлов**, `EXIT=0`; эталон до мержа снят в этом же дереве — тоже 226 |
| 2 | `./build unit_tests pty_test_helper` + прогон | `EXIT=0`, **`OK: 950`**; имена сверены поимённо, расхождение — одно апстримное переименование |
| 3 | Совпадение диффов | **44 и 374**, каждое расхождение отнесено к названному решению — §7.3 |
| 4 | Питоновский набор поимённо | `Ran=6399`, красные **побайтово совпадают** с предмержевой головой |
| 5 | Четыре сканирующих гварда | `EXIT=0` каждый, и каждый доказан пробой в новом написании — §5 |
| 6 | `vterm_boundary` не вырос | **8 строк, 3 файла**, те же, что после `M6b` |
| 7 | `A1`, `A8`, `A9`, `A10` | не нарушены — §7.5 |

### 7.1. Сборка

Эталон, снятый на предмержевой голове `eb016aec` в этом же дереве:

```
$ ./build st --clear -j 10 ;  EXIT=0
[AR] {225/226} $(B)/libshitty_prod.a
[LD] {226/226} $(B)/st
```

После мержа:

```
$ ./build st --clear -j 10 ;  EXIT=0
[CC] {224/226} $(B)/obj/libshitty/lib/vterm/parser.cpp.o
[AR] {225/226} $(B)/libshitty_prod.a
[LD] {226/226} $(B)/st
```

```
$ ./build st pt --clear -j 10 ;  EXIT=0
[LD] {229/230} $(B)/st
[LD] {230/230} $(B)/pt
```

`--clear` обязателен: шаг двигает пути включений (расщепление `pty.h`), а `build.includes` в ключ узла не входит (`CLAUDE.md`).

**Свежесть бинарника, не через систему сборки.** Пути `__FILE__`, вкомпилированные в `st`:

```
$ strings -a .build/st | grep -c "lib/vterm/vt_geometry.cpp"  → 1
$ strings -a .build/st | grep -c "lib/vterm/vt_state.cpp"     → 0
```

Слинкованный бинарник — смерженное дерево, а не штамп из кеша.

Две проверки, которые `./build st pt` не запускает (`CLAUDE.md`), прогнаны отдельно:

```
$ python3 tst/pretty_binary_branding.py .build/pt                     ;  EXIT=0
$ python3 tst/production_surface.py   (с тремя переменными из build.py:1205) ;  EXIT=0
Ran 5 tests in 0.105s / OK
```

### 7.2. Юнит-тесты

```
$ ./build unit_tests pty_test_helper -j 10 ;  EXIT=0
$ SHITTY_PTY_TEST_HELPER="$PWD/.build/pty_test_helper" ./.build/unit_tests --threads=1
EXIT=0
OK: 950
```

Эталон до мержа в этом же дереве — **`OK: 950`**. Сверка имён:

```
$ diff base-ut-names.txt after-ut-names.txt
> VtermHeadless::BuildsItsOwnEmbeddingPieces
< VtermHeadless::InstallsMissingComposerDependencies
< VtermHeadless::SecondVtermCoexistsOnOneComposer
> VtermHeadless::SecondVtermCoexistsOnOneEmbedding
```

**Три строки, ноль потерь.** Оба теста апстрим переименовал, и оба сохранили наши утверждения:

- `InstallsMissingComposerDependencies` → `BuildsItsOwnEmbeddingPieces`. Все четыре наших утверждения на месте (`composer.geometry.columns == 80`, `rows == 24`, обе стороны через `gridPixelWidth/Height(…, contentInsets(), …)`), плюс шесть апстримных на новые аксессоры — это **надмножество**, а не замена.
- `SecondVtermCoexistsOnOneComposer` → `SecondVtermCoexistsOnOneEmbedding`. Тело наше, `Vterm::create` с нашими одиннадцатью аргументами.

### 7.3. Совпадение диффов

Построчно, машинно, в обе стороны, с учётом переименований (ключ — пост-образный путь). `A` — строки, которые апстрим удалил, а у нас они остались. `B` — строки, которые апстрим добавил, а у нас их нет.

```
$ python3 diffcmp.py eb016aec <worktree> bd86ed38
upstream post-image paths: 45
A. upstream-removed lines still present in the merged tree: 44
B. upstream-added lines absent from the merged tree: 374
```

Объём, чтобы числа читались:

```
upstream: 45 путей, +1417 / −1055
ours    : 56 путей, +1720 / −1354
```

`M6b` дал `0` и `0`, потому что переезд не имеет содержательной части. Здесь она есть, и **каждое расхождение отнесено к названному решению**:

| Кластер | A | B | Решение |
|---|---:|---:|---|
| `vterm.cpp` | 10 | 132 | из 132: **106 — переименование грида панели** (`geometry.columns` ↔ `columns_`, `A8`); 12 — арифметика указателя и грида через `geometry.borderPixels` (`A1`/`A10`, `T5.1` §2.7); 6 — наши сигнатуры; 1 — `geometry.resize()` вместо `composer.resize()`; 3 — `R5-qa` Q2, `SixelPatch`, `DECRQSS`. `A` — десять строк фигурных скобок и `VtermImpl* terminal;`, общих у нашего `CallVtermCollectExtras` и удалённого апстримом `CallVtermConfigChanged` |
| `vterm_headless.{h,cpp}` | 25 | 83 | §3.5: наш `create(Composer&)`, `CallHeadlessResize`, `CallHeadlessFontChanged` против апстримного `HeadlessVtHost`. Файл принадлежит `T5.9` |
| `render_reference.cpp`, `render_vk.cpp`, `render_metal.mm` | 3 | 48 | **`A9` целиком.** Апстримные рендереры читают грид композера; наши — `update.gridColumns`/`gridRows`. Взять апстримную сторону здесь **физически нельзя**: `pane_grid_guard` краснеет на каждой такой строке, что показано пробой в §5 |
| `application.cpp` | 0 | 17 | `A1`/`A2`/`A10`: четырёхсторонние инсеты, `gridPixelSize`, панельный кадр, трасса грида в `Composer::resize()` (`F4`, Q2) |
| `test_mode.cpp`, `main_fuzz`, `pty_ut`, `composer_ut`, `session_ut`, `render_reference_ut`, `span_shaper_ut` | 2 | 52 | `2 * borderPixels` против `contentInsets()`, `geometry.resize(…, host)` против `composer.resize()`, безголовая сигнатура |
| `vterm_headless_ut.cpp` | 3 | 20 | наш `SecondPtyStub(Composer&)` и тринадцать вызовов `Vterm::create` с нашей сигнатурой |
| `session.cpp` | 0 | 8 | `ptySize(pane)` на панель, `Vterm::create` с `PaneGeometry`, наш путь закрытия панели |
| `composer.cpp` | 0 | 9 | `scaledBorder()` и два присваивания `geometry.borderPixels` — §6.1 |
| `vterm.h`, `main.cpp`, `lib/shitty/pty.h`, `ui_csd_tabs.mm` | 0 | 5 | наши сигнатуры `create`, наш `spawn(…, size)`, наш `nativeWindow(composer)` |
| `lib/vterm/pty.h` | 1 | 0 | одна строка `};`, совпавшая с другой в том же файле |

**Файлов, которые тронул апстрим и не тронули мы, ноль.**

Апстримные файлы, приехавшие **побайтово**:

```
$ diff <(git show bd86ed38:$f) $f    для шести файлов — пусто
lib/vterm/vt_geometry.h   lib/vterm/vt_geometry.cpp   lib/vterm/vt_host.h
lib/vterm/vt_config.h     lib/vterm/screen.h          lib/vterm/screen.cpp
```

### 7.4. Питоновский набор

Прогнан трижды в одном дереве: на предмержевой голове `eb016aec` и дважды после мержа, 20 групп параллельно с полным окружением из `build.py:1131`.

```
before (eb016aec):  Ran=6399   19 красных строк
after  (49864f1a) #1: Ran=6399  22 красных строки
after  (49864f1a) #2: Ran=6399  19 красных строк, diff по именам ПУСТ
```

Число тестов — эталонные 6399 (6390 уникальных, девять исполняются дважды, `G14`).

Первый прогон дал **три лишних красных**, все три — спавн pty:

```
test_startup.StartupTest.test_child_environment_winsize_and_sigwinch
test_startup.StartupTest.test_spawned_child_uses_normal_tty_output_processing
test_pty.PtyTest.test_child_exit_report_includes_output_flushed_at_exit
```

Отказ у всех трёх один: `RuntimeError: test child tty has no path`. Это флак-класс «порождатель pty», названный в `M6b` §5.6. Проверено, а не предположено: три штуки подряд в изоляции — `OK` три раза из трёх, и **второй полный прогон набора совпал с предмержевым побайтово**. Учитывая, что шаг расщепляет `pty.h`, совпадение стоило перепроверить, и оно перепроверено.

### 7.5. Инварианты

| Инвариант | Чем проверен |
|---|---|
| `A1` — `contentInsets()` единственный источник геометрии раскладки | `border_pixels_guard` зелёный **без единой правки разрешения** (`border_pixels_allowance` в диффе `build.py` отсутствует), краснеет на пробе в файле, переписанном этим шагом, и остаётся зелёным на апстримном поле. `geometry.borderPixels` не заполняется и не читается (§6.1). Зелены `Composer::ResizeCountsTheGridOutOfTheContentInsets`, `ContentInsetsCarryTheBorderOnEverySide`, `ReferenceRenderer::PlacesTheGridAtTheContentInsets` |
| `A8` — геометрия панели передаётся, а не читается из окна | `mouse_geometry_guard` зелёный, счётчики `1` и `1` не изменены; краснеет на пробе и на снятом ключе. 70 из 91 конфликта `vterm.cpp` разрешены **сохранением** `columns_`/`rows_` панели против апстримного `geometry.columns` окна. Зелены `VtermHeadless::TakesItsGridFromThePaneItWasGiven`, `PointerReportsCountFromTheOriginTheVtermWasGiven`, `MovingThePaneMovesWhereItsPointerReportsCountFrom`, `SelectionStartsInTheCellThePaneOwnsAndNotTheWindows`, `AutoscrollMeasuresFromThePanesTopEdgeAndNotTheWindows`, `SessionSet::EveryPaneGetsItsOwnGridAndTellsItsChild` |
| `A9` — рендерер берёт сетку из `TerminalUpdate` | `pane_grid_guard` зелёный; словарь не тронут, новое написание `composer.geometry.columns` краснеет пробой, самопроверка «дошёл до трёх бэкендов» краснеет на спрятанном `render_vk.cpp`. **48 апстримных строк в трёх рендерерах отброшены именно потому, что это `A9`.** Зелены `ReferenceRenderer::AFrameWithoutAGridIsRefused`, `DrawsTwoPanesOfDifferentGrids`, `MetalPanes::AZeroGridInOnePaneRefusesTheWholeFrame` |
| `A10` — `chromeInsets`/`paneInsets`/`contentInsets` названы по отдельности | все три метода на месте и не размножились; `chromeReserves[]` и `contentScale` сосуществуют в `Composer` после разрешения конфликта в `composer.h`. Зелены `Composer::EachChromeSideKeepsItsOwnReserveOnItsOwnEdge`, `EveryPixelOfAComposersContentBoxAnswersFromItsOwnSide`, `SessionSet::PanesDivideTheContentBoxAndNotTheWindow` |

Все восемнадцать названных тестов прогнаны точечно: `OK: 18, SKIP: 932`.

### 7.6. Мина `static_cast` через слушателей

Проверялось не «собралось ли», а **то ли лежит в списках**. После шага пять списков слушателей `Composer` и один в `VtCellExtras`.

Опасен ровно один: `extras.changedListeners` **обходится дважды разными типами** — `VtCellExtras::replace()` кастует к `Listener*` (базовый, безопасно), а `CellExtraStoreImpl::collect()` — к `CellExtraClient*` (производный, обязателен для каждой записи). Перечислены все, кто в него кладёт:

```
lib/vterm/vterm.cpp:8928        CallVtermCollectExtras   : CellExtraClient  ✓
lib/shitty/span_shaper.cpp:667  CallShaperExtrasCollected: CellExtraClient  ✓
lib/shitty/composer_ut.cpp:202  ExtrasClient             : CellExtraClient  ✓
lib/shitty/cell_extra_store_ut.cpp × 8: ExtraChangeListener, CountingClient : CellExtraClient  ✓
```

Ни одного голого `Listener`. Остальные пять списков (`resizedListeners`, `fontChangedListeners`, `configChangedListeners`, `titleChangedListeners`, `contentScaleChangedListeners`) обходятся **только** как `Listener*`, и все 27 регистраций в них — подклассы `Listener` (проверено поимённо).

Мина не сработала, но шаг был к ней ближе, чем `M6b`: он переносит два списка (`titleChangedListeners`, `configChangedListeners`) из ядра к эмбеддеру и заводит `VtHost` с виртуальным `titleChanged()`.

---

## Обнаружено

1. **`installVtHost()` — отказ, который видно только прогоном тестов.** Дерево собиралось, `st` линковался, `./build st pt --clear` был зелёный — и все 65 тестов `SessionSet` падали с SIGSEGV на нулевом `composer.host`. Продуктовый путь вызов содержал (приехал апстримом), девятнадцать тестовых фикстур — нет. Это тот же класс, что «мина `static_cast`»: компилятор не помогает, потому что нарушен не тип, а порядок инициализации. **Для следующих шагов: `./build st pt --clear` не является свидетельством об этом классе вообще.**

2. **Проба в комментарии зеленит все четыре гварда сразу.** `guard_source_reader` заменяет тела комментариев пробелами (это записано в `build.py:1294`, но читается как деталь). Первый заход проб дал `EXIT=0` у всех четырёх, и это на секунду выглядело как «гварды ослепли от растворения `VtState`». **Проба обязана быть настоящим кодом.** Стоит внести это в брифы: форма пробы — это то, что проверяет проверку.

3. **Ловушка `T2.1` закрылась сама, на шаг позже, чем ожидалось.** `pane_grid_names` содержит `composer.geometry.columns` с самого `T2.1` — угаданное написание. `M6` увёл поверхность в `VtState` и написание стало `composer.vt.columns`, на котором гвард ослеп; `M6` это нашёл и добавил четыре имени, `M6b` перепроверил пробой. `M6c` вернул написание к угаданному, и словарь снова видит. Мораль не «угадали правильно», а обратная: **написание менялось дважды за три шага**, и единственное, что отличало зелёный гвард от слепого, — проба.

4. **`git merge` тихо привёл `SecondPtyStub` к апстримному конструктору, оставив наше поле.** В `vterm_headless_ut.cpp` конструктор стал `SecondPtyStub(plt::Scheduler&)` с инициализацией `scheduler(scheduler_)`, а поле осталось `Composer& composer;` — соседние хунки, слитые по отдельности. Это не компилировалось, поэтому нашлось; но тот же механизм в паре, где оба варианта компилируются, дал бы молчаливый отказ. **Автомерж внутри одной структуры надо перечитывать целиком, а не по хунку.**

5. **`reorder_includes` трогает файлы, которых мерж не касался.** Из семи файлов, которые он переписал, у пяти набор включений мерж не менял вовсе — они просто никогда не проходили через `style.py` (`mouse_frontend_ut.cpp`, `ui_csd_tabs.mm`, `ui_csd_tabs_ut.cpp`, `ui_quick_hotkey.mm`, `ui_sidebar_tabs.mm`). Блоки возвращены на место; в диффе мержа их нет. `M6b` тот же эффект принял как побочный — стоит договориться, что перестановка включений применяется только к файлам, у которых **набор** включений изменился.

6. **`geometry.borderPixels` и `VtGeometry::resize()` въехали мёртвыми.** Поле не заполняется и не читается, функция не вызывается. Это осознанно (§6.1), но пока `T5.1` не пришла, в `lib/vterm/vt_geometry.{h,cpp}` лежат поле и метод, которые компилируются и ничего не делают, и следующий читатель может решить, что поле — источник правды. Смягчено комментарием в `composer.h:144-152`; настоящее лечение — `T5.1`.

7. **Апстрим сменил владельца арены у пяти объектов терминала, и это прошло автомержем.** `UnicodeMap`, `Parser`, `notifications`, `savedPrivModes`, `userDefinedKeys` строились в процессном пуле, теперь строятся в арене сессии. Изменение к лучшему (раньше они переживали свою сессию), но оно проехало **без единого конфликта** и заметно только чтением конструктора. Это класс правок, который `-U0`-фильтр `M6b` не показал бы: он ищет строки, а не смысл.

---

## Что осталось задачам `T5.x`, оценённое после мержа

| Задача | Оценка после `M6c` |
|---|---|
| `T5.1` | Вход идеальный: `vt_geometry.{h,cpp}` побайтово апстримные, `borderPixels` мёртв, `VtGeometry::resize()` без вызывающих, `PaneGeometry` изолирована в `vterm.h:52-65`. Работа ровно та, что описана в решении. Плюс один пункт, которого в решении нет: **удалить параметр `windowGeometry` из `Vterm::create`** — после слияния он не нужен |
| `T5.2` | Подтверждается вывод решения §2.6: расщеплять `grid_geometry.h` нечего. Из ядра его функции зовутся из четырёх мест `vterm.cpp` (6534, 6541, 6613, 6614), и все четыре — вопросы про окно |
| `T5.4` | Снимает `composer.sessions` ×2 и тип `Composer&` в четырёх сигнатурах. После `M6c` это **единственный** параметр `Vterm::create`, который не апстримный |
| `T5.5` | 65 механических хунков. Оговорка §2.7 решения теперь конкретна: строки `columnsForPixelWidth`, `rowsForPixelHeight` и ветка `operation == 8` в `windowOperation` — **не механические**, они остаются на `contentInsets()` до `M6d` |
| `T5.9` | Объём вырос: помимо замены `vterm_headless.*` на апстримный `vt_headless.*`, надо вернуть `HeadlessVtHost` (71 строка, §3.5) и перевести тринадцать вызовов `Vterm::create` в `vterm_headless_ut.cpp`. Четыре из восьми строк `vterm_boundary` — её |
| `M6d` | Снимает `composer.resize` через `VtHost::requestResize()`. `VtHost` уже на месте и уже используется ядром двадцатью тремя вызовами (`grep -c 'host\.[a-zA-Z_]*(' lib/vterm/vterm.cpp` → 23), так что шаг стал меньше, чем был |

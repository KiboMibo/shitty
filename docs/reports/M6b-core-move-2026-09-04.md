# M6b. Ядро VT переезжает в `lib/vterm`, и три файла уносят с собой имя эмбеддера

**Дата:** 2026-09-04 · **Задача:** `M6b`, волна 6 плана `docs/plans/2026-08-29-upstream-merge.md` · **Ветка:** `wave/upstream-merge-w6b` · **Вердикт:** `DONE с одним именованным красным` — сборка, юнит-тесты и питоновский набор зелены и совпадают с предмержевой головой, четыре сканирующих гварда зелены, **`vterm_boundary` красный на восьми строках включений** — это расхождение нашего форка, предсказанное чек-листом ревью до начала волны, и оно принадлежит `T5.1`, `T5.2`, `T5.4`, `T5.9`.

Седьмой мерж-шаг и самый механический из всех: 24 файла переезжают, из них 21 — **побайтово**; всё остальное, что апстрим сделал в этом коммите, — это строки `#include`. Проверено, а не предположено: `-U0`-дифф апстрима по двенадцати конфликтующим файлам, очищенный от строк `#include` и пустых, **пуст**.

---

## 1. Коммиты

| Коммит | Что |
|---|---|
| `61f7960a` | `merge wave 6 step M6b: the VT core moves into lib/vterm` — сам мерж, родители `e3614445` + `f3de9de6` |
| `4dd18140` | `mouse_frontend lives in lib/vterm now, and the allowance follows it there` — перекеивание `mouse_geometry_allowance`, счётчики не тронуты |

Предмержевая голова — `e3614445`. Слит один апстримный коммит:

```
f3de9de6 Move the VT core into lib/vterm
```

Следующие коммиты диапазона (`bd86ed38`, `9bf5e497`, `7e2a8e3e`) **не тянулись**: это шаги `M6c`–`M6e` по решению `Р1`.

---

## 2. Что принёс апстрим

Машина состояний переходит границу целиком: vterm, модель экрана, парсер с его ragel-исходником и проверкой тотальности, cell extras, кодировщики мыши, отчёт о возможностях, безголовый хост, трассировочная и тестовая поверхности и **интерфейс** pty. Реализация `forkpty` остаётся в `lib/shitty` — теперь она эмбеддер собственного интерфейса.

| Что | Сколько |
|---|---|
| Переезжающих файлов | **24** (21 `R100`, 3 `R099`) |
| Файлов, где апстрим правил включения | 28 |
| `build.py` | 11 строк: пути `parser.rl`, `check_parser_totality.py`, `generate_utf8_dfa.py`, `parser.cpp`, `vterm.cpp` |
| Всего строк апстримного диффа | `+98 / −91` |

Три `R099` — это `mouse_frontend.h` (перестановка `<std/sys/types.h>` под `#pragma once`), `parser.cpp` и `parser.h` (тот же порядок блоков). Ни одной содержательной правки в переезжающих файлах нет.

**Множество файлов ядра снято командой, а не из таблицы плана**, и совпало с нашим побайтово:

```
$ diff <(git ls-tree -r --name-only f3de9de6 lib/vterm) \
       <(git ls-tree -r --name-only HEAD      lib/vterm)
CORE FILE SETS IDENTICAL          # 86 файлов с каждой стороны
```

`lib/shitty` после мержа отличается от апстримного только **добавками нашего форка** (`grid_geometry`, `pane_layout`, `quick_*`, `render_arena`/`render_blend`/`render_push_constants`, `tint_coat`, `ui_*`) — ни один файл, который апстрим в `lib/shitty` держит, у нас не пропал.

Число узлов цели `st`: **226 → 226**. Ноль изменений, и это ожидаемо: `build.py:765` собирает `lib/shitty/*.cpp` и `lib/vterm/*.cpp` одним глобом в одну библиотеку, поэтому переезд внутри этой пары узлов не создаёт и не убирает.

---

## 3. Конфликты: как разрешён каждый

Двенадцать конфликтующих файлов, **15 конфликтных хунков**. Четырнадцать из них — блоки включений, один — `build.py`.

### 3.0. Метод: доказать, что апстримная правка содержательной части не имеет

Прежде чем разрешать, апстримный дифф по всем конфликтующим файлам был отфильтрован от строк `#include` и пустых:

```
$ git diff -M -U0 f3de9de6~1 f3de9de6 -- <12 конфликтующих файлов> \
  | grep -E '^[+-]' | grep -vE '^(\+\+\+|---)' | grep -vE '^[+-]\s*(#include|$)'
(пусто)
```

Отсюда разрешение по правилу, а не по маркерам: **взять нашу сторону блока включений целиком** и переписать одиннадцать переехавших заголовков из `"x.h"` в `<lib/vterm/x.h>`. Это построчно эквивалентно применению апстримной правки к нашей стороне, и результат сверен машинно — §4.3.

Переехавшие заголовки, чьё написание менялось:
`cell_extra_store.h`, `mouse_frontend.h`, `mouse_protocol.h`, `parser.h`, `pty.h`, `screen.h`, `term_features.h`, `vterm.h`, `vterm_headless.h`, `vterm_test.h`, `vterm_trace.h`.

Внутри `lib/vterm` написание остаётся локальным (`"pty.h"`), как у апстрима: оба файла в одном каталоге, и `check_includes.py` такую форму разрешает.

### 3.1. Одиннадцать блоков включений — **наша сторона плюс апстримная подстановка**

`bin/main_fuzz/main.cpp`, `lib/shitty/composer_ut.cpp`, `pty_ut.cpp`, `render_metal.mm`, `render_reference.cpp`, `render_reference_ut.cpp` (2 хунка), `render_vk.cpp`, `session_ut.cpp`, `span_shaper_ut.cpp`, `test_mode.cpp` (2 хунка), `vterm_headless_ut.cpp`.

**Чья сторона и почему.** Наша — потому что во всех одиннадцати наш набор включений **строгое надмножество** апстримного: наш форк добавил в эти файлы код (панельные тесты, `grid_geometry`, `render_arena`/`render_blend`/`render_push_constants`, `pane_layout`), и его заголовки апстриму неизвестны. Взятие апстримной стороны выбросило бы их и не собралось бы. Апстримная часть при этом принята **вся**: каждая из его строк присутствует в результате (§4.3, направление `B`).

Порядок блоков после подстановки восстановлен `dev/style.py` — только функцией `reorder_includes`, **без** `clang-format`: локальный clang-format 23.1 переформатирует в этих файлах вещи, которых мерж не касался (`(size_t)(w)*h`, тела лямбд, скобки списков инициализации), и это был бы шум под видом мержа.

### 3.2. `build.py` — **обе стороны**

Один хунк, и он не про переезд: git наложил апстримную строку `vterm_source` на наши две строки `all_libshitty_sources.append(...)` для `ui_quick_hotkey.mm` и `ui_sidebar_tabs.mm`. Взяты обе — апстримный путь `$(S)/lib/vterm/vterm.cpp` и наши два darwin-исходника. Остальные десять правок `build.py` (пути ragel, dfa, `parser.cpp`) git слил сам и правильно.

Полный дифф `build.py` за весь шаг — это ровно одиннадцать апстримных строк плюс два ключа `mouse_geometry_allowance` (§5). `border_pixels_allowance`, `pane_grid_names`, `pane_grid_backends`, `darwin_guard_macros`, `guard_scan_roots` — **не тронуты ни на символ**.

### 3.3. Три файла вне мержа, до которых апстримная замена не дотянулась

Известный сигнал волны 3 повторился. Эти файлы включали переехавшие заголовки, но апстрим их не знает, поэтому и не правил:

| Файл | Что включал | Почему апстрим не дотянулся |
|---|---|---|
| `lib/shitty/pane_layout.h` | `"vterm.h"` | файл нашего форка целиком |
| `lib/shitty/ui_sidebar_tabs_ut.cpp` | `"pty.h"` | файл нашего форка целиком |
| `lib/shitty/session.cpp` | `"cell_extra_store.h"` | строка нашего форка в файле, который апстрим правил в другом месте; git слил без конфликта и оставил недоехавшей |

Найдены не чтением диффа, а сканом всего дерева на `#include "<переехавший заголовок>"` — и это тот приём, который стоит держать в методе: список конфликтных файлов не совпадает со списком файлов, которые мерж решает.

Побочный эффект, названный явно: `ui_sidebar_tabs_ut.cpp` никогда не проходил через `style.py`, поэтому `reorder_includes` переписал ему весь блок (`+17/−19`), а не две строки. Это канонический порядок репозитория, но к мержу отношения не имеет.

### 3.4. Чего в этом мерже **не было**

- **Мины `static_cast` через слушателей** (`M5`, `M6`). Этот коммит — переезд; ни одного изменения типов, сигнатур или списков слушателей в нём нет. Проверено структурно: 21 из 24 переехавших файлов `R100`, три оставшихся `R099` отличаются только порядком включений. `CellExtraClient: Listener` (правка `M6`) на месте, оба обхода `cellExtrasChangedListeners` — апстримный как `Listener*` в `vt_state.cpp:18` и наш как `CellExtraClient*` в `cell_extra_store.cpp:555` — переехали без единого изменения.
- **Ловушки `border_pixels_guard`** из плана `T5.1`. Она не сработала и на этом шаге: `borderPixels()`/`scaledPixels()` остались у `Composer` решением `M6` §3.2, и в `lib/vterm` их нет.

---

## 4. Таблица критериев

| № | Критерий | Результат |
|---|---|---|
| 1 | `./build st --clear` зелёная | **226 узлов**, `EXIT=0`; эталон до мержа снят в этом же дереве — тоже 226 |
| 2 | `./build unit_tests pty_test_helper` + прогон | `EXIT=0`, **`OK: 950`**, список имён совпадает с предмержевым побайтово |
| 3 | Совпадение диффов | **0 и 0** расхождений в обе стороны |
| 4 | Питоновский набор поимённо | `Ran=6399`, красные совпадают с предмержевой головой побайтово |
| 5 | Пять гвардов | четыре сканирующих `EXIT=0`, каждый доказан пробой; **`vterm_boundary` `EXIT=1`** — §5.1 |
| 6 | `python3 lib/vterm/check_includes.py lib/vterm <stamp>` | **не проходит**, 8 строк, 3 файла — §5.1 |
| 7 | `A1`, `A8`, `A9`, `A10` | не нарушены, §4.6 |

### 4.1. Сборка

Эталон, снятый на предмержевой голове `e3614445` в этом же дереве:

```
$ ./build st --clear -j 10 ;  EXIT=0
[AR] {225/226} $(B)/libshitty_prod.a
[LD] {226/226} $(B)/st
```

После мержа:

```
$ ./build st --clear -j 10 ;  EXIT=0
[CC] {223/226} $(B)/obj/libshitty/lib/vterm/vterm.cpp.o
[CC] {224/226} $(B)/obj/libshitty/lib/vterm/parser.cpp.o
[AR] {225/226} $(B)/libshitty_prod.a
[LD] {226/226} $(B)/st
```

Узлов столько же, но узлы **другие**: `lib/shitty/vterm.cpp.o` стал `lib/vterm/vterm.cpp.o`. `--clear` здесь обязателен и не формальность — правка `build.includes` в ключ узла не входит (`CLAUDE.md`), а этот шаг двигает пути включений в 41 файле.

`./build st pt --clear -j 10` — `EXIT=0`, **230 узлов**, `[LD] {230/230} $(B)/st` (`CLAUDE.md`: `pt` собирается отдельной целью и своими проверками).

**Свежесть бинарника, не через систему сборки.** Пути `__FILE__`, вкомпилированные в `st`:

```
$ strings -a .build/st | grep -c "lib/vterm/vterm.cpp"    → 1
$ strings -a .build/st | grep -c "lib/shitty/vterm.cpp"   → 0
$ strings -a .build/st | grep -oE "lib/(shitty|vterm)/(vterm|screen|cell_extra_store|vterm_headless)\.cpp" | sort -u
lib/vterm/cell_extra_store.cpp
lib/vterm/screen.cpp
lib/vterm/vterm.cpp
lib/vterm/vterm_headless.cpp
```

Ни одного старого пути. Слинкованный бинарник — это смерженное дерево, а не штамп из кеша.

### 4.2. Юнит-тесты

```
$ ./build unit_tests pty_test_helper -j 10 ;  EXIT=0
$ SHITTY_PTY_TEST_HELPER="$PWD/.build/pty_test_helper" ./.build/unit_tests --threads=1
EXIT=0
OK: 950
```

Имена сверены с предмержевой головой поимённо: **950 и 950, `diff` пуст**. Без переменной было бы `OK: 945, ERR: 5` — неполное окружение, а не регрессия (`CLAUDE.md`).

### 4.3. Совпадение диффов

Сравнение построчное, машинное, в обе стороны, с учётом переименований (файлы ключуются по пост-образу). `A` — строки, которые апстрим **удалил**, а в смерженном дереве они остались. `B` — строки, которые апстрим **добавил** и которых в смерженном дереве нет.

```
$ python3 diffcmp.py e3614445 HEAD f3de9de6
A. upstream-removed lines still present in the merged tree: 0
B. upstream-added lines absent from the merged tree: 0
```

**Ноль расхождений в обе стороны.** В отличие от `M6`, где три решения давали 71 + 43 расхождения, здесь дерево берёт апстримный коммит дословно: у переезда нет содержательной части, которую можно было бы решить иначе.

Объём, чтобы «ноль» не читался как «сравнивать было нечего»:

```
upstream: 34 пост-образных пути, +98 / −91
ours    : 36 пост-образных путей, +134 / −126
```

Два лишних пути — `pane_layout.h` и `ui_sidebar_tabs_ut.cpp` из §3.3; `session.cpp` в разницу не попал, потому что апстрим правил его тоже. Обратного случая нет: **файлов, которые тронул апстрим и не тронули мы, ноль**.

Структурная сверка переименований — апстримный профиль воспроизведён точно:

```
21 × R100 ;  R099: mouse_frontend.h, parser.cpp, parser.h
```

Дополнительно проверено, что мерж не оставил недоехавших включений: во всех 54 затронутых файлах разобраны все `#include` и найдено, куда каждый разрешается по путям из `build.py:75`. **Все 634 проектных включения разрешаются**; 97 «неразрешённых» — системные и стандартные заголовки (`stdio.h`, `map`, `emmintrin.h`), которых в дереве нет по определению. Это же покрывает `lib/shitty/render_vk.cpp`, который на macOS штатной сборкой не компилируется: мерж изменил в нём только строки включений, и все его проектные включения разрешаются.

### 4.4. Питоновский набор

Прогнан дважды в одном дереве: на предмержевой голове `e3614445` (до мержа) и на `4dd18140`, обе — 20 групп параллельно с полным окружением из `build.py:1131`.

```
before (e3614445): Ran=6399   14 errors + 6 failures, 15 уникальных имён
after  (4dd18140): Ran=6399   14 errors + 6 failures, 15 уникальных имён
diff по именам: пуст
```

Число тестов — эталонные 6399 (6390 уникальных; девять исполняются дважды, `G14`). **Ни одного нового отказа.** Красных на одно имя меньше, чем в эталоне отчёта `M6` (16), и это флак-класс `M6` §5.6, а не эффект мержа: одно и то же имя-«порождатель pty» гуляет между прогонами.

### 4.5. Гварды — §5 целиком

### 4.6. Инварианты

| Инвариант | Чем проверен |
|---|---|
| `A1` — `contentInsets()` единственный источник геометрии раскладки | `border_pixels_guard` зелёный **без единой правки разрешения** (`border_pixels_allowance` в диффе `build.py` отсутствует) и краснеет на пробе в переехавшем файле; зелены `Composer::ResizeCountsTheGridOutOfTheContentInsets`, `ContentInsetsCarryTheBorderOnEverySide`, `ContentInsetsReserveTheSidebarOnTheRightInBackingPixels`, `ContentInsetsReserveTheTitleBarStripOnTopInBackingPixels`, `ReferenceRenderer::PlacesTheGridAtTheContentInsets`, `PlacesTheGridAtInsetsThatDifferOnEveryAxis` |
| `A8` — геометрия панели передаётся, а не читается из окна | `mouse_geometry_guard` зелёный после перекеивания, счётчики `1` и `1` не изменены; краснеет и на лишнем вызове в разрешённом файле, и на первом вызове в неразрешённом; зелены `VtermHeadless::TakesItsGridFromThePaneItWasGiven`, `PointerReportsCountFromTheOriginTheVtermWasGiven`, `MovingThePaneMovesWhereItsPointerReportsCountFrom`, `SelectionStartsInTheCellThePaneOwnsAndNotTheWindows`, `AutoscrollMeasuresFromThePanesTopEdgeAndNotTheWindows` |
| `A9` — рендерер берёт сетку из `TerminalUpdate` | `pane_grid_guard` зелёный; словарь `pane_grid_names` не тронут, написание `composer.vt.columns` из `M6` по-прежнему краснеет пробой; собственная проверка гварда «дошёл ли до трёх бэкендов» проверена сокрытием `render_vk.cpp` — краснеет; зелены `ReferenceRenderer::AFrameWithoutAGridIsRefused`, `DrawsTwoPanesOfDifferentGrids`, `MetalPanes::AZeroGridInOnePaneRefusesTheWholeFrame` |
| `A10` — `chromeInsets`/`paneInsets`/`contentInsets` названы по отдельности | все три метода на месте и не размножились, `composer.{h,cpp}` мерж не трогал; зелены `Composer::EachChromeSideKeepsItsOwnReserveOnItsOwnEdge`, `EveryPixelOfAComposersContentBoxAnswersFromItsOwnSide`, `SessionSet::PanesDivideTheContentBoxAndNotTheWindow` |

---

## 5. Гварды: что проверено пробой

Все пять запускались **напрямую**: четыре сканирующих — извлечением их программ из `build.py` разбором AST (вместе со словарями и разрешениями), `vterm_boundary` — вызовом `lib/vterm/check_includes.py`. Через `./build` доказательства не было бы: кеш адресуется содержимым и подставляет готовый штамп с пустым выводом и `exit 0`.

```
border_pixels_guard    EXIT=0   output bytes=0
mouse_geometry_guard   EXIT=0   output bytes=0
pane_grid_guard        EXIT=0   output bytes=0
darwin_call_guard      EXIT=0   output bytes=0
vterm_boundary         EXIT=1   8 строк, 3 файла
```

**Зелёное без пробы ничего не значит.** Для каждого гварда проверялось не «зелёный ли он», а «видит ли он то, что обязан видеть» — то есть подставлялось нарушение **в файл, переехавший на этом шаге**, и проверялось, что гвард его называет.

| Гвард | Проба | Результат |
|---|---|---|
| `border_pixels` | `c.borderPixels()` в **`lib/vterm/screen.cpp`** | `EXIT=1`, `lib/vterm/screen.cpp:41` |
| `mouse_geometry` | первый `mouseGeometry(c)` в **`lib/vterm/vterm.cpp`** (файл вне разрешения) | `EXIT=1`, `lib/vterm/vterm.cpp:91` |
| `mouse_geometry` | **второй** `mouseGeometry(c)` в `lib/vterm/mouse_frontend.cpp` (сверх разрешения `1`) | `EXIT=1`, `lib/vterm/mouse_frontend.cpp:25` |
| `mouse_geometry` | разрешить оба написания сразу — чтобы заговорила вторая половина гварда | `EXIT=1`, `Unreachable: lib/shitty/mouse_frontend.{h,cpp}` |
| `pane_grid` | `composer.vt.columns` в `render_reference.cpp` | `EXIT=1`, `render_reference.cpp:36` |
| `pane_grid` | спрятать `render_vk.cpp` от скана | `EXIT=1`, `Unreachable: render_vk.cpp` |
| `darwin_call` | незащищённый `createMetalRenderer(...)` в **`lib/vterm/vterm.cpp`** | `EXIT=1`, `lib/vterm/vterm.cpp:91 createMetalRenderer` |
| `vterm_boundary` | `#include <lib/shitty/composer.h>` в **`lib/vterm/screen.h`** | `EXIT=1`, `screen.h:8 … crosses into lib/shitty` |

Три из этих проб адресованы файлам, которых до этого шага в `lib/vterm` не было вовсе (`screen.cpp`, `screen.h`, `vterm.cpp`). То есть доказано не «гвард зелёный», а «гвард дотягивается до переехавшего кода».

### 5.1. `mouse_geometry_guard` покраснел, и это перекеивание, а не расширение

**Покраснел, ровно как предупреждал план.** До правки:

```
mouseGeometry(const Composer&) is the pane that fills the window (A8), which
production no longer gets to assume: pass the pane's origin.
Unallowed uses:
  lib/vterm/mouse_frontend.cpp:24
  lib/vterm/mouse_frontend.h:111
```

Чем именно: `mouse_geometry_allowance` был ключён путями `lib/shitty/mouse_frontend.{h,cpp}`, а файлы теперь в `lib/vterm`. Сработала первая половина гварда («неразрешённый файл»); вторая («ключ, до которого скан не дошёл») ждала за ней — она показана отдельной пробой в таблице выше.

**Что сделано и почему это не расширение.** Два ключа переписаны на `lib/vterm/mouse_frontend.{h,cpp}`, **счётчики оставлены `1` и `1`**. Гвард метит ровно то же, что метил до переезда: одно объявление и одно определение, и ничего больше нигде. Ни один файл в разрешение не добавлен, ни один счётчик не поднят.

Это не самодеятельность: `docs/plans/reviews/upstream-merge-invariants.md` §3.3 предписывает именно эту правку дословно — «Перекеить `mouse_geometry_allowance`: `lib/shitty/mouse_frontend.h` → `lib/vterm/mouse_frontend.h`, то же для `.cpp`. **Счётчики (`1` и `1`) не менять**» — и текст ошибки самого гварда говорит `re-key the allowance onto where these live now`. Запрет `T2.1` касается **расширения** разрешения (новые файлы, поднятые счётчики), и он соблюдён.

Что перекеивание не закрывает: `mouseGeometry(const Composer&)` по-прежнему берёт `Composer` — и это владение `T5.1`. Гвард считает эту форму, а не разрешает её.

### 5.2. `vterm_boundary` красный — восемь строк, три файла, четыре чужие задачи

```
$ python3 lib/vterm/check_includes.py lib/vterm /tmp/vb.stamp ;  EXIT=1
mouse_frontend.h:9:    "composer.h"      does not resolve inside lib/vterm
vterm.cpp:22:          "session.h"       does not resolve inside lib/vterm
vterm.cpp:23:          "composer.h"      does not resolve inside lib/vterm
vterm.cpp:27:          "grid_geometry.h" does not resolve inside lib/vterm
vterm_headless.cpp:11: "options.h"       does not resolve inside lib/vterm
vterm_headless.cpp:12: "composer.h"      does not resolve inside lib/vterm
vterm_headless.cpp:13: "pane_layout.h"   does not resolve inside lib/vterm
vterm_headless.cpp:14: "grid_geometry.h" does not resolve inside lib/vterm
```

**Это не дефект разрешения конфликтов и не забытая правка.** Апстримные версии этих трёх файлов границу не нарушают: у апстрима ядро эмбеддера не называет. Наши — называют, потому что `M6` §3.1/§3.2/§3.4 сознательно оставили за `Composer` `resize()`, `borderPixels()`/`scaledPixels()` и оба `create()`. Переезд не создал это расхождение, он **перенёс его через границу**, где оно впервые стало видимым инструментом.

**Это было предсказано до начала волны.** `docs/plans/reviews/upstream-merge-invariants.md` §4.3, разбирая вариант «А» задачи `T5.1`, пишет прямым текстом: `mouseGeometry()` берёт `const VtGeometry&` и перестаёт тянуть `composer.h` в ядро — «**это заодно закрывает одно из четырёх нарушений границы включений, которые иначе валят `vterm_boundary`**». То есть чек-лист ревью заранее считал, что между переездом и задачами волны 6 гвард красный, и назначил его закрытие им.

Дерево при этом **собирается**: `build.includes` содержит `$(S)/lib/shitty` (`build.py:75`), поэтому `#include "composer.h"` из `lib/vterm` разрешается компилятором. Красный здесь — аудит, а не сборка; `Р1` («каждый шаг обязан оставить дерево собирающимся») выполнен.

**Разбивка по владельцам, чтобы это не осталось ничьим:**

| Строки | Что мешает | Чья задача |
|---|---|---|
| `mouse_frontend.h:9` | `mouseGeometry(const Composer&)` и `Insets` в `MouseGeometry` | **`T5.1`** (владеет `lib/vterm/mouse_frontend.{h,cpp}`) |
| `vterm.cpp:23`, `vterm.cpp:22` | `VtermImpl::composer` и четыре места, названные `M6` §3.4 | **`T5.1`** → **`T5.5`**, `session.h` — **`T5.4`** |
| `vterm.cpp:27`, `vterm_headless.cpp:14` | `grid_geometry.h` не расщеплён | **`T5.2`** |
| `vterm_headless.cpp:11–13` | наш `VtermHeadless::create(Composer&)` и `windowPane(composer)` | **`T5.9`** («взять апстримное целиком, наши 17 строк выбросить») |

**Что я сознательно не сделал.** Ни одна из трёх дорог к зелёному гварду не была моей:

1. Расширить `check_includes.py` списком исключений — ослабление гварда, прямо против `T2.1`.
2. Разорвать зависимость по месту — это и есть тела `T5.1` (проектное решение и **точка остановки за человеком**), `T5.2`, `T5.4`, `T5.9`.
3. Не переносить эти три файла, оставив их в `lib/shitty` до `T5.1`. Технически работает и даёт полностью зелёное дерево, но делает мерж-шаг наполовину фиктивным: `vterm.cpp` — это и есть «VT core» из заголовка коммита, а `T5.1` по плану владеет `lib/vterm/mouse_frontend.{h,cpp}`, то есть ожидает файлы уже там. **Если командиру нужна полностью зелёная линия до `T5.1`, это готовый откат: три файла назад в `lib/shitty`, `vterm_source` в `build.py` назад, ключи `mouse_geometry_allowance` назад — и все пять гвардов зелены. Решение за владельцем плана, потому что оно меняет смысл шага, а не его корректность.**

---

## 6. Обнаружено

### 6.1. Апстримный коммит механичен на 100%, и это проверяется одной командой

У `M6` §5.5 записано «97% механики». Здесь — все 100: `-U0`-дифф по двенадцати конфликтующим файлам, очищенный от `#include` и пустых строк, **пуст**. Это стоит записать в метод как первый шаг любого мерж-шага-переезда: если фильтр даёт пусто, разрешение конфликтов сводится к подстановке имён на нашей стороне, а 15 хунков можно не читать по одному.

### 6.2. `git` находит переименования и на нашей стороне тоже

Все 24 файла приехали в `lib/vterm` **автослиянием**, вместе с нашими правками поверх (в `vterm.cpp` их 471 строка расхождения с апстримом, в `vterm.h` — 112). Ни один из 24 не потребовал ручного переноса. Это заметно расходится с ожиданием «переезд 54 файлов — много ручной работы» и объясняется тем, что переименования у апстрима чистые (`R100`): git видит их и накладывает наш дифф на новый путь сам.

Обратная сторона того же: **автослияние молча решает и то, чего в списке конфликтов нет**. `lib/shitty/session.cpp` и `lib/shitty/pty.cpp` слиты без конфликта, но первый остался с недоехавшим `"cell_extra_store.h"`. Ловится не диффом, а сканом всего дерева.

### 6.3. `dev/style.py` нельзя звать целиком: локальный clang-format переформатирует чужое

`style.py` делает три вещи: `reorder_includes`, `clang-format -i` и `restore_constructor_braces`. Локальный clang-format 23.1 в этом дереве **не идемпотентен относительно текущего форматирования**: на четырнадцати файлах, которых мерж коснулся только по включениям, он переписал тела лямбд, скобки списков инициализации и убрал пробелы вокруг `*` в `(size_t)(w) * h`. Правильный вызов для мерж-шага — только `reorder_includes`, как здесь. Стоит попасть в брифы: иначе мерж-коммит несёт десятки строк, к мержу не относящихся, и следующий мерж на них конфликтует.

### 6.4. Два гварда имеют собственную проверку «дошёл ли скан», и обе работают

`mouse_geometry_guard` умеет сказать «ключ, до которого скан не дошёл», `pane_grid_guard` — «бэкенд, до которого скан не дошёл». Обе проверены пробой (§5) и обе сработали. Это ровно тот механизм, которого не хватало `pane_grid_names` на `M6`: словарь имён такой самопроверки не имеет, и его слепота ловится только пробой на новом написании. Заметка для `T6.1`: если у словаря нет самопроверки, у него должна быть проба в отчёте того шага, который переименование принёс.

### 6.5. Одна из задач волны закрыта самим мержем

`T5.7` («`childPid()` в `lib/vterm/pty.h`») больше нечего делать: `pty.h` переехал целиком, вместе с нашим `childPid()` и его комментарием. Итоговый файл — апстримный побайтово плюс ровно две наши вставки (`childPid()` и `size` в `Pty::spawn`). Подробности — §7.

---

## 7. Что осталось задачам `T5.1`–`T5.11` — оценено после мержа

Оценка снята **по смерженному дереву**. Отсчёт — от таблицы `M6` §6.2, где три задачи уже сократились.

### 7.1. Что изменилось по зависимостям

`M6` §6.1 показал, что ни одна из одиннадцати задач не могла начаться на `M6`, потому что каждая владеет файлом, которого ещё нет. `M6b` привёл **пять** из них:

```
$ git ls-tree -r --name-only HEAD lib/vterm | grep -E "pty.h|vterm.cpp|cell_extra|mouse_frontend"
lib/vterm/cell_extra_store.cpp
lib/vterm/cell_extra_store.h
lib/vterm/mouse_frontend.cpp
lib/vterm/mouse_frontend.h
lib/vterm/pty.h
lib/vterm/vterm.cpp
```

| Задача | Владеет | Было | Стало |
|---|---|---|---|
| `T5.1` | `lib/vterm/mouse_frontend.{h,cpp}`, `vt_geometry.{h,cpp}` | оба файла отсутствуют | **половина файлов пришла**; `vt_geometry.*` ждут `M6c` |
| `T5.3` | `lib/vterm/cell_extra_store.{h,cpp}` | отсутствуют | **пришли** |
| `T5.5`, `T5.6` | `lib/vterm/vterm.cpp` | отсутствует | **пришёл** |
| `T5.7` | `lib/vterm/pty.h` | отсутствует | **пришёл, и задача выполнена** — §7.2 |
| `T5.9` | `lib/vterm/vt_headless.*` | отсутствуют | без изменений, ждут `M6e` |

**Начинать сейчас можно `T5.7` (нечего делать) и, после снятия блокировки человеком, `T5.1`.** Остальные по-прежнему ждут `M6c`–`M6e` или голову цепочки.

### 7.2. `T5.7` — закрыта мержем

Плановое содержание: «`childPid()` в `lib/vterm/pty.h`». `f3de9de6` переносит `pty.h` целиком (`R100`), наш `childPid()` уехал вместе с ним:

```
$ git diff f3de9de6 HEAD -- lib/vterm/pty.h
+    // The child behind this handle, so an owner which reaps it waits for ...
+    virtual pid_t childPid() { return -1; }
-    virtual PtyHandle* spawn(stl::ObjPool& owner, const LaunchCommand& command) = 0;
+    virtual PtyHandle* spawn(stl::ObjPool& owner, const LaunchCommand& command, const PtySize& size) = 0;
```

Два наших отличия от апстрима, оба преднамеренные и оба на месте. `lib/shitty/pty.cpp` (`forkpty`) остался в `lib/shitty` и включает `<lib/vterm/pty.h>` — сам апстрим так и задумал. `pty_ut.cpp` переписан мержем. `tst/test_pty*.py` зелены (§4.4).

Остаток, который **не относится к `T5.7`**: `origin/master` держит и `lib/vterm/pty.{h,cpp}`, и `lib/shitty/pty.{h,cpp}` — второе расщепление приносит коммит позже `f3de9de6`, и оно приедет мерж-шагом, а не задачей.

### 7.3. `T5.3` — от «половины» к «файлы на месте, ждём переименования»

`M6` сделал половину: список уже `VtState::cellExtrasChangedListeners`, `CellExtraStore::create()` уже принимает `VtState&`, `CellExtraClient` уже `Listener`. `M6b` привёл файлы в `lib/vterm` побайтово. **Осталась только формулировка `VtCellExtras`** — переименование, которое приносит `bd86ed38` (`M6c`). После `M6c` задача сводится к сверке, что дедуп корней и `cellCapacity()` пережили переименование, плюс незакрытый инвариант из `M6` §5.2 (второй обход списка приведением к `CellExtraClient*` ничем не проверяется).

### 7.4. `T5.1` — объём не изменился, но появился измеренный входной материал

Проектное решение и точка остановки за человеком — как были. Что добавилось после `M6b`:

- **Файлы на месте.** `lib/vterm/mouse_frontend.{h,cpp}` — половина владения задачи.
- **Один из восьми красных `vterm_boundary` теперь принадлежит ей поимённо** (`mouse_frontend.h:9`), что и предсказывал чек-листу §4.3.
- **Ловушка `border_pixels_guard` не сработала и здесь.** План ждал её на переезде `VtGeometry`; `M6` §3.2 показал, что она реальна, но нашим решением закрыта — `borderPixels()`/`scaledPixels()` в `lib/vterm` нет ни одного, разрешение не тронуто. Для `T5.1` это значит: ловушка ждёт её собственного решения (если оно заведёт метод в ядре), а не наследуется от мержей.
- **Перекеенное разрешение — теперь её измеритель.** После `4dd18140` любая новая `mouseGeometry(composer)` в ядре краснеет с первого вызова (проба §5), то есть `T5.1` работает под гвардом, а не вслепую.

### 7.5. Остальные

| Задача | Состояние после `M6b` |
|---|---|
| `T5.2` | Без изменений в объёме; `grid_geometry.h` мерж не тронул. **Приобрела двух красных** `vterm_boundary` (`vterm.cpp:27`, `vterm_headless.cpp:14`) — это же и есть измеримый критерий её приёмки |
| `T5.4` | Без изменений; предупреждение про `A5` и `refocus()` остаётся в силе. Приобрела одного красного (`vterm.cpp:22`) |
| `T5.5`, `T5.6` | Файл появился, зависимость от `T5.1` не снята; объём (65 хунков) не изменился |
| `T5.8` | Без изменений после `M6` (треть сделана): `composer.{h,cpp}` этот мерж не трогал вовсе |
| `T5.9` | Без изменений, ждёт `M6e`. Приобрела трёх красных (`vterm_headless.cpp:11–13`) |
| `T5.10` | Без изменений; приём `M6` §3.7 (`merge-file` на переименованных сторонах) применим |
| `T5.11` | Без изменений после `M6` (половина сделана); `bin/main_fuzz/main.cpp` этот мерж тронул только по включениям |

### 7.6. Одной строкой

Одна задача из одиннадцати **закрыта мержем** (`T5.7`), у одной снята файловая блокировка наполовину (`T5.1`), у одной остался только чужой переезд (`T5.3` ждёт `M6c`), три приобрели поимённые красные строки `vterm_boundary` как критерий приёмки (`T5.2`, `T5.4`, `T5.9`), остальные не изменились.

---

## 8. Воспроизведение

```sh
cd <worktree на wave/upstream-merge-w6b>

./build st pt --clear -j 10
./build unit_tests pty_test_helper -j 10
SHITTY_PTY_TEST_HELPER="$PWD/.build/pty_test_helper" ./.build/unit_tests --threads=1   # OK: 950

# гварды — напрямую, минуя ./build: программы извлекаются из build.py разбором AST
#   (border_pixels/mouse_geometry/pane_grid/darwin_call — строки *_guard_program)
python3 lib/vterm/check_includes.py lib/vterm /tmp/vb.stamp     # EXIT=1, §5.2

# питоновский набор, полное окружение из build.py:1131
./build st_test pt_test toml_dump -j 10
for g in $(seq 0 19); do
  SHITTY_TEST_BINARY="$PWD/.build/st_test" \
  SHITTY_PRETTY_TEST_BINARY="$PWD/.build/pt_test" \
  SHITTY_TOML_DUMP_BINARY="$PWD/.build/toml_dump" \
  SHITTY_TEST_FONTCONFIG=0 SHITTY_TEST_PLATFORM=cocoa \
  SHITTY_TEST_VERSION="$(python3 -c 'from datetime import date; print(date.today().strftime("%Y.%m.%d"))')" \
  python3 tst/run_unittest_group.py --group=$g --group-count=20 &
done; wait

# структурная сверка с апстримом
diff <(git ls-tree -r --name-only f3de9de6 lib/vterm) <(git ls-tree -r --name-only HEAD lib/vterm)
git diff -M --name-status e3614445 HEAD | grep '^R'
```

Сравнение диффов §4.3 — сопоставление множеств `+`/`-` строк по каждому пост-образному пути между `git diff -M -U0 e3614445 HEAD` и `git diff -M -U0 f3de9de6~1 f3de9de6`; ожидаемый результат — `0` и `0`.

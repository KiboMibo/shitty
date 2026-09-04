# `M7b` — порт Metal на span shaper: апстрим пришёл туда, где мы уже стояли

Шаг: `git merge 44d61bfc`, «Port the Metal renderer to the span shaper,
instrument the ligature test». Ветка `wave/upstream-merge-w7b` от `master`
(`6edd62c6`). Мерж-коммит — `54732a8a`.

**Короткий ответ на главный вопрос шага: многопанельность цела.** Апстримная
версия ничего не отменила, потому что во всех одиннадцати конфликтах взята наша
сторона — она строго шире апстримной в каждом. Доказано не сборкой, а тремя
мутациями (§4).

---

## 1. Что принёс апстрим

Коммит делает две несвязанные вещи.

**`lib/shitty/render_metal.mm` (+20/−17).** Снимает `render_metal.mm` со
Screen-API спанов, которую расщепление span-шейпера убрало: `Screen::rowSpans`,
`Screen::shapeCells`, `Screen::spanGeneration`, `Screen::spanMask*`,
`Screen::spanColor*` заменяются на те же методы `SpanShaper`, а от `Screen`
остаётся только `viewRow(row)`, дающий `ScreenRowRef{cells, id}`. Сообщение
коммита прямо говорит, зачем: «every Darwin build since then failed» — у
апстрима это была починка сломанной сборки, а не улучшение.

**`tst/test_wide_ligature_overflow.py` (+27/−14).** Инструментовка: три
утверждения теста начинают нести не голый профиль чернил, а пару
`(profile, diagnostics)`, где `diagnostics` — метрики шрифта из `load_font()`,
первые шесть клеток снимка (`U+XXXX` + флаг двойной ширины) и размер картинки.
Смысл — чтобы в логе CI было видно, **на каком этапе** потерялась лигатура:
сетка, шрифт или растеризатор.

Итого 47 вставок, 31 удаление, два файла. `vt_headless` шаг не трогает —
конфликта «изменён/удалён», обещанного пунктом 1 хвоста задачи «А», здесь нет;
он придёт на `M8` с `73cd2b78`.

---

## 2. Каждый конфликт: чья сторона и почему

`tst/test_wide_ligature_overflow.py` слился без конфликта: мы этот файл не
трогали ни разу (`git diff --numstat 60562f22 HEAD -- tst/test_wide_ligature_overflow.py`
пуст), апстримная версия принята целиком.

`lib/shitty/render_metal.mm` дал **одиннадцать** конфликтов. Наша сторона против
merge-base — `568/202`; волна 5 (`M5`, `T4.1`) переписала здесь ровно те же
функции и по той же причине, только зная про панели.

**Во всех одиннадцати взята наша сторона.** Ниже — построчная сверка: что
апстрим добавляет и где это уже стоит у нас.

| № | Место | Что делает апстрим | Что стоит у нас | Решение |
|---|---|---|---|---|
| 1 | включения, :16 | `+#include "span_shaper.h"` | **уже есть**, `:15` — слилось вне конфликта; конфликтует только соседний блок наших `render_arena.h`/`render_blend.h`/`render_push_constants.h` | наша: апстримная строка уже применена, свои три нужны |
| 2 | объявления, :182 | шесть сигнатур на `SpanShaper&`, `assignStrips(update)` | те же шесть, но с `u16 columns` и `size_t cellOffset`, плюс `assignPaneStrips` и `assignFrameStrips` вместо `assignStrips` | наша: апстримные сигнатуры знают одну панель на окно |
| 3 | `uploadArenas`, :424 | `uploadArenas(SpanShaper&, u32)`, счётчики `maskArenaUploaded`/`colorArenaUploaded` | `uploadArenas(u32)`, внутри `SpanShaper& shaper = *composer.shaper`, зеркала `maskMirror`/`colorMirror` (`ArenaCopy plan()`), обработка `composer.shaper == nullptr` | наша: тот же переход на шейпер плюс контракт `render_arena.h` (`A6-5`) |
| 4 | тело `uploadArenas`, :470 | копия хвоста по счётчику | план копии по зеркалу + `waitFrames()` при копии с нуля + сброс обоих зеркал на любом отказе | наша |
| 5 | `assignRowStrips` заголовок, :525 | `(Screen&, SpanShaper&, u16 row)`, `rowIndex = row*cellColumns` | `(Screen&, u16 columns, u16 row, size_t cellOffset)`, `rowIndex = cellOffset + row*columns` | наша: `cellColumns` — сетка **окна**, у нас её нет |
| 6 | `assignRowStrips` тело, :538 | `shaper.rowSpans(rowRef.cells, cellColumns, rowRef.id, …)` | `composer.shaper->rowSpans(rowRef.cells, columns, rowRef.id, …)` | наша: **та же строка**, ширина панели вместо ширины окна |
| 7 | `overrideOverlayStrips` заголовок, :548 | `(SpanShaper&, const TerminalUpdate&)` — **`Screen&` убран** | `(Screen&, u16, const TerminalUpdate&, size_t)` | наша **плюс** снятие `Screen&` — см. §3 |
| 8 | `overrideOverlayStrips` тело, :564 | `shaper.shapeCells(…)` | `composer.shaper->shapeCells(…)` | наша: та же строка |
| 9 | `assignPaneStrips`, :576 | `SpanShaper& shaper = *composer.shaper;` | `const u16 columns = update.gridColumns;` (`A9`) | наша: доступ к шейперу у нас через `composer.shaper` в местах использования |
| 10 | цикл поколения, :607 | `do { generation = shaper.spanGeneration(); for row … } while (…)` — по **одной панели** | тот же цикл, но обходящий **все панели кадра** (`assignFrameStrips`) | наша: цикл по кадру — инвариант `A3`, см. мутацию `M-b` |
| 11 | `updateOnce`/`buildCellUpdates`, :1191 | `assignStrips(update)` + `uploadArenas(*composer.shaper, generation)` для одной панели | цикл по панелям с `cellOffset`, `uploadArenas(assignFrameStrips(frame, count))` | наша |

**Ни одного расхождения, отнесённого к «взяли апстрим».** Причина одна и та же
во всех одиннадцати строках, и её удобно проверить механически:

```
$ git show HEAD~1:lib/shitty/render_metal.mm |
      grep -n 'shapes\.\(rowSpans\|shapeCells\|spanGeneration\|spanMask\|spanColor\)'
(пусто)
```

То есть Screen-API спанов, которую апстрим этим коммитом снимает, у нас уже не
вызывалась — волна 5 сняла её первой. Апстримный порт для нас **пустой по
существу**: он приводит к состоянию, в котором мы находимся с волны 5.

Второй механический признак того же:

```
$ grep -n 'cellColumns\|cellRows' lib/shitty/render_metal.mm
(пусто)
```

`cellColumns`/`cellRows` — сетка окна, из которой апстримная версия берёт ширину
строки и число строк. В нашем файле этих полей **нет вовсе**: их убрала волна 5,
заменив на `update.gridColumns`/`update.gridRows` панели. Апстримная сторона
любого из конфликтов 5, 6, 10 у нас просто не скомпилировалась бы.

---

## 3. Единственное, что взято у апстрима по существу

Конфликт 7. `overrideOverlayStrips` у нас принимает `Screen& shapes`, и этот
параметр **не используется** — оверлей спрашивает `composer.shaper->shapeCells`
напрямую, `shapes` в теле не встречается ни разу. Апстрим этим же коммитом
параметр убрал.

Правка принята: три места (`:183` объявление, `:503` определение, `:533` вызов),
`3/3`. Это единственная строка диффа мержа поверх нашей стороны:

```
$ git diff HEAD~1 HEAD -- lib/shitty/render_metal.mm --numstat
3	3	lib/shitty/render_metal.mm
```

Ход выбран сознательно: он **уменьшает** расхождение с апстримом, а не углубляет
его, и убирает мёртвый аргумент, который иначе пережил бы ещё один мерж.

---

## 4. Не отменила ли апстримная версия нашу многопанельность

Отдельный раздел, потому что это главный содержательный вопрос шага, и «дерево
собралось» на него не отвечает.

### 4.1. Что проверялось

Три вещи, все — следствия многопанельности, которых у апстрима нет:

- **`A9`** — рендерер берёт сетку у панели (`update.gridColumns/gridRows`), а не
  у окна;
- **смещение панели** — стрипы панели `N` ложатся по `cellOffset` этой панели;
- **`A3`** — цикл поколения шейпера закрывается над **кадром**, а не над одной
  панелью: у окна одна арена, и шейпинг поздней панели может собрать арену, в
  которую уже указывают стрипы ранней.

### 4.2. Наблюдатели названы поимённо и зелены до мутации

Девять тестов `MetalPanes` в `unit_tests`, все `+` в прогоне на итоговом дереве:

```
$ sed 's/\x1b\[[0-9;]*m//g' <лог> | grep -c '^+ MetalPanes'
9
```

```
+ MetalPanes::APaneKeepsItsInkWhenALaterPaneCollectsTheArena
+ MetalPanes::APartiallyDamagedPaneKeepsItsOwnRows
+ MetalPanes::ATranslucentBackgroundReachesTheTextureMultipliedDown
+ MetalPanes::AZeroGridInOnePaneRefusesTheWholeFrame
+ MetalPanes::DrawThreeGridsInOneFrame
+ MetalPanes::EachPanesPaddingIsItsOwnBackgroundAndNotItsNeighbours
+ MetalPanes::EveryMarkThatIsNotBackgroundStaysSolidOnTheShader
+ MetalPanes::SwappedPanesAreAReshape
+ MetalPanes::TheSolidMarksReachTheTextureUnfadedAtAnyOpacity
```

Плюс три оракула волны 5 в `tst/test_gpu_parity.py`, все зелёные в общем прогоне:
`test_scene_parity`, `test_a_split_frame_is_the_same_picture_on_both_backends`,
`test_a_collection_between_panes_is_the_same_picture` — последний заведён именно
под `A3` (комментарий файла, строки 217–281).

### 4.3. Три мутации

Каждая — возврат к апстримной, однопанельной форме. Сборка `./build unit_tests`,
прогон `SHITTY_PTY_TEST_HELPER=… ./.build/unit_tests --threads=1 < /dev/null`.

| Мутация | Что подменено | Результат | Поймали |
|---|---|---|---|
| `M-a` | `assignPaneStrips(update, panes[index].cellOffset)` → `assignPaneStrips(update, 0)` — все панели пишут стрипы в начало буфера | `OK: 965, ERR: 2`, `EXIT=2` | `MetalPanes::APaneKeepsItsInkWhenALaterPaneCollectsTheArena`, `MetalPanes::DrawThreeGridsInOneFrame` |
| `M-b` | цикл поколения сужен с кадра до панели (`do/while` внесён внутрь цикла по панелям) | `OK: 966, ERR: 1`, `EXIT=1` | `MetalPanes::APaneKeepsItsInkWhenALaterPaneCollectsTheArena` |
| `M-c` | `rowIndex` оверлея теряет `cellOffset` панели | `OK: 967`, `EXIT=0` | **никто** |

`M-a` и `M-b` — прямое доказательство: многопанельность цела и охраняется. Если
бы конфликт был разрешён в апстримную сторону, обе мутации были бы не мутациями,
а состоянием дерева, и обе краснеют.

`M-c` — находка, см. §7.

---

## 5. Таблица критериев

| # | Критерий | Итог |
|---|---|---|
| 1 | `./build st --clear` зелёная, 229 узлов | **закрыт** |
| 2 | `unit_tests` ≥ `OK: 967`, `EXIT=0` | **закрыт**: `OK: 967` |
| 3 | построчная сверка диффов | **закрыт**: §2, одиннадцать строк таблицы |
| 4 | многопанельность Metal цела | **закрыт**: §4, две мутации из трёх пойманы поимённо |
| 5 | питоновский набор поимённо | **закрыт**: `Ran 6437`, 20 красных, `skipped=17`; `diff` с эталоном пуст |
| 6 | пять гвардов, четыре доказаны пробой кодом, `vterm_boundary` с пустым разрешением | **закрыт** |
| 7 | `./build example` + 37 тестов | **закрыт**: `EXIT=0`, 37 `ok`, 0 `skipped` |
| 8 | сломанных целей две, третьей нет | **закрыт** |

### Критерий 1

```
$ ./build st --clear
EXIT=0
[LD] {229/229} $(B)/st
$ grep -c '^\[' <лог>
229
```

### Критерий 2

```
$ ./build unit_tests pty_test_helper
EXIT=0
$ SHITTY_PTY_TEST_HELPER="$PWD/.build/pty_test_helper" \
      ./.build/unit_tests --threads=1 < /dev/null
EXIT=0
OK: 967
```

Без `-k`, с перенаправленным stdin (`EXIT=148` — `SIGTSTP`, а не падение).
Число совпадает с эталоном задачи «А»: шаг ничего не прибавил и ничего не убрал.

### Критерий 5

Режим — одним процессом, `--group=0 --group-count=1`, окружение из
`build.py:1274-1283` (`SHITTY_TEST_FONTCONFIG=0`: `optional_pkg()` выключает
fontconfig на darwin вне immutable store). **Эталон снят тем же режимом на том же
дереве**, `git checkout --detach 6edd62c6`, полной пересборкой `st_test pt_test
toml_dump example`.

```
эталон (6edd62c6):  Ran 6437 tests in 128.784s
                    FAILED (failures=6, errors=14, skipped=17, expected failures=549)
итог (54732a8a):    Ran 6437 tests in 137.956s
                    FAILED (failures=6, errors=14, skipped=17, expected failures=549)

$ diff <эталон: 20 красных> <итог: 20 красных>
(пусто)   — СПИСОК КРАСНЫХ ПОБАЙТОВО ТОТ ЖЕ
```

Двадцать красных — унаследованные средовые (`fontconfig`/`freetype`/`harfbuzz`
выключены на macOS): `test_bitmap_font_render` ×9, `test_synthesized_symbols` ×2,
`test_color_font_render`, `test_font_resolver` ×2, `test_soft_render` ×2,
`test_contour_input_generator` ×2, `test_ghostty_key_encoding_tail`,
`test_italic_overhang`. Симлинк `.build/st_test` перед обоими прогонами живой
(иначе набор ответил бы `Ran 6065 … errors=6709` и не сказал бы, почему).

**Три теста изменённого файла зелёные с обеих сторон:**

```
test_a_differently_painted_blank_bounds_the_capture … ok
test_a_neighbor_bounds_the_capture … ok
test_bismillah_ink_reaches_past_the_first_blank … ok
```

### Критерий 6

Пять гвардов на итоговом дереве:

```
$ ./build border_pixels_guard    ; EXIT=0
$ ./build mouse_geometry_guard   ; EXIT=0
$ ./build pane_grid_guard        ; EXIT=0
$ ./build darwin_call_guard      ; EXIT=0
$ ./build vterm_boundary         ; EXIT=0
$ python3 lib/vterm/check_includes.py lib/vterm <stamp>   ; EXIT=0
$ grep -n 'ALLOWANCE' lib/vterm/check_includes.py
73:ALLOWANCE = {}
```

`vterm_boundary` прогнан **и напрямую, минуя `./build`** — штамп из CAS не
доказывает исполнения. Разрешение пусто: шесть из шести переходов закрыты
задачей «А», новых нарушений ноль.

Четыре сканирующих доказаны пробой **кодом** (не комментарием: `guard_source_reader`
заменяет тела комментариев и строк пробелами, так что проба-комментарий зеленит
все четыре сразу). Каждая проба ставилась **по одной**, файл восстанавливался
между пробами.

| Гвард | Проба (файл, код) | С пробой | Гвард назвал |
|---|---|---|---|
| `border_pixels_guard` | `render_metal.mm`: `const u32 probe = composer.geometry.borderPixels();` | `EXIT=1` | `lib/shitty/render_metal.mm:478` |
| `pane_grid_guard` | `render_metal.mm`: `const u16 probe = composer.geometry.columns;` | `EXIT=1` | `lib/shitty/render_metal.mm:478` |
| `mouse_geometry_guard` | `render_metal.mm`: `const auto probe = mouseGeometry(composer);` | `EXIT=1` | `lib/shitty/render_metal.mm:478` |
| `darwin_call_guard` | `render.cpp`: `return createMetalRenderer(composer, pool, context);` вне `#if HAVE_METAL_RENDERER` | `EXIT=1` | `lib/shitty/render.cpp:31  createMetalRenderer` |

`render_metal.mm` подходит трём из четырёх: он в `lib/shitty`, суффикс `.mm` в
`guard_scan_suffixes`, имя начинается на `render` (`pane_grid_guard` читает
только такие) и не оканчивается на `_ut.cpp` (`mouse_geometry_guard` такие
пропускает). `darwin_call_guard` по построению не читает `.mm` как место вызова,
поэтому его проба — в `render.cpp`; гвард вдобавок напечатал отслеживаемое
множество: `applyQuickFrameToWindow createCsdTabsUi createMetalRenderer
createQuickHotkey createSidebarTabsUi`. После снятия пробы — `EXIT=0`,
`git diff` пуст.

### Критерий 7

```
$ ./build example                                    ; EXIT=0
$ grep -c 'test_embed_example' <лог набора>          → 37
$ grep -c 'test_embed_example.*skipped' <лог набора> → 0
$ grep -c 'test_embed_example.* \.\.\. ok' <лог>     → 37
```

Ловушка задачи «А» (пропуск по наличию артефакта выглядит как `OK`) проверена
тем же способом: `skipped` ноль, значит все 37 **исполнены**, а не пропущены.

### Критерий 8

```
$ ./build -k st pt st_memprofile st_test pt_test main_fuzz st_test_prod_parser \
           pt_test_prod_parser pty_test_helper unit_tests toml_dump parser_perf \
           core_perf example                                       ; EXIT=1
FAIL $(B)/obj/st_memprofile/lib/shitty/heap_profile.cpp.o
FAIL $(B)/main_fuzz
build: 3 node(s) failed, 2 requested target(s) broken

$ grep ' error: ' <лог> | sort -u
lib/shitty/heap_profile.cpp:18:10: fatal error: 'gperftools/heap-profiler.h' file not found
clang++: error: linker command failed with exit code 1 (use -v to see invocation)
```

Те же две, обе известные и обе средовые/доволновые. Третьей нет. Ошибок
компиляции, кроме отсутствующего заголовка gperftools, ноль. Список целей — тот
же, каким его снимали `M7` и «А», чтобы числа были сравнимы.

---

## 6. Зависит ли инструментовка теста от `3c91f408`

**Нет, и это проверено, а не предположено.** Апстримная версия требует от
харнесса трёх вещей, все три есть у нас до шага:

| Что нужно тесту | Где у нас | Форма |
|---|---|---|
| `load_font()` возвращает метрики | `tst/harness.py:335-344` | `dict` с ключами `px py bold italic bold_italic` |
| `terminal.snapshot()` | `tst/harness.py:1234` | есть |
| `cell.char`, `cell.double_width` | `tst/harness.py:63-66`, заполняются на `:1332-1334` | есть |

Все три теста файла проходят на этой машине с обеих сторон мержа (§5). Тянуть
`3c91f408` не понадобилось и не следует: он в `M8`.

Оговорка: **зелёность здесь ничего не говорит про Linux.** Апстрим чинил
`3c91f408` именно средовой отказ («all-zero ink profile on every CI distribution
while passing locally»), гвард «глиф-бомбы» во FreeType 2.14.3. Инструментовка
этого шага отказ не чинит — она делает его читаемым в логе. На macOS профиль
чернил ненулевой, поэтому здесь виден только тот факт, что новый код
диагностики исполняется и не ломает утверждения.

---

## Обнаружено

**1. Смещение панели у стрипов IME-оверлея не охраняется никем.** Мутация `M-c`
(`rowIndex` оверлея считается без `cellOffset`, то есть предпросмотр ввода на
панели `N` кладёт свои стрипы на панель 0) переживает:

- `unit_tests` — `OK: 967`, `EXIT=0`, ни одного красного;
- `tst/test_preedit.py` — 6 из 6 `ok`;
- `tst/test_gpu_parity.py` — 3 из 3 `ok`, включая оба многопанельных теста.

Причина видна из устройства наблюдателей: `test_preedit.py` смотрит снимок
модели, а не картинку, и стрипы Metal до него не доходят вовсе; `test_gpu_parity`
рисует многопанельные кадры, но **без предпросмотра ввода**; а во всём
`render_reference_ut.cpp` оверлей ставит **ровно один** тест —
`ReferenceRenderer::PreeditOverlayCoversUnderlyingStrips` (`:642`), и он строит
`ScreenFixture(4, 1)`, то есть одну панель, где `cellOffset` равен нулю и
слагаемое неотличимо от своего отсутствия. В суите `MetalPanes` слова `overlay`
нет ни разу. Это ровно та же схема, что у семи вырожденных фикстур мержа:
параметр, обнуляющий проверяемое, стоит по умолчанию.

Соседние две трети той же функции охраняются (`M-a` краснит два теста), так что
дыра узкая: **только оверлей**. Стоимость закрытия — один тест, ставящий оверлей
на вторую панель непустого кадра.

**2. Апстримный порт для нас пустой, и это первый такой шаг за мерж.** Все
предыдущие шаги приносили либо конфликт по существу, либо решение владельца
форка. Здесь апстрим доехал до состояния, в котором мы находимся с волны 5, и
единственная строка, которую мы у него взяли, — снятие мёртвого аргумента.
Расхождение по этому файлу шаг **сократил**, а не углубил.

**3. `git checkout -- <файл>` посреди мержа молча не работает.** В незакрытом
мерже файл с конфликтом числится `UU`, и `git checkout -- lib/shitty/render_metal.mm`
отвечает `error: path ... is unmerged`, ничего не восстановив. Первый заход на
пробы гвардов из-за этого накопил три пробы в одном файле вместо трёх
независимых прогонов; отказ виден в выводе, но команда, стоящая в цикле после
`./build`, легко теряется. Восстанавливать в этом состоянии — копией сохранённого
файла, а не `git checkout`. (Пробы переставлены заново по одной, таблица §6
снята со второго, чистого захода.)

---

## Что осталось другим задачам

**`T6.1`** (владеет `build.py`, идёт параллельно) — свести гварды,
`vterm_boundary`, `vterm_source` и сборку `lib/embed`. Шаг `M7b` `build.py` не
трогал ни разу, конфликта с `T6.1` не создаёт. Отмечу для неё: четыре
сканирующих гварда и `vterm_boundary` на этом дереве зелёные и доказанно
краснеют, `guard_scan_roots` = `("lib/shitty", "lib/vterm", "ext/plt", "bin")`,
`guard_scan_suffixes` = `(".cpp", ".h", ".mm")`.

**`M8`** (`origin/master`, остаток) — там же `T6.2`–`T6.4`. Передаю три вещи:

1. **Конфликт «изменён/удалён» по `lib/vterm/vt_headless.{h,cpp}` встанет на
   `M8`**, с `73cd2b78`, а не здесь — на `M7b` этих файлов апстрим не касается,
   проверено. Пункт 1 хвоста задачи «А» («решать до `M7b`») по факту относится к
   `M8`: если принять апстримные файлы не думая, они попадут в embed-глоб и
   переопределят `VtermHeadless::create`.
2. **`3c91f408`** (починка гварда «глиф-бомбы» во FreeType 2.14.3, из-за которого
   `test_wide_ligature_overflow` красен на Linux) в `M8`. Инструментовка,
   пришедшая этим шагом, его **не требует** — но и отказ не чинит: на Linux тест
   останется красным, только теперь с читаемой причиной в логе.
3. **Находка 1** — незакрытое место в `overrideOverlayStrips`. Тест на неё стоит
   ставить до `M8`, а не после: `M8` этот файл, скорее всего, снова тронет.

# M5. Span shaper: рендерная машинерия уезжает из модели экрана

**Дата:** 2026-09-03 · **Задача:** `M5`, волна 5 плана `docs/plans/2026-08-29-upstream-merge.md` · **Ветка:** `wave/upstream-merge-w5` · **Вердикт:** `DONE, дерево зелёное`

Пятый мерж-шаг. Один апстримный коммит, пять конфликтующих файлов, 13 конфликтных хунков. Десять из пятнадцати файлов, которых мерж коснулся, легли **побайтово так же**, как в собственном диффе апстрима.

**Про точку остановки сразу:** вопрос про `CellExtraClient` **не встал**. Место, куда его положить, не меняя замысла апстрима, нашлось — это тот же прокси, который апстрим пересоздал в `span_shaper.cpp`. Подробно — §3.2.

---

## 1. Коммиты

| Коммит | Что |
|---|---|
| `892fc8db` | `merge M5: the span shaper leaves the screen model, and the strips come from the window` — сам мерж, `--no-ff` |

Точка мержа (предмержевая голова) — `ee6a266d`. Слит один апстримный коммит:

```
a3712f4e Split the span shaper out of the screen model
```

`behind` относительно `origin/master` уменьшается на 1.

---

## 2. Что принёс апстрим

`Screen` держал под одной крышей две вещи: сетку ячеек и рендерную машинерию, которая режет строки на однофонтовые спаны и растеризует их в арены полос. Машинерия уехала в новый `SpanShaper` (`lib/shitty/span_shaper.{h,cpp}`, 665 + 64 строки), а вся рендерная поверхность модели сжалась до одного метода:

```cpp
virtual ScreenRowRef viewRow(i32 viewRow) const = 0;   // { const TerminalCell* cells; u64 id; }
```

Существенное в замысле, потому что от этого зависят задачи `T4.*`:

- **Шейпер ничего не знает про `Screen`.** Это content-addressed словарь: строки кешируются по идентичности `ScreenRowRef::id`, полосы — по самому отшейпленному тексту. Идентичность выдаёт **глобальная** последовательность `rowIdentityCounter` (`screen.cpp:58`), которая никогда не повторяется: любая мутация штампует свежую, прокрутка везёт её вместе со строкой, переиспользованная строка не может воскресить устаревший ключ кеша.
- **Шейпер один на окно** (`Composer::shaper`), поэтому основной и альтернативный экраны теперь дедуплицируются друг об друга. Это и есть основание решения `Р5` плана — арены на панель становятся не нужны.
- **Полупространственная сборка полос убрана.** Когда арена перерастает бюджет, шейпер сбрасывается целиком, а сдвинувшееся поколение заставляет рендереры перетянуть все видимые строки — та же редкая ограниченная цена, что всегда платила смена шрифта. Бюджет держит пол в одну целую строку, иначе циклы дозаполнения не сходятся.
- `Row` теряет встроенный массив спанов, шрифты уходят из `screen.cpp` целиком, поколения арен на экран схлопываются в одну последовательность. Тест, который прибивал пер-экранные поколения (`ScreensNeverShareASpanGeneration`), уехал вместе с ними.
- `TestApi::shapeGeneration()` удалён; тест-режим спрашивает `composer.shaper->spanGeneration()`.
- Спановый набор тестов переехал в новый `span_shaper_ut.cpp`.

Апстрим в этом же коммите портировал на шейпер **эталонный рендерер и Vulkan**. Metal он портировал позже, отдельным коммитом (`44d61bfc`, см. §1 плана).

---

## 3. Конфликты: по каждому отдельно

Разрешено 5 файлов, 13 хунков. Правило, которым разрешался каждый: **структура апстрима + наши инварианты**, и ни одного случая, где пришлось выбирать между ними — они везде оказались ортогональны.

### 3.1 `vterm.h` — один хунк, объединение

Наш блок `A9` (`gridColumns`/`gridRows` в `TerminalUpdate`) стоял вплотную к комментарию над полем `shapes`, который апстрим переписал. Конфликт чисто позиционный: два соседних изменения, а не спор.

**Взято:** наш блок `A9` целиком **плюс** апстримный текст комментария. Обоснование: поля наши, комментарий описывает апстримное поле `shapes` и его новый смысл («модель за кадром», а не «канва шейпинга»). Ни одна сторона не отброшена.

### 3.2 `screen.cpp` — один хунк на 600 строк, и вопрос про `CellExtraClient`

Апстрим удаляет из `screen.cpp` весь блок шейпинга. Внутри него жила **единственная правка форка в этом файле** — измеренная, а не предположенная:

```
$ git diff a3712f4e~1 ee6a266d -- lib/shitty/screen.cpp
-    struct CallScreenExtrasCollected final: public Listener {
+    // A screen owns no refs of its own - ... (4 строки комментария)
+    struct CallScreenExtrasCollected final: public CellExtraClient {
-        void onListen(void*) override {
+        void extrasCollected() override {
```

Шесть строк, больше в файле нашего нет ничего.

**Почему эта правка существует.** У нас `composer.cellExtrasChangedListeners` — список **`CellExtraClient`**, а не `Listener`: `CellExtraStoreImpl::collect()` (`cell_extra_store.cpp:554`) и `Composer::setCellExtras()` (`composer.cpp:145`) обходят его и делают `static_cast<CellExtraClient*>(node)`. Это R7: сборка не доверяет тому, кто её позвал, а опрашивает всех клиентов окна.

**Что делает апстрим.** Пересоздаёт тот же прокси в `span_shaper.cpp` — но как `CallShaperExtrasCollected final: public Listener` с `onListen(void*)`, потому что у апстрима этот список листенерный.

**Найденный отказ.** Если взять `span_shaper.cpp` дословно, файл соберётся и слинкуется, а `static_cast<CellExtraClient*>` на объекте `Listener` попадёт в чужой vtable-слот: `collectExtras()` уедет в `onListen()`. Ошибка **немая** — ни ошибки компиляции, ни падения на пустом наборе клиентов.

**Разрешение.** Наша правка переезжает вместе с кодом, которому принадлежала:

```cpp
// span_shaper.cpp
struct CallShaperExtrasCollected final: public CellExtraClient {
    void extrasCollected() override;
    ...
};
```

**Чья сторона взята и почему.** Апстримная — целиком: `screen.cpp` после разрешения **побайтово равен** `a3712f4e:lib/shitty/screen.cpp` (проверено `diff`, вывод пуст). Наша правка не сохранена на старом месте (это нарушило бы `Р2`), а перенесена на новую структуру — ровно то, чего `Р2` требует. Замысел апстрима не изменён ни в чём: шейпер по-прежнему подписан на то же уведомление, класс остался один, комментарий сохранён с поправкой «экран» → «шейпер».

**Про точку остановки плана.** Она сформулирована как «если нашему `CellExtraClient` некуда положить». Место есть, и оно единственное очевидное; выбор не потребовал ни решения об архитектуре, ни расширения интерфейса. `T5.3` (перевод `CellExtraClient` на `VtCellExtras::changedListeners`) от этого **не усложняется и не упрощается**: класс как был один, так и остался, только в другом файле.

Доказательство, что разрешение доехало до бинарника (метод волны 2, независимый от системы сборки):

```
$ nm -a .build/grb/*/obj/libshitty/lib/shitty/span_shaper.cpp.o | grep CallShaperExtrasCollected
... t __ZN12_GLOBAL__N_125CallShaperExtrasCollected15extrasCollectedEv
```

`onListen` в объектнике нет.

### 3.3 `render_reference.cpp` — семь хунков, `A9` против `composer_.columns`

Апстрим меняет источник полос (`Screen` → `SpanShaper`). Мы в этих же строках несём `A9`: ширину сетки рендерер берёт **из обновления, которое несёт ячейки** (`update.gridColumns`/`gridRows`), а не из окна.

**Взято: обе стороны, без потерь.** `captureSpan` получает и апстримный `SpanShaper& shaper`, и наш `u16 columns`:

```cpp
void ReferenceRendererImpl::captureSpan(SpanShaper& shaper, u16 columns, u16 row, const ScreenRowSpan& span);
```

Цикл по строкам остаётся на `update.gridRows`, `rowSpans` получает наш `columns`, а не `composer_.columns`:

```cpp
for (u16 row = 0; row < update.gridRows; ++row) {
    const ScreenRowRef rowRef = shapes.viewRow(row);
    const size_t spans = shaper.rowSpans(rowRef.cells, columns, rowRef.id, spanScratch_.mutData());
```

**Обоснование.** Апстримная сторона здесь — про то, **у кого** спрашивать полосы; наша — про то, **какой сеткой** мерить. Взять апстрим целиком значило бы вернуть 23 чтения окна в эталонный рендерер, то есть отменить `A9` в файле, где `pane_grid_guard` его и охраняет. Проверено: после разрешения в файле **ноль** `composer_.columns`/`composer_.rows`.

Формально: наш дифф по этому файлу — те же 13 изменённых строк, что у апстрима, отличаются только присутствием `columns` (§5, таблица).

### 3.4 `render_reference_ut.cpp` — три хунка

- **Блок включений.** Наш (несёт `render_blend.h`, `grid_geometry.h`, `render.h`, условный `render_metal.h`) + апстримный `span_shaper.h`. Порядок не воспроизводился руками: вызвана `style.reorder_includes()` в обход `main()` — правило, записанное в борд после `M4`.
- **Две фикстуры (`ReferenceFixture`, `ScreenFixture`).** Апстрим меняет строку `resize(...)` **и** добавляет создание шейпера. Наша сторона той же строки — `A1`: размер поверхности считается через `contentInsets()` и `gridPixelWidth/Height`, а не через `2 * borderPixels()`.

  **Взято:** наша строка `resize` + апстримная строка `SpanShaper::create`. Обоснование ровно то же, что в 3.3: изменения ортогональны — одно про размер, другое про то, что у композера теперь есть шейпер.

### 3.5 `render_vk.cpp` — один хунк

Конфликт только на включении: наш `render_blend.h` против апстримного `span_shaper.h`. Взяты оба, порядок — `style.reorder_includes()`. Остальные 42 изменённые строки апстрима слились автоматически и **побайтово совпадают** с его собственным диффом.

---

## 4. Правки сверх мержа: четыре, каждая обоснована

Мерж — это не только конфликтные хунки. Ниже всё, что пришлось изменить дополнительно, и почему это не «работа `T4.*`, сделанная заранее».

### 4.1 `render_metal.mm` — механический перевод на шейпер (21 строка)

Апстрим Metal в этом коммите **не портировал**, а `render_metal.mm` компилируется на macOS. Старого API (`shapes.rowSpans`, `shapes.spanGeneration()`, `shapes->spanMask()`) в `Screen` больше нет — без правки `./build st` красный, то есть нарушен контракт плана «каждый `M<n>` оставляет репозиторий собирающимся».

**Что сделано:** источник полос переведён с `pane.shapes` / `update.shapes` на `composer.shaper`; `assignRowStrips` берёт `shapes.viewRow(row)` и отдаёт шейперу `rowRef.cells`, наш `columns` и `rowRef.id`; добавлена проверка `composer.shaper != nullptr` рядом с существующей `update.shapes != nullptr`.

**Что сознательно НЕ сделано:** `PaneArenaMirror` на месте, `biasStrips` на месте, `cellBase`, `update.gridColumns` и швы не тронуты. Это `T4.1`, и объём её работы измерен в §7.

**Корректность на промежуточном состоянии.** Арена теперь одна на окно, поэтому все панели рапортуют одинаковые `used` и одинаковое поколение; зеркало разложит им отдельные базы и скопирует одни и те же байты `N` раз. Расточительно, но не неверно: `biasStrips` прибавляет каждой панели её базу, а `PaneArenaRequest::pane` — непрозрачный идентификатор, и общее поколение означает «арена сбросилась у всех», что теперь и есть правда. Схлопывание зеркала — работа `T4.1`, а не условие сборки.

### 4.2 `span_shaper_ut.cpp` — нарушение `A1`, найденное гвардом

Новый файл, поэтому конфликта не было — и ровно поэтому наша правка до него не дотянулась. Фикстура размеряла поверхность так:

```cpp
composer->resize((u16)(16 * composer->glyphWidth + 2 * composer->borderPixels()), ...);
```

`border_pixels_guard`, запущенный **напрямую**, покраснел на этих двух обращениях:

```
borderPixels()/scaledPixels() are the border option and its scale, not the layout (A1): contentInsets() is what layout reads.
Unallowed uses:
  lib/shitty/span_shaper_ut.cpp:66
  lib/shitty/span_shaper_ut.cpp:66
EXIT=1
```

Исправлено тем же способом, что во всех остальных фикстурах дерева: `contentInsets()` + `gridPixelWidth/gridPixelHeight`. **Разрешение (`allowance`) не расширялось** — это точка остановки по критериям `T2.1`, и она не понадобилась.

**Отдельная сложность: этот файл на macOS не компилируется вовсе.** Он целиком под `#if defined(HAVE_FREETYPE) && defined(HAVE_HARFBUZZ)`, а `62cef373` выключил их на darwin. Объектник — 336 байт, пустой. Поэтому правка проверена принудительной компиляцией с обоими макросами:

```
$ c++ ... -DHAVE_FREETYPE=1 -DHAVE_HARFBUZZ=1 -c lib/shitty/span_shaper_ut.cpp -o /tmp/m5_ssut.o
$ ls -la /tmp/m5_ssut.o          →  38384 байта
$ nm /tmp/m5_ssut.o | grep -c PlainTextIsOneSpanWithInk   →  7
$ c++ ... -c lib/shitty/span_shaper_ut.cpp -o /tmp/m5_ssut_off.o
$ ls -la /tmp/m5_ssut_off.o      →  336 байт
```

38 КБ против 336 байт — доказательство и того, что правка собирается, и того, что пустой объектник на macOS штатный, а не следствие мержа.

### 4.3 `vterm_headless_ut.cpp` — фикстура без шейпера теряла покрытие молча

`QuietPaneFixture` создаёт настоящий `Fontpack` и рендерит через `ReferenceRenderer` (пять тестов в двух наборах: `QuietPaneFrame`, `QuietPaneFrameOnMetal`, `MultiPaneScreenChangeParity`). После мержа `composer->shaper == nullptr`, и `captureStrips()` выходит первой же строкой: **кадры без единой полосы**. Тесты при этом остаются зелёными, потому что проверяют цвета панелей и журнал повреждений.

То есть это ровно тот класс, который волны обязаны отличать от регрессии: зелёный тест, переставший проверять половину того, что проверял. Фикстура получила `SpanShaper::create` — как это сделал сам апстрим в обеих фикстурах `render_reference_ut.cpp`.

### 4.4 Порядок включений

`style.reorder_includes()` вызвана на `render_reference_ut.cpp` и `span_shaper_ut.cpp` (в обоих блок включений и так перестраивался). На `vterm_headless_ut.cpp` **не вызвана сознательно**: там добавлена одна строка, а файл живёт в своём (алфавитном) порядке; прогон функции переставил бы шестнадцать строк, которых мерж не касался, и подарил бы следующему мержу лишний хунк. `render_vk.cpp`, `render_reference.cpp`, `render_metal.mm`, `vterm.h`, `span_shaper.cpp` функция оставила без изменений — проверено прогоном на копиях.

---

## 5. Совпадение диффов (метод волны 2)

Сравнение построчное: для каждого файла берётся множество `+`/`-` строк нашего диффа `ee6a266d..892fc8db` и апстримного `a3712f4e~1..a3712f4e`.

```
IDENTICAL  lib/shitty/application.cpp  (2 lines)
IDENTICAL  lib/shitty/composer.h  (7 lines)
DIFFERS    lib/shitty/render_metal.mm: ours-only 21, upstream-only 0
DIFFERS    lib/shitty/render_reference.cpp: ours-only 13, upstream-only 13
DIFFERS    lib/shitty/render_reference_ut.cpp: ours-only 2, upstream-only 5
IDENTICAL  lib/shitty/render_vk.cpp  (43 lines)
DIFFERS    lib/shitty/screen.cpp: ours-only 6, upstream-only 1
IDENTICAL  lib/shitty/screen.h  (43 lines)
IDENTICAL  lib/shitty/screen_ut.cpp  (304 lines)
DIFFERS    lib/shitty/span_shaper.cpp: ours-only 7, upstream-only 2
IDENTICAL  lib/shitty/span_shaper.h  (64 lines)
DIFFERS    lib/shitty/span_shaper_ut.cpp: ours-only 5, upstream-only 1
IDENTICAL  lib/shitty/test_mode.cpp  (5 lines)
IDENTICAL  lib/shitty/vterm.cpp  (5 lines)
IDENTICAL  lib/shitty/vterm.h  (20 lines)
DIFFERS    lib/shitty/vterm_headless_ut.cpp: ours-only 5, upstream-only 0
IDENTICAL  lib/shitty/vterm_test.h  (1 lines)
```

**Десять файлов из семнадцати — побайтово.** Оставшиеся семь, каждый объяснён поимённо:

| Файл | Расхождение | Где разобрано |
|---|---|---|
| `render_metal.mm` | только наше, апстрим Metal не трогал | §4.1 |
| `render_reference.cpp` | те же 13 строк, отличие ровно в `columns` (`A9`) | §3.3 |
| `render_reference_ut.cpp` | наш `render_blend.h` + другой стартовый порядок включений | §3.4 |
| `screen.cpp` | мы удалили на 5 строк больше — свой комментарий и `CellExtraClient` | §3.2 |
| `span_shaper.cpp` | зеркало предыдущего: `CellExtraClient` вместо `Listener` | §3.2 |
| `span_shaper_ut.cpp` | `contentInsets()` вместо `2 * borderPixels()` (`A1`) | §4.2 |
| `vterm_headless_ut.cpp` | только наше, шейпер в фикстуре | §4.3 |

Ни одной строки апстрима не потеряно: колонка `upstream-only` пуста везде, кроме тех трёх файлов, где апстримная строка **заменена** нашей (по одной-две на файл, все перечислены выше).

---

## 6. Критерии приёмки

| # | Критерий | Результат |
|---|---|---|
| 1 | `./build st --clear` | **зелёная, 225 узлов** |
| 2 | `./build unit_tests pty_test_helper` + прогон | **`OK: 955`** |
| 3 | Совпадение диффов | показано, §5 |
| 4 | Питоновский набор против предмержевой головы | **идентичен, поимённо** |
| 5 | Четыре гварда + `vterm_boundary` | **зелёные**, прямым запуском и через `./build` |
| 6 | `A1`, `A8`, `A9` | не нарушены, §6.6 |

### 6.1 `./build st --clear`

```
$ ./build st --clear -j 10
ST_CLEAR_EXIT=0
...
[CC] {223/225} $(B)/obj/libshitty/lib/shitty/parser.cpp.o
[AR] {224/225} $(B)/libshitty_prod.a
[LD] {225/225} $(B)/st
```

**225 узлов** — 224 в волне 4 плюс новый `span_shaper.cpp`, сходится.

`--clear`, а не обычная сборка, потому что мерж поменял блоки включений в четырёх файлах, а `build.includes` в ключ узла не входит (ловушка волны 3).

### 6.2 `unit_tests`

```
$ ./build unit_tests pty_test_helper -j 10
BUILD_EXIT=0
[CC] {1/2} $(B)/obj/unit_tests/lib/shitty/vterm_headless_ut.cpp.o
[LD] {2/2} $(B)/unit_tests

$ SHITTY_PTY_TEST_HELPER="$PWD/.build/pty_test_helper" ./.build/unit_tests --threads=1
RUN_EXIT=0
...
+ WindowSizingRequests::TheMinimumSizeAndTheResizeUnitPairEachAxisWithItsOwnInsets
OK: 955
```

**955 — ровно эталон, и это не совпадение.** Апстрим удалил из `screen_ut.cpp` четырнадцать спановых тестов и добавил их же в `span_shaper_ut.cpp`; **обе группы были и остаются под `#if defined(HAVE_FREETYPE) && defined(HAVE_HARFBUZZ)`**, который на darwin выключен. То есть на этой машине набор не изменился ни на один тест ни до, ни после. Проверено списком имён: суита `SpanShaper` в прогоне отсутствует и до, и после, а объектник `span_shaper_ut.cpp.o` — 336 байт.

**Следствие, важное для волны 5:** суита `SpanShaper` (14 тестов, включая `ArenaOverflowCollectsToLiveStrips` и `ScrolledRowKeepsItsShapeThroughHistory`) локально **не исполняется вовсе** и проверяется только в Linux-CI. То же было верно и до мержа для её предшественницы в `screen_ut.cpp`, так что регрессии здесь нет — но `T4.1`–`T4.3` не должны считать эти тесты своей локальной страховкой.

### 6.3 Свежесть бинарника, независимо от системы сборки

Строка, которая появляется **только** после разрешения конфликта (апстримный текст `span_shaper.cpp` вместо нашего `screen.cpp`):

```
$ strings -a .build/st | grep -c ": shape: font change, generation "   → 1
$ strings -a .build/st | grep -c ": shape: font change, screen "       → 0
```

Плюс символ из §3.2, доказывающий, что в бинарник попало именно моё разрешение, а не апстримный дословный текст.

### 6.4 Питоновский набор — сравнение с предмержевой головой

Собрано и прогнано **на самой предмержевой голове**, а не сверено с записанным эталоном: отдельный worktree на `ee6a266d`, свои `st_test`/`pt_test`/`toml_dump`, 20 групп с полным окружением из `build.py`.

```
before (ee6a266d): Ran=6398 failures=6 errors=14 skipped=17 expected_failures=549
after  (892fc8db): Ran=6398 failures=6 errors=14 skipped=17 expected_failures=549

$ diff before_names.txt after_names.txt
NAMED REDS: identical to pre-merge head
```

Двадцать красных совпадают **поимённо**, список тот же, что в `T0.1` §4–5. Новых отказов ноль.

`test_wide_ligature_overflow` в списке нет — он средовой (гвард «глиф-бомбы» во FreeType 2.14.3) и на macOS не проявляется, потому что freetype здесь выключен. Это ожидаемо и совпадает с формулировкой брифа.

### 6.5 Гварды

Через систему сборки:

```
$ ./build border_pixels_guard mouse_geometry_guard pane_grid_guard darwin_call_guard vterm_boundary -j 5
GUARDS_BUILD_EXIT=0
[VB] {1/5} $(B)/vterm-boundary.stamp
[PG] {2/5} $(B)/tst/pane-grid-guard.stamp
[MG] {3/5} $(B)/tst/mouse-geometry-guard.stamp
[BP] {4/5} $(B)/tst/border-pixels-guard.stamp
[DA] {5/5} $(B)/tst/darwin-call-guard.stamp
```

И прямым запуском программ гвардов, минуя `./build` (ловушка штампов из CAS):

```
border_pixels  EXIT=0
mouse_geometry EXIT=0
pane_grid      EXIT=0
darwin_call    EXIT=0
vterm_boundary EXIT=0
```

**Два из пяти доказали, что действительно сканируют это дерево, а не пустоту:**

- `border_pixels_guard` — не пробой, а настоящей находкой: он покраснел на `span_shaper_ut.cpp:66` (§4.2) и позеленел после исправления кода, а не разрешения;
- `pane_grid_guard` — пробой, потому что мои разрешения в `render_reference.cpp` касаются именно `A9`. Временная подмена `update.gridRows` на `composer_.rows` в одном цикле:

  ```
  A renderer takes the grid of the pane it is drawing from the update that carries its cells (A9: ...)
  Unallowed uses:
    lib/shitty/render_reference.cpp:466
  EXIT=1
  ```

  После восстановления файла — `EXIT=0`, `git diff --stat` по файлу пуст.

Разрешения (`allowance`) не расширялись ни у одного гварда.

### 6.6 Инварианты

| Инвариант | Чем проверен |
|---|---|
| `A1` (отступ пользователя ≠ резерв под хром) | `border_pixels_guard` — **красный на реальном нарушении**, принесённом апстримом (§4.2), зелёный после правки кода. Плюс ручная сверка обеих фикстур `render_reference_ut.cpp`: `contentInsets()` сохранён, апстримная форма `2 * borderPixels()` не принята |
| `A8` (`Vterm` получает геометрию, а не читает окно) | `mouse_geometry_guard` зелёный прямым запуском. Мерж `vterm.cpp` тронул на пять строк (удаление `TestApi::shapeGeneration`), геометрии не касался — дифф по файлу побайтово совпадает с апстримным |
| `A9` (сетка панели едет с данными панели) | `pane_grid_guard` зелёный **и доказанно сканирующий** (проба выше). Плюс прямой счёт: `composer_.columns`/`composer_.rows` в `render_reference.cpp` — **0**; в `render_metal.mm` цикл остался на `update.gridRows`, `columns` — `update.gridColumns` |

Названная явно оговорка, унаследованная из волны 3 и не снятая здесь: щель `pane_grid` через `const auto& g = composer.geometry;` остаётся; в этом мерже такой конструкции не появилось.

---

## 7. Что осталось для `T4.1`–`T4.3` — оценено **после** мержа

План оценивал эти задачи до мержа. Механизм, записанный в борде («чтения внутри конфликтных хунков не доживают до задач-переводчиков»), сработал сильнее обычного: **апстрим сам портировал два рендерера из трёх**, и обе задачи по ним схлопнулись.

### `T4.3` — софт-рендерер: **работы не осталось**

`render_reference.cpp` и `render_reference_ut.cpp` переведены полностью — апстримом и разрешением §3.3/§3.4. Измерено:

```
$ grep -c "shapes\.\(rowSpans\|shapeCells\|spanGeneration\|spanMask\|spanColor\)" lib/shitty/render_reference.cpp   → 0
$ grep -c "composer_\.columns\|composer_\.rows"                                    lib/shitty/render_reference.cpp   → 0
```

Задача сводится к прогону её критериев приёмки: `tst/test_soft_render.py`, `tst/test_render_contract.py`, `tst/test_damage_only_frames.py`, `./build unit_tests`. Все три набора зелёные уже сейчас (§6.4). **Рекомендация: закрывать `T4.3` проверкой, а не работой.**

### `T4.2` — Vulkan: **работы по шейперу не осталось; остаётся не она**

`render_vk.cpp` слился с апстримом **побайтово** (§5, 43 строки). Формулировка плана «апстрим этого не делал, это наша работа» относилась к состоянию на 2026-08-29 и после `a3712f4e` неверна.

Что в `render_vk.cpp` действительно остаётся — это **другая** работа, уже именованная задачей `G9`: бэкенд не поддерживает сплиты, 36 чтений `cellColumns`/`cellRows`/`composer.*` против 7 обращений к `update.grid*`, восемь полей состояния заведены на поверхность вместо панели. Оценка `G9` — 350–500 строк из 2417, один-два дня. К span shaper это отношения не имеет и объёмом `T4.2` в прежней формулировке не покрывается.

**Рекомендация: `T4.2` переформулировать в «проверить в CI, что апстримный порт Vulkan собирается и линкуется под Linux», а работу по сплитам оставить за `G9`.** Локально бэкенд не линкуется (нет `libvulkan`), поэтому иначе задачу не закрыть — статус `блокирована, ждёт CI`, как и записано в плане.

### `T4.1` — Metal: работа осталась, и она измерима

Механический перевод на шейпер сделан мержем (§4.1). Осталось ровно то, что план и называл: **снять `PaneArenaMirror`**.

| Что | Объём |
|---|---|
| `render_metal.mm`: `maskMirror`/`colorMirror`, `maskRequests`/`colorRequests`, `maskCopies`/`colorCopies`, `biasStrips`, двухпроходный `uploadArenas` | **37 обращений** в 6 участках; после снятия зеркала `uploadArenas` схлопывается в одну копию арены окна, `biasStrips` исчезает целиком (~15 строк) |
| `render_arena.h` | **144 строки**, из них ~45 — комментарий `A3`, объясняющий, почему зеркало нельзя упрощать. Файл удаляется целиком либо остаётся ради `PaneArenaCopy` |
| `render_arena_ut.cpp` | **239 строк**, 12 тестов суиты `PaneArenaMirror` — все зелёные сейчас, все теряют предмет |
| Тест на пер-экранный `rowIdentityCounter` (`Р5`) | новый; сегодня счётчик — `static u64 rowIdentityCounter` в `screen.cpp:58`, `nextRowIdentity()` вызывается из трёх мест |

**Что нельзя потерять при снятии** (проверено на текущем дереве, не по памяти): `cellBase` (смещение панели в общем векторе ячеек), `update.gridColumns`/`gridRows` в `assignStrips`/`assignRowStrips`/`materializeCells`, швы, восемь тестов суиты `MetalPanes`.

**Замечание про `Р5`, которое стоит передать `T4.1` дословно.** Скрытое допущение теперь можно назвать точнее, чем в плане: `PaneArenaMirror` держал пару «идентичность + поколение» **именно потому**, что поколения были почти-уникальны и на них нельзя было полагаться. Со сбросом на весь шейпер поколение стало по-настоящему одно на окно, и допущение сместилось: ломается всё не тогда, когда счётчик станет пер-экранным, а тогда, когда **шейперов станет больше одного на окно**. Тест `Р5` разумно писать против этого условия, а не против `rowIdentityCounter`.

### Итог по оценке

Из трёх задач волны две (`T4.2`, `T4.3`) после мержа не содержат работы по span shaper вовсе, а третья (`T4.1`) сузилась с «портировать Metal и снять зеркало» до «снять зеркало». Механизм тот же, что после `M4`, и второй раз подряд подтверждается.

---

## 8. Обнаружено

**1. Немой отказ, которого не случилось: `Listener` в списке `CellExtraClient`.** Апстримный `span_shaper.cpp`, взятый дословно, компилируется и линкуется, а `static_cast<CellExtraClient*>` попадает в чужой vtable-слот. Ни ошибки, ни падения — `collectExtras()` уходит в `onListen()`. Это цена того, что оба интерфейса наследуют `stl::IntrusiveNode`, а список типизирован соглашением, а не типом: `stl::IntrusiveList` хранит `IntrusiveNode*`. **Уровень: важная.** Тот же класс встретится в `T5.3` и в каждом мерже, где апстрим добавит подписчика в этот список. Дешёвая страховка, которой сегодня нет: сделать `CellExtraClient` единственным способом попасть в список (отдельный тип списка либо `pushBack(CellExtraClient&)` вместо `pushBack(IntrusiveNode*)`).

**2. Апстрим принёс нарушение `A1` в файле, которого не было.** `span_shaper_ut.cpp` — новый, конфликта не создал, наша правка до него не дотянулась, а `2 * borderPixels()` в нём есть. Это **новый подкласс** известного механизма «мерж не дотягивается до неконфликтного»: до сих пор речь шла о наших файлах, которых апстрим не касался, теперь — об апстримных файлах, которых не касались мы. Поймал гвард. **Уровень: важная, для формулировки критериев следующих мерж-шагов:** после каждого `M<n>` гварды надо гонять не как формальность, а как основной инструмент поиска остатка.

**3. Файл, добавленный мержем, может не компилироваться вовсе — и это не видно.** `span_shaper_ut.cpp.o` — 336 байт, суита `SpanShaper` в прогоне отсутствует, `OK: 955` не шелохнулось. Ничто в выводе сборки или тестов об этом не говорит: узел `[CC]` отработал, объектник создан, линковка прошла. Обнаружено только сверкой имён суит с содержимым файла. **Уровень: важная.** Это ровно тот сорт «зелёного, который ничего не проверяет», которого борд насчитал уже три вида (штампы из CAS, `build.includes`, гвард над пустотой); четвёртый — **тест-файл под выключенным `#if`**. Дешёвая проверка: сверять список суит в бинарнике со списком `STD_TEST_SUITE` в дереве.

**4. Фикстура, которая перестала проверять половину, оставшись зелёной.** `QuietPaneFixture` (§4.3). Общий вид: когда апстрим выносит зависимость из объекта в композер, каждая тестовая фикстура, строившая объект руками, тихо теряет эту зависимость. Апстрим свои две фикстуры починил, наша третья ему не видна. **Уровень: важная, повторится в `M6`** (растворение `VtState` — там фикстур больше).

**5. Устаревший комментарий с номером строки в `render_arena.h`.** Строка 15: «nextShapeGeneration() (screen.cpp:142)» — функция уехала в `span_shaper.cpp:68`, и номер был неверен ещё до мержа. Не исправлено сознательно: файл принадлежит `T4.1` и, вероятно, будет удалён ею целиком. **Уровень: мелкая**, но это второй случай подтверждения правила «комментарии без номеров строк» из борда.

**6. План разошёлся с историей апстрима во второй раз.** `M5` в плане владеет `lib/vterm/screen.cpp` и `lib/vterm/vterm.h`; фактически оба файла живут в `lib/shitty/` — переезд ядра, который сделала волна 3, коснулся листьев, а `screen`/`vterm` апстрим перенёс позже. Ожидаемые планом конфликты («`screen.cpp` и `vterm.h`») тоже оказались неполными: конфликтовали ещё три файла, все три — рендереры. **Уровень: мелкая** (замечено `R4-arch` раньше), но брифы `M6`–`M8` стоит писать по `git show --stat`, а не по таблице плана.

---

## 9. Что НЕ делалось

- **Рендереры на `SpanShaper` сверх компиляции.** `PaneArenaMirror` цел, `biasStrips` цел, `render_arena{.h,_ut.cpp}` не тронуты — это `T4.1`.
- **Ни один тест не выключен, не помечен `skip` и не удалён.** Единственный исчезнувший (`ScreensNeverShareASpanGeneration`) удалён самим апстримом вместе с предметом и на darwin не исполнялся.
- **`lib/shitty/session*.{cpp,h}`, `lib/shitty/input_bindings.*`** — мерж их не касался, конфликтов не было, ни одна строка не изменена (`git diff --stat` по этим путям пуст).
- **Ни одно разрешение гварда не расширено.**
- **Не пушилось.**

---

## 10. Как перепроверить

```sh
cd <worktree на wave/upstream-merge-w5>

./build st --clear -j 10                      # 225 узлов
./build unit_tests pty_test_helper -j 10
SHITTY_PTY_TEST_HELPER="$PWD/.build/pty_test_helper" ./.build/unit_tests --threads=1   # OK: 955

./build border_pixels_guard mouse_geometry_guard pane_grid_guard darwin_call_guard vterm_boundary
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
```

Совпадение диффов — сравнение множеств `+`/`-` строк по каждому файлу между `git diff ee6a266d 892fc8db` и `git diff a3712f4e~1 a3712f4e`; ожидаемый результат — таблица §5.

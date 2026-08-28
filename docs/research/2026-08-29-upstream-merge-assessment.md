# Оценка мержа апстрима в форк: стоимость, конфликты, стратегия

Дата: 2026-08-29
Тип: разведка (read-only). Никаких мержей, коммитов и переключений веток в основном дереве не делалось; все пробы шли в одноразовом git worktree, который удалён.

## 1. Контекст и цифры

```
$ git status --short --branch | head -1
## master...origin/master [ahead 537, behind 57]

$ git merge-base HEAD origin/master
8b28e8a9a756f5e37c1572546f80b5f5792cb88e

$ git rev-list --left-right --count origin/master...HEAD
57      537
```

Апстрим (`origin` = pg83/shitty) за 57 коммитов:

```
$ git diff --stat 8b28e8a9..origin/master | tail -1
180 files changed, 7687 insertions(+), 2948 deletions(-)

$ git diff --name-status -M 8b28e8a9..origin/master | grep -c '^R'
76
```

Наш форк (`fork` = KiboMibo/shitty) за 537 коммитов:

```
$ git diff --name-only 8b28e8a9..master | wc -l
251
  из них docs/                                   134
  из них .omc/ .repowise/ .DS_Store               17
  из них код (lib|ext|bin|tst|build.py)           86

$ comm -12 <(git diff --name-only 8b28e8a9..master | sort) \
           <(git diff --name-only 8b28e8a9..origin/master | sort) | wc -l
34
```

Итог пробного мержа (в одноразовом worktree):

```
$ git merge --no-commit --no-ff origin/master
Automatic merge failed; fix conflicts and then commit the result.

$ git diff --name-only --diff-filter=U | wc -l
30
$ (сумма '<<<<<<<' по конфликтным файлам)
209
```

**30 конфликтующих файлов, 209 конфликтных хунков.** Из них по существу содержательных — 11 файлов (разбор ниже); остальные 19 механические или почти механические.

Хорошая новость номер один: **git корректно распознал переезд `lib/shitty/vterm.cpp` → `lib/vterm/vterm.cpp` (R093) и перенёс наши правки туда**. Все 76 переименований апстрима отработали автоматически — «переезд файлов» как источник конфликтов отсутствует. Из 30 конфликтов ни один не является конфликтом переименования; 29 — content, 1 — modify/delete.

Хорошая новость номер два: наш форк и апстрим делали **сходящийся рефакторинг**. Мы вынимали геометрию из окна в панель (`PaneGeometry`, A8); апстрим вынимал ту же геометрию из окна в эмбеддер (`VtGeometry`). Мы вынимали склад cell-extra из терминала в окно (`CellExtraClient`, R7/A11); апстрим вынимал его же в `VtCellExtras`. В обоих случаях наша версия — надмножество апстримной. Это не встречная работа, а одна и та же работа, доведённая с двух сторон.

## 2. Карта конфликтов

Оценка сложности: **S** — минуты, чистая механика; **M** — час-два, надо понимать обе стороны; **L** — полдня и больше, требуется проектное решение.

| Файл | Хунков | Тип | Мех./Содерж. | Сложн. | Суть |
|---|---:|---|---|---|---|
| `lib/vterm/vterm.cpp` | 93 | content | 65 мех. / 28 содерж. | **L** | `columns_`/`rows_` ↔ `geometry.columns/rows`, `composer.opts->` ↔ `config().`; см. §3 |
| `lib/shitty/render_reference.cpp` | 19 | content | смешанный | **M** | span shaper вместо Screen-арен + наши per-pane базы |
| `lib/shitty/render_metal.mm` | 13 | content | содерж. | **L** | наши per-pane арены vs апстримный единый `SpanShaper`; см. §4 |
| `lib/shitty/test_mode.cpp` | 9 | content | мех. | **S/M** | `contentInsets()` vs `2*borderPixels`, `opts->vt.*`, FONT_STATE-строка |
| `lib/shitty/application.cpp` | 8 | content | мех. | **S/M** | `gridPixelSize()` vs `2*border`, состав include |
| `lib/shitty/session.cpp` | 6 | content | содерж. | **M/L** | наши вкладки-панели vs апстримный `Vterm::create(...)` c 10 аргументами |
| `lib/shitty/render_vk.cpp` | 6 | content | мех. | **S** | `composer.pixelWidth` → `composer.geometry.pixelWidth`, `opts->bg` → `opts->vt.bg` |
| `bin/main_fuzz/main.cpp` | 5 | content | мех. | **S** | новая сигнатура headless-хоста |
| `lib/shitty/vt_headless_ut.cpp` | 4 (629 стр.) | content | содерж. | **L** | наши +858 строк pane-тестов на новый API `VtermHeadless` |
| `lib/shitty/options.cpp` | 4 | content | мех. | **M** | разнос опций между `Options` и `Options::vt` |
| `lib/shitty/options.h` | 4 | content | содерж. | **M** | что именно уезжает в `VtConfig`, а что остаётся эмбеддеру |
| `lib/shitty/render_reference_ut.cpp` | 4 | content | мех. | **S/M** | те же переименования, что в render_reference.cpp |
| `lib/shitty/session_ut.cpp` | 4 | content | мех. | **S** | сигнатуры |
| `lib/shitty/ui_csd_tabs.mm` | 3 | content | мех. | **S** | `ShittyTabBarView` ↔ апстримный `CsdTabBarView` (переименование класса) |
| `tst/pty_test_helper.c` | 3 | content | содерж. | **M** | апстримный Darwin-фикс sigwait/canonical vs наш `catch_signal` |
| `lib/shitty/composer.h` | 3 | content | содерж. | **M/L** | наш A10 (chrome/pane/contentInsets) vs апстримные `installVtHost`/`setOptions` |
| `lib/vterm/vterm.h` | 3 | content | содерж. | **M** | наш `PaneGeometry`, `show/hide`, `retainedOutput`, `cellCapacity` vs апстримный `VtHost`-интерфейс |
| `lib/shitty/composer_ut.cpp` | 3 | content | мех. | **S** | |
| `lib/shitty/composer.cpp` | 2 | content | мех. | **S/M** | |
| `.gitignore` | 2 | content | мех. | **S** | апстрим убрал build stamps, мы добавили свои |
| `lib/vterm/cell_extra_store.h` | 2 | content | содерж. | **M** | наш `CellExtraClient` vs апстримный `VtCellExtras` |
| `lib/shitty/application_ut.cpp` | 2 | content | мех. | **S** | |
| `build.py` | 2 | content | мех. | **M** | `vterm_source` путь + слияние списков тестов (наши гварды + апстримный `vterm_boundary`) |
| `lib/shitty/cell_extra_store_ut.cpp` | 2 | content | мех. | **S** | |
| `lib/vterm/screen.cpp` | 1 (594 стр.) | content | содерж. | **M** | апстрим вынес span-шейпинг из Screen; наша правка (`CellExtraClient`) — 6 строк внутри удаляемого блока |
| `lib/shitty/pty.h` | 1 | content | содерж. | **S/M** | апстрим переехал `PtyHandle` в `lib/vterm/pty.h`; наш `childPid()` надо перенести туда |
| `lib/vterm/cell_extra_store.cpp` | 1 | content | содерж. | **M** | наш обход клиентов + дедуп корней vs апстримный однострочник |
| `ext/plt/platform_cocoa.mm` | 1 | content | содерж. | **S** | наш Accessory-режим quick-окна vs апстримный fallback имени |
| `lib/shitty/render_ut.cpp` | 1 | content | мех. | **S** | |
| `lib/shitty/vterm_headless.cpp` | — | **modify/delete** | содерж. | **S** | апстрим удалил файл (переписан как `lib/vterm/vt_headless.cpp` без Composer); наши 17 строк выбрасываются |

Отдельно — конфликты, которых git **не покажет**, но которые сломают сборку:

```
$ python3 lib/vterm/check_includes.py lib/vterm /tmp/stamp.txt   # на смерженном дереве
mouse_frontend.h:9: "composer.h" does not resolve inside lib/vterm
vterm.cpp:21: "session.h" does not resolve inside lib/vterm
vterm.cpp:28: "grid_geometry.h" does not resolve inside lib/vterm
vterm.cpp:32: "options.h" does not resolve inside lib/vterm
exit=1
```

Апстрим ввёл `lib/vterm/check_includes.py` и повесил его в сборку как тест `vterm_boundary`: ядро VT не имеет права включать ничего из `lib/shitty`. Наши правки автомержем затащили в ядро `composer.h`, `session.h`, `grid_geometry.h`, `options.h`. Это **четыре обязательных к устранению нарушения границы**, и они же — главный проектный вопрос мержа (см. §3.3).

## 3. Разбор vterm: куда едут наши +433/−171 и +101

### 3.1 Что мы там сделали

```
$ git log --oneline 8b28e8a9..master -- lib/shitty/vterm.cpp lib/shitty/vterm.h
7050204e T10: a reshaped tab owes the frame every row of every pane
d6c1c8bc T10: a pane's far edges are its own, from an extent it is handed
d3736175 T12: the collection asks the window, not whoever noticed the budget
15a3c8f4 T13: Vterm::retainedOutput() - a quiet pane's entry in the frame
a583b067 T9: SessionSet becomes tabs of panes, visible split from focused
d4840d25 T9: move windowPane() out of the terminal header into pane_layout
ecb77d83 feat(vterm): pane grid size in TerminalUpdate [T0]
0ae193a5 F5: the shared extra store is sized by the window, not by the last pane
0dbeada1 T7: the terminal is given its grid instead of reading the window
96013809 T4: pointer geometry reads four sides, and can be shown to
b4de7577 T4: layout sizing goes through contentInsets(), not borderPixels()
```

Семь смысловых кусков:

1. **A8 — терминал получает свою сетку, а не читает окно.** `PaneGeometry{columns,rows,originX,originY,width,height}` в `vterm.h`, поля `columns_/rows_/originX_/originY_/paneWidth_/paneHeight_` в `VtermImpl`, `paneResized(const PaneGeometry&)` вместо `windowResized()`, геометрия — параметр `Vterm::create()`.
2. **A9 — размер сетки едет с данными сетки.** `TerminalUpdate::gridColumns/gridRows`, ноль = отказанный кадр.
3. **A5 — «видима» и «в фокусе» разделены.** `activate()/deactivate()` → `show()/hide()`.
4. **A2/R7-2 — `retainedOutput()`**: тихая панель всё равно обязана кадру своей записью.
5. **A11 — `cellCapacity()`** и `updateExtraCellCount()`: общий на окно склад размеряется суммой по живым панелям.
6. **R7 — `CallVtermCollectExtras`**: терминал становится клиентом коллекции, а не её распорядителем.
7. **A1/A10 — четырёхсторонние инсеты** вместо скалярного `borderPixels`: `mouseGeometry(composer, originX_, originY_, paneWidth_, paneHeight_)`, `gridColumns/gridRows/gridPixelWidth/gridPixelHeight` из `grid_geometry.h`.

### 3.2 Что сделал апстрим на тех же строках

Апстрим растворил `VtState` на явные «части эмбеддинга» и вынес их в `lib/vterm/`:

- `vt_geometry.h` — `VtGeometry{columns, rows, cellPixelWidth, cellPixelHeight, pixelWidth, pixelHeight, borderPixels}` + `resize(w,h,host)`;
- `vt_config.h` — `VtConfig` (все VT-ручки) и `VtConfigSlot` (перезагружаемый снимок за указателем);
- `vt_host.h` — `VtHost`: буферы обмена, оконные операции, `requestFrame()`, `titleChanged()`, `resized()`;
- `cell_extra_store.h` — `VtCellExtras{store, changedListeners}`;
- `vt_trace.h`, `vt_test.h`, `vt_headless.h` — трассировка, тест-API, headless-хост, все без Composer.

Новая сигнатура:

```cpp
Vterm* Vterm::create(ObjPool& owner, VtGeometry& geometry, const VtConfigSlot& configSlot,
                     VtCellExtras& extras, SmallObjAllocator& smallObjects,
                     plt::Scheduler& scheduler, VtHost& host, PtyHandle& pty,
                     VtermTraceFactory* traceFactory);
```

### 3.3 Классификация 93 конфликтов в `lib/vterm/vterm.cpp`

Канонизировав два словаря друг на друга (`columns_`↔`geometry.columns`, `rows_`↔`geometry.rows`, `composer.opts->`↔`config().`), получаем:

```
total hunks: 93
purely vocabulary-equal after canonicalisation: 65
remaining: 28
```

**65 из 93 — чистое переименование.** Разрешаются правилом «берём апстримную сторону», и почти все они находятся в CSI-обработчиках (`csi_CUU`, `csi_ECH`, `eraseLineBefore`, `rectangleOrigin`, …), где наша правка была ровно `composer.nCols` → `columns_`.

Оставшиеся 28 распадаются на шесть групп с конкретными адресами переезда:

| Наш кусок | Куда едет | Как |
|---|---|---|
| `PaneGeometry.columns/rows` | `VtGeometry.columns/rows` | Прямое отображение. Эмбеддер выдаёт **по одному `VtGeometry` на панель** вместо одного на окно — `Vterm::create` принимает `VtGeometry&` по ссылке, ничто этому не мешает. |
| `PaneGeometry.originX/originY/width/height` | **`VtGeometry` (расширение)** или `VtermImpl` | Проектное решение. У апстрима origin отсутствует как понятие: `borderPixels` скалярный, начало панели всегда (0,0). Рекомендую **добавить `originX/originY` и заменить `borderPixels` на четырёхсторонние инсеты прямо в `VtGeometry`** — это ядро VT, а пиксельная мышь и XTWINOPS считаются в ядре, так что скрыть это в эмбеддере нельзя. |
| `mouseGeometry(composer, ...)`, `mouseCell`, `mouseSelectionCell`, `mouseAutoscrollDirection` | `lib/vterm/mouse_frontend.{h,cpp}` | Уже автомержем оказались там; **но тянут `composer.h` в ядро** — нарушение границы. Чинится заменой параметра `const Composer&` на `const VtGeometry&`: всё, что helper берёт у Composer, — это инсеты, glyph-размеры и pixelWidth/Height, то есть ровно поля `VtGeometry`. Наши четыре места вызова в `vterm.cpp` (строки конфликтов 1675, 2092, 2517, 10253) тогда остаются нашими, а апстримная инлайн-арифметика выбрасывается — наша строго общее. |
| `grid_geometry.h` (`gridColumns/gridRows/gridPixelWidth/gridPixelHeight/cellOrigin`) | **не в `lib/vterm`** | Файл включает `composer.h`. Два выхода: (а) расщепить — чистая арифметика на `Insets` уезжает в `lib/vterm/vt_geometry.h`, а `Composer`-обёртки остаются в `lib/shitty/grid_geometry.h`; (б) оставить целиком в `lib/shitty` и в ядре пользоваться полями `VtGeometry` напрямую. Рекомендую (а): `columnsForPixelWidth()`/`rowsForPixelHeight()` (конфликт 6896) и ответ на XTWINOPS 8t (конфликт 6993) живут в ядре и обязаны считать по четырём сторонам. |
| `CellExtraClient`, `collectExtras()`, `cellCapacity()`, `updateExtraCellCount()`, дедуп корней | `VtCellExtras` в `lib/vterm/cell_extra_store.h` | Наш интерфейс — надмножество. Держим `CellExtraClient` как есть, но список клиентов переезжает с `composer_.cellExtrasChangedListeners` на `VtCellExtras::changedListeners` (апстрим уже завёл это поле). Заодно снимается ещё одна зависимость ядра от Composer. |
| `composer.opts->*` | `config().*` (`VtConfigSlot`) | Механика, но широкая: апстрим вложил VT-ручки в `Options::vt`. Все наши новые опции (`backgroundOpacity`, `quickCornerRadius`, `sidebarWidth`, `quickHotkey`, `quickCompanion`, `quickFullscreenHotkey`, `quickGeometry`, `configPath`, `paneDividerColor`, `sidebarColor`, `sidebarTabs`, `autoHideChrome`, `panes`, …) остаются в `Options` — они оформление, а не семантика VT. Компилятор поймает всё, что не поймал merge. |

Плюс три места, где надо **взять апстримное и дописать наше**:

- `resizedWithHistory(...)` вместо `resized(...)` и проверка `info.saveLines != config().saveLines` (конфликты 2345, 3903, 9453) — апстрим починил, что смена `saveLines` не перестраивала экран. Наша проверка «сетка панели изменилась» дописывается рядом.
- `Screen::createPrimary/Alternate(extras_, ...)` вместо `(composer, ...)` — берём апстримное, наши `columns_/rows_` заменяются на нашу же геометрию панели.
- `parser/notifications/savedPrivModes` теперь строятся на `owner`, а не на `composer.pool` — берём апстримное.

**Оценка `vterm.cpp`: L, но не «переписать заново».** 65 механических хунков + 28 содержательных, из которых половина — однострочники. Реальная работа — четыре проектных решения (origin в `VtGeometry`, инсеты вместо `borderPixels`, судьба `grid_geometry.h`, `CellExtraClient` на `VtCellExtras`), а дальше механика.

### 3.4 `vterm.h`

3 конфликта, все аддитивные. Наши `PaneGeometry`, `show()/hide()`, `retainedOutput()`, `cellCapacity()`, `paneResized()` сосуществуют с апстримными `presentationInvalidated()`, `configChanged()`. Единственное решение — как выглядит `paneResized`: либо остаётся с `PaneGeometry`, либо становится `windowResized()` апстрима, читающим свой `VtGeometry&` (апстримный `VtGeometry::resize()` уже коммитит и оповещает хост). Рекомендую второе: меньше кода и ровно тот контракт, который мы писали в A8 («один вызов, а не сеттер плюс триггер»).

### 3.5 `vterm_headless`

Апстрим переписал headless-хост целиком: `VtermHeadless::create(pool, config, traceFactory, ptyCapture)` — без Composer, хост сам строит `VtGeometry`, `VtConfigSlot`, `VtCellExtras`, `VtHost`. Наши 17 строк в `vterm_headless.cpp` (передача `windowPane(composer)`) **выбрасываются** — берём апстримный файл как есть.

Дорого другое: `lib/shitty/vterm_headless_ut.cpp` (+858 строк наших pane-тестов) апстрим переименовал в `vt_headless_ut.cpp` с 52% схожести — 4 конфликта на 629 строк. Тесты надо переносить на новый API. Смягчает то, что апстрим специально выставил `host()`, `geometry()`, `extras()` «для теста, который выращивает второй терминал у того же окна» — то есть ровно наш сценарий двух панелей.

## 4. Разбор Metal

**Конфликтует по существу, но исход — упрощение нашего кода, а не потеря.**

Мы (A3, `render_arena.h`, `PaneArenaMirror`) построили зеркало арен **на панель**: у каждой панели свой `Screen` со своей ареной полос, мирроринг планирует базы и хвосты по панелям, `biasStrips()` сдвигает полосы на базу панели. 144 строки `render_arena.h` + 239 строк `render_arena_ut.cpp` + 13 конфликтов в `render_metal.mm`.

Апстрим (`a3712f4e` + `44d61bfc`) вынес шейпинг полос из `Screen` в отдельный `SpanShaper` — **один на окно**, `composer.shaper`, одна арена, одна `spanGeneration()`, простая доливка хвоста.

На первый взгляд это лобовое противоречие. Оно им не является:

```cpp
// lib/shitty/span_shaper.h
virtual size_t rowSpans(const TerminalCell* cells, u16 columns, u64 rowId, ScreenRowSpan* out) = 0;
```

Шейпер контент-адресный и принимает `columns` покадрово, а идентичность строки — глобальный счётчик:

```
$ git grep -n 'rowIdentityCounter' origin/master -- lib/vterm/screen.cpp
lib/vterm/screen.cpp:52:static u64 rowIdentityCounter = 0;
lib/vterm/screen.cpp:54:static u64 nextRowIdentity() { return ++rowIdentityCounter; }
```

`rowId` уникальны **между экранами**, а не внутри экрана. Значит один оконный `SpanShaper` корректно обслуживает несколько панелей разной ширины: кэш строк не пересечётся, арена одна, поколение одно.

Следствие: **`PaneArenaMirror` перестаёт быть нужен**. Он существовал только потому, что арены были у каждого `Screen` свои. С единой ареной наша `uploadArenas()` схлопывается в апстримный вариант (хвост + `waitFrames()` при смене поколения), а из нашего остаётся то, что к аренам отношения не имеет:

- адресация ячеек по `cellBase` (панели лежат в одном буфере ячеек друг за другом),
- `update.gridColumns/gridRows` вместо `composer.geometry.*` (A9 — гвард `pane_grid_guard` это и стережёт),
- швы (`seams`, `seamInk`) и `clearBackground` из `opts->vt.bg`,
- `PaneRender`/список панелей в кадре (A2).

Работа: заменить `Screen& shapes` на `SpanShaper& shaper` в `assignRowStrips/overrideOverlayStrips/applySpanStrips`, сохранив параметры `columns`/`cellOffset`; выкинуть `PaneArenaMirror` и его тест либо переписать тест на новый инвариант. Оценка **L по объёму, M по риску**: тут нечего терять по смыслу, есть что аккуратно переносить.

`render_vk.cpp` (наш порт, апстрим его не портировал на шейпер) — 6 конфликтов, все механические: `composer.pixelWidth` → `composer.geometry.pixelWidth`, `opts->bg` → `opts->vt.bg`. Но Vulkan придётся портировать на `SpanShaper` **самим**, иначе `Screen::rowSpans()`/`spanMask()` из ядра исчезнут. Это отдельная задача, апстрим её за нас не сделал.

`render_reference.cpp`/`_ut.cpp` — 19 + 4 конфликта, то же самое: софтверный рендерер тоже читал `Screen` как шейпер.

## 5. Стратегии

### (а) Один большой `git merge origin/master`

- Цена: 30 файлов, 209 хунков, четыре нарушения границы включений — всё одновременно, в одном не собирающемся дереве. До первой успешной сборки не будет ни одного зелёного сигнала.
- Риск: **высокий**. Ошибка в `vterm.cpp` не отличима от ошибки в Metal, пока не соберётся всё. Ревью такого диффа нереалистично.
- Плюс: одна точка ветвления, чистая история.

### (б) Поэтапно, по группам апстримных коммитов

Измеренная накопительная стоимость (`git merge-tree --write-tree --messages HEAD <commit>`):

| Точка апстрима | Тема | Конфликтующих файлов (накопительно) | Новых относительно предыдущей |
|---|---|---:|---|
| `2b16d5bd` | до реорганизации: `-fullscreen`, core_perf, hyperlink на Super, лигатуры | **2** | `options.cpp`, `test_mode.cpp` |
| `da8d6ba3` | стадия границы `lib/vterm` (только схема включений) | 3 | `build.py` |
| `9a5f67c8` | переезд листьев ядра в `lib/vterm` | 10 | `application.cpp`, `composer_ut`, `render_ut`, `session{,_ut}.cpp`, `vterm{,_headless}.cpp` |
| `9c526add` | `VtConfig`: своя конфигурация ядра | 15 | `options.h`, `render_metal.mm`, `render_reference.cpp`, `render_vk.cpp`, `ui_csd_tabs.mm` |
| `a3712f4e` | span shaper отделён от Screen | 18 | `render_reference_ut`, `screen.cpp`, `vterm.h` |
| `25dbda61` | своя embedding-поверхность у ядра | 26 | `main_fuzz`, `application_ut`, `cell_extra_store{,_ut,.h}`, `composer.{h,cpp}`, `vterm_headless_ut` |
| `bd86ed38`…`7e2a8e3e` | растворение `VtState`, переезд файлов, префикс `vt_` | 27 | `pty.h` + переезды |
| `60562f22`, `44d61bfc` | `lib/embed`, порт Metal на шейпер | 27 | — (растёт содержимое, не список) |
| `origin/master` | Darwin pty, `-debug`, релиз | 30 | `.gitignore`, `platform_cocoa.mm`, `pty_test_helper.c` |

- Цена: ~9 мерж-коммитов вместо одного, суммарно та же работа плюс накладные на девять сборок.
- Риск: **низкий**. Каждый шаг собирается и тестируется отдельно; шаг 1 — две строки; тяжёлые шаги (`9a5f67c8`, `9c526add`, `25dbda61`) изолированы и каждый имеет понятную тему.
- Плюс: `2b16d5bd` даёт нам `-fullscreen`, core_perf и фиксы лигатур **сегодня**, за час работы, до всякого разбора vterm.
- Минус: промежуточные состояния — это апстримный рефакторинг наполовину; наш код в них живёт в двух словарях сразу. Требует дисциплины «не чинить то, что чинится следующим шагом».

### (в) Rebase 537 наших коммитов поверх апстрима

- Цена: 537 возможностей конфликта вместо 30 файлов. Наши коммиты внутри себя многократно переписывали одни и те же строки `vterm.cpp` (11 коммитов только по нему), и каждый пришлось бы переигрывать в апстримной вселенной.
- Риск: **очень высокий**, работа кратно больше, а история форка (13 волн с отчётами, на которые ссылаются `docs/`) переписывается и разъезжается с отчётами.
- Единственный аргумент за — линейная история — форку не нужен.

### Рекомендация

**(б) — поэтапный мерж по семи волнам.**

Почему: цифры выше показывают, что стоимость не размазана равномерно, а сосредоточена в трёх апстримных коммитах (`9a5f67c8`, `9c526add`, `25dbda61`). Поэтапный мерж превращает «30 конфликтующих файлов в неизвестном состоянии» в семь задач, каждая со своей темой, своим набором файлов и своим критерием готовности — сборка. При этом первая волна (2 файла) даёт немедленную пользу и проверяет весь конвейер до того, как мы вложимся в vterm.

Вариант (а) стоит рассматривать только если решено делать мерж одним человеком за один заход без параллелизации — тогда девять сборок не окупаются. Вариант (в) отвергается.

## 6. Нарезка на задачи

Параллелить можно **ограниченно**. Волны W0–W3 строго последовательны: каждая следующая мержит апстримный коммит поверх результата предыдущей, ветка одна. Внутри волн W4–W6 задачи разделяются по файлам и допускают параллельных исполнителей на одной ветке-мерже (после того как мерж-коммит с конфликтами разрешён по своим «якорным» файлам).

Ниже `M<n>` — мерж-шаг (последовательный, один исполнитель), `T<n>` — задача внутри шага.

### W0 — подготовка (можно параллельно, до любого мержа)

| Задача | Файлы | Зависимости | Проверка |
|---|---|---|---|
| **T0.1** Зафиксировать базовую зелёную линию: собрать и прогнать всё на текущем master, записать эталон | — | — | `./build st -j 10`, `./build unit_tests -j 10`, `./build st_test pt_test -j 10` |
| **T0.2** Вычистить из индекса `.omc/`, `.repowise/`, `.DS_Store` (17 файлов), добавить в `.gitignore` | `.gitignore`, индекс | — | `git status --short` чист |
| **T0.3** Прочитать `docs/architecture/2026-08-18-panes-and-window-chrome.md` (A1–A11) и выписать инварианты в чек-лист мержа | `docs/` | — | чек-лист существует |

### W1 — «дешёвый апстрим» (`git merge 2b16d5bd`) — последовательно

| Задача | Файлы | Зависимости | Проверка |
|---|---|---|---|
| **M1** `git merge 2b16d5bd`, разрешить 2 конфликта | `lib/shitty/options.cpp`, `lib/shitty/test_mode.cpp` | T0.1 | `./build st -j 10`; `./build unit_tests -j 10`; `tst/test_options.py`, `tst/test_startup.py`, `tst/test_window_operations.py` |

Приобретения волны: `-fullscreen`, применение стартового состояния окна в test mode, `core_perf`, гиперссылка на Super, лигатурные чернила в пустых ячейках.

### W2 — граница и переезд листьев (`da8d6ba3`, `9a5f67c8`) — последовательно

| Задача | Файлы | Зависимости | Проверка |
|---|---|---|---|
| **M2** `git merge da8d6ba3` — схема включений `lib/vterm` | `build.py` | M1 | `./build st -j 10` |
| **M3** `git merge 9a5f67c8` — листья ядра переезжают в `lib/vterm` | `build.py`, `application.cpp`, `composer_ut.cpp`, `render_ut.cpp`, `session.cpp`, `session_ut.cpp`, `vterm.cpp`, `vterm_headless.cpp`, `options.cpp`, `test_mode.cpp` | M2 | `./build st -j 10`, `./build unit_tests -j 10` |
| **T2.1** Обновить наши гварды под новые пути: `guard_scan_roots` += `lib/vterm`, перекеить `border_pixels_allowance` и `mouse_geometry_allowance` | `build.py` | M3 | `./build border_pixels_guard mouse_geometry_guard pane_grid_guard darwin_call_guard` |

**T2.1 обязательна и легко теряется.** Наши гварды сканируют `guard_scan_roots = ("lib/shitty", "ext/plt", "bin")` и ведут разрешения по ключам вида `"lib/shitty/mouse_frontend.h"`. После переезда просканированные файлы уезжают в `lib/vterm`, гварды **молча перестают их видеть** — инварианты A1/A8/A9 остаются без охраны, а сборка при этом зелёная.

### W3 — конфигурация ядра (`9c526add`) — последовательно, затем параллельно

| Задача | Файлы | Зависимости | Проверка |
|---|---|---|---|
| **M4** `git merge 9c526add`, разрешить якорные файлы: разнести `Options` / `Options::vt` | `options.h`, `options.cpp` | M3, T2.1 | компилируется `options_ut` |
| **T3.1** Перевести все `opts->` на `opts->vt.` в терминальном коде | `vterm.cpp`, `test_mode.cpp` | M4 | `./build st -j 10` |
| **T3.2** То же в рендерерах | `render_metal.mm`, `render_vk.cpp`, `render_reference.cpp` | M4 | `./build st -j 10` |
| **T3.3** То же в UI/сессии | `ui_csd_tabs.mm`, `session.cpp`, `application.cpp` | M4 | `./build st -j 10` |
| **T3.4** Обновить FONT_STATE-строку test mode (наши 20 полей + апстримный `fullscreen=`) и тест | `test_mode.cpp`, `tst/test_config.py`, `tst/test_options.py` | T3.1 | `tst/test_config.py`, `tst/test_options.py`, `tst/test_defaults.py` |

T3.1–T3.3 параллелятся (разные файлы), T3.4 после T3.1.

### W4 — span shaper (`a3712f4e`) — якорь последовательный, рендереры параллельно

| Задача | Файлы | Зависимости | Проверка |
|---|---|---|---|
| **M5** `git merge a3712f4e`; разрешить `screen.cpp` (наш `CellExtraClient` внутри удаляемого блока) и `vterm.h` | `lib/vterm/screen.cpp`, `lib/vterm/vterm.h` | W3 | `./build st -j 10` |
| **T4.1** Metal на единый `SpanShaper`: удалить `PaneArenaMirror`, сохранить `cellBase`/`update.gridColumns`/швы | `render_metal.mm`, `render_arena.h`, `render_arena_ut.cpp` | M5 | `./build st -j 10`; `tst/test_gpu_smoke.py`, `tst/test_gpu_parity.py`, `tst/test_font_ligature.py`, `tst/test_wide_ligature_overflow.py` |
| **T4.2** Портировать Vulkan на `SpanShaper` (апстрим этого не делал) | `render_vk.cpp` | M5 | `tst/test_gpu_parity.py` на Linux/CI |
| **T4.3** Софт-рендерер на `SpanShaper` | `render_reference.cpp`, `render_reference_ut.cpp` | M5 | `./build unit_tests`; `tst/test_soft_render.py`, `tst/test_render_contract.py`, `tst/test_damage_only_frames.py` |

T4.1–T4.3 параллелятся полностью.

### W5 — embedding-поверхность и растворение `VtState` (`25dbda61`…`7e2a8e3e`) — **самая тяжёлая, последовательная**

| Задача | Файлы | Зависимости | Проверка |
|---|---|---|---|
| **M6** `git merge 25dbda61` | 26 файлов | W4 | — |
| **T5.1** Проектное решение и его реализация: `originX/originY` + четырёхсторонние инсеты в `VtGeometry`; `mouseGeometry()` принимает `const VtGeometry&` вместо `const Composer&` | `lib/vterm/vt_geometry.{h,cpp}`, `lib/vterm/mouse_frontend.{h,cpp}` | M6 | `python3 lib/vterm/check_includes.py lib/vterm <stamp>` проходит; `./build unit_tests`; `tst/test_mouse_frontend_pointer.py`, `tst/test_mouse_frontend_scroll.py`, `tst/test_mouse.py` |
| **T5.2** Расщепить `grid_geometry.h`: арифметика на `Insets` → `lib/vterm`, `Composer`-обёртки → `lib/shitty` | `grid_geometry.h`, `grid_geometry_ut.cpp`, `vterm.cpp`, `application.cpp`, `test_mode.cpp` | T5.1 | `./build unit_tests`; граница включений проходит |
| **T5.3** `CellExtraClient` на `VtCellExtras::changedListeners` вместо `composer_.cellExtrasChangedListeners`; сохранить дедуп корней и `cellCapacity()` | `lib/vterm/cell_extra_store.{h,cpp}`, `cell_extra_store_ut.cpp`, `vterm.cpp` | M6 | `./build unit_tests`; `tst/test_cells.py`, `tst/test_sixel.py`, `tst/test_hyperlink_input.py` |
| **T5.4** `Vterm::create` с 10 аргументами в `Session`: `VtGeometry` на панель | `session.cpp`, `session.h`, `session_ut.cpp`, `pane_layout.{h,cpp}` | T5.1 | `./build unit_tests`; `tst/test_resize*.py` |
| **T5.5** 65 механических хунков `vterm.cpp` (`columns_`/`rows_` → геометрия панели) | `lib/vterm/vterm.cpp` | T5.1 | `./build st -j 10` |
| **T5.6** Взять апстримное: `resizedWithHistory`, проверка `saveLines`, `Screen::createPrimary(extras_, …)`, пулы на `owner` | `lib/vterm/vterm.cpp` | T5.5 | `tst/test_resize_history_capacity.py`, `tst/test_scrollback.py` |
| **T5.7** `childPid()` перенести в `lib/vterm/pty.h` | `lib/shitty/pty.h`, `lib/vterm/pty.h`, `pty_ut.cpp` | M6 | `tst/test_pty.py`, `tst/test_pty_output.py`, `tst/test_pty_streaming.py` |
| **T5.8** `Composer`: наш A10 (`chromeInsets/paneInsets/contentInsets/setChromeReserve/scaledPixels`) сосуществует с апстримными `installVtHost()`/`setOptions()`/`debugFd` | `composer.{h,cpp}`, `composer_ut.cpp` | T5.1 | `./build unit_tests` |
| **T5.9** Взять апстримный `vt_headless.{cpp,h}` целиком; наши 17 строк выбросить | `lib/vterm/vt_headless.*`, `lib/shitty/vterm_headless.*` (удалить) | M6 | сборка |
| **T5.10** Перенести наши +858 строк pane-тестов на новый API `VtermHeadless` (через `host()`/`geometry()`/`extras()`) | `lib/shitty/vt_headless_ut.cpp` | T5.9, T5.4 | `./build unit_tests -j 10` |
| **T5.11** `bin/main_fuzz` на новую сигнатуру | `bin/main_fuzz/main.cpp` | T5.9 | `./build -j 10` |

T5.1 — **блокирующая для всей волны**: пока не решено, где живут origin и инсеты, T5.2/T5.4/T5.5 не начинаются. T5.3, T5.7, T5.9, T5.11 параллельны T5.1. T5.10 — последняя.

### W6 — embed, Metal-порт, хвост (`60562f22`, `44d61bfc`, `origin/master`)

| Задача | Файлы | Зависимости | Проверка |
|---|---|---|---|
| **M7** `git merge 44d61bfc` (включает `lib/embed`) | `render_metal.mm` и др. | W5 | `./build st -j 10` |
| **T6.1** Слить `build.py`: наши четыре гварда + апстримный `vterm_boundary` + `vterm_source` на новый путь + сборка `lib/embed` | `build.py` | M7 | `./build -j 10 --list`; все гварды зелёные |
| **M8** `git merge origin/master` — остаток | `.gitignore`, `platform_cocoa.mm`, `pty_test_helper.c` | T6.1 | — |
| **T6.2** `platform_cocoa.mm`: наш Accessory для quick-окна + апстримный fallback имени процесса | `ext/plt/platform_cocoa.mm` | M8 | `./build st -j 10`; ручная проверка quick-окна |
| **T6.3** `tst/pty_test_helper.c`: апстримный Darwin sigwait/canonical + наш `catch_signal` | `tst/pty_test_helper.c` | M8 | `tst/test_pty.py` на macOS |
| **T6.4** `.gitignore` | `.gitignore` | M8 | — |

### W7 — приёмка

| Задача | Зависимости | Проверка |
|---|---|---|
| **T7.1** Полная сборка и все тесты | W6 | см. §8 |
| **T7.2** Ручная приёмка панелей и оформления на macOS | T7.1 | см. §8 |
| **T7.3** Ревью по инвариантам A1–A11 из арх-документа | T7.1 | чек-лист T0.3 закрыт |

**Итого: 8 волн (W0–W7), 8 мерж-шагов, 25 задач.** Полноценно параллелятся T3.1–T3.3, T4.1–T4.3, и внутри W5 — {T5.3, T5.7, T5.9, T5.11} против T5.1. Всё остальное последовательно, потому что мерж-коммиты живут на одной ветке.

## 7. Что мы получаем, что теряем, что спорно

### Полезное

- **`lib/embed`** — C-фасад над ядром (`shitty_vt.{h,cpp,map}`, `link_shared.py`, `make_release.py`, `bin/example/main.c`, `tst/test_embed_example.py` на ~690 строк). Прямой пользы форку нет, но именно ради него апстрим вычистил Composer из ядра — а от этой чистки мы выигрываем: наши панели давно упирались в то, что `Vterm` читал окно.
- **Darwin pty**: `f482c269 Solve the Darwin pty mysteries: xnu sigwait and canonical discard` плюс серия проб. Мы на macOS — это прямо наши баги.
- **`-fullscreen`** и применение стартового состояния окна в test mode.
- **`-debug`** (`debug_trace.{cpp,h}`, `tst/test_debug_trace.py`) — трассировка для невоспроизводимых репортов.
- **`vterm_boundary`** — проверка границы включений. Идеологически то же, что наши четыре гварда; принимаем как своё.
- **`SpanShaper`** — снимает необходимость в нашем `PaneArenaMirror` (см. §4).
- **`resizedWithHistory` / проверка `saveLines`** — реальный фикс: смена `saveLines` не перестраивала экран.
- Лигатуры: `833c4cb4`, `tst/test_wide_ligature_overflow.py`, отрисовка крупных глифов через outline.
- Гиперссылки по Super, а не только Control.

### Нейтральное

- `core_perf`, `bin/parser_perf`, `toml_dump` — бенчмарки.
- Darwin-шарды CI, `.github/workflows/*` — сольются, наши изменения релиза (`dev/release.py`, `.github/workflows/release.yml`, `dev/make_app.sh`, `dev/package_darwin_apps.sh`) конфликтов не дали.
- Шрифт Amiri, брендонейтральность fallback-меню.

### Конфликтует по замыслу, а не по строкам

1. **`VtGeometry` — одна на окно у апстрима, одна на панель у нас.** Формально ссылка в `Vterm::create` это позволяет, но апстримный `VtGeometry::resize(w, h, host)` явно комментирует: «каждый терминал за окном должен услышать изменение геометрии, и только эмбеддер знает их всех». Наш `paneResized` адресный. Нужно решить, кто оповещает панели: `VtHost::resized()` (апстримный путь) или наш `applyLayout()`. Рекомендую наш — он уже знает дерево панелей.
2. **`borderPixels` скаляр против четырёхсторонних `Insets`.** Апстрим считает границу одним числом в ядре (пиксельная мышь, XTWINOPS 8t/14t/16t). Наш A1/A10 говорит, что это неверно, как только у окна есть хром. Наша сторона правее и должна победить, но это меняет **публичное поле апстримного ядра** — расхождение с апстримом углубляется, а не сокращается.
3. **`retainedOutput()`/A9-сетка в `TerminalUpdate` против единого шейпера.** Наш `pane_grid_guard` запрещает рендереру читать сетку окна; апстримный Metal после порта делает ровно это (`cellRows`, `cellColumns` из `composer.geometry`). После T4.1 гвард обязан снова быть зелёным — это лучший индикатор того, что порт не потерял A9.
4. **`activate()/deactivate()` → `show()/hide()`.** Апстрим в `session.cpp` добавил воспроизведение состояния окна при активации (`focus(focused_)`, `pointerPresence(pointerPresent_)`) — у нас это ушло в `refocus()`. Слить надо смыслово: воспроизведение состояния адресуется **сфокусированной** панели, а `show()` — всем панелям вкладки.
5. **`Options` против `VtConfig`.** Наши 14+ опций оформления не имеют места в `VtConfig` и остаются в `Options` — это правильно, но означает, что `Options` у нас навсегда шире апстримного, и каждый следующий мерж будет трогать `options.{h,cpp}`.

### Теряем

- Наши 17 строк в `vterm_headless.cpp` — их не жалко, файл переписан.
- `PaneArenaMirror` и `render_arena_ut.cpp` (383 строки) — если решение §4 верно, они становятся мёртвым кодом. Перед удалением стоит убедиться, что единая арена действительно не даёт разъезда поколений между панелями.
- Ничего из фич не теряется: сплиты, боковая панель вкладок, автоскрытие, скруглённые углы, quick-окно, прозрачность — их файлы (`pane_layout.*`, `quick_*.{h,cpp,mm}`, `ui_sidebar_tabs.*`, `ui_quick_hotkey.*`, `tint_coat.h`, `ui_window_tint.h`) апстрим не трогал вовсе и в конфликтах не участвуют.

## 8. Чек-лист верификации

Штатный Apple clang. **Не выставлять `CC`/`CXX`** — Homebrew LLVM не установлен.

### После каждой волны

```bash
./build st -j 10
./build unit_tests -j 10
```

### После W2 и далее — гварды

```bash
./build border_pixels_guard mouse_geometry_guard pane_grid_guard darwin_call_guard -j 10
./build vterm_boundary -j 10          # появляется после W2
python3 lib/vterm/check_includes.py lib/vterm /tmp/vt-boundary.stamp   # быстрая ручная проверка
```

`pane_grid_guard` — главный индикатор того, что A9 пережил порт рендереров.

### Финальная приёмка (W7)

```bash
./build -j 10                                  # всё, включая pt, st_test, pt_test, plt_tests
./build unit_tests -j 10
./build unit_tests_group_00 ... unit_tests_group_19 -j 10   # если гоняются по группам
./build st_test pt_test plt_tests plt_unit_tests -j 10
```

Тесты `tst/` по темам мержа:

| Тема | Тесты |
|---|---|
| Геометрия и ресайз | `test_resize.py`, `test_resize_reflow.py`, `test_resize_viewport.py`, `test_resize_history_capacity.py`, `test_resize_same_grid_pixels.py`, `test_resize_in_band_matrix.py`, `test_resize_margins_tabs.py`, `test_resize_cursor_autowrap.py`, `test_resize_wide_grapheme.py`, `test_font_resize.py`, `test_output_scale.py` |
| Указатель и мышь | `test_mouse.py`, `test_mouse_frontend_pointer.py`, `test_mouse_frontend_scroll.py`, `test_selection.py`, `test_selection_autoscroll.py`, `test_selection_geometry`-семейство ghostty |
| Рендер | `test_render_contract.py`, `test_soft_render.py`, `test_damage_only_frames.py`, `test_gpu_smoke.py`, `test_gpu_parity.py`, `test_renderer_replacement.py`, `test_font_ligature.py`, `test_wide_ligature_overflow.py`, `test_italic_overhang.py` |
| Опции и конфиг | `test_options.py`, `test_config.py`, `test_defaults.py`, `test_startup.py`, `test_toml.py` |
| Окно | `test_window_operations.py`, `test_tmux_regress_window_ops.py`, `test_desktop_identity.py` |
| pty (Darwin!) | `test_pty.py`, `test_pty_output.py`, `test_pty_streaming.py` |
| Склад extra | `test_cells.py`, `test_sixel.py`, `test_hyperlink_input.py`, `test_scrollback.py` |
| Апстримные новые | `test_debug_trace.py`, `test_embed_example.py` |

### Ручная приёмка на macOS

Сверяясь с `docs/plans/state/manual-checks-wave10.md` и `docs/plans/state/panes-status.md`:

1. Сплит по вертикали и по горизонтали; ввод идёт только в сфокусированную панель; соседняя рисуется и не мигает.
2. Ресайз окна при двух панелях: обе получают правильную сетку, шелл в обеих видит верный `stty size`.
3. Боковая панель вкладок слева: терминал не заезжает под неё; `cmd+b` расширяет терминал и шелл получает новый размер.
4. `autoHideChrome`: наведение открывает хром, геометрия сетки не меняется.
5. Quick-окно: горячая клавиша, скруглённые углы, отсутствие иконки в Dock, `-quickGeometry`, запоминание рамки.
6. Полупрозрачность сайдбара и фона; глифы, курсор, выделение и разделитель панелей остаются непрозрачными.
7. Тихая панель: в соседней панели идёт вывод, тихая не перерисовывается целиком и не пропадает из кадра (`retainedOutput`).
8. Новое от апстрима: `-fullscreen` при старте, `-debug` пишет трассу, гиперссылка по Cmd-клику.

## 9. Обнаружено

1. **В историю форка закоммичен операционный мусор.** 17 файлов: `.omc/project-memory.json`, `.omc/state/**` (включая 614-строчный `agent-replay-*.jsonl` и `subagent-tracking.json`), `.repowise/*.db` (бинарные SQLite), `.DS_Store`, и даже `lib/shitty/.omc/state/sessions/.../pre-tool-advisory-throttle.json` — то есть каталог состояния агента внутри исходников библиотеки. Апстрим их не видит, конфликтов они не дают, но они шумят в каждом диффе и в `git status` прямо сейчас. Чистка — T0.2.

2. **Наши гварды после переезда молча ослепнут.** `guard_scan_roots = ("lib/shitty", "ext/plt", "bin")` не включает `lib/vterm`, а `border_pixels_allowance`/`mouse_geometry_allowance` ключуются полными путями `lib/shitty/...`. После W2 файлы уезжают, гварды продолжают быть зелёными, охраняя пустоту. Это худший класс регрессии — проверка, которая проходит, потому что ей нечего проверять. Вынесено в отдельную задачу T2.1.

3. **`git merge-tree` показывает, что цена реорганизации сосредоточена в трёх коммитах.** `9a5f67c8` (+7 файлов), `9c526add` (+5), `25dbda61` (+8) дают 20 из 30 конфликтных файлов. `f3de9de6` («Move the VT core into lib/vterm») и `7e2a8e3e` («Rename to vt_ prefix») — 76 переименований — не добавляют **ни одного** нового конфликтующего файла. То есть «переезд файлов» бесплатен, платим за смену модели.

4. **Апстримные R-метки рождают ложные пары.** `git diff -M` сообщает `lib/shitty/vterm_test.cpp → lib/vterm/pty.cpp (R081)` и `lib/shitty/vterm_trace.cpp → lib/vterm/vt_config.cpp (R081)`. Это артефакт эвристики схожести (общая шапка лицензии + похожая структура), а не реальный переезд: `vt_trace.cpp` и `vt_test.cpp` в апстриме — новые файлы-заглушки на 7 строк. При ручном разборе не поддаваться этой подсказке.

5. **Наши тесты `test_vulkan_*.py` переименованы в `test_gpu_*.py`** (R068/R057) — апстрим этих файлов не трогал, конфликта нет, но при слиянии списков в `build.py` надо не потерять новые имена.

6. **`rowIdentityCounter` — статический глобал на процесс** (`lib/vterm/screen.cpp:52`). Это то, что делает единый оконный `SpanShaper` пригодным для многопанельного окна. Если апстрим когда-нибудь сделает счётчик пер-экранным ради `lib/embed` (несколько независимых терминалов в одном процессе), наш многопанельный шейпинг сломается тихо — строки разных панелей начнут коллидировать в кэше. Стоит записать это как явное допущение в арх-документ и, если возможно, закрыть тестом.

7. **`Options` форка навсегда шире апстримного.** После разноса на `Options`/`Options::vt` наши 14+ опций оформления остаются в внешнем слое. Это правильно по смыслу, но означает, что `options.h`/`options.cpp` будут конфликтовать при **каждом** будущем мерже апстрима. Дешёвая профилактика — сгруппировать наши поля в один вложенный `struct Chrome`, тогда апстримные вставки и наши перестанут делить одни и те же строки.

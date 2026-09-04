# `T7.3` — ревью по инвариантам `A1`–`A11` после закрытия мержа

**Вердикт: мерж прошли все одиннадцать инвариантов. Одиннадцать из одиннадцати
доказаны мутацией. Три наблюдателя стоят в точке, где проверяемая величина равна
нулю, и один из них — единственный прямой наблюдатель `A7`. Два пункта
арх-документа устарели (`A3` — ожидаемо планом, `A7` — нет).**

- **Задача:** `T7.3`, волна 8 плана `docs/plans/2026-08-29-upstream-merge.md`
- **Дата:** 2026-09-05
- **Дерево:** `master` = `1cd208f9` («merge M8e»), `git rev-list --count HEAD..origin/master` = **0**
- **Ветка ревью:** `review/T7.3`
- **Источники:** инварианты — `docs/architecture/2026-08-18-panes-and-window-chrome.md`;
  чек-лист — `docs/plans/reviews/upstream-merge-invariants.md` (`T0.3`)

## Как снята база

**Измерено.** `./build unit_tests pty_test_helper`, затем
`SHITTY_PTY_TEST_HELPER=$PWD/.build/pty_test_helper ./.build/unit_tests < /dev/null`
→ **`OK: 982`, `EXIT=0`**, ни одного красного. Каждая мутация ниже мерялась от
этой базы; после каждой файл возвращён и `git status` пуст. Итоговая сверка после
всех мутаций: снова `OK: 982`, дерево чистое.

**Измерено.** Четыре сканирующих гварда исполнены **напрямую** — тексты программ
извлечены из `build.py` через `ast` и запущены по рабочему дереву, минуя
`./build` и его CAS (приём `G1`, основание — `CLAUDE.md`, «Штамп из CAS»). Все
четыре `rc=0`. `vterm_boundary` запущен своим скриптом:
`python3 lib/vterm/check_includes.py lib/vterm <stamp>` → `rc=0`, `ALLOWANCE` пуст.

**Измерено.** Питоновские наблюдатели `A5`: `tst/test_pane_alt_screen.py` и
`tst/test_pane_frame_recovery.py` со своим окружением из `build.py`
(`SHITTY_TEST_BINARY=$(B)/st_test`, `SHITTY_TOML_DUMP_BINARY`, `SHITTY_TEST_*`),
запуск из `tst/` — **16 из 16 зелёных**. Это критерий готовности №9 плана.

**Приёмка смотрелась по сигнатурам и типам, а не по спискам включений** — ловушка
`session.h → pane_layout.h → composer.h` названа архитектором заранее и обойдена:
`Composer` искался в `lib/vterm` как **имя типа**, а не как `#include`.

---

## Таблица: `A1`–`A11`

Столбец «доказано мутацией» — что именно было сломано в коде и какой поимённо
названный тест или гвард покраснел. Все мутации откачены.

| | Инвариант | Чем держится сегодня | Наблюдатель | Зелен | Доказано мутацией |
|---|---|---|---|:-:|---|
| `A1` | `border` ≠ резерв хрома; раскладку читает `contentInsets()` | `Composer::chromeInsets()`/`paneInsets()`/`contentInsets()`, `borderPixels()` только как чтение опции; гвард `border_pixels_guard` (`\bborderPixels\s*\(`, по границе слова и со скобкой) | гвард + **24** юнит-теста (`Composer::EachChromeSideKeepsItsOwnReserveOnItsOwnEdge`, `…EveryPixelOfAComposersContentBoxAnswersFromItsOwnSide`, `MouseFrontend::TheChromeReserveIsCountedOnceOnTheWayIntoAPanesGeometry`, `ReferenceRenderer::PlacesTheGridAtInsetsThatDifferOnEveryAxis`, …) | да | **да, дважды.** (1) `composer.borderPixels()` в `lib/shitty/guard_probe.h` → гвард `rc=1`, поимённо. (2) `chromeInsets()` схлопнут в скаляр (максимум по сторонам) → **24 красных** |
| `A2` | кадр = список панелей, один вызов — один презент | `Renderer::update(const PaneUpdate*, size_t)`, `windowPane()`/`surfacePane()`; три бэкенда | **10** тестов (`ReferenceRenderer::DrawsTwoPanesInTheirOwnRectangles`, `…PaneOrderDoesNotMovePanes`, `RendererFrameContract::TheSeamBandIsPaintedIntoTheAirAndNotOverEitherGrid`, `MultiPaneScreenChangeParity::…`) | да | **да, дважды.** (1) цикл по панелям обрезан до первой (`index < 1`) → **10 красных**. (2) фон каждой панели Metal взят у `panes[0]` → `MetalPanes::EachPanesPaddingIsItsOwnBackgroundAndNotItsNeighbours` |
| `A3` | **документ устарел.** `PaneArenaMirror` («панель + поколение») снят: один `SpanShaper` на окно ⇒ одно поколение. Осталось `ArenaMirror` + пер-процессная идентичность строк | `lib/shitty/render_arena.h` (`ArenaMirror::plan`), `rowIdentityCounter` в `lib/vterm/screen.cpp` | `ArenaMirror::SendsTheWholeArenaWhenTheGenerationMoves` (+4 соседних); замена, которую требовал `T4.1`, — `Screen::RowIdentitiesNeverRepeatAcrossScreens` | да | **да, дважды.** (1) `plan()` перестал сравнивать поколение → `ArenaMirror::SendsTheWholeArenaWhenTheGenerationMoves`. (2) счётчик идентичностей сделан **членом экрана** → `Screen::RowIdentitiesNeverRepeatAcrossScreens`. Оговорка — ниже, §«маскированный наблюдатель» |
| `A4` | дерево панелей внутри вкладки | `PaneTree` (`pane_layout.{h,cpp}`), `SessionSetImpl::tabs`/`activeTree()` | **13** тестов `SessionSet::…` (`ABackgroundTabsPanesAreNeitherVisibleNorFocused`, `WindowResizeReachesEveryPaneOfEveryTab`, `ADragDoesNotFollowTheUserIntoTheNextTab`, …) + набор `PaneLayout` | да | **да, дважды.** (1) `activeTree()` → `tabs[0]` вместо `tabs[activeTab_]` → **13 красных**. (2) `split()` делит `root` вместо `focused_` → `PaneLayout::ClosingAPaneWhoseSiblingIsASplitKeepsTheWholeSubtree` **плюс падение набора** (см. §«Обнаружено», №5) |
| `A5` | «видима» ≠ «в фокусе» | `Vterm::show()/hide()/focus(bool)`, `SessionSetImpl::refocus()`, `visiblePanes()` | `SessionSet::ActivationReplaysTheWindowsFocusToTheFocusedPaneAndNotToEveryPaneOfTheTab`, `SessionSet::MovingTheFocusTellsBothPanesAndMovesNeitherOffScreen`; питоновские `test_pane_alt_screen.py` (8) и `test_pane_frame_recovery.py` (8) | да (16/16 питоновских зелёные) | **да.** `refocus()` раздаёт фокус **всем видимым** панелям вместо одной → оба теста красные |
| `A6` | фрейм quick-окна — отдельный файл, запись атомарная, сохранённое побеждает `quickGeometry` | `quick_frame_store.{h,cpp}` (`path + ".tmp." + pid` → `::rename`), `applySavedQuickFrame()` в `application.cpp` | `QuickFrameStore::FailedWriteLeavesThePreviousFrameIntactAndNoTempFileBehind`; `ToggleQuickWindow::ShowAppliesTheSavedFrameOverTheDefaultPlacement` (+4) | да | **да, дважды.** (1) временный путь = целевой (запись мимо `rename`) → `QuickFrameStore::FailedWriteLeavesThePreviousFrameIntactAndNoTempFileBehind`. (2) сохранённый фрейм не загружается → **5** тестов `ToggleQuickWindow::…` |
| `A7` | **документ частично устарел.** Ховер меняет видимость, но не сетку — держится. Но «полоса резервируется постоянно» **отменено** правкой `C11`: режим не резервирует **ничего** | `csdTabsChromeHovered()` не трогает геометрию; `csdTabsChromeAlpha()` — вся ховер-логика | `CsdTabsUi::HoverChangesVisibilityAndNothingElse` (десять проходов), `CsdTabsUi::NothingIsReservedSoTheGridReachesTheTopEdge` | да, но **с нулевым запасом** | **частично.** Ховер, заводящий резерв из ничего (`0 → 28`), пойман. Ховер, **двигающий** найденный резерв (`×2` на входе, `÷2` на выходе) — **все 982 зелёные**. Разбор ниже |
| `A8` | `Vterm` получает свою геометрию и своё начало | `VtGeometry::originX/originY/insets/width/height`, `Vterm::paneResized()`, `mouseGeometry(pane, window)`; гвард `mouse_geometry_guard` (разрешение `lib/vterm/mouse_frontend.{h,cpp}` = **0**) | гвард + **10** тестов (`MouseFrontend::KeepsThePaneOriginApartFromItsOwnBorder`, `SessionSet::EachPaneCountsPointerReportsFromItsOwnOrigin`, `VtermHeadless::PointerReportsCountFromTheOriginTheVtermWasGiven`, …); обратное направление — `ApplicationProduction::TheQuietPaneOfAFrameHandsOverItsRetainedFormAndKeepsTheAnchor` | да | **да, трижды.** (1) `mouseGeometry(composer)` в `lib/vterm/guard_probe.h` → гвард `rc=1`. (2) он же в самом `lib/vterm/mouse_frontend.cpp` → `rc=1` (разрешение `0` работает). (3) `paneOriginX/Y` обнулены → **10 красных**. (4) якорь IME потерял `+ anchorX/anchorY` → `…KeepsTheAnchor` |
| `A9` | размер сетки едет с данными; ноль — отказ | `TerminalUpdate::gridColumns/gridRows`, производители `VtermImpl::fillTerminalUpdate()` и `ReferenceRendererImpl::renderUpdate()`; гвард `pane_grid_guard`, **12** имён, `render*`, без разрешений | гвард + `ReferenceRenderer::DrawsTwoPanesOfDifferentGrids`, `ReferenceRenderer::AFrameWithoutAGridIsRefused`, `MetalPanes::AZeroGridInOnePaneRefusesTheWholeFrame`, `VtermHeadless::CarriesThePaneGridInTheFrameAndNotTheWindows` | да | **да, четырежды.** (1) `composer.geometry.columns` в `lib/shitty/render_probe.h` → `rc=1`. (2) он же в самом `render_reference.cpp:1163` → `rc=1`. (3) снят отказ на нулевой сетке → `…AFrameWithoutAGridIsRefused`. (4) сетка всех панелей взята у `panes[0]` → `…DrawsTwoPanesOfDifferentGrids`. (5) производитель пишет колонки вместо строк → **15 красных** |
| `A10` | отступ окна и отступ панели — два преобразования | `chromeInsets()` / `paneInsets()` / `contentInsets()` порознь; в ядре — `VtGeometry::insets` (панель) против `VtHost::contentInsets()` (окно); `lib/vterm/vt_grid.h` пишет формулу один раз | **21** тест (`ReferenceRenderer::PlacesTheGridAtInsetsThatDifferOnEveryAxis`, `SessionSet::TheHitTestTakesTheChromeReserveOffThePixelAndLeavesThePanesOwnBorderOn`, `VtermHeadless::TheWindowReportsCountTheWindowsOwnReserveAndNotThePanesBorder`, `MouseFrontend::TheChromeReserveIsCountedOnceOnTheWayIntoAPanesGeometry`) | да | **да.** `paneInsets()` снова складывает в себя `chromeInsets()` — ровно «правая панель получает сайдбар второй раз» → **21 красный** |
| `A11` | склад, общий на окно, размеряется суммой по живым панелям | `SessionSetImpl::cellCapacityExcept()` (сумма по `sessions`), `VtHost::cellCapacityExcept()`, `VtermImpl::updateExtraCellCount()` (`count = своё + чужое`) | `SessionSet::SumsTheExtraStoreBudgetOverEveryLivePane`; `VtermHeadless::SizesTheSharedExtraStoreByThePaneWhenThereIsNoPaneList`, `…CountsBothScreensOnceThePaneHasEnteredTheAlternate`, `…TheExtraStoreBudgetIgnoresInputThatEntersNoScreen` | да | **да, трижды.** (1) сумма → максимум → `SumsTheExtraStoreBudgetOverEveryLivePane`. (2) вызывающий перестал считать себя → **4 красных**, включая оба headless-наблюдателя. (3) `if (false && altScreenInitialized)` → `…CountsBothScreensOnceThePaneHasEnteredTheAlternate` |

**Итог по числам: 11 из 11 доказаны мутацией.** Всего поставлено 22 мутации
(17 в коде, 5 проб в гварды), все откачены, дерево чистое.

---

## Наблюдатели с нулевым запасом

**Три.** Все три — измерены, а не выведены.

### 1. `A7` · `CsdTabsUi::HoverChangesVisibilityAndNothingElse` — вырожден

Тест, который чек-лист `T0.3` назвал «прямой проверкой ровно этого свойства»,
десять раз водит указатель через границу и после каждого шага утверждает

```cpp
const u16 strip = composer.chromeReserve(ChromeSide::Top);
STD_INSIST(strip == 0);                                   // ← премисса пинит НОЛЬ
…
STD_INSIST(composer.chromeReserve(ChromeSide::Top) == strip);
STD_INSIST(composer.contentInsets().top == strip);
```

Фикстура ставит `options.border = 0`, режим `autoHideChrome = true` **резервирует
ноль** (правка `C11`), поэтому `strip == 0` и `contentInsets().top == 0`. Все
сорок утверждений цикла сравнивают ноль с нулём.

**Измерено мутацией.** `csdTabsChromeHovered()` заставлен двигать резерв — удваивать
на входе указателя и делить пополам на выходе. Это ровно то нарушение, ради
которого `A7` заведена (движение указателя пересчитывает сетку и шлёт `SIGWINCH`).
Результат: **`OK: 982`, ни одного красного во всём наборе.**

Обратное направление тест ловит: мутация «ховер заводит резерв `28` из ничего»
даёт ровно один красный — этот самый тест. То есть наблюдатель различает
**появление** резерва и слеп к **изменению** уже существующего.

### 2. `A7` · `CsdTabsUi::NothingIsReservedSoTheGridReachesTheTopEdge`, ветка Retina — вырождена

Комментарий в теле теста говорит: «И то же на Retina: `contentInsets()`
масштабирует резерв, так что резерв, вернувшийся как отмасштабированный титлбар,
взял бы там вдвое больше строк и был бы пойман здесь». Ноль на масштабе 2.0 —
по-прежнему ноль.

**Измерено мутацией.** `chromeInsets()` перестал масштабировать сторону `Top`
(отдаёт пункты вместо пикселей). Красными стали **три** теста `composer_ut`
(`ContentInsetsReserveTheTitleBarStripOnTopInBackingPixels`,
`EveryPixelOfAComposersContentBoxAnswersFromItsOwnSide`,
`ResizeTakesTheStripOutOfTheRowsAndLeavesTheColumnsAlone`) — те, что сами ставят
ненулевой резерв. `NothingIsReservedSoTheGridReachesTheTopEdge` **зелёный**.

Инвариант при этом не остался без наблюдателя: масштабирование резерва стерегут
`composer_ut`. Не работает именно та ветка, которая объявлена в тесте как её
собственная проверка.

### 3. `A8` · `ApplicationProduction::TheAnchorOffTheHomeCellTakesItsColumnFromTheWidthAndItsRowFromTheHeight` — вырожден по одной оси, и **честно об этом говорит**

Тест — образцовый по приёму `F7`: он утверждает свои опорные величины **до**
проверки поведения и прямо перечисляет четыре способа «тихо перестать проверять».
Но одна из четырёх премисс —

```cpp
STD_INSIST(drive.cursorPaneOriginX == 0);
STD_INSIST(drive.cursorPaneOriginY == 0);
```

— **закрепляет ноль**. То есть обратное направление `A8` («ячейка → пиксель»),
уточнение волны 5, этим тестом при ненулевом начале панели не проверяется вовсе.
Различить `insets.left` от `insets.left + originX` он не может по построению.

Дыру закрывает **другой** тест, и ровно один:
`ApplicationProduction::TheQuietPaneOfAFrameHandsOverItsRetainedFormAndKeepsTheAnchor`.

**Измерено мутацией.** Из `application.cpp:800` убрано `+ anchorX, + anchorY` —
якорь окна кандидатов теряет начало панели. Красный **один**: `…KeepsTheAnchor`.

Разница с предыдущими двумя важна: здесь ноль в фикстуре **осознан и записан**, а
покрытие вынесено в соседний тест. Это не дефект теста, это единственный
наблюдатель на важном месте.

### Чего в списке нет: `A11`

Борд числил отложенным подозрение, что `A11` охраняют **два теста, стоящие в
точке, где проверяемое обнуляется**. **Подозрение не подтвердилось — измерено.**

Оба наблюдателя живы и несут премиссу внутри себя:

- `SessionSet::SumsTheExtraStoreBudgetOverEveryLivePane` берёт `saveLines = 100`
  (без него «сумма» и «окно» — одно число, и это была одна из восьми вырожденных
  фикстур мержа) и **прямо утверждает различность слагаемых**:
  `2 * half > whole`, `half * 10 != 2 * half * 10`, `whole * 10 != 2 * half * 10`.
- `VtermHeadless::SizesTheSharedExtraStoreByThePaneWhenThereIsNoPaneList` утверждает
  `paneCells < windowCells` и затем **обе границы** — `>= paneCells * 10` и
  `< windowCells * 10`, — так что «бюджет просто перестал обновляться» краснеет.

Три независимые мутации (сумма→максимум; вызывающий не считает себя; альт-экран
не считается) дали соответственно 1, 4 и 1 красный, все — поимённо ожидаемые.

### Маскированный наблюдатель (четвёртый случай, другого класса)

`A3` · `Screen::RowIdentitiesNeverRepeatAcrossScreens`. Замена, которую `T4.1`
обязана была оставить, работает — но **не на всякой форме нарушения**.

- Счётчик сделан **членом экрана** (`++ownIdentity`) → тест краснеет. ✅
- Счётчик сброшен в ноль **в `Screen::createPrimary()`** — то есть пер-экранная
  нумерация, введённая через сброс, — → **`OK: 982`, ни одного красного.**

Причина не в нуле, а в порядке: тест создаёт оба экрана, **а потом** пишет в них
вперемежку, поэтому сброс при конструировании происходит до выдачи первой
идентичности и ни на что не влияет. Тест был бы устойчив, если бы писал в первый
экран **до** создания второго.

---

## Расхождения с апстримом: ни одно инварианта не нарушает

Проверено по типам и сигнатурам, а не по спискам включений.

| Расхождение | Инвариант | Вердикт |
|---|---|---|
| `Vterm::create` десятипараметрический (у апстрима девять) | `A8` | **несёт** `A8`: десятый — `const VtGeometry& geometry`, геометрия **панели**, отдельная от `VtGeometry& windowGeometry` (второй параметр). Именно это разделение `A8` и требует |
| `Composer` убран из `lib/vterm` целиком | `A1`, `A10`, граница | **несёт.** Измерено: `grep -rn "Composer" lib/vterm/` даёт **одно** вхождение — комментарий `vterm.h:333`, объясняющий удаление. Ни объявления, ни параметра, ни поля |
| `vt_headless.{h,cpp}` в `lib/shitty`, а не в `lib/vterm` | `A5`, `A9`, `A11`, граница | **несёт.** Это адаптер эмбеддера; он стоял по «шитой» стороне границы, живя в ядре. Переезд — то, что позволило `ALLOWANCE` в `check_includes.py` стать пустым |
| `VtHost` с `contentInsets()`, `surfaceResized()`, `cellCapacityExcept()` | `A1`/`A10`, `A7`, `A11` | **несёт все три.** Это ровно те три величины, которых у ядра нет и не должно быть: резерв окна (`A1`/`A10` — «резерв хрома не доходит до ядра»), фиксация размера поверхности эмбеддером, и список живых панелей (`A11` — «сумма, а не последний записавший»). Все три **спрашиваются**, а не хранятся: копия в ядре была бы вторым местом, знающим, сколько занято слева |
| `windowResized()` → `paneResized(geometry)` | `A8` | **несёт.** Адресное оповещение панели вместо оконного. Наблюдается (прочитано) `SessionSet::TheResizeTellsEveryPaneTerminalAndNotOnlyItsShell` |
| новые `lib/vterm/vt_grid.{h,cpp}` | `A1`, `A10` | **несёт.** Четыре функции над `(прямоугольник, VtInsets)` — то место, где раньше стояло `2 * borderPixels`. Заведён отдельным файлом, а не в `vt_geometry.h`, сознательно: тот заголовок документирован как геометрия **панели**, а три из четырёх вызывающих в ядре спрашивают про **окно**, и смешение размыло бы ровно то, что стережёт `A10` |
| `vterm_boundary` шесть из шести, `ALLOWANCE` пуст | все | **несёт.** Измерено: `rc=0`, и проба (`#include <lib/shitty/composer.h>` в `lib/vterm/vt_grid.h`) даёт `rc=1` с поимённым `vt_grid.h:10` |

**Углубление расхождения — сознательное и записанное.** `T5.1` выбрала вариант А
чек-листа: апстримный скалярный `VtGeometry::borderPixels` заменён
четырёхсторонним `VtInsets insets` плюс `originX/originY` плюс `width/height`.
Цена названа в самом заголовке: `lib/embed` смотрит на публичное поле ядра, и
каждый следующий мерж будет трогать `vt_geometry.h`. Побочный выигрыш, который
чек-лист предсказал, состоялся: `mouseGeometry()` берёт `const VtGeometry&`
и перестал тянуть `composer.h` в ядро — это одно из шести закрытых пересечений
границы.

---

## Исходы сверки по документу

Из трёх исходов чек-листа (`соблюдено` / `не работает` / `документ устарел`):

- **соблюдено** — `A1`, `A2` (форма кадра), `A4`, `A5`, `A6`, `A8`, `A9`, `A10`, `A11`;
- **документ устарел** — `A3` (ожидалось планом), `A7` (**не ожидалось**),
  плюс два пункта внутри `A2`;
- **не работает** — ни одного.

---

## Обнаружено

### 1. `A7`: механизм, который документ описывает, из продукта исчез — и это нигде не записано

Арх-документ, `A7`: «полоса титлбара **резервируется постоянно** (входит в
`contentInsets` всё время, пока режим включён), а наведение переключает только
видимость». Правка `C11` это отменила: режим `autoHideChrome` резервирует **ноль**,
титлбар рисуется **поверх** верхних строк. Комментарий теста
`NothingIsReservedSoTheGridReachesTheTopEdge` объясняет, почему («пустая полоса над
первой строкой выглядела неправильно»), — но арх-документ об этом не знает.

**Измерено.** Во всём продуктовом коде `setChromeReserve()` зовётся **один раз**:
`ui_sidebar_tabs.mm:676`, сторона `Left`. Ни `Top`, ни `Right`, ни `Bottom` не
резервирует никто. Ненулевой `Top` существует только в тестах.

Следствия, ради которых это стоит записать:
- половина `A7` («резервируется постоянно») сегодня ложна, и следующий читатель
  будет искать резерв, которого нет;
- вторая половина («ховер не меняет сетку») держится **тривиально** — менять
  нечего, — а её единственный прямой наблюдатель поэтому вырожден (§1 выше);
- `A1` в продукте нагружена **одной** стороной из четырёх. Асимметрия по `Top`
  проверяется только тестами. Это не дефект, но запас прочности `A1` меньше, чем
  читается из документа.

**Рекомендация:** пометка о пересмотре `A7` в арх-документе с датой и причиной, по
тому же шаблону, что требуется для `A3`. И — премисса в тест: если резерв должен
быть нулевым, тест ховера обязан ставить ненулевой резерв **сам** (как это делают
`composer_ut` и `session_ut`), иначе он проверяет ноль.

### 2. `A3`: исход «документ устарел» состоялся, обмен теста тоже — но замена уже, чем кажется

`PaneArenaMirror` снят, `render_arena.h` пережил переписывание в `ArenaMirror`
(окно, а не панель), `render_arena_ut.cpp` сжался с 12 тестов до 5. Замена,
которую требовал чек-лист (`T4.1` оставляет тест на пер-экранный счётчик), на
месте и краснеет — `Screen::RowIdentitiesNeverRepeatAcrossScreens`.

Что записано в самом `render_arena.h` и заслуживает быть записанным и здесь:
допущение не исчезло, оно **сузилось и стало неохраняемым типами** — «**один
`SpanShaper` на окно**». Второй шейпер (оверлей со своим шрифтом, панель со
своим) делает зеркало неверным тем же тихим способом. Наблюдателя у этого
допущения **нет ни одного**: ни теста, ни гварда. Заголовок называет владельца
(«кто заведёт второй шейпер, тот владеет этим файлом»), и это всё.

Арх-документ до сих пор описывает `A3` через «диапазоны на панель» и
`PaneArenaMirror`. **Пометка о пересмотре обязательна** — она прямо названа
условием закрытия в чек-листе `T0.3`.

### 3. `A2`: обе дыры, признанные документом, изменили состояние — одна закрыта, вторая нет

- **Расхождение Metal и эталона — закрыто.** Документ фиксировал: Metal чистит
  весь drawable цветом первой панели, эталон — по прямоугольнику панели; «как
  только появится `MetalRendererImpl::captureOutput()`, паритетный тест на двух
  панелях покраснеет **по конструкции**». `captureOutput()` у Metal **появился**
  (`render_metal.mm:986`), а расхождение снято правильной стороной: Metal чистит
  весь drawable нейтральным `clearBackground`, затем **каждая панель заливает свой
  прямоугольник своим фоном**. Комментарий на месте: «до этого отступы каждой
  панели, кроме первой, носили фон первой — дефект сам по себе». Наблюдается
  `MetalPanes::EachPanesPaddingIsItsOwnBackgroundAndNotItsNeighbours` (доказано
  мутацией). **Пункт документа «правым считается эталон, приводить его к Metal
  запрещено» устарел: приводить больше нечего.**
- **Открытый вопрос 5 — закрыт наполовину.** Слепота на macOS снята: есть
  `captureOutput()` у Metal и живые паритетные наборы
  (`MultiPaneScreenChangeParity`, `QuietPaneFrameOnMetal`, `MetalPanes`). Вторая
  половина **осталась**: Vulkan по-прежнему **отказывает** больше чем на одной
  панели (`render_vk.cpp:2283`, `count != 1` → `raiseError`), значит в CI
  двухпанельный кадр не сравнивается ни с чем. Формулировка вопроса 5 требует
  правки: «две разные задачи» — одна из них сделана.
- **Мелочь того же корня:** комментарий `render_vk.cpp` ссылается на
  `PaneArenaMirror`, которого в дереве больше нет.

### 4. Места с единственным наблюдателем (запас = 1, а не 0)

Не дефекты, но их стоит знать до того, как кто-то удалит «лишний» тест:

| Свойство | Единственный наблюдатель |
|---|---|
| `A9`, сетка панели в кадре берётся у **своей** панели | `ReferenceRenderer::DrawsTwoPanesOfDifferentGrids` |
| `A9`, нулевая сетка — отказ (эталон) | `ReferenceRenderer::AFrameWithoutAGridIsRefused` (у Metal свой: `AZeroGridInOnePaneRefusesTheWholeFrame`) |
| `A8`, обратное направление «ячейка → пиксель» несёт начало панели | `ApplicationProduction::TheQuietPaneOfAFrameHandsOverItsRetainedFormAndKeepsTheAnchor` |
| `A6`, атомарность записи (`tmp` + `rename`) | `QuickFrameStore::FailedWriteLeavesThePreviousFrameIntactAndNoTempFileBehind` |
| `A3`, пер-процессная идентичность строк | `Screen::RowIdentitiesNeverRepeatAcrossScreens` |
| `A2`, каждая панель Metal носит свой фон | `MetalPanes::EachPanesPaddingIsItsOwnBackgroundAndNotItsNeighbours` |
| `A11`, сумма по списку панелей (не headless) | `SessionSet::SumsTheExtraStoreBudgetOverEveryLivePane` |

### 5. Регрессия `A4` роняет набор целиком, а не краснеет

Мутация «`split()` делит корень вкладки вместо сфокусированной панели» даёт
**падение** `unit_tests` в `stl::TreapNode::visit` — итоговой строки `OK: N` в
выводе нет вовсе, есть только одно красное имя до обрыва. Это ровно тот класс,
про который предупреждает `CLAUDE.md` («Регрессия может дать `SIGSEGV`, а не
красный тест»): автоматика, ищущая `OK:`/`ERR:`, такую регрессию пропустит.
Класс известен, но случай новый — записан здесь, чтобы список примеров не
ограничивался `uriSchemeAllowed()`.

### 6. Гварды: разрешения не расширены, одно **сужено**

Сверено с §3.3 чек-листа `T0.3`:

- `guard_scan_roots` = `("lib/shitty", "lib/vterm", "lib/embed", "ext/plt", "bin")`
  — `lib/vterm` добавлен (`T2.1`/`M3`), `lib/embed` добавлен (`T6.1`), суффиксы
  расширены на `.c` (`bin/example/main.c` — единственный `.c` в дереве, мимо
  которого скан ходил).
- `mouse_geometry_allowance` перекеен на `lib/vterm/…`, и числа **уменьшены с
  `1`/`1` до `0`/`0`**. Это строже, чем требовал чек-лист.
- `border_pixels_allowance` — пять ключей, все в `lib/shitty`, числа те же
  (`composer.h: 2`, `composer.cpp: 8`, `test_mode.cpp: 1`, два `_ut.cpp` без
  лимита). Ложные срабатывания на апстримном `geometry.borderPixels` сняты **не**
  расширением разрешений, а сменой сопоставления на `\bborderPixels\s*\(` —
  скобка отделяет наш метод от апстримного поля. Ровно наименьшая правка, которую
  предлагал `T0.3`.
- `pane_grid_names` вырос с 4 до **12** имён (`composer.geometry.*`,
  `composer.vt.*`, старые). Это был «худший из двух отказов» по чек-листу, и он
  закрыт. Записанный остаток: `const auto& g = composer.geometry;` с чтением
  `g.columns` ни одним написанием не ловится. Сегодня такого алиаса в дереве нет.
- У всех четырёх есть самозащита от осиротевшего ключа (`stale`), у
  `pane_grid_guard` — дополнительно `missing` по трём именам бэкендов. Именно
  эти самопроверки доказывают, что зелёный `rc=0` получен чтением файлов, а не
  пустого множества.

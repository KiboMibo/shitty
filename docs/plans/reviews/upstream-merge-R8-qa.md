# T7.1 — приёмка мержа апстрима: полная сборка, все тесты, сверка с эталоном `T0.1`

**Вердикт: готово с замечаниями.**

Ни один ранее зелёный тест не покраснел — **0**. Ни один ранее зелёный тест не ушёл в
`skipped` — **0**. Из прогона пропало **16 имён**, из них 4 — переименования и одно —
перенос теста в соседний класс; реально снято **12** юнит-тестов, все одной санкционированной
правкой, взамен добавлено 5 более узких. Появилось **256** имён (214 питоновских, 42 юнит).
Прирост `skipped` с 17 до 23 разобран поимённо: ни один из шести не скрывает поломки, но
двое оставляют дыру в покрытии, которую надо назвать вслух.

**Дата:** 2026-09-05 · **Задача:** `T7.1`, волна 8 плана `docs/plans/2026-08-29-upstream-merge.md`
· **Проверяющий:** `T71` · **Ветка ревью:** `review/T7.1` · **Проверяемый коммит:**
`1cd208f9f260f15e89700b62f2ef7486b1c6f223` («merge M8e: the upstream merge closes, behind reaches zero»)

Пометки: **измерено** — снято мной на этом дереве; **прочитано** — взято из исходника или
истории; **выведено** — рассуждение поверх двух первых. Числа, снятые не мной, названы чужими.

---

## 0. Как это мерялось и почему этому можно верить

Сверка идёт не с числами эталона, а с **самим эталоном, пересобранным заново**. Рядом с
рабочим деревом заведён второй worktree на коммите `0c303cd6` — том самом, на котором снят
`T0.1`, — там собраны те же цели и прогнаны те же наборы теми же командами.

**Измерено.** Эталон воспроизвёлся до единицы:

| | `T0.1` (чужое число) | Пересъёмка `0c303cd6` мной |
|---|---|---|
| `.build/unit_tests` | `OK: 955` | **`OK: 955`**, `EXIT=0` |
| питоновский набор, 20 групп | 6391 / 6 / 14 / 17 / 549 | **6391 / 6 / 14 / 17 / 549** |

Это важнее любого отдельного числа в отчёте: значит расхождения ниже — свойство мержа, а не
разницы в машине, интерпретаторе или способе запуска. Оба прогона идут **одним режимом** —
`tst/run_unittest_group.py --group-count=20`, параллелизм 10, — тем самым, что описан в разделе 3
эталона и что исполняет `build.py:1291`.

Поимённая сверка сделана двумя независимыми способами:

1. **Состав набора** — `unittest.defaultTestLoader.discover("tst")` на обоих деревьях, полный
   список `test.id()`. Даёт точный ответ на «пропало / появилось» без единого предположения.
   Контроль: на эталоне сборщик выдал ровно **6391** имя, на вершине — **6604**.
2. **Статус каждого теста** — собственный `TestResult`, записывающий `id → ok / FAIL / ERROR /
   skipped / xfail / xpass`, поверх той же выборки шардов. Даёт ответ на «покраснело / ушло в
   пропуск» по каждому имени, а не по счётчикам.

Юнит-тесты сверены по именам, которые печатает сам бинарник (`+ Suite::Test`): 955 имён на
эталоне, 982 на вершине.

**Замечание о методе.** Попытка собрать состав набора из одного каталога `tst/`, вынутого
`git archive`, даёт **6012** вместо 6391: `test_foot_parser_unicode` генерирует свои 378 случаев
из корпуса, лежащего вне `tst/`, и без остального дерева молча производит ноль тестов. Сверка,
сделанная так, «нашла бы» 379 пропавших тестов, которых нет. Сборщик состава надо запускать из
полного дерева.

---

## 1. Сводка числами

Всё в таблице — **измерено** мной в worktree `review/T7.1` на холодном `.build`.

| # | Что | Результат |
|---|---|---|
| 1 | `./build st --clear` | **231** узел, `EXIT=0`, 51,5 с |
| 2 | `./build pt unit_tests pty_test_helper st_test pt_test toml_dump example plt_unit_tests` | **326** узлов, `EXIT=0`, 52,7 с |
| 3 | `.build/unit_tests` (с `SHITTY_PTY_TEST_HELPER`, `< /dev/null`) | **`OK: 982`**, `ERR: 0`, `EXIT=0`, три прогона подряд одинаковы |
| 4 | питоновский набор, 20 групп | **`Ran 6604`**, `failures=0`, `errors=14`, `skipped=23`, `expected failures=549` |
| 5 | `tst/test_embed_example.py` | **109 / 109 `ok`**, пропусков 0 |
| 6 | `.build/ext/plt/plt_unit_tests` | **`OK: 54`**, `EXIT=0` |
| 7 | пять гвардов (`BP`, `MG`, `PG`, `DA`, `VB`) | все пять **исполнены** и зелёные |
| 8 | `production_surface` | `Ran 5`, `OK` |
| 9 | `pretty_binary_branding` | зелёный |
| 10 | `./build test -k` | **20** упавших узлов (второй прогон), 21 в первом — см. находку 1 |
| 11 | `main_fuzz`, `st_memprofile` | сломаны — **и на эталоне тоже**, см. раздел 6 |

Все одиннадцать чисел совпали с таблицей командира, кроме одного: упавших узлов `./build test`
не «~24», а **20**. Разница объяснена в разделе 5 и является следствием мержа, а не ошибкой
замера.

### Гварды: почему «зелёный» здесь что-то значит

`.build` в этом worktree создан с нуля, штампов гвардов в CAS не было — первый же запуск
исполнил все пять, что видно по их собственным тегам в выводе (**измерено**):

```
[VB] {1/5} $(B)/vterm-boundary.stamp
[PG] {2/5} $(B)/tst/pane-grid-guard.stamp
[MG] {3/5} $(B)/tst/mouse-geometry-guard.stamp
[BP] {4/5} $(B)/tst/border-pixels-guard.stamp
[DA] {5/5} $(B)/tst/darwin-call-guard.stamp
```

Сверх этого две пробы (**измерено**), обе сняты, дерево чистое:

- `lib/vterm/zz_probe_tmp.h` с `#include "lib/shitty/composer.h"` → `vterm_boundary` `EXIT=1`,
  `zz_probe_tmp.h:1: "lib/shitty/composer.h" crosses into lib/shitty`;
- `lib/shitty/render_zz_probe_tmp.cpp` с `composer.columns` → `pane_grid_guard` `EXIT=1` с
  текстом про `A9`.

**Прочитано:** `guard_scan_roots` теперь `("lib/shitty", "lib/vterm", "lib/embed", "ext/plt", "bin")`
(`build.py:1546`) — корни за переездом ядра последовали. Находка 3 эталона («три гварда ослепнут
молча») закрыта: у `border_pixels` и `mouse_geometry` есть проверка осиротевших ключей разрешения
поверх непустых словарей, у `pane_grid` — прямая проверка достижимости трёх бэкендов
(`build.py:1911`), у `darwin_call` — прежняя проверка непустого множества. Исключение — пятый
гвард, см. находку 3.

---

## 2. Вопрос 1: какие тесты были зелёными и стали красными

### **Никакие. Список пуст.**

**Измерено**, по каждому имени, а не по счётчику.

Питоновский набор. Множество красных на вершине — **14 `errors`**, и это **побайтово тот же
список**, что в разделе 5 эталона; `failures` — **ноль**. Переходов `green → red` в таблице
статусов нет ни одного:

| переход | сколько |
|---|---|
| `green → red` | **0** |
| `green → skipped` | **0** |
| `green → xfail` | **0** |
| `→ unexpected success` | **0** |
| `red → green` | 3 |
| `red → skipped` | 2 |
| `green → отсутствует` | 1 |
| `отсутствует → green` | 210 |
| `отсутствует → skipped` | 4 |

Юнит-тесты. На эталоне 955 из 955 зелёные, на вершине 982 из 982 зелёные, `EXIT=0` в обоих
случаях. Покраснеть было нечему.

Три теста, наоборот, **починились** (`red → green`) — все три чинил апстрим, каждый своим
коммитом (**прочитано**):

| Тест | Кто починил |
|---|---|
| `test_contour_input_generator.ContourInputGeneratorTest.test_legacy_arrow_modifier_matrix` | `c049a672` (апстрим): Cmd+Left/Right — резервация macOS за обход вкладок, тест теперь ждёт, что фронтенд их удержит |
| `test_ghostty_key_encoding_tail.GhosttyKeyEncodingTailTest.test_russian_shift_ctrl_c_has_no_legacy_control_sequence` | `ee9a576a` (апстрим), «Make the Darwin shards honest about platform semantics» |
| `test_italic_overhang.ItalicOverhangTest.test_sheared_tail_lands_in_the_captured_blank` | `ee9a576a` / `18fc9a20` (апстрим) |

---

## 3. Вопрос 2: какие пропали из прогона

**Пропало 16 имён.** Из них 12 юнит-тестов сняты по существу, остальные три — переименования,
плюс один питоновский тест переехал в соседний класс и потому сменил `id`, не перестав
исполняться.

### 3.1. Питоновский набор — одно имя, и оно не потеря

| Имя на эталоне | Что с ним |
|---|---|
| `test_soft_render.SoftRenderTest.test_out_of_range_values_fail_loudly` | **исполняется**, под именем `test_soft_render.SoftOptionTest.test_out_of_range_values_fail_loudly`, статус `ok` |

**Прочитано:** апстримный `c049a672` вынес проверку валидации опции `-soft` в отдельный класс
`SoftOptionTest` именно затем, чтобы классовый `skipIf(cocoa)` на `SoftRenderTest` её не гасил.
Правка увеличивает покрытие, а не уменьшает: раньше один `skip` снял бы все три теста файла,
теперь — только два рендерных.

Больше в питоновском наборе не пропало **ничего**: 6390 из 6391 эталонных `id` присутствуют, и
кратность совпадает поимённо (в том числе девятикратный дубль `FontResolverTest`, находка 2
эталона, — он на месте и по-прежнему дубль).

### 3.2. Юнит-тесты — 15 имён

**Переименованы (3), тест тот же:**

| Было | Стало | Кем |
|---|---|---|
| `MouseFrontend::KeepsThePaneOriginApartFromTheWindowInsets` | `MouseFrontend::KeepsThePaneOriginApartFromItsOwnBorder` | наш `df268812` |
| `VtermHeadless::InstallsMissingComposerDependencies` | `VtermHeadless::BuildsItsOwnEmbeddingPieces` | апстрим `bd86ed38` |
| `VtermHeadless::SecondVtermCoexistsOnOneComposer` | `VtermHeadless::SecondVtermCoexistsOnOneEmbedding` | апстрим `bd86ed38` |

**Сняты по существу (12) — весь набор `PaneArenaMirror`:**

```
PaneArenaMirror::APaneThatMovedLaterInTheListKeepsItsArena
PaneArenaMirror::APaneWithoutAScreenTakesNoSpaceFromTheNextOne
PaneArenaMirror::ClampsTheTailToAShrunkPane
PaneArenaMirror::ForgetsAPaneThatLeftTheFrame
PaneArenaMirror::LaysPanesOutBackToBackAndKeepsThem
PaneArenaMirror::NeverMatchesAPaneWithoutAScreen
PaneArenaMirror::ResetForgetsTheMirror
PaneArenaMirror::SendsAMovedPaneWhole
PaneArenaMirror::SendsAPaneWholeOnANewGeneration
PaneArenaMirror::SendsAReplacedPaneWhole
PaneArenaMirror::SendsOnePaneTailOnly
PaneArenaMirror::SwappedPanesSendBothAgain
```

**Прочитано:** снял наш коммит `2a11ffa7` («Metal mirrors the window's one arena, and the pane
ranges go»). Взамен появился `ArenaMirror` — 5 тестов на то, что от вопроса осталось: хвост, пока
держится поколение, и вся арена, когда поколение сдвинулось.

**Это санкционированная замена, а не потеря по недосмотру.** План предупреждает о ней прямо
(строка 443: «`T4.1` сносит `PaneArenaMirror` вместе с `render_arena_ut`»), и `T7.3` уже
поручено пометить арх-документ по `A3`.

**Поправка к плану, мелкая, для `T7.3` (измерено / прочитано):** число снятого план назвал
верно — двенадцать, — а вот замена не «тест на пер-экранный `rowIdentityCounter`», как там
записано, а набор `ArenaMirror` из пяти тестов в том же файле `lib/shitty/render_arena_ut.cpp`;
сам файл не снесён, а переписан.

**Итог по покрытию (выведено):** 12 тестов ушли, 5 пришли. Обмен именно обмен, и минус семь
— факт, а не иллюзия; оправдан он тем, что при одном шейпере на окно у каждой панели база равна
нулю, и почти все прежние вопросы перестают быть вопросами.

---

## 4. Вопрос 3: какие появились

**256 имён: 214 питоновских и 42 юнит-теста.** Все 214 питоновских исполняются (210 `ok`,
4 `skipped` — разобраны в разделе 5); все 42 юнит-теста зелёные.

### Питоновские, по модулям

| Модуль | Прирост | Модуль | Прирост |
|---|---|---|---|
| `test_embed_example` | **109** | `test_selection_editing` | 3 |
| `test_dec` | 15 | `test_sgr_stack` | 3 |
| `test_options` | 8 | `test_unicode` | 3 |
| `test_mouse` | 7 | `test_wide_ligature_overflow` | 3 |
| `test_preedit` | 7 | `test_contour_kitty_clipboard` | 2 |
| `test_notifications` | 5 | `test_contour_shell_integration` | 2 |
| `test_clipboard` | 4 | `test_debug_trace` | 2 |
| `test_gpu_smoke` | 4 | `test_deccara` | 2 |
| `test_hyperlink_input` | 4 | `test_dynamic_colors` | 2 |
| `test_config` | 3 | `test_font_optical` | 2 |
| `test_font_fallback` | 3 | `test_keyboard` | 2 |

плюс по 2 в `test_reference_decorations`, `test_selection`, `test_selection_autoscroll`,
`test_window_operations` и по 1 в `test_gpu_parity`, `test_osc`, `test_render_image_geometry`,
`test_resize`, `test_resize_viewport`, `test_selection_word_unicode`, `test_sgr_reports`,
`test_sixel`, `test_soft_render`, `test_terminal_modes`, `test_toml`.

Половина прироста — один файл: `test_embed_example.py`, 109 тестов на фасад встраивания
(`SHITTY_EMBED_EXAMPLE_BINARY`, `build.py:1341`). Ни один из них не пропускается: классовый
`skip` в этом файле снимается самим фактом существования артефакта `example`, и артефакт собран.

### Юнит-тесты, по наборам

| Набор | Прирост | Набор | Прирост |
|---|---|---|---|
| `VtermHeadless` | 9 | `ApplicationProduction` | 3 |
| `Pty` | 7 | `MouseFrontend` | 2 |
| `ArenaMirror` | 5 | `MetalPanes` | 2 |
| `SessionSet` | 4 | `Composer` | 2 |
| `Options` | 4 | `CellExtraStore` | 2 |
| | | `Screen`, `FontResolver` | по 1 |

Полные списки имён — в разделе 8.

---

## 5. Разбор пропусков: 17 → 23

Шесть новых пропусков. Для каждого: чей, почему, и что он скрывает.

| # | Тест | Чей | Был ли зелёным на `T0.1` | Что скрывает |
|---|---|---|---|---|
| 1 | `test_soft_render.SoftRenderTest.test_soft_zero_departs_from_the_hinted_grid` | **апстрим**, `c049a672` | нет — был **красным** (`failures` №6 эталона) | ничего: гасит унаследованный красный, причина названа верно |
| 2 | `test_soft_render.SoftRenderTest.test_darkening_scales_with_the_option` | **апстрим**, `c049a672` | нет — был **красным** (`failures` №1) | то же |
| 3 | `test_options.OptionTest.test_non_utf8_and_unknown_locales_warn` | **апстрим**, `b1bf744e` | **теста не существовало** | предупреждение о не-UTF-8 локали на macOS не проверяется никем — см. находку 2 |
| 4 | `test_font_fallback.FontFallbackTest.test_symbol_font_range_beats_primary_coverage` | **наш**, `4dda80cb` (`F8d`) | **теста не существовало** | `[[symbolFont]]` на Darwin не проверяется — см. находку 4 |
| 5 | `test_font_fallback.FontFallbackTest.test_symbol_font_defaults_to_private_use_pictograms` | **наш**, `4dda80cb` | **теста не существовало** | то же |
| 6 | `test_font_fallback.FontFallbackTest.test_symbol_font_that_misses_the_range_leaves_it_alone` | **наш**, `4dda80cb` | **теста не существовало** | то же |

Семнадцать эталонных пропусков — **все на месте**, с теми же причинами: один `test_pty` за
`/proc/self/fd`, двенадцать `FontResolverTest` (шесть тестов ×2 из-за дубля), два `strace`,
два `SystemFallbackTest`.

### 5.1. «Красное ушло в пропуск» — два случая, оба честные

Строки 1–2 — ровно тот случай, которого просили опасаться: тест был красным и стал
пропущенным. **Оба — апстримные, и оба обоснованы (прочитано, `c049a672`):**

> `-soft` steers the FreeType rasterizer; on cocoa the CoreText renderer sits first in the
> chain, so the soft rendering tests skip there and only the option validation keeps running
> everywhere.

Проверяемое свойство физически недостижимо на этой платформе: `-soft` крутит ручку FreeType,
а рендерит Core Text. Это тот же класс причины, что у соседнего `SystemFallbackTest`, который
уже пропускался на эталоне. Главное — **валидацию опции апстрим из-под пропуска вынул**
(`SoftOptionTest`, раздел 3.1), то есть покрытие сузилось ровно на недостижимое.

**Выведено:** это не увод красного. Увод красного — когда пропуск ставят на тест, который на
этой машине мог бы наблюдать проверяемое. Здесь не мог.

### 5.2. Строки 4–6 — наш пропуск, разобран отдельно в `F8d`

Три теста `[[symbolFont]]` пришли из апстрима (`7c04a229`, `85823399`), на `T0.1` их не было.
На нашем дереве два были красными, третий — зелёным. Наш коммит `4dda80cb` пропустил все три,
и `docs/reports/F8d-symbolfont-darwin-2026-09-05.md` доказывает **побайтным равенством трёх
рендеров**, что третий был зелёным впустую: кадр возвращается один и тот же независимо от того,
попадает `[[symbolFont]]` в диапазон, промахивается или отсутствует вовсе.

**Против критерия `T7.1` нарушения нет** — на эталоне этих имён не существовало, зелёными они
никогда не были. Дыра в покрытии от этого не исчезает, см. находку 4.

### 5.3. Кратность пропусков совпадает

23 = 17 эталонных + 6 новых, и ни один эталонный пропуск не превратился ни во что другое.
Обратного перехода (`skipped → green`) тоже нет.

---

## 6. `./build test` целиком

**Измерено**, два прогона с `-k`. Без `-k` граф исполняет ~2990 узлов, встаёт на первом отказе,
не печатает итоговой строки про упавшие узлы вовсе и оставляет в логе всего два отказавших
запуска — то есть на вопрос «сколько всего падает» не отвечает:

| Прогон | Упало узлов | Стенное время |
|---|---|---|
| 1 | **21** | 1 мин 07 с |
| 2 | **20** | ~1 мин |

Устойчивое число — **20**, и оно раскладывается ровно: **10 групп × 2 варианта набора**
(`python-tests/group-N` и `python-tests-prod-parser/group-N`). Красные группы — 0, 2, 3, 4, 5,
6, 7, 8, 11, 12. Все двадцать падают на тех же **14 унаследованных `errors`**, что и на
эталоне; ни одного другого имени в логе нет.

Первый прогон дал 21 из-за одного дополнительного отказа, разобранного ниже как находка 1.

**Почему 20, а не «~24».** Чужое число «~24» из `CLAUDE.md` (замер `G16`) снималось, когда в
наборе ещё жили **шесть `failures`** эталона. Апстримные `c049a672` / `ee9a576a` три из них
починили и два увели в законный пропуск, шестой переехал классом — группы, падавшие только из-за
них, стали зелёными. Уменьшение числа упавших узлов здесь — **следствие мержа**, и оно в нужную
сторону.

Остальной граф зелёный: все узлы `unit_tests`, оба набора `plt`, гварды, `production_surface`,
`pretty_binary_branding`, `vterm_boundary`.

### Цели, сломанные и на эталоне

**Измерено на обоих деревьях**, чтобы не приписать мержу чужое:

| Цель | Вершина | Эталон `0c303cd6` | Причина |
|---|---|---|---|
| `main_fuzz` | `EXIT=1` | **`EXIT=1`** | `ld: symbol(s) not found`, `_main` — нет libFuzzer |
| `st_memprofile` | `EXIT=1` | **`EXIT=1`** | `gperftools/heap-profiler.h` не найден |

Ни та, ни другая не входит в цель `test`. Обе сломаны одинаково до и после — унаследованное.

---

## 7. Обнаружено

Порядок — по убыванию цены. Ни одна находка не блокирует приёмку `T7.1`.

### Находка 1 (важная). Мигающий тест под нагрузкой: `test_pty.PtyTest.test_blocking_child_tty_does_not_block_control_reply_reads`

**Где:** `tst/test_pty.py:134`, отказ в `tst/harness.py:298`.

**Что измерено.** В первом прогоне `./build test -k` этот тест упал **один раз** — в варианте
`python-tests-prod-parser`, группа 13:

```
RuntimeError: test child tty has no path
  File "tst/test_pty.py", line 136, in test_blocking_child_tty_does_not_block_control_reply_reads
    terminal.spawn(
```

Тот же вариант той же группы, запущенный отдельно **пять раз подряд** с правильными
`st_test_prod_parser` / `pt_test_prod_parser`, — **`OK` пять раз из пяти**. Второй полный
прогон `./build test -k` этого отказа не воспроизвёл: 20 узлов вместо 21, ни одного
не-шрифтового `ERROR` в логе.

**Выведено:** мигание под конкуренцией. `./build test` держит ~600 % CPU и десятки процессов
разом; `ptsname`/выдача пути к tty ребёнка под такой нагрузкой иногда не отвечает. Тест не
переспрашивает.

**Чем грозит.** Эталон `T0.1` заявлял «мигающих не обнаружено» — но он и не гонял `./build test`,
только группы при параллелизме 10. То есть это первое мигание, наблюдённое на этом дереве, и
следующая задача, которая увидит его в CI, потратит время на поиск регрессии, которой нет.

**Что предлагается (вне рамок `T7.1`):** дать `spawn()` в `harness.py` короткий повтор на
`test child tty has no path`, либо явно закрепить это имя как известное мигающее. Чинить в
рамках приёмки не стал — правка тестов вне моих полномочий.

### Находка 2 (важная). Пропуск на macOS с формулировкой про musl

**Где:** `tst/test_options.py:338` — **прочитано**:

```python
@unittest.skipUnless(platform.libc_ver()[0] == "glibc", "musl silently maps every locale to UTF-8")
```

**Что не так.** На macOS `platform.libc_ver()` возвращает `('', '')`, поэтому условие ложно и
тест пропускается — с сообщением про musl, к Darwin отношения не имеющим. Апстримный
`b1bf744e` закрывал конкретную проблему musl, но написал условие «только glibc», а не «не musl».

**Чем грозит.** Предупреждение о не-UTF-8 и неизвестных локалях (`f321198e`) на macOS не
проверяется **ничем**, и по логу это выглядит как чужая платформенная особенность, а не как
дыра. Ровно тот случай, о котором `CLAUDE.md` пишет «место, чей единственный наблюдатель
недоступен, этим методом не проверяется вовсе».

**Что предлагается:** заменить условие на `platform.libc_ver()[0] != "musl"` либо снабдить
Darwin своей причиной пропуска. Вне рамок мержа.

### Находка 3 (важная). `vterm_boundary` потерял самозащиту, когда разрешение опустело

**Где:** `lib/vterm/check_includes.py:73` — `ALLOWANCE = {}`, и `:111` — `sorted(set(ALLOWANCE) - seen)`.

**Что измерено.** Гвард, натравленный на **пустой каталог**, отвечает `EXIT=0`:

```
python3 lib/vterm/check_includes.py /tmp/t71-empty /tmp/t71-empty/st.stamp   →   EXIT=0
```

**Что не так.** Единственная самозащита этого гварда — проверка осиротевших ключей разрешения.
Пока в `ALLOWANCE` были ключи, она заодно ловила «скан не дошёл ни до одного файла». Разрешение
опустело (все нарушения закрыты — это хорошая новость), и вместе с ним исчезла защита: теперь
переезд `lib/vterm` или опечатка в корне дадут зелёный гвард над пустым множеством. Четыре
гварда в `build.py` от этого защищены каждый по-своему, пятый — уже нет.

**Чем грозит.** Ровно находка 3 эталона `T0.1`, вернувшаяся с другой стороны: «гвард молча
ослеп» при зелёной сборке.

**Что предлагается:** добавить в `check(root)` проверку `if not sources: return ошибка` — три
строки, по образцу `pane_grid_backends` или `darwin_call`. Вне рамок мержа.

### Находка 4 (замечание). `[[symbolFont]]` не покрыт на Darwin ничем

После `4dda80cb` три теста `[[symbolFont]]` пропускаются на cocoa, а `SystemFallbackTest` — уже
пропускался. **Выведено:** на этой платформе фича не наблюдается ни одним тестом. `F8d` сам
относит это в отложенное (раздел 7 своего отчёта), и я с ним согласен: доказательство
вырожденности фикстуры там честное. Но пометить надо: покрытие `[[symbolFont]]` на Darwin —
**ноль**, и следующая правка резолвера шрифтов на macOS пройдёт мимо всех наблюдателей.

### Находка 5 (замечание). Комментарии `render_vk.cpp` ссылаются на снесённый `PaneArenaMirror`

**Прочитано:** `lib/shitty/render_vk.cpp:247` и `:2295` — оба комментария обещают, что «this has
to become `PaneArenaMirror`» и «the arenas need `PaneArenaMirror` on top of that». Типа больше
нет: `2a11ffa7` снёс его вместе с тринадцатью тестами. Читатель Vulkan-бэкенда пойдёт искать
класс, которого не существует. Одна из тех же дверей, что план поручает закрыть `T7.3` в
арх-документе по `A3`.

### Находка 6 (замечание). План неверно называет замену снесённому `PaneArenaMirror`

Строка 443 плана: «`T4.1` сносит `PaneArenaMirror` вместе с `render_arena_ut` (12 тестов)» и
«замена по решению `Р5` (тест на пер-экранный `rowIdentityCounter`)». Число верно — **измерено**,
снято ровно 12. Расходится вторая половина: замена — набор `ArenaMirror` из **5** тестов в том
же файле, а `render_arena_ut.cpp` не снесён, а переписан. Для `T7.3` это важно: он будет сверять
именно эту строку.

---

## 8. Приложение: полные списки имён

### 8.1. Пропали (16)

Питоновский набор (1, переехал классом):

```
test_soft_render.SoftRenderTest.test_out_of_range_values_fail_loudly
```

Юнит-тесты (15):

```
MouseFrontend::KeepsThePaneOriginApartFromTheWindowInsets
PaneArenaMirror::APaneThatMovedLaterInTheListKeepsItsArena
PaneArenaMirror::APaneWithoutAScreenTakesNoSpaceFromTheNextOne
PaneArenaMirror::ClampsTheTailToAShrunkPane
PaneArenaMirror::ForgetsAPaneThatLeftTheFrame
PaneArenaMirror::LaysPanesOutBackToBackAndKeepsThem
PaneArenaMirror::NeverMatchesAPaneWithoutAScreen
PaneArenaMirror::ResetForgetsTheMirror
PaneArenaMirror::SendsAMovedPaneWhole
PaneArenaMirror::SendsAPaneWholeOnANewGeneration
PaneArenaMirror::SendsAReplacedPaneWhole
PaneArenaMirror::SendsOnePaneTailOnly
PaneArenaMirror::SwappedPanesSendBothAgain
VtermHeadless::InstallsMissingComposerDependencies
VtermHeadless::SecondVtermCoexistsOnOneComposer
```

*(в разделе 3.2 таблица переименований показывает, какие три из пятнадцати живы под другими
именами; остальные двенадцать — весь набор `PaneArenaMirror` — сняты по существу.)*

### 8.2. Покраснели (0)

Список пуст.

### 8.3. Появились: юнит-тесты (42)

```
ApplicationProduction::EveryPanesRectangleCrossesToTheSurfaceByTheChromeReserveAlone
ApplicationProduction::TheAnchorOffTheHomeCellTakesItsColumnFromTheWidthAndItsRowFromTheHeight
ApplicationProduction::TheWindowShownAtStartupAsksForTheRequestedColumnsAndRowsOnTheirOwnAxes
ArenaMirror::AnEmptyArenaOwesNothing
ArenaMirror::ResetOwesTheWholeArenaAgain
ArenaMirror::SendsTheTailWhileTheGenerationHolds
ArenaMirror::SendsTheWholeArenaWhenItShrankUnderOneGeneration
ArenaMirror::SendsTheWholeArenaWhenTheGenerationMoves
CellExtraStore::ARootReachedOnlyThroughTheArgumentIsRewrittenToo
CellExtraStore::ARootReachedThroughBothDoorsIsRewrittenOnce
Composer::InstalledHostEchoesResizesIntoTheListResizeAlreadyWalks
Composer::SetOptionsPublishesOneSnapshotToTheCoreAndToTheBorder
FontResolver::SymbolSpanBeatsCoverageOrder
MetalPanes::APaneKeepsItsInkWhenALaterPaneCollectsTheArena
MetalPanes::AnOverlayLandsOnTheStripsOfItsOwnPane
MouseFrontend::KeepsThePaneOriginApartFromItsOwnBorder
MouseFrontend::TheChromeReserveIsCountedOnceOnTheWayIntoAPanesGeometry
Options::AnOptionsNobodyParsedPermitsNoUriSchemeAndAParsedOnePermitsHttp
Options::SymbolFontCodepointBasesAndMalformedShapes
Options::SymbolFontDropsHalfUnderstoodEntries
Options::SymbolFontTablesParseFromConfig
Pty::EngagedOwnerDeathReturnsTheLoan
Pty::EngagedRegistryRecyclesAcrossHandles
Pty::EngagedWriteToDeadChildDropsTheQueue
Pty::EngagedWriterParksOnBudgetAndResumes
Pty::ForegroundProcessGroupReportsTheChild
Pty::OwnerDeathReturnsTheStreamLoan
Pty::StreamWriteRidesOutBackpressure
Screen::RowIdentitiesNeverRepeatAcrossScreens
SessionSet::ActivationReplaysThePointersPresenceToTheFocusedPaneAndNotToTheNeighbour
SessionSet::ActivationReplaysTheWindowsFocusToTheFocusedPaneAndNotToEveryPaneOfTheTab
SessionSet::ReadlineChordsReachThePty
SessionSet::TheHitTestTakesTheChromeReserveOffThePixelAndLeavesThePanesOwnBorderOn
VtermHeadless::AnswersTheWindowReportsAboutTheWindowAndTheTextAreaAboutThePane
VtermHeadless::BuildsItsOwnEmbeddingPieces
VtermHeadless::CountsBothScreensOnceThePaneHasEnteredTheAlternate
VtermHeadless::ForegroundProcessFillsTheTitleFallback
VtermHeadless::PointerPresenceIsVisibleToTheChildThroughTheMotionFilter
VtermHeadless::ScrollViewMovesClampsAndReturnsTheOffset
VtermHeadless::SecondVtermCoexistsOnOneEmbedding
VtermHeadless::TheExtraStoreBudgetIgnoresInputThatEntersNoScreen
VtermHeadless::TheWindowReportsCountTheWindowsOwnReserveAndNotThePanesBorder
```

### 8.4. Появились: питоновский набор (214)

```
test_clipboard.ClipboardTest.test_auto_copy_mirrors_a_mouse_selection_into_the_clipboard
test_clipboard.ClipboardTest.test_clipboard_paste_neutralizes_c1_and_pictures_controls
test_clipboard.ClipboardTest.test_paste_converts_newlines_and_brackets
test_clipboard.ClipboardTest.test_paste_shortcut_carries_large_and_truncated_payloads
test_config.ConfigFileTest.test_a_reload_that_cannot_reopen_the_font_still_applies_geometry
test_config.ConfigFileTest.test_a_reload_that_fails_to_load_keeps_the_running_config
test_config.ConfigFileTest.test_import_accepts_an_absolute_path
test_contour_kitty_clipboard.ContourKittyClipboardTest.test_empty_metadata_records_are_skipped
test_contour_kitty_clipboard.ContourKittyClipboardTest.test_malformed_metadata_is_refused_per_operation
test_contour_shell_integration.ContourShellIntegrationTest.test_click_modes_cover_the_vertical_variants
test_contour_shell_integration.ContourShellIntegrationTest.test_fresh_line_moves_off_a_started_line_only
test_debug_trace.DebugTraceTest.test_trace_records_resizes_with_content_evidence
test_debug_trace.DebugTraceTest.test_without_the_option_no_file_appears
test_dec.DecProtocolTest.test_alternate_screen_tracks_the_saved_cursor_and_scrolls_down
test_dec.DecProtocolTest.test_ansi_modes_are_inspectable
test_dec.DecProtocolTest.test_bulk_print_in_margins_falls_back_around_special_rows
test_dec.DecProtocolTest.test_bulk_print_shorter_than_the_region_scrolls_only_its_lines
test_dec.DecProtocolTest.test_checksum_request_with_an_inverted_rectangle_is_silent
test_dec.DecProtocolTest.test_clearing_the_alternate_screen_from_the_primary
test_dec.DecProtocolTest.test_deleting_through_a_wrap_point_restores_the_flag_at_the_edge
test_dec.DecProtocolTest.test_double_width_line_clamps_a_cursor_past_the_half_width
test_dec.DecProtocolTest.test_margin_scrolls_over_special_rows
test_dec.DecProtocolTest.test_reflow_keeps_a_double_height_row_whole
test_dec.DecProtocolTest.test_repeat_carries_blink_and_double_height_stops_a_bulk_run
test_dec.DecProtocolTest.test_row_moves_carry_double_height_and_wide_rows
test_dec.DecProtocolTest.test_tab_stop_restore_skips_columns_beyond_the_grid
test_dec.DecProtocolTest.test_whole_row_erases_follow_attributes_and_protection
test_dec.DecProtocolTest.test_window_frame_colors_are_accepted_and_ignored
test_deccara.ChangeRectangleAttributesTest.test_every_attribute_and_default_color_can_be_changed
test_deccara.ChangeRectangleAttributesTest.test_underline_color_reaches_cells_carrying_a_grapheme
test_dynamic_colors.DynamicColorTest.test_cie_and_tek_specs_cover_their_degenerate_branches
test_dynamic_colors.DynamicColorTest.test_special_color_edge_indices_and_repeats_are_harmless
test_embed_example.CellColorSourceTest.test_a_256_color_keeps_its_index_too
test_embed_example.CellColorSourceTest.test_a_background_carries_its_own_source
test_embed_example.CellColorSourceTest.test_a_redefined_palette_entry_is_still_that_entry
test_embed_example.CellColorSourceTest.test_a_special_color_overriding_an_index_is_direct
test_embed_example.CellColorSourceTest.test_a_special_color_standing_in_for_the_default_is_direct
test_embed_example.CellColorSourceTest.test_a_true_color_request_is_direct
test_embed_example.CellColorSourceTest.test_an_ansi_color_keeps_its_palette_index
test_embed_example.CellColorSourceTest.test_an_unset_underline_color_follows_the_foreground
test_embed_example.CellColorSourceTest.test_an_unstyled_cell_names_the_defaults
test_embed_example.CellColorSourceTest.test_the_inverse_special_color_makes_the_background_direct
test_embed_example.CellColorSourceTest.test_the_underline_color_is_reported_separately
test_embed_example.DriverEdgeTest.test_a_middle_click_pastes_the_primary_selection
test_embed_example.DriverEdgeTest.test_command_line_failures_and_short_forms
test_embed_example.DriverEdgeTest.test_growing_the_session_past_the_snapshot_grid
test_embed_example.DriverEdgeTest.test_link_schemes_are_folded_and_filtered
test_embed_example.DriverEdgeTest.test_odd_hex_in_a_feed_stops_at_the_last_full_byte
test_embed_example.DriverEdgeTest.test_out_of_range_keys_and_buttons_are_refused
test_embed_example.DriverEdgeTest.test_reapplying_the_same_history_cap_changes_nothing
test_embed_example.EmbedExampleTest.test_a_control_click_opens_the_detected_link
test_embed_example.EmbedExampleTest.test_alt_screen_swaps_and_reports_its_mode
test_embed_example.EmbedExampleTest.test_alternate_scroll_is_reported_and_cleared
test_embed_example.EmbedExampleTest.test_carriage_return_and_backspace_overwrite
test_embed_example.EmbedExampleTest.test_combining_grapheme_stays_one_cell
test_embed_example.EmbedExampleTest.test_construction_publishes_nothing
test_embed_example.EmbedExampleTest.test_cursor_addressing_and_clamped_movement
test_embed_example.EmbedExampleTest.test_cursor_position_report_reflects_the_cursor
test_embed_example.EmbedExampleTest.test_cursor_style_changes
test_embed_example.EmbedExampleTest.test_custom_tab_stop
test_embed_example.EmbedExampleTest.test_decaln_fills_the_screen
test_embed_example.EmbedExampleTest.test_device_attributes_land_in_the_reply_buffer
test_embed_example.EmbedExampleTest.test_erase_character_leaves_the_cursor
test_embed_example.EmbedExampleTest.test_erase_display_below_and_above
test_embed_example.EmbedExampleTest.test_erase_line_variants
test_embed_example.EmbedExampleTest.test_grid_matches_the_full_terminal_attributes_and_charsets
test_embed_example.EmbedExampleTest.test_grid_matches_the_full_terminal_cursor_motion
test_embed_example.EmbedExampleTest.test_grid_matches_the_full_terminal_editing
test_embed_example.EmbedExampleTest.test_grid_matches_the_full_terminal_on_recorded_corpus
test_embed_example.EmbedExampleTest.test_grid_matches_the_full_terminal_scroll_and_screens
test_embed_example.EmbedExampleTest.test_hard_reset_restores_the_defaults
test_embed_example.EmbedExampleTest.test_history_cap_change_survives_an_alternate_screen_visit
test_embed_example.EmbedExampleTest.test_index_scrolls_at_the_bottom
test_embed_example.EmbedExampleTest.test_insert_and_delete_characters
test_embed_example.EmbedExampleTest.test_insert_and_delete_lines
test_embed_example.EmbedExampleTest.test_keypad_origin_and_reverse_modes
test_embed_example.EmbedExampleTest.test_line_drawing_charset
test_embed_example.EmbedExampleTest.test_modes_reflect_private_mode_changes
test_embed_example.EmbedExampleTest.test_origin_mode_addresses_inside_the_margins
test_embed_example.EmbedExampleTest.test_osc52_targets_pick_their_selection
test_embed_example.EmbedExampleTest.test_paste_carries_newlines_and_large_payloads
test_embed_example.EmbedExampleTest.test_plain_text_lands_on_the_grid
test_embed_example.EmbedExampleTest.test_repeat_repeats_the_last_graphic
test_embed_example.EmbedExampleTest.test_replies_drain_incrementally
test_embed_example.EmbedExampleTest.test_resize_moves_the_session_to_the_new_geometry
test_embed_example.EmbedExampleTest.test_resize_rejects_a_zero_dimension
test_embed_example.EmbedExampleTest.test_reverse_index_scrolls_at_the_top
test_embed_example.EmbedExampleTest.test_save_and_restore_cursor
test_embed_example.EmbedExampleTest.test_scroll_region_confines_the_scroll
test_embed_example.EmbedExampleTest.test_scrollback_keeps_the_tail_visible
test_embed_example.EmbedExampleTest.test_shift_out_uses_g1
test_embed_example.EmbedExampleTest.test_space_toggles_rectangular_mid_drag
test_embed_example.EmbedExampleTest.test_title_and_bell_reach_the_callbacks
test_embed_example.EmbedExampleTest.test_wide_grapheme_occupies_two_columns
test_embed_example.EmbedExampleTest.test_wide_grapheme_wraps_whole_at_line_end
test_embed_example.EmbedExampleTest.test_wrap_and_wrap_disabled
test_embed_example.HistoryBudgetTest.test_lowering_the_cap_drops_the_oldest_rows_at_once
test_embed_example.HistoryBudgetTest.test_lowering_the_cap_releases_the_rows_it_dropped
test_embed_example.HistoryBudgetTest.test_memory_grows_with_the_history_it_backs
test_embed_example.HistoryBudgetTest.test_raising_the_cap_does_not_resurrect_dropped_rows
test_embed_example.HistoryBudgetTest.test_the_visible_grid_survives_a_cap_change
test_embed_example.HistoryRowTest.test_a_terminal_without_history_addresses_only_the_grid
test_embed_example.HistoryRowTest.test_every_retained_row_is_addressable_oldest_first
test_embed_example.HistoryRowTest.test_reading_past_the_last_row_yields_nothing
test_embed_example.HistoryRowTest.test_row_reads_ignore_the_view_position
test_embed_example.InputEncodingTest.test_arrow_key_follows_the_cursor_mode
test_embed_example.InputEncodingTest.test_control_chord_encodes_through_the_key_event
test_embed_example.InputEncodingTest.test_focus_reports_when_asked
test_embed_example.InputEncodingTest.test_kitty_flags_change_the_escape_key
test_embed_example.InputEncodingTest.test_kitty_reports_the_release
test_embed_example.InputEncodingTest.test_motion_reports_under_any_event_tracking
test_embed_example.InputEncodingTest.test_paste_honors_the_bracketed_mode
test_embed_example.InputEncodingTest.test_selection_drag_reaches_the_clipboard_callback
test_embed_example.InputEncodingTest.test_sgr_mouse_reports_press_and_release
test_embed_example.InputEncodingTest.test_text_sends_utf8
test_embed_example.InputEncodingTest.test_wheel_reports_when_captured
test_embed_example.InputEncodingTest.test_wheel_scrolls_the_view_otherwise
test_embed_example.PreeditTest.test_a_combining_mark_shares_the_cell_it_extends
test_embed_example.PreeditTest.test_a_cursor_range_past_the_text_is_clamped
test_embed_example.PreeditTest.test_a_double_width_row_shows_no_preview
test_embed_example.PreeditTest.test_a_joined_emoji_is_one_preview_cluster
test_embed_example.PreeditTest.test_a_preview_too_long_for_the_row_keeps_the_fresh_end
test_embed_example.PreeditTest.test_a_wide_character_takes_two_columns_of_the_preview
test_embed_example.PreeditTest.test_an_empty_preview_clears_the_composition
test_embed_example.PreeditTest.test_an_invalid_byte_aborts_the_preview
test_embed_example.PreeditTest.test_clipping_never_starts_on_a_wide_continuation
test_embed_example.PreeditTest.test_preview_bytes_are_decoded_strictly
test_embed_example.PreeditTest.test_the_cursor_hides_and_anchors_the_candidate_window
test_embed_example.PreeditTest.test_the_preview_is_drawn_from_the_cursor
test_embed_example.PreeditTest.test_the_preview_stays_out_of_the_grid_and_the_child
test_embed_example.ScrollbackTest.test_a_terminal_keeping_no_lines_retains_no_history
test_embed_example.ScrollbackTest.test_alternate_screen_has_no_history_to_scroll
test_embed_example.ScrollbackTest.test_history_holds_what_scrolled_off_the_grid
test_embed_example.ScrollbackTest.test_history_is_capped_by_save_lines
test_embed_example.ScrollbackTest.test_scrolling_back_down_returns_to_the_live_bottom
test_embed_example.ScrollbackTest.test_scrolling_clamps_to_the_retained_history
test_embed_example.ScrollbackTest.test_scrolling_moves_the_view_over_the_history
test_embed_example.ScrollbackTest.test_scrolling_to_an_absolute_offset_lands_there
test_embed_example.ScrollbackTest.test_scrolling_to_past_the_history_clamps
test_embed_example.ScrollbackTest.test_scrolling_to_the_current_offset_changes_nothing
test_embed_example.ScrollbackTest.test_scrolling_to_zero_returns_to_the_live_bottom
test_font_fallback.FontFallbackTest.test_symbol_font_defaults_to_private_use_pictograms
test_font_fallback.FontFallbackTest.test_symbol_font_range_beats_primary_coverage
test_font_fallback.FontFallbackTest.test_symbol_font_that_misses_the_range_leaves_it_alone
test_font_optical.OpticalFontTest.test_latin_and_cyrillic_runs_are_optically_reflowed
test_font_optical.OpticalFontTest.test_unsupported_codepoint_passes_the_whole_run_through
test_gpu_parity.ArenaCollectionParityTest.test_a_collection_between_panes_is_the_same_picture
test_gpu_smoke.GpuSmokeTest.test_composition_preview_overlays_the_grid
test_gpu_smoke.GpuSmokeTest.test_selection_drag_and_link_hover_repaint_their_rows
test_gpu_smoke.VulkanBlitSmokeTest.test_composition_preview_overlays_the_grid
test_gpu_smoke.VulkanBlitSmokeTest.test_selection_drag_and_link_hover_repaint_their_rows
test_hyperlink_input.HyperlinkInputTest.test_an_overlong_scheme_is_never_allowed
test_hyperlink_input.HyperlinkInputTest.test_detected_link_corners
test_hyperlink_input.HyperlinkInputTest.test_detection_gives_up_past_the_scan_limit
test_hyperlink_input.HyperlinkInputTest.test_super_arms_the_hover_and_the_click_like_control
test_keyboard.KeyboardTest.test_a_dropped_c1_lead_split_across_chunks_is_resolved_later
test_keyboard.KeyboardTest.test_dropped_path_lists_stream_in_chunks_and_cap_a_line
test_mouse.MouseProtocolTest.test_dec_locator_button_reports_can_be_switched_off
test_mouse.MouseProtocolTest.test_dec_locator_filter_rectangle_reports_on_exit
test_mouse.MouseProtocolTest.test_dec_locator_filter_survives_in_continuous_mode
test_mouse.MouseProtocolTest.test_drags_with_the_other_buttons_report_their_motion_codes
test_mouse.MouseProtocolTest.test_highlight_tracking_with_a_zero_start_is_disarmed
test_mouse.MouseProtocolTest.test_non_finite_wheel_deltas_are_ignored
test_mouse.MouseProtocolTest.test_one_shot_locator_disarms_after_a_button_report
test_notifications.NotificationProtocolTest.test_broken_base64_ending_a_quartet_drops_the_notification
test_notifications.NotificationProtocolTest.test_encoded_chunks_past_the_budget_drop_the_notification
test_notifications.NotificationProtocolTest.test_invalid_utf8_drops_the_notification
test_notifications.NotificationProtocolTest.test_multibyte_titles_survive_the_utf8_check
test_notifications.NotificationProtocolTest.test_wholly_empty_notification_is_dropped
test_options.OptionTest.test_an_option_without_its_value_fails
test_options.OptionTest.test_dump_records_the_raw_pty_stream
test_options.OptionTest.test_fullscreen_and_maximized_are_independent_options
test_options.OptionTest.test_fullscreen_is_a_boolean_startup_option
test_options.OptionTest.test_non_utf8_and_unknown_locales_warn
test_options.OptionTest.test_symbol_font_ranges_take_hex_bounds_and_refuse_lists
test_options.OptionTest.test_title_fallback_none_keeps_the_brand_title
test_options.OptionTest.test_verbose_startup_reports_sessions_and_resizes
test_osc.OscProtocolTest.test_a_payload_past_the_megabyte_cap_is_dropped
test_preedit.PreeditTest.test_a_combining_mark_extends_the_preview_cell
test_preedit.PreeditTest.test_a_joined_emoji_keeps_one_preview_cluster
test_preedit.PreeditTest.test_a_lone_joiner_leaves_no_preview
test_preedit.PreeditTest.test_a_preview_cluster_outlives_a_grapheme_collection
test_preedit.PreeditTest.test_a_variation_selector_widens_the_preview_cluster
test_preedit.PreeditTest.test_the_cursor_anchor_counts_cluster_cells
test_preedit.PreeditTest.test_the_preview_clusters_like_printed_text
test_reference_decorations.ReferenceDecorationTest.test_double_underline_overline_and_strike_add_ink
test_reference_decorations.ReferenceDecorationTest.test_wrap_marks_and_the_hollow_cursor_are_drawn
test_render_image_geometry.RenderImageGeometryTest.test_the_image_takes_its_width_from_the_columns_and_its_height_from_the_rows
test_resize.ResizeTest.test_a_resize_ends_synchronized_output
test_resize_viewport.ResizeViewportTest.test_a_scrolled_view_survives_growing_and_shrinking
test_selection.SelectionTest.test_shift_click_extends_a_rectangular_selection_from_its_far_corner
test_selection.SelectionTest.test_space_toggles_rectangular_while_the_button_is_held
test_selection_autoscroll.SelectionAutoscrollTest.test_autoscroll_stops_when_there_is_no_history_to_reveal
test_selection_autoscroll.SelectionAutoscrollTest.test_returning_inside_stops_the_armed_autoscroll
test_selection_editing.SelectionEditingTest.test_alternate_screen_resize_moves_or_drops_the_selection
test_selection_editing.SelectionEditingTest.test_edits_touching_a_held_selection_drop_it
test_selection_editing.SelectionEditingTest.test_reflow_carries_the_selection_through_history
test_selection_word_unicode.SelectionWordUnicodeTest.test_double_click_past_an_early_wrap_snaps_to_the_padding
test_sgr_reports.SgrStatusReportTest.test_out_of_range_palette_indices_change_nothing
test_sgr_stack.SgrStackTest.test_selective_foreground_pop_restores_a_true_color
test_sgr_stack.SgrStackTest.test_selective_pop_covers_every_attribute_bit
test_sgr_stack.SgrStackTest.test_selective_underline_pop_handles_curly_double_and_mixed
test_sixel.SixelTest.test_bands_past_the_declared_height_are_dropped
test_soft_render.SoftOptionTest.test_out_of_range_values_fail_loudly
test_terminal_modes.TerminalModeTest.test_alternate_scroll_mode_is_reported
test_toml.TomlComplianceTest.test_unreadable_files_and_the_stdin_mode
test_unicode.UnicodeTest.test_a_truncated_sequence_before_a_graphic_set_byte_is_replaced
test_unicode.UnicodeTest.test_gr_stays_utf8_while_gl_is_designated_away
test_unicode.UnicodeTest.test_greek_national_replacement_set
test_wide_ligature_overflow.WideLigatureOverflowTest.test_a_differently_painted_blank_bounds_the_capture
test_wide_ligature_overflow.WideLigatureOverflowTest.test_a_neighbor_bounds_the_capture
test_wide_ligature_overflow.WideLigatureOverflowTest.test_bismillah_ink_reaches_past_the_first_blank
test_window_operations.WindowOperationsTest.test_fullscreen_startup_takes_the_screen_and_wins_over_maximized
test_window_operations.WindowOperationsTest.test_popping_a_partial_entry_borrows_the_missing_half_below
```

---

## 9. Допущения

1. **Сверка велась в отдельном worktree `review/T7.1` на коммите `1cd208f9`**, а не в главном
   дереве. Основание: холодный `.build` — единственный способ доказать, что гварды исполнились,
   а не подставили штамп из CAS.
2. **Эталон не взят из отчёта, а пересобран** во втором worktree на `0c303cd6`. Числа сошлись
   до единицы, поэтому все расхождения ниже отнесены к мержу.
3. **Режим прогона питоновского набора — 20 групп через `tst/run_unittest_group.py`**, тот же на
   обоих деревьях. Числа «одним процессом», которыми пользовался командир, здесь не
   используются: `CLAUDE.md` прямо говорит, что режимы несравнимы.
4. **`SHITTY_EMBED_EXAMPLE_BINARY` добавлен в окружение** — его не было в рецепте `T0.1`, потому
   что цели `example` тогда не существовало. Значение прочитано из `build.py:1341`.
5. **Статусы тестов сняты собственным `TestResult`**, а не разбором текстового лога: под
   параллельным запуском вывод подпроцессов рвёт строки `unittest` посередине, и построчный
   разбор терял ~370 из 6604 записей. Контроль корректности — совпадение итогов
   (6391 / 6604 и 14 `errors`) с обычным прогоном.
6. **Пробы гвардов поставлены двум из пяти** (`vterm_boundary`, `pane_grid_guard`). Остальные три
   доказаны исполнением на холодном кеше и наличием собственной самозащиты; полная проба всех
   пяти сделана на эталоне (`T0.1`, раздел 8) и повторять её в рамках приёмки не требовалось.
7. **`./build test` прогнан дважды.** Первый прогон дал 21 отказ, второй — 20; разница разобрана
   как находка 1. Третьего прогона не делал: два уже показали, что число нестабильно сверху, а
   устойчивая часть разложена поимённо.
8. **Ручная приёмка на macOS (`T7.2`) и ревью инвариантов (`T7.3`) не делались** — это соседние
   задачи волны 8.
9. **Продуктовый код и тесты не правились.** Обе пробы гвардов сняты, `git status` чист.

---

## 10. Вердикт

**Готово с замечаниями.**

Критерий `T7.1` из плана — «ни один ранее зелёный тест не покраснел, ни один не пропал из
прогона» — выполнен по первой половине буквально (**0 покрасневших**) и по второй с одной
оговоркой, которую план сам предвидел: 12 юнит-тестов `PaneArenaMirror` сняты санкционированной
правкой `2a11ffa7` и заменены пятью более узкими. Всё прочее «пропавшее» — переименования и один
перенос в соседний класс; ни один тест не перестал исполняться незаметно.

Шесть находок, ни одна не блокирующая. Две из них (`1` — мигание под нагрузкой, `3` — ослепший
`vterm_boundary` на пустом скане) стоит завести задачами: обе про наблюдаемость, то есть про
класс дефектов, который этот репозиторий ловил дороже всего.

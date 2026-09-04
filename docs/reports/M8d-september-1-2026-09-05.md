# M8d — 1 сентября апстрима, и единственный настоящий конфликт мержа

Ветка `wave/upstream-merge-w8d`, база `945f60f0`, мерж-коммит `4a49c8c9`.
Четырнадцать коммитов `ea1de5ea..a1b8cb29` включительно (`git rev-list --count`
даёт 13 для полуоткрытого интервала; `ea1de5ea` в `master` не был, вошёл этим
мержем — итого 14).

Всё, что ниже помечено **измерено**, снято командой на этом дереве; **прочитано**
— взято из исходника или истории без запуска; **выведено** — рассуждение поверх
двух первых.

## Таблица критериев

| # | Критерий | Результат |
|---|---|---|
| 1 | `./build st --clear` | **231** узел, `EXIT=0` — ровно эталон, расхождения нет |
| 2 | `unit_tests` | **`OK: 982`**, `EXIT=0`, красных **0**; +13 имён, потеряно **0** |
| 3 | `vt_headless` вне `lib/vterm` | файлов нет; два оставшихся вхождения — комментарии, и они же были на `master` |
| 4 | `foregroundProcessGroup()` у всех `PtyHandle` | **8 из 8**, поимённо ниже |
| 5 | питоновский набор | `Ran 6485`, `errors=14` — те же 14 имён; **+2 FAIL**, оба апстримные и средовые |
| 6 | пять гвардов | зелёные прямым запуском; пять проб, каждая краснит свой и только свой |
| 7 | `example` / `plt_unit_tests` | `test_embed_example` **77/77**, пропусков 0; `plt_unit_tests` **`OK: 54`** |
| 8 | обе правки командира в `ci.yml` | на месте, обе |
| 9 | ссылки на `test_mode.cpp` из продуктового кода `pt` | **0** |
| 10 | опция в обоих конфигах | `titleFallback` в `shitty.toml` **и** `pretty.toml` |
| 11 | принесённые файлы против локальных узлов | 31 файл из 35 доходит; не доходит **877 строк из 2484 — 35,3 %** |
| 12 | сломанные цели | те же две: `main_fuzz`, `st_memprofile` |

## 1. Сборка

**Измерено.** `./build st --clear` — 231 узел, `EXIT=0`. Эталон командира — 231.

Почему мерж не добавил `st` ни одного узла (**выведено**): весь новый продуктовый
код лёг в уже существующие единицы трансляции (`options.cpp`, `font_pack.cpp`,
`pty.cpp`, `application.cpp`, `vterm.cpp`). Три новых файла узлы дают, но не в
цели `st`: `dev/merge_lcov.py` вызывается только из `ci.yml` и узла не имеет
вовсе, `tst/wayland_title_fallback.py` регистрируется в тестовой группе под
`if linux:` (`build.py:4645`), а `tst/test_font_fallback.py` уже существовал.

`./build pt unit_tests pty_test_helper st_test pt_test toml_dump example
plt_unit_tests` — 180 узлов, `EXIT=0`.

**Про `./build so`.** Группы `so` и `a` объявлены внутри `if linux:`
(`build.py:1131`, группы на строках 1153 и 1183), поэтому на этой машине
`./build so` отвечает `unknown target or group: so`. Проверить критерий буквально
здесь нельзя, и это **прочитано**, а не измерено. Местный эквивалент, который
измерен: `libshitty_vt_core.a` собирается (узел 179/180), `example` линкуется
поверх фасада и даёт 77/77.

## 2. `unit_tests`

**Измерено.** `SHITTY_PTY_TEST_HELPER=$PWD/.build/pty_test_helper
./.build/unit_tests --threads=1 < /dev/null` → **`OK: 982`**, `EXIT=0`,
красных строк ноль. Эталон — `OK: 969`.

Поимённо: **добавлено 13, потеряно 0.** Список снят одинаковым способом
(разбор `STD_TEST_SUITE`/`STD_TEST` по всем `*_ut.cpp` кроме `ext/plt`) для
`master` и для дерева мержа; разность ровно 13 и совпадает с 982 − 969.

```
FontResolver::SymbolSpanBeatsCoverageOrder
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
SessionSet::ReadlineChordsReachThePty
VtermHeadless::ForegroundProcessFillsTheTitleFallback
```

Первая же попытка снять эту разность — сверка «имена из исходников `master`»
против «имена из вывода прогона» — дала мусор: 733 против 964 и полсотни
фальшивых «потерь». Причина в том, что списки строились разными способами.
Сравнимо только одинаковое; числа между способами несравнимы ровно так же, как
CLAUDE.md пишет про режимы прогона питоновского набора.

## 3. `vt_headless` и граница включений

**Измерено**, дословно:

```
$ grep -rn "vt_headless" lib/vterm/
lib/vterm/vterm.cpp:6645:// lib/shitty/vt_headless_ut.cpp, which is the one fixture where the
lib/vterm/check_includes.py:41:# grid_geometry.h with them, and vt_headless.cpp - an embedder's adapter that

$ grep -n "foregroundProcessGroup" lib/shitty/vt_headless.cpp
50:        pid_t foregroundProcessGroup() override;
165:pid_t OutputPtyHandle::foregroundProcessGroup() {
```

Критерий требовал пустого вывода. Вывод не пуст — **и не был пуст на `master`**:
обе строки суть комментарии, ссылающиеся на переехавший файл, и обе присутствуют
в `945f60f0` без изменений. Файла `lib/vterm/vt_headless.{h,cpp}` в дереве нет;
`git rm` удаления сохранил, embed-глоб второго `VtermHeadless::create` не
получает. Формулировку критерия стоит поправить на «нет файла», иначе он
недостижим на любом дереве после задачи А.

## 4. Все реализации `PtyHandle`

`PtyHandle::foregroundProcessGroup()` пришёл **чисто виртуальным** (`= 0`,
`lib/vterm/pty.h`), поэтому его обязаны получить все реализации, а не только та,
что назвал бриф. **Измерено:** их **восемь**.

| # | Файл, строка | Тип | Кто дописал |
|---|---|---|---|
| 1 | `lib/shitty/pty.cpp:256` | `PtyHandleImpl` | апстрим (`tcgetpgrp` на мастере) |
| 2 | `lib/embed/shitty_vt.cpp:143` | `ReplyPty` | апстрим |
| 3 | `lib/shitty/test_mode.cpp:376` | `TestPty` | апстрим |
| 4 | `lib/shitty/session_ut.cpp:60` | `StubHandle` | апстрим |
| 5 | `lib/shitty/pty_ut.cpp:85` | `SurvivorHandle` | апстрим |
| 6 | `lib/shitty/vt_headless_ut.cpp:116` | `SecondPtyStub` | апстрим (возвращает `group`) |
| 7 | **`lib/shitty/vt_headless.cpp:41`** | `OutputPtyHandle` | **перенесено вручную** — файл переехал задачей А |
| 8 | **`lib/shitty/pty_ut.cpp:146`** | `TeeHandle` | **дописано** — нашего происхождения, апстрим о нём не знает |

`TeeHandle` — прозрачный tee поверх настоящего хендла, поэтому он не отвечает
нулём, а переспрашивает `inner.foregroundProcessGroup()`.

Бриф назвал одну недостающую реализацию из двух. Вторую нашёл не grep, а
компилятор: `field type 'TeeHandle' is an abstract class`. Это и есть довод в
пользу чисто виртуального объявления — молча пропустить реализацию нельзя.

## 5. Питоновский набор

**Измерено**, одним процессом, командой командира:

```
Ran 6485 tests in 119.445s
FAILED (failures=2, errors=14, skipped=19, expected failures=549)
```

Эталон: `Ran 6476`, `errors=14`, `skipped=19`, `expected failures=549`.

**Четырнадцать ERROR совпадают с `reds-master.txt` поимённо, один в один.**
Ни починено, ни пропущено, ни переписано ожидание — унаследованные красные не
тронуты.

`+9` тестов — новые апстримные в `test_embed_example.py` (+6) и
`test_font_fallback.py` (+3, из них два красны, см. ниже).

### Два новых FAIL — находка, не регрессия мержа

```
FAIL: test_font_fallback.FontFallbackTest.test_symbol_font_range_beats_primary_coverage
      AssertionError: True is not false                 (tst/test_font_fallback.py:101)
FAIL: test_font_fallback.FontFallbackTest.test_symbol_font_defaults_to_private_use_pictograms
      AssertionError: 26476 not greater than 26476      (tst/test_font_fallback.py:141)
```

Оба пришли коммитом `7c04a229` вместе с `[[symbolFont]]`.

**Доказательство, что это не мерж и не сама фича** (измерено):

1. Первое из падающих утверждений — `assertFalse(has_ink(plain))` — про прогон
   **без** всякого `symbolFont`. Оно проверяет, что фикстурный шрифт
   `Shitty Coverage Fixture` рисует «M» пустым контуром. Оно красное, значит
   фикстурный шрифт не подхватился **до того**, как `symbolFont` вообще вступает
   в дело. Второе — `ink_weight(assigned) == ink_weight(plain)` ровно, символ в
   символ: конфиг не изменил ничего, потому что ни один фикстурный шрифт не
   зарезолвился.
2. Прогон тех же двух тестов с `SHITTY_TEST_FONTCONFIG=1` даёт те же два FAIL.
   Флаг ни при чём.
3. Причина названа в самом файле, десятью строками ниже:
   `@unittest.skipIf(TEST_PLATFORM == "cocoa", "Core Text precedes the Fontconfig
   fixture on Darwin")` — это уже стоит на `SystemFallbackTest`
   (`tst/test_font_fallback.py:251`). Оба новых теста опираются на
   `FontResolverTest.write_fontconfig` и `FONTCONFIG_FILE`, которых резолвер
   Core Text не читает, и никакого гварда не несут.
4. Соседний `test_symbol_font_that_misses_the_range_leaves_it_alone` зелёный —
   но только потому, что утверждает **отсутствие** эффекта. На дереве, где ничего
   не резолвится, он зелёный по неправильной причине; это вырожденная фикстура из
   таблицы CLAUDE.md, восьмой случай той же породы.
5. Сама фича при этом покрыта и зелена в `unit_tests`:
   `FontResolver::SymbolSpanBeatsCoverageOrder` и три `Options::SymbolFont*`
   прошли.

**Я их не трогал.** Однострочная правка, которая их уведёт, — это `skipIf` по
той же формуле, что уже стоит в файле; но это **не починка**, а увод красного, и
на дарвиновских шардах CI, которые командир включил на push, она снимет
единственное покрытие `[[symbolFont]]` на Darwin. Решение — территория командира,
поэтому оставил красными и назвал явно. Итог: **ветка несёт на два красных
больше эталона, оба средовые, оба апстримные.**

## 6. Гварды

Программы четырёх сканирующих гвардов извлечены из `build.py` разбором AST и
запущены **напрямую**, минуя `./build` и его CAS-штампы; `vterm_boundary` —
запуском `lib/vterm/check_includes.py`.

```
border_pixels    rc=0
mouse_geometry   rc=0
pane_grid        rc=0
darwin_call      rc=0
vterm_boundary   rc=0
```

`ALLOWANCE = {}` в `check_includes.py` — не расширялось.

**Пробы.** Каждая ставилась в файл, который соответствующий гвард действительно
читает, после чего запускались все пять и дерево восстанавливалось.

| Проба | Файл | Код | Покраснело |
|---|---|---|---|
| `border_pixels` | `lib/shitty/application.cpp` | `composer.borderPixels()` | только `border_pixels` |
| `mouse_geometry` | `lib/shitty/application.cpp` | `mouseGeometry(composer)` | только `mouse_geometry` |
| `pane_grid` | `lib/shitty/render_reference.cpp` | `composer.geometry.columns` | только `pane_grid` |
| `darwin_call` | `lib/shitty/application.cpp` | `createMetalRenderer(composer)` | только `darwin_call` |
| `vterm_boundary` | `lib/vterm/vterm.cpp` | `#include <lib/shitty/composer.h>` | только `vterm_boundary` |

`pane_grid_guard` читает только `render*`, поэтому его проба стоит в
`render_reference.cpp`, а не там, где две другие. `mouse_geometry_guard`
пропускает `*_ut.cpp` — проба в продуктовом `application.cpp`. Отслеживаемое
множество `darwin_call_guard` снято с самого гварда, а не угадано:
`applyQuickFrameToWindow createCsdTabsUi createMetalRenderer createQuickHotkey
createSidebarTabsUi`.

После всех проб пять гвардов снова зелёные, следов в дереве нет.

## 7. `example` и `plt_unit_tests`

**Измерено.** `test_embed_example` — `Ran 77 tests … OK`, пропусков **ноль**.
Эталон был 71/71; +6 принёс `f5d8241b` (composition preview в фасаде).

`plt_unit_tests` — **`OK: 54`**, `EXIT=0`, совпадает с эталоном.

Мелочь на будущее: голый `./.build/ext/plt/plt_unit_tests --threads=1
< /dev/null` **висит** и был снят по таймауту в пять минут. Через штатную
обёртку `python3 ext/plt/tests/run_timed.py 120 …` — 54 зелёных за секунды.
Гонять только через обёртку.

## 8. Правки командира в `ci.yml`

Файл смержился автоматически, обе правки целы. **Измерено.**

**Первая — Codecov.** На месте дословно:

```
      # Codecov knows the upstream repository, not the forks.  From a fork the
      # upload answers 404 and fail_ci_if_error below turns that into a red
      # job, drowning the signal we actually push for.  Skip the upload where
      # it cannot succeed; upstream keeps the strict behaviour untouched.
      - name: Upload coverage to Codecov
        if: github.repository == 'pg83/shitty'
```

**Вторая — шарды Darwin. Здесь нужна поправка к брифу.** Бриф описывает её как
«пять строк `if:` включают шарды». На деле правка `2eb6848e` — это **удаление**
пяти строк `if: ${{ github.event_name == 'workflow_dispatch' && inputs.darwin_tests }}`
вместе со входом `darwin_tests`; шарды гоняются потому, что гварда больше нет.
Сохраняемый инвариант — «у `tests-darwin-*` нет `if:`», а не наличие пяти строк.

Инвариант держится: **измерено**, ни у одного из пяти `tests-darwin-*` нет `if:`,
и `grep -c darwin_tests` даёт 0.

**Отдельно, для командира: апстрим пришёл к тому же сам.** В `a1b8cb29`
`grep -c darwin_tests` тоже 0 и `if:` у шардов тоже нет. Наша правка перестала
быть локальной — с этого мержа расхождения по этому месту больше нет, и в
следующей порции его можно не сторожить.

Побочно: `workflow_dispatch:` остался без единого входа — `c4f086a4` убрал
`sandboxed` вместе с strace-джобами.

## 9. `test_mode.cpp` и брендирование `pt`

Файл получил +7/−2: объявление и определение `foregroundProcessGroup()`,
возвращающее 0. Новых `SHITTY_*` строк и новых внешних символов нет
(**прочитано** по диффу `73cd2b78`).

**Ссылок из продуктового кода `pt` — ноль.** Три независимых измерения:

1. `test_mode.h` объявляет ровно одну функцию — `runTestMode()`. Её единственный
   вызов из продуктового кода, `lib/shitty/application.cpp:1147`, стоит под
   `#ifdef SHITTY_FOR_TESTS` (`application.cpp:1130-1132`), которого продуктовая
   сборка не определяет. Итого **0** ссылок.
2. `strings .build/pt | grep -ci shitty` → **0**; `grep -c SHITTY_TEST` → **0**.
3. `python3 tst/pretty_binary_branding.py .build/pt` → `EXIT=0`.

Мина CLAUDE.md не сработала и порцией не приближена.

## 10. Опции в обоих конфигах

`titleFallback = "process"` добавлен и в `bin/st/shitty.toml:201`, и в
`bin/pt/pretty.toml:200`. Конфликт был в обоих файлах и в обоих разрешён
одинаково: порядок алфавитный, `title` → `titleFallback` → `transparentTitlebar`,
наш `transparentTitlebar` сохранён.

Наблюдателя у этого по-прежнему нет ни одного — проверено глазами, как и
предписано.

## 11. Принесённые файлы против локальных узлов

**Измерено**, `git diff --numstat 945f60f0`: 35 файлов, **+2153 −331**, всего
2484 тронутых строки.

Не доходят до локальных узлов сборки **четыре файла, 877 строк — 35,3 %**:

| Файл | Строк | Почему не доходит |
|---|---|---|
| `.github/workflows/ci.yml` | 393 | узла нет ни одного; это восемь коммитов порции про шардинг покрытия |
| `tst/wayland_title_fallback.py` | 204 | узел заводится, но в тестовую группу попадает под `if linux:` (`build.py:4645`), и нужен headless sway |
| `dev/merge_lcov.py` | 144 | вызывается только из `ci.yml:620,624`; в `build.py` не упоминается |
| `flake.nix` | 136 | Nix на машине есть, демон не поднят |

Доходят **31 файл, 1607 строк — 64,7 %**. Сюда входит и `ext/libstd/std/thr/context.cpp`
(узел 105/231 в `st`), который легко принять за невидимый.

`plt_unit_tests` порция не трогает вовсе — ни один изменённый файл не лежит в
`ext/plt`. Прогнан всё равно: `OK: 54`.

## 12. Сломанные цели

Те же две, обе по домержевым причинам (**измерено**):

- `main_fuzz` — `Undefined symbols: "_main"`, и больше ничего. Цель ждёт
  `-fsanitize=fuzzer`, который даёт `main` за libFuzzer; Apple clang его здесь не
  даёт. К переезду `vt_headless` отношения не имеет — недостающих
  `VtermHeadless::*` в списке нет.
- `st_memprofile` — `#include <gperftools/heap-profiler.h>`, заголовка нет.

## Обнаружено

**1. Девять безмолвных вызовов `reapChild()`.** Апстримные тесты
(`85823399`) зовут `reapChild()` без аргумента, а наш `pty_ut.cpp:414` со времён
G-серии требует pid: «Reaping by pid, never by -1: this binary forks in more than
one suite, and a blind wait hands the caller whichever corpse is ready». Git
пометил конфликтом **одно** место из десяти — остальные девять слились чисто и
упали бы на компиляции. На `master` безаргументных вызовов было ноль, стало
девять, теперь снова ноль: каждый новый тест берёт `handle->childPid()` сразу
после `spawnShell`, а `EngagedRegistryRecyclesAcrossHandles` — три pid'а и жнёт в
порядке A, C, B.

**2. Три места, где апстримный код звал наши разошедшиеся сигнатуры,
и все три git слил без конфликта.**

- `EngagedPtyFixture` (`pty_ut.cpp`) и `ForegroundProcessFillsTheTitleFallback`
  (`vt_headless_ut.cpp`) звали `VtermHeadless::create(*composer.pool,
  *composer.vtConfig.config, nullptr)` и девятиаргументную `Vterm::create`.
  Перенесены на нашу форму. У фикстуры четыре строки после `create()` —
  `composer.platform = …`, `composer.window = …`, `installVtHost()`,
  `setCellPixelSize(1,1)`, `geometry.resize(80,24,…)` — оказались телом нашей
  `create()` (`vt_headless.cpp:237-249`) и удалены как дубль, а не как лишнее.
- `TitleCaptureHost` (принесён апстримом) абстрактен против нашего `VtHost`: у
  нас есть `contentInsets()`, `surfaceResized()` и `cellCapacityExcept()` сверх
  апстримного. Все три проброшены во вложенный хост.
- `Fontpack::create` вырос на `symbols, symbolCount` (`7c04a229`). Апстрим
  поправил все свои вызовы; наш, в `vt_headless_ut.cpp`, он поправить не мог —
  этой строки у него нет.

Общее у всех трёх: **git молчит там, где разошлись сигнатуры, а не тексты.**
Единственный наблюдатель — компилятор, и в этой порции он нашёл больше, чем
`git merge`.

**3. Тест на `foregroundProcessGroup` я усилил премиссой.** Апстримный
`ForegroundProcessFillsTheTitleFallback` переключает `pty.group` с `getpid()` на
`getppid()` и ждёт, что имя переднего плана сменится. Если бы эти два pid
совпали или один был нулём, последний блок прошёл бы на пустом месте — ровно
восьмой случай из таблицы CLAUDE.md. Тест теперь сам утверждает
`getpid() != getppid()` и `оба > 0` **до** того, как что-либо проверять, по
образцу `MetalPanes::AnOverlayLandsOnTheStripsOfItsOwnPane`.

**4. `master` ушёл вперёд на четыре коммита, пока шла порция.** На момент отчёта
`master` = `5139723e` (мерж `GLASS-recon` и правки борда), моя база — `945f60f0`,
как и было велено. Первый же `git diff … master` показал два наших документа
«удалёнными»; это артефакт сравнения с уехавшей веткой, а не работа мержа. Все
числа критерия 11 сняты относительно `945f60f0`.

**5. `plt_unit_tests` без обёртки висит** — см. секцию 7.

**6. Формулировка критерия 3 недостижима** — см. секцию 3.

**7. Формулировка правки Darwin-шардов в брифе обратна фактической** — см.
секцию 8. Плюс: апстрим теперь несёт ту же форму сам.

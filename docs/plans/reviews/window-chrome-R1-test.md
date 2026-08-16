# Тесты: разбор опций window-chrome (T1, волна 1) — PASS

- **Дата:** 2026-08-17
- **Статус:** PASS
- **Скоуп:** волна 1-ревью плана `docs/plans/2026-08-17-window-chrome-quick-terminal.md` — тесты на разбор опций `transparentTitlebar`, `quick`, `quickHotkey`, введённых в T1 (коммит `5aeaf83f`)
- **Команда прогона:** `.build/unit_tests --threads=1 Options` (C++), `python3 -m pytest tst/test_config.py -v` (интеграционные)

## Итог

Добавлено 9 тестов (7 C++ unit в `options_ut.cpp`, 2 python-интеграционных в
`tst/test_config.py`), все зелёные на текущем коде и все чувствительны к
точечным дефектам (см. «Чувствительность тестов»). Блокирующих находок по
коду T1 нет. Независимо перепроверено и подтверждено утверждение исполнителя
про `11 node(s) failed, 29 requested target(s) broken` на `./build test -k` —
идентично на этой ветке и на чистом `master`, полный список ниже. Изменений в
`ext/plt/platform_cocoa_ut.mm` не вносил — в этой волне там нет чистой логики
для юнит-теста (объяснение в «Допущения»).

## Что покрыто

| Файл | Тесты | Кейсы |
|------|-------|-------|
| `options.cpp` (таблица `optionsTable`, `parse()`, `get()`) | `options_ut.cpp` — 7 новых `STD_TEST` в `Options` | дефолты трёх опций; CLI включает `quick`/`transparentTitlebar`; `-quickHotkey` принимает произвольный непустой текст (включая мусорный чорд — грамматика чорда не T1); пустой `-quickHotkey` отклоняется; значения из конфига (`quick`, `transparentTitlebar`, `quickHotkey`); CLI побеждает конфиг для `quick` (`options.cpp:593-624`); мусорное булево значение из конфига отклоняется (`getBool`, `options.cpp:882-896`) |
| `test_mode.cpp` (поля `transparent_titlebar=`/`quick=` в ответе `OPTIONS`), `application.cpp` (проброс в `composer.opts`) | `tst/test_config.py` — 2 новых теста в `ConfigFileTest` | дефолт `0`/`0` через протокол `OPTIONS`; значения из конфига доходят через `application.cpp`/`composer.opts` до `test_mode.cpp` |

Полный список новых тестов:

- `Options::QuickTransparentTitlebarAndHotkeyDefaultToDisabled`
- `Options::CommandLineTogglesQuickAndTransparentTitlebar`
- `Options::CommandLineQuickHotkeyAcceptsAnyNonEmptyText`
- `Options::EmptyCommandLineQuickHotkeyIsRejected`
- `Options::ConfigFileSetsQuickTransparentTitlebarAndQuickHotkey`
- `Options::CommandLineQuickBeatsConfiguredQuick`
- `Options::GarbageBooleanValueInConfigIsRejected`
- `ConfigFileTest::test_quick_and_transparent_titlebar_default_to_disabled`
- `ConfigFileTest::test_quick_and_transparent_titlebar_come_from_the_config`

## Результат прогона

- `options_ut.cpp` (suite `Options`): было 1 тест зелёный (пре-существующий) → стало 8, все зелёные. Прогнано дважды подряд (`--threads=1` и `--threads=4`, разный порядок выполнения) — результат одинаковый, флейка нет.
- `tst/test_config.py`: было 18 зелёных (по прогону T1) → стало 20, все зелёные. Прогнано дважды подряд — идентично.
- `./build test -k` (полный граф) на этой ветке (T1 + мои тесты):
  `build: 11 node(s) failed, 29 requested target(s) broken` — то же число, что называет T1.

Падающих тестов, введённых мной, нет — все новые тесты проходят на текущем
коде и падают только на намеренно внесённых дефектах (см. ниже).

## Независимая перепроверка «11 node(s) failed» (по просьбе team-lead)

Исполнитель (T1) утверждал, что все 11 падений `./build test -k` —
пре-существующие на `master`, не связанные с его правками, и назвал причину:
`tst/pty_test_helper.c` падает на `SIGWINCH` под macOS из-за
`_POSIX_C_SOURCE` без `_DARWIN_C_SOURCE`.

Перепроверено самостоятельно, независимо от прогона T1: запустил
`./build test -k` дважды — на этой ветке (`feat/window-chrome`, коммит
с моими тестами) и в отдельном чистом worktree на `master` (`b2cb0f58`).
Оба прогона дали дословно одинаковую сводку:

```
build: 11 node(s) failed, 29 requested target(s) broken
```

и один и тот же набор первопричин:

1. **Компиляция `tst/pty_test_helper.c` падает дважды** (двумя
   `run_unittest_group.py` шардами, тянущими её как зависимость) с
   `error: use of undeclared identifier 'SIGWINCH'` — подтверждаю диагноз
   T1: `_POSIX_C_SOURCE` без `_DARWIN_C_SOURCE`, macOS-специфично.
2. **`pretty_binary_branding.py`** — `forbidden branding at byte offset ...`
   в бинаре `pt`, тот же результат на branch и на master.
3. **Восемь python-шардов** (`group-02`, `group-03`, `group-04`, `group-06`,
   в обычном и `prod-parser`-варианте) с одними и теми же пятью тестами:
   - `test_legacy_arrow_modifier_matrix` (падает дважды: `key=262` и
     `key=263`, `modifiers=8`)
   - `test_soft_zero_departs_from_the_hinted_grid`
   - `test_sheared_tail_lands_in_the_captured_blank`
   - `test_russian_shift_ctrl_c_has_no_legacy_control_sequence`
   - `test_darkening_scales_with_the_option`

Ни один из этих тестов не касается опций, окна или `plt::Window`. Утверждение
T1 **подтверждено дословно**: список идентичен на branch и на чистом master,
включая номера шардов и имена тестов. Следующие волны (`R2-qa`, `R3-*`) могут
сверяться с этим списком вместо повторного расследования.

## Чувствительность тестов

Точечная проверка: 5 дефектов внесены по очереди в продакшн-код, каждый раз
пересобран `unit_tests`/`st_test` и прогнан целевой набор, дефект возвращён,
`git status`/`git diff` по продакшн-файлам проверен на чистоту после каждого
шага (и в самом конце — пусто).

| # | Дефект | Файл | Что покраснело |
|---|--------|------|-----------------|
| 1 | `hardDefault` опции `quick` `"false"` → `"true"` | `options.cpp:99` | `QuickTransparentTitlebarAndHotkeyDefaultToDisabled` |
| 2 | `quick = getBool("quick");` → `quick = false;` (CLI/конфиг больше не читаются) | `options.cpp:1064` | `CommandLineTogglesQuickAndTransparentTitlebar`, `ConfigFileSetsQuickTransparentTitlebarAndQuickHotkey`, `GarbageBooleanValueInConfigIsRejected` (побочный эффект: без вызова `getBool` пропадает и валидация мусора) |
| 3 | В `get()` конфиг проверяется раньше командной строки (обратный приоритет) | `options.cpp:605-611` | только `CommandLineQuickBeatsConfiguredQuick` — точное попадание в цель |
| 4 | Проверка `hotkey.empty()` заменена на `false` (никогда не срабатывает) | `options.cpp:1068` | только `EmptyCommandLineQuickHotkeyIsRejected` |
| 5 | Поля `transparent_titlebar=`/`quick=` в ответе `OPTIONS` захардкожены в `0` | `test_mode.cpp:2392` | только `test_quick_and_transparent_titlebar_come_from_the_config` (тест «default» остался зелёным — корректно, дефолт и так `0`) |

Все 5 дефектов дали ожидаемый и **только** ожидаемый красный список — ни
одного случая, когда тест ловил не то, что должен, или не ловил ничего.
Продакшн-код после каждого шага возвращён `Edit`-ом к исходной строке;
финальная проверка — `git diff` по `options.cpp`, `options.h`, `test_mode.cpp`
пуста.

Тесты не переспецифицированы отдельно не проверялось на копии репозитория —
в этой волне нет находок с предлагаемым исправлением (см. «Находки»), тестам
не на чем перегибать.

Mutation-тестирование: в проекте не настроено (нет Stryker/mutmut/cosmic-ray
для C++/Python здесь), не проводилось — только точечная проверка выше.

## Покрытие

Автоматический подсчёт по изменённым строкам (`diff_coverage.py` из скила)
не проводился: в проекте не настроена ни C++-инструментация (gcov/lcov —
`./build` не собирает coverage-вариант), ни `coverage.py` на стороне Python
(`ModuleNotFoundError: No module named 'coverage'` в окружении). Поднимать
coverage-сборку ради одной ревью-задачи — инфраструктурное решение шире
скоупа R1-test, поэтому вместо процента даю ручное сопоставление новых строк
и тестов, покрывающих их прямым выполнением:

- `options.cpp:99-100,107` (строки таблицы `quick`/`quickHotkey`/`transparentTitlebar`) — покрыты всеми 7 C++-тестами (через `parse()`).
- `options.cpp:1064` (`quick = getBool("quick")`) — `CommandLineTogglesQuickAndTransparentTitlebar`, `ConfigFileSets...`, `CommandLineQuickBeatsConfiguredQuick`, `GarbageBooleanValueInConfigIsRejected`.
- `options.cpp:1065-1072` (блок `quickHotkey`, включая ветку `raiseError`) — обе ветки покрыты: happy path `CommandLineQuickHotkeyAcceptsAnyNonEmptyText`/`ConfigFileSets...`, ошибка `EmptyCommandLineQuickHotkeyIsRejected`.
- `options.cpp:1075` (`transparentTitlebar = getBool(...)`) — `CommandLineTogglesQuickAndTransparentTitlebar`, `ConfigFileSets...`.
- `options.h` (новые поля) — покрыты транзитивно всеми тестами, которые их читают; своей логики там нет.
- `test_mode.cpp:2392` (два новых поля в `OPTIONS`) — оба python-теста.
- `application.cpp` (условный `requestShow()` при `quick`, проброс `.transparentTitlebar`/`.quick` в `WindowOptions`) — **не покрыто**, см. «Допущения».

Непокрытых веток внутри «моих» файлов (`options.cpp`/`options.h`/`test_mode.cpp`)
нет — каждая новая строка исполняется хотя бы одним новым тестом, включая
обе ветки `raiseError`/успех.

## Находки

Блокирующих и важных находок по коду T1 нет — все проверенные критерии
приёмки T1 подтверждаются тестами без отклонений.

### 1. Список владения R1-test не включает `tst/test_options.py` — замечание

- **Где:** план `docs/plans/2026-08-17-window-chrome-quick-terminal.md` (секция R1-test) и постановка team-lead — оба ограничивают владение `options_ut.cpp`, `ext/plt/platform_cocoa_ut.mm`, `tst/test_config.py`.
- **Чем грозит:** `tst/test_options.py` — файл с точным прецедентом для такого рода тестов (`test_maximized_is_a_boolean_startup_option`, `test_no_decorations_is_a_boolean_option`, `test_boolean_minus_plus_and_advanced_values` уже проверяют ровно паттерн «дефолт / CLI / `-x +x` / мусорное значение» для соседних булевых опций). Тесты CLI-стороны для `quick`/`transparentTitlebar` тематически ближе к этому файлу, чем к `test_config.py`, который сфокусирован на поведении TOML-конфига (импорты, reload, hostile-формы).
- **Предлагается:** ничего менять сейчас не нужно — я соблюдал заданную границу владения и разместил CLI-тесты в `options_ut.cpp` (что тоже корректно и даже точнее, так как даёт прямой доступ к структуре `Options` без полной сборки бинаря). Для будущих волн (`R3-test` тоже не владеет `test_options.py`) стоит иметь в виду это несоответствие при разметке владения файлами в `coding-plan`.

### 2. `ext/plt/platform_cocoa_ut.mm` не тронут — замечание, осознанное решение

- **Где:** `ext/plt/platform_cocoa.mm:1178-1189` (`requestHide()`, `requestShowAt()`).
- **Почему не покрывал:** `requestHide()` — прямой вызов `[window orderOut:nil]` без ветвления; `requestShowAt(ShowPlacement)` в этой волне **полностью игнорирует** аргумент и всегда делегирует в `requestShow()` — само значение `ShowPlacement::TopOfActiveScreen` пока нигде не читается (T1 сам это фиксирует в отчёте и плане: наполняет T4). Юнит-тестировать нечего — нет ни чистой функции с логикой (как `cocoaWindowStyleMask()`, уже покрытая существующим тестом), ни ветвления по значению enum. Это симметрично собственному решению плана не заводить `R2-test` для T2 по той же причине («не добавляет тестируемой логики сверх маски окна»).

## Допущения

- **`application.cpp`'s условный `requestShow()` и проброс `.quick`/`.transparentTitlebar` в `plt::WindowOptions` не покрыты автотестами.** Единственный наблюдаемый через существующую тестовую инфраструктуру факт — значения `composer.opts->quick`/`transparentTitlebar` (через `OPTIONS`), а не фактическое поведение окна (показано/скрыто). Проверка реального поведения требует либо живого `NSWindow` (это территория `platform_cocoa_ut.mm`/T4, вне контракта этой волны — заглушки ещё ничего не делают с `ShowPlacement`), либo сигнала видимости окна, которого протокол теста сейчас не предоставляет. T1 в своём отчёте проверял это вручную через `osascript` (подсчёт окон процесса); дальнейшая автоматизация — решение вне скоупа R1-test.
- **Покрытие изменённых строк не посчитано процентом** — обоснование и ручное сопоставление см. в разделе «Покрытие».
- **Диапазон `-lines=` для стиля не проверялся отдельно для `tst/test_config.py`** — файл питоновский, `./style.py`/`clang-format` на него не распространяются (см. `STYLE_PRJ.md`: форматтер покрывает только C++/Obj-C++). Новый код в этом файле соответствует стилю соседних тестов в том же файле (4 пробела отступ, `assertEqual`, `tempfile.TemporaryDirectory`).
- **Полный `./style.py` без аргументов на затронутых файлах не запускал** — по предупреждению T1 (существующий разрыв между зафиксированным форматированием дерева и любым доступным `clang-format`, воспроизводится и на чистом `master`). Вместо этого точечно проверил `options_ut.cpp` через `clang-format -lines=1:195 --style=file` (весь файл, так как в нём нет ни одной непотронутой строки старого форматирования, кроме уже написанного мной кода) — diff пустой, 0 нарушений.

## Было сломано до начала работы

Да, весь набор описан в разделе «Независимая перепроверка «11 node(s) failed»»
выше: `tst/pty_test_helper.c` (SIGWINCH), `pretty_binary_branding.py`, и пять
python-тестов в восьми шардах. Все пре-существующие на `master`, никак не
связаны ни с T1, ни с моими тестами. Список стоит использовать как эталон
для последующих волн (`R2-qa`, `R3-test`, `R3-qa`) вместо повторного
расследования.

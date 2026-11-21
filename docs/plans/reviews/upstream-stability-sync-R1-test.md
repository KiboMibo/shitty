Находок: 0 блок / 0 важн / 1 зам / 1 сущ

# Тесты: upstream stability sync (R1-test, волна 1) — PASS_WITH_FINDINGS

- **Дата:** 2026-11-21
- **Статус:** `PASS_WITH_FINDINGS`
- **Проверяемый коммит:** `8b163758a053c9b1fca826dddb580d853c5547e3` (`test/R1-upstream-stability-sync`)
- **База diff:** `0ac846d530cd76cada88f2eebf870238658e5879` (`fork/master`)
- **Скоуп:** критерии T1–T3 плана `docs/plans/2026-11-19-upstream-stability-sync.md`: `lib/vterm/{utf8,grapheme}.cpp`, `lib/shitty/{pty,session,startup,configuration,options,application}.cpp`, `ui_csd_tabs.mm`, заголовки `composer.h`, `options.h`, `startup.h`. Тестовые файлы: `options_ut.cpp`, `startup_ut.cpp`, `grapheme_ut.cpp`, `utf8_ut.cpp`, `pty_ut.cpp`, `session_ut.cpp`, `ui_csd_tabs_ut.cpp`, `configuration_ut.cpp`.
- **Команда прогона:** `SHITTY_PTY_TEST_HELPER="$PWD/.build/pty_test_helper" .build/unit_tests < /dev/null`; покрытие — instrumented-сборка `.buildcov/unit_tests` + `xcrun llvm-cov`.

## Итог

Три новых наблюдателя добавлены в тестовые файлы; вместе с уже написанными в T1–T3 наблюдателями criteria-тесты покрывают все четыре desktop-случая, оба новых spawn-error пути и сохранение сессий при отказе вкладки. Полный C++-набор зелёный (`OK: 1128`), дважды подряд, production-код не изменён. Покрытие изменённых исполняемых строк — 80.4% (45/56), порог взят.

Обнаружен один surviving mutation в `ui_csd_tabs.mm`: замена listener-route в `tabOpened()` на прямой `sessions->newSession()` не покрасила ни один существующий тест. Строки не покрыты в прогоне coverage-сборки (`0/6` по `tabOpened()`), то есть есть и непокрытый фрагмент, и отсутствующая чувствительность к регрессии. Продакшн-код править запрещено, поэтому это finding, а не правка; дополнительный тест в `ui_csd_tabs_ut.cpp` потребовал бы изменения production-интерфейса (см. «Находки»).

## Что покрыто

| Файл | Тесты | Кейсы |
|------|-------|-------|
| `lib/vterm/utf8.cpp` | `utf8_ut.cpp` (T1) | инкрементальный decode и resync multibyte-блоков; мутация снятия `resetBefore` даёт красный `Utf8::TextBlockResyncsIntoMultibyteRuns` |
| `lib/vterm/grapheme.cpp` | `grapheme_ut.cpp` (T1) | boundary-state seeding для независимых кластеров; мутация `Prepend` даёт красный `Grapheme::KeepsIndependentClustersIndependent` |
| `lib/shitty/pty.cpp` | `pty_ut.cpp` (T2 + **новый** `RefusedMasterOpenThrowsAndAllowsACleanRetry`, `RefusedForkRestoresTheSignalMaskAndLeaksNoDescriptor`) | `posix_openpt()` под `RLIMIT_NOFILE`, отказ `fork()` под `RLIMIT_NPROC` в child-процессе: exception, восстановление signal mask, отсутствие утечки fd, повторный spawn в родителе |
| `lib/shitty/session.cpp` | `session_ut.cpp` (T2) | отказ initial spawn пробрасывается и освобождает arena; отказ new tab сохраняет прежние сессии и позволяет retry |
| `lib/shitty/startup.cpp` | `startup_ut.cpp` (T3) | `launchedFromDesktop(parent)` только для `parent == 1` на macOS; мутация предиката краснит `Startup::OnlyLaunchdAsParentMeansADesktopLaunch` |
| `lib/shitty/configuration_ut.cpp` | **новый** `Config::DesktopLaunchClassificationIsReusedOnReload` | desktop + HardDefault даёт login, reload сохраняет классификацию и применяет новые значения конфига; обычный launch на том же парсере и пустом конфиге login не включает — фикстура не вырождена |
| `lib/shitty/options.cpp` | `options_ut.cpp` (T3) | desktop + явный false (CLI/config), non-desktop + HardDefault, non-desktop + явный true, reload с захваченной классификацией |
| `lib/shitty/application.cpp`, `composer.h`, `options.h`, `startup.h` | `Options::*`, `Config::*`, `Startup::*` | capture при старте и передача классификации через reload; точечные мутации дают red (см. ниже) |

Добавлено тестов в этой проверке: **3** (`configuration_ut.cpp` — 1, `pty_ut.cpp` — 2). Плюс 10 тестов, написанных исполнителями T1–T3 и проверенных здесь на чувствительность.

## Результат прогона

- Базовый прогон до написания новых тестов: `OK: 1125` (`/private/tmp/r1-baseline-unit.log`).
- Первый прогон новых тестов: `OK: 1, ERR: 2` — оба новых PTY-теста уперлись в process-global `setrlimit` и повлияли друг на друга; тесты переписаны (fork для `RLIMIT_NPROC`, снятие лимита immediately after refusal для `RLIMIT_NOFILE`), production-код не тронут.
- Итоговый прогон после правки тестов: `OK: 3, SKIP: 1125` (`/private/tmp/r1-new-tests2.log`).
- Полный набор на проверяемом коммите: `OK: 1128`, `EXIT=0` — два подряд свежих прогона (`/private/tmp/r1-rereview-final-unit.log`, `/private/tmp/r1-rereview-final-unit2.log`).
- Стабильность: ключевые наблюдатели 10/10 зелёные в отдельном прогоне; полный набор дважды подряд без расхождений.
- Падающих тестов, добавленных этой проверкой, нет.

Косвенно подтверждено выполнение критериев T3 по Python-набору: `tst/test_options.py` + `tst/test_startup.py` — `Ran 33`, `OK (skipped=1)`; `tst/test_config.py` — `Ran 34`, `OK`. Оба бренда собраны (`./build st pt --clear`, rc `0`) в отчётах T2/T3 и QA. Полный `./build test -k` и 40 shard-ов Python-набора в T0 красные по host-local причинам (см. «Было сломано до начала работы»).

## Покрытие

- Изменённые исполняемые строки: **80.4%** (45/56), порог 80% взят.
- Покрытие посчитано по diff `0ac846d5..8b163758` для production-файлов; неисполняемые строки (комментарии, `#include`,`#if`-ветки другой платформы) исключены. Первичный отчёт — `/private/tmp/r1-resume-changed-coverage.json` (llvm-cov JSON + `llvm-cov show`).
- Скрипт `scripts/diff_coverage.py` в репозитории отсутствует; вместо него использован `xcrun llvm-cov` с ручным разбором `git diff --unified=0`, как в предыдущих отчётах этого проекта (`docs/plans/reviews/window-chrome-R1-test.json`).

По файлам (исполняемые изменённые строки): `utf8.cpp` 13/13, `grapheme.cpp` 4/4, `application.cpp` 1/1, `configuration.cpp` 1/1, `options.cpp` 7/7, `session.cpp` 5/5, `startup.cpp` 4/4, `pty.cpp` 10/15, `ui_csd_tabs.mm` 0/6.

Непокрытые изменённые строки и почему:

- `pty.cpp:126,129,132,136` — ошибки `fcntl(FD_CLOEXEC)`, `grantpt()`, `unlockpt()`, `ptsname()`. Все четыре — системные вызовы, которые на macOS без depth-first fault injection на каждый вызов не воспроизводятся: `RLIMIT_NOFILE` ловит только `posix_openpt()`. Обработчики однотипны и уже проверены через mutation на `posix_openpt()` и через `RefusedSpawnThrowsAndLeaksNothing` для `open()` slave; отдельные тесты были бы прошивкой окружения, а не поведением.
- `pty.cpp:939` — `spawnError("fork", ...)`. Наблюдатель `RefusedForkRestoresTheSignalMaskAndLeaksNoDescriptor` проверяет поведение при отказе `fork()` через child с `RLIMIT_NPROC=0`; мутация ветки поднимает именно этот тест, но строка `spawnError` в профиле не исполнялась: отказ происходит в дочернем процессе вне профиля coverage-сборки. Покрытие этой ветки остаётся спорным — см. допущения.
- `ui_csd_tabs.mm:760,764-768` — `tabOpened()` недостижим из unit-тестов: `CsdTabsUi` определён в `.mm`, не экспортирован в заголовке, вызывается только из `mouseDown:` NSView. Мутация маршрута выжила, что зафиксировано как находка.

## Находки

### 1. `CsdTabsUi::tabOpened()` не покрыт тестом и мутация маршрута выжила — замечание

- **Где:** `lib/shitty/ui_csd_tabs.mm:759-769` (мутация: замена `composer.newTabListeners` на прямой `sessions->newSession()`)
- **Чем грозит:** T2 явно переносит plus-button на recoverable new-tab route. Если маршрут сломать, GUI-клик по `+` снова убьёт процесс при отказе spawn; unit-набор останется зелёным. Строки точно непокрыты (0/6).
- **Доказательство:** временная мутация (`r1-t2-ui-route-mutation-tests.log`) — `OK: 10, SKIP: 1118`, ни один тест не покраснел.
- **Предлагается:** либо экспортировать в `ui_csd_tabs.h` узкий тестовый вход (аналог `csdTabsChromeHovered`, уже объявленного в `ui_csd_tabs_ut.cpp`), либо оформить integration-тест GUI-клика. Это правка production-интерфейса, вне владения R1-test.

### 2. Наслелованные красные Python-шарды и `production_surface` — существовавшая (была блокирующей)

- **Где:** T0-эталон `docs/reports/upstream-stability-sync-T0-baseline-2026-11-19.md`; `/private/tmp/r1-qa-current-red.txt`.
- **Чем грозит:** общий `./build test` красный на shitty host (2 failure в `production_surface` из-за локального конфига и HH:MM:SS), 19/40 Python-шардов падают из-за отсутствующих bitmap/color fonts. Состояние идентично на чистом `0ac846d5`, волна T1–T3 его не внесла.
- **Предлагается:** не относить к F1; в текущей волне использовать поимённые зелёные прогоны (unit, `test_options.py`, `test_startup.py`, `test_config.py`, diagnostics with empty HOME).

## Чувствительность тестов

Точечная проверка (все мутации вносились во временных копиях production-файлов, затем файлы возвращены; `git status` показывает только два изменённых тестовых файла, `git diff` по production-файлам пуст):

| Мутация | Наблюдатель | Результат |
|---|---|---|
| UTF-8: убрать `text.resetBefore |= ...` | `Utf8::TextBlockResyncsIntoMultibyteRuns` | `ERR: 1`, красный на `utf8_ut.cpp:151` |
| Grapheme: `stateClass = GraphemeClass::Prepend` в boundary seeding | `Grapheme::KeepsIndependentClustersIndependent` | `ERR: 1`, красный на `grapheme_ut.cpp:54` |
| Pty: убрать re-set signal mask after fork failure | `Pty::RefusedForkRestoresTheSignalMaskAndLeaksNoDescriptor` | `ERR: 1`, child exit 15 |
| Pty: утечка fd на fork-failure path | тот же тест | `ERR: 1`, child exit 16 |
| Pty: вернуть прежний fatal `sysError` | `Pty::RefusedSpawnThrowsAndLeaksNothing`, `SessionSet::RefusedNewTab…` | process exit на `open(): Too many open files`, `ERR: 1` |
| Session: убрать recovery в new-tab listener | `SessionSet::RefusedNewTabPreservesExistingSessionsAndCanRetry` | `ERR: 1` |
| Session: initial spawn swallow | `SessionSet::RefusedInitialSpawnPropagatesAndReleasesItsArena` | `ERR: 1` |
| Startup: `parent == 1` → `true` | `Startup::OnlyLaunchdAsParentMeansADesktopLaunch` | `ERR: 1` |
| Options: сбросить desktop hard default | `Options::DesktopLaunchTurnsTheLoginHardDefaultOn` и др. | 3 красных |
| Options: убрать priority explicit false | `Options::DesktopLaunchHonoursExplicitLoginFalseFromCliAndConfig` | `ERR: 1` |
| Config: не передавать desktopLaunch в reload | `Config::DesktopLaunchClassificationIsReusedOnReload` | `ERR: 1`, `configuration_ut.cpp:147` |
| **Ui: listener route → прямой `newSession()`** | **нет** | **выжившая мутация, `OK: 10`** |

Родительские (до T1/T2) реализации UTF-8/grapheme и pty/session в control-прогонах не дают observer-различия там, где изменения чисто оптимизационные; для них чувствительность доказана адресными мутациями состояния, а не сравнением output.

Не переспецифицированы: у предлагаемых правок в отдельной копии набор зелёный без правок тестов в тех случаях, где есть понятная гипотеза (T3 forwarding, explicit-priority). Mutation-тестирование (Stryker/mutmut) в проекте не настроено, не запускалось.

## Допущения

- Был выбран де delegated-режим: уточняющих вопросов не задавалось; все неочевидные решения зафиксированы здесь.
- База для diff'а информативна (`0ac846d5`), покрытие считалось по изменённым строкам, не по всему файлу.
- Тест `RefusedForkRestoresTheSignalMaskAndLeaksNoDescriptor` запускает `setrlimit(RLIMIT_NPROC, 0)` в child-процессе, чтобы не испортить process-global limit остального набора; это обход ограничения macOS, а не изменение продакшн-логики.
- `ui_csd_tabs.mm` остался без нового теста: это требует экспорта внутреннего типа/метода в заголовок (production-изменение). Находка оставлена видимой, тест не помечен skip/xfail.
- Строка `pty.cpp:939` (`spawnError("fork")`) непокрыта в профиле coverage-сборки, хотя поведение при отказе fork проверено и sensitive к мутации; если команда сочтёт это существенным, нужен отдельный fault-injection тест с обновлённым владением файлов.
- Форматирование brace в `configuration_ut.cpp`/`pty_ut.cpp` приведено к стилю surrounding local code (`): {`) как часть правки тестов; поведенческого смысла не несёт.

## Было сломано до начала работы

- `./build test -k`: `11 node(s) failed, 29 requested target(s) broken` на этой ветке и на чистом `master` — идентично (перепроверено в R1-test previous run; причина — host-local, не волна).
- `production_surface`: 2 failure из-за локального `~/.config/shitty/shitty.toml` и HH:MM:SS; с пустым HOME `OK`.
- 19/40 Python-шардов: отсутствующие real bitmap/color fonts; в T0 то же самое.
- Ни один из этих отказов не закрыт и не отнесён к F1.

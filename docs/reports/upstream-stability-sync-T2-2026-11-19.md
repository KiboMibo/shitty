# T2. Recoverable new tab

**Ветка:** `feat/T2-recoverable-new-tab` → `wave/upstream-stability-sync-w1`
**База:** `3e77835555d719a86afd109d6ea4d7c5f910ef04`
**Код и тесты:** `69889b54`
**Источники:** upstream `b65dfcfb`, `25d98cf9`
**План:** `docs/plans/2026-11-19-upstream-stability-sync.md`

## Что сделано

Ошибки открытия/настройки PTY и `fork()` больше не завершают процесс через `exit(1)`: они становятся `FatalError`, после освобождения уже открытых descriptors. Keyboard new-tab action ловит ошибку, печатает warning и сохраняет существующие вкладки; `CsdTabsUi::tabOpened()` теперь вызывает тот же `composer.newTabListeners` route. Первый spawn при создании `SessionSet` по-прежнему пробрасывает ошибку вызывающему startup-коду.

## Изменения

| Файл | Что |
|------|-----|
| `lib/shitty/pty.cpp` | throw-based `spawnError`; закрытие master/slave на ошибках; восстановление signal mask перед ошибкой `fork` |
| `lib/shitty/session.cpp` | recoverable обработчик `InputActions::NewTab` с warning |
| `lib/shitty/ui_csd_tabs.mm` | Cocoa plus-button проходит через зарегистрированный new-tab action |
| `lib/shitty/pty_ut.cpp` | повторяемый отказ по `RLIMIT_NOFILE`, проверка отсутствия FD-утечки |
| `lib/shitty/session_ut.cpp` | отказ с сохранением двух вкладок и retry; startup-fatal семантика и освобождение arena |

## Решения

- Сохранена архитектура форка: существующий `SessionSetImpl::openSession()` уже удаляет per-session arena на исключении, а `newSession()` откатывает только что посаженный pane. Второй механизм cleanup не добавлялся.
- Recovery находится только в общем `CallSessionAction`: поэтому keyboard и назначенный Cocoa route имеют одну семантику, а прямой startup `SessionSet::create()` не перехватывает ошибку.
- `sysError()` оставлен для process-lifetime drain pipe/thread failures; они не являются recoverable new-tab spawn errors.
- Сигнатура `Pty::spawn` и `PtyHandle` не менялись. Grep по дереву проверил реализации: `PtyHandleImpl`, `StubHandle`, `SurvivorHandle`, `TeeHandle`, `ReplyPty`, `SecondPtyStub`, `TestPty`, `OutputPtyHandle`; адаптация им не нужна.

## Проверка

Все команды выполнялись в выделенном worktree на macOS.

| Проверка | Команда | Код / результат |
|----------|---------|-----------------|
| Оба бренда, чистая сборка | `./build st pt --clear` | `0` |
| Unit tests и helper | `./build unit_tests pty_test_helper` | `0` |
| Полный набор | `SHITTY_PTY_TEST_HELPER="$PWD/.build/pty_test_helper" .build/unit_tests < /dev/null` | `0`, итог `OK: 1118` |
| Финальная пересборка после mutation control | `./build st pt unit_tests pty_test_helper` | `0` |
| Финальный полный набор | та же команда `unit_tests` | `0`, итог `OK: 1118` |
| Diff hygiene | `git diff --check` | `0` |

Новые наблюдатели присутствуют и зелёные в финальном полном прогоне:

- `Pty::RefusedSpawnThrowsAndLeaksNothing`;
- `SessionSet::RefusedNewTabPreservesExistingSessionsAndCanRetry` — три warning, две прежние вкладки остаются активными/работоспособными, последующий retry создаёт третью;
- `SessionSet::RefusedInitialSpawnPropagatesAndReleasesItsArena`.

**Mutation control на родительской реализации.** Временно были восстановлены только `HEAD:lib/shitty/pty.cpp` и `HEAD:lib/shitty/session.cpp`, затем оба файла возвращены к исправленным версиям и набор пересобран:

| Тест на старой реализации | Результат |
|---------------------------|-----------|
| `Pty::RefusedSpawnThrowsAndLeaksNothing` | `EXIT=1`, старый `sysError` завершил процесс на `open(): Too many open files (errno=24)`; итоговой строки нет |
| `SessionSet::RefusedNewTabPreservesExistingSessionsAndCanRetry` | `EXIT=1`, `OK: 0, ERR: 1, SKIP: 1117` |

**Критерии T2:**

- [x] Отказ new tab не завершает процесс, существующая `SessionSet` работает и retry возможен — названный `RefusedNewTab…` тест, три warning, финальный `OK: 1118`.
- [x] Keyboard и `CsdTabsUi` проходят один recoverable action — `CallSessionAction::onListen()` и цикл `composer.newTabListeners` в `CsdTabsUi::tabOpened()`; оба бренда собраны.
- [x] Нет FD-утечки на новых error paths — двухраундовый `RLIMIT_NOFILE` тест; ручная проверка всех веток `posix_openpt`, `fcntl`, `grantpt`, `unlockpt`, `ptsname`, slave `open`, `fork`.
- [x] `SIGCHLD` mask восстанавливается на fork-error path — `sigprocmask(SIG_SETMASK, &previousMask, nullptr)` выполняется после закрытия обоих FD и до `spawnError`; success parent/child paths также сохраняют прежнее восстановление.
- [x] Startup spawn failure не становится молча успешным — `RefusedInitialSpawn…` получает исходный `FatalError`, подтверждает `destroyed == 1` и `liveSessions == 0`.

## Ограничения и оставшиеся routes

- Отдельного fault-injection теста именно для отказа `fork()` нет; его FD/mask порядок проверен чтением кода. Перенесённый upstream test индуцирует отказ PTY по descriptor limit.
- Python-набор и Linux-стенд не запускались в T2: поимённое сравнение с T0 и кросс-платформенная приёмка относятся к I1/R1-qa. Локальные критерии T2 (оба бренда и полный unit suite) выполнены.
- Вне разрешённого скоупа остаются прямые `sessions->newSession()` в `SidebarTabsUi::tabOpened()` (`ui_sidebar_tabs.mm`), sidebar folder popover, `PaletteSession::pick()` и двух Wayland chrome routes. Это оставшееся расхождение с общей recoverable семантикой, а не часть двух портируемых upstream-коммитов. Особо: folder popover после `newSession()` вызывает `dropTab`; механически заменять его на void-listener, который проглатывает исключение, нельзя — иначе `dropTab` может примениться к прежней активной вкладке. Нужен отдельный success/failure contract.

## Как откатить

`git revert 69889b54` и коммита этого отчёта. Миграций, флагов и изменений конфигурации нет.

## Ревьюеру

В первую очередь проверить порядок close/restore/throw в `PtyImpl::spawn()` и то, что recovery не перемещён в `SessionSet::create()`, где startup failure обязан остаться видимым.

## Обнаружено

Pi-lens без project include path сообщал ложные `lib/vterm/pty.h file not found` и производные unknown-type diagnostics на ранее существующих строках. Штатный компиляторный граф собрал оба бренда и unit tests успешно; исходники под этот неполный analyzer context не менялись.

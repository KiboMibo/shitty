# План: перенос stability-исправлений из upstream 2026-09/10

- **Дата:** 2026-11-19
- **Статус:** к исполнению
- **Слаг:** `upstream-stability-sync`
- **Борд:** `docs/plans/state/upstream-stability-sync-status.md`
- **База:** `fork/master` = `0ac846d5`
- **Источник:** `origin/master` = `509bd3f9` (`https://github.com/pg83/shitty`)
- **Цель:** перенести четыре связанных исправления, не затягивая конфликтный upstream merge целиком: UTF-8 resync, grapheme cluster class, recoverable new tab и desktop login shell.

## 1. Контекст

Форк существенно разошёлся с upstream: полный пробный `merge-tree` даёт 24 конфликта. Выбран перенос маленькой проверяемой порции. Два Unicode-коммита применяются чисто; tab recovery и desktop login требуют ручного портирования.

У форка уже есть `launchDirectory()` в `lib/shitty/startup.cpp`: пустой `-directory` при унаследованном `/` переводит процесс в home. Поэтому из upstream desktop-истории переносится **только классификация desktop launch и автоматический login default**, а не второй механизм `chdir()`.

Правила тестов обязательны к прочтению в `CLAUDE.md`: `./build st pt` не строит и не запускает unit tests; ручной `unit_tests` требует `SHITTY_PTY_TEST_HELPER`; `EXIT=148` без итогового `OK: N` перезапускается, `EXIT=139` является падением; питоновский набор сравнивается с эталоном поимённо и тем же режимом запуска.

## 2. Критерии готовности

1. Перенесены поведение и тесты `0842b9ed`, `78037448`, `b65dfcfb`, `25d98cf9`, `88d3e3e7`, `519c2c5e` применительно к архитектуре форка.
2. Ошибка создания PTY/процесса при открытии дополнительной вкладки не завершает приложение и не разрушает существующие сессии; keyboard path и macOS UI path используют один recoverable action.
3. Desktop launch на macOS получает login shell, пока пользователь явно не задал `login`; классификация захвачена один раз до первого parse и не меняется на config reload. Обычный shell launch и не-macOS поведение не меняются.
4. Существующий `launchDirectory()` остаётся единственным механизмом выбора стартового каталога; второго безусловного `chdir()` из upstream нет.
5. `./build st pt --clear`, `./build unit_tests pty_test_helper` успешны; ручной полный `unit_tests` завершается `EXIT=0` и содержит итоговый `OK: N`.
6. Python-набор запущен штатно через `./build`; список красных не вырос относительно свежего эталона `T0` на той же базе и тем же режимом.
7. Проверочные `R1-test`, `R1-sec`, `R1-qa` закрыты; блокирующие находки устранены через `F1` и перепроверены.
8. Собранная ветка опубликована в `fork` и имеет PR в `master`. Сам merge в `master` — отдельное решение пользователя.

## 3. В скоупе / вне скоупа

**В скоупе:** шесть названных upstream-коммитов; их адаптация и тесты; необходимые описания существующей опции `login`; отчёты и борд.

**Вне скоупа:** остальные 45 upstream-коммитов; PTY ring; scroll batching; cell-extra и Unicode-оптимизации кроме двух bug fixes; Core Text cache; Cocoa/tab chrome redesign; `cursorKeepSelectionFg`; CI action bumps; release signing; общий build/style sync; визуальные снимки окон (поведение здесь не визуальное).

## 4. Архитектурные решения

### Р1. Selective port, не merge и не rebase

T1 может сохранить upstream-историю через cherry-pick. T2/T3 портируют смысл вручную: механический cherry-pick конфликтует с нашими PTY, options и startup. Не разрешать конфликты выбором одной стороны целиком.

### Р2. Один recoverable new-tab action

`CallSessionAction` остаётся общей точкой keyboard action. Cocoa `tabOpened()` вызывает зарегистрированных `composer.newTabListeners`, а не `sessions->newSession()` напрямую. Ошибка логируется как warning с описанием исключения; существующие tabs продолжают жить.

### Р3. Desktop classification живёт в `Composer`

`launchedFromDesktop(getppid())` вызывается один раз в `ApplicationImpl::run()` до первого `Options::create/parse`; результат передаётся в parser и сохраняется в `Composer`. На reload используется сохранённое значение. Это предотвращает переклассификацию после reparenting к launchd.

### Р4. Явный пользовательский `login` сильнее desktop default

Автоматика действует только когда источник значения — `OptionSource::HardDefault`. Явные CLI/config `login=true` и `login=false` сохраняют своё значение. Hard default опции не меняется и инициализаторы `Options` не переупорядочиваются.

### Р5. Не менять границы `lib/vterm`

T1 переносит только две локальные правки и тесты в существующих `utf8`/`grapheme` модулях. Не подтягивать соседние оптимизации и не менять публичные сигнатуры.

## 5. Контракты

- Интеграционная ветка: `wave/upstream-stability-sync-w1` от `fork/master`. Отдельного `develop` в проекте нет; wave-ветка является рабочей до PR.
- Исполнители работают каждый в собственном worktree и ветке `feat/<ID>-…`; PR между задачами не открывают.
- Ни один существующий тест не удаляется и не помечается skip ради зелёного результата.
- Изменённые production-файлы одной волны не пересекаются между задачами. Общие документы плана и борд меняет только командир.
- Отчёт исполнителя содержит SHA, точные команды, коды возврата, поимённые критерии и все незакрытые ограничения.

## 6. Карта задач

| Волна | ID | Название | Зависит от |
|---|---|---|---|
| 1 | `T0` | Свежий эталон сборки и тестов на `fork/master` | — |
| 1 | `T1` | UTF-8 resync и grapheme cluster fixes | — |
| 1 | `T2` | Recoverable new tab на keyboard и Cocoa paths | — |
| 1 | `T3` | Desktop launch login shell без нового chdir | — |
| интеграция | `I1` | Свести T1–T3, сборка и smoke на общей ветке | T1, T2, T3 |
| 1-ревью | `R1-test` | Покрытие, прогоны, чувствительность тестов | I1, T0 |
| 1-ревью | `R1-sec` | Ошибки spawn, ресурсы, shell arguments, секреты | I1 |
| 1-ревью | `R1-qa` | Приёмка критериев, Linux-проверка, документация | I1, T0 |
| 1-fix | `F1` | Исправление только подтверждённых блокеров | R1-* |
| финал | `V1` | Общая верификация, PR, итоговый отчёт | R1-* / F1 |

T0–T3 идут параллельно в изолированных worktree. R1-test/R1-sec/R1-qa идут параллельно после I1; R1-test владеет только тестами и своим отчётом, R1-sec только отчётом, R1-qa документацией и своим отчётом.

## 7. Задачи

### [~] T0. Свежий эталон

- **Волна:** 1
- **Файлы (владеет):** `docs/reports/upstream-stability-sync-T0-baseline-2026-11-19.md`
- **Файлы (только читает):** всё дерево; `CLAUDE.md`, `build.py`
- **Скилл:** `/coding-qa`
- **Ветка:** `qa/T0-upstream-stability-baseline`

На неизменённом `fork/master` выполнить `./build st pt --clear`, `./build unit_tests pty_test_helper`, затем полный `.build/unit_tests` с `SHITTY_PTY_TEST_HELPER` и `< /dev/null`. Запустить Python-набор тем штатным режимом `./build`, который затем будет использоваться для результата; зафиксировать **имена** красных, skipped, число тестов, коды возврата и версии бинарников. Не чинить унаследованные красные.

**Критерии:** отчёт содержит воспроизводимые команды, окружение из `build.py`, SHA, итоговые строки/коды возврата и поимённый baseline для сравнения. `EXIT=148` перезапущен; отсутствие итоговой строки не принято за успех.

### [~] T1. UTF-8 resync и grapheme cluster class

- **Волна:** 1
- **Файлы (владеет):** `lib/vterm/utf8.cpp`, `lib/vterm/utf8_ut.cpp`, `lib/vterm/grapheme.cpp`, `lib/vterm/grapheme_ut.cpp`, `docs/reports/upstream-stability-sync-T1-2026-11-19.md`
- **Файлы (только читает):** upstream `0842b9ed`, `78037448`; соседние `lib/vterm/*`
- **Скилл:** `/coding-writer`
- **Ветка:** `feat/T1-upstream-unicode-fixes`

Перенести `0842b9ed` (bulk UTF-8 decoder повторно входит после каждого resync) и `78037448` (cluster class сохраняется через grapheme boundary). Добавить компактные regression tests на путь, который до правки давал неверный результат; не переносить соседние performance-only коммиты.

**Критерии:** оба upstream-изменения видны в diff; новые тесты краснеют на родительской реализации и зеленеют после; `./build unit_tests pty_test_helper` и ручной `unit_tests` успешны; никаких изменений вне разрешённых файлов.

### [~] T2. Recoverable new tab

- **Волна:** 1
- **Файлы (владеет):** `lib/shitty/pty.cpp`, `lib/shitty/pty_ut.cpp`, `lib/shitty/session.cpp`, `lib/shitty/session_ut.cpp`, `lib/shitty/ui_csd_tabs.mm`, `docs/reports/upstream-stability-sync-T2-2026-11-19.md`
- **Файлы (только читает):** `lib/shitty/pty.h`, `lib/shitty/composer.h`; upstream `b65dfcfb`, `25d98cf9`; реализации `PtyHandle` по всему дереву
- **Скилл:** `/coding-writer`
- **Ветка:** `feat/T2-recoverable-new-tab`

Портировать throw-based spawn errors вместо process exit для отказов открытия/настройки PTY и `fork`; закрывать уже открытые descriptors и восстанавливать signal mask на каждом error path. `CallSessionAction` ловит исключение `newSession()` и печатает warning. Cocoa `tabOpened()` проходит через `composer.newTabListeners`. Перенести и адаптировать upstream unit tests, включая отказ UI route.

**Критерии:** смоделированный отказ new tab не завершает процесс и оставляет существующую SessionSet работоспособной; оба entry points проходят recoverable path; нет descriptor/signal-mask утечки на новых error paths; startup spawn failure вне new-tab не становится молча успешным; unit tests успешны.

### [~] T3. Desktop login shell

- **Волна:** 1
- **Файлы (владеет):** `lib/shitty/application.cpp`, `lib/shitty/composer.h`, `lib/shitty/configuration.cpp`, `lib/shitty/options.cpp`, `lib/shitty/options.h`, `lib/shitty/options_ut.cpp`, `lib/shitty/startup.cpp`, `lib/shitty/startup.h`, `lib/shitty/startup_ut.cpp`, `docs/reports/upstream-stability-sync-T3-2026-11-19.md`
- **Файлы (только читает):** `lib/shitty/composer.cpp`, `tst/test_options.py`, `tst/test_startup.py`; upstream `88d3e3e7`, `519c2c5e`
- **Скилл:** `/coding-writer`
- **Ветка:** `feat/T3-desktop-login-shell`

Перенести `launchedFromDesktop(pid_t)` (macOS: parent == 1; остальные платформы: false), одноразовый capture в `Composer`, передачу значения во все `Options::create` reload paths и условный login default. **Не переносить** `enterHomeWhenLaunchedFromDesktop()` и не добавлять `chdir()`: home directory уже обрабатывает `launchDirectory()`.

Добавить тесты: desktop + HardDefault => login; desktop + явный false => не login; non-desktop + HardDefault => прежнее поведение; повторный parse/reload использует захваченную классификацию. Проверить, что вызовы `Options::create` в production и тестах совместимы без неявной смены поведения.

**Критерии:** все четыре тестовых случая доказаны; `launchDirectory()` не дублируется; явный CLI/config приоритет сохранён; unit tests и релевантные `test_options.py`/`test_startup.py` успешны; оба бренда собираются.

### I1. Интеграция волны

Командир мержит `feat/T1…`, `feat/T2…`, `feat/T3…` в `wave/upstream-stability-sync-w1` с `--no-ff`. Конфликтов по production-файлам быть не должно. После сборки волны командир запускает `./build st pt --clear`, `./build unit_tests pty_test_helper` и ручной unit_tests; красный результат блокирует review и требует F1.

### R1-test. Тестовая проверка

- **Файлы (владеет):** только `*_ut.cpp`, тесты `tst/`, `docs/plans/reviews/upstream-stability-sync-R1-test.md`, `.json`
- **Скилл:** `/coding-test`
- **Ветка:** `test/R1-upstream-stability-sync`

Проверить критерии T1–T3, полноту покрытия изменённых строк и чувствительность новых/существующих тестов мутациями. Назвать наблюдателя поимённо и убедиться, что он зелёный до мутации. Для spawn failures проверить все перенесённые классы ошибок, а не только один удобный. Для desktop tests исключить вырожденную фикстуру, где true/false и desktop/non-desktop неразличимы. Production code не править.

### R1-sec. Security review

- **Файлы (владеет):** `docs/plans/reviews/upstream-stability-sync-R1-sec.md`
- **Скилл:** `/coding-secure`

Проверить command/argv integrity при login shell, отсутствие shell injection и утечек секретов в warnings, корректное закрытие FD и обработку fork errors, отсутствие новых unchecked external inputs. Не требовать отключения существующих проверок и не править код.

### R1-qa. Приёмка и документация

- **Файлы (владеет):** `docs/plans/reviews/upstream-stability-sync-R1-qa.md`; `README.md` и документация опций только если существующее описание стало неверным
- **Скилл:** `/coding-qa`

Проверить каждый критерий T1–T3 на macOS и доступный Linux-стенд `arch-mac` в отдельном worktree, не трогая чужие незакоммиченные изменения. Там снять свой baseline тем же режимом перед результатом либо использовать эквивалентный уже зафиксированный baseline с доказуемой базой. Не считать локальный macOS build доказательством Linux/libstdc++. Сравнить Python-набор с T0 поимённо. Проверить README/описание `login`; никаких скриншотов пользовательского экрана.

### F1. Исправления проверочной волны

Ставится только при красном вердикте и содержит полные подтверждённые находки. `/coding-writer`, ветка `fix/F1-upstream-stability-sync`, файлы ограничены исходной задачей-владельцем. Повторный круг соответствующего проверяющего пишет новый отчёт `…-round2.md`, смотрит diff исправления и закрытие названных находок. Максимум два круга.

## 8. Общая верификация

На итоговой wave-ветке:

```bash
./build st pt --clear
./build unit_tests pty_test_helper
SHITTY_PTY_TEST_HELPER="$PWD/.build/pty_test_helper" .build/unit_tests < /dev/null
./build test   # критерий — не абсолютный зелёный, а поимённое равенство унаследованным красным T0
./build plt_unit_tests && .build/ext/plt/plt_unit_tests < /dev/null
```

Дополнительно: оба бренда и `production_surface` в штатном графе; guards не должны быть сняты со штампов CAS при подозрении на пропуск; Linux shared-core paths проверяются на `arch-mac`; PR CI сравнивается с известными ограничениями проекта.

## 9. Риски и точки остановки

- Ручной порт `spawnError` может изменить startup-fatal semantics: проверить все call sites и обе реализации `PtyHandle`, а не только upstream diff.
- `getppid()` нельзя вызывать на каждом reload: только capture один раз.
- Новый тест, который не краснеет на старой реализации, не закрывает перенос.
- Полный `unit_tests` без итоговой строки не является PASS; `148` перезапускать, `139` разбирать.
- Не объявлять Python-набор улучшившимся/регрессирующим по одному числу: только те же режим и имена.
- Merge/push в `master`, теги и релиз запрещены без отдельного разрешения пользователя. Push только рабочих веток в `fork` разрешён.
- Если для исправления потребуется соседняя подсистема вне скоупа или изменение публичного контракта — остановиться и вынести решение пользователю.

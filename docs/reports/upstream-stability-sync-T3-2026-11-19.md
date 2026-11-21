# T3. Desktop login shell

**Ветка:** `feat/T3-desktop-login-shell` · **Точка ответвления:** `wave/upstream-stability-sync-w1` на `534d1d8cb21bdcc731df5e15b345f29dfac0caf0`
**Коммит реализации:** `04ca234a66a5a8c874a08e94d6bf33e923b97175`
**Источники:** upstream `88d3e3e7`, `519c2c5e`; план `docs/plans/2026-11-19-upstream-stability-sync.md`.

## Что сделано

Перенесена классификация desktop launch для macOS (`getppid() == 1`) и автоматический login shell только для значения `login` из `OptionSource::HardDefault`. Классификация захватывается один раз в `ApplicationImpl::run()` до первого parse, сохраняется в `Composer` и передаётся в каждый startup/reload parse.

`enterHomeWhenLaunchedFromDesktop()` и новый `chdir()` намеренно не перенесены: существующий `launchDirectory()` остаётся единственным механизмом выбора стартового каталога.

## Изменения

| Файл | Что |
|---|---|
| `lib/shitty/application.cpp` | одноразовый capture `launchedFromDesktop(getppid())` до `Config::create()` |
| `lib/shitty/composer.h` | стабильное на reload поле `desktopLaunch` |
| `lib/shitty/configuration.cpp` | передача сохранённой классификации во все `Options::create()` |
| `lib/shitty/options.cpp` | новый аргумент parser и login-default только для `HardDefault` |
| `lib/shitty/options.h` | совместимая сигнатура `Options::create(..., load, desktopLaunch = false)` |
| `lib/shitty/options_ut.cpp` | desktop/default, explicit false/true, ordinary launch и reload tests |
| `lib/shitty/startup.cpp`, `startup.h` | платформенный предикат `launchedFromDesktop(pid_t)` |
| `lib/shitty/startup_ut.cpp` | parent==1 только на macOS; остальные PID и платформы — false |

## Решения

- Значение по умолчанию нового аргумента — `false`, поэтому существующие production/test call sites сохраняют прежнее поведение без механической правки.
- Источник `login` читается через существующий `OptionsParser::get()`: CLI и config оба сильнее автоматики, отдельного пути разбора не добавлено.
- Reload не запрашивает `getppid()` повторно. Это исключает переклассификацию shell launch после reparenting к launchd.

## Проверка

| Проверка | Команда | Код / результат |
|---|---|---|
| Оба бренда, чистая сборка | `./build st pt --clear` | `0`, 255/255 узлов |
| Unit tests и helper | `./build unit_tests pty_test_helper` | `0` |
| Тестовые бинарники для Python | `./build unit_tests pty_test_helper st_test pt_test toml_dump` | `0`, 168/168 узлов |
| Полный unit-набор | `SHITTY_PTY_TEST_HELPER="$PWD/.build/pty_test_helper" .build/unit_tests < /dev/null` | `0`, итог `OK: 1120` |
| Options Python | `python3 tst/test_options.py` с окружением ниже | `0`, `Ran 29`, `OK (skipped=1)` |
| Startup Python | `python3 tst/test_startup.py` с окружением ниже | `0`, `Ran 4`, `OK` |
| Whitespace/conflict markers | `git diff --check` | `0` |

Окружение Python взято из `build.py` (`SHITTY_TEST_FONTCONFIG=0` на этой машине):

```bash
export SHITTY_TEST_BINARY="$PWD/.build/st_test"
export SHITTY_PRETTY_TEST_BINARY="$PWD/.build/pt_test"
export SHITTY_TOML_DUMP_BINARY="$PWD/.build/toml_dump"
export SHITTY_TEST_FONTCONFIG=0
export SHITTY_TEST_PLATFORM=cocoa
export SHITTY_TEST_VERSION="$(date +%Y.%m.%d)"
```

**Чувствительность тестов.** В `options.cpp` временно возвращена старая строка `login = getBool("login");`, затем пересобран и запущен тот же полный unit-набор. Результат мутации: `EXIT=2`, `OK: 1118`, `ERR: 2`; красными стали ровно `Options::DesktopLaunchTurnsTheLoginHardDefaultOn` и `Options::ConfigReloadReusesTheCapturedDesktopLaunchClassification`. Файл восстановлен, сборка восстановления завершилась с `0`, финальный полный прогон снова `OK: 1120`, `EXIT=0`.

**Статическая проверка границ порта:** ровно один production call `launchedFromDesktop(getppid())`; в добавленных строках `chdir` и `enterHomeWhenLaunchedFromDesktop` — ноль.

### Критерии T3

- [x] Desktop + HardDefault => login: `Options::DesktopLaunchTurnsTheLoginHardDefaultOn`.
- [x] Desktop + explicit false => не login: `Options::DesktopLaunchHonoursExplicitLoginFalseFromCliAndConfig` проверяет и `+login`, и `login = false` в TOML.
- [x] Non-desktop + HardDefault => прежнее поведение; explicit true сохраняется: `Options::OrdinaryLaunchKeepsTheLoginDefaultAndHonoursExplicitTrue`.
- [x] Повторный parse/reload использует переданную захваченную классификацию: `Options::ConfigReloadReusesTheCapturedDesktopLaunchClassification`; production-связка — `application.cpp` → `Composer::desktopLaunch` → `configuration.cpp`.
- [x] macOS parent==1, остальные платформы false: `Startup::OnlyLaunchdAsParentMeansADesktopLaunch`.
- [x] Второго `chdir()` нет, `launchDirectory()` не изменён.
- [x] Оба бренда собираются; полный unit и релевантные Python suites зелёные.

## За рамками / ограничения

- Полный `./build test` и поимённое сравнение с T0 здесь не запускались: это общая проверка I1/V1, отчёта T0 в точке ответвления не было. Выполнены только назначенные T3 релевантные Python suites.
- Linux/`arch-mac` не запускался; не-macOS ветка предиката проверена кодом и кроссплатформенным unit-test, но не исполнена на Linux в этой задаче. Это назначено R1-qa.
- Реальный Finder/Dock launch не воспроизводился; поведение проверено на чистом предикате и parser/reload tests, без визуальных снимков.

## Как откатить

`git revert 04ca234a66a5a8c874a08e94d6bf33e923b97175` для реализации и тестов; миграций и сохранённых данных нет.

## Ревьюеру

В первую очередь проверить приоритет `OptionSource` в `OptionsParser::parse()` и отсутствие второго обращения к `getppid()` на reload. Тест reload специально содержит ordinary-launch контроль, чтобы true/false и desktop/non-desktop не были неразличимы.

## Обнаружено

Первая версия reload-фикстуры повторно использовала один массив `argv`. `Options::create()` компактизирует его, поэтому второй parse дал ожидаемый по коду возврата, но не относящийся к продукту `EXIT=139`. Фикстура исправлена на отдельный массив для каждого parse, как это делает `ConfigImpl`; после этого полный набор зелёный.

Плагин ponytail в сессии недоступен; его правила минимализма применялись вручную по `references/ponytail-rules.md`.

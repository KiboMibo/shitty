Находок: 0 блок / 0 важн / 0 зам / 0 сущ.

# R1-sec: upstream stability sync

- **Дата:** 2026-11-19
- **Проверяемый коммит:** `8b163758a053c9b1fca826dddb580d853c5547e3`
- **База diff:** `0ac846d530cd76cada88f2eebf870238658e5879` (`fork/master`)
- **Область:** задачи T1–T3 плана `docs/plans/2026-11-19-upstream-stability-sync.md`; production-diff `0ac846d5..8b163758` и непосредственные call paths в `lib/shitty/**`, `lib/vterm/**`.
- **Вердикт:** security-блокеров нет; с точки зрения этой проверки волну можно принимать без задачи F1.

## Резюме

| Уровень | Количество |
|---|---:|
| Блокирующие | 0 |
| Важные | 0 |
| Замечания | 0 |
| Существовавшие | 0 |

Shell injection, нарушения целостности `argv`, новые утечки секретов или локальных путей в добавленном warning, утечки PTY descriptors на новых error paths и оставленная заблокированной `SIGCHLD` после ошибки `fork()` не подтверждены. Стартовый отказ spawn остаётся видимым вызывающему коду, а назначенные keyboard/Cocoa new-tab routes сохраняют процесс и прежние сессии.

## Находки

Находок нет. Срабатывания сканера секретов относятся к неизменённым тестовым фикстурам и после ручной проверки уязвимостями не являются; см. «Инструменты и верификация».

## Проверенное и чистое

### 1. Login shell: источник решения, `argv` и отсутствие shell injection

Реальный путь прослежен целиком:

`ApplicationImpl::run()` (`lib/shitty/application.cpp:1172`) → сохранённый `Composer::desktopLaunch` → `ConfigImpl::load()` (`lib/shitty/configuration.cpp:74`) → `OptionsParser::get()` / `parse()` (`lib/shitty/options.cpp:997-1035`, `1650-1654`) → `buildLaunchCommand()` (`lib/shitty/startup.cpp:108-145`) → `PtyImpl::spawn()` → `execvp()` (`lib/shitty/pty.cpp:975`).

- `getppid()` вызывается один раз до первого parse; reload получает сохранённое поле, а не новую классификацию.
- `OptionsParser::get()` выбирает CLI раньше config, config раньше hard default. Поэтому условие `loginSource == OptionSource::HardDefault && desktopLaunch` не перекрывает явные `-login`, `+login`, `login = true` или `login = false`.
- Login default меняет только первый байтовый префикс `-` в отдельно хранящемся `argv[0]` (`lib/shitty/startup.cpp:134-142`). Executable остаётся отдельным `command.executableOffset`; аргументы копируются в NUL-terminated storage без объединения в командную строку.
- Явный `-e` возвращает `LaunchCommand` до построения login `argv[0]` (`lib/shitty/startup.cpp:111-118`), сохраняя executable и каждый аргумент отдельно.
- Конечный запуск — `execvp(command.executable(), arguments.mutData())`, без `/bin/sh -c`, `system()` или `popen()`. Shell-метасимволы в пути или аргументах новым интерпретатором не обрабатываются.
- Новых опций, config keys или сетевых входов нет. Новый OS-вход — только `getppid()`; существующий PTY byte stream проходит изменённый Unicode fast path, разобранный ниже.

### 2. PTY/fork errors, descriptors и signal mask

Проверены все новые переходы от `sysError()` к `spawnError()`:

- До открытия master descriptor отсутствует. После `posix_openpt()` каждая ошибка `fcntl(FD_CLOEXEC)`, `grantpt()`, `unlockpt()` или `ptsname()` передаёт master в `spawnError()`, который закрывает его до `raiseError()` (`lib/shitty/pty.cpp:87-94`, `121-137`).
- Ошибка slave `open()` закрывает уже открытый master (`lib/shitty/pty.cpp:143-150`); slave в этой ветке ещё не существует.
- Ошибка `fork()` сохраняет `errno`, закрывает оба descriptors, восстанавливает прежний mask и только затем выбрасывает исключение (`lib/shitty/pty.cpp:934-939`). Родитель после успешного `fork()` восстанавливает mask и закрывает slave; child восстанавливает mask до дальнейшей настройки (`lib/shitty/pty.cpp:941-942`, `978-979`).
- `resizePty()` по-прежнему только предупреждает и не создаёт нового исключения между открытием descriptors и `fork()`.
- `SessionSetImpl::openSession()` удаляет per-session arena на исключении; `newSession()` откатывает посаженный pane и повторно выбрасывает ошибку (`lib/shitty/session.cpp:595-640`). Это закрывает ресурсы выше PTY error path без скрытия ошибки.

Повторяемый отказ по `RLIMIT_NOFILE` исполнен тестом `Pty::RefusedSpawnThrowsAndLeaksNothing`: два раунда завершились успешно, второй не потерял ёмкость из-за оставшегося descriptor. Отдельный fault injection именно для отказа `fork()` не запускался; его порядок close/restore/throw проверен чтением кода.

### 3. Исключения и process-exit semantics

- `SessionSet::create()` регистрирует `newTabAction` и затем вызывает первый `newSession()` вне recoverable catch (`lib/shitty/session.cpp:1529-1556`). Ошибка первого spawn не превращается в успешный startup.
- Keyboard path ловит `Exception&` только вокруг `parent->newSession()` и печатает warning (`lib/shitty/session.cpp:1751-1757`).
- `CsdTabsUi::tabOpened()` вызывает `composer.newTabListeners` (`lib/shitty/ui_csd_tabs.mm:759-769`), то есть тот же зарегистрированный action, а не обходит его прямым spawn.
- `SessionSet::RefusedNewTabPreservesExistingSessionsAndCanRetry` подтвердил сохранение двух прежних сессий, три recoverable отказа и успешный retry. `SessionSet::RefusedInitialSpawnPropagatesAndReleasesItsArena` подтвердил проброс стартовой ошибки и освобождение arena.
- Прямые sidebar/Wayland/palette calls (`ui_sidebar_tabs.mm:1960,3003`, `ui_wayland_chrome.cpp:876,1278`, `palette_session.cpp:364`) план явно оставляет вне recoverable routes этой волны. Они не выдаются здесь за новые находки: файлы не менялись, а до волны эти spawn failures также завершали процесс через `sysError()`.

### 4. Warnings, секреты и пути

- Новый parent warning состоит из фиксированного префикса и `Exception::description()` (`lib/shitty/session.cpp:1755`). Все новые `spawnError()` descriptions содержат фиксированное имя системной операции, `strerror(error)` и числовой `errno`; shell, `argv`, environment, config values и directory туда не передаются (`lib/shitty/pty.cpp:87-94`, `121-150`, `934-939`).
- Существующее child-сообщение о неудачном `chdir()` по-прежнему печатает выбранный каталог в PTY (`lib/shitty/pty.cpp:965-969`), но этот код не изменён волной и не является новым warning parent process.
- Gitleaks по рабочему дереву дал четыре срабатывания: два localhost test key/certificate в `ext/libstd/tst/http_serve_{files,ok}.cpp` и два generic-api-key в vendored `tst/contour/vttest/keyboard.c`. Все эти файлы отсутствуют в diff волны; ключи явно названы тестовыми и используются локальными test servers. Это не production credentials и не новые секреты.
- Ручной grep типовых имён секретов и URL с credentials не выявил нового значения, требующего находки или ротации. Полный аудит git-истории не выполнялся.

### 5. Новые Unicode transitions и границы буферов

- Новый re-entry в `decodeRun()` находится внутри цикла с инвариантом `text.count < Utf8Text::capacity - 1` (`lib/vterm/utf8.cpp:108`). Capacity следующего run равна `capacity - 1 - text.count`, а `decodeRun()` пишет не больше переданного capacity (`lib/vterm/utf8.cpp:66-79`, `137`). После run `text.count <= 63` при массиве на 64 codepoints; underflow capacity и выход за массив из этого пути не следуют.
- `decodeRun()` получает остаток входа как `input + consumed` / `length - consumed`; предварительная проверка continuation byte ограничена `consumed + 1 < length`. Малformed/truncated suffix остаётся существующему побайтовому recovery, decoder tail и `pendingTrace` не сбрасываются новым путём.
- `VtermImpl::placeUtf8Run()` повторяет decode, пока `text.full`, и останавливается на нулевом прогрессе (`lib/vterm/vterm.cpp:4790-4814`), поэтому заполнение блока не приводит к потере остатка или бесконечному циклу.
- Grapheme fast path заменяет сохранённое состояние свойствами текущего codepoint только после boundary (`lib/vterm/grapheme.cpp:79-89`); новых внешних индексов, размеров или allocations нет.
- `Utf8::TextBlockResyncsIntoMultibyteRuns` и `Grapheme::KeepsIndependentClustersIndependent` прошли в полном наборе.

## Инструменты и верификация

| Проверка | Результат |
|---|---|
| `bash /Users/kibomibo/.agents/skills/coding-secure/scripts/scan.sh "$PWD"` | Gitleaks и secret-oriented grep отработали; 4 срабатывания разобраны выше. Osv-scanner дал пустой вывод; semgrep не отработал; trivy не установлен. |
| Повторный `semgrep --config=p/security-audit --metrics=off` по девяти изменённым production-файлам | Не отработал: окружение Python падает с `ImportError: cannot import name '_ExtendedAttributes'` до загрузки правил. Это не засчитано как чистый скан. |
| `./build unit_tests pty_test_helper` | `RC=0`, 309/309 узлов. |
| `SHITTY_PTY_TEST_HELPER="$PWD/.build/pty_test_helper" .build/unit_tests < /dev/null` | `RC=0`, итог `OK: 1125`; итоговая строка присутствует. |
| Поимённая проверка 12 security-relevant tests | Все 12 найдены со знаком `+`: UTF-8/grapheme, refused spawn/new tab/startup, четыре desktop-login cases, desktop predicate, `-e` integrity и login `argv[0]`. |
| `git diff --check 0ac846d5..HEAD` | `RC=0`. |

## План устранения / F1

Блокирующих находок нет. Задач F1 по результатам R1-sec не требуется.

## Не проверено / границы вердикта

- Semgrep не выполнил статический анализ из-за сломанной Python-зависимости; вывод выше основан на ручном анализе и unit tests, а не на semgrep.
- Osv-scanner не дал результата; полного CVE-аудита зависимостей не было. В проверяемом diff нет manifest/lockfile или dependency changes.
- Trivy недоступен. Инфраструктура, CI, Docker и release signing волной не изменялись и полноценно не аудировались.
- Полный git-history secret scan не выполнялся; gitleaks проверял рабочее дерево, как предписано для режима diff.
- Реальный Finder/Dock launch, Linux runtime и отдельный принудительный отказ `fork()` не воспроизводились. Desktop predicate и resource cleanup проверены кодом и доступными tests; кроссплатформенная приёмка относится к R1-qa.
- Fuzzing и sanitizer-прогон изменённого Unicode decoder в этой задаче не выполнялись.

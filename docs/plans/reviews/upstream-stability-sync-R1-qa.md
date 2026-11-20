Находок: 0 блок / 2 важн / 0 зам / 1 сущ

# R1-qa: приёмка upstream-stability-sync

**Дата:** 2026-11-19
**План:** `docs/plans/2026-11-19-upstream-stability-sync.md`, задачи T0–T3 и R1-qa
**Проверяемый код:** `8b163758a053c9b1fca826dddb580d853c5547e3` (`8b163758`)
**Ветка QA:** `qa/R1-upstream-stability-sync` от `wave/upstream-stability-sync-w1`
**Вердикт:** **готово с замечаниями**. На macOS подтверждены поведение и тесты T1–T3; новых product-red в Python-наборе нет. До merge в `master` остаются обязательный зелёный Linux/libstdc++ PR CI gate и уточнение встроенного `-help` для контекстного `login` default.

## 1. Резюме

Чистая сборка обоих брендов, сборка unit tests и полный ручной прогон успешны: `OK: 1125`, rc `0`. Все десять новых наблюдателей T1–T3 присутствуют и зелёные. Python-набор запущен тем же режимом, что T0; все 40 shards завершились. Поимённое сравнение показало 13 общих унаследованных отказов и изменения только среди ранее зафиксированных environment/f flaky-классов, без нового отказа изменённой функциональности.

`arch-mac` отклонил SSH-аутентификацию с rc `255`, поэтому Linux здесь не исполнен и не объявляется проверенным. Production code и тесты не изменялись. Исправлены только README и описание `login` в обоих примерных конфигах; встроенный production help оставлен владельцу T3 как находка QA-1.

## 2. Команды и коды возврата

Все локальные команды выполнены на macOS 27.2.1 arm64, Apple clang 21.0.0, Python 3.12.5, 2026-11-19 одним заходом с версией binaries `2026.11.19`.

| Проверка | Команда | rc / результат |
|---|---|---|
| Оба production brand | `./build st pt --clear` | `0`; 255/255 nodes |
| Unit binary и helper | `./build unit_tests pty_test_helper` | `0`; 162/162 nodes |
| Полный unit suite | `SHITTY_PTY_TEST_HELPER="$PWD/.build/pty_test_helper" .build/unit_tests < /dev/null` | `0`; итоговая строка `OK: 1125`; `148`/`139` не было |
| Python default + prod-parser, режим T0 | `./build -k test_suite test_suite_prod_parser` | `1`; все 40 shards получили итог, 13292 invocations; разбор по именам ниже |
| Production surface без host config | `HOME=<новый пустой каталог> SHITTY_PRODUCTION_BINARY="$PWD/.build/st" SHITTY_PRETTY_BINARY="$PWD/.build/pt" SHITTY_TEST_VERSION=2026.11.19 python3 tst/production_surface.py` | `0`; `Ran 6 tests`, `OK` |
| Platform tests | `./build plt_unit_tests && .build/ext/plt/plt_unit_tests < /dev/null` | build `0`; tests `0`, `OK: 54` |
| Релевантные T3 Python suites | `PYTHONPATH="$PWD/tst" python3 -m unittest -v test_options test_startup` с env из `build.py` | `0`; `Ran 33 tests`, `OK (skipped=1)` |
| Контроль nested-build environment | `PYTHONPATH="$PWD/tst" python3 -m unittest -v test_build_metadata.BuildMetadataTests.test_groups_are_additive_cli_aliases test_build_metadata.BuildMetadataTests.test_build_rejects_every_other_descr_shape` | `0`; `Ran 2 tests`, `OK` |
| Сборка после документации | `./build st pt st_test pt_test toml_dump example` | `0`; 10/10 nodes, оба embedded config пересобраны |
| Проверка обоих примерных конфигов | `PYTHONPATH="$PWD/tst" python3 -m unittest -v test_config` с env из `build.py` | `0`; `Ran 34 tests`, `OK` |
| Повтор production surface после документации | та же команда с новым пустым `HOME` | `0`; `Ran 6 tests`, `OK` |
| Diff hygiene | `git diff --check` | `0` |
| Linux-стенд | `ssh -o BatchMode=yes -o ConnectTimeout=10 arch-mac …` | `255`; `Permission denied (publickey,password)`, тесты не начались |

Для `test_config`, `test_options` и `test_startup` выставлены `SHITTY_TEST_BINARY`, `SHITTY_PRETTY_TEST_BINARY`, `SHITTY_EMBED_EXAMPLE_BINARY`, `SHITTY_TOML_DUMP_BINARY`, `SHITTY_TEST_FONTCONFIG=0`, `SHITTY_TEST_PLATFORM=cocoa`, `SHITTY_TEST_VERSION=2026.11.19`. Скриншоты не делались.

Повторная проверка при приёмке отчёта 2026-11-20 на том же коммите `8b163758`: полный `.build/unit_tests` с `SHITTY_PTY_TEST_HELPER` и `< /dev/null` — rc `0`, итог `OK: 1125`, все десять наблюдателей T1–T3 присутствуют в выводе; `ssh -o BatchMode=yes -o ConnectTimeout=10 arch-mac 'true'` — rc `255`, `Permission denied (publickey,password)`, тесты на стенде не начинались; `git diff --check` — rc `0`. Version binaries при повторе не пересобирались: коммит и дерево исходников те же.

## 3. Поимённые критерии T1–T3

### T1. UTF-8 resync и grapheme fast paths — подтверждено на macOS

- Изменённые строки `lib/vterm/utf8.cpp` точно совпадают с upstream `0842b9ed`; `lib/vterm/grapheme.cpp` — с `78037448`. Сравнение changed-lines дало `True` для обоих файлов.
- `Utf8::TextBlockResyncsIntoMultibyteRuns` и `Grapheme::KeepsIndependentClustersIndependent` зелёные в полном `OK: 1125`; assertions покрывают codepoints, reset mask, consumption и boundaries.
- Зафиксированные в T1 mutation checks прочитаны и относятся к этим наблюдателям: удаление reset propagation и подмена `stateClass` дали поимённые красные тесты с rc `1`. R1-qa не повторял production-мутации, соблюдая запрет правки production code.
- Изменения T1 ограничены четырьмя разрешёнными source/test files и отчётом; соседние performance-only изменения отсутствуют.
- Сравнительный benchmark есть в отчёте T1; обязательного performance threshold у задачи нет, отдельный вывод о производительности здесь не делается.

### T2. Recoverable new tab — подтверждено на macOS, Linux не исполнен

- `Pty::RefusedSpawnThrowsAndLeaksNothing`, `SessionSet::RefusedNewTabPreservesExistingSessionsAndCanRetry`, `SessionSet::RefusedInitialSpawnPropagatesAndReleasesItsArena` зелёные в полном наборе.
- Keyboard path ловит `Exception` в `CallSessionAction::onListen()`; `CsdTabsUi::tabOpened()` публикует `composer.newTabListeners`, то есть использует тот же recoverable action. Оба бренда, включая Cocoa path, собраны.
- Прочитаны все новые PTY error paths: `posix_openpt`, `fcntl`, `grantpt`, `unlockpt`, `ptsname`, slave `open`, `fork`. Удерживаемые descriptors закрываются; на fork error оба descriptors закрыты и прежняя signal mask восстановлена до `spawnError`.
- Первый spawn не перехватывается recoverable handler: startup-fatal наблюдатель подтверждает исходную ошибку и освобождение arena.
- Прямые routes в `ui_sidebar_tabs.mm`, `ui_wayland_chrome.cpp` и `palette_session.cpp` остаются ровно в перечисленном планом вне-scope списке; скрытого расширения порта нет.

### T3. Desktop login shell — подтверждено на macOS, non-macOS branch не исполнен

- Зелёные: `Options::DesktopLaunchTurnsTheLoginHardDefaultOn`, `Options::DesktopLaunchHonoursExplicitLoginFalseFromCliAndConfig`, `Options::OrdinaryLaunchKeepsTheLoginDefaultAndHonoursExplicitTrue`, `Options::ConfigReloadReusesTheCapturedDesktopLaunchClassification`, `Startup::OnlyLaunchdAsParentMeansADesktopLaunch`.
- `launchedFromDesktop(getppid())` вызывается один раз в `ApplicationImpl::run()` до `Config::create()`/первого parse. Reload получает `composer.desktopLaunch`; повторного `getppid()` в parser нет.
- `OptionSource::HardDefault` отделяет автоматику от явных CLI/config значений; `+login` и `login = false` выигрывают.
- Новых строк `chdir()`/`enterHomeWhenLaunchedFromDesktop()` в diff нет. Существующий `launchDirectory()` в `lib/shitty/startup.cpp:143` не изменён и остаётся единственным механизмом выбора стартового каталога.
- `test_options` + `test_startup`: 33 tests, rc `0`, один ожидаемый platform skip.

## 4. Python: сравнение с T0 по именам

Режим совпадает с T0: `./build -k test_suite test_suite_prod_parser`. Числа приведены только как контроль полноты, вердикт основан на test IDs и параметрах subTest.

### 4.1. Общие с T0 унаследованные красные — 13 уникальных ID

1. `test_font_file_path_is_not_treated_as_a_family (test_font_resolver.FontResolverTest.test_font_file_path_is_not_treated_as_a_family)`
2. `test_strike_metrics_shape_the_cell … (font='spleen-8x16.bdf')`
3. `test_strike_metrics_shape_the_cell … (font='cozette.bdf')`
4. `test_descenders_hang_below_the_strike_baseline … (font='spleen-8x16.bdf')`
5. `test_descenders_hang_below_the_strike_baseline … (font='cozette.bdf')`
6. `test_repeated_glyph_hits_the_strike_cache_bit_for_bit (test_bitmap_font_render.BitmapFontRenderTest.test_repeated_glyph_hits_the_strike_cache_bit_for_bit)`
7. `test_color_zwj_grapheme_renders_to_image (test_color_font_render.ColorFontRenderTest.test_color_zwj_grapheme_renders_to_image)`
8. `test_mismatched_size_still_uses_the_only_strike (test_bitmap_font_render.BitmapFontRenderTest.test_mismatched_size_still_uses_the_only_strike)`
9. `test_media_symbols_have_their_shapes (test_synthesized_symbols.SynthesizedSymbolTest.test_media_symbols_have_their_shapes)`
10. `test_box_drawing_spans_the_whole_cell_at_the_font_stem_width … (font='spleen-8x16.bdf')`
11. `test_box_drawing_spans_the_whole_cell_at_the_font_stem_width … (font='cozette.bdf')`
12. `test_matching_size_draws_the_strike_bit_for_bit (test_bitmap_font_render.BitmapFontRenderTest.test_matching_size_draws_the_strike_bit_for_bit)`
13. `test_dentistry_brackets_hug_the_cell_edges (test_synthesized_symbols.SynthesizedSymbolTest.test_dentistry_brackets_hug_the_cell_edges)`

Это те же font/bitmap/symbol environment failures, что в T0; новых Unicode/new-tab/login ID среди них нет.

### 4.2. Текущий-only ID

- `FAIL test_groups_are_additive_cli_aliases (test_build_metadata.BuildMetadataTests.test_groups_are_additive_cli_aliases)` — вложенный `build` упал в `supervised_main()` на `os.setpgid(0, 0)` с `PermissionError: [Errno 1] Operation not permitted`.
- `build`, `build.py` и `tst/test_build_metadata.py` не изменены волной. Прямой запуск этого теста вне parent build runner дал `OK`; это environment instance существующего класса QA-E1, не product regression.

### 4.3. Baseline-only IDs, теперь зелёные в полном прогоне

- `test_build_rejects_every_other_descr_shape … (descr='ABC')` — тот же nested-build `setpgid` класс в T0; сейчас `ok` в обоих режимах и в прямом контрольном запуске.
- `test_pretty_child_gets_pretty_version (test_startup.StartupTest.test_pretty_child_gets_pretty_version)` — `ok` в обоих режимах и в отдельном `test_startup`.
- `test_child_signal_exit_uses_shell_status_convention (test_pty.PtyTest.test_child_signal_exit_uses_shell_status_convention)` — `ok` в обоих режимах.

Итого: списки **не побайтово равны**, но единственный current-only ID воспроизводимо относится к уже зафиксированному T0 environment классу и зелёный вне него. Роста красных product IDs относительно T0 не обнаружено. Skipped instances — 58, как в T0; unexpected successes — 0.

## 5. Находки

### Важные

#### QA-1. Встроенный `-help` называет `false` общим default для `login`

**Где:** `lib/shitty/options.cpp:110`, generic suffix на `lib/shitty/options.cpp:1800`; фактическая desktop корректировка — `lib/shitty/options.cpp:1651-1654`.

**Что не так:** чистый `HOME` + `.build/st -help` печатает `-login Start shell as a login shell (default: false)`. Для macOS desktop launch при `OptionSource::HardDefault` эффективное значение теперь `true`.

**Чем грозит:** встроенная документация противоречит новому поведению именно на пути, ради которого сделан T3; пользователь может неверно понять необходимость `-login`/`+login`.

**Предлагается:** владельцу T3 уточнить `helpDescr`, явно назвав `false` hard default ordinary shell launch и описав desktop default/явный override. QA не менял production `options.cpp`.

**Экземпляры:** проверены README, оба `bin/*/*.toml` и docs по `login`; внешние описания исправлены этим отчётом. Других устаревших описаний `login` не найдено.

#### QA-2. Linux/libstdc++ критерий остаётся неисполненным локально

**Где:** критерий R1-qa плана; shared paths T1/T2 и non-macOS предикат `lib/shitty/startup.cpp:186-193`.

**Что не так:** `arch-mac` отклонил подписанную SSH-аутентификацию, rc `255`. Отдельный worktree не создан, baseline/result и Linux build не запускались. Safety net и SSH keys/config не обходились.

**Чем грозит:** macOS clang/libc++ не доказывает Linux/libstdc++ компиляцию и исполнение shared-core/non-macOS веток.

**Предлагается:** согласованный с командиром обязательный gate перед merge — зелёный PR CI для Linux/libstdc++ и релевантных shared-core тестов на проверяемом коде. Если CI недоступен или красный по новым причинам, PR не считать готовым к merge. Это ограничение проверки, а не красный тест продукта.

### Существовавшие

#### QA-E1. Nested-build tests нестабильны под sandbox/process-group ограничением (исходно важная)

**Где:** `build:2633`, `os.setpgid(0, 0)` в `supervised_main()`.

**Что не так / доказательство:** T0 поймал EPERM на `test_build_rejects_every_other_descr_shape`, текущий прогон — на `test_groups_are_additive_cli_aliases`. Вне parent build runner оба теста прошли `2/2`, rc `0`. Файлы runner/tests волной не тронуты.

**Чем грозит:** меняющийся test ID создаёт ложный «новый красный» при обязательном поимённом сравнении и тратит QA-циклы.

**Предлагается:** отдельной задачей тестового runner устранить зависимость nested `--list` probes от запрещённого `setpgid`, не ослабляя assertions тестов.

**Экземпляры:** просмотрены все 14 текущих и 16 baseline red IDs; других EPERM instances, кроме названной пары между прогонами, нет. Не влияет на вердикт волны.

## 6. Документация

Обновлено:

- `README.md` — `false` для `login` назван hard default ordinary shell launch; описаны desktop default и явные `+login`/`login = false`;
- `bin/st/shitty.toml` и `bin/pt/pretty.toml` — одинаково уточнён `# CLI: -login / +login`, отдельно назван приоритет явного `false` в shipped config.

README про `launchDirectory()` остаётся верным: унаследованный `/` переводится в home существующим механизмом, второго `chdir()` не появилось. После правки `test_config` дал 34/34 `OK`, включая parity и побайтовый `-printConfig`; production surface повторно 6/6 `OK`.

## 7. Ограничения и рекомендуемый следующий шаг

- Linux/libstdc++, Vulkan и реальный Finder/Dock launch локально не проверялись; по ним PASS не заявляется.
- Production-мутации не повторялись R1-qa; использованы зафиксированные адресные mutation results T1–T3 и независимый полный прогон наблюдателей.
- Скриншоты не создавались. Production code и тесты не менялись, новых тестов не добавлено.

Следующий шаг: закрыть текст built-in help QA-1, опубликовать итоговую wave-ветку в fork и дождаться зелёного Linux/libstdc++ PR CI как компенсирующего gate QA-2. После этого V1 может принимать решение о merge; локальный QA-отчёт сам по себе Linux PASS не заменяет.

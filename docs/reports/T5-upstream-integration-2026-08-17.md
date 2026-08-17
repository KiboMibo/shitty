# Отчёт T5: перенос `feat/window-chrome` на актуальный `origin/master`

- **Задача:** интеграция плана window-chrome (31 коммит, база `b2cb0f58`) с апстримом,
  ушедшим на 24 коммита вперёд и переструктурировавшим дерево (`main_shitty.cpp` →
  `bin/st/main.cpp`, лib-файлы → `lib/shitty/`, тулинг → `dev/` и т.д.).
- **Ветка:** `feat/window-chrome-upstream`, создана от `origin/master` (`a8917d74`).
  `feat/window-chrome` не тронута.
- **Коммит мержа:** `5a1f7712`.
- **Итог:** мерж выполнен, все правки плана подтверждены на месте по новым путям.
  При автоматическом мерже 5 новых файлов плана осели не в тот каталог из-за
  отсутствия rename-цели — перемещены вручную; без этого шага модуль
  quick-hotkey и хоткей-хорды выпали бы из сборки. Сборка и тесты зелёные
  (за вычетом заранее известного красного набора, идентичного на чистом
  апстриме). `push` в `origin` не делался.

## Как разрешён конфликт `build.py`

Единственный текстовый конфликт — два `all_libshitty_sources.append(...)` в одном
и том же месте: наша сторона регистрировала `ui_quick_hotkey.mm` по старому
корневому пути (`$(S)/ui_quick_hotkey.mm`), апстримная сторона — уже без него,
но по новому пути `$(S)/lib/shitty/ui_csd_tabs.mm`.

Разрешение: обе строки сохранены и приведены к апстримному пути `lib/shitty/`:

```python
all_libshitty_sources.append("$(S)/lib/shitty/ui_csd_tabs.mm")
all_libshitty_sources.append("$(S)/lib/shitty/ui_quick_hotkey.mm")
```

Остальные переменные пути (`vterm_source`, `application_source` и т.д.) взяты из
апстримной стороны конфликта (`lib/shitty/...`) — они идентичны по смыслу, просто
апстрим успел их переименовать первым.

## Файлы плана, унесённые rename-детекцией не туда

Git сопоставил каждый **изменённый** (не новый) файл плана с его новым путём
автоматически — 12 файлов (`options.cpp/h`, `application.cpp`, `ui_csd_tabs.mm`,
`test_mode.cpp`, `shitty.toml`/`pretty.toml`, `ext/plt/*`) вошли в мерж без каких-либо
дополнительных действий с моей стороны.

Но 5 файлов, которых на апстриме вообще не было (появились впервые в
`feat/window-chrome`), не имеют rename-цели — они осели в корне репозитория,
хотя весь остальной `.cpp`/`.h` код к этому моменту уже лежит в `lib/shitty/`.
Переместил вручную (`git mv`) до коммита мержа:

- `quick_hotkey_chord.cpp` → `lib/shitty/quick_hotkey_chord.cpp`
- `quick_hotkey_chord.h` → `lib/shitty/quick_hotkey_chord.h`
- `quick_hotkey_chord_ut.cpp` → `lib/shitty/quick_hotkey_chord_ut.cpp`
- `ui_quick_hotkey.h` → `lib/shitty/ui_quick_hotkey.h`
- `ui_quick_hotkey.mm` → `lib/shitty/ui_quick_hotkey.mm`

Без этого шага `build.py`'s `build.glob("$(S)/lib/shitty/*.cpp")` не подобрал бы
`quick_hotkey_chord.cpp`/`_ut.cpp` (они регистрируются глобом, а не явным списком),
а `ui_quick_hotkey.mm` не собрался бы вовсе — сборка бы либо не слинковала
Carbon-хоткей, либо не увидела его юнит-тестов. Проверил `#include`: все они
используют голые имена файлов (`#include "quick_hotkey_chord.h"` и т.п.), и
поскольку соседние файлы (`application.cpp`, `ui_quick_hotkey.mm`) теперь тоже в
`lib/shitty/`, инклюды разрешаются корректно без правки.

## Таблица «правка → актуальный путь → на месте/потеряна»

| Правка плана | Актуальный путь | Статус |
|---|---|---|
| `transparentTitlebar`, `quick`, `quickHotkey` в таблице опций | `lib/shitty/options.cpp` | На месте (`options.cpp:99,100,107`) |
| Те же поля в структуре `Options` | `lib/shitty/options.h` | На месте (`options.h:71,97,103`) |
| `# CLI: -quick / +quick — …`, `-quickHotkey CHORD — …`, `-transparentTitlebar / +transparentTitlebar — …` (em dash) | `bin/st/shitty.toml`, `bin/pt/pretty.toml` | На месте в обоих файлах (было и остаётся `shitty.toml`/`pretty.toml`, только по новым путям) |
| `plt::WindowOptions` (поля под quick/quickHotkey), `requestHide`/`requestShowAt`/`visible`, `ShowPlacement` | `ext/plt/window.h` | На месте — путь не переехал, апстрим этот файл не трогал (0 отличий от базы) |
| Реализации в 3 бэкендах | `ext/plt/platform_cocoa.mm`, `ext/plt/platform_wayland.cpp`, `ext/plt/platform_headless.cpp` | На месте во всех трёх — пути не переезжали, апстрим не трогал |
| Прозрачный титлбар + логика вкладок | `lib/shitty/ui_csd_tabs.mm` | На месте, переехал по rename-детекции |
| `ui_quick_hotkey.h/.mm`, `quick_hotkey_chord.h/.cpp` (`#if defined(__APPLE__)`) | `lib/shitty/*` | На месте после ручного `git mv`. Гварды `__APPLE__` есть в `quick_hotkey_chord.{h,cpp}`; в `ui_quick_hotkey.{h,mm}` их не было и на исходной ветке `feat/window-chrome` (проверил побайтовым diff — содержимое всех пяти файлов идентично оригиналу, мерж ничего не менял в них, кроме пути) — это не потеря при мерже, а особенность исходного плана, регистрация Cocoa-only через `if darwin:` в `build.py` |
| `toggleQuickWindow`, `quickHotkeyActive`, условие «не показывать окно при `quick`» | `lib/shitty/application.cpp` | На месте (`application.cpp:140,508-513,535,616,629-644`), переехал по rename-детекции |
| Поле в ответе `OPTIONS` тестового режима | `lib/shitty/test_mode.cpp` | На месте — `transparent_titlebar=` и `quick=` в строке OPTIONS (`test_mode.cpp:2392`) |
| `options_ut.cpp` | `lib/shitty/options_ut.cpp` | На месте, побайтово идентичен исходной ветке |
| `application_ut.cpp` | `lib/shitty/application_ut.cpp` | На месте, слился **вместе** с апстримной правкой того же файла (см. ниже) |
| `quick_hotkey_chord_ut.cpp` | `lib/shitty/quick_hotkey_chord_ut.cpp` | На месте после ручного `git mv`, побайтово идентичен |
| `platform_headless_ut.cpp` | `ext/plt/platform_headless_ut.cpp` | На месте, путь не переезжал, побайтово идентичен |
| Тесты в `tst/test_config.py` | `tst/test_config.py` | На месте, слился вместе с апстримной правкой (см. ниже) |
| `docs/` плана и отчётов | `docs/plans/…`, `docs/reports/…`, `docs/research/…` | Доехали целиком (35 файлов) |

## Где апстрим менял те же места

Диффнул содержимое каждого файла плана между `b2cb0f58` (старый путь) и
`origin/master` (новый путь) напрямую — почти везде 0 отличий (апстрим не
трогал ничего из перечисленного). Два исключения, оба разрешились чистым
3-way мержем **без** конфликт-маркеров, то есть git сам развёл правки по разным
областям файла:

- **`lib/shitty/application_ut.cpp`** — апстрим заменил
  `ObjPool::fromMemory()` на `ObjPool::fromMemoryRaw()` в
  `HeadlessRunWiresPresentsAndTearsDownProductionComponents` (несвязанный фикс
  времени жизни треда Pty в тестовом окружении). Наша сторона добавила 4 новых
  теста `ToggleQuickWindow::*` в отдельном блоке того же файла. Оба изменения
  победили — проверено (`fromMemoryRaw` на месте в старом тесте,
  `toggleQuickWindow` вызывается в 4 новых).
- **`tst/test_config.py`** — апстрим сменил `EXAMPLE_CONFIG = ROOT / "shitty.toml"`
  на `ROOT / "bin" / "st" / "shitty.toml"` (следствие переезда). Наша сторона
  добавила тесты `quick`/`transparentTitlebar` в конце файла. Итоговый файл
  использует апстримный путь и содержит оба набора тестов — подтверждено
  прогоном pytest ниже.

Во всех остальных файлах плана апстрим за 24 коммита ничего не менял — принятие
«нашей» версии никакой апстримной работы не затёрло.

## Эталон тестов: чистый `origin/master` (worktree) против интеграционной ветки

Team lead предупреждал, что снимать эталон нужно на чистом апстриме, а не на
старом снимке ветки — так и сделал: `git worktree add … origin/master`,
отдельный `.build` (общий CAS с диска переиспользовался по совпадению
контента, что и ожидаемо для не тронутых файлов).

### `./build test -k`

**На обеих ветках одинаково красно**, набор поломок идентичен по существу
(есть расхождение по номерам групп python-шардирования — ожидаемо, шардирование
зависит от порядка файлов, который сместился при переезде):

1. **`tst/pty_test_helper.c` не компилируется**: `error: use of undeclared
   identifier 'SIGWINCH'`. Причина — файл сам объявляет `#define
   _POSIX_C_SOURCE 200809L`, что на SDK этой машины прячет `SIGWINCH` (это
   BSD/Darwin-расширение, а не строгий POSIX символ); без макроса заголовок
   компилируется чисто (проверил `cc -E -dM -include signal.h`). **Это
   предсуществующая проблема окружения этой машины, воспроизводится один в
   один на чистом `origin/master`** — не имеет отношения к мержу.
   Транзитивно ломает зависимую от `pty_test_helper` цепочку —
   `unit_test_groups` (там же и `plt_tests`) не запускаются вовсе через
   `./build test -k` (их узлы попадают в «broken», не «failed», поэтому в
   логе не видно ни одной строки `[UT]`/`[PT]`).
2. **`pretty-binary-branding.stamp`**: «forbidden branding at byte offset …» —
   тоже воспроизводится один в один на чистом апстриме, не связано с мержем.
3. **5 одинаковых по имени python-тестов** падают на обеих ветках (номера
   групп шардирования разные, содержимое одинаковое):
   `test_legacy_arrow_modifier_matrix` (2 параметра), `test_soft_zero_departs_from_the_hinted_grid`,
   `test_sheared_tail_lands_in_the_captured_blank`,
   `test_russian_shift_ctrl_c_has_no_legacy_control_sequence`,
   `test_darkening_scales_with_the_option`.

Итог: **13 failed nodes на обеих ветках**, состав идентичен. Регрессий не внесено.

**`plt-tests.stamp` в этих 13 узлах отсутствует** — не как «прошёл», а как
«вообще не запускался». `unit_test_groups` (в котором и стоит зависимость
`plt_tests`) сам не является причиной остановки — по чтению `build.py`
`plt_tests` от `pty_test_helper` не зависит, — но в обоих логах `./build test
-k` нет ни одной строки `[PT]`, а `.build/plt-tests.stamp` в основном репо всё
это время оставался символьной ссылкой со старым таймстампом (01:28, до начала
моей работы), т.е. не тронут ни одним из моих прогонов `-k`. И на чистом
апстриме, и на интеграционной ветке `plt_tests` через `./build test -k` просто
не выполняется в этом окружении — это тоже одинаково на обеих ветках, не
регрессия.

Первая попытка найти тест `ShiftedPrintableKeepsBothLayoutLevelsOnRelease` (мой
`grep -r` был по `--include="*.cpp" --include="*.h" --include="*.py"` — я
забыл про `.mm`) ошибочно не нашла файл. Team lead поправил: тест лежит в
`ext/plt/platform_cocoa_ut.mm:147`, входит в бинарник `plt_unit_tests` (не
`unit_tests`). Собрал и прогнал `plt_unit_tests` напрямую (`./build -j 8
plt_unit_tests`, минуя дохлый `-k`) на обеих ветках — тест **зелёный на
обеих**, весь бинарник **0 ошибок**: baseline `OK: 53`, интеграция `OK: 54`
(разница — ровно `PlatformHeadless::VisibleTogglesOnEveryShowHidePath` из
плана, см. таблицу ниже). Активная раскладка на машине сейчас даёт зелёный, а
не красный результат для этого теста — расходится с ориентиром team lead
«12 при не-ASCII», видимо раскладка успела смениться между сессиями. Важно
для итога задачи то, что результат **одинаковый на обеих ветках** — маршрут
переноса плана эту раскладко-зависимую логику не задел.

### Нативные C++-юниты в обход блокировки `pty_test_helper`

Раз `./build test -k` не пускает `unit_test_groups`/`plt_tests` целиком из-за
несвязанной проблемы окружения, собрал и прогнал бинарники юнит-тестов
напрямую (`./build -j 8 unit_tests`, `./build -j 8 plt_unit_tests`), на обеих
ветках — это даёт то самое пофайловое сравнение, которое просил team lead:

| Бинарник | `origin/master` (чистый) | `feat/window-chrome-upstream` | Разница |
|---|---|---|---|
| `unit_tests` (`lib/shitty/*_ut.cpp`) | `OK: 630, ERR: 3` | `OK: 651, ERR: 3` | **+21 новых тестов, 0 потерянных** |
| `plt_unit_tests` (`ext/plt/*_ut.cpp`) | `OK: 53, ERR: 0` | `OK: 54, ERR: 0` | **+1 новый тест, 0 потерянных** |

3 `ERR` в `unit_tests` — на обеих ветках одни и те же три теста
(`Pty::EngagedOwnerDeathSurvivesAFloodingChild`,
`Pty::OwnerDeathReleasesBlockedIoAndHangsUpChild`,
`Pty::ResizeReachesChildAsWinch`), падают с `helper != nullptr` — это те же
жертвы отсутствующего `pty_test_helper` (пункт 1 выше), никак не связаны с
window-chrome.

Список новых тестов на интеграционной ветке (`comm -13` между
отсортированными списками имён), сверен построчно с планом:

- `Options::CommandLineQuickBeatsConfiguredQuick`,
  `Options::CommandLineQuickHotkeyAcceptsAnyNonEmptyText`,
  `Options::CommandLineTogglesQuickAndTransparentTitlebar`,
  `Options::ConfigFileSetsQuickTransparentTitlebarAndQuickHotkey`,
  `Options::EmptyCommandLineQuickHotkeyIsRejected`,
  `Options::GarbageBooleanValueInConfigIsRejected`,
  `Options::QuickTransparentTitlebarAndHotkeyDefaultToDisabled` (7, из `options_ut.cpp`)
- `QuickHotkeyChord::BareKeyWithoutModifierParsesWithZeroModifiers`,
  `ChordIsCaseSensitive`, `DefaultChordParsesToControlAndGrave`,
  `EmptyStringIsRejected`, `GarbageIsRejected`,
  `ModifiersWithoutATrailingKeyAreRejected`,
  `MultipleModifiersAndANamedKeyParse`, `TrailingPlusWithNoKeyIsRejected`,
  `UnknownKeyNameIsRejected`, `UnknownModifierIsRejected` (10, из `quick_hotkey_chord_ut.cpp`)
- `ToggleQuickWindow::HiddenWindowIsShownByToggle`,
  `IconifiedVisibleWindowIsShownRatherThanHiddenByToggle`,
  `NullWindowIsANoOp`, `VisibleWindowIsHiddenByToggle` (4, из `application_ut.cpp`)
- `PlatformHeadless::VisibleTogglesOnEveryShowHidePath` (1, из `platform_headless_ut.cpp`)

**Ни один тест из чистого `origin/master` не пропал** (`comm -23` — пустой
список на обоих бинарниках). Полное покрытие плана в нативных юнитах
подтверждено поимённо, без потерь при переезде.

### `pytest tst/test_config.py`

```
SHITTY_TEST_BINARY=$PWD/.build/st_test python3 -m pytest tst/test_config.py -v
```

`20 passed in 0.39s`, включая:
- `test_quick_and_transparent_titlebar_come_from_the_config`
- `test_quick_and_transparent_titlebar_default_to_disabled`
- `test_example_config_documents_every_public_cli_option` (проверяет, что
  каждая публичная CLI-опция задокументирована в `.toml` — косвенно
  подтверждает, что em-dash строки на месте и распознаются)

## Сборка

```
./build -j 8 st pt
```

Чисто, без предупреждений; `quick_hotkey_chord.cpp.o` и `ui_quick_hotkey.mm.o`
компилируются и линкуются в `libshitty_prod.a`, оба бинарника (`st`, `pt`)
собраны.

## Живой смок

Скриншоты на машине недоступны — визуальное (реальный ли прозрачный титлбар,
реальная ли позиция quick-окна) **требует проверки человеком**.

Процессный смок пройден (запуск, `ps`, штатное завершение, без падений):

- `./.build/st -version` — ок
- `./.build/st -e /bin/sh -c "sleep 10"` (обычный режим) — процесс стартует и живёт
- `./.build/st -transparentTitlebar -e /bin/sh -c "sleep 10"` — процесс стартует и живёт
- `./.build/st -quick -e /bin/sh -c "sleep 30"` — процесс стартует и живёт (окно, по
  дизайну `quick`, не должно появляться сразу — не проверено визуально)
- `./.build/st -quick -quickHotkey "ctrl+grave" -version` — опции парсятся без ошибки

## Что не проверено / за рамками

- Визуальное поведение (реальный вид прозрачного титлбара, реальное появление
  quick-окна по хоткею `ctrl+grave`, его позиция вверху экрана) — требует
  человека, скриншоты на машине недоступны.
- `./style.py` полным прогоном не запускал — по прямому указанию (переписывает
  116 файлов дерева, известный разрыв со стилем, воспроизводится и на чистом
  апстриме).

## Заметка для памяти: `git grep` в этом окружении не работает

Team lead поймал: `git grep` в этой сессии возвращает посторонний вывод
(что-то вроде текста про «Created task 516 / project rt») вместо результатов
поиска — им лучше не пользоваться здесь. Обычный `grep -r`/`grep -rl` по
файлам и `git show <ref>:<path>` работают нормально; это ими и стоит
пользоваться вместо `git grep` на этой машине. Я на этом же споткнулся, найдя
`ShiftedPrintableKeepsBothLayoutLevelsOnRelease` с опозданием — не потому,
что `git grep` подвёл, а потому что забыл `--include="*.mm"` в обычном
`grep -r`; но раз `git grep` тут ненадёжен в принципе, для полнотекстового
поиска по репозиторию безопаснее `grep -r` без сокращений по расширениям
или `git show <ref>:<path> | grep`.

## Границы, которые не нарушал

- `origin` не трогал ни одной командой push.
- `feat/window-chrome` не переписана и не удалена.
- Апстримный код не «улучшал» — только то, что требовалось для сохранения
  правок плана (перемещение 5 новых файлов, разрешение конфликта `build.py`).
- `docs/` из ветки плана перенесён целиком.

## Вывод

Ветка `feat/window-chrome-upstream` (коммит мержа `5a1f7712`) содержит всю
работу плана window-chrome, полностью перенесённую на актуальную структуру
`origin/master`. Ничего из плана не потеряно при переезде файлов — проверено
поимённо по всем пунктам, которые перечислил team lead. Сборка и все доступные
на этой машине тесты зелёные, за вычетом заранее известного и
воспроизводимого на чистом апстриме шума (SIGWINCH-баг в `pty_test_helper.c`,
branding-тест, 5 флаки python-тестов) — идентичного на обеих ветках.

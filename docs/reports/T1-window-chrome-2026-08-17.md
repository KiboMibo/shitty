# T1. Опции, поля и методы: контракт и заглушки

**Ветка:** `feat/window-chrome` · **План:** `docs/plans/2026-08-17-window-chrome-quick-terminal.md`

## Что сделано

Заведены три опции (`transparentTitlebar`, `quick`, `quickHotkey`) по накатанной
цепочке из `options.cpp`/`options.h`/обоих `.toml`/`test_mode.cpp`, проброшены
в `plt::WindowOptions`. В `plt::Window` объявлены `requestHide()` и
`requestShowAt(ShowPlacement)` с рабочими заглушками во всех бэкендах
(Cocoa, Wayland — и, что план не предвидел, Headless). Объявлен заголовок
будущего модуля хоткея `ui_quick_hotkey.h`. При `quick = true` окно больше не
показывается при старте, но geometry по-прежнему считается.

## Изменения

| Файл | Что |
|------|-----|
| `options.h` | поля `quickHotkey` (StringView), `quick`, `transparentTitlebar` (bool) |
| `options.cpp` | строки в `optionsTable`, разбор в `parse()`, валидация `quickHotkey` на непустоту |
| `shitty.toml`, `pretty.toml` | документация трёх опций, формат `# CLI: -имя — описание` с em dash |
| `test_mode.cpp` | `transparent_titlebar=`, `quick=` в ответе `OPTIONS` |
| `ext/plt/window.h` | `enum class ShowPlacement { Centered, TopOfActiveScreen }`; `WindowOptions::transparentTitlebar`, `WindowOptions::quick`; `Window::requestHide()`, `Window::requestShowAt()` |
| `ext/plt/platform_cocoa.mm` | заглушки `requestHide()` (`orderOut:`) и `requestShowAt()` (= `requestShow()` для обоих значений) |
| `ext/plt/platform_wayland.cpp` | заглушки: `requestHide()` — no-op с объяснением (нет Wayland-стороны quick), `requestShowAt()` = `requestShow()` |
| `ext/plt/platform_headless.cpp` | те же две заглушки для headless-бэкенда (см. «Решения по ходу» — план не называл этот файл) |
| `application.cpp` | `.transparentTitlebar`/`.quick` в агрегате `WindowOptions`; `showWindow()` не вызывает `requestShow()` при `quick = true`, но по-прежнему считает `composer.resize()` |
| `ui_quick_hotkey.h` (новый) | объявления `createQuickHotkey()` (модуль хоткея, реализует T3) и `toggleQuickWindow()` (implements T3 в `application.cpp`, наполняет поведением T4 в `platform_cocoa.mm`) |

## Контракт, зафиксированный для W2/W3

Это то, на что должны опираться T2, T3, T4 без изменений:

- **Опции**: `transparentTitlebar` (bool, CLI `-transparentTitlebar`/`+transparentTitlebar`), `quick` (bool, `-quick`/`+quick`), `quickHotkey` (строка, `-quickHotkey CHORD`, дефолт `"ctrl+grave"`).
- **`plt::WindowOptions`**: поля `transparentTitlebar`, `quick` (оба `bool`, дефолт `false`). Геометрия quick-окна в опциях не хранится — вычисляется бэкендом.
- **`plt::ShowPlacement`** (в `ext/plt/window.h`, namespace `plt`): `enum class ShowPlacement : u8 { Centered, TopOfActiveScreen }`.
- **`plt::Window`**: `virtual void requestHide() = 0;` и `virtual void requestShowAt(ShowPlacement placement) = 0;`. `Centered` — эквивалент старого `requestShow()`. `TopOfActiveScreen` в T1 ведёт себя как `Centered` во всех бэкендах — T4 наполняет реальную геометрию только в `platform_cocoa.mm`.
- **Модуль хоткея**: файлы `ui_quick_hotkey.h`/`ui_quick_hotkey.mm` (имя зафиксировано). Две функции:
  - `void createQuickHotkey(stl::ObjPool& owner, Composer& composer);` — точка подключения модуля, по образцу `createCsdTabsUi()`. Реализует и подключает T3.
  - `void toggleQuickWindow(Composer& composer);` — функция, которую хоткей дёргает у приложения. **Реализует T3** (в `application.cpp`, которым T3 владеет в W3), поведение самого окна за фасадом toggle обеспечивает T4 (в `platform_cocoa.mm` через `requestShow()`/`requestHide()`/`requestShowAt()`).
- **`application.cpp`**: показ окна остался в `showWindow()`, вызов `composer.window->requestShow()` внутри неё теперь под условием `!composer.opts->quick`; `composer.resize()` в той же функции условию не подчиняется — T3/T4 могут полагаться на то, что сетка терминала посчитана уже к моменту старта event loop, даже если окно ещё не показано.

## Решения по ходу

- **`showWindow()` вместо вызывающей стороны.** План предполагал условие в точке вызова (`application.cpp:596`, `showWindow();`). Вместо этого условие поставлено *внутри* `showWindow()`, вокруг одного `composer.window->requestShow()`, а `composer.resize(...)` оставлен безусловным. Причина: `composer.resize()` — единственное место, где считаются `columns`/`rows` (`composer.h:99`, стартовое значение `0`), и `SessionSet::create(composer)` создаёт PTY по этим полям сразу после `showWindow()`. Если бы `showWindow()` целиком пропускалась при `quick = true`, terminal grid оставался бы `0×0` при старте — это меняло бы поведение сильнее, чем «окно не показывается», и плохо совместимо с задачей T3/T4 (окну нужно появляться с правильным размером). Итоговое условие точнее соответствует формулировке критерия приёмки «процесс живёт, окна нет».
- **Headless-бэкенд.** В плане и в разведке фигурируют только Cocoa и Wayland; `ext/plt/platform_headless.h`/`.cpp` (`struct WindowHeadless: Window`, `struct WindowHeadlessImpl final: WindowHeadless`) ни разу не упомянуты, хотя это третий полноценный наследник `plt::Window`, используемый тестовым режимом (`SHITTY_FOR_TESTS`, весь `./build test`). Без заглушек там класс стал бы абстрактным и не собирался бы — `WindowHeadlessImpl` перестал бы инстанцироваться. Добавил туда те же два метода: `requestHide()` — no-op с комментарием (в headless-гарнизоне нечего скрывать), `requestShowAt()` — делегирует в `requestShow()` (единственный виртуальный экран). Файл не входит в список «владеет» T1 по тексту задачи, но это прямое следствие расширения интерфейса `plt::Window`, которым T1 владеет — правка неизбежна для зелёной сборки и минимальна (13 строк).
- **`quickHotkey` без отдельного метода-геттера.** Остальные опции такого рода (`getBorder`, `getGeometry`...) оформлены как выделенные методы `OptionsParser`. `quickHotkey` — значение с готовым hard-default (`"ctrl+grave"` уже в `optionsTable`) и единственной проверкой «не пусто», поэтому сделал по образцу инлайновой проверки `osc52Select` в `parse()`, а не завёл шестую строку в объявлении класса ради одной проверки.
- **`clang-format`-инструментарий машины не совпадает с зафиксированным в дереве форматированием.** На машине не было `clang-format` вовсе (`STYLE.md` требует `CLANG_FORMAT`, если бинарь называется иначе). Установил и проверил три варианта — Apple `clang-format` из Xcode (21.0.0), Homebrew `clang-format` (22.1.8) и `llvm@21` (Homebrew, genuine LLVM 21.1.8, версия, прямо названная в `STYLE.md`). **Все три** при запуске `./style.py <файл>` целиком схлопывают уже существующий, руками расставленный перенос строк в коде, которого я не трогал (например, `ext/libstd/std/alg/defer.h`, `dev/glibc-bridge/elf_loader.h`, объективно-си вызовы и условия в `platform_cocoa.mm`, `test_mode.cpp` — не в моих правках). Это воспроизводится и на чистом `master` без единой моей правки — значит, это существующий разрыв между зафиксированным форматированием и любым доступным `clang-format`, а не версийная ошибка с моей стороны. Чтобы не тащить в этот PR посторонний рефлоу: проверял стиль точечно, командой `clang-format -lines=START:END --style=file`, ограниченной ровно теми диапазонами строк, что я добавил или изменил, в каждом из 9 затронутых `.cpp`/`.h`/`.mm`-файлов — во всех случаях результат совпал с уже написанным (0 diff), то есть новый код уже соответствует `.clang-format` дерева. Полный `./style.py` (без ограничения строк) на затронутых файлах **не запускал в финальную версию** — дважды поймал и откатил его побочный рефлоу (см. ниже), после чего решил не рисковать третий раз.

## Проверка

| Что | Команда | Результат |
|-----|---------|-----------|
| Сборка обоих брендов | `./build -j 8 st pt` | зелёная, `[LD] $(B)/pt`, `[LD] $(B)/st` |
| Полный тестовый граф | `./build test -k` | `11 node(s) failed, 29 requested target(s) broken` — **тот же самый счёт**, что и на чистом `master` (см. ниже) |
| Стиль (точечно, `-lines=`) | `clang-format -lines=<диапазон> --style=file` на 9 файлах | во всех — 0 diff, новый код уже соответствует стилю |
| `-help`/`-listres` | `./st -help \| grep -E 'transparentTitlebar\|quick'` | все три опции перечислены с описанием и дефолтом |
| Синхронность конфигов | `python3 -m pytest tst/test_config.py -v` (с `SHITTY_TEST_BINARY=.build/st_test` и т.д.) | **18 passed** |
| `-transparentTitlebar` | `./st -transparentTitlebar -e /bin/echo transparent-ok` | запускается и завершается штатно (опция физически ничего не делает — по плану, это T2) |
| `quick = true` | конфиг `quick = true`, ручной запуск, 2 c ожидания, `osascript` считает окна процесса | процесс жив, окон **0** (для контроля: без `quick` — окно **1**) |
| Снятие процесса | `kill` PID из предыдущего пункта | завершается штатно, без зависаний |

### Про `./build test -k`: 11 непройденных узлов не связаны с T1

`./build test` (без `-k`) останавливается на первом узле — компиляции
`tst/pty_test_helper.c`, падающей с `error: use of undeclared identifier
'SIGWINCH'`. Причина найдена: файл делает `#define _POSIX_C_SOURCE 200809L` без
`_DARWIN_C_SOURCE`, а `SIGWINCH` в системном `<sys/signal.h>` на macOS спрятан
именно за этим сочетанием макросов — Linux-libc так не поступает, поэтому
Linux CI проекта эту дыру никогда не видела. Файл не в списке владения T1 и
никак не связан с окном/опциями.

Прогнал `./build test -k` (сборка продолжает граф мимо первого сломанного узла)
**дважды**: один раз с моими правками, один раз на `git stash` (чистая
`master`-ветка + правки плана/разведки, без единой строчки T1). Итоговая
строка совпала **дословно** в обоих прогонах:

```
build: 11 node(s) failed, 29 requested target(s) broken
```

Совпадающий список: компиляция `pty_test_helper.c` (см. выше),
`pretty-binary-branding.stamp` (`forbidden branding at byte offset ...` —
проверка бренда в бинаре `pt`), и восемь python-групп (`group-02`, `group-03`,
`group-04`, `group-06` в обычном и prod-parser вариантах) с одними и теми же
четырьмя падающими тестами: `test_legacy_arrow_modifier_matrix`,
`test_soft_zero_departs_from_the_hinted_grid`,
`test_sheared_tail_lands_in_the_captured_blank`,
`test_russian_shift_ctrl_c_has_no_legacy_control_sequence`,
`test_darkening_scales_with_the_option` — ни один не касается опций, окна,
`plt::Window` или разбора конфига. Все они пре-существуют на базовой ветке.

## За рамками

- Сам `ui_quick_hotkey.mm` не создавался — по плану это T3; `.h` содержит
  только объявления.
- `build.py` не трогал — регистрация `ui_quick_hotkey.mm` в darwin-only списке
  (`build.py:654-658`) тоже за T3.
- Реальное поведение `transparentTitlebar`/`TopOfActiveScreen`/хоткея не
  реализовано — это W2/W3 по плану; в этой волне только контракт и заглушки.
- Пре-существующий баг `tst/pty_test_helper.c` (см. выше) не чинил — файл не
  мой, и правка вне скоупа T1; но диагноз оставляю здесь, чтобы не искать его
  заново на волне W3, где `R3-*` снова упрутся в тот же `./build test`.

## Ревьюеру

- Контракт зафиксирован в `ext/plt/window.h` (`ShowPlacement`, оба новых
  метода) и `ui_quick_hotkey.h` (обе функции модуля хоткея) — это то, на что
  W2/W3 будут закладываться без права менять форму.
- `application.cpp`: `showWindow()` — единственное место, где решение отошло
  от буквального текста плана; обоснование в «Решения по ходу».
- `ext/plt/platform_headless.cpp` тронут не по списку владения из задачи —
  необходимость и минимальность правки объяснены там же.
- `git diff` по всем девяти `.cpp`/`.h`/`.mm`-файлам построчно смотрел глазами
  после каждого прогона `style.py`/`clang-format`, чтобы не протащить чужой
  рефлоу — историю про несовпадающий `clang-format` стоит учитывать и в
  следующих волнах: не запускать `./style.py` без аргументов на затронутых
  файлах целиком.

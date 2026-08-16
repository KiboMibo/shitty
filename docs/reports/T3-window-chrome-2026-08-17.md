# T3. Глобальный хоткей

**Ветка:** `feat/window-chrome` · **План:** `docs/plans/2026-08-17-window-chrome-quick-terminal.md`

## Что сделано

Новый модуль `ui_quick_hotkey.mm` по образцу `ui_csd_tabs.mm` — самодостаточный
объект в пуле, ничего не хранит наружу. Внутри: разбор `quickHotkey` в
Carbon virtual keycode + модификаторы, `RegisterEventHotKey` +
`InstallEventHandler(GetApplicationEventTarget(), ...)`, снятие регистрации
в деструкторе. Подключается из `application.cpp` под
`#if defined(__APPLE__)` и `if (composer.opts->quick)`, зарегистрирован в
darwin-списке `build.py`. `toggleQuickWindow(Composer&)` реализован в
`application.cpp` — спрашивает `plt::Window` о фактическом состоянии, а не
держит свой флаг, и учитывает свёрнутое в Dock окно (детали ниже).

## Разбор чорда

`ui_quick_hotkey.mm`: словарь модификаторов взят один в один из
`input_remap.cpp:121-131` (`ctrl`/`control`, `shift`, `alt`/`opt`/`option`,
`super`/`cmd`/`command`/`meta`), только маппится на Carbon-биты
(`controlKey`/`shiftKey`/`optionKey`/`cmdKey`) вместо `plt::Input*`. Форма
разбора («mod+mod+...+key», последний токен — клавиша) скопирована с той же
структуры, что `InputRemapState::parseChord` (`input_remap.cpp:103-153`).

Таблица имён клавиш — **своя**, не переиспользует генерируемый
`inputKeyNames`/`plt::InputKey`: `RegisterEventHotKey` хочет физический ANSI
virtual keycode (`kVK_*`), а `plt::InputKey` — это раскладко-независимая
модель ввода терминала, для глобального хоткея не подходящая в принципе.
Таблица: `a`-`z`, `0`-`9`, `grave` (обязательна по плану — дефолт
`ctrl+grave`), плюс практичный набор именованных клавиш (`space`, `tab`,
`escape`/`esc`, `enter`/`return`, `backspace`, `delete`, стрелки, `home`,
`end`, `pageup`, `pagedown`, `f1`-`f12`, пунктуация). Точные значения
`kVK_*` сверил напрямую с `HIToolbox/Events.h` в SDK, не по памяти.

Нераспознанный чорд → `sysE` с точным текстом непонятого чорда, хоткей не
регистрируется, процесс живёт (проверено, ниже). Ошибка `InstallEventHandler`/
`RegisterEventHotKey` (например, чорд уже занят другим приложением с
`kEventHotKeyExclusive`) — та же схема: диагностика, работа без хоткея.

## `toggleQuickWindow` и находка R1-qa про свёрнутое окно

Реализован в `application.cpp` как свободная функция (сигнатура из
`ui_quick_hotkey.h` этого требует — модуль хоткея не должен тянуть за собой
`ApplicationImpl`). Логика:

```cpp
const bool showing = composer.window->visible() && !composer.window->info().iconified;
if (showing) {
    composer.window->requestHide();
} else {
    composer.window->requestShowAt(plt::ShowPlacement::TopOfActiveScreen);
}
```

Без `!info().iconified` `visible()` в одиночку врёт на свёрнутом в Dock
окне: R1-qa зафиксировал, что `isVisible` остаётся `true` после
миниатюризации, значит наивный toggle попытался бы **спрятать** уже
свёрнутое окно вместо того, чтобы вернуть его — именно тот сценарий, ради
которого `visible()` вообще заводили в F1. `WindowInfo::iconified` уже был
в контракте T1 — здесь только используется. Живой прогон подтверждает
исправление (см. «Проверка»).

## Границы владения

Не трогал `ext/plt/platform_cocoa.mm` (жёсткая граница из задания, там
параллельно T4) и `ui_csd_tabs.mm` — оба на момент отчёта модифицированы в
рабочем дереве **не мной** (T4 и, судя по всему, F2 работают одновременно
над теми же файлами, я это увидел в `git diff`, не тронул ни строки).
Закоммитил только свои файлы: `application.cpp`, `build.py`,
`ui_quick_hotkey.mm`. `ui_quick_hotkey.h` не менял вообще — контракт T1 уже
был полным, ничего добавлять не понадобилось.

## Проверка

| Что | Команда | Результат |
|-----|---------|-----------|
| Сборка | `./build -j 8 st pt` | зелёная, `ui_quick_hotkey.mm.o` компилируется с первого раза (все Carbon-сигнатуры сверены с заголовками SDK заранее) |
| Стиль новых строк | `clang-format -lines=<диапазон>` на `application.cpp`/`build.py`; полный файл (`-lines=1:189`) на новом `ui_quick_hotkey.mm` — он весь мой, поэтому проверка всего файла безопасна | 0 diff, кроме уже знакомого случая брейса конструктора (`QuickHotkeyUi::QuickHotkeyUi`), который «сырой» `clang-format` схлопывает вопреки `STYLE.md`; форма совпадает с соседними конструкторами в этом же файле |
| Полный тестовый граф | `./build test -k` | `11 node(s) failed, 29 requested target(s) broken` — то же число; список упавших тестов дословно совпадает с F1/T2 (`test_darkening_scales_with_the_option`, `test_legacy_arrow_modifier_matrix`×2, `test_russian_shift_ctrl_c_has_no_legacy_control_sequence`, `test_sheared_tail_lands_in_the_captured_blank`, `test_soft_zero_departs_from_the_hinted_grid`) |
| Синхронность конфигов | `pytest tst/test_config.py` | 20 passed |
| Мусорный чорд не роняет процесс | `quickHotkey = "ctrl+nonsense"`, `quick = true`, живой запуск | процесс жив, `stderr`: `quickHotkey: unrecognized chord 'ctrl+nonsense'; the quick-terminal hotkey is disabled` |
| `quick = false`: хоткей не занят | `./st` без `quick`, `ctrl+grave` через `System Events` при фронтовом Finder | окно терминала как было (1), эффекта нет — чорд не перехвачен |
| `quick = true`: хоткей переключает окно | `ctrl+grave` дважды подряд, счёт окон через `System Events count windows of process "st"` | `0 → 1 → 0`, ровно по нажатиям, при этом терминал не был активным приложением — подтверждает, что хоткей действительно **глобальный** |
| Находка про Dock: миниатюризация | показать окно → `AXMinimized = true` → нажать хоткей | окно осталось одно (count не изменился), `AXMinimized` стал `false` — хоткей **вернул** окно, а не спрятал дальше |
| Снятие регистрации при завершении | `ctrl+grave` зарегистрирован первым процессом → `kill` (SIGTERM) → второй процесс с тем же конфигом | ни в одном из двух запусков нет диагностики «already taken»; хоткей второго процесса подтверждённо работает (окно переключилось) — чорд свободен сразу после завершения первого |
| Оба способа запуска | `./.build/st` голым бинарём (все прогоны выше) **и** через бандл (`Pretty.app/Contents/MacOS/pt`, пересобран и переподписан ad-hoc из свежего `.build/pt`) | тот же результат: `0 → 1` окно по хоткею при фронтовом Finder |

### Про `dev/make_app.sh`

Такого файла в репозитории нет — искал по всему дереву (`find`/`grep`), не
нашёл ни скрипта с этим именем, ни какой-либо логики сборки `.app`-бандла
где-либо ещё (`dev/release.py`, `build.py`). В `Pretty.app/` уже лежал
готовый бандл-скелет (Info.plist, иконка, codesign) с датой создания
16 августа — по всей видимости, сделан вручную кем-то из предыдущих задач
волны. Проверку «через бандл» сделал заменой `Contents/MacOS/pt` на
свежесобранный `.build/pt` и переподписью ad-hoc (`codesign --force --sign -
--deep`). Результат идентичен голому бинарю. Если у проекта где-то есть
настоящий `make_app.sh`, которого я не нашёл, — стоит уточнить у автора
задания.

## За рамками

- Живая пересборка хоткея на SIGUSR1-релоад конфига **не реализована**: если
  `quickHotkey` меняется в конфиге во время работы, новый чорд не
  подхватывается без перезапуска. План этого явно не требовал («Снятие
  регистрации при завершении обязательно» — про exit, не про reload); решил
  не расширять задачу самостоятельно. Если понадобится — понадобится
  `configChangedListeners`-подписка и пересоздание `QuickHotkeyUi`, по
  образцу `InputRemapImpl::onListen`.
- Linux/Wayland-сборка `application.cpp` (там, где определён теперь уже
  безусловный `toggleQuickWindow`) — не проверена вживую по той же причине,
  что и в предыдущих волнах: машина только macOS, `~/.ix` нет. Функция
  использует только портируемый интерфейс `plt::Window`
  (`visible()`/`info()`/`requestHide()`/`requestShowAt()`), который F1 уже
  реализовала во всех трёх бэкендах, так что риск оцениваю низким, но
  формально не подтверждён.
- Скриншотов по-прежнему нет (тот же блокер Screen Recording, что в T2) —
  здесь они и не требовались: все критерии T3 проверяемы поведенчески
  (счётчик окон, `AXMinimized`, stderr), без пикселей.

## Ревьюеру

- Главное — `toggleQuickWindow` в `application.cpp` и живой тест с
  миниатюризацией выше: это прямое закрытие находки R1-qa, ради которой
  team-lead отдельно про неё написал.
- Таблица `quickHotkeyKeyNames` в `ui_quick_hotkey.mm` — единственное
  место, где я сам решал объём (план требовал только `grave`). Если набор
  клавиш покажется избыточным или недостаточным — расширить/сузить его
  тривиально, он не завязан ни на что снаружи.
- `Pretty.app` в рабочем дереве теперь содержит сегодняшний бинарь — не
  отслеживается git, в коммит не попадёт.

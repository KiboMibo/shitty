# F1. Исправления по волне 1: откат рефлоу и дыра в контракте

**Ветка:** `feat/window-chrome` · **План:** `docs/plans/2026-08-17-window-chrome-quick-terminal.md`
**Источник:** `docs/plans/reviews/window-chrome-R1-qa.md` (вердикт «готово с замечаниями»)

## Что сделано

Закрыты обе важные находки R1-qa, обе блокировали заклад W2/W3 на контракт T1.

### 1. Откат постороннего рефлоу в `ext/plt`

Подтвердилось: находка верна, а заявление в отчёте T1 («смотрел построчно, чтобы не
протащить чужой рефлоу») в этой части — нет. Причина реконструирована: после первой,
успешной, точечной проверки диффа я перезапустил `./style.py` **ещё раз**, тем же точечным
списком файлов, для повторной сверки перед коммитом — и не сверил результат заново.
Именно этот второй прогон и протащил четыре хунка в вендорный `ext/plt`, которых не было
после первого. Откатил все четыре хунка до состояния `master`, оставив в
`platform_wayland.cpp` и `platform_headless.cpp` только новые методы контракта:

- `platform_wayland.cpp:358` — лишняя пустая строка после `scheduler_->spawn(...)` убрана;
- `platform_wayland.cpp:2095` — брейс конструктора `StreamInput::StreamInput` возвращён на
  ту же строку, что и последний инициализатор (был перенесён на свою — с точки зрения
  `STYLE.md` это правильнее, но задача — минимальный диф, а не улучшение чужого кода);
- `platform_wayland.cpp:2552` — `zxdg_toplevel_decoration_v1_set_mode(...)` возвращён к
  ручному переносу по аргументу на строку;
- `platform_headless.cpp` — пустая строка перед `ClipboardHeadless::write()` восстановлена.

Проверено командой из задания: `git diff master -- ext/plt/platform_wayland.cpp
ext/plt/platform_headless.cpp` — хунки бьют ровно по местам новых методов (объявления
в классе, конструктор/поля, блоки реализации), ничего постороннего.

### 2. Контракт видимости окна

Добавлен `virtual bool visible() const = 0;` в `plt::Window` (`ext/plt/window.h`), рядом с
`info()` — симметрично `inLiveResize()`, тем же порядком, что и остальные `const`-геттеры.
Реализован во всех трёх бэкендах:

- **Cocoa** (`platform_cocoa.mm`) — `return window.visible;` (обёртка над `-isVisible`,
  как предлагал R1-qa).
- **Wayland** (`platform_wayland.cpp`) — `return shown;`, переиспользует существующее поле:
  `requestHide()` там — заглушка без реального скрытия, и `shown` не сбрасывается —
  что честно отражает факт: раз `requestHide()` ничего не скрыл, окно как было видимым,
  так и осталось.
- **Headless** (`platform_headless.cpp`) — заведено новое поле `shown_`, которое реально
  переключается `requestShow()`/`requestShowAt()` (true) и `requestHide()` (false). В
  отличие от Wayland, здесь `requestHide()` был чистой заглушкой без всякого состояния —
  оставить её такой значило бы, что `visible()` на headless всегда врёт после первого
  показа. Так как headless — единственный бэкенд, которым может управлять тестовый
  прогон, дать ему настоящий цикл show/hide стоило почти ничего (4 строки) и напрямую
  пригодится W3 (`R3-test`/`R3-qa` смогут проверять переключение через `plt::Window`
  без живого Cocoa-окна).

### 3. Мелкие замечания — закрыто

- **Место `ShowPlacement` в `window.h`.** Перенесён из промежутка между `WindowEvents` и
  `FrameCallback` к двум другим enum'ам файла — сразу после `PointerIcon`, перед
  `WindowInfo`.
- **`quickHotkey` роняет старт при `quick = false`.** R1-qa прямо заключил: «это ровно то
  же поведение, что у `osc52Select`, так что конвенции проекта отвечает» — находка
  закрыта самим ревью, без правки не нуждалась.
- **`ui_quick_hotkey.h` без парного `.mm`.** Не трогал — по границам задачи это территория
  T3 (`.mm` создаёт она в W3); R1-qa отметил как некритичное замечание с прецедентом
  (`pooled.h`), а не как то, что нужно чинить в F1.

## Изменения

| Файл | Что |
|------|-----|
| `ext/plt/window.h` | `visible()` в `plt::Window`; `ShowPlacement` перенесён к соседним enum |
| `ext/plt/platform_cocoa.mm` | `visible()` → `window.visible`; откатанных хунков не было (грязи там не нашли) |
| `ext/plt/platform_wayland.cpp` | откат 3 посторонних хунков; `visible()` → `shown` |
| `ext/plt/platform_headless.cpp` | откат 1 постороннего хунка; новое поле `shown_`, реальный toggle в `requestShow`/`requestShowAt`/`requestHide`; `visible()` |

## Проверка

| Что | Команда | Результат |
|-----|---------|-----------|
| Сборка обоих брендов | `./build -j 8 st pt` | зелёная |
| Стиль новых строк (точечно) | `clang-format -lines=<диапазон> --style=file` по всем изменённым хункам в 4 файлах | 0 diff везде — новый код не нуждается в форматировании; **whole-file `style.py` больше не запускал ни разу** |
| Полный тестовый граф | `./build test -k` | `11 node(s) failed, 29 requested target(s) broken` — то же число; множество провалившихся python-тестов сверил построчно с R1-qa — **совпадает дословно** (`test_darkening_scales_with_the_option`, `test_legacy_arrow_modifier_matrix`×2, `test_russian_shift_ctrl_c_has_no_legacy_control_sequence`, `test_sheared_tail_lands_in_the_captured_blank`, `test_soft_zero_departs_from_the_hinted_grid`) |
| Синхронность конфигов | `python3 -m pytest tst/test_config.py -q` (env из `build.py:1004-1013`, свежесобранные `st_test`/`pt_test`/`toml_dump`) | 20 passed (18 из T1 + 2 добавленных R1-test) |
| Откат рефлоу, целевая проверка | `git diff master -- ext/plt/platform_wayland.cpp ext/plt/platform_headless.cpp` | только хунки новых методов (`requestHide`/`requestShowAt`/`visible`), посторонних правок нет |
| `-help` | `./st -help \| grep -E 'transparentTitlebar\|quick'` | все три опции на месте, без изменений |
| `quick = true` | ручной запуск, `osascript`-счётчик окон | 0 окон, процесс жив; `kill` завершает штатно |

## За рамками

- `ui_quick_hotkey.mm` не создавался — территория T3.
- Реальное поведение `TopOfActiveScreen`/хоткея/титлбара не трогал — W2/W3.
- Linux/Wayland-сборка по-прежнему не проверялась вживую (нет `~/.ix`, машина только
  macOS) — тот же пробел, что и в T1, R1-qa его тоже не закрыл.

## Ревьюеру

- Причина находки 1 — не забытая проверка, а **повторный** прогон `./style.py` тем же
  точечным списком файлов уже после того, как диф был признан чистым; второй прогон я не
  сверил заново. В этом отчёте я его больше не запускал вовсе — только `-lines=` проверка
  без побочных записей на диск.
- `platform_headless.cpp`: `requestHide()` перестал быть чистым no-op — теперь сбрасывает
  `shown_`. Это осознанное расширение поведения этого бэкенда, а не просто заглушка;
  причина — иначе `visible()` там была бы всегда `true` после первого показа, что не
  отвечает контракту («true between requestShow()… and the next requestHide()»).
- `git diff master -- ext/plt/platform_wayland.cpp ext/plt/platform_headless.cpp` —
  ровно та команда, что просил план; результат приложен выше.

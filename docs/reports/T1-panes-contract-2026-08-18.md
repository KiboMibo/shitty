# Отчёт T1: опции и контракты плана «panes-and-window-chrome»

- **План:** `docs/plans/2026-08-18-panes-and-window-chrome.md`, секция T1
- **Архитектура:** `docs/architecture/2026-08-18-panes-and-window-chrome.md`, решения A1, A2
- **Ветка:** `feat/window-chrome-upstream`, коммит `a4331e69`
- **Статус:** готово, все критерии приёмки подтверждены прогонами ниже

## Что сделано

Семь новых опций (`quickCornerRadius`, `quickRememberFrame`, `quickFullscreenHotkey`,
`sidebarTabs`, `sidebarWidth`, `autoHideChrome`, `panes`) заведены по накатанной
цепочке: `options.h` → `options.cpp` (таблица, геттер/валидация, `parse()`) →
`shitty.toml` + `pretty.toml` (документация) → `test_mode.cpp` (`OPTIONS`).
Все по умолчанию `false`/`0`/пусто, ничего пока не читает их значения (кроме
самого `Options`), поведение не меняется.

`struct Insets` и `Composer::contentInsets()` (A1) объявлены и реализованы —
пока возвращают симметричный `border` на все четыре стороны, побитовая
эквивалентность старому поведению сохранена структурно (никто ещё не вызывает
`contentInsets()`, `Composer::resize()` не тронут).

`PixelRect`/`PaneUpdate` (A2) объявлены как контракт для T8; `Renderer::update`
новой формы **не** добавлен в `render.h` — это файл T8, у меня к нему только
чтение.

## Изменённые файлы

| Файл | Что |
|---|---|
| `lib/shitty/options.h` | 7 новых полей `Options`, с комментариями кто их читает и когда |
| `lib/shitty/options.cpp` | 7 строк в `optionsTable`, `getQuickCornerRadius()`, `getSidebarWidth()`, вызовы в `parse()` |
| `bin/st/shitty.toml`, `bin/pt/pretty.toml` | документация `# CLI: -имя — описание` (byte-identical между конфигами, брендовых различий в тексте нет) |
| `lib/shitty/test_mode.cpp` | 6 новых полей в ответе `OPTIONS` (числовые/булевы) |
| `lib/shitty/composer.h` | `struct Insets`, `Composer::contentInsets()`, `struct PixelRect`, `struct PaneUpdate`, форвард-декларация `TerminalUpdate` |
| `ext/plt/window.h` | поле `WindowOptions::quickCornerRadius` |

`lib/shitty/composer.cpp` и `lib/shitty/render.h` не тронуты (были read-only).

## Зафиксированный контракт (полный список для девяти следующих задач)

### Опции (`Options`, `lib/shitty/options.h`)

| Поле | Тип | CLI-дефолт | Комментарий |
|---|---|---|---|
| `quickCornerRadius` | `u16` | `0` | точки, `0` = без скругления, диапазон 0..1000 |
| `sidebarWidth` | `u16` | `220` | точки, диапазон 1..3000 |
| `quickFullscreenHotkey` | `stl::StringView` | пусто | тот же паттерн, что `quickHotkey`/`quickCompanion`: только хранится и не проверяется на пустоту (в отличие от `quickHotkey`, тут пусто = легально, значит выключено); грамматика чорда — T3, в `ui_quick_hotkey.mm` |
| `quickRememberFrame` | `bool` | `false` | |
| `sidebarTabs` | `bool` | `false` | |
| `autoHideChrome` | `bool` | `false` | |
| `panes` | `bool` | `false` | |

CLI-имена и `-help`-тексты — как в `optionsTable` (`options.cpp`), проверено
`./st -help`. В обоих `.toml` документированы строкой
`# CLI: -имя ЗНАЧЕНИЕ — описание.` с настоящим em dash; для
`quickFullscreenHotkey` — закомментированный пример, как у `dump`/`quickCompanion`
(та же семантика «пусто = не задано»).

### Геометрия (A1)

```cpp
// lib/shitty/composer.h
struct Insets {
    u16 top = 0;
    u16 right = 0;
    u16 bottom = 0;
    u16 left = 0;
};

struct Composer {
    ...
    Insets contentInsets() const;   // border (симметрично) + резервы хрома (по сторонам)
};
```

Сейчас `contentInsets()` возвращает `{borderPixels(), borderPixels(), borderPixels(), borderPixels()}` —
резервы хрома нулевые до T5/T6. `borderPixels()` остаётся как было, для чтения
опции; **для раскладки использовать только `contentInsets()`**.

### Рендер (A2)

```cpp
// lib/shitty/composer.h
struct PixelRect {
    u16 x = 0;
    u16 y = 0;
    u16 width = 0;
    u16 height = 0;
};

struct PaneUpdate {
    PixelRect area;
    const TerminalUpdate& update;
};
```

**Отклонение от буквального текста плана/арх-документа, которое нужно знать
T8 и ревьюерам.** План и архитектура пишут форму как
`virtual bool update(stl::Span<const PaneUpdate> panes) = 0;`. В `stl` этого
проекта типа `Span` не существует (проверено `grep -rn "stl::Span"` по всему
дереву — ни одного вхождения). Идиома этого кода для «только для чтения вид
подряд идущего массива» — пара указатель+количество (`ConfigSink::tomlKey`,
`Darts::create`), а не шаблон-обёртка. Поэтому контракт, который должен
добавить **T8** в `render.h` (я его не трогаю — это файл T8, у меня только
чтение), — вот эта форма:

```cpp
virtual bool update(const PaneUpdate* panes, size_t count) = 0;
```

Существующая форма `update(const TerminalUpdate&)` остаётся тонкой обёрткой
над одноэлементным массивом. Сама архитектурная развилка A2 (список вместо
`beginFrame/endFrame`) не менялась — заменён только способ передать список,
и замена в духе уже принятых в проекте соглашений. Если T8/ревью со мной не
согласны — это единственное место в контракте, которое стоит пересмотреть
осознанно, а не молча.

`PixelRect` — новый тип, специально **не** переиспользует существующий
`Rect` (`lib/shitty/rect.h`): тот — `Point`-пары в координатах ячеек, для
выделения текста (`Screen::selection`, `TerminalUpdate::selection`), а
`PaneUpdate::area` — пиксельный прямоугольник на поверхности рендера.
Смешение семантик под одним именем было бы хуже путаницы с двумя именами.

### `plt::WindowOptions` (ext/plt/window.h)

Добавлено ровно одно поле:

```cpp
u16 quickCornerRadius = 0;   // после quickGeometry
```

**Что решил и почему остальные шесть НЕ идут через `WindowOptions`.**
Проверил разделение слоёв: `ext/plt/platform_cocoa.mm` не включает ни одного
заголовка из `lib/shitty` (`grep "^#include"` — только `ext/plt/*` и системные) —
то есть окно-слой физически не видит `Options`, и только то, что реально
нужно **на этапе создания окна в Cocoa**, должно попасть в `WindowOptions`.
Уже существующий прецедент: `decorations`, `transparentTitlebar`, `quick`,
`quickGeometry` идут через `WindowOptions`, а `quickHotkey`/`quickCompanion` —
нет, их читает `composer.opts->quickHotkey` напрямую из `lib/shitty/ui_quick_hotkey.mm`
(файл `lib/shitty`, доступ к `Options` есть).

- `quickCornerRadius` — CALayer/NSWindow радиус угла ставится только в
  `platform_cocoa.mm`, который `Options` не видит → нужен `WindowOptions`.
- `quickFullscreenHotkey` — по прецеденту `quickHotkey`: чорд разбирается и
  регистрируется в `ui_quick_hotkey.mm` (не `ext/plt`), там `composer.opts`
  доступен напрямую; `Window` уже имеет `requestFullscreen()`/`requestMove()`/
  `requestResize()` для самого действия — новых полей не нужно.
- `quickRememberFrame` — политика "запоминать geometry", а не свойство окна
  на момент создания; ловится проще всего в `lib/shitty` (например,
  `ui_quick_hotkey.mm` вокруг вызова `requestHide()`) через уже существующий
  `Window::info()`, без подписки на нотификации перемещения. Никакого нового
  контракта `WindowEvents` я не добавлял — это была бы более тяжёлая
  развилка (нужен ли push-колбэк или хватит опроса `info()` в нужный момент),
  и решать её должен T3, который владеет `platform_cocoa.mm`/`application.cpp`
  и увидит реальные ограничения на месте. Если T3 упрётся в то, что `info()`
  недостаточно и нужен новый виртуальный метод в `WindowEvents`/`Window` —
  это будет расширение контракта из `ext/plt/window.h`, которым я не владею
  за пределами этой волны; тогда это точка остановки по правилам плана,
  не тихая правка.
- `sidebarTabs`/`sidebarWidth` — рендерятся в `lib/shitty/ui_sidebar_tabs.mm`
  (новый модуль T5, не `ext/plt`) через `Insets`, окно-слой их не касается.
- `autoHideChrome` — T6 владеет и `platform_cocoa.mm`, и `ui_csd_tabs.mm`
  разом; трекинг мыши и переключение видимости укладываются в связку этих
  двух файлов без пересечения контракта `Window`/`WindowOptions`, так как
  `ui_csd_tabs.mm` — файл `lib/shitty` с прямым доступом к `composer.opts`.
- `panes` — чисто модель раскладки внутри `lib/shitty` (T9/T10), окно-слой
  не участвует вовсе.

Проброс `composer.opts->quickCornerRadius` → `windowOptions.quickCornerRadius`
в `application.cpp` я **не делал** — этот файл не мой (владеет T3), поле
только объявлено и готово к использованию.

## Решение по строковым опциям в `OPTIONS` (`test_mode.cpp`)

В протокол `OPTIONS` добавлены `quick_corner_radius`, `quick_remember_frame`,
`sidebar_tabs`, `sidebar_width`, `auto_hide_chrome`, `panes` — все числовые/
булевы. **`quickFullscreenHotkey` в `OPTIONS` не включён** — по аналогии с уже
существующим ограничением: `quickHotkey` и `quickCompanion`, оба
`stl::StringView`, тоже отсутствуют в этом протоколе (`grep` по `test_mode.cpp`
подтверждает — единственная строковая штука там, `quickGeometry`, на самом
деле раскладывается на 8 числовых полей `.percent`/`.value`, не строка).
Причина ограничения — сам протокол `writeParts(...)` пишет `key=value` через
`(i64)(...)`, то есть каждое поле обязано быть приводимо к целому; строку
пришлось бы либо hex-кодировать отдельной командой (как `ARGV`/
`LAUNCH_COMMAND`), либо городить новый формат ответа — непропорционально
для одной опции, которая ничего не делает до T3. Раз прецедент для
`quickHotkey`/`quickCompanion` уже принят и не считается пробелом в тестах
(python-гарнитура их не проверяет через `OPTIONS`), следую ему.

## Критерии приёмки — с доказательством

1. **`./build -j 8 st pt`** — собирается, `exit 0` (лог: `[LD] {77/77} $(B)/st`).
2. **`./st -help`** показывает все семь опций — вывод приведён выше в разделе
   контракта, проверено `grep -E`.
3. **`SHITTY_TEST_BINARY=$PWD/.build/st_test python3 -m pytest tst/test_config.py`**
   зелёный: `20 passed`. Важная деталь: тест `-config`/`-version` первый раз
   упал не из-за моих правок, а из-за того, что `st_test` не был пересобран
   после правок (`./build -j 8 st pt` не трогает тестовый бинарь) — после
   `./build -j 8 st_test` все 20 тестов зелёные.
4. **Каждая новая опция не меняет поведения** — проверено через
   `run_startup_failure` (валидация границ: `-quickCornerRadius 5000`/`-1` и
   `-sidebarWidth 0`/`5000` дают `exit 255` с диагностикой в духе `border`
   (`options.cpp:655`); валидные значения `-quickCornerRadius 12
   -quickRememberFrame -sidebarTabs -sidebarWidth 333 -autoHideChrome -panes
   -quickFullscreenHotkey cmd+f` приняты и видны в `OPTIONS` протоколе один в
   один) и через сам код: ни `Composer::resize()`, ни рендереры, ни `vterm.cpp`
   не читают ни одно из семи новых полей — физически нечему меняться.
5. **Поведение без новых опций побитово прежнее** — структурно: `contentInsets()`
   нигде не вызывается (я его только объявил), `Composer::resize()` не
   тронут, `borderPixels()` не тронут. Плюс базис `./build test -k` идентичен
   до и после (см. ниже).

## `./build test -k` — сверка по именам с чистым деревом

Прогнал на двух деревьях: моей ветке (коммит `a4331e69`) и на `git worktree`
чистого `60d4808e` (родитель, без T1). Результат идентичен в обоих случаях —
**14 узлов**, одни и те же имена:

```
FAIL $(B)/obj/pty_test_helper/tst/pty_test_helper.c.o
FAIL $(B)/plt-tests.stamp
FAIL $(B)/tst/pretty-binary-branding.stamp
FAIL $(B)/python-tests/group-01.stamp
FAIL $(B)/python-tests/group-03.stamp
FAIL $(B)/python-tests/group-04.stamp
FAIL $(B)/python-tests/group-05.stamp
FAIL $(B)/python-tests/group-08.stamp
FAIL $(B)/python-tests-prod-parser/group-01.stamp
FAIL $(B)/python-tests-prod-parser/group-03.stamp
FAIL $(B)/python-tests-prod-parser/group-04.stamp
FAIL $(B)/python-tests-prod-parser/group-05.stamp
FAIL $(B)/python-tests-prod-parser/group-08.stamp
build: 14 node(s) failed, 32 requested target(s) broken
```

Все — предсуществующие (раскладка клавиатуры/кеш сборки/окружение, как
описано ловушками плана), T1 их не вносит и не убирает. Worktree сравнения
удалён после проверки (`git worktree remove --force`).

## Стиль

Диф ограничен ровно владеемыми файлами (`git status --short` после коммита —
только те 7). `clang-format -lines=<диапазон изменений>` прогнан по каждому
файлу отдельно (не полный `./style.py`, который переписывает 116 файлов);
одно расхождение нашлось и исправлено (пустая строка перед комментарием
`contentInsets()` в `composer.h`), повторный прогон — чисто.

## Что осталось не сделано (сознательно, не моя часть)

- `Renderer::update(const PaneUpdate*, size_t)` в `render.h` и реализации в
  трёх бэкендах — **T8**.
- Резервы `Insets.right`/`Insets.top` от боковой панели и автоскрытия хрома —
  **T5**, **T6**.
- Проброс `composer.opts->quickCornerRadius` в `WindowOptions` и его
  применение к `NSWindow`/`CALayer` — **T3**.
- Чтение `quickRememberFrame`/`quickFullscreenHotkey` и вся логика вокруг
  них (файл состояния, чорд фуллскрина) — **T2**, **T3**.
- `InputActions::ToggleSidebar`/`SplitVertical`/`SplitHorizontal` и
  `defaultBindings[]` — упомянуты в разделе «Контракты» плана, но
  `input_bindings.{h,cpp}` в списке владения T1 нет; согласно списку файлов
  T1 в самой задаче и явному «Что делаешь» от тимлида это не входит в мою
  волну — оставляю **T5**/**T10**, которые владеют этими файлами.

## Точки остановки

Не упёрся ни в одну — единственное существенное решение (замена
`stl::Span` на пару указатель+количество) задокументировано выше как
изменение в пределах «форм, которые я обязан продумать сам», а не как
пересмотр архитектурной развилки A2.

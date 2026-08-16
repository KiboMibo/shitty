# Разведка: окно, титлбар, quick terminal, сплиты

Дата: 2026-08-16. Проводили три параллельных исследователя по текущему дереву
(`master`, `b2cb0f58`). Код не менялся. Номера строк — на момент разведки.

Документ существует, чтобы исполнители не переоткрывали одно и то же. Ссылки
проверяйте перед использованием: дерево живое.

## Сборка на macOS (проверено)

Проект собирается локально **системным Apple clang**, Homebrew-LLVM не нужен.
Достающие зависимости — только `ragel`, `glslang`, `spirv-cross`
(`brew install ragel glslang spirv-cross`). Полная сборка `./build -j 8 st pt` —
200 шагов. Локальный CI-эквивалент из `CONTRIBUTING.md` (nix-чеки) —
**только Linux**; macOS-специфичный код ими не покрывается вообще.

## 1. Окно и титлбар на macOS

- Своё окно `PltWindow: NSWindow` — `ext/plt/platform_cocoa.mm:117`. Маска — чистая
  функция `cocoaWindowStyleMask(bool decorations)` (`:51-57`): с декорациями
  `Titled|Closable|Miniaturizable|Resizable`, без — `Borderless|Resizable`.
  Создание — `:1121`.
- **Нет ни одного вхождения** `titlebarAppearsTransparent`,
  `NSWindowStyleMaskFullSizeContentView`, `NSVisualEffectView`, `isOpaque`,
  `NSPanel`, `NSWindowLevel`, `collectionBehavior`, `NSAnimationContext`.
- `ui_csd_tabs.mm` — не полные CSD, а своя `NSView` **внутри системного
  титлбара**: контейнер добывается через `[window standardWindowButton:
  NSWindowZoomButton].superview` (`:162-169`), заголовок прячется
  `titleVisibility = NSWindowTitleHidden` (`:176-187`). Контент терминала под
  титлбар не заходит.
- Опции внешнего вида окна: только `no-decorations` (`options.cpp:98`),
  `maximized`, `geometry`, `border`. **Прозрачности/альфы/блюра нет**, альфа-канала
  в `Color` нет.

### Ключевая завязка: отступ скалярный

`borderPixels()` (`composer.cpp:141-150`) — одно число на все четыре стороны,
используется в `composer.cpp:152-176` (единственное место, где считаются ячейки),
`render_metal.mm:669`, `render_reference.cpp:452`, `vterm.cpp:1592`,
**`vterm.cpp:9662` (обратный маппинг мыши)**, `vterm_headless.cpp:182`,
`application.cpp:498`, `mouse_frontend.h:21`.

Отсюда цена вариантов:

| Вариант | Цена | Почему |
|---|---|---|
| Прозрачный титлбар «лайт» (контент не под титлбаром) | низкая | `platform_cocoa.mm` + `ui_csd_tabs.mm`, геометрия не трогается |
| Полный (`FullSizeContentView`, контент под светофором) | высокая | нужен асимметричный верхний инсет через все точки выше; плюс `willResize` (`:1575-1595`) считает снаппинг через `contentRectForFrameRect:`, который при `FullSizeContentView` вырождается |
| Автоскрытие по ховеру | самая высокая | всё из предыдущего + трекинг над титлбаром + **риск `SIGWINCH` на каждое движение мыши**, если менять число строк. Требование «геометрия постоянна, меняется только видимость» надо фиксировать явно |

Трекинг мыши есть, но только на контентной вьюхе: `PltView.tracking`
(`:130`, `updateTrackingAreas` `:228-235`, `NSTrackingActiveInKeyWindow`).

## 2. Quick terminal

Изолирован от геометрии терминала полностью — `composer.cpp`, `vterm.cpp`,
рендереры трогать не надо. Но с нуля нужно:

- **Глобальных хоткеев нет ни в каком виде**: `RegisterEventHotKey`,
  `addGlobalMonitorForEvents`, `CGEventTap`, Accessibility — ноль вхождений.
  Реалистичный выбор — Carbon `RegisterEventHotKey` (не требует Accessibility,
  работает поверх фуллскрина); `<Carbon/Carbon.h>` уже импортируется
  (`platform_cocoa.mm:27`) ради `kVK_*`.
- **В `plt::Window` нет метода «спрятать»** (`ext/plt/window.h:112-158`):
  `orderOut:` наружу не выставлен, единственный вызов — в деструкторе (`:1169`).
- `requestShow` (`:1172-1177`) жёстко делает `[window center]` — quick-окну нужен
  верхний край экрана.
- Поверх фуллскрина: `window.level` + `collectionBehavior` с
  **`FullScreenAuxiliary`** — это и есть ключ.
- Потеря фокуса уже отслеживается: `windowDidResignKey` (`:196-199`) →
  `WindowImpl::focused(false)` (`:1597-1603`) — готовая точка подцепки.
- **Архитектурное ограничение:** приложение создаёт ровно одно окно за запуск
  (`application.cpp:565-580`), `Composer` держит единственные `window`/`renderer`.
  Quick terminal реалистично делать **режимом существующего окна**, не вторым окном.
- Риск: анимация выезда против `presentsWithTransaction` и синхронного
  `displayLayer:` (`platform_cocoa.mm:219-222`, `render_metal.mm:294-309`) —
  проверять экспериментально.
- Точка подключения macOS-only модуля — по образцу `ui_csd_tabs.mm`:
  `application.cpp:581-585` под `#if defined(__APPLE__)`, список darwin-only `.mm`
  в `build.py:654-658`.

## 3. Сплиты панелей — отложены

Подложки нет: ни панелей, ни слоя раскладки. Автор вынес сплиты за скоуп в
дизайн-доке вкладок: `dev/docs/superpowers/specs/2026-08-04-tabs-design.md:37`.

Оценка — три волны:

1. **Разглобаливание геометрии в `Vterm`**: ~105 обращений к
   `composer.columns/rows` + ~20 к пиксельным полям → поля экземпляра. Риск не в
   объёме, а в `resizeGrid` (`vterm.cpp:8885+`) — лишний прогон шлёт `SIGWINCH` и
   фантомный resize-report в дочерний процесс.
2. **Модель раскладки и маршрутизация ввода**: дерево сплитов, фокус панели
   отдельно от активной вкладки (сейчас `activate()` деактивирует **все**
   терминалы, `session.cpp:404-427`), хит-тест мыши, перетаскивание разделителей.
3. **Рендер — смена контракта.** `Renderer` принимает ровно одно обновление за
   кадр и сам презентует (`render.h:24-27`); `render_metal.mm:209-211` держит одну
   сетку; каждый кадр чистится весь drawable (`:653-659`); шейдер кладёт ячейку от
   скалярного border (`render.comp:641-642`). Плюс арены глифов на один экран
   (`render_metal.mm:377-402`) — наивная реализация даст полную перезаливку
   каждый кадр. Симметрично в `render_vk.cpp` и `render_reference.cpp`.

Что уже играет за нас: `Screen` параметризован своими `columns/rows` и к
`composer` за геометрией не ходит (`screen.h:94, 188-190`); PTY уже per-session
(`session.cpp:438`, `pty.cpp:145`); арена на сессию с отложенным reaper'ом
(`session.cpp:486-528`); действия-аккорды отвязаны от конкретного терминала
(`composer.cpp:52-73`); шейдер один на оба бэкенда.

## 4. Действия и клавиши

- Действие — `enum class InputActions : u8` (`input_bindings.h:19-54`), **28 штук**,
  строковых имён нет. Вкладки покрыты полностью, действий уровня окна нет,
  панелей нет.
- Добавление действия — 5 механических правок: enum (`input_bindings.h`),
  строка в `defaultBindings[]` (`input_bindings.cpp:37-101`, отдельно под
  `__APPLE__` и `__linux__`), список слушателей (`composer.h:136-153`),
  регистрация (`composer.cpp:52-73`, **порядок значим** — `find()` возвращает
  первое совпадение), обработчик (`session.cpp:659-734` или
  `application.cpp:216-226`).
- **Биндинги не настраиваются из конфига**: `defaultBindings[]` —
  `static constexpr`, вкомпилирован. Косвенный рычаг — `remap`
  (`options.cpp:99`, `shitty.toml:88-92`), работает до биндингов.
- `cmd+d` **свободен**. `cmd+h` **до приложения не доходит**: AppKit съедает его
  пунктом меню `Hide` (`platform_cocoa.mm:824-826`), `performKeyEquivalent:` нигде
  не переопределён. Решение пользователя: горизонтальный сплит вешать на
  **`cmd+shift+d`** (как iTerm2/Ghostty), вендорное меню не трогать.

## 5. Обязательные правила проекта

- **Новая опция**: `options.cpp` (таблица + разбор), `options.h`, оба конфига
  (`shitty.toml` **и** `pretty.toml`), `application.cpp`, оба бэкенда plt,
  `test_mode.cpp:2392` (иначе не видна python-тестам). Строка документации
  строго `# CLI: -имя — описание` **с em dash**: `tst/test_config.py:52-73`
  сверяет множества `assertSetEqual` и падает при расхождении.
- **Стиль** (`STYLE.md`, `STYLE_PRJ.md`): никакого `std::` — только `stl::`;
  скобки обязательны у каждой ветки; одно выражение — одна строка; методы
  классов определяются вне тела; макросы с префиксом `SHITTY_`; форматирование
  прогонять `./style.py`, не `clang-format`.
- **Шапка нового файла** — строго MIT (`CONTRIBUTING.md:97-108`), GPL в новый
  файл не добавлять.
- **CI — шесть Linux-проверок**, macOS-код в них не попадает вообще.
- Проект принимает AI-контрибуции, но требует человека в цикле
  (`CONTRIBUTING.md:10-18`).

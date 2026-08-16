# T4. Quick-режим окна

**Ветка:** `feat/window-chrome` · **План:** `docs/plans/2026-08-17-window-chrome-quick-terminal.md`
**Коммит:** `94bf2ee4`

## Важно прочитать в первую очередь: как я проверял без скриншотов и без хоткея

Как и в T2, на машине нет разрешения `kTCCServiceScreenCapture` — скриншотов
нет физически (`screencapture -x` падает `could not create image from
display`). Дополнительно к этому: показать окно моими средствами было нечем —
хоткей делает T3 параллельно в тех же файлах, `application.cpp` я не трогаю.

Вместо реального прогона через `st -quick` + хоткей я написал три отдельные
standalone-программы (Objective-C++, компилировались `clang++ -framework
AppKit`, живут в scratch-директории, **не в дереве репозитория, не
коммитятся**):

1. `quick_window_probe.mm` — байт-в-байт копия двух статических хелперов
   (`screenUnderPointer()`, `topOfActiveScreenFrame()`) и тех же трёх строк
   (`window.level = ...`, `window.collectionBehavior = ...`), что теперь лежат
   в `platform_cocoa.mm`, прогнанные против настоящего `NSScreen`/`NSWindow`.
2. `quick_window_probe2.mm`/`probe3.mm` — разбор порядка `windowWillMiniaturize:`
   → `windowDidMiniaturize:` → `window.miniaturized` относительно
   `windowDidResignKey:`.
3. `quick_window_probe4.mm` — проверка, действительно ли снятие
   `NSWindowStyleMaskMiniaturizable` блокирует `[window miniaturize:nil]` и
   `performMiniaturize:` (то, что реально шлёт Cmd-M).

Это подтверждает API-предположения и геометрическую математику кода, а не
скомпилированный `WindowImpl` напрямую (слинковать пробник с настоящим
`libplt.a` не вышло без правки `build.py`, который занят T3, а
content-addressed кеш `.build/cas` не даёт стабильного пути к артефакту).
Дополнительная гарантия правильности проводки — успешная сборка, зелёный
`./build test -k` с тем же списком pre-existing падений, и построчный ревью
диффа (единственный изменённый файл).

Всё, что требует реального глаза на реальном `st -quick` (фуллскрин другого
приложения, второй монитор), **не проверено** — ниже указано явно у каждого
такого критерия.

## Что сделано

Всё в `ext/plt/platform_cocoa.mm`, под условием `options.quick` /
member-флагом `WindowImpl::quick`; ветка `quick = false` кода не задевает
вообще — единственная новая ветвь на пути обычного окна это `if
(placement == ShowPlacement::TopOfActiveScreen)` в `requestShowAt`, которая
раньше не существовала и не может сработать без нового значения enum.

### Уровень окна и `collectionBehavior`

```cpp
window.level = NSStatusWindowLevel;
window.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces
    | NSWindowCollectionBehaviorFullScreenAuxiliary;
```

Выбрал `NSStatusWindowLevel`, а не `NSFloatingWindowLevel` — он выше не только
обычных окон, но и `NSMainMenuWindowLevel` (меню-бар), и это тот запас
высоты, который реально нужен, чтобы стабильно выигрывать порядок отрисовки
у fullscreen-приложения на чужом Space. Пробником подтверждено:
`NSStatusWindowLevel == 25`, `> NSFloatingWindowLevel`, `>
NSMainMenuWindowLevel`.

`FullScreenAuxiliary` — то, что вообще разрешает окну появиться над чужим
fullscreen Space; `CanJoinAllSpaces` — что оно видно вне зависимости от
текущего Space, а не приколочено к тому, на котором создано. Ни то ни другое
не переключает систему на Space самого окна (в отличие от
`FullScreenPrimary`, который я намеренно не ставлю — пробником подтверждено,
что бит отсутствует).

### `TopOfActiveScreen`

Два новых `static`-хелпера рядом с точкой использования (по образцу
`modifiers()`/`inputKey()` ниже в этом же файле — паттерн уже принят в
файле для File-local функций вне анонимного namespace):

- `screenUnderPointer()` — экран, на котором сейчас курсор
  (`[NSEvent mouseLocation]` + перебор `NSScreen.screens`, `NSMouseInRect`),
  а не `window.screen`: quick-окно стартует скрытым и `window.screen` на
  свежесозданном, ни разу не показанном окне не отвечает на вопрос «где
  сейчас пользователь».
- `topOfActiveScreenFrame()` — верх `visibleFrame` этого экрана, вся ширина,
  40% высоты. `visibleFrame` уже исключает меню-бар и Dock, так что верхний
  край не заходит под них.

`requestShowAt(TopOfActiveScreen)` ставит окну этот `frame` через
`setFrame:display:NO`, дальше `makeKeyAndOrderFront:` +
`activateIgnoringOtherApps:` + `resized()` — тот же хвост, что уже был у
`requestShow()`.

Пробником (`probe.mm`) на реальном единственном экране этой машины:
ширина/высота/x/верхний край считаются верно; после `setFrame:` + показа окно
физически встаёт в запрошенный прямоугольник (с точностью до долей пикселя —
AppKit подравнивает `NSRect` к целым точкам backing store; `650.4` → `650`,
`433.6` → `434`, сумма `650+434=1084` **точно** совпадает с верхним краем
`visibleFrame` — это округление AppKit, не ошибка в математике).

### Скрытие по потере фокуса

`focused(bool value)` (вызывается из `windowDidResignKey:`/`windowDidBecomeKey:`
через `cocoaFocusImpl`) получил хвост **после** существующей доставки события
в `input->focus(value)`/`input->flush()` — событие фокуса терминалу не
подменяется и не переставляется, просто следом добавлено:

```cpp
if (!value && quick && !window.miniaturized && visible()) {
    requestHide();
}
```

### Находка по ходу: minimize-бит снят у quick-окна

Разбирая гонку `windowDidResignKey:` vs `window.miniaturized` (см. ниже),
обнаружил более чистое решение, чем защита по таймингу: quick-окно вообще не
должно уметь сворачиваться в Dock — это не то, для чего оно существует
(Spotlight/Ghostty-style панель никогда не имеет кнопки minimize). Добавил в
тот же `if (options.quick)`-блок:

```cpp
window.styleMask &= ~NSWindowStyleMaskMiniaturizable;
```

Пробником (`probe4.mm`) подтверждено на живом `NSWindow`: без этого бита и
`[window miniaturize:nil]`, и `performMiniaturize:` (это именно то, что шлёт
Cmd-M через responder chain) — no-op, `miniaturized`/`visible` не меняются.

Это закрывает находку, переданную из R1-qa в борд («Свёрнутое в Dock окно
`isVisible` считает видимым... если твоё поведение окна может привести в это
состояние — учти»): теперь оно физически не может привести в это состояние.
Заодно снимает и найденную мной гонку: пробником (`probe2.mm`/`probe3.mm`)
подтверждено, что `window.miniaturized` становится `true` только в
`windowDidMiniaturize:`, **на одно уведомление позже**, чем
`windowDidResignKey:` — то есть без снятого бита `!window.miniaturized` в
момент resign-key почти наверняка ещё `false`, и мой guard не спас бы от
`orderOut:` посреди Cmd-M-анимации. `window.miniaturized`-проверка в
`focused()` осталась как защитный fallback (комментарий это отдельно
объясняет), а не как основной механизм.

Ограничение честности: `windowDidResignKey:` в моих пробниках так и не
сработал ни разу — начиная с macOS 14 `activateWithOptions:` с
`NSApplicationActivateIgnoringOtherApps` стал no-op для процесса, который не
является уже активным (задокументированная деприкация), и несобранный
CLI-процесс не может забрать key-статус у Terminal без настоящего клика
пользователя. Порядок `WillMiniaturize`/`DidMiniaturize` относительно
`miniaturized`-флага подтверждён напрямую; порядок именно `ResignKey`
относительно них — по документированному поведению AppKit (resign key —
часть последовательности miniaturize, происходит до `DidMiniaturize`), но не
пронаблюдён вживую в этой среде.

## Изменения

| Файл | Что |
|---|---|
| `ext/plt/platform_cocoa.mm` | поле `WindowImpl::quick`; уровень окна + `collectionBehavior` + снятие `Miniaturizable` при `options.quick`; `screenUnderPointer()`/`topOfActiveScreenFrame()` + geometry-ветка в `requestShowAt()`; hide-on-blur хвост в `focused()` |

Больше ничего не менял — `window.h`, `options.h`, `application.cpp` только
читал, как предписано границами владения.

## Проверка

| Что | Как | Результат |
|---|---|---|
| Сборка | `./build -j 8 st pt` | зелёная, без предупреждений |
| Стиль добавленных строк | `clang-format -lines=<диапазон>` (Apple 21.0.0) по каждому хунку, дважды (после первого и после второго раунда правок) | 0 diff везде, кроме брейса конструктора `WindowImpl::WindowImpl` — тот же случай, что и в T2: `clang-format` сам не умеет разрывать эту скобку для конструкторов (см. `STYLE.md:82-85`), вернул её на свою строку вручную оба раза |
| Полный тестовый граф | `./build test -k` (дважды: до и после снятия minimize-бита) | `11 node(s) failed` оба раза — список сверен построчно с `docs/plans/reviews/window-chrome-R1-test.md`: те же `SIGWINCH` в `tst/pty_test_helper.c` (×2 шарда), `forbidden branding` в `pretty_binary_branding.py`, и те же пять python-тестов на восьми шардах (`test_legacy_arrow_modifier_matrix`×2, `test_soft_zero_departs_from_the_hinted_grid`, `test_sheared_tail_lands_in_the_captured_blank`, `test_darkening_scales_with_the_option`, `test_russian_shift_ctrl_c_has_no_legacy_control_sequence`). Список не вырос |
| Геометрия `TopOfActiveScreen` | standalone-пробник, байт-в-байт копия хелперов, против реального `NSScreen` | ширина/высота(40%)/x/верхний край — все PASS; окно после `setFrame:`+показ садится в прямоугольник с точностью до подравнивания AppKit к целым точкам (объяснено выше) |
| `window.level`/`collectionBehavior` | тот же пробник, те же три строки кода против реального `NSWindow` | `NSStatusWindowLevel` выше `NSFloatingWindowLevel` и меню-бара; `CanJoinAllSpaces`+`FullScreenAuxiliary` выставлены; `FullScreenPrimary` не выставлен |
| Скрытие/показ (без хоткея, вручную через `orderOut:`/`makeKeyAndOrderFront:`) | тот же пробник | `visible` корректно переключается в обе стороны на окне с уже применёнными quick-настройками уровня |
| Cmd-M / minimize на quick-окне не срабатывает | отдельный пробник, окно без `Miniaturizable` в styleMask | `[window miniaturize:nil]` и `performMiniaturize:` — оба no-op, `miniaturized`/`visible` не меняются |
| Обычный режим (`quick=false`) | построчный ревью диффа | весь новый код — либо новая свободная функция (не вызывается вне `TopOfActiveScreen`-ветки), либо под `if (options.quick)`/`if (... && quick && ...)`; ни одна существующая строка не изменена и не переставлена |

## Критерии приёмки — поимённо

| Критерий | Статус |
|---|---|
| После показа окно занимает верх экрана с курсором, во всю ширину, 40% высоты | Математика и применение `setFrame:` подтверждены пробником на реальном `NSScreen`/`NSWindow` (см. таблицу выше). **Не проверено** — что это выглядит правильно на настоящем `st -quick` после реального хоткея; нужен человек, когда T3 закончит |
| Окно всплывает поверх приложения в фуллскрине, не уводит на другой Space | Код и константы (`NSStatusWindowLevel`, `FullScreenAuxiliary`+`CanJoinAllSpaces` без `FullScreenPrimary`) подтверждены пробником как корректно выставленные и документированно означающие именно это. **Не проверено вживую** — нужен человек: развернуть любое приложение в фуллскрин, показать quick-окно поверх, убедиться что Space не переключился |
| Потеря фокуса прячет окно; терминал получает событие расфокуса как раньше | Код review: хвост в `focused()` добавлен строго после существующей доставки `input->focus/flush`, ничего не переставлено. Guard от гонки с Cmd-M закрыт снятием `Miniaturizable`. **Порядок `windowDidResignKey:` относительно `miniaturized` не пронаблюдён вживую** (см. «Ограничение честности» выше) — по документированному поведению AppKit риска нет, но это не то же самое, что видеть это глазами |
| `quick = false`: старт/показ/ресайз/фуллскрин/вкладки — как до правки | Подтверждено: `./build test -k` даёт тот же список pre-existing падений, ничего нового; построчный ревью диффа показывает, что весь новый код под `options.quick`/`quick`-условием |
| Двухмониторная проверка | **Не проверено** — на этой машине физически один экран (`screen count: 1` в выводе пробника). `screenUnderPointer()` перебирает весь `[NSScreen screens]`, логика не завязана на число экранов, но само переключение между двумя реальными мониторами нужно проверять на железе с двумя мониторами |
| `./build test` зелёный, `./style.py` чистый | `./build test -k` — тот же pre-existing счёт, что у T1/T2/F1. Стиль — `clang-format -lines=` по каждому хунку, полный `./style.py` не запускал (запрещено инструкцией и подтверждённым инцидентом волны 1) |

## За рамками / не решал сам

- Реальный визуальный прогон (фуллскрин чужого приложения, второй монитор,
  просто «как это выглядит») — заблокирован окружением (нет Screen
  Recording) и отсутствием готового хоткея (T3 параллельно). Нужен человек
  после того, как T3 сдаст `toggleQuickWindow`.
- Не трогал `application.cpp`, `build.py`, `ui_csd_tabs.mm`,
  `ui_quick_hotkey.*` — они активно менялись T3 параллельно в этом же
  рабочем дереве (не git worktree); я закоммитил только
  `ext/plt/platform_cocoa.mm`, ничего чужого в индекс не добавлял.
- Не встретил ни одной из точек остановки плана (`FullSizeContentView`,
  правка меню/`performKeyEquivalent:`, переделка `Composer`, смена
  `activationPolicy`) — соответственно, не останавливался и не спрашивал.

## Ревьюеру

Смотреть в первую очередь: блок `if (options.quick) { ... }` в конструкторе
`WindowImpl::WindowImpl` (три решения сразу — style mask, level,
collectionBehavior — с обоснованием в комментариях) и guard в конце
`focused()`. `screenUnderPointer()`/`topOfActiveScreenFrame()` — чистые
функции без побочных эффектов, самое простое место для отдельной проверки
математики, если не доверяете моему пробнику.

Главная незакрытая дыра — то, что живого прогона `st -quick` с реальным
хоткеем никто ещё не делал (ни я, ни T3 порознь не могут: у меня нет
хоткея, у T3 в этой волне — не его задача проверять поведение окна). Это
логично закрывается в `R3-qa`, когда обе задачи волны 3 сданы одновременно.

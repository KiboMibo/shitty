# R10-test — приёмка волны 10 (прозрачность окна и размытие подложки)

Проверяющий: `R10-test`. Дерево: `/tmp/r10test`, ветка `review/R10-test`.

## База

Командир назвал голову `master` = `8ce53bc9`. **Такого объекта в репозитории нет**
(`git cat-file -t 8ce53bc9` → `fatal: Not a valid object name`). Фактическая голова —
`adb5cc78 merge T10: window translucency and a blurred backdrop`. Проверка ведётся от неё.

Отчёт исполнителя: `docs/reports/T10-translucency-2026-08-21.md` (364 строки, прочитан целиком).

## Статус

_в работе_

## Мой базис (снят своими руками)

| прогон | результат |
|---|---|
| `./build -j 8 st st_test pt unit_tests pty_test_helper` | **`exit 0`** |
| `unit_tests --threads=1` | **`OK: 949`** |
| `MallocScribble=1 … -Pty:: --threads=1` | **`OK: 942, SKIP: 7`** |
| `pytest tst/test_config.py tst/test_tabs.py -q` | **`46 passed`** |

Сходится с числами командира точь-в-точь.

## Вопрос 2 — предумножение: мои мутации

Драйвер печатает число наблюдённых тестов; базис без мутации — **942**, счётчик сошёлся.

| мутация | что убрано | rc | тестов | как покраснело |
|---|---|---|---|---|
| **MA** | `premultiplyChannel` не умножает | 3 | 941+... = **942** (939 OK + 3 ERR) | `render_ut.cpp:81`, `render_reference_ut.cpp:1284`, `:1542` |
| **MC** | заливка в **шейдере** не умножается | 1 | **942** (941 OK + 1 ERR) | `render_reference_ut.cpp:1520` — настоящий Metal, `half.red` |
| **MD** | проход глифов в **шейдере** не умножается | 1 | **942** (941 OK + 1 ERR) | `render_reference_ut.cpp:1529` — настоящий Metal, `halfCell.red` |

Ни одна мутация не потеряла ни одного теста. Все три — точечное удаление **одного** множителя,
а не «сломать всё»: MC и MD трогают по одному выражению шейдера и краснят ровно по одному
утверждению каждая.

Разделяющий случай выписан в тесте буквально (`render_ut.cpp`,
`AHalfTransparentBackgroundIsMultipliedDownNotMerelyLabelled`): `edge.color.red == 64` **и**
явное `edge.color.red != mixChannel(0, 255, 128)` — то есть 64 против 127 сравниваются в самом
утверждении, а не только в комментарии.

**Предумножение подтверждаю.**
## Вопрос 3 — хит-тест подложки, и риск, о котором отчёт молчал

`PltBackdropView.hitTest:` отбрасывает аргумент `point` и возвращает `nil` **безусловно**
(`ext/plt/platform_cocoa.mm:452`) — недостижимость по построению, а не по геометрии, так что
вопрос «а на краю?» к ней просто не возникает.

Но при чтении обнаружилось то, чего в отчёте нет: подложка — **сосед** `contentView` внутри
фрейм-вью, а `styleMask` меняется **после** её вставки, в трёх местах:
`platform_cocoa.mm:1365` (quick-окно, сразу после вставки), `ui_csd_tabs.mm:359` и `:362`
(**`autoHideChrome`, в рантайме**). Если бы AppKit пересоздавал `NSThemeFrame`, подложка бы
молча пропадала.

Проверено не рассуждением, а отдельной программой (`probe/framesib.m`, `clang -Werror -Wall
-Wextra`, запущена):

```
frame view class: NSThemeFrame
after insert                        : present
after -Miniaturizable (quick window): present
after +FullSizeContentView (autoHide): present
after -FullSizeContentView          : present
contentView identity unchanged      : yes
hitTest at many points              : 0 hits (0 = недостижима)
window hitTest never returns probe  : 0 leaks
```

Сосед пережил все три смены `styleMask`, `contentView` сохранил идентичность, а хит-тест не отдал
подложку **ни в одной** из ~3900 точек — включая края и точки **вне** её прямоугольника; обход
настоящего `NSThemeFrame.hitTest:` в 900 точках тоже не вернул её ни разу.

**Остаток, который так не закрыть:** полноэкранный переход (`toggleFullScreen:`,
`platform_cocoa.mm:1666`, `:1718`) требует живого цикла событий и смены пространства. Отдан
человеку отдельным пунктом рецепта.

## Vulkan — независимый повтор приёма

Файл Vulkan здесь не собирается (заголовков нет), поэтому четыре изменённых выражения вынесены в
**свою** единицу трансляции (`probe/vkident.cpp`), собраны
`clang++ -std=c++20 -Werror -Wall -Wextra` и запущены. В отличие от исполнителя, сверка идёт не по
трём образцам, а перебором **всего куба 2²⁴** цветов, по три выражения на цвет:

```
clear 0.784314 0.392157 0.156863 alpha 1.000000
pane 0x002864c8 seam 0x012864c8
colours checked: 16777216 (all 2^24), expressions per colour: 3
mismatches vs pre-T10: 0 -> IDENTICAL
```

**Тождество подтверждаю.** `backgroundAlphaFromPercent(100) = 255` делает `premultiply()`
тождеством, а `packPaneBackground(x, 100)` кладёт в поле прозрачности ноль — то есть ровно
`packColor()`, каким он и был.

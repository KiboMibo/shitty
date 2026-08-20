# V1 — `quickCornerRadius` не давал скругления

Ветка `fix/V1-quick-corner-radius` от `0117d9f9`. Файл один: `ext/plt/platform_cocoa.mm`.

## Причина

Опция ставила `cornerRadius`/`masksToBounds` на слой content view — а это `CAMetalLayer`,
в drawable которого рисует рендерер (`makeBackingLayer`, `platform_cocoa.mm`). Инструментированный
прогон по рецепту из `docs/plans/state/manual-checks-2026-08-20.md` (`kill -USR2`, временный
`fprintf` свойств AppKit) на машине, где пользователь видел квадратные углы:

```
V1[show]     layer=CAMetalLayer corner=12.00 masks=1 layerOpaque=1 scale=2.00 mask=nil
V1[show+2s]  layer=CAMetalLayer corner=12.00 masks=1 layerOpaque=1 scale=2.00 mask=nil
```

Радиус доезжает, стоит с первого показа, никем не затирается — и на экране углы квадратные.
Значит собственный rounded-rect клип слоя не доходит до drawable, который оконный сервер
композитит прямо с этого слоя. Гипотезы «не применилось», «затёрлось при пересоздании»,
«перепутаны точки и пиксели» этим замером закрыты.

Второй дефект, из того же замера, — структурный, и он виден из одних чисел:

```
V1[show] styleMask=0xb (Titled|Closable|Resizable)
V1[show] winFrame=1728.0x434.0  viewFrame=0.0,0.0 1728.0x402.0
```

Quick-окно **титульное**, а его content view — 1728x402 внутри рамки 1728x434, на 32pt ниже
верха. То есть два верхних угла content view — не углы окна, а внутренние точки под титлбаром.
Скруглить окно с этого слоя было нельзя в принципе, независимо от Metal.

Третий факт, объясняющий, почему очевидная починка «скруглить слой выше» не заработала бы
в лоб: на момент, когда конструктор читает опцию, frame view ещё не layer-backed.

```
V1[req r=12] frameView=NSKVONotifying_NSThemeFrame layer=NIL corner=0.00 masks=0
```

## Починка

Клип поднят на один view выше — на frame view окна (`view.superview`):

* это обычный `NSViewBackingLayer`, а не металлический, поэтому работает штатное
  ancestor-маскирование поддерева слоёв, а не клип собственного drawable;
* он владеет всей рамкой окна, поэтому скругляются все четыре угла;
* контейнер титлбара тоже внутри него — замерено: `titlebarContainer=NSTitlebarView
  underFrameView=1 depth=2`, то есть титлбар попадает под тот же клип.

Так как слой на момент чтения опции ещё не существует, радиус теперь запоминается
(`WindowImpl::cornerRadius`) и применяется `applyCornerRadius()` из `requestShow()` и
`requestShowAt()` — после `makeKeyAndOrderFront:`, когда слой уже есть, и заново на каждом
показе (frame view — AppKit'овский, никто здесь не владеет тем, переживёт ли его слой скрытие).

Дополнительно `[window invalidateShadow]`: тень окна кэшируется по его форме, без сброса за
новыми скруглёнными углами остаётся прямоугольный ореол старой тени (один из подозреваемых,
названных в постановке).

`masksToBounds` только поднимается, никогда не опускается: AppKit и так держит его `YES` на
своём frame view (замерено), view не наш, а радиус 0 выражается самим радиусом.

Арбитраж `window.opaque`/`window.backgroundColor` с `CsdTabsUi::applyTitlebarColor()` не
тронут — блок кода и его комментарий перенесены дословно, `window.opaque` остаётся живой
записью решения, как это зафиксировали F2b/F2d.

Отвергнутые альтернативы:

* **`layer.opaque = NO` на `CAMetalLayer`** — лечило бы в лучшем случае нижние два угла и
  оставило бы верхние два структурно недостижимыми.
* **Сделать `CAMetalLayer` подслоем обычного backing layer** (форма, которую использует
  Ghostty: клип на view-контейнере, металлический слой внутри). Даёт тот же клип, но требует
  переселить `displayLayer:`, `needsDisplayOnBoundsChange` и синхронизацию live-resize с
  backing layer на ручной подслой — это трогает то, как рендерер отдаёт кадр. Клип на frame
  view даёт тот же результат и вдобавок покрывает титлбар, ничего не трогая.
* **Сделать quick-окно borderless** (тоже как у Ghostty) — снимает титлбар, а он в этом
  проекте нужен: на нём висят F2b/F2d (тонирование) и CSD-табы. Не мой вызов.

## Что подтверждено числами

Прогон: `SHITTY_FOR_TESTS=1 .build/st_test -config q.toml &` + `kill -USR2`, временный дамп
свойств (в коммит не входит).

| Замер | До | После |
|---|---|---|
| frame view: `cornerRadius` после показа | `0.00` | `12.00` |
| frame view: `masksToBounds` | `1` | `1` |
| `CAMetalLayer`: `cornerRadius` | `12.00` | `0.00` |
| frame view: слой в момент чтения опции | `NIL` | `NIL` (потому и отложенное применение) |
| аккорд fullscreen: square-off → restore | — | `12.00 → 0.00 → 12.00` |
| титлбар под frame view | — | `depth=2`, `underFrameView=1` |

Замер умеет краснеть: он же показывает `0.00` на непочиненном коде, `NIL` на слое, которого
нет, и различает три состояния аккорда в одном прогоне.

**Отброшенная проверка (честно):** пробовал доказать пиксели через форму окна, которую знает
оконный сервер — `+[NSWindow windowNumberAtPoint:belowWindowWithWindowNumber:]` в углах рамки.
Проверка **не умеет краснеть**: при `quickCornerRadius = 200` на окне 1728x434 точка в 20pt от
угла заведомо вне скругления, а сервер всё равно отвечает «это моё окно». Значит он отвечает по
прямоугольнику рамки, а не по композитной альфе, и как доказательство это не годится. Выброшено.

## Что остаётся человеку

Пикселей отсюда не получить: `screencapture -x` в этой среде отвечает `could not create image
from display` (Screen Recording нет), а прочитать композитный результат `CAMetalLayer` из самого
процесса нельзя. Поэтому глазами нужно посмотреть ровно одно — **скруглены ли теперь все четыре
угла quick-окна**:

```sh
printf 'quick = true\nquickHotkey = "cmd+shift+f12"\nquickCornerRadius = 12\n' > /tmp/q.toml
SHITTY_FOR_TESTS=1 .build/st_test -config /tmp/q.toml & kill -USR2 $!
```

Заодно стоит проверить два соседних эффекта, которые числами не ловятся:

1. **Тень.** Не остался ли прямоугольный ореол по углам (`invalidateShadow` должен это снять).
2. **Титлбар.** Верхние углы теперь режутся вместе с полосой титлбара — не выглядит ли срез
   грубым рядом со стандартными кнопками.
3. **Маленький радиус на титульном окне.** Если задать радиус меньше системного (`= 4`),
   визуально может выиграть системное скругление: наш клип режет меньше, чем система. Это
   ограничение титульного окна, а не регрессия.

## Проверки

* `./build -j 8 st pt st_test unit_tests plt_unit_tests` — `exit 0`.
* `plt_unit_tests` — **OK: 54**, 0 ERR (базис: 54/0). Три сторожа, `darwin_call_guard` в их числе.
* `unit_tests --threads=1` c `SHITTY_PTY_TEST_HELPER=.build/pty_test_helper` — **OK: 812, ERR: 1**,
  красный `Pty::OwnerDeathReleasesBlockedIoAndHangsUpChild`. Это объявленный не-мой красный
  (гонка `waitpid(-1)`, починена в `fix/pty-reap-own-child`); показал, что он мигает: три прогона
  подряд дали `OK: 813` / `OK: 812, ERR: 1` / `OK: 812, ERR: 1` без изменений кода. Правка живёт в
  оконном коде Cocoa, который `unit_tests` не поднимает вовсе.
* `clang-format --dry-run -Werror` по каждому изменённому диапазону отдельно (шесть диапазонов,
  по одному) — чисто.
* `./build test -k` — 13 упавших узлов. Сверены **имена**, как требует базис:
  * шесть падающих python-тестов — ровно предсуществующий набор, записанный в
    `docs/plans/state/window-chrome-status.md:74`: `test_darkening_scales_with_the_option`,
    `test_legacy_arrow_modifier_matrix` ×2, `test_russian_shift_ctrl_c_has_no_legacy_control_sequence`,
    `test_sheared_tail_lands_in_the_captured_blank`, `test_soft_zero_departs_from_the_hinted_grid`
    (расползлись по шести группам `python-tests` и пяти `python-tests-prod-parser` — номера
    шардов плавают, имена совпадают дословно);
  * `tst/pretty-binary-branding` — из того же записанного набора;
  * `tst/xterm_vttests/other_sgr` — **лишний узел**, но это таймаут: апстримный
    `other-sgr.sh` не уложился в 10 секунд под параллельной нагрузкой прогона. В одиночку
    проходит: `SHITTY_TEST_BINARY=.build/st_test SHITTY_TEST_PLATFORM=headless python3
    tst/xterm_vttests/adapter.py other-sgr.sh ...` → `PASS xterm-vttests/other-sgr.sh:
    11101 stream bytes`. Правка живёт в оконном коде Cocoa, которого этот путь не касается.

## Без юнит-теста, и почему

`WindowImpl` лежит в анонимном namespace внутри `platform_cocoa.mm`, `platform_cocoa_ut.mm` —
отдельная единица трансляции и до него не достаёт. Чтобы покрыть `applyCornerRadius()` тестом,
пришлось бы расширять контракт `ext/plt/window.h` — а это объявленная точка остановки. Проверять
же было бы нечего кроме одного присваивания свойству AppKit: вся суть правки в том, **какому**
слою и **когда** оно достаётся, и это подтверждается замером на живом окне, а не заглушкой.

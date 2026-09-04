# Liquid Glass в терминале: разведка

Задача `GLASS-recon`, 2026-09-05. Разведка read-only: кода продукта не написано, файлы
`lib/`, `bin/`, `ext/`, `build.py` не тронуты. Машина — macOS 26.5.2 (build 25F84), arm64,
SDK 26.5, Apple clang 21.0.0.

Каждое утверждение помечено: **[измерено]** — запущено и увидено; **[прочитано]** —
написано в заголовке SDK или в нашем коде, со ссылкой; **[выведено]** — рассуждение.

---

## Главный ответ

**Возможно.** Стекло ложится **под** `CAMetalLayer` в трёх раскладках из пяти
проверенных, и одна из них — ровно та позиция, где уже живёт `PltBackdropView`
(`ext/plt/platform_cocoa.mm:1326`). Условие: **`NSGlassEffectView` не должен
получать `PltView` в свой `contentView`** — единственная раскладка, которую
заголовок SDK гарантирует, единственная же кладёт стекло **поверх** текста.

Порядок слоёв измерен прямым дампом дерева `CALayer` на живом окне. **Пиксельное
подтверждение снято только для стекла без Metal**: посреди работы экран машины
заблокировался (`CGSSessionScreenIsLocked = true`), и с этого момента любой
`screencapture` отдаёт обои локскрина. Что именно осталось неснятым — в разделе
[Чего не хватает](#чего-не-хватает).

---

## Наша иерархия вью и слоёв на darwin

**[прочитано]** из `ext/plt/platform_cocoa.mm`, `lib/shitty/ui_sidebar_tabs.mm`,
`lib/shitty/ui_csd_tabs.mm`; **[измерено]** структура рамки — дампом дерева слоёв
пробника (`plain`), который повторяет ту же конструкцию.

```
NSWindow  (PltWindow)
└── NSThemeFrame                     ← «frame view»; слой появляется только после
    │                                  orderFront (applyCornerRadius это уже мерил,
    │                                  platform_cocoa.mm:1776)
    ├── CUIWindowFrameLayer
    ├── [PltBackdropView]            ← -backgroundBlur: NSVisualEffectView,
    │                                  addSubview:positioned:NSWindowBelow relativeTo:view
    │                                  (platform_cocoa.mm:1302-1326)
    ├── PltView  ==  window.contentView
    │   ├── слой вью ЕСТЬ CAMetalLayer   (makeBackingLayer, platform_cocoa.mm:213)
    │   │                                 весь текст, курсор, выделение, разделитель
    │   └── TerminalSidebarView          ← [content addSubview:view],
    │                                      ui_sidebar_tabs.mm:794; рисуется ПОВЕРХ
    │                                      CAMetalLayer — это и сказано в S10,
    │                                      ui_sidebar_tabs.mm:904
    └── NSTitlebarContainerView
        └── NSTitlebarView
            ├── TerminalTitlebarFillView ← positioned:NSWindowBelow, ui_csd_tabs.mm:489
            ├── TerminalTabBarView       ← ui_csd_tabs.mm:426
            ├── TerminalChromeHoverView  ← ui_csd_tabs.mm:338
            └── стандартные кнопки окна
```

Три места читают эту структуру и сломаются, если её переставить:

| Кто | Что читает | Ссылка |
|---|---|---|
| сайдбар | `window.contentView` и его `bounds`; вешает свою вью в него | `ui_sidebar_tabs.mm:744,794` |
| тинт хрома | `window.contentView.layer.opaque` — «прозрачно ли окно» | `ui_window_tint.h:48-49` |
| рендерер | `metalLayer.opaque` — можно ли писать альфу | `render_metal.mm:983` |

Комментарий `platform_cocoa.mm:1298-1301` называет ровно эту цену: перепарентить
`PltView` внутрь visual-effect-вью не стали, потому что это **сдвинуло бы вью,
которую читает сайдбар**. Разведка подтверждает, что запрет надо сохранить, и
показывает, что для стекла он не нужен.

---

## Что говорит заголовок SDK

**[прочитано]** `MacOSX.sdk/System/Library/Frameworks/AppKit.framework/Versions/C/Headers/NSGlassEffectView.h`,
целиком — 57 строк. Дословно:

> `/// A view that embeds its content view in a dynamic glass effect.`

> `/// - Important:` `NSGlassEffectView` `only guarantees the` `contentView` `will be placed inside the glass effect; arbitrary subviews aren't guaranteed specific behavior with regard to z-order in relation to the content view or glass effect.`

> `/// A view that efficiently merges descendant glass effect views together when they are within a specified proximity to each other.`

> `/// The glass effect container view does the following:`
> `/// 1. Elevates the z-order of descendants of` `contentView` `to position them above the` `contentView`.
> `/// 2. Merges descendants together if the views are sufficiently similar and within the proximity specified in` `spacing`.
> `/// 3. Processes similar glass effect views as a batch to improve performance.`

Весь API: `contentView`, `cornerRadius`, `tintColor`, `style` (`Regular` / `Clear`);
у контейнера — `contentView` и `spacing`. **Ни одного свойства, управляющего
z-порядком, и ни одного, задающего источник размытия.** `API_AVAILABLE(macos(26.0))`,
`API_UNAVAILABLE(macCatalyst)`.

**[выведено]** Предупреждение читается в обе стороны. Оно не обещает, что произвольная
сабвью окажется под стеклом, — но и не обещает, что она окажется над ним. Гарантии нет
**никакой**, а значит опираться на неё нельзя ни в ту, ни в другую сторону; ниже —
что происходит фактически на этой сборке.

---

## Что измерено

Пробник — `scratchpad/glass-probe/probe.m`, одиночное Cocoa-приложение вне дерева
репозитория. Окно с `CAMetalLayer`, в который блитом кладётся картинка из трёх
вертикальных зон — альфа `0`, `0.30`, `1.0` — плюс непрозрачные белые полосы
поперёк всех трёх («текст»). За окном — второе окно с цветными полосами, чёрными
волосяными линиями и крупным текстом: по ним видно и размытие, и преломление.
Собирается `clang -fobjc-arc -o probe probe.m -framework AppKit -framework Metal
-framework QuartzCore`.

### 1. Стекло на этой машине работает **[измерено]**

`NSGlassEffectView` создаётся, `NSClassFromString` его находит, вариант `glassEmpty`
(стекло с `NSTextField` в `contentView`, без Metal) даёт снимок, на котором:

- цветные полосы заднего окна **видны сквозь стекло**, размыты — волосяные линии
  исчезли полностью, слово `WINDOW` читается как расплывшееся пятно;
- стекло заметно затемняет и тонирует: красная полоса становится тёмно-бордовой,
  зелёная — тёмно-зелёной;
- `cornerRadius = 20` виден, скругление настоящее;
- надпись `glass, no metal` из `contentView` лежит **поверх** размытия и **чёткая**,
  не размытая.

Источник размытия — **то, что за окном**, не содержимое окна. То есть по смыслу
это `BehindWindow`, как у нынешнего `PltBackdropView`.

Снимок: `scratchpad/glass-probe/shots/glassEmpty-1-initial.png` (снят до блокировки
экрана, региональным захватом).

### 2. Порядок слоёв: три раскладки из пяти кладут стекло под текст **[измерено]**

Дамп дерева `CALayer` живого окна после раскладки и первой отрисовки. Порядок
`sublayers` — это и есть порядок композитинга: последний рисуется поверх.

| Раскладка | Куда попал `CAMetalLayer` | Стекло относительно текста |
|---|---|---|
| `glassContent` — `PltView` отдан в `glass.contentView` | внутрь `ContentHolderView`, **индекс 0** из 2 | **над текстом** |
| `glassSub` — `PltView` добавлен в стекло как обычная сабвью | **последним**, индекс 2 из 3 | **под текстом** |
| `glassSibling` — стекло и `PltView` — соседи в контейнере | **последним** среди соседей | **под текстом** |
| `glassBelowSibling` — стекло в `NSThemeFrame` ниже `contentView` | в своём `contentView`, **последним** в рамке | **под текстом** |
| `plain` — без стекла, эталон | единственный | — |

Те же числа, снятые отдельной печатью индексов (`sublayers` — порядок отрисовки;
все `zPosition` в этих деревьях измерены нулевыми, кроме одного служебного
разделителя AppKit в титлбаре):

```
ZORDER glassContent:      metal=0 из 1 под CALayer (ContentHolderView),
                          а сам ContentHolderView — 0 из 2 под стеклом,
                          SwiftUI-хостинг стекла — 1  →  стекло поверх
ZORDER glassSub:          metal=2 из 3 под слоем стекла               →  metal поверх
ZORDER glassSibling:      metal=1, glass=0 из 2                       →  METAL ABOVE GLASS
ZORDER glassBelowSibling: metal=0 из 1 под контейнером, а контейнер —
                          последний в NSThemeFrame после стекла        →  metal поверх
ZORDER plain / vfx:       metal=0 из 1, стекло не сосед               →  эталон
```

Дерево `glassContent` дословно (`z`, `frame`, `opaque` опущены для читаемости):

```
NSViewBackingLayer                  ← контейнер
  NSViewBackingLayer                ← NSGlassEffectView
    CALayer                         ← ContentHolderView          [индекс 0]
      CAMetalLayer                  ← наш текст
    NSViewBackingLayer              ← SwiftUI-хостинг стекла     [индекс 1 — ПОВЕРХ]
      SwiftUI.SDFLayer
        CALayer
          CALayer            filters=1
          CALayer
            CAPortalLayer
        CALayer
          CABackdropLayer    filters=1   ← собственно размытие
            CASDFLayer
              CASDFElementLayer
          SDFPortalLayer
          CASDFLayer         filters=1
```

Санкционированная заголовком раскладка — единственная, которая ставит стекло
**после** держателя контента, то есть поверх него.

`glassSub` (то, о чём заголовок предупреждает) на этой сборке даёт обратный порядок:

```
NSViewBackingLayer                  ← контейнер
  NSViewBackingLayer                ← NSGlassEffectView
    CALayer                         ← ContentHolderView (пустой)  [0]
    NSViewBackingLayer              ← SwiftUI-хостинг стекла      [1]
      ...
    CAMetalLayer                    ← наш текст                   [2 — ПОВЕРХ]
```

`glassBelowSibling` — позиция сегодняшнего `PltBackdropView`:

```
NSViewBackingLayer                  ← NSThemeFrame
  CUIWindowFrameLayer
  NSViewBackingLayer                ← NSGlassEffectView в рамке
    ...
  NSViewBackingLayer                ← contentView                 [последний — ПОВЕРХ]
    CAMetalLayer
```

**[выведено]** `CAPortalLayer` и `SDFPortalLayer` внутри стекла — механизм, которым
`NSGlassEffectView` переносит отрисовку своего `contentView` наверх; именно поэтому
надпись в `glassEmpty` осталась чёткой, хотя её слой лежит ниже стекла. Будет ли
портал корректно переносить `CAMetalLayer` — вопрос пиксельный, и он не снят.
На результат он не влияет: раскладка `glassContent` нам всё равно не годится по
другой причине (пункт 3).

### 3. Стекло как `contentView` окна навязывает окну свой размер **[измерено]**

Первая сборка пробника делала `NSGlassEffectView` содержимым окна напрямую —
и окно `640x440` вышло **`640x43`**: стекло тянет размер к интринсику своего
`contentView` через Auto Layout, и это доходит до окна. Дальше:

- `translatesAutoresizingMaskIntoConstraints = YES` на стекле **не помогло** —
  окно осталось `640x75` (43 контент + 32 титлбар);
- помогло только опустить `contentHuggingPriority` **и**
  `contentCompressionResistancePriority` до `1` по обеим осям, плюс явные
  констрейнты по четырём краям.

**[выведено]** Для терминала это существенно: размер окна там задаётся сеткой
(`resizeUnit`, `resizeBase` в `platform_cocoa.mm`), и вью, которая тянет окно
к своему интринсику, конфликтует с этим напрямую. Раскладка, где стекло — сосед
или лежит в рамке, этой проблемы не имеет вовсе: там стекло не участвует в
размерной цепочке окна.

### 4. Цена по слоям **[измерено]**

Счёт слоёв в дереве окна, один и тот же пробник:

| Раскладка | Слоёв всего | Слоёв с непустым `filters` |
|---|---|---|
| `plain` (без эффекта) | 30 | 0 |
| `vfx` (`NSVisualEffectView`, как сегодня) | 36 | 0 |
| `glassSibling` (одно стекло) | 46 | 3 |

Одна стеклянная вью — это **+16 слоёв и три слоя с фильтрами** против **+6 слоёв
и ноль фильтров** у нынешнего матового стекла.

**[выведено]** Это верхняя оценка структуры, а не времени кадра: размытие считает
window server, вне нашего процесса, и три `filters=1` — это три прохода, которые
он обязан выполнить на каждом кадре окна. Заголовок сам называет это ценой,
рекомендуя `NSGlassEffectContainerView` «to improve performance by reducing the
number of passes required to render similar glass effect views» — то есть проходы
на стеклянную вью существуют и складываются.

---

## Чего не хватает

**Пиксельного подтверждения для трёх раскладок, где Metal оказался сверху.**
Порядок слоёв измерен, картинка — нет.

Что помешало, по порядку и честно:

1. `screencapture -R` (региональный захват) **потерял права** посреди сессии:
   вместо файла — `could not create image from rect`, при этом полноэкранный
   `screencapture` продолжал работать. Причина видна: система показала диалог
   «"unknown" is requesting to bypass the system private window picker and directly
   access your screen and audio», и он не был подтверждён.
2. Полноэкранный захват с подложкой во весь экран заработал — и почти сразу
   **экран машины заблокировался**. `ioreg -n Root -d1` даёт
   `CGSSessionScreenIsLocked = <true/>`. С этого момента `screencapture` отдаёт
   обои локскрина, а до того, пока дисплей просто спал, — сплошной чёрный кадр,
   **с кодом возврата 0 в обоих случаях**.

Пробник и скрипт `scratchpad/glass-probe/run-when-unlocked.sh` оставлены готовыми:
скрипт ждёт разблокировки и сам прогоняет все восемь вариантов плюс бенчмарк.
Достаточно разблокировать экран и посмотреть `shots/`.

**Времени кадра.** Не измерено и на заблокированном экране измеряться не может:
композитинга не происходит. Способ, заложенный в пробник (`--bench N`): рисовать
кадры в цикле и мерить достигнутый fps — если window server не успевает со
стеклом, `nextDrawable` начинает блокировать и частота падает. Стоит один прогон
на разблокированном экране, порядка минуты на четыре раскладки. Второй,
независимый способ — снять `%cpu` процесса `WindowServer` с эффектом и без;
он не требует ничего, кроме `ps`.

---

## Инварианты `A1` и `A8`

**[прочитано]** `docs/architecture/2026-08-18-panes-and-window-chrome.md:86` (`A1`),
`:263` (`A8`).

- **`A1` — отступ пользователя и резерв под хром — разные понятия.** Стекло **не
  задевает `A1`**, если оно не занимает места. Ни одна из трёх годных раскладок не
  добавляет резерва: стекло лежит на всю площадь, под текстом, и `contentInsets()`
  о нём не знает. `A1` сработает только в варианте «стеклянная рамка вокруг текста»
  — там появляется новый резерв хрома, и его обязана вернуть `chromeReserve()`,
  а не `border`. Прямой запрет из `A1` («*Оставить скаляр, а панель рисовать поверх
  сетки — текст уезжает под панель, попадание мыши врёт*») к стеклу под текстом
  не относится: стекло ничего не перекрывает.
- **`A8` — `Vterm` получает свою геометрию, а не читает окно.** Стекло **не задевает
  `A8`** ни в одной раскладке: оно не меняет ни `columns_`/`rows_`, ни
  `originX_`/`originY_`, ни пересчёты «пиксель → ячейка» в `mouse_frontend`.
  `A8` заденется только если стекло потребует перепарентить `PltView` — а именно
  этого разведка и не рекомендует.
- **Что стекло задевает вместо них** — не `A1`/`A8`, а три чтения из таблицы выше
  (`ui_sidebar_tabs.mm:744`, `ui_window_tint.h:48`, `render_metal.mm:983`). Все три
  выживают, пока `window.contentView` остаётся `PltView`, а его слой —
  `CAMetalLayer`. Все три ломаются от перепарентинга.

---

## Варианты

### Вариант A. Стекло в рамке, ниже `contentView` — рекомендуется

Ровно там, где сегодня `PltBackdropView` (`platform_cocoa.mm:1326`), тем же
`addSubview:positioned:NSWindowBelow relativeTo:view`, только
`NSGlassEffectView` вместо `NSVisualEffectView`.

- **Даёт:** стекло под всей поверхностью окна — под текстом, под курсором, под
  разделителем панелей, и заодно под титлбаром (то, ради чего эта позиция и
  выбиралась в S10).
- **Стоит:** +16 слоёв, 3 прохода фильтра на кадр окна **[измерено структурно,
  не по времени]**. Плюс `@available(macOS 26.0, *)` и ветка отката на
  `NSVisualEffectView` для macOS 15 и ниже.
- **Ломает:** ничего из трёх чтений. `window.contentView` не двигается,
  `contentInsets()` не меняется, `A1`/`A8` не задеты. Комментарий
  `platform_cocoa.mm:1298-1301` остаётся верным и не требует правки.
- **Риск:** не снят пиксельно. Структурно порядок верный; поведение
  `CABackdropLayer`, когда стекло лежит в приватной рамке AppKit, а не в обычной
  иерархии контента, — единственное, что осталось проверить. `NSVisualEffectView`
  в этой позиции работает уже сегодня, что делает риск небольшим **[выведено]**.

### Вариант B. Стекло — сосед `PltView` внутри `contentView`

Контейнерная вью становится `contentView`, в неё кладутся стекло и `PltView`.

- **Даёт:** то же стекло под текстом, но не под титлбаром.
- **Стоит:** столько же по слоям, плюс **`window.contentView` перестаёт быть
  `PltView`** — а это ровно те три чтения. `ui_sidebar_tabs.mm` начнёт вешать
  панель на контейнер (может и сработать), `ui_window_tint.h` начнёт спрашивать
  `opaque` у слоя контейнера вместо `CAMetalLayer` — тихо неверный ответ.
- **Вердикт:** платит цену, которой вариант A не платит, и даёт меньше. Брать
  только если A провалится пиксельно.

### Вариант C. `PltView` в `glass.contentView`

Единственная раскладка, гарантированная заголовком.

- **Измерено:** кладёт стекло **поверх** текста, и вдобавок навязывает окну свой
  размер через Auto Layout.
- **Вердикт:** отказ. Не «рискованно» — измеренно неверный порядок.

### Вариант D. Стекло только под хромом

Стеклянные подложки под сайдбар и под титлбарную полосу, текст без стекла.

- **Даёт:** современный вид хрома дёшево, `NSGlassEffectContainerView` сольёт
  соседние стёкла в один проход **[прочитано]**.
- **Стоит:** новый резерв хрома, если стекло шире нынешних полос — **вот здесь
  `A1` действительно задевается**.
- **Вердикт:** это не то, что заказано («включая поверхность под текстом»).
  Годится как запасной вариант, если пиксельная проверка A и B провалится.

### Вариант E. Отказ

Оснований нет: порядок слоёв складывается в нужную сторону, и делается это в
позиции, которую проект уже использует.

**Рекомендация: вариант A**, с обязательным пиксельным прогоном пробника перед
планированием — он уже собран и ждёт разблокированного экрана.

---

## Обнаружено

Всё, что узнано сверх вопроса.

### Комментарий `platform_cocoa.mm:1634` — не про Liquid Glass

В брифе он назван местом «про цену кадра по дороге к стеклу». Прочитан контекст:
это `WindowImpl::draw()`, и речь про `CVDisplayLink`, который перестали
останавливать между кадрами.

> `// Idle frames coast for a while before the link stops. Starting one costs a thread wake and a sync to the display, and a terminal redraws in bursts paced by the user - a full-screen TUI repaints once per keystroke - so stopping between them made every repaint pay that price on its way to the glass.`

«Стекло» здесь — **экран**, стекло дисплея. К `NSGlassEffectView` отношения не
имеет, и **измеренной цены кадра в числах там нет** — ни одной. Место, где такие
числа есть, разведка не нашла.

### Именование нового ObjC-класса

Префиксы в дереве: `Plt*` (`PltView`, `PltWindow`, `PltBackdropView`,
`PltWindowDelegate`, `PltDisplayLinkTarget`) и `Terminal*` (`TerminalSidebarView`,
`TerminalTabBarView`, `TerminalTitlebarFillView`, `TerminalChromeHoverView`) — оба
нейтральны. `tst/pretty_binary_branding.py` ищет `shitty` в **опущенном в нижний
регистр** содержимом бинарника **после `strip`**, то есть имя класса вида
`ShittyGlassView` покраснело бы, а `PltGlassView` — нет. Новому классу подходит
`PltGlassView`, по соседству с `PltBackdropView` в том же файле.

### Инструменты вокруг снимков врут кодом возврата

Три отказа подряд, каждый с `exit 0`. Стоили полутора часов:

- `screencapture` печатает `cannot write file to intended destination` и
  **возвращает 0**. Спотыкается, в частности, об имя файла, начинающееся с точки.
- `sips` печатает `Warning: ... not a valid file - skipping` и **возвращает 0**.
- при спящем дисплее `screencapture` пишет **сплошной чёрный** PNG, `rc=0`;
  при заблокированном экране — **обои локскрина**, `rc=0`.

Единственный работающий критерий — проверять, что выходной файл появился,
и смотреть на него. Проверка `CGSSessionScreenIsLocked`:

```
ioreg -n Root -d1 -a | grep -A1 CGSSessionScreenIsLocked
```

### Пробник поймал сам себя на ложном отрицании

Первый прогон `glassContent` дал совершенно пустой снимок — «стекло скрыло Metal».
Вывод был бы неверным: `CAMetalLayer` в этой раскладке имел `drawableSize = 0x0`
и **не нарисовал ни одного кадра**. Причина — `NSGlassEffectView` перепарентил вью
в свой `ContentHolderView` и разложил её так, что `-setFrameSize:` не был вызван,
а пробник брал размер drawable оттуда.

Поймано приёмом, который в этом репозитории уже описан: **премисса внутри теста**
(`CLAUDE.md`, раздел про вырожденную фикстуру). Пробник теперь печатает перед
каждым снимком `framesDrawn`, `drawableSize`, кадр вью в координатах окна,
`hiddenOrHasHiddenAncestor` и `alphaValue`, и пустая картинка больше не может
означать «мы ничего не рисовали». Оба факта — что пустой снимок был, и что он был
ложным — оставлены здесь намеренно.

### Оконный менеджер на этой машине двигает чужие окна

Свежесозданное окно `640x472` в позиции `(320, 320)` через долю секунды оказывалось
`854x75` в `(869, 1003)` **[измерено]** — окно переносилось и меняло размер без
участия пробника. Лечится `window.level = NSFloatingWindowLevel`, `movable = NO` и
`collectionBehavior` со `Stationary | IgnoresCycle | FullScreenAuxiliary`. Любой
будущий интерактивный тест окна на этой машине наступит на то же.

### Стекло не даёт собственного `CAFilter` до раскладки

Дамп дерева слоёв сразу после `makeKeyAndOrderFront:` показывал у контейнера
**ноль** сабслоёв: CoreAnimation привязывает слои сабвью позже. Дамп до
`layoutIfNeeded` показывает пустоту и выглядит как «стекла нет». Мерить дерево
слоёв — только после раскладки и первой отрисовки.

### Опция стекла придётся заводить в четырёх местах, а не в двух

Нынешняя пара опций заведена согласованно, и это готовый образец: `backgroundBlur`
живёт в `lib/shitty/options.cpp:78` (описание), `:1191` (чтение), `:1204`
(взаимная проверка с `backgroundOpacity`), `lib/shitty/options.h:186`,
`lib/shitty/application.cpp:1137` (передача в `plt`) — и **в обоих** конфигах,
`bin/st/shitty.toml:21` и `bin/pt/pretty.toml:21`, слово в слово. Проверено: файлы
сегодня не разошлись.

Четвёртое место, которое легко забыть: `lib/shitty/test_mode.cpp:2585` печатает
`background_blur=` в общей строке дампа опций. Новая опция стекла без записи туда
не будет видна питоновскому набору.

### Файлы разведки

- `scratchpad/glass-probe/probe.m` — пробник, восемь раскладок, дампы дерева вью и
  дерева слоёв, премисса перед снимком, режим `--bench N`.
- `scratchpad/glass-probe/run-when-unlocked.sh` — ждёт разблокировки и прогоняет всё.
- `scratchpad/glass-probe/shots/` — снимки.

Ничего из этого не лежит в дереве репозитория.

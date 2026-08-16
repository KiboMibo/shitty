# QA-отчёт: приёмка волны 1, круг 2 (F1)

**Дата:** 2026-08-17
**Область:** коммит `926970c1` («F1: revert vendor reflow, add plt::Window::visible()»), диапазон `1358e5a4..HEAD`
**Предыдущий круг:** `docs/plans/reviews/window-chrome-R1-qa.md` (готово с замечаниями, две важные находки)
**Вердикт:** готово

## Резюме

Обе важные находки первого круга закрыты, и закрыты правильно. Откат рефлоу проверен
построчно: все четыре места в `ext/plt` совпадают с `master` дословно, а в диффе к `master`
остались только новые методы контракта. `visible()` закрывает исходную дыру не флагом, а
опросом самой `NSWindow` — то есть T3 узнает правду независимо от того, как именно T4
спрячет окно. Регрессии нет: список падений `./build test -k` идентичен первому кругу,
`pytest tst/test_config.py` — 20 passed, ручные сценарии дают прежние числа.

Блокирующих и важных находок нет. Три замечания, все — про будущие волны, не про F1.

**Оговорка о среде:** T2 уже начала править `platform_cocoa.mm` и `ui_csd_tabs.mm` в рабочем
дереве. Все замеры этого отчёта сделаны либо до появления её правок (дерево кода тогда было
чистым, проверял `git status`), либо в отдельном `git worktree` на коммите `926970c1` — то
есть относятся строго к F1 и работой T2 не загрязнены.

## 1. Откат рефлоу — закрыт, проверен построчно

`git diff master -- ext/plt/platform_wayland.cpp ext/plt/platform_headless.cpp` содержит
**только** содержательные хунки: объявления `requestHide`/`requestShowAt`/`visible` в классах,
их определения, поле `shown_` в headless. Ни одного хунка форматирования.

Каждое из четырёх испорченных мест сверил с `master` отдельно, сравнивая содержимое строк, а
не факт отсутствия в диффе:

| Место | Что было | Сейчас |
|---|---|---|
| `platform_wayland.cpp:358` | добавлена пустая строка после `scheduler_->spawn(...)` | совпадает с `master` |
| `platform_wayland.cpp:2095` | брейс конструктора `StreamInput::StreamInput` перенесён на свою строку | совпадает с `master` |
| `platform_wayland.cpp:2552` | `zxdg_toplevel_decoration_v1_set_mode(...)` схлопнут в ~180 символов | совпадает с `master` (ручной перенос по аргументу на строку восстановлен) |
| `platform_headless.cpp` | удалена пустая строка перед `ClipboardHeadless::write()` | совпадает с `master` |

**Откат ничего нужного не задел.** Заглушки T1 (`requestHide`, `requestShowAt`) на месте во
всех трёх бэкендах вместе со своими комментариями; сборка `./build -j 8 st pt` зелёная.

Отдельно проверил, что новый код F1 форматтеру соответствует: прогнал `./style.py` в
изолированном worktree и посмотрел, попадают ли под рефлоу строки F1 — **не попадают** ни в
одном из четырёх файлов. Сам факт, что `style.py` снова хочет тронуть `platform_wayland.cpp`
(11 строк) и `platform_headless.cpp` (1 строка), — подтверждение отката: эти файлы вернулись к
ручному форматированию `master`, которое форматтер не одобряет и в чистой ветке тоже.

Замечание к отчёту F1: строка «в `platform_cocoa.mm` откатанных хунков не было (грязи там не
нашли)» — верна, в первом круге рефлоу нашёлся только в `platform_wayland.cpp` и
`platform_headless.cpp`.

## 2. `visible()` — дыра закрыта

### Cocoa: источник истины — сама NSWindow, а не флаг

`WindowImpl::visible()` (`ext/plt/platform_cocoa.mm:1350`) возвращает `window.visible`, то есть
`-[NSWindow isVisible]`. Это ключевое свойство решения: состояние не дублируется в приложении,
поэтому его нельзя рассинхронизировать.

Unit-тестом Cocoa-путь не покрывается (`WindowImpl` — тип в анонимном namespace, в
`platform_cocoa_ut.mm` тестируются только чистые функции), поэтому системный факт, на котором
всё держится, проверил отдельной программой на живом `NSWindow`:

```
fresh:             visible=0
after show:        visible=1
after orderOut:    visible=0
after reshow:      visible=1
after miniaturize: visible=1
```

**Разбор исходного сценария, ради которого находка заводилась:**

| Шаг | Что происходит | Что вернёт `visible()` | Верно? |
|---|---|---|---|
| Старт с `quick = true` | `requestShow()` не вызывается (`application.cpp:501`) | `false` (строка «fresh» выше) | да |
| Хоткей #1 | T3 видит `false` → `requestShowAt(TopOfActiveScreen)` | `true` | да |
| Потеря фокуса | T4 в `windowDidResignKey` прячет окно | `false` (строка «after orderOut») | **да — это и было сломано** |
| Хоткей #2 | T3 видит `false` → показывает | `true` | да |

Дыра закрыта. Существенно, что T4 может прятать окно и через `requestHide()`, и напрямую
`[window orderOut:nil]` — в обоих случаях `isVisible` станет `NO`, то есть корректность не
зависит от дисциплины T4. Именно это и требовалось.

### Headless: реальный toggle — проверен, а не принят на слово

Решение дать headless настоящее состояние вместо заглушки считаю правильным: это
единственный бэкенд, доступный тестовому прогону, и без него `visible()` там врала бы после
первого показа.

Toggle проверил не чтением, а прогоном: собрал `plt_unit_tests` в изолированном worktree с
временным тестом, проверяющим все три пути и идемпотентность:

```
+ PlatformHeadless::QaVisibleTogglesOnEveryShowHidePath
OK: 3, SKIP: 51
```

Тест проверяет: стартовое `false`; `requestShow()` → `true`; `requestHide()` → `false`;
`requestShowAt(Centered)` → `true`; `requestShowAt(TopOfActiveScreen)` → `true`; повторный
`requestHide()` и повторный `requestShow()` состояние не портят. **Все три пути корректны.**

Убедился, что тест не зелёный впустую, — двумя мутациями, обе откачены:

1. `requestShowAt()` перестаёт вызывать `requestShow()` → тест краснеет (`ERR: 1`);
2. `requestHide()` перестаёт сбрасывать `shown_` → тест краснеет (`ERR: 1`).

### Wayland: честен сегодня, но расходится с буквой контракта

`visible()` возвращает `shown`, который выставляется в `requestShow()`
(`platform_wayland.cpp:2700`) и не сбрасывается, потому что `requestHide()` там no-op.
Рассуждение в комментарии верное: раз hide ничего не скрыл, окно осталось видимым, и
`visible()` говорит правду о фактическом состоянии.

Ловушкой **сегодня** это не является: `WindowOptions::quick` — Cocoa-only, `requestHide()` на
Wayland никто не вызывает. Но см. замечание 1 ниже — расхождение с формулировкой контракта
стоит снять словами, пока оно дешёвое.

## 3. Регрессия — нет

| Проверка | Результат | Сверка с кругом 1 |
|---|---|---|
| `./build -j 8 st pt` | exit 0 | — |
| `./build test -k` | `11 node(s) failed, 29 requested target(s) broken` | множество упавших тестов **идентично** (`diff` пуст) |
| `python3 -m pytest tst/test_config.py` | **20 passed** | было 18; +2 от R1-test, как и ожидалось |
| `./st -quick -e sleep` | 0 окон, процесс жив, rc=0 | было 0 |
| `./st -e sleep` (без опций) | 1 окно, rc=0 | было 1 |
| `./st -quick`, размер PTY | `80x24` | было `80x24` |
| `plt_unit_tests` (включая `PlatformHeadless`) | `OK: 53` | зелёные |

Список упавших тестов тот же самый, что и в первом круге, и тот же, что на чистом `master`:
`test_darkening_scales_with_the_option`, `test_legacy_arrow_modifier_matrix` (×2),
`test_russian_shift_ctrl_c_has_no_legacy_control_sequence`,
`test_sheared_tail_lands_in_the_captured_blank`, `test_soft_zero_departs_from_the_hinted_grid`.

## Находки

### Блокирующие

Нет.

### Важные

Нет.

### Замечания

**1. Контракт `visible()` обещает больше, чем даёт Wayland** — `ext/plt/window.h:178-184`,
`ext/plt/platform_wayland.cpp:2701-2707`

Комментарий контракта: «True between a requestShow()/requestShowAt() and the next
requestHide()». На Wayland после `requestHide()` `visible()` вернёт `true` — то есть буква
контракта и реализация одного из трёх бэкендов расходятся. Сегодня безвредно, но читающий
только `window.h` вправе ожидать обратного.

Вторая половина того же: предупреждение стоит в `visible()`, а править будет тот, кто возьмётся
за `requestHide()`. При этом в `requestShow()` есть ранний выход `if (shown) { return; }`
(`:2694`) — реализовав настоящий hide и забыв сбросить `shown`, автор получит не только
врущий `visible()`, но и окно, которое не показывается обратно.

Предлагается (дёшево, словами): в `window.h` добавить оговорку «бэкенд, где `requestHide()`
не скрывает окно, возвращает фактическую видимость», а предупреждение про сброс `shown`
перенести или продублировать в теле `requestHide()` в `platform_wayland.cpp` — туда, где его
прочитает будущий реализатор.

**2. `visible()` не покрыт ни одним тестом репозитория** — `ext/plt/platform_headless.cpp:383`

R1-test писался до F1, поэтому нового метода не касается; мой проверочный тест жил во
временном worktree и в репозиторий не попал. Метод сейчас не вызывается вообще нигде
(проверил `git grep`) — первым его вызовет T3.

Предлагается: завести этот тест в `R3-test` (или раньше, если будет ещё одна фикс-задача
волны 1). Готовый текст, ловящий обе мутации, — в конце отчёта; его можно взять как есть.

**3. Мелочи для W3, не для F1:**

- Свёрнутое в Dock окно `isVisible` считает видимым (`after miniaturize: visible=1`). Если
  quick-окно окажется сворачиваемым, хоткей на свёрнутом окне спрячет его вместо показа.
  T4 стоит либо убрать кнопку сворачивания у quick-окна, либо учесть `info().iconified`.
- `WindowHeadlessImpl::requestClose()` не сбрасывает `shown_`: закрытое окно остаётся
  «видимым». Для тестов W3, которые будут закрывать окно и проверять состояние, это может
  оказаться неожиданностью.

## Что проверено и оказалось в порядке

- Откат рефлоу — построчной сверкой с `master`, а не по отсутствию в диффе.
- Заглушки T1 и их комментарии переживают откат без потерь.
- Стиль новых строк: `./style.py` в изолированном worktree их не трогает.
- Расположение `ShowPlacement` — перенесён к двум другим enum файла (замечание круга 1 закрыто
  попутно, хотя об этом не просили).
- `visible()` объявлен рядом с `info()`, тем же порядком, что и прочие `const`-геттеры —
  форме `plt::Window` соответствует.
- Утверждения отчёта F1 о содержании правок сверены с диффом — расхождений не нашёл.
- Поведение по умолчанию: без новых опций окно, PTY и набор падающих тестов прежние.

## Не проверено и почему

- **Linux/Wayland-сборка** — по-прежнему не подтверждена: машина macOS, локального IX-реалма
  нет. Пробел тот же, что в первом круге; проявится на первом пуше в CI.
- **Живой сценарий hide-on-resign-key** — T4 ещё не написана, прятать окно некому. Проверено
  на уровне системного поведения `-[NSWindow isVisible]`, чего для вывода достаточно.

## Приложение: тест, которым проверялся headless-toggle

Ловит обе мутации (`requestShowAt` без показа, `requestHide` без сброса). Место —
`ext/plt/platform_headless_ut.cpp`, внутрь `STD_TEST_SUITE(PlatformHeadless)`:

```cpp
    STD_TEST(VisibleTogglesOnEveryShowHidePath) {
        auto pool = ObjPool::fromMemory();
        Platform* const platform = createHeadlessPlatform(*pool);
        auto& window = static_cast<WindowHeadless&>(*platform->createWindow(
            *pool,
            {
                .width = 2,
                .height = 2,
            }
        ));
        STD_INSIST(!window.visible());
        window.requestShow();
        STD_INSIST(window.visible());
        window.requestHide();
        STD_INSIST(!window.visible());
        window.requestShowAt(ShowPlacement::Centered);
        STD_INSIST(window.visible());
        window.requestHide();
        STD_INSIST(!window.visible());
        window.requestShowAt(ShowPlacement::TopOfActiveScreen);
        STD_INSIST(window.visible());
        window.requestHide();
        STD_INSIST(!window.visible());
    }
```

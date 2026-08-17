# Тесты: волна 3 window-chrome (T3 хоткей, T4 quick-режим) — PASS_WITH_FINDINGS

- **Дата:** 2026-08-17
- **Статус:** PASS_WITH_FINDINGS
- **Скоуп:** волна 3-ревью плана `docs/plans/2026-08-17-window-chrome-quick-terminal.md` —
  тесты на новую логику T3 (`ui_quick_hotkey.mm`) и T4/F1 (`toggleQuickWindow`,
  `plt::Window::visible()`). Проверяемые коммиты: `9fc18e2f` (T3), `94bf2ee4` (T4),
  `b2001f53` (F2)
- **Команда прогона:** `.build/unit_tests --threads=1 ToggleQuickWindow`,
  `.build/ext/plt/plt_unit_tests PlatformHeadless` (без `--threads=1`, см. «Находки»)

## Итог

Добавлено 5 тестов: 4 на `toggleQuickWindow()` (`application_ut.cpp`) и 1 на
`plt::Window::visible()` (`ext/plt/platform_headless_ut.cpp`, готовый текст R1-qa,
взят как есть). Все зелёные, все чувствительны к точечным дефектам. Список
пре-существующих падений `./build test -k` не вырос — сверено независимо на
интегрированном состоянии волны 3.

Единственная новая **чистая** логика, которую план требовал покрыть буквально —
разбор чорда `quickHotkey` — **физически недоступна** для юнит-теста: это `static`
функция в анонимном namespace `ui_quick_hotkey.mm`, нигде не объявленная. Не правил
чужой файл, чтобы её оттуда вынести — это находка ниже, с готовой формой
извлечения. R3-sec независимо наткнулся на цену этого пробела (находка S1) —
привожу как аргумент, не как повод исправить самому.

## Что покрыто

| Файл | Тесты | Кейсы |
|------|-------|-------|
| `application.cpp:525-535` (`toggleQuickWindow`) | `application_ut.cpp`, suite `ToggleQuickWindow` — 4 новых `STD_TEST` | `composer.window == nullptr` — no-op; скрытое окно показывается (`requestShowAt`); видимое окно прячется (`requestHide`); видимое **и свёрнутое в Dock** окно — показывается заново, а не прячется (регрессионный тест на находку R1-qa/T3) |
| `ext/plt/platform_headless.cpp` (`visible()`, `requestShow`/`requestHide`/`requestShowAt`) | `ext/plt/platform_headless_ut.cpp`, suite `PlatformHeadless` — 1 новый `STD_TEST` (`VisibleTogglesOnEveryShowHidePath`, текст R1-qa) | полный цикл: старт скрыт → show → hide → showAt(Centered) → hide → showAt(TopOfActiveScreen) → hide, на каждом шаге проверка `visible()` |

Полный список новых тестов:

- `ToggleQuickWindow::NullWindowIsANoOp`
- `ToggleQuickWindow::HiddenWindowIsShownByToggle`
- `ToggleQuickWindow::VisibleWindowIsHiddenByToggle`
- `ToggleQuickWindow::IconifiedVisibleWindowIsShownRatherThanHiddenByToggle`
- `PlatformHeadless::VisibleTogglesOnEveryShowHidePath`

**Почему `toggleQuickWindow`, а не сам чорд.** `toggleQuickWindow(Composer&)`
(`application.cpp:525`) — единственная функция волны 3, которая (а) новая, (б)
содержит настоящее ветвление («новинка» этой волны — учёт `info().iconified`,
закрывающий находку R1-qa/T3) и (в) **не guard'ится `#if defined(__APPLE__)`** —
компилируется и работает на headless-бэкенде без единого макоса. Пробовать
имитировать «живое окно» глазами (T3/T4 договорились не делать этого в тестах)
не пришлось: headless — уже принятый в проекте суррогат живого окна
(`R1-qa`-прецедент теста `visible()`), и `toggleQuickWindow` работает через
портируемый интерфейс `plt::Window`, ничего Cocoa-специфичного не касаясь —
глобальный хоткей физически не регистрируется в этом тесте, поскольку сам
Carbon-код лежит отдельно, в `ui_quick_hotkey.mm`, и этой функцией не
вызывается.

## Что не покрыто и почему (буквально из задания)

Живое окно, системный фокус, глобальный хоткей — не покрыты, как и было
сказано в задании. Уточнение по каждому:

- **Реальная регистрация Carbon-хоткея** (`RegisterEventHotKey`,
  `InstallEventHandler` в `QuickHotkeyUi`) — намеренно не трогал вообще:
  вызов этого кода из теста реально захватил бы системный чорд на машине,
  где параллельно работают другие агенты и человек. Это не гипотетический
  риск: `RegisterEventHotKey` **не идемпотентен относительно теста** — он
  меняет состояние всей сессии Aqua, а не только процесса.
- **`WindowImpl` (Cocoa)** — уровень окна, `collectionBehavior`, снятие
  `Miniaturizable`, hide-on-resign-key — тип в анонимном namespace
  `platform_cocoa.mm`, `platform_cocoa_ut.mm` тестирует только чистые функции
  вроде `cocoaWindowStyleMask()` (тот же прецедент, что и в R1-qa для
  `visible()`). Не тронул `platform_cocoa_ut.mm` вообще — там за волну 3 не
  появилось ни одной новой чистой функции без побочных эффектов на реальном
  `NSWindow`.

## Находки

### 1. `parseQuickHotkey` не вынесена в тестируемый вид — важная

- **Где:** `ui_quick_hotkey.mm:132-158` (`parseQuickHotkey`), `:114-127`
  (`quickHotkeyModifierBit`), `:32-110` (`quickHotkeyKeyNames[]`) — всё в
  анонимном `namespace { ... }`, ничего не объявлено в `ui_quick_hotkey.h`
- **Чем грозит:** это единственная новая чистая логика волны (чистая функция
  строка → `(modifiers, keyCode)`, без побочных эффектов), и план прямо требует
  её покрыть тестами на валидные чорды, регистр, неизвестный модификатор,
  пустую строку, «только модификаторы без клавиши». Ни один из этих кейсов
  сейчас не проверяется автоматически — вся проверка держится на ручном
  разборе R3-sec (фаззинг под ASan/UBSan, 300k случайных строк — покрывает
  «не падает», а не «разбирает правильно») и на живых прогонах T3
  (`quickHotkey = "ctrl+nonsense"` — один кейс из десятков возможных).
  **Не гипотетическая цена:** R3-sec независимо нашёл находку S1
  (`docs/plans/reviews/window-chrome-R3-sec.md`) — чорд без модификатора
  (`quickHotkey = "space"`) молча захватывает клавишу глобально во всей
  системе, потому что `parseQuickHotkey` для однотокенной строки не проверяет
  наличие модификатора. Юнит-тест с кейсом «только клавиша, без модификатора»
  (ровно то, что просит план: «только модификаторы без клавиши» — зеркальный
  случай) обнаружил бы это на этапе R3-test, а не постфактум в секьюрити-ревью.
- **Почему не починил сам:** файл не в списке владения R3-test (`ui_quick_hotkey.mm`
  принадлежит T3/F3), и правка ради тестируемости — не тестовая инфраструктура,
  а изменение поведения производственного файла. К моменту этого отчёта в
  рабочем дереве уже идёт незакоммiченная работа F3 над этим же файлом (см.
  «Допущения») — тем более не время лезть туда параллельно.
- **Предлагается (форма извлечения, дёшево и без риска):**
  1. Перенести `quickHotkeyKeyNames[]`, `quickHotkeyModifierBit()`,
     `parseQuickHotkey()` из анонимного namespace `ui_quick_hotkey.mm` в новую
     пару файлов `quick_hotkey_chord.h`/`quick_hotkey_chord.cpp` — **обычный
     `.cpp`, не `.mm`**: `<Carbon/Carbon.h>` (`UInt32`, `kVK_*`) — чистый Си-заголовок,
     Objective-C не нужен для этой части кода.
  2. В новом `.h` объявить `bool parseQuickHotkey(stl::StringView text, UInt32& modifiers, UInt32& keyCode);`
     вне анонимного namespace. Весь файл под `#if defined(__APPLE__)`, как и
     сейчас `ui_quick_hotkey.h`/`.mm`.
  3. `ui_quick_hotkey.mm` подключает новый заголовок и зовёт функцию вместо
     собственного определения — поведение не меняется ни на бит.
  4. Новый `quick_hotkey_chord_ut.cpp` на верхнем уровне репозитория —
     подходит под существующий glob `unit_sources = sorted(build.glob("$(S)/*_ut.cpp"))`
     (`build.py:634`) **без единой правки `build.py`**, компилируется в уже
     существующий `unit_tests`. Заодно стоит перенести туда и проверку S1
     («модификаторы == 0» отклоняется) — сейчас это отдельная ветка в
     конструкторе `QuickHotkeyUi` (`ui_quick_hotkey.mm:199-207`), но при
     извлечении её естественно завести прямо в `parseQuickHotkey` рядом с
     остальной грамматикой, одним новым параметром или отдельной проверкой
     сразу после вызова — на усмотрение того, кто будет это делать.
  - **Оценка стоимости:** ~30 минут, ~70 строк перемещаются почти без
    изменений, поведение не меняется (чистое перемещение кода), риск
    регрессии нулевой — новый файл линкуется туда же, куда линковался старый
    анонимный код, только теперь ещё и в `unit_tests`.
- **Тест:** нет (блокирующий для теста, не для кода — само по себе не проваливает
  волну 3, но делает критерий приёмки плана «обязана быть покрыта» формально не
  выполненным)

### 2. `plt_unit_tests`, запущенный с `--threads=1`, ложно валит `FiberSchedulerPlatform` — замечание (для будущих волн)

- **Где:** `ext/plt/tests/fiber_ut.cpp`, suite `FiberSchedulerPlatform` (6 тестов:
  `PipePingPongAcrossFibers`, `PtyChildOutputWakesReader`,
  `SharedDescriptorServesMixedWaiters`, `TimedOutWaiterLeavesDataForTheNext`,
  `TimedParkDeadlinesResumeInOrder`, `YieldingFiberDoesNotStarveReader`)
- **Чем грозит:** сам наступил на это при первой попытке прогнать
  `plt_unit_tests` целиком с `--threads=1` (по привычке из top-level
  `unit_tests`, где `--threads=1` — часть официального вызова) — все шесть
  тестов планировщика волокон падают детерминированно, никак не связаны с
  window-chrome. Официальный узел графа (`ext/plt/build.py`, `plt_tests`) зовёт
  `plt_unit_tests` **без единого флага** — многопоточность нужна планировщику
  по конструкции теста (кросс-волоконный pipe/таймер). Прогон без флагов
  (как в `./build test -k`) даёт чистые `OK: 54` (было 53 до этой волны, +1 —
  мой новый тест).
- **Предлагается:** ничего чинить не нужно, это не баг — но если следующая
  волна (`R3-qa`/будущий `R4-test`) запустит `plt_unit_tests` вручную для
  диагностики, стоит помнить: **без `--threads=1`**, иначе получите шесть
  ложных падений в `FiberSchedulerPlatform`, не имеющих отношения к вашим
  правкам.
- **Тест:** не применимо (наблюдение про инструмент, не про код)

## Независимая перепроверка: список падений `./build test -k` не вырос

Запустил `./build test -k` на интегрированном состоянии волны 3 (после T3, T4,
F2 — коммит `400feada` на момент прогона, до коммита тестов этой задачи).
Результат:

```
build: 11 node(s) failed, 29 requested target(s) broken
```

— **то же число**, что зафиксировано в `docs/plans/reviews/window-chrome-R1-test.md`
после волны 1. Список `FAIL:` построчно идентичен эталону:

- `test_darkening_scales_with_the_option`
- `test_legacy_arrow_modifier_matrix` (×2 варианта: `key=262`/`key=263`, `modifiers=8`)
- `test_russian_shift_ctrl_c_has_no_legacy_control_sequence`
- `test_sheared_tail_lands_in_the_captured_blank`
- `test_soft_zero_departs_from_the_hinted_grid`

Плюс те же неизменные первопричины: `tst/pty_test_helper.c` (`SIGWINCH`,
4 вхождения — 2 шарда × 2 сообщения компилятора) и `forbidden branding` в
`pretty_binary_branding.py`. Волна 3 (хоткей, quick-режим окна, второй проход
по вкладкам) не добавила ни одного нового падения к унаследованному от R1-test
списку.

Отдельно, при прямом (не через `./build test`) запуске `.build/unit_tests`
видны 2 падения в suite `Pty` (`OwnerDeathReleasesBlockedIoAndHangsUpChild`,
`ResizeReachesChildAsWinch`) с сообщением `helper != nullptr`. Это тот же
`pty_test_helper.c`, не собравшийся из-за `SIGWINCH` — просто прямой вызов
бинаря не устанавливает `SHITTY_PTY_TEST_HELPER`, который официальный граф
задаёт через `env=` в `build.py`. Не новая находка, тот же диагноз R1-test,
проявляется только при обходе официального раннера.

## Чувствительность тестов

Точечная проверка: 3 дефекта внесены по очереди в продакшн-код, пересборка и
прогон целевого набора после каждого, дефект возвращён, `git status`/`git diff`
проверен на чистоту после каждого шага.

| # | Дефект | Файл | Что покраснело |
|---|--------|------|-----------------|
| 1 | `showing = composer.window->visible() && !composer.window->info().iconified;` → убран `&& !...iconified()` | `application.cpp:529` | только `IconifiedVisibleWindowIsShownRatherThanHiddenByToggle` |
| 2 | `if (showing) { hide } else { showAt }` → инвертировано на `if (!showing) { hide } else { showAt }` | `application.cpp:530-534` | `HiddenWindowIsShownByToggle`, `VisibleWindowIsHiddenByToggle`, `IconifiedVisibleWindowIsShownRatherThanHiddenByToggle` (три из четырёх — `NullWindowIsANoOp` не завязан на эту ветку, ожидаемо) |
| 3 | `requestHide()` перестаёт сбрасывать `shown_` | `ext/plt/platform_headless.cpp:187-192` | `PlatformHeadless::VisibleTogglesOnEveryShowHidePath` **и**, тем же прогоном, `ToggleQuickWindow::VisibleWindowIsHiddenByToggle` (оба набора используют один и тот же headless-бэкенд — дефект пойман синхронно в обоих) |

Отдельно повторно проверил дефект **4** из приложения R1-qa (`requestShowAt()`
перестаёт звать `requestShow()`) — ловится `PlatformHeadless::VisibleTogglesOnEveryShowHidePath`,
как и в оригинальной проверке R1-qa; не дублирую таблицу, ссылаюсь на
`docs/plans/reviews/window-chrome-R1-qa-round2.md`, где эта же мутация уже
задокументирована для того же теста.

Проверку «не переспецифицированы» отдельно не делал — находка 1 не имеет
предложенного мной кода-исправления (только форма извлечения, не поведенческий
фикс), тестам не на чем перегибать.

Mutation-тестирование инструментом — не настроено, не проводилось.

## Покрытие

`diff_coverage.py` не запускался (см. обоснование в `window-chrome-R1-test.md` —
ситуация с инструментарием не изменилась). Ручное сопоставление:

- `application.cpp:525-535` (`toggleQuickWindow`, все 4 ветки: null-guard,
  hide, showAt по `!showing`, влияние `iconified`) — покрыты полностью, каждая
  ветка отдельным тестом.
- `ext/plt/platform_headless.cpp:182-197` (`requestShow`/`requestHide`/`requestShowAt`)
  — покрыты полностью циклом в `VisibleTogglesOnEveryShowHidePath`.
- `ui_quick_hotkey.mm:32-227` (весь модуль, включая `parseQuickHotkey`,
  `QuickHotkeyUi`, регистрация/снятие Carbon-хоткея) — **не покрыто**, см.
  находку 1 и раздел «Что не покрыто и почему».
- `ext/plt/platform_cocoa.mm` (уровень окна, `collectionBehavior`, hide-on-resign-key,
  геометрия `TopOfActiveScreen`) — не покрыто юнит-тестами, как и в предыдущих
  волнах (нет чистых функций без побочных эффектов на реальном `NSWindow`).

## Допущения

- **Рабочее дерево не изолировано (не `git worktree`) и параллельно правилось
  F3a/F3b во время этой задачи.** Пока я тестировал `toggleQuickWindow()` и
  `visible()`, в том же дереве появились незакоммiченные правки
  `application.cpp` (поле `quickHotkeyActive`, условие в `showWindow()`),
  `ui_quick_hotkey.h`/`.mm` (`createQuickHotkey` теперь возвращает `bool`,
  добавлена проверка «без модификатора» — закрытие находки S1 R3-sec, уже
  найденной независимо, см. выше), `ext/plt/platform_cocoa.mm`. Ни один из
  моих файлов эти правки не задели (проверено `git diff --stat` до коммита:
  ровно 97 добавленных строк, ровно в двух моих файлах). Мои тесты
  собирались и проходили и до, и после появления этих правок — `toggleQuickWindow`
  сама не менялась. Финальный коммит тестов (`4322b975`) сделан поверх
  `400feada`, F3-работа на момент коммита в ветку ещё не попала — если её
  коммитят позже с расхождением от того, что я видел, независимая
  переприёмка (R3-qa) должна это заметить.
- **`ext/plt/platform_cocoa_ut.mm` не тронут** — за волну 3 туда не добавилось
  ни одной чистой функции без побочных эффектов; всё новое либо в
  `WindowImpl` (тип в анонимном namespace, недоступен извне), либо в
  `focused()`/конструкторе, которые требуют живого `NSWindow`.
- **`build.py` не трогал и не предлагаю трогать в рамках этой задачи** —
  форма извлечения находки 1 сознательно спроектирована так, чтобы новый
  тестовый файл попал под уже существующий glob без единой правки build-графа;
  это проверено чтением `build.py:634`, а не запуском (сам файл `quick_hotkey_chord.*`
  не создавался — я его не пишу, это чужая находка на исполнение).

## Было сломано до начала работы

Да, тот же набор, что в `window-chrome-R1-test.md` — подтверждено независимо
на интегрированном состоянии волны 3, список идентичен (см. «Независимая
перепроверка» выше). Дополнительно задокументирован артефакт инструмента
(`plt_unit_tests` с `--threads=1` — см. находку 2) и env-зависимость двух
`Pty::*`-тестов при прямом запуске `unit_tests` — оба не регрессии, оба
диагностированы, ни один не мой.

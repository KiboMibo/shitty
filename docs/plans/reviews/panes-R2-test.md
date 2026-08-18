# Тесты: волна 2 panes-and-window-chrome (quick-окно) — FAIL

- **Дата:** 2026-08-19
- **Статус:** `FAIL` — блокирующая находка `L1`: сборка на Linux не линкуется.
  Падающих тестов на macOS нет (710 OK / 3 ERR, три `ERR` — среда, разобраны ниже).
- **Скоуп:** диапазон `0b80f0b9..671e301d` (T3, F2, F2b, F2c, F2d) плюс модуль T2
  `quick_frame_store.{h,cpp}`, изменённый внутри диапазона.
- **Рабочее дерево:** `git worktree /tmp/r2test` на `671e301d`; отдельное дерево
  `/tmp/r2test-cov` для сборки с инструментацией покрытия. Рабочее дерево пользователя
  не изменялось, кроме этого отчёта и `.json` рядом.
- **Команды прогона:**
  `./build -j 8 st pt st_test unit_tests plt_unit_tests` → `exit 0`;
  `.build/unit_tests` и `.build/unit_tests --threads=1`;
  `.build/ext/plt/plt_unit_tests`; `./build test -k`.

## Итог одним экраном

| Что | Значение |
|---|---|
| Базис `unit_tests` на `HEAD` | **697 OK / 3 ERR** — совпало с ожидаемым |
| После моих тестов | **710 OK / 3 ERR**, два прогона × два режима планировщика |
| Базис `plt_unit_tests` | **54 OK / 0 ERR** — совпало |
| `./build test -k` | **12 именованных упавших узлов**, имена **идентичны** списку круга 2 |
| Новых тестов | **13** (11 в `lib/shitty/*_ut.cpp` + 2 туда же под `#if defined(__APPLE__)`) |
| Покрытие изменённых **продакшн**-строк | **22.3 %** (59/265); было 20.8 % до моих тестов |
| Мутаций проведено | **18**, из них **6 выживших** до моих тестов, **1 выживает и сейчас** |
| Блокирующих находок | **1** (`L1`) |

Порог 80 % по изменённым строкам **не взят и взят быть не может** имеющимися средствами:
175 из 265 изменённых продакшн-строк — это Cocoa-код, требующий живого `NSWindow`/`NSScreen`,
которого в этом проекте нет ни в одном тестовом бинаре. Разбор ниже, по файлам и строкам.

## Покрытие по изменённым строкам

Считалось `scripts/diff_coverage.py --base 0b80f0b9` по `lcov`, снятому
`llvm-cov export` с бинаря `unit_tests`, собранного с
`-fprofile-instr-generate -fcoverage-mapping` (те же флаги, что в `flake.nix:88`).

| Файл | Покрыто/изменено | % | Что не покрыто |
|---|---|---|---|
| `lib/shitty/quick_frame_store.cpp` | 21/21 | **100 %** | — |
| `lib/shitty/application.cpp` | 26/67 | 38.8 % | 614-619 (пересчёт сетки, Cocoa-ветка), 698-740 + 863 (`SHITTY_FOR_TESTS`-блок `SIGUSR2`), 820 |
| `lib/shitty/ui_csd_tabs.mm` | 4/29 | 13.8 % | 271-304, 319-321, 376-379 — тонировка титлбара, требует `NSWindow` |
| `lib/shitty/ui_quick_hotkey.mm` | 8/135 | **5.9 %** | всё, кроме двух ранних возвратов: Carbon-регистрация, фуллскрин-чорд, обратная запись кадра, поиск экрана |
| `ext/plt/platform_cocoa.mm` | 0/11 | 0 % | `requestCornerRadius()` целиком |
| `ext/plt/platform_headless.cpp` | 0/2 | 0 % | `requestCornerRadius()` — пустой no-op, вызовов из тестов нет |
| **Итого продакшн** | **59/265** | **22.3 %** | |
| Тестовые файлы диапазона | 450/450 | 100 % | (в общий процент `diff_coverage.py` они входят и дают 71.2 %, поэтому здесь считаю отдельно) |

Разрез, который объясняет цифру честнее общего процента:

- **портируемый, тестируемый код** (`quick_frame_store.cpp` + `application.cpp`) — **47/88 = 53.4 %**;
- **Cocoa-только** (`ui_quick_hotkey.mm` + `ui_csd_tabs.mm` + `platform_cocoa.mm`) — **12/175 = 6.9 %**.

**Оговорка к измерению (`Z7`).** Инструментированный покрытием бинарь падает по `SIGSEGV`
на `ApplicationProduction::HeadlessRunWiresPresentsAndTearsDownProductionComponents` и на
части сюит `Screen*`; профиль такого прогона не пишется. Поэтому измерение сделано
прогоном без `ApplicationProduction::` и `Pty::`, и цифра по `application.cpp` — **нижняя
оценка**: строки 698-740 и 863 (`installQuickToggleSignal`) исполняются именно из `run()`,
то есть из исключённого теста. Тот же прогон без инструментации проходит штатно
(710 OK / 3 ERR), так что это дефект связки «этот проект + `-fprofile-instr-generate`
на arm64-darwin», а не тестов.

## Три `ERR` в базисе: это среда, не продукт — и за ними скрыто больше

Три предсуществующих падения (`Pty::EngagedOwnerDeathSurvivesAFloodingChild`,
`Pty::OwnerDeathReleasesBlockedIoAndHangsUpChild`, `Pty::ResizeReachesChildAsWinch`,
все — `helper != nullptr failed, pty_ut.cpp:174`) впервые разобраны. Цепочка:

1. `pty_ut.cpp:173` читает `getenv("SHITTY_PTY_TEST_HELPER")`; переменную ставит только
   официальный узел графа (`build.py:989`). Прямой запуск `.build/unit_tests` её не имеет —
   это и есть причина `helper != nullptr failed`. **Не дефект продукта.**
2. Но узел графа тоже её не даёт: `tst/pty_test_helper.c` **не компилируется на macOS**
   (`error: use of undeclared identifier 'SIGWINCH'`, строки 35 и 42). `#define
   _POSIX_C_SOURCE 200809L` в шапке файла занижает `__DARWIN_C_LEVEL`, а `SIGWINCH` —
   BSD-расширение, которое за этим уровнем скрыто.
3. Следствие, которое важнее самих трёх `ERR`: узлы `unit_tests_group_*` зависят от
   `pty_test_helper`, поэтому в `./build test -k` они **не выполняются вовсе** — в логе
   полного прогона **ноль** узлов `[UT]`. То есть весь C++-набор (710 тестов, включая всю
   регрессию волны 2) на macOS официальным графом не гоняется; все три круга проверки
   видели его только потому, что запускали бинарь руками.

Правка на три строки проверена компиляцией (`#if defined(__APPLE__) / #define
_DARWIN_C_SOURCE / #endif` сразу после `_POSIX_C_SOURCE`) — файл собирается и `build`
линкует `pty_test_helper`. С рабочим хелпером картина такая:

| Тест | С починенным хелпером |
|---|---|
| `Pty::EngagedOwnerDeathSurvivesAFloodingChild` | **проходит** |
| `Pty::OwnerDeathReleasesBlockedIoAndHangsUpChild` | **падает**: `!writerReturned failed, pty_ut.cpp:482` |
| `Pty::ResizeReachesChildAsWinch` | **виснет**: 100 с при 91 % CPU, снят по `timeout` |

Файл `tst/pty_test_helper.c` в мои границы не входит — правка внесена в мой worktree ради
проверки гипотезы и **откачена**; хунк приложен ниже. Разбор двух обнажившихся падений —
отдельная задача: они про `Pty`, а не про волну 2.

## Мутационная проверка: 18 мутаций

Каждая — свой патч в продакшн-файл в моём worktree, сборка, прогон, откат; после каждой
серии `git status` пуст. Мутации мои, не повтор семи из `F2c` и двух из `F2d`.

### Мутации, которые существующие тесты ловили и до меня

| # | Мутация | Что покраснело |
|---|---|---|
| M1 | `quickFrameTarget()`: убрать `+ titlebar` из `wantedHeight` | `TargetLeavesAFittingFrameAlone`, `TargetKeepsAFrameSavedOnASecondaryScreenExactly`, `TargetPullsAFrameFromAnotherScreenOntoThisOne` |
| M2 | `quickFrameTarget()`: снять верхнюю границу `x` (`min(max(...))` → `max(...)`) | `TargetPullsAnOffScreenFrameBackOntoTheScreen`, `ShowClampsASavedFrameLargerThanTheScreen`, `ShowClampsASavedFramePositionInPointsNotPixelsAtDoubleScale` |
| M5b | `loadQuickFrame()`: все четыре ключа обратно в старое написание (`x`/`width`) | 7 тестов, включая `PreviousGenerationFileIsTreatedAsAbsent` и четыре `ToggleQuickWindow::Show*` |
| M6 | `saveQuickFrame()`: писать прямо в целевой путь, без `.tmp.<pid>` + `rename()` | `FailedWriteLeavesThePreviousFrameIntactAndNoTempFileBehind` |
| M8 | `defaultQuickFramePath()`: искать последнюю `.` по всей строке, игнорируя `/` | `DefaultPathWithoutAnExtensionAppendsTheSuffix` |
| M9 | `CsdTabsUi::nativeWindow()`: снять проверку тега бэкенда перед бридж-кастом | бинарь падает по `SIGSEGV` на `CsdTabsUi::HeadlessWindowIsNeverBridgedToAnNSWindow` — регрессия `B5` держит |
| M13 | `applySavedQuickFrame()`: игнорировать `opts->quickRememberFrame` | `ShowIgnoresASavedFrameWhenQuickRememberFrameIsOff` |
| M16 | `applyQuickFrameToWindow()`: снять проверку тега бэкенда | `SIGSEGV` (после моих тестов — ещё и именованное падение, см. ниже) |
| M18 | `resolveQuickCompanionConfig()`: не выставлять `selfReference` | `SelfReferenceDetectedThroughDifferentSpelling` + мой новый companion-тест |

**Ответ на прямой вопрос задания про `TargetLeavesAFittingFrameAlone`** (`F2c` писала, что
он не покраснел ни от одной её мутации): тест **чувствителен** — `M1` его роняет. Он
проверяет ровно то, что должен: что подходящий кадр не двигают, а высоту наращивают на
титлбар. Дополнять его нечем; запись `F2c` о «контрольном случае» стоит считать
артефактом набора её семи мутаций, ни одна из которых не трогала слагаемое `titlebar`.

### Мутации, которые выживали — то есть дыры, закрытые моими тестами

| # | Мутация | До | После |
|---|---|---|---|
| M3 | `quickFrameTarget()`: убрать `max(1.0, …)` для размеров экрана | никто | `TargetSurvivesADegenerateScreenRect` |
| M4 | `loadQuickFrame()`: снять `value >= 0` у `width-points` | никто | `NegativeSizeIsTreatedAsAbsent` |
| M5 | `loadQuickFrame()`: **частично** вернуть старые ключи (алиас `width`/`height`) | никто | `FileMixingOldAndNewKeysIsTreatedAsAbsent` |
| M6b | `saveQuickFrame()`: временный файл без `pid` в имени (`.tmp` вместо `.tmp.<pid>`) | никто | `SaveLeavesAnotherProcessTemporaryFileAlone` |
| M7 | `saveQuickFrame()`: не удалять временный файл при ошибке записи | никто | `WriteFailureAfterOpenRemovesTheTemporaryFile` |
| M17 | `quickFrameTarget()`: `max(0.0, titlebarHeight)` → `titlebarHeight` | никто | `TargetNeverLetsTheTitlebarShrinkTheFrame` |

`M6b` и `M7` стоит отметить особо: `FailedWriteLeavesThePreviousFrameIntactAndNoTempFileBehind`
выглядит как тест атомарности, но его сценарий (`chmod 0500`) валит `open()` — временный
файл вообще не создаётся, поэтому ни имя с `pid` (контракт «два процесса `quickCompanion`
не столкнутся»), ни ветка `unlink()` в `catch` им не проверялись.

### Мутация, которая выживает и сейчас

| # | Мутация | Кто ловит |
|---|---|---|
| M14 | `applySavedQuickFrame()`: `toComposerScale = composer.contentScale / restoredScale` → `1.0f` | **никто** |

Это пересчёт сетки после кросс-масштабного восстановления — побочная находка `F2c`,
измеренная ею живьём (`980×490` вместо `1000×500`). Строки 614-619 лежат внутри ветки
`if (applyQuickFrameToWindow(...))`, а на headless эта функция всегда возвращает `false`,
то есть ветка недостижима из любого юнит-теста. Находка `I13` ниже.

### Отдельно: перепроверка починки `F2d` про раскладку клавиатуры

Проверено не на слово, а сменой раскладки (Carbon `TISSelectInputSource`; исходная `ABC`
восстановлена):

| Версия теста | `com.apple.keylayout.ABC` | `com.apple.keylayout.Russian` |
|---|---|---|
| текущая (ожидания у живой раскладки через `UCKeyTranslate`) | зелёный | **зелёный** |
| мутация: вернуть литералы `'a'`/`'A'` | зелёный | **красный** |

То есть починка `F2d` реальна: раскладко-зависимость воспроизводится на прежней форме
теста и снята на нынешней. `plt_unit_tests` целиком — `54 OK` на обеих раскладках.

## Находки

### `L1` — блокирующая: сборка на не-Apple платформах не линкуется

`lib/shitty/application.cpp:595` вызывает `applyQuickFrameToWindow()` **вне какой-либо
условной компиляции** — от начала файла до этой строки нет ни одной открытой директивы
`#if` (проверено машинным перебором директив: единственная пара до неё, `#if
defined(SHITTY_FOR_TESTS)` на 511-519, закрыта). Определена функция **только** в
`lib/shitty/ui_quick_hotkey.mm:309`, а этот файл попадает в сборку исключительно на
darwin — `build.py:660` (`if darwin:`) … `build.py:666`.

Сосед по тому же файлу сделан правильно: `createQuickHotkey()` и `createCsdTabsUi()`
вызываются под `#if defined(__APPLE__)` (`application.cpp:829-840`).

**Доказательство, а не рассуждение.** Убрал `ui_quick_hotkey.mm` из графа (эмуляция
не-darwin сборки, правка `build.py` в моём worktree, откачена) и собрал `st`:

```
Undefined symbols for architecture arm64:
  "createQuickHotkey(stl::ObjPool&, Composer&)", referenced from:
      (anonymous namespace)::ApplicationImpl::run(int, char**) in libshitty_prod.a[3](application.cpp.o)
  "applyQuickFrameToWindow(Composer&, QuickFrame const&)", referenced from:
      toggleQuickWindow(Composer&) in libshitty_prod.a[3](application.cpp.o)
ld: symbol(s) not found for architecture arm64
```

Первый символ в этом выводе — артефакт эксперимента (`__APPLE__` в нём остаётся
определён, поэтому гвард на 829 не срабатывает); второй — настоящий, он не под гвардом
ни при каких условиях. На Linux `ui_quick_hotkey.mm` не компилируется, `__APPLE__` не
определён, и остаётся ровно одна неразрешённая ссылка: `applyQuickFrameToWindow`,
затягиваемая через `toggleQuickWindow()`, которая вызывается безусловно.

Затрагивает `st`, `pt`, `st_test`, `unit_tests` — то есть **всю** Linux-сборку. Внесено
волной 2, коммитом `F2c` `45eae88d` (до него ветвления «Cocoa-путь или портируемый» не
существовало). CI это не поймал: ветка `feat/window-chrome-upstream` на 90 коммитов
впереди `origin/master`, Linux-джобы по ней не гонялись.

**Что предлагается.** Развилка уже описана словами в комментарии `application.cpp:601-612`
(«fallback для бэкенда без конкретного `NSWindow`»), нужно только сделать её
компилируемой на не-Apple: либо обернуть Cocoa-ветку в `#if defined(__APPLE__)` (тогда
портируемый путь остаётся единственным вне darwin), либо дать `applyQuickFrameToWindow()`
портируемое определение-заглушку `return false;` в новом `.cpp`, добавляемом в граф в
ветке `else` к `build.py:660`. Первое короче и не заводит второго определения символа.
Проверить результат в этой среде нечем — кросс-компиляция под Linux недоступна, — но
статически он проверяем тем же экспериментом с исключением файла из графа.

### `I11` — важная: C++-тесты на macOS не исполняются официальным графом

Разобрано выше в разделе про три `ERR`. Практическое следствие: любая проверка, полагающаяся
на `./build test`, на macOS не увидит ни одного падения `unit_tests` — включая регрессии
`B1`, `B4`, `B5`, ради которых волна писала тесты. Хунк починки (три строки, откачен из
моего дерева):

```c
 #define _POSIX_C_SOURCE 200809L
+#if defined(__APPLE__)
+// SIGWINCH is a BSD extension that _POSIX_C_SOURCE above hides on Darwin.
+#define _DARWIN_C_SOURCE
+#endif
```

### `I12` — важная: за неработающим хелпером скрыты два реальных падения `Pty`

`Pty::OwnerDeathReleasesBlockedIoAndHangsUpChild` падает (`!writerReturned failed,
pty_ut.cpp:482`), `Pty::ResizeReachesChildAsWinch` виснет (снят по 100-секундному
таймауту, 91 % CPU). Оба — вне волны 2; отношу как находку, а не как задачу: чинить их
без разбора, что именно виснет (тест или продукт), значит гадать.

### `I13` — важная: пересчёт сетки после кросс-масштабного восстановления не покрыт

`application.cpp:614-619`. Мутация `M14` выживает. Побочная находка `F2c`, которую она
измерила живьём, тестом не закреплена и не может быть закреплена в нынешней форме: код
стоит в ветке, недостижимой на headless. Чтобы стать проверяемым, ему нужен либо
headless-путь, доходящий до того же пересчёта, либо вынос арифметики
(`restored.width * composer.contentScale / restoredScale`) в чистую функцию рядом с
`quickFrameTarget()` — второе дешевле и повторяет ровно тот приём, которым `F2c` закрыла
`I8` для клэмпа.

### `I14` — важная: `I8` закрыт наполовину

Общая чистая функция `quickFrameTarget()` действительно одна и покрыта на 100 %. Но
всё, что вокруг неё в Cocoa-пути, покрытия по-прежнему не имеет — 5.9 % по файлу:

- поиск экрана по сохранённому origin (`ui_quick_hotkey.mm:334-341`) — лечение `B4`;
- флаг `quickFrameScreenGuessed` и пропуск обратной записи (`:242-296`, `:347-353`) —
  вторая половина лечения `B4`, и ровно то место, по которому `F2c` отклонилась от буквы
  приёмки;
- фуллскрин-чорд целиком (`:118-164`) — лечение `B3`;
- разбор и регистрация чордов (`:178-231`).

Мои два новых теста покрывают только ранние возвраты (`:311-321`). Дальше нужен живой
`NSWindow`, а в этом проекте ни один тестовый бинарь его не создаёт: `platform_cocoa_ut.mm`
проверяет чистые функции (`cocoaWindowStyleMask`, `cocoaResizeUsesExactProposal`),
классы (`PltWindow`) и трансляцию клавиш, но окна не открывает. Завести его — решение об
инфраструктуре (`NSApplication`, Metal-устройство, главный цикл), а не правка теста;
принимать его самому я не стал.

### `Z4` — замечание: висячий указатель `quickToggleEvent` после разрушения пула

`application.cpp:713` — файловый `EventFD* quickToggleEvent`, указывающий на объект в
`composer.pool`. При разрушении пула он не обнуляется, а обработчик `SIGUSR2`
(`:715-719`) продолжает по нему писать. В боевом бинаре недостижимо (блок целиком под
`SHITTY_FOR_TESTS`), в `st_test` — только после завершения `run()`. Стоит одной строки
`quickToggleEvent = nullptr` там же, где приложение снимает остальные обработчики.

### `Z5` — замечание: разбор строки файла состояния несимметричен по пробелам

`quick_frame_store.cpp:76-87`: значение проходит через `stripSpace()`, ключ — нет.
`y-points=  50  ` разбирается, `y-points = 50` молча игнорируется, и файл после этого
считается неполным, то есть отбрасывается целиком. Для файла, который пользователь может
править руками (а `F2b` документировала именно ручное вмешательство — удаление файла как
способ сброса), это неочевидно. Либо стрипать ключ, либо сказать в `quick_frame_store.h`,
что формат не допускает пробелов вокруг `=`. Тест `BlankAndUnrecognizedLinesDoNotSpoil…`
намеренно **не** фиксирует нынешнее поведение как контракт — иначе будущая починка
упрётся в мой же тест.

### `Z6` — замечание: запись `F2c` о `TargetLeavesAFittingFrameAlone` вводит в заблуждение

См. выше: тест чувствителен. Формулировку в отчёте `F2c` и в борде стоит поправить, иначе
следующая волна решит, что тест декоративный, и удалит его.

### `Z7` — замечание: инструментация покрытия валит тестовый бинарь

Описано в разделе про покрытие. Практическое следствие для тех, кто будет мерить
покрытие на macOS: `ApplicationProduction::` и `Pty::` придётся исключать, и цифра будет
нижней оценкой.

## Новые тесты (13) — для механического переноса

Патч целиком: два коммита в worktree `/tmp/r2test` (`b9311424`, `50799d77`), одним файлом —
`/tmp/r2test-tools/r2test-tests.patch` (369 строк, `git apply` поверх `671e301d`).
Затронуты **только** тестовые файлы; продакшн-кода в патче нет.

| Файл | Хунк (после `671e301d`) | Тесты |
|---|---|---|
| `lib/shitty/quick_frame_store_ut.cpp` | `@@ -13,7 +13,9 @@` | +`#include <signal.h>`, `<sys/resource.h>` |
| | `@@ -204,6 +206,165 @@` (сюита `QuickFrameStore`) | `NegativeSizeIsTreatedAsAbsent`, `FileMixingOldAndNewKeysIsTreatedAsAbsent`, `BlankAndUnrecognizedLinesDoNotSpoilAnOtherwiseCompleteFile`, `RepeatedKeyTakesTheLastValue`, `SaveLeavesAnotherProcessTemporaryFileAlone`, `WriteFailureAfterOpenRemovesTheTemporaryFile` |
| | `@@ -295,6 +456,45 @@` (сюита `QuickFrameTargetResolution`) | `TargetSurvivesADegenerateScreenRect`, `TargetNeverLetsTheTitlebarShrinkTheFrame`, `TargetClampsThePositionAgainstTheFrameHeightIncludingTheTitlebar` |
| `lib/shitty/application_ut.cpp` | `@@ -322,6 +322,32 @@` | `ShowKeepsTheDefaultPlacementWhenThereIsNoConfigPath` |
| | `@@ -454,6 +480,48 @@` | `ApplyingASavedFrameRefusesANonCocoaBackend`, `ApplyingASavedFrameRefusesAComposerWithNoWindow` (обе под `#if defined(__APPLE__)`) |
| `lib/shitty/quick_companion_ut.cpp` | `@@ -6,6 +6,8 @@` | +`#include "quick_frame_store.h"` |
| | `@@ -119,4 +121,43 @@` | `CompanionKeepsAFrameStoreSeparateFromItsParent` |

Правок в `build.py` не требуется: `unit_sources` — это `glob("$(S)/lib/shitty/*_ut.cpp")`
(`build.py:641`), все три файла уже в нём.

**Про параллельную работу.** Пока я писал эти тесты, в рабочем дереве пользователя шла
правка `quick_frame_store.{cpp,ut.cpp}`, `ui_quick_hotkey.{h,mm}` и `platform_cocoa_ut.mm`
(судя по содержимому — исправления по кругу 3 приёмки, со **своей** нумерацией находок
`B6`/`B7` про выбор экрана). Из-за этой коллизии моя блокирующая находка названа `L1`, а
не `B6`. Патч проверен на применимость поверх текущего состояния рабочего дерева:
`git apply --check` — чисто, хунки не пересекаются.

Что каждый тест утверждает и какую мутацию ловит — в таблицах выше. Три пояснения:

- **`WriteFailureAfterOpenRemovesTheTemporaryFile`** временно ставит `RLIMIT_FSIZE = 0` и
  игнорирует `SIGXFSZ`, чтобы `open()` прошёл, а `write()` упал — единственный способ
  довести код до ветки `unlink()` в `catch`. И лимит, и диспозиция сигнала
  восстанавливаются в том же тесте до первого `STD_INSIST`, который может сработать, —
  иначе падение утащило бы за собой весь остальной набор.
- **`CompanionKeepsAFrameStoreSeparateFromItsParent`** закрывает **пункт 9 списка для
  человека** («`quickCompanion`: два процесса, два файла состояния»), который не
  проверялся ни в одном круге. Проверяет композицию двух модулей: companion — это тот же
  бинарь с **другим** `-config`, `defaultQuickFramePath()` выводит имя файла состояния из
  конфига, а `resolveQuickCompanionConfig()` отказывает ровно той конфигурации, в которой
  оба процесса получили бы один файл. Тестом закрывается целиком; человеку остаётся
  подтвердить глазами только сам факт двух окон.
- **`ShowKeepsTheDefaultPlacementWhenThereIsNoConfigPath`** — покрытие ветки, а не
  чувствительный тест: путь при пустом `configPath` не строится, но и `loadQuickFrame("")`
  вернул бы `false`, поэтому мутация «убрать проверку» результата не меняет. Оставлен
  сознательно: он станет чувствительным в ту минуту, когда путь начнут строить иначе.

## Что проверить не удалось

- **Живой прогон quick-окна.** Не проводился: это работа `R2-qa` круга 3, у неё рецепт и
  `dev/quick_window_probe.sh`. Синтетические чорды в этой среде не доставляются (0 из 22
  у круга 2), Screen Recording не выдан.
- **Linux-сборка** (`L1`) — кросс-компиляция недоступна; вывод держится на аудите
  директив препроцессора плюс эксперимент с исключением файла из графа.
- **Wayland-реализация `requestCornerRadius()`** — по-прежнему не проверена компилятором
  никем (отмечено ещё кругом 1).
- **Покрытие Cocoa-кода** — нужен живой `NSWindow` в тестовом бинаре, см. `I14`.
- **Точная атрибуция двух падений `Pty`** (`I12`) — вне скоупа волны 2.

## Допущения

- **База для diff-покрытия** — `0b80f0b9`, как задано в задании. `T2` (`8f7f9006`) в
  диапазон не входит: она лежит **до** этой базы. Её файл `quick_frame_store.{h,cpp}`
  всё равно взят в скоуп целиком — волна 2 переписала его формат и добавила
  `quickFrameTarget()`; покрытие изменённой части — 100 %.
- **Порог 80 %** считаю неприменимым к этому диапазону как единому целому и привожу
  разрез портируемый/Cocoa вместо натягивания цифры. Тестов-пустышек ради процента не
  добавлял.
- **`tst/pty_test_helper.c` и `build.py`** — вне выданных мне границ; правки в них
  делались только как эксперименты в моём worktree и откачены (`git status` пуст,
  проверено после каждой серии).
- **Раскладка клавиатуры машины** менялась в ходе проверки `F2d` и возвращена в исходную
  (`com.apple.keylayout.ABC`).

# R8-test — приёмка тестами волны 8 (сплиты панелей)

**Статус: В РАБОТЕ** (промежуточный отчёт; пишется по ходу, коммит после каждого шага)

База дерева: `e74011b8`, рабочее дерево `/tmp/r8test`, ветка `review/R8-test`.
Все номера строк ниже — от `e74011b8`, если не сказано иное.

## 1. Базис — снят у себя, и он не совпал с переданным

| Прибор | Число |
|---|---|
| `.build/unit_tests --threads=1` c `SHITTY_PTY_TEST_HELPER` на `e74011b8` | **892 OK / 0 ERR**, `exit 0` |

Мне передали 888. У меня 892 на том же коммите.

**Причина расхождения — в самой команде базиса, и это находка.**
Команда `./build -j 8 st pt` собирает **только** `st` и `pt`: после неё
`.build/` содержит `cas grb pt st tmp uid` и **никакого `unit_tests`**.
Цели называются `unit_tests` и `pty_test_helper` (`./build --list`).
То есть измерявший 888 запускал `.build/unit_tests`, **оставшийся от
более раннего дерева**, — ровно тот случай «сборка и измерение разными
командами», от которого предостерегает свод правил, только наоборот:
сборка не собрала того, что измеряли.

Мои команды:

```
./build -j 8 unit_tests pty_test_helper
SHITTY_PTY_TEST_HELPER=$PWD/.build/pty_test_helper .build/unit_tests --threads=1
```

## 2. Задание №1 — дыра `retainedOutput()`: перевзвод `outputPending` изолирован

`T10` передала дыру с формулировкой «что перевзводит флаг, я не изолировал;
`Composer::resize()` и `setContentScale()` выходят рано»
(`docs/reports/T10-splits-2026-08-20.md`).

**Изолировано. `Composer::resize()` рано НЕ выходит, и перевзводит именно она.**

Инструмент: временная проба за `getenv("SHITTY_PROBE_PANES")` — в
`ApplicationImpl::presentTerminal()` (печатает состав кадра и кто из панелей
`spoke`/`QUIET`), в `VtermImpl::redraw()` (печатает `backtrace(3)` в момент,
когда флаг взводится с `false`) и в `VtermImpl::consume()`. Пробы в продуктовом
коде временные, снимаются откатом по конкретным путям.

Снятая трасса перевзвода тихой панели **между первым и вторым кадром** стенда
`ApplicationProduction::ATabOfTwoPanesPresentsOneFrameAndAnchorsOnTheFocusedOne`:

```
PROBE frame panes=2: [0]=spoke [1]=spoke
PROBE consume [0]
PROBE consume [1]
PROBE arm [0]   <- VtermImpl::processInputImpl <- feedPty     (ожидаемо: это ввод стенда)
PROBE arm [1]   <- VtermImpl::paneResized
                <- SessionSetImpl::applyLayout
                <- CallSessionsResize::onListen
                <- Composer::resize
                <- ApplicationImpl::frame
                <- WindowHeadlessImpl::dispatchFrame
PROBE frame panes=2: [0]=spoke [1]=spoke
```

Цепочка целиком: `dispatchFrame()` → `ApplicationImpl::frame()` →
`updateWindowInfo()` → `Composer::resize()` → слушатель `CallSessionsResize` →
`SessionSetImpl::applyLayout()` → `VtermImpl::paneResized()` →
`cf->expose(); redraw();` → `outputPending = true` **у каждой панели вкладки**.

То есть флаг взводит **сам кадр**, до того как `presentTerminal()` спросит
панели: `paneResized()` (`vterm.cpp:8841`, тот самый `cf->expose()` из коммита
`98a08f42`) доходит до всех панелей вкладки безусловно.

Почему `Composer::resize()` не вышла рано, хотя окно не меняли: у стенда
геометрия ещё устаканивается. `updateWindowInfo()` на первом кадре видит
`initialGeometryPending` и зовёт `fontChanged()`, который меняет метрики и
запрашивает новый размер окна, — так что второй кадр приходит с **другими**
числами, `resize()` их принимает и веером доходит до `paneResized()`.

Следствие для теста: тихой панель может стать только на кадре, на котором
`Composer::resize()` действительно вышла рано, то есть **не на первых двух**.

### Дыра закрыта

Измерено пробой на стенде `T10` с добавленными кадрами:

| Кадр | Состав |
|---|---|
| 1 | `[0]=spoke [1]=spoke`, `consume()` обеим |
| 2 | `[0]=spoke [1]=spoke`, `consume()` обеим |
| 3 | `[0]=QUIET [1]=QUIET` → ветка `!spoke` → `repaintTerminal()` |
| 4 (после `feedPty` только в панель 0) | **`[0]=spoke [1]=QUIET`**, `consume()` **только** панели 0 |

То есть ветка достижима начиная с третьего кадра; `T10` до неё не дошла ровно
на один кадр.

Тест: `ApplicationProduction::TheQuietPaneOfAFrameHandsOverItsRetainedFormAndKeepsTheAnchor`
(`lib/shitty/application_ut.cpp`). Он ведёт стенд `T10` дальше: два кадра на
устаканивание геометрии, затем **явный слив** сфокусированной панели через
`output()`/`consume()` (теперь он держится — перевзводить некому), `feedPty()`
во вторую, и кадр.

Наблюдаемое выбрано так, чтобы ветка читалась **снаружи** `ApplicationImpl`:
тихая панель сделана **сфокусированной**, а якорь IME запрашивается из
`update` сфокусированной панели — в какой бы из двух форм этот `update` ни
пришёл. Кадр, выбросивший тихую панель, дошёл бы до `requestTextInputRect()`
без якоря и не записал бы ничего. Поэтому утверждается **и счётчик запросов**,
а не только координата: одна координата повторяет число предыдущего кадра и
держалась бы, даже если бы этот кадр не якорил ничего.

`consume()` на тихой панели ловится самим продуктом: `Vterm::consume()`
проверяет `updateScreen`, которого удержанная форма не выставляет.

### Мутации по заданию №1

Базис драйвера — в логе прежде первой мутации: `[baseline] exit=0 OK: 893`
(892 базиса + один новый тест). Тест закоммичен **до** первой мутации
(`c5065eb0`), откат мутаций — по конкретному пути файла.

| # | Мутация | Как краснеет |
|---|---|---|
| M8 | `consume()` на каждой панели кадра, включая отдавшую удержанную форму | **ERR** `TheQuietPaneOfAFrameHandsOverItsRetainedForm…`: `updateScreen != nullptr failed, vterm.cpp:2814`, стек `VtermImpl::consume` ← `ApplicationImpl::frame`. У `T10` эта мутация проходила молча |
| M10 | тихая панель выброшена из кадра вместо `retainedOutput()` | **ERR** тот же тест, `drive.quietFramePresented failed, application_ut.cpp:470`: рендерер отказывает кадру, форма которого изменилась, и `dispatchFrame()` возвращает `false`. У `T10` проходила молча |
| M11 | якорь берётся только у панели, которая говорила (положительный контроль **самой проверки якоря**, а не ветки) | **ERR** `drive.quietAnchor.count == drive.anchorsBeforeQuietFrame + 1 failed, application_ut.cpp:479` |

`M11` добавлена намеренно: `M8` и `M10` роняют тест **до** утверждений о якоре,
и без `M11` эти три строки были бы слепым прибором — зелёными при любой поломке
якоря.

## 3. Задание №2 — утверждение `T10` о ненаблюдаемости мутации на пути мыши

В работе. Читка кода уже дала кандидата на опровержение (асимметрия
доставки press/release при `pressedPane_ == 0`, `session.cpp:1331..1352`);
конструкция строится.

## 4. Задание №3 — чувствительность тестов `T10` мутациями

Не начато.

# T5 — начальный `winsize` слейв-pty до `fork`

Ветка `feat/T5-initial-winsize` от `wave/pane-frame-stall-w1` (`e4781db5`),
worktree `/Users/kibomibo/Projects/github.com/shitty-T5`.

Коммиты:

- `e59b60ab` T5: режим helper'а, который печатает winsize первой операцией
- `9f1f2bb8` T5: размер слейв-pty выставляется до fork, а не после

## Что сделано и зачем

`PtyImpl::spawn()` форкал потомка с pty размера 0×0 и полагался на
`handle->resize()`, который зовётся уже в родителе, **после** `fork`
(`session.cpp:452`). Потомок, читающий `TIOCGWINSZ` первой операцией после
`exec` — `st -e vim`, `st -e htop` — успевал увидеть 0×0.

По контракту К3 `PtySize` протащен в `Pty::spawn()`, и размер выставляется
`TIOCSWINSZ` на открытом слейве **до** `fork` — существующим `resizePty()`
(`pty.cpp:138`), нового кода для этого не написано. Ставший избыточным
`handle->resize()` сразу после `spawn()` из `openSession()` удалён: и первая
панель, и панель, рождённая сплитом, получают геометрию одним и тем же путём.

Отдельно — наблюдаемость. Существующий `ResizeReachesChildAsWinch` этот дефект
увидеть не мог: режим `winsize` печатает `ready` **до** ресайза, которого ждёт,
поэтому слейв 0×0 и правильно размеренный выглядят для теста одинаково. Добавлен
режим `winsize-now`, читающий `TIOCGWINSZ` первой операцией, без ожидания
`SIGWINCH`.

## Изменённые файлы

| Файл | Что |
|---|---|
| `lib/shitty/pty.h` | `spawn()` принимает `const PtySize&` (К3), с комментарием о причине |
| `lib/shitty/pty.cpp` | `resizePty(slave, size)` до `fork`; сигнатура объявления и определения |
| `lib/shitty/session.cpp` | размер передаётся в `spawn()`, `handle->resize()` после него удалён |
| `lib/shitty/pty_ut.cpp` | два новых теста, `TeeHandle`, `BornSizePty`, `parseWinsize`; фальшивый `TwoSessionPty` и хелперы под новую сигнатуру |
| `lib/shitty/session_ut.cpp` | фальшивый `StubPty::spawn` под новую сигнатуру (один токен) |
| `lib/shitty/ui_sidebar_tabs_ut.cpp` | вызов `spawn()` под новую сигнатуру (один токен) |
| `tst/pty_test_helper.c` | режим `winsize-now`; чтение и печать вынесены в `report_winsize()` |

## Ключевые решения

**`TIOCSWINSZ` на слейве, а не `openpty()`.** План допускал оба. `spawn()` уже
открывает мастер и слейв по отдельности (`posix_openpt`/`grantpt`/`unlockpt`/
`open`), и переход на `openpty()` переписал бы работающий и объяснённый
комментариями код ради одного аргумента. Одна строка `resizePty(slave, size)`
рядом с `openPtySlave()` — то же самое дешевле.

**Размер на слейве, а не на мастере.** Оба фд смотрят в один tty, эффект
одинаков. Слейв назван в плане и читается как «этим родится потомок».

**`TeeHandle` вместо чтения экрана vterm'а.** Сессии владеют своими ручками, и
единственная альтернатива — доставать текст из headless-vterm'а. `TeeHandle`
копирует то, что читатель сессии уже снял с ручки: тест слышит потомка панели
дословно, не трогая парсер.

**По первой панели проверяются строки, а не столбцы.** После `splitFocused()`
`applyLayout()` шлёт первой панели ресайз, и он гонится с `TIOCGWINSZ` её
потомка — потомок мог успеть прочитать и до, и после. Вертикальный сплит меняет
ширину и не трогает высоту, поэтому строки — единственная ось, по которой у
первой панели возможно точное утверждение. По ширине проверяется «не ноль».

> **Поправка `F1b` (2026-08-29).** Дальше в этом абзаце стояло: «точное
> совпадение ширины проверяется у панели, рождённой сплитом: ей после рождения
> никто размер не менял». Это неверно. `applyLayout()` (`session.cpp:844-857`)
> проходит по **всем** placement'ам новой раскладки, панель от сплита включая, и
> шлёт `resize()` каждой — то есть новорождённой панели размер меняют сразу
> после `openSession()`, и меняют на тот же самый. Ошибка не косметическая: из
> неё выросла находка `В1` отчёта `R1a-qa` — тест `EveryPanesChildIsBornWithThatPanesSize`
> проходил на коде без `resizePty()` до `fork`, потому что показание потомка
> снималось уже после этого ресайза. `F1b` перенёс наблюдение внутрь `spawn()`,
> до возврата в `applyLayout()`; теперь обе оси у обеих панелей утверждаются
> точно, и мутация «убрать `resizePty()` до `fork`» роняет тест 30 раз из 30.

## Как проверено

### 1. Механизм: `0 0` против `47 123`

`.build/pty_test_helper` в режиме `winsize-now`, запущенный ровно той
последовательностью, что теперь в `spawn()` (`openpty` → `TIOCSWINSZ` на слейве →
`fork` → `setsid`/`TIOCSCTTY`/`dup2` → `exec`), с ioctl'ом и без него:

```
without TIOCSWINSZ before fork (the defect): child's first TIOCGWINSZ -> '0 0', exit status 0
with TIOCSWINSZ before fork (the fix):       child's first TIOCGWINSZ -> '47 123', exit status 0
```

Это и есть дефект, показанный в лоб: без правки первая операция потомка
возвращает нули. Скрипт — `scratchpad/probe_winsize.py`.

### 2. Юнит-тесты

> Заполнено задачей `F1a` 2026-08-29. `T5` до прогона не дошёл, и прогон
> нашёл три отказа — они разобраны в
> `docs/reports/F1a-wave1-completion-2026-08-29.md` и исправлены там же.
> Ниже — состояние после этих исправлений.

`./build st -j 10`, `./build unit_tests -j 10` — обе зелёные.

Весь `unit_tests` целиком, с `TMPDIR` вне гитового чекаута:

```
$ TMPDIR=/private/tmp SHITTY_PTY_TEST_HELPER=.build/pty_test_helper \
      .build/unit_tests --threads=1
+ Pty::ChildOutputReachesEof
+ Pty::EngagedOwnerDeathSurvivesAFloodingChild
+ Pty::EofClosesOneSessionBeforeItsFollowupWake
+ Pty::EveryPanesChildIsBornWithThatPanesSize
+ Pty::InputRoundTripsThroughTheSlave
+ Pty::LargeChildOutputSurvivesBackpressure
+ Pty::OwnerDeathReleasesBlockedIoAndHangsUpChild
+ Pty::ResizeReachesChildAsWinch
+ Pty::TheChildIsBornWithTheSizeSpawnWasGiven
+ SessionSet::CreateOpensAndActivatesTheFirstSession
OK: 949
```

Оба новых теста зелёные, и `ResizeReachesChildAsWinch` вместе с ними: 949
из 949, ни одного отказа.

`EveryPanesChildIsBornWithThatPanesSize` прогнан отдельно 25 раз подряд —
25 зелёных, ноль отказов. Это не праздная проверка: до правки он падал 29
раз из 30 (см. отчёт `F1a`).

Оговорка про `TMPDIR`. Штатный прогон `./build unit_tests_group_NN`
подставляет `TMPDIR` внутрь `.build/tmp/<hash>`, то есть внутрь гитового
чекаута, и на этом падает `ui_sidebar_tabs_ut.cpp:529` — тест требует
директории, над которой нет репозитория. К этой задаче отношения не имеет:
тест пришёл с `43800639` (волна `C10`), а волна трогала в этом файле один
вызов `spawn()`. Тот же бинарь с `TMPDIR=/private/tmp` даёт 949 из 949.

## Обнаружено

**`clang-format` на этой машине — 23.1.0, а `dev/style.py` рассчитан на 21.**
`STYLE.md` прямо это оговаривает («Clang-format 21 cannot express a brace break
for constructors…»). Прогон `python3 dev/style.py` переформатировал **130
файлов**, из них 120 — чужие и вообще не относящиеся к задаче (`ext/libstd`,
`ext/plt`,半 `lib/shitty`), включая `lib/shitty/test_mode.cpp`, который мне не
принадлежит. Всё откачено, свои файлы отформатированы руками по `STYLE.md`.
**Следующему агенту: не гонять `dev/style.py` на этой машине, пока не появится
clang-format 21.**

**`libshitty_test` собирается глобом, поэтому `test_mode.cpp` блокирует и
`unit_tests`.** `build.py:705` строит `all_libshitty_sources` из
`build.glob("$(S)/lib/shitty/*.cpp")`, и `libshitty_test` (`build.py:747`) — из
того же списка. То есть некомпилирующийся `test_mode.cpp` роняет не только `st`,
но и `unit_tests`, и любой другой тестовый бинарь. Это шире, чем предполагала
постановка задачи.

**Правка сигнатуры `spawn()` понадобилась в двух файлах вне карты владения
плана.** `lib/shitty/session_ut.cpp:158` (фальшивый `StubPty`) и
`lib/shitty/ui_sidebar_tabs_ut.cpp:687` (вызов) в плане не упомянуты ни у T5, ни
у T1. Обе правки — по одному токену; сделаны здесь.

**Основное дерево репозитория стояло на ветке T1.** `git worktree list` на старте
показал `/Users/kibomibo/Projects/github.com/shitty` на `feat/T1-test-mode-panes`.
Создание своей ветки в том же дереве переключило бы дерево под работающим
параллельно T1. Поэтому T5 сделан в отдельном worktree
`/Users/kibomibo/Projects/github.com/shitty-T5`; основное дерево не тронуто.
**Следующим параллельным задачам волны — то же самое.**

## Что осталось за рамками

**`lib/shitty/test_mode.cpp` не тронут — им владеет T1.** Без правки в нём сборка
не проходит вообще. Нужно (точные строки — от `e4781db5`):

- `:400` — `PtyHandle* spawn(ObjPool& owner, const LaunchCommand& command, const PtySize& size) override;`
- `:676` — `PtyHandle* TestPtyFactory::spawn(ObjPool& owner, const LaunchCommand&, const PtySize& size) {`

Этого хватает для компиляции. Сверх того желательно, чтобы фальшивый pty тоже
рождался с геометрией панели, иначе T2 в тестовом режиме увидит у панели,
рождённой сплитом, ровно тот 0×0, который эта задача чинит в продукте: завести в
теле `:676` `struct winsize born{}` из `size` и передать его пятым аргументом в
`openpty()` вместо `nullptr` (`:683`). Первый фд приходит снаружи через `firstFd`
и через `openpty()` не проходит — там размер применить нечем без отдельного
ioctl'а; на компиляцию это не влияет.

**Проверка `-e` на живом бинаре руками не делалась.** Путь `st -e vim` покрыт
юнит-тестом на настоящем pty (`TheChildIsBornWithTheSizeSpawnWasGiven`), но
глазами TUI в окне я не запускал — это GUI-сценарий.

## Риски и точки внимания для ревьюера

- `PtySize{}` в `spawnShell()` и в `ui_sidebar_tabs_ut.cpp` — сознательные нули:
  там ни один потомок размера не читает. Если такой тест появится, он должен
  назвать размер явно.
- `BornSizePty` держит `PtySize born[2]` и `std::string heard[2]` фиксированными
  слотами, а не вектором: `TeeHandle` хранит ссылку внутрь на всё время жизни
  пула, и переаллокация вектора её бы протухла. `STD_INSIST(spawns < 2)`
  сторожит третий спавн.
- `resizePty()` при неудаче делает `sysWarn`, а не `sysError` — это было так и
  до правки. Теперь этот warn может прозвучать до `fork`, то есть на пути
  открытия сессии. Пробовать чинить размер после `fork` смысла нет: `fork` ещё
  не случился, а `resize()` от `applyLayout()` придёт следом своим чередом.
- `darwin_call_guard` новых вызовов не получил: `ioctl`/`TIOCSWINSZ` уже
  использовались в этом файле, разрешения гварда не расширялись.

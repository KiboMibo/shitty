# R8-qa — приёмка волны 8 (`T10`: сплиты, маршрутизация, разделители)

**готово с замечаниями**

Блокирующих нет: все критерии `T10` подтверждены измерениями на моём дереве, точка
остановки волны переснята и совпала до цифры. Замечания — важные, и все они об
одном классе: **утверждения, обоснованные несуществующим решением `A12`**.

## База всех утверждений

| Что | Значение |
|---|---|
| Дерево | `/tmp/r8qa`, ветка `review/R8-qa` |
| Голова | `e74011b8` (`board: T10 accepted at 888 green, and the titlebar drags like a titlebar`) |
| Все ссылки вида `файл:строка` | на этой голове |

Собрано и измерено разными командами:

```sh
./build -j 8 st pt                      # exit 0
./build -j 8 unit_tests pty_test_helper # exit 0, отдельной командой
SHITTY_PTY_TEST_HELPER=$PWD/.build/pty_test_helper .build/unit_tests --threads=1
```

**Результат: `OK: 892`, ERR — ноль** (строк с ошибками в выводе нет).

### Расхождение с ожидаемым базисом — объяснено, не дефект

Поручение ждало `888 OK`. Это число верно **не для этой головы**. Отчёт `T10` снял
`888` на своём дереве, а затем в ветку приехал мердж `ea2fcde6`
(`merge R7-test round six`), который добавил ровно четыре `STD_TEST`:

```sh
git diff 64074c15 ea2fcde6 -- '*_ut.cpp' | grep -c "^+.*STD_TEST("   # 4
```

`888 + 4 = 892`. Ожидаемый базис на `e74011b8` — **892 OK / 0 ERR**, и он снят.

## Точка остановки волны 8 — переснята

Прибор плана: клетки, смещённые `biasStrips()` за кадр (`render_metal.mm:518`).
Инструментированная сборка **внутри моего дерева**, отдельным каталогом, чтобы не
мерить прошлый бинарь:

```sh
./build -B .build-cov -Dcoverage -j 8 unit_tests pty_test_helper st_test
```

**Одна панель** — `pytest tst/test_gpu_parity.py` против `.build-cov/st_test`:

```
509|     20|void MetalRendererImpl::biasStrips(...)
511|     20|        return;
518|      0|        GpuCell& cell = cells.mut(pane.cellOffset + index);
```

**Три панели `6×2`, `3×2`, `4×2`** — `.build-cov/unit_tests MetalPanes::DrawThreeGridsInOneFrame`:

```
509|      3|void MetalRendererImpl::biasStrips(...)
511|      1|        return;
518|     14|        GpuCell& cell = cells.mut(pane.cellOffset + index);
```

| Стенд | вызовов `:509` | ранних возвратов `:511` | клеток `:518` | план требует |
|---|---|---|---|---|
| одна панель | 20 | 20 | **0** | 0 |
| три панели | 3 | 1 | **14** | `3·2 + 4·2 = 14` |

**Критерий выполнен.** Числа отчёта `T10` подтверждены независимо: команды и каталог
свои, продуктовый код рендерера в волне действительно не тронут.

## Критерии приёмки `T10` — поимённо

Все тесты ниже прогнаны мной на `e74011b8` по имени, каждый зелёный.

| # | Критерий плана | Вердикт | Доказательство |
|---|---|---|---|
| 1 | `cmd+d` делит вертикально, `cmd+shift+d` горизонтально | **закрыт** | `InputBindings::SplitsTheFocusedPaneOnTheTwoChordsAndOnlyWithTheOption` — `Shift` выбирает другую ось и только её; `SessionSet::TheSplitChordsDivideAndBothPanesTakeInputOfTheirOwn` |
| 2 | Обе панели живые и принимают ввод независимо | **закрыт** | тот же стенд: байты идут в сфокусированную, вторая молчит ровно ноль байт; после переноса фокуса — наоборот |
| 3 | Клик переносит фокус; ввод идёт в сфокусированную | **закрыт** | `SessionSet::AClickInAPaneTakesTheFocusAndTheKeystrokesWithIt` — и обратно, так что реализация, всегда отвечающая первой панелью, краснеет |
| 4 | Перетаскивание разделителя меняет размеры, оба шелла получают корректные (`tput cols` в обоих) | **закрыт числами, живой `tput` — человеку** | `SessionSet::DraggingTheSeamResizesBothPanesAndTellsBothShells`: `handles[0]->size.columns == 24`, `handles[1] == 56` — оба pty, не только сжавшийся; `SessionSet::TheSeamStopsBeforeEitherPaneRunsOutOfCells`. Живые `tput` — `H4` в `manual-checks-wave8.md` |
| 5 | Закрытие панели отдаёт место соседней; последней — закрывает вкладку | **закрыт** | `SessionSet::TheCloseChordTakesThePaneAndOnlyThenTheTab`, `SessionSet::ClosingTheLastPaneOfATabClosesTheTab`, `SessionSet::ClosingAPaneGivesItsRoomToTheSurvivorAndKeepsTheTab` |
| 6 | При `panes = false` чорды ничего не делают | **закрыт с оговоркой** | внешний замок — `input_bindings_ut.cpp:162-163` (`key()` вернул `false`, чорд **не потреблён**); внутренний — `SessionSet::TheSplitChordsDoNothingWhileThePanesOptionIsOff`. Оговорка ниже |
| 7 | Долг волны 5: четыре зажима берут начало и протяжённость из одной системы | **закрыт, все три условия** | ниже отдельно |
| 8 | Точка остановки волны (счётчик клеток) | **закрыт, переснят** | раздел выше |
| 9 | Визуальное — требует человека | **разобран** | 5 из 11 пунктов закрыты числами, 6 отданы человеку рецептом |

**Незакрытых критериев нет.** Оговорка к №6: критерий сформулирован как «чорды ничего
не делают», и в этой формулировке он выполнен. Но нужное поведение сильнее — чорд
обязан **дойти до программы**, а не быть съеденным. Слой биндингов чист (не потребляет),
слой терминала не проверен ничем: `InputSuper` доезжает до `VtModifier::super`
(`vterm.cpp:1503`) и до kitty-модификаторов (`:1520`), но ни один тест не утверждает
байт на выходе. Это `H2` в файле ручных проверок.

### Долг волны 5 — три условия плана, все три выполнены

| Условие плана | Проверено |
|---|---|
| Протяжённость **отдельным полем**, не `columns_ * glyphWidth` | `mouse_frontend.h:62-63` — `contentWidth`/`contentHeight` как поля; `contentRight()` (`:91`) = `contentLeft() + contentWidth`, `contentBottom()` (`:95`) — так же. Оба конца зажима называют одно начало **по построению** |
| Тест долга **переписан, а не удалён** | `TheFarEdgesAreStillTheWindowsWhileNoPaneHasAnExtent` → `MouseFrontend::TheFarEdgesAreThePanesOwnOnceItHasAnExtent` (`mouse_frontend_ut.cpp:498`), зелёный |
| `NamesACellPastTheGridWhenTheContentBoxEndsMidCell` прогнан и **не изменён** | `git log -S` по его имени даёт единственный коммит `a2c58740` (`R3-test`), задолго до волны 8; `git diff 54b95aeb 9382fd1e -- mouse_frontend_ut.cpp` его тела не касается. Прогнан: `MouseFrontend` — `OK: 26` |

Четыре зажима действительно берут обе величины из одной системы: `vterm.cpp:1634`,
`:2054`, `:2465`, `:9781` — все четыре вызова `mouseGeometry()` передают
`originX_, originY_, paneWidth_, paneHeight_`, а `paneWidth_`/`paneHeight_` приезжают
из `PaneGeometry` (`vterm.cpp:8823-8824`, `:8854-8855`).

# T0 — контрактные поля сетки панели в `TerminalUpdate`

## Что сделано и зачем

`TerminalUpdate` получил `gridColumns`/`gridRows` — сетку **панели**, которой
принадлежит кадр (решение `A9`). `VtermImpl::fillTerminalUpdate()` заполняет их
из `columns_`/`rows_`, то есть из геометрии, которую терминал получил при
создании (`A8`).

Читателей пока нет: 38 чтений `composer.columns`/`composer.rows` в рендерерах
переводит `T11`. Коммит существует ровно затем, чтобы `T11` и `T9` шли в волне 7
параллельно, а не по очереди — `T11` нужны поля, живущие в файлах `T9`.

Поведение и сборка не меняются.

## Изменённые файлы

- `lib/shitty/vterm.h` — два поля в `TerminalUpdate` плюс правило нуля.
- `lib/shitty/vterm.cpp` — два присваивания в `fillTerminalUpdate()`.
- `lib/shitty/vterm_headless_ut.cpp` — тест
  `VtermHeadless::CarriesThePaneGridInTheFrameAndNotTheWindows`.

## Расхождение с постановкой

Задача называла `lib/shitty/composer.h`; `struct TerminalUpdate` объявлена в
`composer.h` (строка 43) и **определена** в `lib/shitty/vterm.h:134`. Поля
поехали в определение. `composer.h` не тронут.

## Ключевые решения

- **Поля в `TerminalUpdate`, не в `PaneUpdate`** — как предписано `A9`, менять
  было нечего. Это длина буфера `TerminalRow::cells`, и она обязана ехать рядом
  с буфером; а `surfacePane()` строит `PaneUpdate` из `TerminalUpdate`, так что
  оконное умолчание уцелело бы ровно в беспанельном пути.
- **`PaneUpdate` и `PixelRect` не тронуты** — контракт `T1` дословно тот же.
- **Правило нуля записано в заголовке**, а не подразумевается: отказ кадра, не
  оконное умолчание. Умолчания нет намеренно.
- **Имена `gridColumns`/`gridRows`**, потому что `rows`/`rowCount` в этой же
  структуре — про массив повреждённых строк.

## Как проверено

`./build -j 8 st pt st_test unit_tests plt_unit_tests` — `exit 0`.

| Прогон | Результат |
|---|---|
| `unit_tests --threads=1` с `SHITTY_PTY_TEST_HELPER` | **OK: 813** (базис 812 + новый тест) |
| `unit_tests --threads=1` без хелпера | **OK: 810, ERR: 3** — три `Pty::`, требующие хелпер: `EngagedOwnerDeathSurvivesAFloodingChild`, `OwnerDeathReleasesBlockedIoAndHangsUpChild`, `ResizeReachesChildAsWinch` |

**Мутация** (`update.gridColumns = composer.columns; update.gridRows = composer.rows;`,
пересборка, полный прогон с хелпером):

```
- VtermHeadless::CarriesThePaneGridInTheFrameAndNotTheWindows
OK: 812, ERR: 1
```

Краснеет ровно новый тест и только он. Мутация откачена, сборка после отката —
`exit 0`.

`clang-format -i -lines=` по каждому диапазону отдельно (`2476:2478` в
`vterm.cpp`, `138:145` в `vterm.h`, `214:244` в `vterm_headless_ut.cpp`) —
правок не внёс.

## Что осталось за рамками

- Заполнение в `render_reference.cpp:786 renderUpdate()` (+2 строки) и в восьми
  местах `render_reference_ut.cpp` — это файлы `T11`, они же и единственные
  читатели.
- Сторож `gridColumns == 0 || gridRows == 0 → return false` в трёх рендерерах —
  `T11`.
- `A6-3` (свой ретейн на панель в эталонном рендерере) — `A9` требует его в той
  же задаче, что и подстановку размеров, то есть в `T11`.

## Риски и точки внимания

- Поля со значением по умолчанию `0` — любой производитель `TerminalUpdate`,
  который их не заполнит, отдаст кадр, который `T11` обязана отклонить. На
  сегодня незаполненными остаются `render_reference.cpp` и тесты рендерера;
  до появления сторожа это невидимо, после — станет падением, и это ровно то,
  ради чего умолчания нет.
- Утверждение `paneUpdate->rowCount == paneUpdate->gridRows` в тесте опирается
  на то, что `expose()` повреждает весь вид. Если это когда-нибудь перестанет
  быть правдой, строка станет ложным падением — но первые четыре утверждения
  теста от неё не зависят.

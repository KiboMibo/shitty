# `F7` — сторож смещения стрипов IME-оверлея

Задача: закрыть восьмую вырожденную фикстуру мержа до шага `M8`. Ветка
`fix/F7-overlay-offset` от `master` (`f3b02950`). Изменён **один** файл,
`lib/shitty/render_reference_ut.cpp`, `+182/−0`; продуктовый код не тронут.

Короткий ответ: сторож поставлен — `MetalPanes::AnOverlayLandsOnTheStripsOfItsOwnPane`
(`render_reference_ut.cpp:2677`). Мутация `M-c` краснит его и только его
(`OK: 967, ERR: 1`). Фикстура различает проверяемое: возврат `cellOffset` к нулю
при целом коде делает тест слепым, и это показано прогоном, а не рассуждением.
Оси не маскируют друг друга: две мутации падают на **разных** утверждениях.

---

## 1. Дефект и почему его не ловили

`overrideOverlayStrips()` в `lib/shitty/render_metal.mm:503` адресует стрипы
предварительного ввода суммой трёх слагаемых:

```
lib/shitty/render_metal.mm:509
    const size_t rowIndex = cellOffset + (size_t)(update.overlayRow) * columns;
lib/shitty/render_metal.mm:511
        GpuCell& cell = cells.mut(rowIndex + update.overlayColumn + index);
```

`cellOffset` — начало панели в **едином** массиве клеток кадра; у окна один
арена-буфер и один массив `cells`, панель `N` живёт в нём со смещения
`Σ(панели до неё)`. Мутация `M-c` шага `M7b` снимает первое слагаемое: строка
предварительного ввода панели `N` пишет свои стрипы в панель 0.

Мутация переживала всё дерево:

| Наблюдатель | Итог с мутацией |
|---|---|
| `unit_tests` | `OK: 967`, `EXIT=0`, ни одного красного |
| `tst/test_preedit.py` | 6 из 6 `ok` |
| `tst/test_gpu_parity.py` | 3 из 3 `ok`, включая **оба** многопанельных |

Причина у каждого наблюдателя своя, и ни одна не случайна:

- `test_preedit.py` читает снимок **модели**; стрипы Metal до него не доходят
  вовсе;
- `test_gpu_parity.py` рисует многопанельные кадры, но **без** предпросмотра
  ввода;
- во всём `render_reference_ut.cpp` оверлей ставил **ровно один** тест —
  `ReferenceRenderer::PreeditOverlayCoversUnderlyingStrips` (`:642`), на
  `ScreenFixture(4, 1)`: одна панель, `cellOffset` нулевой, слагаемое
  неотличимо от своего отсутствия. В суите `MetalPanes` слова `overlay` не
  было ни разу.

Это восьмой случай той же схемы за мерж: **параметр, обнуляющий проверяемое,
стоит по умолчанию, и им пользуются все.**

Отдельно проверено, что у эталонного рендерера этого класса дефекта нет и быть
не может: `render_reference.cpp:476` и `:899` считают базу оверлея от
`retain.columns` — сетки **самой панели**, — потому что эталонный бэкенд держит
клетки по панели, а не одним массивом на кадр. Слагаемого `cellOffset` там
просто не существует. Поэтому сторож имеет смысл только на Metal.

## 2. Где поставлен сторож и почему там

`STD_TEST_SUITE(MetalPanes)`, `lib/shitty/render_reference_ut.cpp:2677`, плюс
читалка чернил `paneCellHasInk()` на `:1727` рядом с `MetalFixture`.

Три причины именно там:

1. **Дефект живёт в `render_metal.mm`.** `MetalPanes` — единственный суит,
   который гоняет настоящий бэкенд Metal и читает обратно текстуру, которую он
   написал (`captureOutput`). Это единственный инструмент, видящий
   `overrideOverlayStrips()` вообще.
2. **`unit_tests` — единственный уровень, где кадр один.** Комментарий
   `APaneKeepsItsInkWhenALaterPaneCollectsTheArena` (`:2247`) объясняет, почему
   `tst/test_gpu_parity.py` тут не годится: харнесс гонит несколько кадров до
   того, как текстуру можно прочесть, и испорченный кадр всегда затирается. У
   `update()` + `capture()` этой проблемы нет.
3. **Соседние две трети функции уже охраняются там же** (`M-a` краснит
   `APaneKeepsItsInkWhenALaterPaneCollectsTheArena` и `DrawThreeGridsInOneFrame`).
   Дыра была узкая — только оверлей, — и закрывать её логично рядом.

Устройство стенда: две панели 6×3, обе залиты буквами во **всех** клетках,
оверлей — две пустые клетки на **второй** панели, строка 2, столбец 1. Пустой
оверлей снимает стрипы накрытых клеток, и они падают на свой фон; всё, что
оверлей не накрыл, чернила сохраняет. Тот же инструмент, что у эталонного
близнеца `PreeditOverlayCoversUnderlyingStrips`.

Утверждается **отсутствие** чернил по верному адресу и их **наличие** по обоим
неверным плюс по одной клетке слева и справа от прогона.

## 3. Три обязательных требования

### Требование 1. Мутация краснит нового сторожа и только его

**До мутации, целое дерево:**

```
$ ./build unit_tests pty_test_helper
BUILD EXIT=0
$ SHITTY_PTY_TEST_HELPER="$PWD/.build/pty_test_helper" \
      ./.build/unit_tests --threads=1 < /dev/null
EXIT=0
OK: 968
```

**С мутацией `M-c`** (`render_metal.mm:509`, `cellOffset +` снято):

```
$ SHITTY_PTY_TEST_HELPER="$PWD/.build/pty_test_helper" \
      ./.build/unit_tests --threads=1 < /dev/null
EXIT=1
!inked(overlayPane, overlayRow, overlayColumn) failed, at …/render_reference_ut.cpp:2782
- MetalPanes::AnOverlayLandsOnTheStripsOfItsOwnPane
OK: 967, ERR: 1
```

Один красный, и это новый тест. Остальные 967 — те же, что на `master`.

**После отката:**

```
$ git diff --exit-code lib/shitty/render_metal.mm && echo restored
restored
$ ./build unit_tests pty_test_helper
BUILD EXIT=0
$ SHITTY_PTY_TEST_HELPER="$PWD/.build/pty_test_helper" \
      ./.build/unit_tests --threads=1 < /dev/null
EXIT=0
OK: 968
```

### Требование 2. Фикстура различает проверяемое

Фикстура ломается одной константой: `overlayPane` с `1` на `0`. Тогда панель
оверлея начинается с нулевого смещения — ровно исходная вырожденность
`ScreenFixture(4, 1)`, только в двухпанельном стенде.

**Шаг а. Целый код, вырожденная фикстура — премисса ловит её вслух:**

```
$ SHITTY_PTY_TEST_HELPER=… ./.build/unit_tests --threads=1 < /dev/null
EXIT=1
lostOffset + overlayCount <= rightAnswer || rightAnswer + overlayCount <= lostOffset
    failed, at …/render_reference_ut.cpp:2742
- MetalPanes::AnOverlayLandsOnTheStripsOfItsOwnPane
OK: 967, ERR: 1
```

То есть **вырожденную фикстуру теперь нельзя завести молча**: тест сам
отказывается стоять на ней. Это и есть ответ на восемь случаев подряд — свойство
«фикстура невырождена» перестало быть свойством автора и стало утверждением.

**Шаг б. Снимаем и премиссу — и видно, что именно она удерживала.** Целый код,
вырожденная фикстура, три утверждения непересекаемости заменены на `(void)`:

```
$ SHITTY_PTY_TEST_HELPER=… ./.build/unit_tests --threads=1 < /dev/null
EXIT=0
+ MetalPanes::AnOverlayLandsOnTheStripsOfItsOwnPane
OK: 968
```

**Шаг в. Та же вырожденная фикстура плюс мутация `M-c` — тест слеп:**

```
$ SHITTY_PTY_TEST_HELPER=… ./.build/unit_tests --threads=1 < /dev/null
EXIT=0
+ MetalPanes::AnOverlayLandsOnTheStripsOfItsOwnPane
OK: 968
```

Зелено с дефектом. Значит тест ловит **смещение панели**, а не что-то соседнее:
единственное, что изменилось между шагом «краснеет» и шагом «слеп», — ненулевой
`cellOffset` у панели оверлея.

### Требование 3. Оси не маскируют друг друга

Стенд взят с **различными и обоими ненулевыми** смещениями: `overlayRow = 2`,
`overlayColumn = 1`, панель оверлея — вторая, `paneColumns = 6`,
`paneRowCount = 3`, `overlayCount = 2`.

Арифметика трёх ответов в клетках единого массива кадра:

| Ответ | Формула | Значение | Куда попадает |
|---|---|---|---|
| верный | `18 + 2·6 + 1` | **31**, 31–32 | панель 1, строка 2, столбцы 1–2 |
| потеря смещения | `0 + 2·6 + 1` | **13**, 13–14 | **панель 0**, строка 2, столбцы 1–2 |
| перестановка осей | `18 + 1·6 + 2` | **26**, 26–27 | панель 1, **строка 1**, столбцы 2–3 |

Три прогона по две клетки, попарно непересекающиеся. Это утверждается **в самом
тесте**, до отрисовки (`:2740-2742`), а не только здесь: `overlayRow != 0`,
`overlayColumn != 0`, `overlayRow != overlayColumn` и три условия
непересекаемости прогонов.

Арифметики мало — проверено прогоном. `STD_INSIST` роняет тест на первом же
несошедшемся утверждении, а первое утверждение («верный адрес очищен») ломают
обе мутации, поэтому обе назвали бы `:2782`. Чтобы увидеть, что мутации
**различимы**, пара утверждений верного адреса была временно снята, и каждая
мутация прогнана отдельно:

```
### мутация: перестановка осей (rowIndex по overlayColumn, +overlayRow)
inked(overlayPane, overlayColumn, overlayRow) failed, at …/render_reference_ut.cpp:2790
- MetalPanes::AnOverlayLandsOnTheStripsOfItsOwnPane
OK: 967, ERR: 1

### мутация: M-c, потеря cellOffset
inked(quietPane, overlayRow, overlayColumn) failed, at …/render_reference_ut.cpp:2786
- MetalPanes::AnOverlayLandsOnTheStripsOfItsOwnPane
OK: 967, ERR: 1
```

**Разные утверждения, разные строки** — `:2790` против `:2786`. Одна мутация не
прячется в ответе другой, чего не было у якоря IME в `T5.2`.

## 4. Таблица критериев

| # | Критерий | Итог |
|---|---|---|
| 1 | три требования выше | **закрыт**: §3, по каждому вывод |
| 2 | `./build unit_tests pty_test_helper` без `-k`, `OK: 968`+ | **закрыт**: `OK: 968`, `EXIT=0` |
| 3 | `./build st --clear` зелёная, 229 узлов | **закрыт** |
| 4 | питоновский набор поимённо | **закрыт**: 20 красных, список тот же, и бинарники побайтово те же |
| 5 | четыре гварда пробой кодом, `vterm_boundary` с пустым разрешением | **закрыт** |
| 6 | `./build example`, 37 тестов | **закрыт**: `EXIT=0`, 37 `ok`, 0 `skipped` |

### Критерий 2

```
$ ./build unit_tests pty_test_helper
BUILD EXIT=0
$ SHITTY_PTY_TEST_HELPER="$PWD/.build/pty_test_helper" \
      ./.build/unit_tests --threads=1 < /dev/null
EXIT=0
OK: 968
```

Эталон `master` — `OK: 967`, снят на этом же дереве до правки. Прибавился ровно
один тест. Без `-k`, stdin перенаправлен (`EXIT=148` — `SIGTSTP`, не падение).

### Критерий 3

```
$ ./build st --clear
EXIT=0
$ grep -c '^\[' <лог>
229
[LD] {229/229} $(B)/st
```

### Критерий 4

Режим — одним процессом, `--group=0 --group-count=1`, окружение по `build.py:1276-1283`
(`SHITTY_TEST_FONTCONFIG=0`, `SHITTY_TEST_PLATFORM=cocoa`). Тот же режим, каким
снимал эталон `M7b` на этом же дереве.

```
$ python3 tst/run_unittest_group.py --group=0 --group-count=1
EXIT=1
Ran 6437 tests in 126.534s
FAILED (failures=6, errors=14, skipped=17, expected failures=549)
```

Двадцать красных, поимённо совпадают с эталоном `M7b`: `test_bitmap_font_render`
×9, `test_synthesized_symbols` ×2, `test_color_font_render`, `test_font_resolver`
×2, `test_soft_render` ×2, `test_contour_input_generator` ×2,
`test_ghostty_key_encoding_tail`, `test_italic_overhang`. Симлинк `.build/st_test`
живой перед прогоном (иначе набор ответил бы `Ran 6065 … errors=6709` и не сказал
бы, почему).

Сверх поимённой сверки — **доказательство по построению**. Набор гоняет четыре
бинарника, и все четыре побайтово те же, что даёт `master`: сборка адресуется
содержимым, так что достаточно сравнить CAS-хеши, сняв правку и пересобрав.

```
$ git stash push lib/shitty/render_reference_ut.cpp && ./build st_test pt_test toml_dump example
BUILD EXIT=0
$ diff <хеши на master> <хеши на ветке>
IDENTICAL
st_test   d01dd2a019f6df3c2eb2164cca67d33dfa13170bba26a566ef59bcacc547a92b
pt_test   c8ddfc40c654c530cb3d3ba52d52d808284340b2b6214fdfc519639c6b6cfda7
toml_dump 539a74091c4146b8ff272d760345cbb4d4d409364bcbd3afb239a4b7346698c3
example   810a7776ac0270196f250f08e4d3dfd6b49546043967eae32046599231d7ae0e
```

Правка лежит в `*_ut.cpp`, а ни один узел этих четырёх целей его не компилирует,
поэтому двадцать красных здесь — это двадцать красных `master`, а не совпадение
имён.

### Критерий 5

Пять гвардов на итоговом дереве:

```
$ ./build border_pixels_guard    ; EXIT=0
$ ./build mouse_geometry_guard   ; EXIT=0
$ ./build pane_grid_guard        ; EXIT=0
$ ./build darwin_call_guard      ; EXIT=0
$ ./build vterm_boundary         ; EXIT=0
$ python3 lib/vterm/check_includes.py lib/vterm <stamp>   ; EXIT=0
$ grep -n 'ALLOWANCE' lib/vterm/check_includes.py
73:ALLOWANCE = {}
```

`vterm_boundary` прогнан **и напрямую, минуя `./build`** — штамп из CAS не
доказывает исполнения. Разрешение пусто.

Четыре сканирующих доказаны пробой **кодом**, а не комментарием
(`guard_source_reader` заменяет тела комментариев и строк пробелами, и
проба-комментарий зеленит все четыре сразу). Каждая проба ставилась по одной,
файл восстанавливался между пробами.

| Гвард | Проба (файл, код) | С пробой | Гвард назвал |
|---|---|---|---|
| `border_pixels_guard` | `render_reference_ut.cpp`: `const u32 probe = fx.composer->geometry.borderPixels();` | `EXIT=1` | `lib/shitty/render_reference_ut.cpp:2716` |
| `pane_grid_guard` | `render_reference_ut.cpp`: `const u16 probe = composer.geometry.columns;` | `EXIT=1` | `lib/shitty/render_reference_ut.cpp:2716` |
| `mouse_geometry_guard` | `render_reference.cpp`: `static const auto f7Probe = mouseGeometry(composer);` | `EXIT=1` | `lib/shitty/render_reference.cpp:8` |
| `darwin_call_guard` | `render.cpp`: `return createMetalRenderer(composer, pool, context);` вне `#if HAVE_METAL_RENDERER` | `EXIT=1` | `lib/shitty/render.cpp:8  createMetalRenderer` |

Два гварда из четырёх пробуются **в самом изменённом файле**, и это максимум,
достижимый для тестовой правки: `mouse_geometry_guard` пропускает всё, что
оканчивается на `_ut.cpp` (`build.py:1637`), а `darwin_call_guard` по построению
не считает `.mm`/`_ut.cpp` местом вызова. Их пробы — в ближайших файлах, которые
эти гварды действительно читают. После снятия каждой пробы — `EXIT=0`.

### Критерий 6

```
$ ./build example                                        ; EXIT=0
$ grep -c 'test_embed_example' <лог набора>              → 37
$ grep -cE 'test_embed_example.* \.\.\. ok' <лог>        → 37
$ grep -c 'test_embed_example.*skipped' <лог>            → 0
```

Ловушка «пропуск по наличию артефакта выглядит как `OK`» проверена: `skipped`
ноль, значит все 37 **исполнены**.

---

## Обнаружено

**1. `overrideOverlayStrips()` не проверяет границы панели, в отличие от
материализации клеток той же панели.** Материализация оверлея отказывается
писать за край:

```
lib/shitty/render_metal.mm:1125
    if (update.overlayCount != 0 && update.overlayCells != nullptr
        && update.overlayRow < paneRows
        && (size_t)(update.overlayColumn) + update.overlayCount <= paneColumns) {
```

Присвоение же стрипов той же самой панели (`:503`) проверяет **только**
`update.overlayCount == 0` и дальше пишет `cells.mut(rowIndex + overlayColumn + index)`
без единого сравнения с `paneColumns`/`paneRows`. Оверлей, чья строка или столбец
выходят за панель, получает клетки, но не стрипы — и с достаточно большим
`overlayRow` адресует **чужую панель или конец массива**. Дыры сегодня, вероятно,
нет: `Vector::mut()` в отладочной сборке проверяет длину, а фронтенд оверлей за
край не отправляет. Но два признака одного условия разъехались по разным местам,
и **это ровно та схема, которую `CLAUDE.md` описывает в разделе про `base64.cpp`**:
две половины одного условия, гасящие друг друга. Правка продуктовая — оставлена
владельцу `render_metal.mm`, не тронута.

**2. Устройство восьмой фикстуры даёт правило, применимое к следующим.** Все
восемь случаев ловятся одним и тем же вопросом: **дают ли слагаемые проверяемой
величины разные ответы на этом стенде?** Ответ — арифметика, а не прогон, и его
можно записать в тест: три адреса, три `STD_INSIST` на непересекаемость. Стоимость
— шесть строк; выигрыш — вырожденную фикстуру нельзя завести молча, что и показал
шаг «а» требования 2. Рекомендую тот же приём остальным стендам, где проверяемое
— сумма: якорь IME (`T5.2`), alt-половина, резерв хрома.

**3. Утверждение «мутация краснит тест» не различает мутации, если сторож
падает на первом же несошедшемся утверждении.** `STD_INSIST` — паника, а не
накопление; обе мутации назвали бы `:2782` и выглядели бы одинаково пойманными.
Чтобы увидеть, что стенд их **различает**, пришлось временно снять утверждение,
которое ломают обе. Это метод, а не частность: там, где стенд должен разделять
два дефекта, «оба краснят» — не доказательство, нужен прогон с временно снятым
общим утверждением. Той же природы, что находка `R6-test` про красного
наблюдателя.

---

## Что осталось другим задачам

**`M8`.** Сторож стоит **до** мержа, как и просила задача: `render_metal.mm`
`M8` скорее всего тронет (`73cd2b78`, `vt_headless`), и регрессия смещения
теперь приедет с именем.

Оговорка о покрытии, которую стоит держать в уме: `MetalPanes` целиком стоит под
`#if defined(HAVE_METAL_RENDERER)` и на Linux не исполняется вовсе. Сторож
охраняет darwin-бэкенд, где дефект и живёт; аналога на `render_vk.cpp` не
ставилось, потому что там клетки панели не адресуются общим смещением — это
надо проверить отдельно, если Vulkan когда-нибудь перейдёт на единый массив
кадра.

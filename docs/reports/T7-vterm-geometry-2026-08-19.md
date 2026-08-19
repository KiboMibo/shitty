# Отчёт T7: геометрия в `Vterm` (A8)

- **Задача:** `docs/plans/2026-08-18-panes-and-window-chrome.md`, `### T7. Геометрия в `Vterm` (A8)`
- **Архитектура:** `docs/architecture/2026-08-18-panes-and-window-chrome.md`, `### A8`,
  включая «Уточнение после волны 3»
- **Ветка:** `feat/window-chrome-upstream`
- **Статус:** `DONE`

## Коммиты

| Коммит | Что в нём |
|---|---|
| `0ec6d46b` | `T7: the pane's origin is its own pair of numbers, not more inset` — `MouseGeometry` получает `paneOriginX/paneOriginY` и `contentLeft()/contentTop()`; два теста |
| `ff8de47d` | `T7: the terminal is given its grid instead of reading the window` — 105 замен в `vterm.cpp`, `PaneGeometry`, `paneResized()`, `windowPane()` |
| `7b39305a` | `T7: the tests that count the resize instead of trusting it` — три теста, считающие события, а не констатирующие их |

Отчёт коммитится отдельно.

## Резюме по критериям приёмки

| Критерий | Чем подтверждён |
|---|---|
| Все тесты терминала зелёные по именам | `unit_tests` без хелпера **`OK: 782, ERR: 3`**, с хелпером **`OK: 783, ERR: 2`**; три `ERR` — предсуществующие `Pty::*` из базиса |
| Ни один тест не пришлось поправить под новое поведение | Ни одного изменённого или удалённого `STD_INSIST` во всех трёх коммитах — сверка ниже, раздел «Ни одного подогнанного ожидания» |
| Ровно один `SIGWINCH` на изменение, не два | Замер сквозь настоящий pty: один ресайз → `WINCH|1|80x5`, два ресайза → `WINCH|2|90x6`. Плюс `SessionSet::EachResizeCostsEveryShellExactlyOneWinsize` |
| `grep -r "composer\.\(columns\|rows\)" lib/shitty/vterm.cpp` — пусто | Пусто: `0` вхождений (было 105) |
| Сборка `./build -j 8 st pt st_test unit_tests plt_unit_tests` | `exit 0` |
| Базис `./build test -k` не вырос | 13 узлов, состав совпал с базисом узел в узел |
| Аудит директив с двумя контролями | положительный `671e301d` → ровно одна находка; отрицательный (рабочее дерево со снятым guard) → ровно одна находка |
| Тесты чувствительны, показано мутациями | 5 мутаций, 5 красных |
| `clang-format -lines` по каждому диапазону отдельно | 104 диапазона в `vterm.cpp`, остальные файлы отдельно; `dev/style.py` не запускался |

## Что сделано

### `Vterm` больше не читает окно — `lib/shitty/vterm.{cpp,h}`

105 обращений к `composer.columns`/`composer.rows` заменены на поля экземпляра
`columns_`/`rows_`. Рядом с ними — `originX_`/`originY_`: начало панели внутри
контентной коробки окна, в backing-пикселях.

Замена вышла равномерной, и это не совпадение: **каждое из 105 мест уже означало
«сетка этого терминала»** — граница прокрутки, протяжённость стирания, зажим
курсора, отчёт `CSI` про текстовую область. Ни одно не означало «окно». Места,
которые действительно про окно, остались нетронутыми и читают `composer` дальше:

- `windowOperation()` и `columnsForPixelWidth()/rowsForPixelHeight()` —
  пиксельные размеры окна и `contentInsets()`; это `CSI 4/8 t`, запрос к окну;
- `xtReportWindowPixelSize(compositorSize = true)` — `composer.pixelHeight/
  pixelWidth`, по определению размер поверхности компоновщика;
- `xtReportCellSize()` — `composer.glyphWidth/glyphHeight`, метрика шрифта.

### Геометрия приходит при рождении, а не после — `Vterm::create`

`Vterm::create()` получил параметр `const PaneGeometry&`. Первый экран строится
уже из него.

Альтернатива — «родиться сеткой окна, а потом принять панель» — стоила бы
дочернему процессу лишнего reflow и лишнего resize-репорта **до первой строки
вывода**. Ровно тот класс дефекта, о котором предупреждает `A8`.

### `windowResized()` → `paneResized(const PaneGeometry&)`

Один метод вместо «сеттер плюс триггер». Причина в риске, названном в плане:
`resizeGrid()` перекладывает скроллбэк, пересоздаёт экран и **обязан отчитаться
дочернему процессу** (`CSI 48` при `inBandResizeMode`). Раздельные сеттер и
триггер дают вызывающему возможность вызвать их не в том порядке или дважды;
единственный метод такой возможности не даёт.

### `windowPane(const Composer&)` — в `session.cpp`, а не в `vterm.cpp`

«Панель, занимающая всё окно» — определение слоя раскладки, а не терминала.
Сейчас это `SessionSet`, поэтому функция живёт там. Это **единственное место вне
самого `Composer`, которому позволено прочитать сетку окна и назвать ответ
панелью**; когда сплиты начнут делить окно, менять придётся тех, кто её ещё
называет — а не искать по всему `vterm.cpp`.

Побочный эффект, ради которого это в том числе сделано: критерий приёмки
требует, чтобы `grep` по `vterm.cpp` был пуст. Он пуст буквально, а не «пуст,
если не считать одного законного места».

### Начало панели — отдельным полем, как требует `A8`

`MouseGeometry` получил `paneOriginX`/`paneOriginY` и два аксессора:

```cpp
// lib/shitty/mouse_frontend.h
struct MouseGeometry {
    int framebufferWidth = 1;
    int framebufferHeight = 1;
    Insets insets;
    int paneOriginX = 0;
    int paneOriginY = 0;
    int glyphWidth = 1;
    int glyphHeight = 1;

    int contentLeft() const { return insets.left + paneOriginX; }
    int contentTop() const { return insets.top + paneOriginY; }
};

MouseGeometry mouseGeometry(const Composer& composer);
MouseGeometry mouseGeometry(const Composer& composer, int paneOriginX, int paneOriginY);
```

`contentLeft()`/`contentTop()` — единственное место, где отступ окна и смещение
панели складываются. Хранятся они раздельно: отступы — `Composer`, начало —
раскладка. `A8` прямо запрещает сводить их в одно поле, и запрет содержательный:
слитое число нельзя разобрать обратно, а именно это и было дефектом, от которого
защитил `A1`.

Все четыре пересчёта в `mouse_frontend.cpp` (`mouseCell`, `mouseSelectionCell`,
`mouseAutoscrollDirection`, `mouseProtocolPoint`) считают от `contentLeft()`/
`contentTop()`. Четыре места в `vterm.cpp`, которые их вызывают, передают
`originX_`, `originY_`.

Однопараметрическая форма `mouseGeometry(composer)` сохранена и означает
«панель во всё окно» — она нужна `composer_ut.cpp`, файлу за границей моей
задачи.

### Дальние края — осознанно не тронуты

Правый и нижний края контентной коробки в `mouse_frontend.cpp` остались
**оконными**: `framebufferWidth - insets.right`, `framebufferHeight -
insets.bottom`. Протяжённость панели не входит в контракт `A8`, и её сейчас
никто не выдаёт — это работа `T9`/`T10`.

Причина не только формальная. Естественный кандидат на протяжённость —
`columns_ * glyphWidth` — **не равен** нынешней ширине контентной коробки, когда
она кончается посреди ячейки; подставить его сейчас значило бы изменить
поведение на краю и подогнать под него тест
`NamesACellPastTheGridWhenTheContentBoxEndsMidCell`. Это запрещено критерием
приёмки, и правильно запрещено. Ограничение записано комментарием у самого
зажима в `mouse_frontend.cpp`, а не оставлено на обнаружение.

При одной панели во всё окно оба края — одно и то же число, поэтому сегодня это
не приближение, а тождество.

## Отступления от границ файлов

Нет. Изменены только файлы из списка владения:
`vterm.cpp`, `vterm.h`, `session.cpp`, `vterm_headless.cpp`,
`mouse_frontend.cpp`, `mouse_frontend.h`, `mouse_frontend_ut.cpp`,
`vterm_headless_ut.cpp`, `session_ut.cpp`.

`test_mode.cpp` не понадобился: он читает `composer.columns/rows` в команде
`FONT_STATE`, и там это по-прежнему верно — команда сообщает геометрию **окна**.

Файлы `F4` не открывались на запись ни разу. Коммиты делались явным перечислением
путей (`git add <файлы>`), не `-a`: рабочее дерево общее с `F4`, и в нём всё
время лежали её незакоммиченные правки.

## Чем проверено

### Сборка

```
./build -j 8 st pt st_test unit_tests plt_unit_tests   → exit 0
```

### Юнит-тесты

```
.build/unit_tests --threads=1                                   OK: 782, ERR: 3
SHITTY_PTY_TEST_HELPER=.build/pty_test_helper .build/unit_tests  OK: 783, ERR: 2
python3 ext/plt/tests/run_timed.py 120 .build/ext/plt/plt_unit_tests   OK: 54, ERR: 0
```

Базис на входе был `774/3` и `775/2`. Прирост `+8`/`+8` — это пять моих тестов и
три теста `F4`, чьи коммиты приземлились в ветку между моими. Состав `ERR`
не изменился: без хелпера — три `Pty::*` (`pty_ut.cpp:174`), с хелпером — два
(`pty_ut.cpp:482`, `pty_ut.cpp:403`), ровно как в базисе.

Новые тесты:

| Тест | Что доказывает |
|---|---|
| `MouseFrontend::KeepsThePaneOriginApartFromTheWindowInsets` | начало панели читается обратно отдельно от отступов окна; форма без начала — панель во всё окно |
| `MouseFrontend::EveryPointerMappingCountsFromThePaneOrigin` | все четыре пересчёта считают от начала панели; каждая проба отвечает по-разному с началом, без начала и с переставленными осями |
| `VtermHeadless::TakesItsGridFromThePaneItWasGiven` | терминал `10x4` на окне `80x24` описывает себя дочернему процессу как `10x4` |
| `VtermHeadless::ReportsOneInBandResizePerWindowResize` | один ресайз — **один** отчёт `CSI 48`; сдвиг панели без изменения сетки — тоже один; ресайз в тот же размер — ни одного |
| `SessionSet::EachResizeCostsEveryShellExactlyOneWinsize` | ровно один `PtyHandle::resize` на каждый хендл, с неперепутанными осями; повторный ресайз в тот же размер — ноль |

Про `TakesItsGridFromThePaneItWasGiven`: пробой выбран `DEC 2048`, потому что
ему не нужна опция `allowWindowOps` и он называет обе оси в одном отчёте
(`48;rows;columns;height;width`). Размеры панели (`10x4`) не равны ни друг другу,
ни размерам окна (`80x24`), поэтому и «прочитал окно», и «переставил оси» дают
другой ответ, а не случайно верный.

### `SIGWINCH` — замерено, а не предположено

Скрипт в scratchpad поверх `tst/harness.py` (в репозиторий не кладётся): ребёнок
ставит обработчик `SIGWINCH`, считает сигналы, **ждёт три секунды после ресайза**
и печатает счётчик. Ожидание нужно, чтобы запоздавший второй сигнал был посчитан,
а не проскочил мимо.

```
один ресайз  (72x4 → 80x5):            READY|72x4   WINCH|1|80x5
два ресайза  (72x4 → 80x5 → 90x6):     READY|72x4   WINCH|2|90x6
```

Второй прогон — контроль на «зелёный при нуле событий»: инструмент не залипший,
два изменения он считает как два.

Отдельно, на уровне `SessionSet`: `EachResizeCostsEveryShellExactlyOneWinsize`
проверяет, что `TIOCSWINSZ` уходит по одному разу на хендл и с правильными
осями (`100x30`, не `30x100`), а повторный ресайз в тот же размер не уходит
вовсе — `Composer` отфильтровывает его раньше, чем терминал о нём услышит.

### Мутации — 5 штук, 5 красных

Каждая мутация вносилась в рабочее дерево, собиралась, прогонялась
`unit_tests --threads=1` и откатывалась `git checkout`.

| Мутация | Что упало |
|---|---|
| `paneResized`: `columns_ = geometry.rows`, `rows_ = geometry.columns` | `VtermHeadless::ReportsOneInBandResizePerWindowResize`, `ApplicationProduction::HeadlessRunWiresPresentsAndTearsDownProductionComponents` |
| Конструктор: те же оси переставлены при рождении | `VtermHeadless::TakesItsGridFromThePaneItWasGiven`, `VtermHeadless::BulkUtf8DecoderMatchesByteWiseDecoder` |
| `contentLeft()` возвращает `insets.left` — начало панели потеряно | `MouseFrontend::EveryPointerMappingCountsFromThePaneOrigin`, `MouseFrontend::KeepsThePaneOriginApartFromTheWindowInsets` |
| `contentLeft()/contentTop()` берут чужую ось начала | те же два |
| `paneResized`: `resizeGrid()` вызван дважды — лишний холостой прогон | `VtermHeadless::ReportsOneInBandResizePerWindowResize` |

Последняя — та самая, ради которой писался счётчик, а не проверка «отчёт есть».
Проверка «есть» прошла бы на двух отчётах.

### Ни одного подогнанного ожидания

Полный список удалённых строк в тестовых файлах по всем трём моим коммитам:

```
-        Vterm* const second = Vterm::create(*composer.pool, composer, *composer.pool->make<SecondPtyStub>(composer), nullptr);
-        void send(Chunk*, size_t) override {
```

Первая — вызов, получивший новый обязательный аргумент. Вторая — пустое тело
заглушки, которое стало записывать отправленное, чтобы новые тесты могли считать.
**Ни один `STD_INSIST` не изменён и не удалён.** Это не «удалось», а проверено
командой: `git show <три коммита> -- <тестовые файлы> | grep '^-'`.

### `./build test -k`

```
build: 13 node(s) failed, 13 requested target(s) broken     (базис: 13)
```

Состав совпал с базисом:

- `tst/pretty-binary-branding` — 1;
- `unit-tests/group-17`, `unit-tests/group-18` — 2, и это та самая пара из
  базиса: `pty_ut.cpp:482` (`Pty::OwnerDeathReleasesBlockedIoAndHangsUpChild`) и
  `pty_ut.cpp:403` (`Pty::ResizeReachesChildAsWinch`). Номера групп сдвинулись,
  как и предупреждал базис, — сверялась пара «условие + строка падения»;
- `python-tests` группы `01,03,04,05,08` — 5;
- `python-tests-prod-parser` группы `01,03,04,05,08` — 5.

Имена упавших python-тестов — те же пять предсуществующих:
`test_darkening_scales_with_the_option`, `test_legacy_arrow_modifier_matrix`,
`test_russian_shift_ctrl_c_has_no_legacy_control_sequence`,
`test_sheared_tail_lands_in_the_captured_blank`,
`test_soft_zero_departs_from_the_hinted_grid`.

Зелёный узел `border_pixels_guard` от `T5` не тронут: новых обращений к
`borderPixels()` не вводилось, геометрия берётся через `contentInsets()`.

### Аудит директив — оба контроля

Скрипт свой, написан с нуля, лежит в scratchpad и в репозиторий не кладётся.
Метод — из `docs/plans/reviews/panes-R2-qa-round4.md:137-160`: собрать
определения верхнего уровня из darwin-only единиц (`lib/shitty/*.mm`,
`ext/plt/*.mm`), вычесть определённые и в портируемой единице, найти вызовы
оставшихся вне блока, требующего darwin. В darwin-макросы положены `__APPLE__`,
`HAVE_METAL_RENDERER` и `HAVE_CORETEXT`; `#if/#elif/#else/#endif` обходятся
стеком, ветка `#else` darwin-условия считается **не** защищённой.

```
рабочее дерево:              tracked darwin-only symbols: 77   unguarded calls: 0   exit=0
положительный (671e301d):    UNGUARDED lib/shitty/application.cpp:595  applyQuickFrameToWindow
                             tracked: 69   unguarded calls: 1   exit=1
отрицательный (дерево + снятый guard у createSidebarTabsUi):
                             UNGUARDED lib/shitty/application.cpp:865  createSidebarTabsUi
                             tracked: 77   unguarded calls: 1   exit=1
```

Оба контроля дали **ровно по одной** находке, и обе — ожидаемые. Строка
отрицательного контроля — `865`, а не `867` из постановки: файл
`application.cpp` в рабочем дереве несёт незакоммиченные правки `F4`, и
нумерация сдвинулась на две строки. Символ и файл совпадают.

Отрицательный контроль ставился на **копии** дерева в scratchpad
(`rsync` без `.git`), потому что `application.cpp` принадлежит `F4`; ни одной
записи в файлы `F4` не сделано.

**Компилятором Linux это не проверено — на этой машине его нет.** Аудит —
статический разбор исходников, а не сборка; он ловит тот класс дефекта, что дал
`L1`, но не заменяет линковку не-Apple цели.

### Формат

`clang-format -lines=A:B`, по одному диапазону за запуск, диапазоны берутся из
`git diff -U0` и применяются в обратном порядке. `vterm.cpp` — 104 диапазона,
`vterm.h` — 3, `session.cpp` — 3 (позже 1), `vterm_headless.cpp` — 6,
`vterm_headless_ut.cpp` — 4, `mouse_frontend*` — 14 суммарно.
`dev/style.py` **не запускался** ни разу.

Три места clang-format переписал сверх подстановки:

- `vterm.cpp:4995` — двухстрочное условие `findCycle` собрано в одну строку
  (при `ColumnLimit: 10000` это то, чего конфиг и хочет);
- `vterm.cpp:5842` — `(u32)(columns_)*SixelPatch::width`, потерянные пробелы
  вокруг `*` после каста; так форматирует конфиг репозитория;
- скобка тела конструктора в `vterm.cpp` и `vterm_headless.cpp` была свёрнута в
  `, hMargin(0) {`. Это **не** стиль репозитория: `dev/style.py` возвращает её
  обратно постобработкой (`restore_constructor_braces`). Восстановлено вручную,
  ровно то же преобразование.

## Что осталось непроверенным и почему

- **Не-Apple сборка.** Машина только macOS. Аудит директив — статическая замена,
  и она названа заменой, а не доказательством.
- **Попиксельное сравнение кадра.** `captureOutput()` есть только в Vulkan,
  `render_vk.cpp` на macOS не компилируется. Проверка «панель рисуется там же»
  для одной панели опирается на неизменность отступов и на то, что рендерер
  вообще не участвовал в правке.
- **Настоящая мышь через трекинг-области.** Замер начала панели сделан юнит-
  тестами на `mouse_frontend`, не движением курсора: при одной панели начало
  равно нулю, и любое реальное движение мыши дало бы тот же ответ, что и до
  правки, — то есть ничего бы не доказало.
- **Сравнение `SIGWINCH` «до и после».** Замер сделан только на дереве после
  правки. Дерево общее с `F4`, и отдельную сборку предыдущего состояния я
  разворачивать не стал; вместо сравнения приведён контроль на два ресайза,
  показывающий, что счётчик не залип.
- **Протяжённость панели в геометрии указателя.** Дальние края остались
  оконными, см. выше. При одной панели это тождество; при двух — работа
  `T9`/`T10`.

## Риски и точки внимания для ревьюера

1. **`Vterm::create` сменил сигнатуру.** Три вызывающих места — все в моих
   файлах. Если кто-то в параллельной волне добавит четвёртое, оно не соберётся,
   что и требуется.

2. **`windowPane()` живёт в `session.cpp`, а `vterm.cpp` её вызывает** — но
   после правки уже не вызывает: единственный оставшийся вызов из `vterm.cpp`
   ушёл вместе с параметром `Vterm::create`. Сейчас её зовут `session.cpp`,
   `vterm_headless.cpp` и `vterm_headless_ut.cpp`. Межмодульной зависимости
   `vterm.o → session.o` не возникло.

3. **`SessionSetImpl::ptySize()` по-прежнему считает от окна.** Это верно, пока
   панель одна, и это работа `T9`/`T10`: у каждой панели должен быть свой размер
   pty. Место одно, названо здесь.

4. **`composer.cellExtras` остаётся общим на все терминалы** — как и до правки.
   `Vterm::create` теперь считает `setCellCount` от переданной геометрии, то
   есть при двух панелях последний созданный терминал задаст счётчик по себе.
   Дефект предсуществующий (раньше его задавал последний созданный терминал по
   окну), правкой не введён и не исправлен — но с панелями он станет заметен.

5. **`originX_`/`originY_` сегодня всегда ноль.** Их читают только пересчёты
   указателя. Тесты на них — не на интеграции, а на `mouse_frontend`, где
   ненулевое начало можно задать прямо. Это осознанный выбор: интеграционный
   тест на нулевое значение был бы зелёным при нуле событий.

## Для следующих волн

- Панель получает свою геометрию через `Vterm::paneResized(const PaneGeometry&)`
  (`lib/shitty/vterm.h`). Один вызов делает и присвоение, и перестройку — не
  разделяйте их обратно, на этом держится «ровно один отчёт».
- Геометрия указателя получает начало панели **отдельным полем**:
  `MouseGeometry::paneOriginX/paneOriginY`, складываются с отступами только в
  `contentLeft()`/`contentTop()` (`lib/shitty/mouse_frontend.h`). Конструктор —
  `mouseGeometry(const Composer&, int paneOriginX, int paneOriginY)`
  (`lib/shitty/mouse_frontend.cpp`).
- Когда панелей станет больше одной, `T9`/`T10` придётся: выдать панели
  протяжённость (дальние края в `mouse_frontend.cpp`), развести `ptySize()` по
  панелям (`session.cpp`) и перестать звать `windowPane()` там, где панель уже
  не занимает окно целиком.

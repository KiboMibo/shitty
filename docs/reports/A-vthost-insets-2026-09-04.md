# Отчёт `A`: инсеты окна, ёмкость стора и внутриполосный resize ушли в `VtHost`

**Дата:** 2026-09-04 · **Ветка:** `feat/A-vthost-insets` (от `master`, `864696ee`) ·
**Коммиты:** `b8256585`, `6760d3b1` · **Дерево:**
`scratchpad/wt-A` · **Вход:** `docs/plans/reviews/upstream-merge-vthost-decision.md`
(вторая редакция), `docs/reports/M7-lib-embed-2026-09-04.md` (§4, §8, «Исходные
данные для задачи А»), `docs/plans/reviews/upstream-merge-T5.1-decision.md` §2.7,
`docs/plans/reviews/upstream-merge-invariants.md`, `CLAUDE.md`.

Всё ниже — вывод исполненных команд на этом дереве. Что не измерено, названо
в §9 прямо.

---

## 1. Коротко

`Composer&` ушёл из `lib/vterm` целиком: в коде ядра слово `Composer` встречается
**ноль раз** (комментарии забелены тем же читателем, которым это делают гварды),
`Vterm::create` стал **десятипараметрическим**, `vt_headless.{h,cpp}` переехал
в `lib/shitty`, разрешение `check_includes.py` **опустело** — шесть переходов
закрылись все шесть. `embed_facade_links = True`, `./build example` собирается,
**37 апстримных тестов исполняются и проходят**, `skipped` среди них ноль.

`./build so` на этой машине **исполнить нельзя** — группа заведена под `if linux:`
(§6, критерий 5). Проверено то же свойство прямым линкованием динамической
библиотеки под darwin, с негативным контролем, который называет четыре символа
поимённо.

`unit_tests`: **`OK: 967`** (966 + новый сторож), поимённая сверка — ровно одна
новая строка. Питоновский набор: `Ran 6437`, **красных двадцать, побайтово те же**,
`skipped` `54 → 17` — это ровно те 37, что теперь исполняются.

---

## 2. Что реализовано, и чем это отличается от документа решения

Реализовано всё, что решение предписывает, плюс перенос `vt_headless.cpp`,
который решение назвало «не факультативным», но принимать отказалось (§8.3).
Три отличия от буквы документа, все в сторону измерения:

**1. Функций в `vt_grid.h` шесть, а не четыре.** Решение §2.4 назвало
`vtGridColumns`, `vtGridRows`, `vtGridPixelWidth`, `vtGridPixelHeight` и там же
(довод 3) потребовало, чтобы `VtGeometry::resize()` перестал держать вторую копию
формулы. Эти два требования несовместимы вчетвером: `resize()` нужны не только
`columns`/`rows`, но и `contentWidth`/`contentHeight` — он кладёт их в
`VtGeometry::width/height` как протяжённость, которую делением не восстановить
(комментарий `vt_geometry.h:99-103`). Поэтому вычитание резерва названо
отдельно — `vtGridContentWidth`/`vtGridContentHeight`, — а `vtGridColumns`/`Rows`
определены через них. Дубликата не осталось ни одного; `resize()` зовёт
`vt_grid.h`, а не повторяет её.

**2. Разрешение гварда закрылось целиком, а не на четыре ключа из шести.**
Решение считало «четыре из шести», оставляя `vt_headless.cpp` открытым, потому
что переезд файла оно за собой не брало. Перенос входил в постановку задачи,
поэтому закрылось шесть из шести и `ALLOWANCE` стал пустым словарём. Форма
разрешения (ключ «файл + включение», счёт, «каждый ключ говорит, чем закроется»,
проверка осиротевших ключей) оставлена в комментарии — следующему переходу есть
куда записаться.

**3. Переехал не только `.cpp`, но и `.h`.** Решение §8.3 измерило цену переезда
именно как «`bin/core_perf/main.cpp:20` и `bin/main_fuzz/main.cpp:38` включают
`<lib/vterm/vt_headless.h>`», то есть считало переезд заголовка частью цены.
Он переехал; включателей пять, все поправлены (`bin/core_perf`, `bin/main_fuzz`,
`lib/shitty/main.cpp`, `lib/shitty/pty_ut.cpp`, `lib/shitty/vt_headless_ut.cpp`).
`bin/core_perf/main.cpp` расходится с апстримом теперь на две строки вместо одной.

Отдельно: `vt_headless.cpp` **не** пришлось переписывать на `vtGrid*`, как
предполагала таблица §5 решения. Он теперь в `lib/shitty`, где `grid_geometry.h`
— свой заголовок; переписывать было бы работой ради формы.

---

## 3. Три метода `VtHost` — сигнатуры и обоснование каждого

Добавлены в конец `struct VtHost` (`lib/vterm/vt_host.h`). Диф против апстрима:
`git diff --numstat origin/master -- lib/vterm/vt_host.h` → **`45 0`**, то есть
**45 добавленных строк и ни одной удалённой**; из них кода три строки, остальное
комментарии. Апстрим за 89 коммитов трогал этот файл один раз и тоже добавлением
метода в конец, так что конфликт при следующем мерже — контекстный.

```c++
virtual VtInsets contentInsets() = 0;
virtual void surfaceResized(u32 width, u32 height) = 0;
virtual size_t cellCapacityExcept(const Vterm* except) = 0;
```

### `VtInsets contentInsets()`

**Было:** `composer.contentInsets()` в трёх местах `vterm.cpp` — два в
`columnsForPixelWidth`/`rowsForPixelHeight` (`CSI 18t`/`19t`) и одно в
`windowOperation` (`CSI 8t`). Тип `Insets` из `lib/shitty/composer.h` был
единственным типом эмбеддера, употреблённым в теле ядра.

**Стало:** ядро спрашивает хоста и получает `VtInsets` — тип, который уже жил в
`lib/vterm/vt_geometry.h:31` с тем же порядком полей, заведённым `T5.1` §2.1
именно ради этого перехода. `ComposerVtHost::contentInsets()` отвечает
`vtInsets(composer.contentInsets())` — копирование поля в поле по имени.

**Чем отличается:** тип аргумента, не значение. Величина по-прежнему приходит из
`Composer::contentInsets()` — единственного источника правды, у которого нет
места хранения (`composer.cpp:306-318`); поэтому метод возвращает **по значению
и на каждом обращении**, а не отдаёт ссылку и не кеширует. Запрет §2.7 решения
`T5.1` — про **инсеты панели вместо окновых** — не нарушен: все четыре окновых
места продолжают спрашивать про окно, и это теперь охраняется тестом (§5),
чего до сих пор не было.

`vtInsets()` переехал из `pane_layout.h` в `composer.h` — туда, где объявлен
`Insets`: перевод между двумя написаниями одного числа к раскладке панелей
отношения не имеет.

### `void surfaceResized(u32 width, u32 height)`

**Было:** `composer.resize((u16)(min(pixelWidth, UINT16_MAX)), …)` в лямбде
`resize` внутри `windowOperation`, строкой ниже `window->requestResize(...)` —
строка, которую борд числил без владельца с волны 6.

**Стало:** `window->surfaceResized(pixelWidth, pixelHeight)`. Обрезание до `u16`
уехало в адаптер: `u16` — ширина полей `Composer::geometry`, то есть форма
хранения эмбеддера. Сигнатура берёт `u32`, как соседняя `requestResize(u32, u32)`.

**Чем отличается:** ничем по поведению — `ComposerVtHost::surfaceResized()`
зовёт тот же `Composer::resize()`, с той же verbose-трассой `window:` (`F4` Q2)
и тем же обходом `resizedListeners`. Отличается адресом: строка перестала быть
обращением к структуре из `lib/shitty` и стала вызовом интерфейса ядра.
**Не удалена**, потому что она не дубликат `requestResize()`: в `VtermHeadless`
и в `lib/embed` кадра нет вовсе, и это единственный внутриполосный путь.
`EmbedHost::surfaceResized()` — дословно апстримная строка `geometry.resize(w, h, this)`,
перенесённая на одну сторону границы.

### `size_t cellCapacityExcept(const Vterm* except)`

**Было:**

```c++
if (composer.sessions != nullptr) {
    count += composer.sessions->cellCapacityExcept(this);
}
```

— обращение к **полю** `Composer`, требующее полного типа. Пока оно там,
`Composer&` обязан остаться параметром: именно поэтому «десять параметров при
сохранённом `sessions`» недостижимо.

**Стало:** `count += host.cellCapacityExcept(this);` Проверка на `nullptr`
уехала в `ComposerVtHost`, где `SessionSet` вообще существует как понятие.

**Чем отличается:** инвариант `A11` не переехал ни на строку —
`SessionSet::cellCapacityExcept()` (`session.h:114`, `session.cpp:823`) не тронут.
Переехал **вопрос**, а не ответ. Ветка «списка нет» стала строже: раньше её
проверял `nullptr` внутри ядра, теперь — то, что адаптер вернул ноль.
`EmbedHost::cellCapacityExcept()` возвращает `0`, и тогда
`updateExtraCellCount()` считает ровно то же, что апстримное ядро, у которого
этого слагаемого нет вовсе.

---

## 4. Как доказано, что `Composer` ушёл из сигнатур, а не из включений

Ловушка названа архитектором заранее: `session.h:9` → `pane_layout.h:9` →
`composer.h`, поэтому `vterm.cpp` мог бы удалить собственный `#include "composer.h"`
и продолжать компилироваться — гвард зазеленел бы на ключ, а граница не сдвинулась.
Три независимых доказательства, что это не тот случай.

**Первое — `Composer` не встречается в коде `lib/vterm` вовсе.** Считано с
забеливанием комментариев и строковых литералов функцией `blanked()` из
`build.py` (той самой, которой пользуются четыре сканирующих гварда):

```
$ python3 <…blanked() над lib/vterm/*.{h,cpp}, поиск \bComposer\b…>
total: 0
```

**Второе — `vterm.cpp` остался и без `session.h`.** Именно наличие `session.h`
при отсутствии `composer.h` было бы признаком сработавшей дыры:

```
$ grep -n 'session.h\|composer.h\|grid_geometry.h\|pane_layout.h' lib/vterm/vterm.cpp
(пусто)
```

Ни одного включения, разрешающегося в `lib/shitty`, во всём `lib/vterm` не
осталось: единственное неместное имя — `unicode_data.h`, генерируемый заголовок
из списка `GENERATED`.

**Третье — сигнатура.** Полный тип `Composer` больше неоткуда взять, потому что
его больше негде употребить:

```
$ grep -n 'static Vterm\* create' lib/vterm/vterm.h
320:    static Vterm* create(stl::ObjPool& owner, VtGeometry& windowGeometry, const VtConfigSlot& config,
        VtCellExtras& extras, stl::SmallObjAllocator& smallObjects, plt::Scheduler& scheduler,
        VtHost& host, const VtGeometry& geometry, PtyHandle& pty, VtermTraceFactory* traceFactory);

параметров: 10
  1. stl::ObjPool& owner              6. plt::Scheduler& scheduler
  2. VtGeometry& windowGeometry       7. VtHost& host
  3. const VtConfigSlot& config       8. const VtGeometry& geometry
  4. VtCellExtras& extras             9. PtyHandle& pty
  5. stl::SmallObjAllocator& smallObjects   10. VtermTraceFactory* traceFactory
```

`struct Composer;` из `vterm.h` снят, член `Composer& composer` из `VtermImpl`
снят, оба конструктора и `create` потеряли параметр. Пятнадцать мест вызова
переписаны: `session.cpp:481`, `lib/shitty/vt_headless.cpp`, тринадцать в
`vt_headless_ut.cpp`.

---

## 5. Седьмая вырожденная фикстура и её мутации

**Находка решения (§11.2) подтверждена.**
`AnswersTheWindowReportsAboutTheWindowAndTheTextAreaAboutThePane`
(`vt_headless_ut.cpp:287`) — лучший из существовавших сторожей окновых
отчётов — построен добросовестно на панельной стороне (панель несёт границу `7`,
и комментарий прямо объясняет, зачем), но **инсеты окна в нём нулевые**:
`Options::border` по умолчанию `0`, резерв хрома не ставится нигде в файле.

Заведён `VtermHeadless::TheWindowReportsCountTheWindowsOwnReserveAndNotThePanesBorder`
(`lib/shitty/vt_headless_ut.cpp:371`). Фикстура: `Options::border = 3` **и**
`setChromeReserve(ChromeSide::Left, 11)` — резерв ненулевой **и асимметричный**,
потому что все три подстановки суть суммы разных подмножеств тех же чисел, и
симметричный резерв заставил бы две из них совпасть случайно.
`composer.contentInsets()` даёт `{top:3, right:3, bottom:3, left:14}`, инсеты
панели — `{3,3,3,3}`; тест утверждает обе величины поимённо, прежде чем
утверждать что-либо ещё.

Проверяется:

- **`CSI 19t`** (экран в символах). Экран headless — фиксированные `1920x1080`
  при клетке `1x1`, значит резерв окна и есть вся разница: ответ обязан быть
  `9;1074;1903t`. Отдельно утверждается **отсутствие** трёх неправильных
  ответов: `9;1080;1920t` (нули), `9;1080;1909t` (хром без границы),
  `9;1074;1914t` (инсеты панели).
- **`CSI 8t`** — обратное направление той же арифметики и заодно два других
  метода: `\x1b[8;10;20t` обязан дать поверхность `37x16` пикселей
  (`14+3+20` и `3+3+10`) и сетку `20x10`, посчитанную из неё обратно. Это
  утверждение доходит до `surfaceResized()`: без него `composer.geometry` не
  изменилась бы вовсе.

**Три мутации, каждая — прогон `unit_tests` целиком:**

| Мутация | Итог | Красные |
|---|---|---|
| `ComposerVtHost::contentInsets()` → `VtInsets{}` | `OK: 966, ERR: 1` | `TheWindowReportsCountTheWindowsOwnReserve…` |
| `ComposerVtHost::contentInsets()` → `vtInsets(composer.chromeInsets())` | `OK: 966, ERR: 1` | она же |
| `VtermImpl` читает `pane_.insets` вместо `host.contentInsets()` (три места) | `OK: 965, ERR: 2` | она же **и** `AnswersTheWindowReportsAboutTheWindowAndTheTextAreaAboutThePane` |

**Доказательство, что фикстура различает проверяемое, — в первых двух строках.**
При мутациях 1 и 2 краснеет **ровно один** тест, и это новый; остальные **966**
зелены. То есть до этой задачи обе мутации не поймал бы **никто**: старая
фикстура на нулевых инсетах окна утверждает про них ровно ничего.

**Третья строка расходится с формулировкой критерия «краснит нового сторожа и
только его», и это правильно.** Мутация 3 — подстановка инсетов панели — как раз
то, ради чего старая фикстура несла границу `7`; решение прямо пишет, что она
«ловит подстановку инсетов панели и не ловит потерю инсетов окна». Оба сторожа
краснеют, и второй краснеет по делу. Ошибкой было бы обратное.

---

## 6. Таблица критериев

Всякая строка — с выводом. `echo "EXIT=$?"` сразу после команды, не через пайп.

| № | Критерий | Итог |
|---|---|---|
| 1 | `Composer` не в сигнатурах и телах `lib/vterm` | **закрыт**, §4: `total: 0`, и `vterm.cpp` без `session.h` |
| 2 | `Vterm::create` — десять параметров | **закрыт**, §4 |
| 3 | Три метода `VtHost` названы с сигнатурами | **закрыт**, §3 |
| 4 | `embed_facade_links = True`, `./build example`, 37 тестов исполняются и проходят | **закрыт**, ниже |
| 5 | `./build so` | **не исполним на этой машине**, ниже |
| 6 | `./build st --clear` | **закрыт**: `229` против эталонных `228`, `+1` — `vt_grid.cpp` |
| 7 | `unit_tests` без `-k` | **закрыт**: `OK: 967`, `EXIT=0`, поимённо `+1` строка |
| 8 | Питоновский набор поимённо | **закрыт**: 20 красных побайтово те же, `skipped 54 → 17` |
| 9 | Седьмая фикстура и три мутации | **закрыт**, §5 |
| 10 | `vterm_boundary`: сколько переходов закрылось | **шесть из шести**, ниже |
| 11 | Пять гвардов зелёные, четыре — доказаны пробой кодом | **закрыт**, ниже |
| 12 | `.gitignore` знает про `example` | **закрыт**: `/example` добавлен, `git status` чист при собранном бинарнике |
| 13 | Список сломанных целей не вырос | **закрыт**: `st_memprofile` и `main_fuzz`, третьей нет |

### Критерий 4 — фасад собирается, 37 тестов исполняются

```
$ ./build example                                                 ; EXIT=0
[LD] {174/174} $(B)/example

$ SHITTY_TEST_BINARY=… SHITTY_PRETTY_TEST_BINARY=… SHITTY_TOML_DUMP_BINARY=… \
  SHITTY_EMBED_EXAMPLE_BINARY="$PWD/.build/example" SHITTY_TEST_FONTCONFIG=0 \
  SHITTY_TEST_PLATFORM=cocoa SHITTY_TEST_VERSION=2026.09.04 \
  python3 -m unittest discover -s tst -p 'test_embed_example.py' -v   ; EXIT=0
Ran 37 tests in 0.748s
OK

$ grep -c 'skipped' <лог>
0
```

`skipped` ноль, `OK` без скобок — то есть все 37 **исполнены**. Ловушка,
на которой этот прогон один раз соврал: без `SHITTY_TEST_BINARY` тринадцать из
37 краснеют с `Error: unknown option: --test-fd` — они сверяются с полным
терминалом, а не только с фасадом.

### Критерий 5 — `./build so` исполнить нельзя, и вот чем это заменено

```
$ ./build so
build: unknown target or group: so
$ ./build a
build: unknown target or group: a
```

Причина поимённо: оба узла заведены внутри `if linux:` (`build.py`, блок
`embed_facade_links`), а машина — darwin. Это не отказ сборки, а отсутствие цели;
docker для настоящей проверки не поднимался (§9).

Проверено то же свойство, ради которого переносился `vt_headless.cpp`:
`link_shared.py:22-32` требует `-Wl,--whole-archive` над архивом ядра плюс
`-Wl,--no-undefined`. Darwin-эквивалент — `-Wl,-force_load` над тем же архивом и
подразумеваемый `-undefined error`:

```
$ ar t <libshitty_vt_core.a>
… vt_geometry.cpp.o vt_grid.cpp.o vt_host.cpp.o vt_test.cpp.o vt_trace.cpp.o vterm.cpp.o
  (vt_headless.cpp.o в архиве отсутствует)

$ c++ -dynamiclib -o /tmp/A-libshitty_vt.dylib \
      -Wl,-force_load,<libshitty_vt_core.a> <libstd_pic.a> <libplt_headless.a> \
      -lpthread -lm                                                ; EXIT=0
-rwxr-xr-x  1212632  /tmp/A-libshitty_vt.dylib
$ nm -gU /tmp/A-libshitty_vt.dylib | grep -c '_shitty_vt_'
8
```

Ни одного неразрешённого символа при загруженном целиком архиве ядра — это ровно
то, что проверяет `--no-undefined`.

**Негативный контроль**, называющий цену переноса поимённо. Тот же линк с
добавленным объектником `vt_headless.cpp` (то есть в состоянии «файл остался в
`lib/vterm`»):

```
$ c++ -fPIC -c lib/shitty/vt_headless.cpp -o /tmp/A-vt_headless.o   ; EXIT=0
$ nm -u /tmp/A-vt_headless.o | c++filt | grep -E 'Composer|windowPane'
windowPane(Composer const&)
Composer::installVtHost()
Composer::resize(unsigned short, unsigned short)
Composer::contentInsets() const

$ c++ -dynamiclib … -Wl,-force_load,<core.a> /tmp/A-vt_headless.o … ; EXIT=1
Undefined symbols for architecture arm64:
  "windowPane(Composer const&)", … "Composer::installVtHost()", …
  "Composer::resize(unsigned short, unsigned short)", … "Composer::contentInsets() const"
ld: symbol(s) not found for architecture arm64
```

**Четыре символа, а не два.** Решение (§8.1) считало два — `contentInsets()` и
`resize()` — потому что мерило `vterm.o`. У `vt_headless.o` их четыре:
добавляются `installVtHost()` и `windowPane()`. Отсюда же следует, что перенос
файла был не «размен ради опрятности».

### Критерий 6 — `./build st --clear`

```
$ ./build st --clear   (master, до правок)                        ; EXIT=0
[LD] {228/228} $(B)/st
$ ./build st --clear   (feat/A-vthost-insets)                     ; EXIT=0
[LD] {229/229} $(B)/st
```

`228 → 229`, ровно `+1`: новая единица трансляции `lib/vterm/vt_grid.cpp`
(парный `.cpp` к заголовку, `STYLE.md:24-26`). `vt_headless.cpp` сменил каталог,
но обе глоб-строки `all_libshitty_sources` покрывают и `lib/shitty`, и
`lib/vterm`, поэтому переезд узлов не добавил и не убрал.

### Критерий 7 — `unit_tests`

```
$ ./build unit_tests pty_test_helper                              ; EXIT=0
$ SHITTY_PTY_TEST_HELPER="$PWD/.build/pty_test_helper" \
      ./.build/unit_tests < /dev/null                             ; EXIT=0
OK: 967

$ diff <эталон 966 имён> <967 имён>
965a966
> + VtermHeadless::TheWindowReportsCountTheWindowsOwnReserveAndNotThePanesBorder
```

Эталон снят на этом дереве **до** первой правки: `OK: 966`, `EXIT=0`. Разница —
одна строка, новый сторож. Ни один тест не исчез и ни один не покраснел.

### Критерий 8 — питоновский набор

Режим один и тот же с обеих сторон: одним процессом, `--group=0 --group-count=1`,
с окружением из `build.py:1274-1283`.

```
эталон (master):  Ran 6437 tests in 114.757s
                  FAILED (failures=6, errors=14, skipped=54, expected failures=549)
итог:             Ran 6437 tests in 129.836s
                  FAILED (failures=6, errors=14, skipped=17, expected failures=549)

$ diff <эталон: 20 красных> <итог: 20 красных>
(пусто)   — СПИСОК КРАСНЫХ ПОБАЙТОВО ТОТ ЖЕ

$ grep -c 'test_embed_example' <итог>            → 37
$ grep -c 'test_embed_example.*skipped' <итог>   → 0
```

`skipped 54 → 17` — это ровно те 37, что были пропущены по отсутствию артефакта
и теперь исполняются. Двадцать красных — унаследованные средовые
(`fontconfig`/`freetype`/`harfbuzz` выключены `optional_pkg()` на macOS), тот же
список, что у `M7` и `G16`.

### Критерий 10 — переходы границы

Не «гвард зелёный» — он зелёный и был. Проверяемое утверждение: сколько
нарушений даёт `check()` при **обнулённом** `ALLOWANCE`, на обоих деревьях,
каждое своим скриптом:

```
--- master: с обнулённым ALLOWANCE ---
    vt_headless.cpp:11: "composer.h"      does not resolve inside lib/vterm
    vt_headless.cpp:12: "pane_layout.h"   does not resolve inside lib/vterm
    vt_headless.cpp:13: "grid_geometry.h" does not resolve inside lib/vterm
    vterm.cpp:22: "session.h"             does not resolve inside lib/vterm
    vterm.cpp:24: "composer.h"            does not resolve inside lib/vterm
    vterm.cpp:27: "grid_geometry.h"       does not resolve inside lib/vterm
    total: 6
--- feat/A-vthost-insets: с обнулённым ALLOWANCE ---
    total: 0
```

**Шесть из шести, а не два и не четыре.** Расхождение с ожиданием брифа
объясняется полностью: «два из шести» — вариант, где `cellCapacityExcept()`
остаётся `T6.1`; «четыре из шести» — вариант решения, где `vt_headless.cpp`
остаётся в `lib/vterm`. Задача делала оба шага, поэтому закрылись все шесть, и
`ALLOWANCE` стал пустым словарём. Осиротевших ключей нет по построению: словарь
пуст, `set(ALLOWANCE) - seen` пусто.

### Критерий 11 — пять гвардов

Четыре сканирующих запущены **напрямую программами из `build.py`**, минуя
`./build` (кеш адресуется содержимым и подставляет готовый штамп — `CLAUDE.md`):

```
=== border_pixels_guard_program:  EXIT=0
=== mouse_geometry_guard_program: EXIT=0
=== pane_grid_guard_program:      EXIT=0
=== darwin_call_guard_program:    EXIT=0
$ python3 lib/vterm/check_includes.py lib/vterm <stamp>           ; EXIT=0
```

Пробы — **кодом**, не комментарием, каждая в файле, который соответствующий
гвард действительно читает (`pane_grid_guard` читает только файлы на `render*`):

| Гвард | Проба | Результат |
|---|---|---|
| `border_pixels` | `const u16 borderProbe = composer.borderPixels();` в `lib/shitty/vt_headless.cpp` | `EXIT=1`, `vt_headless.cpp:229` |
| `mouse_geometry` | `mouseGeometry(composer.geometry);` там же (один аргумент) | `EXIT=1`, `vt_headless.cpp:230` |
| `pane_grid` | `const u16 paneGridProbe = composer.geometry.columns;` в `lib/shitty/render_reference.cpp` | `EXIT=1`, `render_reference.cpp:38` |
| `darwin_call` | `createMetalRenderer(composer);` вне `#if __APPLE__` | `EXIT=1`, `vt_headless.cpp:231` |
| `vterm_boundary` | `#include "composer.h"` в `lib/vterm/vt_grid.cpp` | `EXIT=1`, `vt_grid.cpp:11` |

Все пробы сняты, все пять гвардов после снятия снова `EXIT=0`.

### Критерий 13 — сломанные цели

```
$ ./build -k st pt st_memprofile st_test pt_test main_fuzz st_test_prod_parser \
           pt_test_prod_parser pty_test_helper unit_tests toml_dump parser_perf \
           core_perf example                                       ; EXIT=1
FAIL $(B)/obj/st_memprofile/lib/shitty/heap_profile.cpp.o
FAIL $(B)/main_fuzz
build: 3 node(s) failed, 2 requested target(s) broken

$ grep ' error: ' <лог> | sort -u
lib/shitty/heap_profile.cpp:18:10: fatal error: 'gperftools/heap-profiler.h' file not found
clang++: error: linker command failed with exit code 1
```

Две, обе известные: `st_memprofile` — средовая (нет gperftools),
`main_fuzz` — линковка, и её причина проверена отдельно, чтобы исключить мой
переезд заголовка: `Undefined symbols … "_main", referenced from <initial-undefines>`,
то есть отсутствующая точка входа фуззера, а не `VtermHeadless`. Третьей цели
не появилось. `example` и `libshitty_vt_core` теперь собираются, а не отсутствуют.

Дополнительно, ловушки `CLAUDE.md`:

```
$ python3 tst/pretty_binary_branding.py .build/pt                  ; EXIT=0
$ python3 tst/production_surface.py  (с тремя переменными)         ; Ran 5 tests / OK
```

---

## 7. Цена, числом

```
$ git diff --stat master..HEAD
 22 files changed, 419 insertions(+), 184 deletions(-)
```

Точные числа — `git diff --numstat master..HEAD`, добавлено/удалено:

| Файл | Что | + | − |
|---|---|---:|---:|
| `lib/vterm/vt_grid.h` | **новый** | 74 | 0 |
| `lib/vterm/vt_grid.cpp` | **новый**, парный TU (`STYLE.md:24-26`) | 10 | 0 |
| `lib/vterm/vt_host.h` | три метода с комментариями | 45 | 0 |
| `lib/vterm/vterm.cpp` | 5 мест, член, 2 сигнатуры, 3 включения | 22 | 25 |
| `lib/vterm/vterm.h` | сигнатура, `struct Composer;`, комментарии | 13 | 11 |
| `lib/vterm/vt_geometry.cpp` | `resize()` через `vt_grid.h` | 10 | 6 |
| `lib/vterm/check_includes.py` | разрешение опустело | 17 | 66 |
| `lib/shitty/composer.cpp` | три метода `ComposerVtHost` | 31 | 0 |
| `lib/shitty/composer.h` | `vtInsets()` переехал сюда | 14 | 0 |
| `lib/shitty/grid_geometry.h` | четыре функции — переходники | 14 | 8 |
| `lib/shitty/pane_layout.h` | `vtInsets()` уехал | 0 | 8 |
| `lib/{vterm→shitty}/vt_headless.cpp` | **переезд** + вызов `create` | 3 | 3 |
| `lib/{vterm→shitty}/vt_headless.h` | **переезд** + комментарий | 13 | 5 |
| `lib/shitty/vt_headless_ut.cpp` | 13 вызовов + новый тест | 89 | 14 |
| `lib/embed/shitty_vt.cpp` | три метода, `paneResized`, аргумент | 34 | 2 |
| `build.py` | флаг и комментарий | 24 | 31 |
| `.gitignore`, `bin/*`, `lib/shitty/{main,pty_ut,session}.cpp` | по строке | 5 | 4 |

Прогноз решения (§6) — «двенадцать изменённых файлов, три новых, порядка
+220/−125» — занижен: файлов двадцать два, добавлений 419. Разница в трёх
местах: комментарии к трём методам `VtHost` (`+45` против `+32`),
`vt_headless_ut.cpp` (`+89/−14` против `+58/−13` — новый тест вышел длиннее
обещанных сорока пяти строк, потому что несёт четыре отрицательных утверждения)
и переезд `vt_headless.{h,cpp}`, которого решение в смету не включало вовсе.

---

## 8. Обнаружено

**1. У `vt_headless.o` четыре неразрешённых символа `lib/shitty`, а не два.**
Решение и `M7` мерили `vterm.o` и получили два. Для линковки фасада значение
имеет объединение: четыре (`contentInsets`, `resize`, `installVtHost`,
`windowPane`). Ни на что в этой задаче не влияет — файл переехал, — но цифра
«два» в документах относится только к `vterm.o`.

**2. Следующий мерж принесёт `lib/vterm/vt_headless.{h,cpp}` обратно.** Апстрим
владеет обоими путями (`git ls-tree origin/master lib/vterm/` показывает оба).
Наше удаление даст конфликт «изменён/удалён», а если апстримный файл при
разрешении принять, он попадёт в глоб `lib/vterm/*.cpp` **и в embed-цель**, и
`VtermHeadless::create` определится дважды. Это должно быть записано как условие
следующего шага мержа; я его не записывал никуда, кроме этого отчёта и
комментария в `lib/shitty/vt_headless.h`.

**3. `bin/core_perf/main.cpp` перестал быть побайтово апстримным.** Он расходился
на одну строку после `M6e`; теперь на две — вторая это `<lib/shitty/vt_headless.h>`.
Цена названа решением заранее, но названа она была как цена **`.cpp`**; заплачена
за `.h`.

**4. Утверждение `M7` о том, что закрывается `mouse_frontend.h → composer.h`,
неверно.** В разрешении такого ключа нет и не было: `mouse_frontend.*` живут в
`lib/vterm` и `composer.h` не включают (это отдельно проверял `T5.1`). Шесть
ключей — это `vt_headless.cpp` × 3 и `vterm.cpp` × 3, и ровно они и закрылись.

**5. Питоновский набор молча врёт при повисшем симлинке.** `./build <цель>`
снимает симлинки прочих целей (`CLAUDE.md` знает это про `--clear`, но это верно
и без него), а `.build/st_test` при этом остаётся **битым симлинком**: `ls -l`
его показывает, `test -x` — нет. В таком состоянии набор даёт
`Ran 6065 … errors=6709` вместо `Ran 6437 … errors=14`: **тестов на 372 меньше**,
потому что часть параметризуется во время сбора, и шесть тысяч одинаковых
`FileNotFoundError` вместо одного внятного отказа. Я получил этот результат
дважды, прежде чем заметил. Годная строка в `CLAUDE.md`: перед прогоном набора
проверять `test -x .build/st_test`, а не наличие имени.

**6. `./build so` и `./build a` не существуют на darwin.** Они заведены под
`if linux:`, и `./build so` отвечает `unknown target or group: so` — то есть
критерий «`./build so` собирается» на этой машине непроверяем в принципе, и это
свойство `build.py`, а не поломка. Стоит внести в бриф следующей задачи, иначе
его снова назначат как проверяемый.

**7. `lib/shitty/grid_geometry.h` по-прежнему без парного `.cpp`.** `STYLE.md:24-26`
требует его от каждого заголовка. Досталось от волны 6, я не трогал: файл не мой
по существу и правка была бы вне задачи.

**8. `lib/vterm/vt_geometry.cpp` сохранил `#include <std/alg/minmax.h>` и
`using namespace stl;`, которые после переезда формулы ничего не используют.**
Оставлено намеренно: `vt_grid.h` тянет `minmax.h` сам, и опираться на
транзитивное включение хуже, чем держать явное лишнее.

---

## 9. Что я не проверял

1. **`./build so` и `./build a` по-настоящему.** Docker не поднимался: на этой
   машине он arm64, а сборка требует clang с `-std=c++26`, и цена подъёма
   несоразмерна. Проверен darwin-эквивалент (§6, критерий 5) — он проверяет
   ровно то же требование `--no-undefined` над целиком загруженным архивом, но
   это не GNU ld, не version script и не Linux.
2. **Поведение при `contentScale != 1`.** Ни один тест этой задачи его не
   трогает; `scaledPixels()` не менялся ни строкой.
3. **Рендереры.** `R4-qa` пишет, что рендерер локально не собирается ни одной
   конфигурацией; `render_metal.mm` компилируется в составе `st`, но никакой
   картинки я не смотрел.
4. **`./build test` целиком.** По `CLAUDE.md` цель принципиально не зелёная (24
   узла), и критерием она не была; вместо неё — поимённая сверка обоих наборов.
5. **Долговременное поведение `surfaceResized()` в GUI.** Строка исполняется на
   пути `CSI 4t/8t/9t` при `allowWindowOps`, который по умолчанию выключен;
   покрытие у неё то же, что было, — python-набор через `test_mode`. Я перенёс
   её дословно и не улучшал покрытие.
6. **Сверка `lib/embed` с апстримом построчно.** Правки в `shitty_vt.cpp`
   минимальные и проверены компилятором и 37 тестами; но `TerminalUpdate`,
   `VtermState` и `shitty_vt_cell` я построчно с `60562f22` не сверял — это
   делал `M7`.

---

## Что осталось `T6.1`

- **Область сканирования гвардов.** `lib/embed` вне `guard_scan_roots`
  (`build.py:1423`), `.c` вне `guard_scan_suffixes` (`build.py:1424`). Пять
  гвардов не читают ни `lib/embed/shitty_vt.cpp`, ни `bin/example/main.c` — а
  теперь оба в графе сборки, то есть слепая зона стала больше, чем была у `M7`.
  Правка того же класса, что делала `T2.1`: корни и суффиксы, не разрешения.
- **Возврат апстримного `lib/vterm/vt_headless.{h,cpp}` при следующем мерже**
  (§8, находка 2). Решение о том, что делать с конфликтом «изменён/удалён», надо
  принять до мержа, а не в нём.
- **Не фиксировать поверхность фасада на состоянии `M7`.** Двадцать четыре
  коммита между `44d61bfc` и `origin/master` меняют `lib/embed`;
  `shitty_vt_memory_usage` и `shitty_vt_set_save_lines` приходят в `M8`, и
  `tst/test_embed_example.py` вырастет вместе с ними. Флаг `embed_facade_links`
  теперь `True`, значит эти тесты будут исполняться, а не пропускаться, — и
  красное в них будет настоящим.
- **`grid_geometry.h` без парного `.cpp`** (§8, находка 7).
- **Строка в `CLAUDE.md` про битый симлинк `.build/st_test`** (§8, находка 5).
  Я её не вносил: `CLAUDE.md` вне моих файлов.

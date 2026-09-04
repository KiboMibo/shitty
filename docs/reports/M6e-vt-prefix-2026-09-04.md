# M6e. Префикс `vt_`, и мерж, в котором изменились только строки `#include`

**Дата:** 2026-09-04 · **Задача:** `M6e`, волна 6 плана `docs/plans/2026-08-29-upstream-merge.md` · **Ветка:** `wave/upstream-merge-w6e` · **Коммит мержа:** `e659cf5c`, родители `fa6a5f4f` + `7e2a8e3e` · **Вердикт:** `DONE с двумя унаследованными красными` — сборка `st`/`pt` зелёная (228 узлов, как до мержа), `unit_tests` даёт `OK: 950` при побайтово том же списке имён, питоновский набор `6399` тестов и те же двадцать красных, четыре сканирующих гварда зелены и доказаны шестнадцатью пробами, `vterm_boundary` красный на тех же восьми строках; список сломанных программ не вырос — те же `main_fuzz` и `core_perf`.

Десятый и последний мерж-шаг волны 6. Самый механический за весь план: **36 содержательных строк, и все 36 — `#include`**.

---

## 1. Коммит

| Коммит | Что |
|---|---|
| `e659cf5c` | `merge wave 6 step M6e: the core's satellite files take the vt_ prefix` — сам мерж, родители `fa6a5f4f` + `7e2a8e3e` |

Предмержевая голова — `fa6a5f4f`. Слит один апстримный коммит:

```
7e2a8e3e Rename the core's vterm_ files to the vt_ prefix
```

---

## 2. Что принёс апстрим

Шесть файлов ядра меняют имя, приводя спутников `Vterm` к префиксу, заданному ещё `M6c`/`M6d` (`vt_config`, `vt_geometry`, `vt_host`):

| Было | Стало |
|---|---|
| `lib/vterm/vterm_headless.{h,cpp}` | `lib/vterm/vt_headless.{h,cpp}` |
| `lib/vterm/vterm_test.{h,cpp}` | `lib/vterm/vt_test.{h,cpp}` |
| `lib/vterm/vterm_trace.{h,cpp}` | `lib/vterm/vt_trace.{h,cpp}` |
| `lib/shitty/vterm_headless_ut.cpp` | `lib/shitty/vt_headless_ut.cpp` |

`vterm.{h,cpp}` имя сохраняет — это файл самого класса `Vterm`, а не спутник; так сказано в сообщении апстримного коммита, и на дереве это подтверждается: `vterm.cpp` в списке переименований отсутствует.

### 2.1. Механичность — первым же фильтром

`-U0`-дифф от `#include` показал, что содержания в коммите нет вовсе:

```
$ git diff -U0 -M 7e2a8e3e~1 7e2a8e3e | grep -cE '^[+-][^+-]'   → 32
$ git diff -U0 -M 7e2a8e3e~1 7e2a8e3e | grep -cE '^[+-]#include' → 32
```

Тридцать две строки из тридцати двух — включения. Ни одного объявления, ни одной сигнатуры, ни одного тела. Апстрим: `15 files changed, 16 insertions(+), 16 deletions(-)`.

Тот же фильтр по **нашему** мержу:

```
$ git diff -U0 -M fa6a5f4f HEAD | grep -cE '^[+-][^+-]'   → 36
$ git diff -U0 -M fa6a5f4f HEAD | grep -cE '^[+-]#include' → 36
$ git diff -U0 -M fa6a5f4f HEAD | grep -E '^[+-][^+-]' | grep -v '^[+-]#include'
(ни одной строки)
```

**Это же и есть проверка ловушки 5 («мина `static_cast` через слушателей»).** Мина требует изменённой строки не-включения: приведение типа, подмена слушателя, механический перевод вызова. Таких строк в шаге ноль — ни в апстримной части, ни в нашей. Проверять «то ли лежит в слушателях» здесь не на чем: `listener.h`, `listener.cpp` и все объявления `override` шагом не тронуты, `git diff -M fa6a5f4f HEAD -- lib/vterm/listener.*` пуст.

### 2.2. Число узлов

Цель `st`: **228 → 228**. Переименование файла имя узла не меняет: `build.py` собирает `lib/vterm/**` глобом (`build.py:765`), и шесть переименований внутри одного каталога дают ровно те же шесть единиц трансляции.

---

## 3. Конфликты: два хунка, оба — блок включений

```
lib/vterm/vterm.cpp 1 · lib/shitty/vt_headless_ut.cpp 1
```

Тринадцать остальных файлов апстрим смержил сам. Из них `bin/core_perf/main.cpp` лёг **побайтово апстримным** (`git diff --stat 7e2a8e3e HEAD -- bin/core_perf/main.cpp` пуст) — это понадобится в §9.

Оба конфликта разрешены **одинаково: взята наша сторона, к ней применено апстримное переименование.** Ни одной апстримной строки не отброшено, ни одной нашей не потеряно.

### 3.1. `lib/vterm/vterm.cpp` — наши три включения против апстримных двух переименований

```
<<<<<<< HEAD
#include "session.h"
#include "composer.h"
#include "utf8_dfa.h"
#include "vterm_test.h"
#include "vterm_trace.h"
#include "grid_geometry.h"
=======
#include "vt_test.h"
#include "utf8_dfa.h"
#include "vt_trace.h"
>>>>>>> 7e2a8e3e
```

Ложный конфликт соседства: апстрим переименовал две строки внутри блока, в который `M6b`/`M6c` добавили три наших (`session.h`, `composer.h`, `grid_geometry.h`). Три наших — **наша сторона**, они держат обращения к эмбеддеру, которые снимут `T5.1`, `T5.2` и `T5.4`; переименование — **апстримная сторона**, это предмет коммита.

Результат — объединение с применённым переименованием. Порядок внутри блока задан `dev/style.py reorder_includes` (звать `style.py` целиком нельзя — локальный clang-format 23.1 переформатирует ~130 чужих файлов); инструмент передвинул `vt_test.h` на одну позицию вверх, и блок принял канонический вид:

```c++
#include "pty.h"
#include "parser.h"
#include "screen.h"
#include "session.h"
#include "vt_test.h"
#include "composer.h"
#include "utf8_dfa.h"
#include "vt_trace.h"
#include "grid_geometry.h"
#include "term_features.h"
```

Это ровно апстримный блок (`pty`, `parser`, `screen`, `vt_test`, `utf8_dfa`, `vt_trace`, `term_features`, …) плюс три наших имени, вставленных туда, куда их кладёт сортировка проекта.

### 3.2. `lib/shitty/vt_headless_ut.cpp` — наш файл на 858 строк тестов против апстримной одной строки

```
<<<<<<< HEAD:lib/shitty/vterm_headless_ut.cpp
#include <lib/vterm/vterm_test.h>
#include <lib/vterm/vterm_trace.h>
#include <lib/vterm/vterm_headless.h>
#include <lib/vterm/cell_extra_store.h>

#if defined(HAVE_METAL_RENDERER)
    #include "render_metal.h"
#endif

#include <plt/fiber.h>
#include <plt/platform.h>
#include <plt/platform_headless.h>
#include <plt/window.h>
=======
#include <lib/vterm/vt_headless.h>
>>>>>>> 7e2a8e3e:lib/shitty/vt_headless_ut.cpp
```

Апстримный вариант этого файла включает **одну** строку — его безголовому тесту больше ничего не нужно. Нашему нужны рендерер, платформа и окно: pane-тесты волны панелей гоняют настоящий `ReferenceRenderer` и `plt::Window`. **Взята наша сторона целиком**, переименование применено к трём её строкам. `reorder_includes` файл не изменил ни на байт.

Переименование самого файла (`vterm_headless_ut.cpp` → `vt_headless_ut.cpp`) git распознал сам, `R099`, и `+858` строк наших тестов приехали внутрь.

### 3.3. Два включения, до которых апстримное переименование не дотянулось

Ровно тот класс, что в `M6d` §3.5: строка есть только у нас, поэтому механическая замена апстрима её не касается, а смержилась бы она молча и перестала бы компилироваться.

```
$ diff <(апстримные +/- строки) <(наши +/- строки)
> -#include <lib/vterm/vterm_test.h>
> -#include <lib/vterm/vterm_trace.h>
> +#include <lib/vterm/vt_test.h>
> +#include <lib/vterm/vt_trace.h>
```

Обе — в `lib/shitty/vt_headless_ut.cpp`, обе переведены руками. **Это вся наша правка сверх апстримной**, и это единственная причина, по которой наш дифф — `18 insertions(+), 18 deletions(-)` против апстримных `16/16`.

Проверено, что недоехавших не осталось нигде:

```
$ git grep -n 'vterm_headless\|vterm_test\|vterm_trace' -- . ':!docs'
build.py:4048:libvterm_tests = []
build.py:4051:    libvterm_tests.append(command(
build.py:4433:    *libvterm_tests,
```

Три попадания — подстрока `vterm_test` внутри `libvterm_tests`, набора фикстур `tst/libvterm/`. К переименованным файлам отношения не имеют.

---

## 4. Гварды: чем доказано, что каждый **видит** переименованный код

Это главный вопрос шага. `M6` показал, чего стоит зелёный гвард: `pane_grid_guard` был зелёным над неохраняемым `A9`, потому что знал написание, угаданное `T2.1`. **Массовое переименование — ровно тот случай**, поэтому по каждому гварду проверялось не «зелёный ли», а «краснеет ли он на пробе, поставленной в файл с новым именем».

Все прогоны — **прямым исполнением программы гварда**: текст вынут из `build.py` разбором AST и исполнен вне `./build` (`CLAUDE.md`: `.build/` адресуется содержимым и подставляет штамп из `cas/`, ничего не исполнив).

Состояние на смерженном дереве:

```
border_pixels_guard    EXIT=0   output bytes=0
mouse_geometry_guard   EXIT=0   output bytes=0
pane_grid_guard        EXIT=0   output bytes=0
darwin_call_guard      EXIT=0   output bytes=0
vterm_boundary         EXIT=1   8 строк, 3 файла
```

### 4.1. Двенадцать проб в переименованный и изменённый код

Каждая проба — **строка кода**, приписанная в конец файла, и откат подтверждён `git diff --quiet` по этому файлу. Комментарием пробовать нельзя: `blanked()` вычищает тела комментариев до сканирования, и на `M6c` комментарная проба «зеленила» все четыре гварда сразу (проба 5 ниже — контроль именно на это).

| № | Гвард | Файл (что это) | Проба | Результат |
|---|---|---|---|---|
| 1 | `border_pixels` | `lib/vterm/vt_headless.cpp` — **переименован** | `u16 m6eProbe() { return composer.borderPixels(); }` | `EXIT=1`, `lib/vterm/vt_headless.cpp:262` |
| 2 | `border_pixels` | `lib/vterm/vt_test.h` — **переименован**, заголовок | `u16 m6eProbe() { return composer.scaledPixels(1); }` | `EXIT=1`, `lib/vterm/vt_test.h:70` |
| 3 | `border_pixels` | `lib/shitty/vt_headless_ut.cpp` — **переименован** | `u16 m6eProbe() { return composer.borderPixels(); }` | `EXIT=1`, `lib/shitty/vt_headless_ut.cpp:1926` |
| 4 | `border_pixels` | тот же `vt_headless.cpp` | `composer.geometry.borderPixels + composer.geometry.borderPixels` | `EXIT=0` — **поле апстрима не считается, форма вызова считается** |
| 5 | `border_pixels` | тот же `vt_headless.cpp` | `// composer.borderPixels() and composer.scaledPixels(1)` | `EXIT=0` — комментарий в зачёт не идёт, ловушка 3 воспроизведена |
| 6 | `mouse_geometry` | `lib/vterm/vt_trace.cpp` — **переименован** | `mouseGeometry(composer)` | `EXIT=1`, `lib/vterm/vt_trace.cpp:9` |
| 7 | `mouse_geometry` | `lib/vterm/vt_headless.cpp` — **переименован** | `mouseGeometry(composer)` | `EXIT=1`, `lib/vterm/vt_headless.cpp:262` |
| 8 | `mouse_geometry` | `lib/vterm/vt_trace.cpp` | `mouseGeometry(composer, 1, 2, 3, 4)` | `EXIT=0` — форма с origin не считается |
| 9 | `pane_grid` | `lib/shitty/render_reference.cpp` — **изменён этим шагом** | `composer.geometry.columns + composer.geometry.rows` | `EXIT=1`, две строки `render_reference.cpp:1162` |
| 10 | `pane_grid` | `lib/shitty/render_metal.mm` | `composer.vt.columns + composer_.rows` (написание `M6`, мёртвое) | `EXIT=1`, две строки `render_metal.mm:1173` |
| 11 | `darwin_call` | `lib/shitty/main.cpp` — **изменён этим шагом** | `createCsdTabsUi(composer)` | `EXIT=1`, `lib/shitty/main.cpp:164` |
| 12 | `darwin_call` | `lib/shitty/vt_headless_ut.cpp` — **переименован** | `createCsdTabsUi(composer)` | `EXIT=1`, `lib/shitty/vt_headless_ut.cpp:1926` |

**Оговорка по `pane_grid`, та же, что у `M6d`, и сказанная прямо.** Гвард читает только файлы, чьё имя начинается на `render` (`if not path.name.startswith("render"): continue`), а ни один рендерер этим шагом не переименован. Поэтому проба «в переименованный файл» для него невозможна, и доказано другое: он видит файл, **изменённый этим шагом** (проба 9), и по-прежнему знает оба написания словаря (проба 10).

### 4.2. Самопроверки: что было бы, переименуй шаг ключ разрешения

Ловушка 4 брифа — «гвард слепнет от переименования». Проверена не рассуждением, а имитацией: файл, который стоит **ключом в разрешении**, временно переименован по шаблону этого шага.

| № | Что сымитировано | Результат |
|---|---|---|
| 13 | `lib/vterm/mouse_frontend.cpp` → `lib/vterm/vt_mouse_frontend.cpp` (как если бы шаг захватил и его) | `EXIT=1`: `Unallowed uses: lib/vterm/vt_mouse_frontend.cpp:24` — определение по новому пути **немедленно неучтено**, гвард краснеет, а не слепнет |
| 14 | `lib/shitty/composer.cpp` убран из области сканирования | `EXIT=1`: «re-key the allowance onto where these live now» — `Unreachable: lib/shitty/composer.cpp` |
| 15 | `lib/shitty/render_vk.cpp` убран из области сканирования | `EXIT=1`: «point the scan roots at where these live now» — `Unreachable: render_vk.cpp` |
| 16 | `darwin_call`: корни сканирования подменены на пустое множество | `EXIT=1`: «the darwin call audit tracks nothing at all, which means it stopped working» |

Проба 13 — прямой ответ на вопрос шага. **Переименование ключа разрешения не даёт зелёной пустоты: оно даёт красный с двух сторон сразу** — узаконенное обращение всплывает как неузаконенное (13), а осиротевший ключ ловится самозащитой (14). Это защита, которой `T2.1` этот механизм и снабдила.

### 4.3. Разрешения не расширялись — и перекеивать было нечего

```
$ git diff -M fa6a5f4f HEAD -- build.py | wc -l
0
```

Ни один ключ разрешений этим шагом не переезжал, поэтому перекеивание, которое `docs/plans/reviews/upstream-merge-invariants.md` §3.3 предписывает при переезде файла, не понадобилось. Проверено поимённо:

- `border_pixels_allowance`: `lib/shitty/composer.{h,cpp}`, `composer_ut.cpp`, `mouse_frontend_ut.cpp`, `test_mode.cpp` — ни одного переименования среди них;
- `mouse_geometry_allowance`: `lib/vterm/mouse_frontend.{h,cpp}` — не тронуты;
- `pane_grid_backends`: `render_metal.mm`, `render_reference.cpp`, `render_vk.cpp` — не тронуты;
- `guard_scan_roots` (`lib/shitty`, `lib/vterm`, `ext/plt`, `bin`) покрывают все шесть новых имён, что и показывают пробы 1, 2, 3, 6, 7, 12.

### 4.4. `vterm_boundary` — список не вырос

```
$ python3 lib/vterm/check_includes.py lib/vterm /tmp/vb.stamp ; EXIT=1
mouse_frontend.h:9:  "composer.h"       does not resolve inside lib/vterm
vt_headless.cpp:11:  "options.h"
vt_headless.cpp:12:  "composer.h"
vt_headless.cpp:13:  "pane_layout.h"
vt_headless.cpp:14:  "grid_geometry.h"
vterm.cpp:22:        "session.h"
vterm.cpp:24:        "composer.h"
vterm.cpp:27:        "grid_geometry.h"
```

**Восемь строк в трёх файлах — те же самые.** Номера строк сдвинулись (`vterm.cpp:23` → `:24` из-за перестановки `vt_test.h`), имя файла сменилось (`vterm_headless.cpp` → `vt_headless.cpp`), поэтому сверка велась **по содержанию**, как и предписано критерием:

```
$ diff <(эталон, с переименованием файла и без номеров строк) <(после мержа, без номеров строк)
(пусто)   — 8 из 8 совпали как пара «файл + включение»
```

---

## 5. `A1`, `A8`, `A9`, `A10` — чем проверено

| Инвариант | Чем проверено |
|---|---|
| `A1` (граница — опция эмбеддера) | `border_pixels_guard` зелёный при неизменных разрешениях и доказан пробами 1–3; проба 4 подтверждает, что скобка по-прежнему отделяет наш метод от апстримного поля. `geometry.borderPixels` **по-прежнему не заполняется**: во всём дереве это имя встречается только в объявлении `vt_geometry.h:37` с умолчанием `0`, в арифметике `vt_geometry.cpp:30` и в трёх комментариях `composer.h` — присваивания нет ни одного, ровно как оставил `M6c`. Место для варианта «А» задачи `T5.1` не тронуто |
| `A8` (пиксельная мышь считает от начала панели) | четыре вызова `mouseGeometry(…, originX_, originY_, paneWidth_, paneHeight_)` в `vterm.cpp` (1648, 2053, 2485, 9778) — **посимвольно те же и на тех же номерах строк**, что на `fa6a5f4f`; `mouse_geometry_guard` зелёный и доказан пробами 6–7 |
| `A9` (рендерер не читает грид окна) | весь вклад шага в рендереры — **одна строка**: `#include <lib/vterm/vterm_test.h>` → `vt_test.h` в `render_reference.cpp`. `render_metal.mm` и `render_vk.cpp` не тронуты вовсе. `pane_grid_guard` зелёный, доказан пробами 9–10, самозащита — 15 |
| `A10` (разделитель панелей) | `session.{h,cpp}`, `pane_layout.{h,cpp}`, `composer.{h,cpp}` шагом **не тронуты ни на строку** (`git diff -M fa6a5f4f HEAD` по ним пуст); `paneDividerWidth` живёт там же, где жил |

---

## 6. Таблица критериев

| № | Критерий | Результат |
|---|---|---|
| 1 | `./build st --clear` зелёная | **228 узлов**, `EXIT=0` — §6.1 |
| 2 | `./build unit_tests pty_test_helper` + прогон ≥ 950 | `EXIT=0`, **`OK: 950`**, список имён **побайтово** равен предмержевому — §6.2 |
| 3 | Список сломанных программ не вырос | до и после: **4 узла, 2 цели** — `main_fuzz`, `core_perf`, те же файлы и строки — §6.3 |
| 4 | Совпадение диффов, каждое расхождение отнесено | **A = 0, B = 0**; построчное расхождение — 4 строки, все в §3.3 — §6.4 |
| 5 | Питоновский набор поимённо | `Ran 6399` до и после, **6390 уникальных**, красные (20) побайтово те же — §6.5 |
| 6 | Четыре гварда видят переименованный код, проба кодом | 12 проб + 4 самопроверки — §4 |
| 7 | `vterm_boundary` не вырос | **8 строк, 3 файла**, сверено по содержанию — §4.4 |
| 8 | `A1`, `A8`, `A9`, `A10` | не нарушены — §5 |

### 6.1. Сборка

Эталон, снятый на предмержевой голове `fa6a5f4f` в этом же дереве:

```
$ ./build st --clear -j 10 ;  EXIT=0
[AR] {227/228} $(B)/libshitty_prod.a
[LD] {228/228} $(B)/st
```

После мержа — то же число:

```
$ ./build st --clear -j 10 ;  EXIT=0
[CC] {226/228} $(B)/obj/libshitty/lib/vterm/parser.cpp.o
[AR] {227/228} $(B)/libshitty_prod.a
[LD] {228/228} $(B)/st
```

`--clear` обязателен: шаг двигает пути включений, а `build.includes` в ключ узла не входит (`CLAUDE.md`).

**Свежесть бинарников — не через штамп системы сборки.** Переименование не меняет ни одного символа, поэтому `nm` здесь бесполезен; доказательство даёт **путь объектного файла**, которого до шага существовать не могло:

```
$ grep -E 'vt_headless_ut|LD\].*unit_tests' <лог сборки после мержа>
[CC] {340/427} $(B)/obj/unit_tests/lib/shitty/vt_headless_ut.cpp.o
[LD] {417/427} $(B)/unit_tests
```

Две проверки, которых `./build st pt` не запускает (`CLAUDE.md`), прогнаны отдельно:

```
$ python3 tst/pretty_binary_branding.py .build/pt                       ;  EXIT=0
$ python3 tst/production_surface.py  (с тремя переменными из build.py)  ;  EXIT=0
Ran 5 tests in 0.096s / OK
```

### 6.2. Юнит-тесты

```
$ ./build unit_tests pty_test_helper -j 10 ;  EXIT=0
$ SHITTY_PTY_TEST_HELPER="$PWD/.build/pty_test_helper" ./.build/unit_tests --threads=1
EXIT=0
OK: 950

$ diff base-ut-names.txt after-ut-names.txt
(пусто)   — 950 имён, ноль расхождений
```

Эталон до мержа в этом же дереве — **`OK: 950`**. Ловушка 1 брифа (`M6c`: зелёная сборка при 65 падающих тестах) закрыта именно этим прогоном, а не сборкой.

**Попутно сработала ловушка со стухшей символической ссылкой, и её стоит записать.** После сборки с `-k`, где две цели ломаются, `./build` **не публикует символические ссылки** `.build/st_test`, `.build/pt_test`, `.build/toml_dump` — они остаются указывать на предмержевые объекты в `cas/`. Замечено сверкой целей ссылок до и после успешной сборки: все три сменились. Питоновский набор гонялся уже на перепубликованных, `unit_tests` — на своей, проверенной отдельно (её ссылка не менялась, потому что её опубликовала успевшая цель `./build unit_tests pty_test_helper`).

### 6.3. Список сломанных программ

Команда критерия, до мержа:

```
$ ./build -k st pt st_test pt_test main_fuzz st_test_prod_parser pt_test_prod_parser \
           pty_test_helper unit_tests toml_dump parser_perf core_perf -j 10 ;  EXIT=1
bin/main_fuzz/main.cpp:121:24: error: too many arguments to function call, expected at most 3, have 4
bin/core_perf/main.cpp:84:103: error: too many arguments to function call, expected at most 3, have 4
build: 4 node(s) failed, 2 requested target(s) broken
```

После мержа — **посимвольно то же**: те же два файла, те же номера строк и колонок, те же `4 node(s) failed, 2 requested target(s) broken`. Список не вырос и не изменился.

### 6.4. Совпадение диффов

Машинно, в обе стороны, с учётом переименований (ключ — пост-образный путь):

```
$ python3 diffcmp.py <worktree> 7e2a8e3e
upstream post-image paths: 12
A. upstream-removed lines still present in the merged tree: 0
B. upstream-added lines absent from the merged tree: 0
```

**A = 0** — ни одной апстримной удалённой строки у нас не осталось. **B = 0** — ни одной апстримной добавленной у нас не пропало. Двенадцать путей, а не пятнадцать, потому что три переименования (`vt_headless.h`, `vt_test.h`, `vt_trace.h`) прошли без единой изменённой строки и в дифф не попадают.

Построчная сверка множеств `+`/`-` строк:

```
$ diff <(апстримные) <(наши)
> -#include <lib/vterm/vterm_test.h>
> -#include <lib/vterm/vterm_trace.h>
> +#include <lib/vterm/vt_test.h>
> +#include <lib/vterm/vt_trace.h>
```

Четыре строки, все наши, все в `lib/shitty/vt_headless_ut.cpp`, отнесены к §3.3: включения, которых нет в апстримной копии файла, поэтому апстримная замена до них не дотянулась.

```
апстрим:  15 files changed, 16 insertions(+), 16 deletions(-)
наш мерж: 15 files changed, 18 insertions(+), 18 deletions(-)
```

### 6.5. Питоновский набор

Прогон одной группой (`--group=0 --group-count=1`), с полным окружением из `build.py:1139`:

```
до мержа:    Ran 6399 tests in 123.920s
             FAILED (failures=6, errors=14, skipped=17, expected failures=549)
после мержа: Ran 6399 tests in 124.079s
             FAILED (failures=6, errors=14, skipped=17, expected failures=549)

$ diff base-red.txt after-red.txt   → пусто (20 строк, посимвольно те же)
$ diff base-names.txt after-names.txt → пусто (6390 уникальных имён)
```

Двадцать красных — известные средовые: `fontconfig`/`freetype`/`harfbuzz` на macOS выключены `optional_pkg()` (`build.py:205`). **Новых красных нет, второй прогон для отсева флака не понадобился.**

---

## 7. Что этот шаг сделал ради `T5.1` — ничего, и это правильно

Решение по `T5.1` (вариант «А»: четырёхсторонние `VtInsets` и `origin` в `VtGeometry`) утверждено человеком и реализуется отдельно. Шаг его не приближал и не отдалял:

- `lib/vterm/vt_geometry.{h,cpp}` этим коммитом **не тронуты вовсе** — их даже нет в списке изменённых файлов;
- `Composer::borderPixels()` берёт `opts->border`, как оставил `M6c`; `geometry.borderPixels` не заполняется (§5, `A1`);
- три обращения `composer.contentInsets()` и одно `composer.resize` в `vterm.cpp` на месте — их снимает `T5.1`, не мерж;
- гвард по-прежнему отличает наш метод от апстримного поля по скобке (проба 4), то есть соблазн расширить разрешение, о котором предупреждает план в §`T5.1`, не возник.

---

## Обнаружено

### 8.1. `bin/core_perf/main.cpp` после мержа стал **побайтово апстримным**

```
$ git diff --stat 7e2a8e3e HEAD -- bin/core_perf/main.cpp
(пусто)
```

Он не наш: наших правок в нём нет ни одной, а его вызов

```c++
VtermHeadless::create(*pool, *composer->vtConfig.config, nullptr, nullptr);
```

совпадает с апстримным посимвольно. То же и в `bin/main_fuzz/main.cpp`: пять хунков, которыми он расходится с апстримом, — это наши правки под `A1` (`contentInsets()` вместо `geometry.borderPixels`), а **строка 121 с вызовом `create` в них не входит** — она апстримная.

**Следствие для `T5.11`, названное как измерение, а не как обещание:** оба клиента ломает ровно одно — наша трёхаргументная сигнатура `VtermHeadless::create(Composer&, VtermTraceFactory*, stl::Output*)`, введённая `M6c`. `T5.9` по плану заменяет `vt_headless.*` апстримным целиком, то есть возвращает апстримную четырёхаргументную форму — ту самую, которую оба файла и зовут. Если это так и произойдёт, `T5.11` сведётся к проверке сборки без правки кода. Утверждать этого нельзя, пока `T5.9` не сделана: `main_fuzz` вдобавок читает `composer->contentInsets()` и `gridPixelWidth()` из нашего `grid_geometry.h`, и эти строки должны пережить замену.

### 8.2. Сборка с `-k` не публикует символические ссылки на успевшие цели

Описано в §6.2. Наблюдаемость нулевая: `.build/st_test` существует, указывает на валидный исполняемый файл, запускается и даёт правдоподобные числа — просто предмержевые. Ловится сверкой целей ссылок до и после успешной сборки, стоит одну команду.

Это близкий родственник ловушки 2 брифа: там «зелёная сборка `st` не доказывает, что собираются остальные», здесь — «сборка, где что-то сломалось, не доказывает, что бинарники под рукой свежие». Обе про то, что состояние `.build/` слабее связано с деревом, чем кажется.

### 8.3. `vt_test.h` и `vt_trace.h` переехали с `similarity 100%`, а `.cpp` — с `81%`

```
R100  lib/vterm/vterm_test.h  -> lib/vterm/vt_test.h
R081  lib/vterm/vterm_test.cpp -> lib/vterm/vt_test.cpp
```

`81%` пугает, но это артефакт размера. `vt_test.cpp` — файл **из семи строк**: пять строк лицензионной шапки, пустая, и одно включение. Меняется как раз включение, и на таком объёме одна строка даёт индекс сходства 81%. Содержательной потери нет: `diffcmp` (§6.4) даёт по этим файлам `A = 0, B = 0` независимо от того, как git посчитал сходство.

---

## 9. Что осталось задачам `T5.x`, оценённое после мержа

| Задача | Что именно осталось | Изменилось ли после `M6e` |
|---|---|---|
| `T5.9` | заменить наш `lib/vterm/vt_headless.{h,cpp}` апстримным. Расхождение теперь **измеримо на одном пути**: `vt_headless.h` `+8/−4`, `vt_headless.cpp` `+76/−143`. Снимет четыре строки `vterm_boundary` (`vt_headless.cpp:11–14`) | **упростилось:** файл больше не надо создавать и удалять — он уже лежит под нужным именем, задача свелась к содержимому одного файла. Удалять `lib/shitty/vterm_headless.*` (как пишет план) нечего: такого файла нет |
| `T5.10` | перенести pane-тесты на новый API. Объём уточнён: `lib/shitty/vt_headless_ut.cpp` расходится с апстримным на **`+1163/−25`** | **упростилось:** переименование файла и его включений сделано мержем, задаче остаётся только API |
| `T5.11` | новые сигнатуры в `bin/main_fuzz`, `bin/core_perf` | **вероятно сократилось до нуля** — §8.1. `core_perf` побайтово апстримный, вызов `create` в `main_fuzz` тоже апстримный; обоих ломает только наша сигнатура, которую `T5.9` и возвращает к апстримной. Проверять после `T5.9`, не раньше |
| `T5.1` | четырёхсторонние `VtInsets`, `origin` в `VtGeometry`, закрытие `PaneGeometry`, `mouseGeometry(const VtGeometry&)`, три `composer.contentInsets()` и один `composer.resize` в `vterm.cpp` | без изменений — §7 |
| `T5.2` | `grid_geometry.h` из `vterm.cpp:27` | без изменений (номер строки сдвинулся с 26 на 27) |
| `T5.4` | `composer.sessions` ×2, тип `Composer&` в четырёх сигнатурах `vterm.cpp`, параметр `Composer&` в `Vterm::create` | без изменений |
| `T5.5`, `T5.6`, `T5.3` | цепочка по `vterm.cpp` | без изменений |

`vterm_boundary` после всей волны закрывается ровно так, как считало решение `T5.1`: 2 строки выкладкой `T5.1`, 1 ею же следом, 4 с `T5.9`, последняя с `T5.4`. **`M6e` не снял ни одной и ни одной не добавил.**

# `G15`: разрешения для `vterm_boundary`

Ветка `fix/G15-boundary-allowance`, коммит `4d213231`. Тронут один файл:
`lib/vterm/check_includes.py`. `build.py` не менялся — его узел `vterm_boundary`
(строка 4342) уже держит `check_includes.py` во входах, поэтому правка
разрешения перестраивает узел сама, и второе место, где разрешение могло бы
разойтись с гвардом, не заводится.

---

## 1. Цена ошибки

Гвард красный с `eb016aec` (мерж-шаг `M6b`, переезд ядра в `lib/vterm`,
2026-09-04 05:35 UTC). Между ним и `master` на момент задачи — **62 коммита и
17 мержей**.

```
$ git rev-list --count eb016aec..master
62
$ git log --oneline --merges eb016aec..master | wc -l
17
```

Прогонов CI на `master` за это время — **пять, все красные**:

| Прогон | Время (UTC) | Коммит |
|---|---|---|
| `33852202104` | 08:11 | `53ef3cff` |
| `33856430946` | 09:03 | `540a08de` |
| `33862433804` | 10:16 | `9fbf7f98` |
| `33867553074` | 11:21 | `c1118304` |
| `33885414701` | 14:43 | `7ca0f94b` |

`FAIL $(B)/vterm-boundary.stamp` присутствует и в первом из них
(`33852202104`, джоба `Tests Fedora`), и в последнем (`33885414701`), — то есть
все пять.

**Слепых джоб не две, а четыре.** По логам `33885414701`, строку
`vterm-boundary.stamp: command exited 1` дают:

```
Coverage
Tests Alpine
Tests Darwin 2/5
Tests Fedora
```

Насколько именно слепа каждая — считано по её собственному куску лога:

| Джоба | `OK: N` (наборы `unit_tests`) | `PASS ` (питоновские валидаторы) |
|---|---:|---:|
| `Tests Alpine` | **0** | 28 |
| `Tests Fedora` | **0** | 29 |
| `Coverage` | 21 | 2849 |
| `Tests Darwin 2/5` | 6 | 564 |

То есть на Alpine и Fedora не исполнился **ни один** набор `unit_tests`.
Уточнение к формулировке борда «ни одного теста»: два-три десятка `PASS` там
всё же есть — это каталог-валидаторы (`tst/konsole/*`, `tst/wezterm/*`),
которые в графе стоят **до** гварда и успевают отработать; всё, что за ним,
не исполнялось. На Coverage и Darwin 2/5 часть графа проходит по параллельным
ветвям, но джоба всё равно красная.

`Tests ASan` и `Tests UBSan` в этом прогоне красные **по другой причине**
(`xkbcommon: [XKB-769] syntax error`) и до гварда доходят; пять джоб
`Sandboxed *` — `skipped`. К этой задаче ни те, ни другие не относятся, но их
стоит назвать, чтобы «CI красный» после мержа `G15` не читалось как «`G15` не
сработала».

Причина самой ошибки записана на борде и здесь не пересматривается: гвард
проверялся локально одной командой, где он и правда аудит, и ни разу — внутри
`./build test`, где он узел графа сборки внутри `add_test(...)`.

---

## 2. Как устроено разрешение и чем оно отличается от отмены

Разрешение — словарь на два уровня: **файл → включение → счёт**.

```python
ALLOWANCE = {
    "vt_headless.cpp": {"composer.h": 1, "pane_layout.h": 1, "grid_geometry.h": 1},
    "vterm.cpp":       {"session.h": 1,  "composer.h": 1,    "grid_geometry.h": 1},
}
```

Четыре гварда в `build.py` умеют ключ только по пути (`"composer.cpp": 8`),
потому что метят **вызов**, а у вызова нет собственного имени. `G1` записала
эту слабость (инварианты §3.4, пункт 1): счёт прощает любые восемь, поэтому
убрать законное обращение и добавить незаконное — счёт тот же, гвард зелёный;
ловится **рост**, но не **подмена**. Здесь у предмета имя есть — это само
включение, — и ключ его несёт. Отсюда три свойства, каждое отличающее
разрешение от отмены:

1. **Поимённость.** `vterm.cpp`, поменявший `composer.h` на `pane_layout.h`, —
   ключ, которого никто не писал, хотя число нарушений в файле не изменилось.
   Подмена ловится наравне с ростом — то, чего четыре соседних гварда не умеют.
   Проба B2 ниже.
2. **Точность по счёту.** Счёт тратится в порядке файла: первое `composer.h` в
   `vterm.cpp` прощено, второе — красное. Так ловится дубль включения под двумя
   написаниями. Проба B1.
3. **Объяснение у каждого ключа.** Рядом с каждым — что его закрывает:
   `vt_headless.cpp` целиком — `T5.9`; `session.h` — `T5.4` (инвариант `A11`,
   единственный `composer.sessions->cellCapacityExcept(this)`); `composer.h` —
   `T5.1` + `M6d` + `T5.4`, включение уходит с последним обращением;
   `grid_geometry.h` — **никто**, потому что §2.7 решения `T5.1` прямо
   запрещает единственную правку, которая его закрывает. Ключ без выхода —
   ключ, переживающий свою причину.

Что **намеренно не красное**: включение, стоящее в разрешении и не имеющее
больше попаданий. Счёт может упасть до нуля с сохранением ключа — так `T5.8`
сузила `mouse_geometry_allowance`, и ноль там есть самое тесное число.
Самозащита ниже устроена **по файлам**, а не по включениям, ровно чтобы это
сужение оставалось возможным: обнулённое включение держит свой файл
достижимым, и гарантия «гвард всё ещё видит это место» продолжает работать.

**Самозащита** — ловушка `T2.1`, перенесённая сюда: ключ, называющий файл,
которого гвард больше не читает, даёт красный. Иначе переименование или переезд
файла превращает шесть известных нарушений в невидимые, и гвард отчитывается
зелёной пустотой. На `M6b` эта ловушка поймала осиротевший ключ, на `M6e`
покраснела с двух сторон сразу. Проба C.

---

## 3. Три пробы

Гвард читает **только** строки `#include`; комментарии он не разбирает вовсе,
поэтому все пробы поставлены кодом, а не прозой. После каждой — откат и
`git checkout`.

### Проба A. Новое нарушение в файле, которого нет в разрешении

`lib/vterm/mouse_frontend.cpp`, строка 8:

```
#include "mouse_frontend.h"
#include "composer.h"
```

```
$ python3 lib/vterm/check_includes.py lib/vterm /tmp/g15-b.stamp
the VT core cannot see the GUI: these crossings are not in the allowance at the top of this file, and closing one is cheaper than widening it.
  mouse_frontend.cpp:8: "composer.h" does not resolve inside lib/vterm
EXIT=1
```

Файл и строка названы точно. После отката — `EXIT=0`.

### Проба B. Седьмое нарушение в файле, который уже стоит в разрешении

Две отдельные пробы, потому что «седьмое» бывает двух видов.

**B1 — повтор уже разрешённого включения.** Второй `#include "composer.h"` в
`vterm.cpp`, счёт которого равен единице:

```
#include "composer.h"      <- строка 24, разрешённая
#include "composer.h"      <- строка 25, проба
```

```
$ python3 lib/vterm/check_includes.py lib/vterm /tmp/g15-d.stamp
the VT core cannot see the GUI: these crossings are not in the allowance at the top of this file, and closing one is cheaper than widening it.
  vterm.cpp:25: "composer.h" does not resolve inside lib/vterm
EXIT=1
```

Известная строка 24 промолчала, седьмая — 25 — покраснела. Прощения «оптом»
не случилось.

**B2 — чужое включение в разрешённом файле.** `#include "pane_layout.h"` в
`vterm.cpp`. Включение разрешено, но **в другом файле** (`vt_headless.cpp`):

```
$ python3 lib/vterm/check_includes.py lib/vterm /tmp/g15-e.stamp
the VT core cannot see the GUI: these crossings are not in the allowance at the top of this file, and closing one is cheaper than widening it.
  vterm.cpp:25: "pane_layout.h" does not resolve inside lib/vterm
EXIT=1
```

Это и есть проверка поимённости из §2.1: общее число нарушений в `vterm.cpp`
осталось бы прежним, если бы одновременно ушло другое, — а гвард всё равно
красный, потому что ключ `("vterm.cpp", "pane_layout.h")` никто не писал.

### Проба C. Самозащита

**C1 — переименование.** `git mv lib/vterm/vt_headless.cpp
lib/vterm/vt_frontend.cpp`:

```
the VT core cannot see the GUI: these crossings are not in the allowance at the top of this file, and closing one is cheaper than widening it.
  vt_frontend.cpp:11: "composer.h" does not resolve inside lib/vterm
  vt_frontend.cpp:12: "pane_layout.h" does not resolve inside lib/vterm
  vt_frontend.cpp:13: "grid_geometry.h" does not resolve inside lib/vterm
EXIT=1
```

Красный — но по первой причине: под новым именем те же три нарушения снова
никем не разрешены. Устаревший ключ здесь не проявился, потому что до второй
проверки дело не дошло.

**C2 — уезд из области сканирования, главный случай.** `check()` глобит
`root/*.h` и `root/*.cpp` нерекурсивно, поэтому файл в подкаталоге перестаёт
существовать для гварда вовсе. Это ровно та «зелёная пустота», против которой
ловушка и построена: три нарушения исчезают **из вида**, а не закрываются.

```
$ git mv lib/vterm/vt_headless.cpp lib/vterm/moved/vt_headless.cpp
$ python3 lib/vterm/check_includes.py lib/vterm /tmp/g15-g.stamp
the boundary allowance names files this check never read, so it is guarding a core that no longer exists: re-key the allowance onto where these live now, or drop them.
  vt_headless.cpp
EXIT=1
```

Раздела «нарушения» в выводе **нет вообще** — их гвард уже не видит. Без
самозащиты этот прогон был бы зелёным. После отката — `EXIT=0`.

---

## 4. Таблица критериев

| # | Критерий | Команда и вывод | Статус |
|---|---|---|---|
| 1 | Гвард зелёный на текущем дереве | `python3 lib/vterm/check_includes.py lib/vterm /tmp/g15-a.stamp` → пусто, `EXIT=0` | ✅ |
| 2 | Новое нарушение краснеет с файлом и строкой | Проба A: `mouse_frontend.cpp:8: "composer.h" …`, `EXIT=1` | ✅ |
| 3 | Седьмое нарушение в разрешённом файле краснеет | Проба B1: `vterm.cpp:25: "composer.h" …`, `EXIT=1`; B2: `vterm.cpp:25: "pane_layout.h" …`, `EXIT=1` | ✅ |
| 4 | Самозащита от осиротевшего ключа | Проба C2: `the boundary allowance names files this check never read … vt_headless.cpp`, `EXIT=1` | ✅ |
| 5 | `./build test` доходит дальше гварда | `[VB] {138/3173} $(B)/vterm-boundary.stamp`, сборка ушла до узла `{193/3173}`; `./build vterm_boundary --clear` → `[VB] {1/1}`, `EXIT=0`, штамп содержит `ok` | ✅ (с оговоркой, см. §5) |
| 6 | `./build st --clear` зелёная, 228 узлов | `[LD] {228/228} $(B)/st`, `EXIT=0` | ✅ |
| 7 | `unit_tests` даёт `OK: 961` и выше | `./build unit_tests pty_test_helper` → `EXIT=0`; прогон → `OK: 961`, `EXIT=0` | ✅ |
| 8 | Четыре сканирующих гварда зелёные, каждый доказан пробой кодом | см. ниже | ✅ |

### Критерий 8 подробно

Гварды прогнаны **не по штампу**: программа каждого вытащена из `build.py`
через `ast` и исполнена по рабочему дереву (`.build` адресуется содержимым, и
восстановленный из CAS штамп не доказывает, что гвард исполнялся). Скрипт —
`scratchpad/g15/run_guard.py`.

```
border_pixels          EXIT=0
mouse_geometry         EXIT=0
pane_grid              EXIT=0
darwin_call            EXIT=0
```

Пробы поставлены кодом (комментарий бесполезен: `blanked()` вычищает тела
комментариев и зеленит все четыре сразу) и в файлах, которые соответствующий
гвард действительно читает; файлы `T5.2` (`grid_geometry.*`, `application.cpp`,
`application_ut.cpp`, `test_mode.cpp`) не трогались.

| Гвард | Куда поставлена проба | Код пробы | Красный вывод |
|---|---|---|---|
| `border_pixels_guard` | `lib/vterm/mouse_frontend.cpp` (нет в разрешении) | `composer.borderPixels()` | `Unallowed uses: lib/vterm/mouse_frontend.cpp:15`, `EXIT=1` |
| `mouse_geometry_guard` | `lib/vterm/mouse_frontend.cpp` (в разрешении, счёт `0`; не `_ut.cpp`) | `mouseGeometry(geometry)` | `Unallowed uses: lib/vterm/mouse_frontend.cpp:15`, `EXIT=1` |
| `pane_grid_guard` | `lib/shitty/render_damage.cpp` (имя на `render`, иначе гвард файл не читает) | `composer.geometry.columns` | `Unallowed uses: lib/shitty/render_damage.cpp:13`, `EXIT=1` |
| `darwin_call_guard` | `lib/shitty/render_damage.cpp`, вне `#if __APPLE__` | `createMetalRenderer()` | `Unguarded calls: lib/shitty/render_damage.cpp:13  createMetalRenderer`, `EXIT=1` |

После отката всех проб `git status --short` содержит только
`M lib/vterm/check_includes.py`, и все четыре снова `EXIT=0`.

---

## 5. Оговорка к критерию 5

Цель задачи достигнута: `vterm_boundary` исполнился зелёным узлом
`{138/3173}`, и граф пошёл дальше — до `{193/3173}` включительно.

**`./build test` целиком при этом красный, и падает он не на гварде.** Первое,
до чего CI теперь доходит:

```
!sidebarTabsBranch(StringView(root), out) failed, at lib/shitty/ui_sidebar_tabs_ut.cpp:528
Suite_SidebarTabsUi::Test_TheBranchIsFoundFromASubdirectoryAndThroughEveryKindOfDotGit
```

Разбор — в «Обнаружено», пункт 1. Это не регрессия `G15`: правка касается
только `check_includes.py`, а падение воспроизводится переменной окружения на
неизменённом бинарнике.

---

## Обнаружено

**1. Следующий блокер CI за гвардом: `ui_sidebar_tabs_ut.cpp:528` падает, когда
`TMPDIR` лежит внутри репозитория.** Тест
`TheBranchIsFoundFromASubdirectoryAndThroughEveryKindOfDotGit` создаёт временный
корень через `makeTempDir()`, который читает `TMPDIR`
(`lib/shitty/ui_sidebar_tabs_ut.cpp:183-184`), и утверждает
`STD_INSIST(!sidebarTabsBranch(StringView(root), out))` — «выше этого корня нет
репозитория вовсе», единственный ответ, который строке позволено рисовать как
«no git». `./build test` даёт временным файлам каталог **внутри дерева**
(`.build/tmp/<hash>/`), поэтому репозиторий выше есть, и утверждение падает.

Доказано пробой в обе стороны на одном и том же бинарнике:

```
$ TMPDIR="$PWD/.build/tmp/g15probe" SHITTY_PTY_TEST_HELPER=… ./.build/unit_tests --threads=1 < /dev/null
OK: 960, ERR: 1        (EXIT=1; !sidebarTabsBranch … ui_sidebar_tabs_ut.cpp:528)

$ TMPDIR=/tmp/g15-outside      SHITTY_PTY_TEST_HELPER=… ./.build/unit_tests --threads=1 < /dev/null
OK: 961                (EXIT=0)
```

Это ровно тот класс отказа, который CI не мог показать: до `unit_tests` он не
доходил. Локально его тоже не видно — ручной прогон из корня дерева наследует
системный `TMPDIR`, и оба способа проверки, записанные в `CLAUDE.md`, дают
зелёное. Владельца у файла в волне 6 нет; сам тест правильный, чинить надо либо
`makeTempDir()` (не доверять `TMPDIR` внутри дерева), либо то, что `./build`
подставляет `TMPDIR` внутрь `.build`.

**2. Слепых джоб было четыре, а не две.** Кроме `Tests Alpine` и
`Tests Fedora`, `FAIL $(B)/vterm-boundary.stamp` дают `Coverage` и
`Tests Darwin 2/5` (прогон `33885414701`). Первые две — слепы целиком: ноль
наборов `unit_tests`. У Coverage и Darwin 2/5 часть графа проходит по
параллельным ветвям (21 и 6 наборов соответственно), но джоба красная. Разница
существенна для чтения борда: «Alpine и Fedora не исполняют ничего» верно,
«гвард роняет две джобы» — нет.

**3. «Ни одного теста» — почти, но не буквально.** У Alpine и Fedora в логе
есть 28 и 29 строк `PASS` — это каталог-валидаторы `tst/konsole/*` и
`tst/wezterm/*`, стоящие в графе до гварда. Ноль относится к наборам
`unit_tests` и ко всему, что за узлом гварда. Формулировку борда стоит уточнить,
иначе первый же увиденный `PASS` в логе будет прочитан как опровержение.

**4. `./build vterm_boundary --clear` исполняет узел, а `--clear` у одноузловых
целей не спасает от CAS.** Штамп после прогона — симлинк в `.build/cas/…`.
Ловушка из `CLAUDE.md` («штамп из CAS») в полной мере относится и к этому
гварду, поэтому все пробы в §3 сняты прямым запуском программы, а не через
`./build`; для четырёх соседних — через `ast`-извлечение из `build.py`.

**5. `PIPESTATUS` в этой оболочке не даёт кода команды до пайпа.** Первая
попытка снять критерий как `./build vterm_boundary 2>&1 | tail -20; echo
"EXIT=${PIPESTATUS[0]}"` напечатала пустое `EXIT=`. Требование брифа «`echo
"EXIT=$?"` сразу после команды, не через пайп» — не формальность: обратная
форма здесь молча теряет код возврата, то есть выглядит как доказательство,
ничего не доказывая.

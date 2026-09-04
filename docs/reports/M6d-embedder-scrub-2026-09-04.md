# M6d. Политика URI-схем уходит из ядра, и трижды выигрывает та сторона, которую выбрал `M6c`

**Дата:** 2026-09-04 · **Задача:** `M6d`, волна 6 плана `docs/plans/2026-08-29-upstream-merge.md` · **Ветка:** `wave/upstream-merge-w6d` · **Коммит мержа:** `04f01585`, родители `a9cc139b` + `9bf5e497` · **Вердикт:** `DONE с двумя унаследованными красными` — сборка `st`/`pt`, юнит-тесты и питоновский набор зелены и **побайтово совпадают** с предмержевой головой, четыре сканирующих гварда зелены и доказаны пробами кодом, `vterm_boundary` красный на тех же восьми строках, что и до шага; сверх того найдены две цели (`main_fuzz`, `core_perf`), которые **не компилировались уже до этого шага**.

Девятый мерж-шаг и самый маленький содержательный за волну: 18 файлов, +101/−50 у апстрима. Конфликтов **пять хунков в трёх файлах**, и три из пяти — это уже принятые решения `M6c`, приехавшие второй раз.

---

## 1. Коммит

| Коммит | Что |
|---|---|
| `04f01585` | `merge wave 6 step M6d: URI scheme policy leaves the VT core` — сам мерж, родители `a9cc139b` + `9bf5e497` |

Предмержевая голова — `a9cc139b`. Слит один апстримный коммит:

```
9bf5e497 Scrub the last embedder concepts out of the VT core
```

`7e2a8e3e` **не тянулся**: это шаг `M6e`.

---

## 2. Что принёс апстрим

Коммит вычищает из `lib/vterm` последние понятия, которые ядру знать не положено. Четыре независимых куска.

### 2.1. Политика URI-схем переезжает к эмбеддеру

Ядро **обнаруживает** плоскую ссылку как раньше; **можно ли по ней ходить** теперь решает эмбеддер.

| Было (в ядре) | Стало |
|---|---|
| `VtConfig::uriSchemeTrie` | `Options::uriSchemeTrie` (`lib/shitty/options.h`) |
| `VtConfig::uriSchemeAllowed()` (`vt_config.cpp`, 16 строк) | `Options::uriSchemeAllowed()` (`options.cpp`) + **чистый виртуальный** `VtHost::uriSchemeAllowed()` |
| `lib/vterm/darts.{h,cpp,_ut.cpp}` | `lib/shitty/darts.{h,cpp,_ut.cpp}` — рядом с единственным пользователем |
| `vterm.cpp`: `config().uriSchemeAllowed(link.scheme)` | `host.uriSchemeAllowed(link.scheme)` |

Апстрим реализует новый метод дважды: `ComposerVtHost` спрашивает `composer.opts`, а `HeadlessVtHost` отвечает зашитым «разобранным умолчанием» `http`/`https`/`file`. У нас `HeadlessVtHost` нет с `M6c` — см. §3.4 и §4.

Побочно апстрим добавил в `Options::uriSchemeAllowed()` **проверку на нулевой trie**, которой в `VtConfig::uriSchemeAllowed()` не было. Это меняет поведение, и в нашу пользу — §4.3.

### 2.2. `fontChanged()` становится `presentationInvalidated()`

Ядро перестаёт называть то, чем эмбеддер рисует, и называет следствие: «всё, что ты держал, протухло». Переименован **только виртуальный метод `Vterm`** и два его вызова (`session.cpp`, сам `vterm.cpp`). Одноимённый `ApplicationImpl::fontChanged()` и все пять списков `composer.fontChangedListeners` апстрим **не трогает** — это по-прежнему шрифты эмбеддера.

### 2.3. Две парные проверочные единицы трансляции

`lib/vterm/pty.cpp` и `lib/vterm/vt_host.cpp` — по 7 строк: лицензионная шапка и `#include` собственного заголовка. Требование `STYLE.md`: заголовок обязан компилироваться сам по себе. **Это единственная причина, по которой число узлов `st` выросло на два.**

### 2.4. Комментарии и тестовые данные

`vt_geometry.h` — замена одного слова («from its font» → «from whatever it draws with»), ровно как предсказывало решение `T5.1`. Плюс `screen.cpp`, `vterm.h` (два места), `vterm.cpp` и удалённая строка комментария в `vterm_test.h`. `darts_ut.cpp` перестаёт писать в тестовых данных имена опций: ключи `font`/`fontsize` заменены на `tab`/`tabsize`.

### 2.5. Коммит не механический — и это видно за секунду

Фильтр `-U0`-диффа от `#include` (метод `M6b`):

```
$ git diff -U0 -M 9bf5e497~1 9bf5e497 | grep -cE '^[+-][^+-]'   → 140
$ git diff -U0 -M 9bf5e497~1 9bf5e497 | grep -cE '^[+-]#include' →   5
```

Пять включений из ста сорока строк: `"darts.h"` вместо `<lib/vterm/darts.h>` в `options.cpp`, снятое `"darts.h"` в `vt_config.cpp` и по одному собственному включению в двух новых файлах. Остальные 135 строк — содержание.

### 2.6. Число узлов

Цель `st`: **226 → 228**. Пришли `lib/vterm/pty.cpp` и `lib/vterm/vt_host.cpp`; `darts.cpp` переехал из `lib/vterm` в `lib/shitty`, а глоб `build.py:765` берёт оба каталога и разницы не видит. `unit_tests`: **128 → 130** по той же причине (`darts_ut.cpp` переехал между двумя глобами `build.py:751`).

---

## 3. Конфликты: пять хунков, три файла

```
lib/vterm/vterm_headless.cpp 2 · lib/vterm/vterm.cpp 2 · lib/shitty/options.cpp 1
```

Пятнадцать остальных файлов апстрим смержил сам, и одиннадцать из них лежат **побайтово апстримными**:

```
$ for f in vt_config.h vt_config.cpp vt_geometry.h vt_host.h vt_host.cpp pty.cpp \
           screen.cpp vterm_test.h ../shitty/darts.h ../shitty/darts.cpp ../shitty/darts_ut.cpp
  do git diff --quiet 9bf5e497 -- lib/vterm/$f && echo "SAME $f"; done
одиннадцать строк SAME
```

### 3.1. `options.cpp` — включение против включения, взяты обе стороны

```
<<<<<<< HEAD
#include "quick_geometry.h"
=======
#include "darts.h"
>>>>>>> 9bf5e497
```

Ложный конфликт соседства: апстрим вставляет `"darts.h"` ровно туда, где у нас стоит наш `"quick_geometry.h"`. **Взяты обе**; снятие апстримом `<lib/vterm/darts.h>` строкой ниже применилось само. Порядок проверен `dev/style.py reorder_includes` (звать `style.py` целиком нельзя — локальный clang-format переформатирует лишнее): блок остался `toml.h`, `brand.h`, `darts.h`, `quick_geometry.h`, `terminal_colors.h`, файл не изменился ни на байт.

### 3.2. `vterm.cpp`, хунк 1 — список переопределений: наша строка и апстримная

```
        void paneResized(const PaneGeometry& geometry) override;   // наша
        void presentationInvalidated() override;                   // апстримная
```

Апстрим предлагал `windowResized()` + `presentationInvalidated()`. `paneResized(const PaneGeometry&)` — **наша сторона**, `A8`: терминал получает геометрию **панели**, а не окна; это ровно хунк 13 из `M6c` §3.1, приехавший второй раз, потому что апстрим переписал соседнюю строку. Переименование — **апстримная сторона**, безоговорочно: это и есть предмет коммита.

### 3.3. `vterm.cpp`, хунк 2 — посев стора против нашего комментария

Апстрим снова кладёт в `Vterm::create()` строку

```c++
extras.store->setCellCount((size_t)(geometry.columns) * (geometry.rows + config.saveLines));
```

**Взята наша сторона** — по той же причине, что в `M6c` §3.1 хунк 20 (`R5-qa`, Q2): грид одной панели — неверная база для стора, который делит всё окно, а посев вдобавок избыточен (`resetTerminal()` доходит до `updateExtraCellCount()` до возврата из `create()`). Апстримная **формулировка комментария** («Resize and invalidation delivery» вместо «Resize and font-change delivery») при этом взята: она про переименование, а не про посев.

### 3.4. `vterm_headless.cpp`, два хунка — апстримный `HeadlessVtHost` отброшен второй раз

Оба конфликта — прямое следствие `M6c` §3.5, где апстримный адаптер `HeadlessVtHost` был отброшен целиком (71 строка): наш безголовый хост — эмбеддер `Composer`, и адаптер у него уже есть, `ComposerVtHost`. Апстрим дописывает в несуществующий у нас класс объявление и определение `uriSchemeAllowed()` — **взята наша сторона**, 28 строк апстрима не приняты.

Это **единственное** место, где апстримный код этого коммита отброшен, и единственный вклад в расхождение диффов (§7.3).

### 3.5. Одно доведение, которого в конфликтах не было

`CallHeadlessFontChanged::onListen()` звал `terminal->fontChanged()`. У апстрима такого слушателя нет (его безголовый хост не подписан на смену шрифта), поэтому переименование до этой строки автоматически не дошло — она смержилась молча и перестала бы компилироваться. Строка переведена вручную на `presentationInvalidated()`. Это **вся** наша правка сверх апстримной: `git diff a9cc139b HEAD -- lib/vterm/vterm_headless.cpp` — ровно одна строка.

---

## 4. Что стало с `uriSchemeTrie` / `uriSchemeAllowed()`

План называл это вторым кандидатом во второй источник правды и предписывал «наши — отдать, а не отстоять». **Отдано полностью, и второго источника правды не возникло** — по трём проверенным причинам.

### 4.1. Где это живёт после мержа

| Что | Где | Кто читает |
|---|---|---|
| Список схем из `-uriScheme` / конфига | `Options::uriSchemes` | разбор опций |
| Свёрнутый в trie нижний регистр | `Options::uriSchemeTrie` (`options.cpp:1041`) | `Options::uriSchemeAllowed()` |
| Ответ «можно ли по этой схеме ходить» | `Options::uriSchemeAllowed()` (`options.cpp:1283`) | `ComposerVtHost::uriSchemeAllowed()` (`composer.cpp:210`) |
| Точка спроса в ядре | `VtHost::uriSchemeAllowed()` (`vt_host.h:41`) | `VtermImpl::resolveHyperlink()` (`vterm.cpp:2495`) |

В ядре не осталось ни одного упоминания `Darts` и ни одной строки, знающей про схемы:

```
$ git grep -n 'uriScheme\|Darts\|darts' -- lib/vterm/
lib/vterm/screen.cpp:504:    static bool uriSchemeCodepoint(u32 codepoint) {
lib/vterm/screen.cpp:591:            if (uriSchemeCodepoint(codepoint)) {
lib/vterm/vt_host.h:41:    virtual bool uriSchemeAllowed(stl::StringView scheme) = 0;
```

Первые две — грамматика **обнаружения** (какие байты вообще могут быть схемой), она в ядре и остаётся; третья — вопрос эмбеддеру.

Проверено на слинкованном бинарнике, а не по исходникам:

```
$ nm -C .build/st | grep uriSchemeAllowed
00000001000047d0 t (anonymous namespace)::ComposerVtHost::uriSchemeAllowed(stl::StringView)
000000010000def0 T Options::uriSchemeAllowed(stl::StringView) const
$ nm -C .build/st | grep -c 'VtConfig::uriSchemeAllowed'   → 0
```

### 4.2. Что изменилось для пользователя `st` и `pt`: **ничего**

Одно и то же дерево трогает один и тот же объект. `Composer::setOptions()` ставит `opts = options` **и** `vtConfig.config = &options->vt` — то есть до мержа ядро читало trie через `VtConfigSlot` из того же экземпляра `Options`, из которого теперь читает `ComposerVtHost` напрямую. Перезагрузка конфига (`ConfigImpl::publish()`) меняет оба указателя одним вызовом `setOptions()`. Разойтись им негде: `git grep 'composer.opts ='` пуст, `opts` присваивается только в `setOptions()`.

Значение тоже то же самое: `OptionsParser::parse()` строит trie **всегда** (пустой список схем заменяется умолчанием `http`/`https`/`file` до свёртки, `options.cpp:1018-1041`), и строит его из того же `uriSchemes`, что и до мержа. Тесты, которые это держат, — `tst/test_plain_uri_input.py` (10 сценариев, включая `-uriScheme NoSuch`, заменяющий умолчание) — гоняются на настоящем бинарнике и зелены до и после, побайтово (§7.4).

### 4.3. Что изменилось на самом деле: разыменование нуля стало определённым `false`

`VtConfig::uriSchemeAllowed()` **не проверяла** `uriSchemeTrie` на ноль:

```c++
return uriSchemeTrie->find(...) != Darts::missing;   // до мержа
```

`Options::uriSchemeAllowed()` проверяет:

```c++
if (uriSchemeTrie == nullptr) { return false; }       // после мержа
```

Нулевой trie — это `Options`, который никто не разбирал: зашитые умолчания `Composer` (`composer.cpp:50`), безголовые клиенты (`bin/core_perf/main.cpp:81`) и юнит-фикстуры. До мержа такой терминал, встретив плоскую ссылку на экране, **разыменовывал ноль**; после — честно отвечает «нельзя». Продуктовый путь этого никогда не касался: `ConfigImpl::initialize()` публикует разобранные опции до создания окна и терминалов.

### 4.4. Одно расхождение с апстримом, названное и отложенное

Апстримный `HeadlessVtHost::uriSchemeAllowed()` отвечает зашитым `http`/`https`/`file` — «чтобы безголовый терминал видел те же ссылки, что и свежезапущенный GUI». Наш безголовый хост — `ComposerVtHost`, и он отвечает **из `composer.opts`**. Для безголового клиента, который опции разобрал (`st -dump`, весь питоновский набор), это то же самое; для клиента, который их не разбирал, — «ничего нельзя» вместо «три схемы можно». Ни один тест на это не смотрит (числа §7.2 и §7.4 совпали побайтово), и `T5.9` заменяет `vterm_headless.*` апстримным `vt_headless.*` целиком — апстримный ответ вернётся оттуда.

---

## 5. Гварды: чем доказано, что каждый видит изменившийся код

Все пять запускались **напрямую** — программа гварда вынимается из `build.py` разбором AST и исполняется вне `./build`, иначе доказательства не было бы (`CLAUDE.md`: кеш адресуется содержимым).

```
border_pixels_guard    EXIT=0   output bytes=0
mouse_geometry_guard   EXIT=0   output bytes=0
pane_grid_guard        EXIT=0   output bytes=0
darwin_call_guard      EXIT=0   output bytes=0
vterm_boundary         EXIT=1   8 строк, 3 файла
```

**Зелёный без пробы не значит ничего** (`M6`: `pane_grid_guard` был зелёным над неохраняемым `A9`). Пробы ставились **кодом**, не комментарием (`M6c`: `blanked()` вычищает тела комментариев до сканирования, и комментарная проба зеленит все четыре гварда сразу).

Главный вопрос этого шага — **видят ли гварды файлы, которых до шага не было или которые переехали**. Ответ получен пробой в каждый такой файл.

| Гвард | Проба (кодом, в конец файла) | Результат |
|---|---|---|
| `border_pixels` | `u16 probe() { return composer.borderPixels(); }` в **новом** `lib/vterm/vt_host.cpp` | `EXIT=1`, `lib/vterm/vt_host.cpp:8` |
| `border_pixels` | то же в **новом** `lib/vterm/pty.cpp` | `EXIT=1`, `lib/vterm/pty.cpp:8` |
| `border_pixels` | то же в **переехавшем** `lib/shitty/darts.cpp` | `EXIT=1`, `lib/shitty/darts.cpp:250` |
| `border_pixels` | `composer.geometry.borderPixels + composer.geometry.borderPixels` в `vt_host.cpp` | `EXIT=0` — **поле не считается, форма вызова считается**, как записано в решении `T5.1` §2.2 |
| `mouse_geometry` | `mouseGeometry(composer)` в `lib/vterm/vt_host.cpp` | `EXIT=1`, `lib/vterm/vt_host.cpp:8` |
| `mouse_geometry` | то же в `lib/shitty/darts.cpp` | `EXIT=1`, `lib/shitty/darts.cpp:250` |
| `mouse_geometry` | `mouseGeometry(composer, 1, 2, 3, 4)` там же | `EXIT=0` — форма с origin не считается |
| `mouse_geometry` | самопроверка: спрятан `lib/vterm/mouse_frontend.cpp` | `EXIT=1`, «re-key the allowance onto where these live now» |
| `darwin_call` | `void probe(Composer& composer) { createCsdTabsUi(composer); }` в изменённом шагом `lib/shitty/composer.cpp` | `EXIT=1`, `lib/shitty/composer.cpp:409` |
| `pane_grid` | `composer.geometry.columns + composer.geometry.rows` в `lib/shitty/render_reference.cpp` | `EXIT=1`, две строки `render_reference.cpp:1161` |
| `pane_grid` | `composer.vt.columns` (написание `M6`, мёртвое) в `lib/shitty/render_metal.mm` | `EXIT=1`, `render_metal.mm:1172` |
| `pane_grid` | самопроверка: спрятан `lib/shitty/render_vk.cpp` | `EXIT=1`, «Unreachable: render_vk.cpp» |

**Оговорка по `pane_grid`, сказанная прямо.** Этот гвард читает только файлы, чьё имя начинается на `render` (`if not path.name.startswith("render"): continue`), а `M6d` не изменил ни одного рендерера. Поэтому пробу «в изменившийся код» для него поставить негде, и доказано другое: гвард по-прежнему видит все три бэкенда `A9` в обоих написаниях словаря и краснеет, если один из них исчезнет из области сканирования.

**Разрешения не расширялись.** `border_pixels_allowance` (`composer.h` 2, `composer.cpp` 8, `test_mode.cpp` 1), `mouse_geometry_allowance` (`lib/vterm/mouse_frontend.{h,cpp}` по 1), `pane_grid_names`, `pane_grid_backends`, `darwin_guard_macros`, `guard_scan_roots` — не тронуты ни на символ, потому что не тронут сам файл:

```
$ git diff a9cc139b HEAD -- build.py | wc -l
0
```

Перекеивания тоже не потребовалось: ни один файл из ключей разрешений этим шагом не переезжал. `darts.{h,cpp,_ut.cpp}` переехали, но ни в одном разрешении не значились, а корни сканирования (`lib/shitty`, `lib/vterm`, `ext/plt`, `bin`) покрывают и старое, и новое место — проба в `lib/shitty/darts.cpp` это и показывает.

### 5.1. `vterm_boundary` — список не вырос

```
$ python3 lib/vterm/check_includes.py lib/vterm /tmp/vb.stamp ; EXIT=1
mouse_frontend.h:9:    "composer.h"      does not resolve inside lib/vterm
vterm.cpp:22:          "session.h"
vterm.cpp:23:          "composer.h"
vterm.cpp:27:          "grid_geometry.h"
vterm_headless.cpp:11: "options.h"
vterm_headless.cpp:12: "composer.h"
vterm_headless.cpp:13: "pane_layout.h"
vterm_headless.cpp:14: "grid_geometry.h"
```

**Восемь строк в трёх файлах, побайтово те же, что до шага и после `M6b`/`M6c`.** Снято до мержа и после — вывод совпадает посимвольно.

Шаг мог бы одну снять и не снял: `vterm_headless.cpp:11` — это `"options.h"`, и после переезда политики схем ядро само по себе к `Options` не обращается. Строка остаётся, потому что `options.h` включает **наш** безголовый хост ради `Composer::contentInsets()` и `windowPane()`, а не ради схем. Уходит она с `T5.9`.

---

## 6. `A1`, `A8`, `A9`, `A10` — чем проверено

| Инвариант | Чем проверено |
|---|---|
| `A1` (граница — опция эмбеддера, раскладку считает `contentInsets()`) | `border_pixels_guard` зелёный с неизменными разрешениями и доказан пробой в двух новых файлах; `geometry.borderPixels` по-прежнему **не заполняется** — единственное упоминание в дереве это комментарий `composer.h:150`, ровно как оставил `M6c` §6.1 |
| `A8` (пиксельная мышь и хит-тест считают от начала панели) | четыре вызова `mouseGeometry(…, originX_, originY_, paneWidth_, paneHeight_)` в `vterm.cpp` (1648, 2053, 2485, 9778) **посимвольно те же**, что в `a9cc139b`; `paneResized(const PaneGeometry&)` отстоян в конфликте §3.2; `mouse_geometry_guard` зелёный и доказан пробой |
| `A9` (рендерер не читает грид окна) | `pane_grid_guard` зелёный, оба написания словаря живы (проба), все три бэкенда достижимы (самопроверка); шаг не изменил ни одного рендерера |
| `A10` (разделитель панелей) | `paneDividerWidth` и `SessionSetImpl::dividerGrab` шагом не затронуты: в диффе `a9cc139b..HEAD` из `session.cpp` **одна строка** — переименование `fontChanged()` → `presentationInvalidated()` в `everyTerminalFontChanged()` |

---

## 7. Таблица критериев

| № | Критерий | Результат |
|---|---|---|
| 1 | `./build st --clear` зелёная | **228 узлов**, `EXIT=0`; эталон до мержа в этом же дереве — **226**; +2 — две новые проверочные единицы трансляции (§2.3) |
| 2 | `./build unit_tests pty_test_helper` + прогон | `EXIT=0`, **`OK: 950`**; список имён **посимвольно совпал** с предмержевым, `diff` пуст |
| 3 | Совпадение диффов | **A = 5** (все пять — пустые строки, артефакт счёта), **B = 22**, все 22 — отброшенный `HeadlessVtHost::uriSchemeAllowed()` (§3.4) |
| 4 | Питоновский набор поимённо | `Ran 6399` до и после, красные **побайтово совпадают** (20 строк), множества имён тестов идентичны |
| 5 | Четыре сканирующих гварда | `EXIT=0` каждый, пробы кодом в новых и переехавших файлах — §5 |
| 6 | `vterm_boundary` не вырос | **8 строк, 3 файла**, те же — §5.1 |
| 7 | `A1`, `A8`, `A9`, `A10` | не нарушены — §6 |
| 8 | `uriSchemeTrie` / `uriSchemeAllowed()` | отданы полностью, поведение `st`/`pt` не изменилось — §4 |

### 7.1. Сборка

Эталон, снятый на предмержевой голове `a9cc139b` в этом же дереве:

```
$ ./build st --clear -j 10 ;  EXIT=0
[AR] {225/226} $(B)/libshitty_prod.a
[LD] {226/226} $(B)/st
```

После мержа:

```
$ ./build st --clear -j 10 ;  EXIT=0
[AR] {227/228} $(B)/libshitty_prod.a
[LD] {228/228} $(B)/st

$ ./build st pt --clear -j 10 ;  EXIT=0
[LD] {231/232} $(B)/st
[LD] {232/232} $(B)/pt
```

И одной чистой сборкой всё вместе, чтобы `--clear` покрыл и тестовые цели (`darts_ut.cpp` тоже сменил каталог):

```
$ ./build st pt unit_tests pty_test_helper parser_perf --clear -j 10 ;  EXIT=0
[LD] {362/364} $(B)/st
[LD] {363/364} $(B)/parser_perf
[LD] {364/364} $(B)/pt
```

`--clear` обязателен: шаг двигает пути включений (`darts.h` меняет каталог), а `build.includes` в ключ узла не входит (`CLAUDE.md`).

**Свежесть бинарника, не через систему сборки.** Символы, а не штамп кеша:

```
$ nm -C .build/st | grep -c 'Options::uriSchemeAllowed'   → 1
$ nm -C .build/st | grep -c 'VtConfig::uriSchemeAllowed'  → 0
```

Две проверки, которые `./build st pt` не запускает (`CLAUDE.md`), прогнаны отдельно:

```
$ python3 tst/pretty_binary_branding.py .build/pt                        ;  EXIT=0
$ python3 tst/production_surface.py  (с тремя переменными из build.py)   ;  EXIT=0
Ran 5 tests in 0.090s / OK
```

### 7.2. Юнит-тесты

```
$ ./build unit_tests pty_test_helper -j 10 ;  EXIT=0
[LD] {130/130} $(B)/unit_tests

$ SHITTY_PTY_TEST_HELPER="$PWD/.build/pty_test_helper" ./.build/unit_tests --threads=1
EXIT=0
OK: 950
```

Эталон до мержа в этом же дереве — **`OK: 950`**. Сверка имён:

```
$ diff base-ut-names.txt after-ut-names.txt
(пусто)
```

**Ноль расхождений** — в отличие от `M6c`, этот шаг не переименовал ни одного теста. Ловушка 1 (`M6c`: зелёная сборка при 65 падающих тестах) проверена именно этим прогоном, а не сборкой.

### 7.3. Совпадение диффов

Построчно, машинно, в обе стороны, с учётом переименований (ключ — пост-образный путь). `A` — строки, которые апстрим удалил, а у нас они остались. `B` — строки, которые апстрим добавил, а у нас их нет.

```
$ python3 diffcmp.py a9cc139b <worktree> 9bf5e497
upstream post-image paths: 16
A. upstream-removed lines still present in the merged tree: 5
B. upstream-added lines absent from the merged tree: 22
```

**`A` = 5 — артефакт, а не расхождение.** Все пять — пустые строки в `vt_config.cpp` и `vt_config.h`: апстрим удалил из этих файлов пустые строки вместе с удалённым кодом, а в файлах остались другие пустые строки, и посчитанный по содержимому ключ их не различает. Оба файла лежат **побайтово апстримными** (§3), то есть настоящее расхождение здесь ноль.

**`B` = 22 — одно решение, названное заранее.** Все 22 строки в одном файле, `lib/vterm/vterm_headless.cpp`, и все — тело отброшенного `HeadlessVtHost::uriSchemeAllowed()` (§3.4, следствие `M6c` §3.5). Функция в апстриме 28 строк; 6 из них (закрывающие скобки, `}`) встречаются в файле в других местах и потому в счёт не попали.

Итоговое сравнение объёмов:

```
апстрим:  18 files changed, 101 insertions(+), 50 deletions(-)
наш мерж: 18 files changed,  73 insertions(+), 51 deletions(-)
```

Разница 28 добавленных строк — ровно отброшенная функция; лишняя удалённая строка — наш `terminal->fontChanged()` (§3.5), которого у апстрима нет.

### 7.4. Питоновский набор

Прогон одной группой (`--group=0 --group-count=1`), с окружением из `build.py:1130`:

```
до мержа:    Ran 6399 tests in 125.157s
             FAILED (failures=6, errors=14, skipped=17, expected failures=549)
после мержа: Ran 6399 tests in 113.858s
             FAILED (failures=6, errors=14, skipped=17, expected failures=549)

$ diff base-red.txt after-red.txt   → пусто (20 строк, посимвольно те же)
$ множества имён тестов             → идентичны, 0 только-до и 0 только-после
```

Двадцать красных — известные средовые: `fontconfig`/`freetype`/`harfbuzz` на macOS выключены `optional_pkg()` (`build.py:205`), поэтому растровые и цветные шрифтовые тесты падают одинаково до и после. **Новых красных нет, второй прогон для отсева флака не понадобился.**

### 7.5. Гварды

Числа и пробы — §5. `build.py` не изменён ни на строку.

---

## 8. Обнаружено

### 8.1. `main_fuzz` и `core_perf` не компилируются — и не компилировались до этого шага

Два бинарника зовут `VtermHeadless::create()` в апстримной четырёхаргументной форме, которой в нашем дереве нет с `M6c`:

```
bin/main_fuzz/main.cpp:121: VtermHeadless::create(*composer->pool, *composer->vtConfig.config, &capture, pty);
bin/core_perf/main.cpp:84:  VtermHeadless::create(*pool, *composer->vtConfig.config, nullptr, nullptr);
lib/vterm/vterm_headless.h:54: static VtermHeadless* create(Composer&, VtermTraceFactory*, stl::Output* = nullptr);
error: too many arguments to function call, expected at most 3, have 4
```

**Это не `M6d`.** Оба `.cpp` и `vterm_headless.h` этим шагом не тронуты (`git diff a9cc139b HEAD --stat` по этим трём файлам пуст), а текст ошибки определяется только ими. Отказ унаследован от `M6c`, где `VtermHeadless::create` получила наши одиннадцать аргументов, а два клиента за ней не пошли.

**Почему это никто не заметил.** Ни `main_fuzz`, ни `core_perf` не зарегистрированы в `add_test()`, то есть цель `./build test`, которую гоняет CI (`ci.yml:92`, `ci.yml:153`), их не строит. `bin/main_fuzz/main.cpp` и `bin/core_perf/main.cpp` входят в `python_test_inputs` **как отпечаток**, а не как сборочная зависимость. Это ровно тот класс, о котором предупреждает `CLAUDE.md` («`./build st` не строит тесты и не запускает проверок»), только с другой стороны: цель, которую не строит никто.

Проверено прямым вызовом:

```
$ ./build core_perf -j 10   ; EXIT=1
$ ./build main_fuzz -j 10   ; EXIT=1   (в составе финальной чистой сборки)
```

Соседний `parser_perf` из того же каталога `bin/` собирается и линкуется (`[LD] {363/364}` в §7.1) — сломаны ровно эти две цели, обе безголовые.

Починка — за рамками мерж-шага; кандидат в `T5.9` (там `vterm_headless.*` заменяется апстримным `vt_headless.*` целиком, и оба клиента всё равно придётся переписать) либо в отдельную задачу волны 6.

### 8.2. Решение `T5.1` §7 приписывает `M6d` снятие `composer.resize` — этого не происходит

Документ решения говорит: «**`composer.resize` снимает `M6d`** (`VtHost::requestResize`)». На дереве после мержа `composer.resize` **на месте**:

```
$ grep -o 'composer\.[a-zA-Z_]*' lib/vterm/vterm.cpp | sort | uniq -c
   3 composer.contentInsets
   1 composer.h
   1 composer.resize
   2 composer.sessions
```

Десять обращений к эмбеддеру (три `contentInsets`, одно `resize`, два `sessions`, четыре `mouseGeometry(composer, …)`) — **столько же, сколько после `M6c`**. Причина простая: `9bf5e497` внутриполосный ресайз не трогает вовсе, а апстримная строка на этом месте (`geometry.resize(…, &host)`, `vterm.cpp:6451` у апстрима) появилась ещё в `bd86ed38`, и `M6c` взял на ней нашу сторону сознательно (§3.1 хунк 13: раскладку считает `Composer::resize()`, потому что резервы хрома — его).

**Следствие для планирования:** снятие `composer.resize` — не работа мерж-шага, а работа `T5.1` (вариант «А» сливает `windowGeometry` и `geometry` в один `VtGeometry`, и тогда `VtGeometry::resize()` становится осмысленным) либо `T5.5`. Ни один оставшийся апстримный коммит волны 6 её не сделает.

### 8.3. Параметр `Composer&` в `Vterm::create` тоже остаётся

Отчёт `M6c` §3.2 писал, что «`M6d` и `T5.4` оставляют ровно одно удаление — параметр `Composer&`». Апстримная сигнатура `Vterm::create` в `9bf5e497` **побайтово та же**, что в `bd86ed38` (сменился только номер строки, 225 → 226), так что удалять параметр этот шаг не мог. Задача целиком за `T5.4`.

### 8.4. `vt_geometry.*` — предсказание решения `T5.1` подтвердилось точно

Единственная правка файла за 82 апстримных коммита — замена одного слова в комментарии, ровно как записано в §3 решения. Больше в `vt_geometry.h` этот коммит не трогает ничего, `vt_geometry.cpp` не трогает вовсе. Оба файла остаются **побайтово апстримными** — тот самый вход, который вариант «А» описывает.

---

## 9. Что осталось задачам `T5.x`, оценённое после мержа

| Задача | Что именно осталось | Изменилось ли после `M6d` |
|---|---|---|
| `T5.1` | четырёхсторонние `VtInsets` и `originX`/`originY`/`width`/`height` в `VtGeometry`; закрытие `PaneGeometry`; `mouseGeometry(const VtGeometry&)`; три `composer.contentInsets()` в `vterm.cpp` с оговоркой §2.7 | **выросло на один пункт:** `composer.resize` (§8.2) — план приписывал его `M6d` |
| `T5.2` | `grid_geometry.h` из `vterm.cpp:27` | без изменений |
| `T5.4` | `composer.sessions` ×2 и тип `Composer&` в четырёх сигнатурах `vterm.cpp` (428, 993, 8856, 9925) плюс параметр `Composer&` в `Vterm::create` (§8.3) | **выросло:** параметр `create` теперь целиком её |
| `T5.9` | замена `vterm_headless.{h,cpp}` апстримным `vt_headless.*`; вернёт `HeadlessVtHost` и вместе с ним апстримный ответ про схемы (§4.4); снимет четыре строки `vterm_boundary` | **выросло:** починка `main_fuzz` и `core_perf` (§8.1) естественно ложится сюда |
| `T5.5` | 65 механических хунков; явный запрет механического перевода `2u * geometry.borderPixels` в `columnsForPixelWidth`/`rowsForPixelHeight`/`windowOperation` | без изменений |

`vterm_boundary` после всей волны закрывается так же, как считало решение `T5.1`: 2 строки выкладкой `T5.1`, 1 ею же следом, 4 с `T5.9`, последняя с `T5.4`. `M6d` не снял ни одной и ни одной не добавил.

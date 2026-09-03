# G14: `dependency()` без аргументов — включённая зависимость, и на этом врал один флаг

- **Задача:** вне плана, делегированная — две находки из раздела `## Обнаружено` отчёта `G13`
- **Дата:** 2026-09-03
- **Дерево:** `scratchpad/wt-G14`, ветка `fix/G14-fontconfig-flag` от `master` (`f6da2068`)
- **Файл:** `build.py` (и только он); `flake.nix` не потребовался
- **Итог:** врала **одна** переменная из семи (`SHITTY_TEST_FONTCONFIG`); три `HAVE_*`
  приведены к форме `G3` — define на объекте зависимости
- **Статус:** исправлено и проверено локально в обеих конфигурациях (без шрифтового
  бэкенда и с ним, через подставной стор `G13`); настоящий Nix здесь не запускался

---

## 1. Находка первая: `SHITTY_TEST_FONTCONFIG` врал

### Что именно врало

`build.py:1142` и `build.py:4291` строят переменную окружения питоновских тестов так:

```python
"SHITTY_TEST_FONTCONFIG": "1" if fontconfig else "0",
```

Вопрос задан правильно — «истинен ли объект зависимости». Врал **ответ**. В ветке
«шрифтового бэкенда нет» стояло:

```python
else:
    freetype = dependency()
    fontconfig = dependency()
    harfbuzz = dependency()
    brotli_common = dependency()
```

`dependency` — это `BuildContext.interface` (`build:565`), а у него `enabled=True`
по умолчанию (`build:380`), и `Target.__bool__` возвращает ровно `self.enabled`
(`build:250-251`). То есть `dependency()` без аргументов — **включённая** пустая
зависимость: она ничего не даёт ни компилятору, ни линковщику, но истинна.

Переприсваивание задумывалось как «забудь то, что нашёл pkg-config». Оно и правда
стирало флаги — но заодно подменяло выключенный объект включённым, и единственный
код, который спрашивал этот объект как булево, получал `"1"`.

### С каких пор

| Дата | Коммит | Что произошло |
|---|---|---|
| 2026-07-27 | `433ea328` | появилась сама переменная `SHITTY_TEST_FONTCONFIG` |
| 2026-07-29 | `68cf9899` | появилась ветка `else: … = dependency()` и три `HAVE_*` в `build.cppflags` — **с этого дня ложь возможна** |
| 2026-08-25 | `62cef373` | macOS по умолчанию собирается без Homebrew — **ложь становится дефолтом на каждом Mac** |
| 2026-09-03 | `bfba0429` | Darwin-шарды включены в CI — впервые появился наблюдатель |

Ложь была видима только там, где бэкенда нет **и** гоняется питоновский набор.
До `62cef373` такая пара почти не встречалась; после — это каждая локальная
macOS-сборка, но питоновский набор там гоняют руками и редко. В CI это
проявилось одним отказом из шестнадцати: `FontResolverTest.test_collection_face_and_representative_advances_define_cells`
стоит под `@unittest.skipUnless(FONTCONFIG_AVAILABLE)` и обязан был пропуститься.

**Чего эта правка в CI не делает.** После `G13` fontconfig в Nix-сборке на darwin
**есть**, значит переменная там честно равна `"1"`, тест исполняется и проходит —
тот отказ закрыт `G13`, а не G14. Эта правка не меняет `Tests Darwin` ни в одну
сторону; она убирает *механизм*, которым один отказ был замаскирован под чужой:
пока флаг врал, «тест упал» и «тест не должен был запускаться» выглядели
одинаково. Если признак стора однажды ошибётся на fontconfig (пункты 1 и 6
раздела 3 отчёта `G13`), шесть тестов теперь пропустятся с внятной причиной,
а не упадут шестью загадочными отказами.

### Что изменено

```python
else:
    for font_dependency in (freetype, fontconfig, harfbuzz, brotli_common):
        font_dependency.enabled = False
```

Выключается **тот самый объект**, который вернул `pkg_config`, а не подставляется
второй. Это тот же приём, которым `G13` гасит пакет вне стора (`optional_pkg`), и
по той же причине: пока зависимость одна, «включена» и «даёт флаги» — одно и то
же свойство, и разъехаться им негде.

Отдельно проверено, что выключенный объект в `deps` не роняет цель: раннер просто
не заходит в него — `_usage_compile_flags` (`build:1273`), `_link_libraries`
(`build:1306`) и `_link_flags` (`build:1322`) все начинаются с `if not target.enabled: return`.
Поэтому замена `dependency()` → `enabled = False` не меняет ни одного флага, что и
подтверждено диффом графа (критерий 2).

### Почему не правил выражение в `env=`

Вариант «`"1" if (have_freetype_backend and fontconfig) else "0"`» чинит два места
из двух, но заводит третий признак: теперь правда о fontconfig живёт в объекте
зависимости, в `have_freetype_backend` и в двух строках `env=`. Ровно тот класс,
против которого написано правило `CLAUDE.md`. Правка в объекте чинит **всех**
будущих читателей, а не двух известных.

---

## 2. Находка вторая: три `HAVE_*` в глобальных флагах

### Утверждение `G13` проверено — и оно неточно

`G13` пишет: «разъехаться сегодня они не могут: `have_freetype_backend` выводится
из самих зависимостей». Для нашего C++-кода это верно. Но глобальные
`build.cppflags` доезжают до объектов, которые с этими библиотеками **не
линкуются вовсе**. Состояние «define есть, библиотеки нет» было не гипотетическим,
а материальным — просто в 27 объектах, которые эти define не читают:

```console
$ python3 /tmp/g14-cmp.py before.json after.json HAVE_FREETYPE
HAVE_FREETYPE: nodes before=454 after=427
  lost (27):
   - $(B)/obj/contour_vttest_helper/tst/contour/vttest/charsets.c.o
   … (все 25 файлов vttest)
   - $(B)/obj/pty_test_helper/tst/pty_test_helper.c.o
   - $(B)/obj/wraptest_helper/tst/wraptest/wraptest.c.o
  gained (0):
```

Это чистые **C**-хелперы тестов. Ни один из них слова `HAVE_FREETYPE` не знает:

```console
$ grep -rl "HAVE_FREETYPE\|HAVE_HARFBUZZ\|HAVE_FONTCONFIG" tst/
EXIT=1   (ни одного файла)
```

То есть сегодня это безвредно, но форма — «define отдельно, библиотека отдельно»,
и в ней разъезд уже случился; он просто пока никого не касается.

### Что изменено

```python
freetype.public_cppflags += ["-DHAVE_FREETYPE=1"]
harfbuzz.public_cppflags += ["-DHAVE_HARFBUZZ=1"]
if fontconfig:
    fontconfig.public_cppflags += ["-DHAVE_FONTCONFIG=1"]
```

Каждый define — на той зависимости, которая несёт его `-l`. После правки
`HAVE_FREETYPE` встречается 427 раз — **ровно столько же, сколько `HAVE_SIMDUTF`**
после `G3`. Совпадение не случайное: это одна и та же величина — число целей,
которые действительно зависят от `libshitty`.

Все четыре места, которые читают эти define, дефайны сохранили:

```console
$ (граф в конфигурации со стором, после правки)
$(B)/obj/libshitty/lib/shitty/font.cpp.o             -> FREETYPE HARFBUZZ FONTCONFIG
$(B)/obj/libshitty/lib/shitty/font_fontconfig.cpp.o  -> FREETYPE HARFBUZZ FONTCONFIG
$(B)/obj/libshitty/lib/shitty/font_freetype.cpp.o    -> FREETYPE HARFBUZZ FONTCONFIG
$(B)/obj/unit_tests/lib/shitty/span_shaper_ut.cpp.o  -> FREETYPE HARFBUZZ FONTCONFIG
```

(и то же во всех четырёх остальных вариантах библиотеки — `libshitty_test`,
`libshitty_test_prod_parser`, `libshitty_fuzz`, `libshitty_memprofile`).

`span_shaper_ut.cpp` — единственный, кто читает эти define **вне** `libshitty`;
он в `unit_tests`, и define доезжает к нему транзитивно, потому что
`_usage_compile_flags` обходит `deps` рекурсивно. Проверено не только по графу, но
и числом тестов: в конфигурации со стором `unit_tests` даёт `OK: 963` — на 13
больше, чем `OK: 950` без бэкенда, и эти 13 — как раз `SpanShaper::*` под
`#if defined(HAVE_FREETYPE) && defined(HAVE_HARFBUZZ)`. До правки в той же
конфигурации было ровно те же `OK: 963`.

### Оговорка про заголовки

Эти три define не встречаются ни в одном `*.h` — только в трёх `.cpp` и одном
`_ut.cpp`. Поэтому перенос **не** может развести раскладку типов между единицами
трансляции: макросы не участвуют в объявлениях. Будь они в заголовке, эта правка
была бы опасной, и я бы её не делал.

---

## 3. Критерии приёмки

### Критерий 1 — `SHITTY_TEST_FONTCONFIG` говорит правду

**Локально, шрифтового бэкенда нет. До:**

```console
$ ./build -G test_suite_group_00 | grep -o 'SHITTY_TEST_[A-Z]*":"[^"]*"' | sort -u
SHITTY_TEST_BINARY":"$(B)/st_test"
SHITTY_TEST_BINARY":"$(B)/st_test_prod_parser"
SHITTY_TEST_FONTCONFIG":"1"
SHITTY_TEST_PLATFORM":"cocoa"
SHITTY_TEST_VERSION":"2026.09.03"
EXIT=0
```

**После:**

```console
$ ./build -G test_suite_group_00 | grep -o 'SHITTY_TEST_[A-Z]*":"[^"]*"' | sort -u
SHITTY_TEST_BINARY":"$(B)/st_test"
SHITTY_TEST_BINARY":"$(B)/st_test_prod_parser"
SHITTY_TEST_FONTCONFIG":"0"
SHITTY_TEST_PLATFORM":"cocoa"
SHITTY_TEST_VERSION":"2026.09.03"
EXIT=0
```

**Конфигурация, где fontconfig есть.** Способ — подставной неизменяемый стор,
собранный `G13` (`scratchpad/g13-store`), приём описан в его критерии 3:

```console
$ STORE=…/scratchpad/g13-store
$ NIX_STORE="$STORE" PKG_CONFIG_LIBDIR="$STORE/pkgconfig" \
      ./build test_suite_group_00 -B .build-g14-on -G \
      | grep -o 'SHITTY_TEST_FONTCONFIG":"[^"]*"' | sort -u
SHITTY_TEST_FONTCONFIG":"1"
EXIT=0
```

И третья, отделяющая признак `G13` от «pkg-config нашёл» — стор виден, `NIX_STORE`
не задан. До правки здесь тоже было `"1"`, теперь:

```console
$ PKG_CONFIG_LIBDIR="$STORE/pkgconfig" ./build test_suite_group_00 -B .build-g14-off2 -G \
      | grep -o 'SHITTY_TEST_FONTCONFIG":"[^"]*"' | sort -u
SHITTY_TEST_FONTCONFIG":"0"
EXIT=0
$ … | grep -o "HAVE_[A-Z_]*" | sort | uniq -c
  25 HAVE_CONFIG_H
 450 HAVE_CORETEXT
 450 HAVE_METAL_RENDERER
```

**Закрыт.**

### Критерий 2 — остальные переменные того же блока

Блок целиком (`build.py:1139-1144`, он же `build.py:4290-4293`) — шесть переменных,
плюс седьмая, `SHITTY_COVERAGE`, из соседнего механизма; проверены все. Заодно
просмотрен единственный другой блок с этими именами — `build.py:1205-1209`
(`production_surface.py`): там три переменные, две пути и версия, булевых нет.

| Переменная | Из чего | Тот же дефект? |
|---|---|---|
| `SHITTY_TEST_BINARY` | путь `$(B)/st_test` | нет — строка пути, булева вопроса нет |
| `SHITTY_PRETTY_TEST_BINARY` | путь `$(B)/pt_test` | нет — то же |
| `SHITTY_TOML_DUMP_BINARY` | путь `$(B)/toml_dump` | нет — то же |
| `SHITTY_TEST_FONTCONFIG` | `bool(fontconfig)` — объект `Target` | **да, врала; исправлена** |
| `SHITTY_TEST_PLATFORM` | `darwin` = `"apple-darwin" in build.target` | нет — строковая проверка триплета, не объект зависимости; вдобавок совпадает с собственным дефолтом `tst/harness.py:21` |
| `SHITTY_TEST_VERSION` | `date.today()` | нет |
| `SHITTY_COVERAGE` (`build.py:110,121`) | флаг сборки / переменная окружения | нет |

Полный список мест, где `Target` вообще спрашивают как булево — их пять, и после
правки все пять честны:

```console
$ grep -nE "^(if|elif|.*= bool\(|.*if ) *(freetype|fontconfig|harfbuzz|brotli_common|simdutf|vulkan|wayland_backend|darwin_backend|plt|threads|libstd)\b" build.py
222:if simdutf:
232:have_freetype_backend = bool(freetype and harfbuzz)
241:    if fontconfig:
1142:                "SHITTY_TEST_FONTCONFIG": "1" if fontconfig else "0",
4291:            "SHITTY_TEST_FONTCONFIG": "1" if fontconfig else "0",
EXIT=0
```

Оставшиеся `dependency()` без аргументов — в разделе «Обнаружено». **Закрыт.**

### Критерий 3 — `./build st --clear` и `otool`

```console
$ ./build st --clear
[CC] {223/225} $(B)/obj/libshitty/lib/shitty/parser.cpp.o
[AR] {224/225} $(B)/libshitty_prod.a
[LD] {225/225} $(B)/st
EXIT=0

$ otool -L .build/st | grep -c /opt/homebrew
0
```

225 узлов — эталон ветки. Ни одной библиотеки из `/opt/homebrew`.

Дополнительно, поскольку `st --clear` в локальной конфигурации не компилирует ни
одного файла с этими define, та же цель собрана в конфигурации со стором:

```console
$ NIX_STORE="$STORE" PKG_CONFIG_LIBDIR="$STORE/pkgconfig" ./build st --clear -B .build-g14-on
[LD] {226/226} $(B)/st
EXIT=0
$ grep -c font_freetype  # в логе сборки
1
```

226 узлов, лишний — `font_freetype.cpp.o`, как и у `G13`.

И четыре сканирующих гварда плюс граница vterm, с `--clear` (штамп из CAS не
считается — `CLAUDE.md`):

```console
$ ./build border_pixels_guard mouse_geometry_guard pane_grid_guard darwin_call_guard vterm_boundary --clear
[VB] {1/5} $(B)/vterm-boundary.stamp
[PG] {2/5} $(B)/tst/pane-grid-guard.stamp
[MG] {3/5} $(B)/tst/mouse-geometry-guard.stamp
[BP] {4/5} $(B)/tst/border-pixels-guard.stamp
[DA] {5/5} $(B)/tst/darwin-call-guard.stamp
EXIT=0
```

**Закрыт.**

### Критерий 4 — `unit_tests`

```console
$ ./build unit_tests pty_test_helper
[AR] {126/127} $(B)/libshitty_test.a
[LD] {127/127} $(B)/unit_tests
BUILD_EXIT=0

$ SHITTY_PTY_TEST_HELPER="$PWD/.build/pty_test_helper" ./.build/unit_tests --threads=1
+ WindowSizingRequests::TheMinimumSizeAndTheResizeUnitPairEachAxisWithItsOwnInsets
OK: 950
EXIT=0
```

`OK: 950` — та же цифра, что записал `G13` на этой машине.

В конфигурации со стором — до и после правки одинаково:

```console
$ # до правки (build.py из stash)
$ SHITTY_PTY_TEST_HELPER=…/.build-g14-on-pre/pty_test_helper ./.build-g14-on-pre/unit_tests --threads=1
OK: 963
EXIT=0

$ # после правки
$ SHITTY_PTY_TEST_HELPER=…/.build-g14-on/pty_test_helper ./.build-g14-on/unit_tests --threads=1
OK: 963
EXIT=0
```

Это и есть прямое доказательство, что перенос `HAVE_*` на объекты зависимостей
ничего не отнял: 13 тестов `SpanShaper::*` под `#if defined(HAVE_FREETYPE)`
собираются и проходят как раньше. **Закрыт.**

### Критерий 5 — полный питоновский набор, поимённая сверка

Прогон один и тот же, окружение — точно то, что подаёт `build.py`; между «до» и
«после» в локальной конфигурации отличается **ровно** `SHITTY_TEST_FONTCONFIG`
(бинарники здесь побайтово те же — правка не меняет ни одной команды компиляции,
см. дифф графа ниже).

```console
$ env SHITTY_TEST_BINARY=$PWD/.build/st_test SHITTY_PRETTY_TEST_BINARY=$PWD/.build/pt_test \
      SHITTY_TOML_DUMP_BINARY=$PWD/.build/toml_dump SHITTY_TEST_PLATFORM=cocoa \
      SHITTY_TEST_VERSION=2026.09.03 SHITTY_TEST_FONTCONFIG=1 \
      python3 tst/run_unittest_group.py --group=0 --group-count=1
Ran 6399 tests in 123.086s
FAILED (failures=8, errors=14, skipped=5, expected failures=549)
EXIT=1

$ … SHITTY_TEST_FONTCONFIG=0 …
Ran 6399 tests in 115.827s
FAILED (failures=6, errors=14, skipped=17, expected failures=549)
EXIT=1
```

**Что сломалось:**

```console
$ comm -13 fails-before.txt fails-after.txt
(пусто)
EXIT=0
```

**Что перестало падать** — один тест, и это именно тот, из-за которого задача:

```console
$ comm -23 fails-before.txt fails-after.txt
FAIL: test_collection_face_and_representative_advances_define_cells (test_font_resolver.FontResolverTest.…)
EXIT=0
```

**Что стало пропускаться** — двенадцать записей, шесть тестов (каждый в наборе
исполняется дважды, см. «Обнаружено»), все шесть — под `skipUnless(FONTCONFIG_AVAILABLE)`:

```console
$ comm -13 skips-before.txt skips-after.txt | sed 's/ (test_font_resolver.*//' | sort -u
test_collection_face_and_representative_advances_define_cells
test_extra_fallback_fonts_keep_primary_metrics
test_fontconfig_family_loads_without_a_search_path
test_fontconfig_loads_all_four_style_faces_when_available
test_fontconfig_resolves_family_and_alias
test_overlay_width_is_independent_but_vertical_metrics_must_match
EXIT=0
$ comm -23 skips-before.txt skips-after.txt
(пусто)
```

Арифметика сходится: `failures 8 → 6` — это один тест, исполнявшийся дважды;
`skipped 5 → 17` — это шесть тестов, исполняемых дважды.

**Пять из шести раньше проходили.** Это не регрессия, а честность: они проходили
через CoreText, хотя декоратор говорит «нужен fontconfig». Решение, какой ширины
должен быть этот декоратор, — в `tst/**`, оно вне моих границ; вынес в «Обнаружено».

Наконец, набор прогнан в конфигурации со стором **после** правки, чтобы увидеть,
не отняла ли она чего-нибудь там, где бэкенд есть:

```console
$ env … SHITTY_TEST_BINARY=$PWD/.build-g14-on/st_test … SHITTY_TEST_FONTCONFIG=1 \
      python3 tst/run_unittest_group.py --group=0 --group-count=1
Ran 6399 tests in 144.285s
FAILED (failures=6, skipped=5, expected failures=549)
EXIT=1
```

`failures=6, skipped=5, expected failures=549` — **дословно** то, что записал `G13`
для той же конфигурации до этой правки. Одиннадцать шрифтовых тестов `G13`
по-прежнему проходят. **Закрыт.**

### Критерий 6 — `G3` и `G13` целы

`HAVE_SIMDUTF` на объекте зависимости, не тронут:

```console
$ grep -n "HAVE_SIMDUTF" build.py
225:    # translation unit can only see HAVE_SIMDUTF where the library is also on
230:    simdutf.public_cppflags += ["-DHAVE_SIMDUTF=1"]
235:    # HAVE_SIMDUTF does above: the runner walks a disabled target out of the
EXIT=0
```

(строка 235 — комментарий этой задачи, ссылающийся на приём `G3`.)

Локально simdutf не линкуется:

```console
$ nm .build/libshitty_prod.a | grep -c simdutf
0
$ otool -L .build/st | grep -ci simdutf
0
```

Признак `G13` работает: см. критерий 1 — три конфигурации дают три разных ответа,
и «стор виден, но `NIX_STORE` не задан» по-прежнему выключает пакеты.
`from_immutable_store` и `optional_pkg` не тронуты:

```console
$ git diff --stat
 build.py | 21 +++++++++++++++------
 1 file changed, 15 insertions(+), 6 deletions(-)
```

Дифф целиком — только блок `have_freetype_backend`. **Закрыт.**

### Дифф графа: что вообще изменилось в сборке

Локальная конфигурация (бэкенда нет), полный граф до и после — единственное
содержательное отличие:

```console
$ diff <(json before) <(json after) | grep -E '^[<>]' | grep -v '"node":\|"uid"\|"id":'
50 >    "SHITTY_TEST_FONTCONFIG": "0",
50 <    "SHITTY_TEST_FONTCONFIG": "1",
(остальное — списки хешей узлов, изменившиеся вслед за этими пятьюдесятью)
```

Ни одной изменившейся команды компиляции или линковки. Конфигурация со стором —
дифф из раздела 2: 27 объектов чистого C потеряли три define, которых не читают;
ни один объект ничего не приобрёл.

---

## Обнаружено

- **`dependency()` без аргументов остаётся ловушкой ещё в трёх местах** —
  `build.py:271` (`darwin_backend` на не-darwin), `build.py:275` (`vulkan`),
  `build.py:276` (`wayland_backend`). Все три — включённые пустые зависимости.
  Сегодня это безвредно ровно потому, что **никто не спрашивает их как булево**:
  единственные пять булевых проверок `Target` перечислены в критерии 2. Первый же
  `if vulkan:` или `"1" if wayland_backend else "0"` повторит дефект G14 дословно.
  Дешёвое лечение на будущее — не «поправить эти три», а сделать так, чтобы
  «выключено» было единственным способом сказать «пусто»: там, где зависимость
  заводится как заглушка, писать `dependency(enabled=False)`. Не делал: `vulkan`
  и `wayland_backend` на Linux переприсваиваются настоящими, и выключение
  заглушки меняло бы смысл на других платформах — это отдельное решение с
  отдельной проверкой.

- **Каждый тест `FontResolverTest` исполняется в наборе дважды.**
  `tst/test_font_fallback.py:10` делает `from test_font_resolver import FontResolverTest`
  на уровне модуля, и `unittest.defaultTestLoader.discover("tst")` находит класс в
  обоих модулях. Из 6399 тестов 6390 уникальны; все девять дублей — это
  `FontResolverTest`:

  ```console
  $ python3 -c "…discover('tst')…"
  всего тестов: 6399 уникальных: 6390 дублей: 9
  ```

  Стоимость — девять лишних прогонов терминала в каждом наборе и удвоение любого
  отказа этого класса в отчёте CI (что и наблюдалось: `failures=8` при семи
  разных именах). Лечится одной строкой — импортировать модуль, а не класс,
  — но это `tst/**`, вне моих границ.

- **Пять из шести тестов под `skipUnless(FONTCONFIG_AVAILABLE)` проходили без
  fontconfig.** После правки они честно пропускаются, и на macOS без бэкенда
  резолвер шрифтов остаётся покрытым только тремя тестами
  `FontResolverTest`, не имеющими декоратора. Похоже, декоратор шире, чем нужно:
  `test_fontconfig_resolves_family_and_alias` и соседи проверяют разрешение
  семейства, которое CoreText делает тоже. Правильный ответ, вероятно, — гейт по
  «есть **какой-нибудь** резолвер», а fontconfig-специфичным оставить только то,
  что и правда про fontconfig. Это правка `tst/**` и смена смысла тестов; по
  условию задачи я её не делал.

- **Глобальные `build.cppflags` — не «почти безопасно», а уже разъехавшееся
  состояние.** `G13` записал, что три `HAVE_*` разъехаться не могут. Проверка
  показала: 27 объектов получали define, не линкуясь ни с одной из библиотек.
  Безвредно лишь потому, что это C-хелперы, которые макросов не читают. Формулировка
  правила в `CLAUDE.md` («define вешается на сам объект зависимости») этим
  подтверждается на втором независимом примере после `base64.cpp`.

- **Ни один из трёх `HAVE_*` не встречается в заголовках** — только в `font.cpp`,
  `font_fontconfig.cpp`, `font_freetype.cpp` и `span_shaper_ut.cpp`. Это и сделало
  перенос безопасным. Если такой define когда-нибудь появится в `*.h`, перенос
  того же вида станет опасен: единицы трансляции разных целей увидят разные
  объявления. Проверять — тем же `grep` по `--include='*.h'`.

- **`flake.nix` снова не потребовал правок.** Как и в `G13`, это проверка на
  «второй признак»: договариваться двум файлам не о чем.

---

## Что осталось непроверенным

1. **Настоящий Nix не запускался** — ни `nix build`, ни `nix develop`: демон не
   поднят (то же, что записали `G3` и `G13`). Конфигурация «fontconfig есть»
   воспроизведена подставным стором `G13` со сторовой формой путей и
   рукописными `.pc`; библиотеки за симлинками — Homebrew'ские. Что
   `SHITTY_TEST_FONTCONFIG` равно `"1"` в **настоящей** Nix-сборке на darwin,
   подтвердит CI, а не этот отчёт.
2. **CI не прогонялся.** Не пушил. И этой правке нечего там показать: в
   Nix-сборке на darwin после `G13` fontconfig есть, флаг честно равен `"1"` и
   до правки, и после, так что `Tests Darwin` обязана остаться ровно такой же,
   какой её сделал `G13`. Что она такой осталась — не проверено.
3. **Не проверено на Linux.** Правка платформо-независима (`optional_pkg`
   выключает пакеты только на darwin, а перенос define не зависит от платформы),
   но ни одной сборки под Linux я не делал — ни локально, ни в docker.
4. **`-Werror` с перенесёнными define не проверялся.** `G13` гонял
   `CFLAGS=-Werror` для `font_freetype.cpp`; я этого не повторял. Набор флагов
   компиляции для всех C++-объектов после правки идентичен (дифф графа), поэтому
   новых предупреждений взяться неоткуда — но команда не запускалась.
5. **Пять локальных отказов полной сюиты** (`test_darkening_scales_with_the_option`,
   `test_legacy_arrow_modifier_matrix`, `test_russian_shift_ctrl_c_has_no_legacy_control_sequence`,
   `test_sheared_tail_lands_in_the_captured_blank`, `test_soft_zero_departs_from_the_hinted_grid`)
   и тринадцать `ERROR` шрифтовых тестов в локальной конфигурации — те же до и
   после, к правке отношения не имеют, не разбирались. Это ровно тот же остаток,
   который зафиксировал `G13`.
6. **Остальные четыре отказа Darwin-шардов** из шестнадцати — не мои по условию
   задачи, не смотрел.
7. **Три `dependency()` из «Обнаружено» не тронуты** и, соответственно, не
   проверены в изменённом виде.

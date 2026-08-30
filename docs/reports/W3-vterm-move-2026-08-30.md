# Волна 3: граница `lib/vterm`, переезд листьев ядра и гварды

- **Задачи:** `M2`, `M3`, `T2.1` плана `docs/plans/2026-08-29-upstream-merge.md`
- **Дата:** 2026-08-30
- **Ветка:** `wave/upstream-merge-w3` (основное рабочее дерево, не пушилась)
- **Эталон:** `docs/reports/T0.1-baseline-2026-08-30.md`, действующие числа после волны 2 — `docs/reports/M1-cheap-upstream-2026-08-30.md`
- **Входной материал по гвардам:** `docs/plans/reviews/upstream-merge-invariants.md`, §3
- **Статус:** **готово**, все три задачи закрыты, точек остановки не возникло

## 1. Что сделано

| Задача | Коммит | Одной строкой |
|---|---|---|
| `M2` | `9e7f2fb8` | `merge M2: the lib/vterm include boundary, staged before anything moves` |
| `M3` | `854e1246` | `merge M3: the dependency-free core leaves cross into lib/vterm` |
| `T2.1` | `461e849c` | `guards: a root, a dictionary, a bracket and a floor under all three [T2.1]` |

Порядок строго последовательный, каждая задача своим коммитом, ни одна не
начиналась до зелёного критерия предыдущей.

---

## 2. `M2` — граница включений

`git merge --no-ff da8d6ba3`. Апстрим кладёт корень репозитория на путь
включений (`lib/shitty` достаёт ядро как `<lib/vterm/...>`) и заводит узел
`vterm_boundary` над `lib/vterm/check_includes.py`.

**Один конфликт, в `build.py`:** апстрим дописал `vterm_boundary` в ту самую
строку `add_test(...)`, где у нас перечислены четыре гварда. Разрешено
объединением — в строке теперь `production_surface`,
`pretty_binary_branding`, `vterm_boundary` и все четыре гварда.

**Критерий `./build st -j 10` зелёная — выполнен, но не тем прогоном, каким
казалось.** См. находку 1: добавление корня в `build.includes` **не
инвалидирует ни одного узла**, и первый прогон после мержа вернул `exit 0`,
не исполнив ничего. Настоящее доказательство — `./build st -j 10 --clear`:
223 узла пересобраны, `exit 0`. `unit_tests` — `OK: 955`, `0 ERR`.

---

## 3. `M3` — переезд листьев ядра

`git merge --no-ff 9a5f67c8`. 54 файла (unicode-таблицы и сегментация, utf8,
клавиатурный энкодер, цвета, геометрия, типы ячеек, мелкие утилиты и словарь
`fatal`/`listener`/`input_handler`) уезжают из `lib/shitty` в `lib/vterm`
вместе со своими юнит-тестами и генератором unicode-таблиц.

### 3.1 Конфликты и как разрешён каждый

Десять конфликтующих файлов, в каждом ровно один хунк, и все — одной природы:
наш форк дописал включение рядом с тем, которое апстрим переписывал, и блок
включений разъехался. **Разрешение везде одинаковое по замыслу: обе стороны
сохранены** — наше локальное включение в кавычках, апстримное ядро в форме
`<lib/vterm/...>`.

| Файл | Наше | Апстримное | Как разрешено |
|---|---|---|---|
| `application.cpp` | `grid_geometry.h` | `num.h` | оба, наше первым |
| `composer_ut.cpp` | `mouse_frontend.h` | `listener.h` | оба |
| `options.cpp` | `quick_geometry.h` | `darts.h`, `fatal.h`, `num.h` | оба |
| `pty_ut.cpp` | `options.h` | `listener.h` | оба |
| `render_ut.cpp` | `render_blend.h` | `terminal_types.h` | оба, пустая строка между группами сохранена |
| `session_ut.cpp` | `drop_target.h`, `options.h`, `pane_layout.h` | `input_handler.h`, `listener.h` | оба |
| `test_mode.cpp` | `grid_geometry.h` | `grapheme.h` | оба |
| `vterm.cpp` | `grid_geometry.h` | `unicode_map.h`, `unicode.h`, `grapheme.h` | оба |
| `vterm_headless.cpp` | `grid_geometry.h` | `listener.h` | оба |
| `session.cpp` | `cell_extra_store.h`, `pane_layout.h` | **весь блок переупорядочен** | взят апстримный порядок, наши два включения возвращены в него |

`session.cpp` стоит особняком: этим коммитом апстрим починил `dev/style.py`
(его переупорядочивание включений было мёртвым — лицензионная шапка обрывала
разбор до начала), и блок включений `session.cpp` переписан целиком. Мы берём
новый порядок, а не свой.

`build.py` слился автоматически; проверено вручную, что наши darwin-строки
(`ui_csd_tabs.mm`, `ui_quick_hotkey.mm`, `ui_sidebar_tabs.mm`) в
`all_libshitty_sources` уцелели.

### 3.2 Наши файлы: кто уехал и не остался ли кто на старом пути

**Ни один из 54 переехавших файлов не нёс правок форка** — все 54 прошли как
чистые переименования, и вопрос «оставить у себя или отдать» на них не встал
ни разу. Проверено: в `lib/shitty` не осталось ни одного файла, одноимённого
файлу в `lib/vterm`.

Обратная сторона: **десять наших собственных файлов** включают переехавший
заголовок, но апстрим их никогда не трогал, поэтому его собственная замена
включений до них не дотянулась. Компилятор нашёл их сразу
(`render.h:10: fatal error: 'color.h' file not found`). Все переведены на
`<lib/vterm/...>`, по одному включению в каждом:

`application_ut.cpp`, `mouse_frontend.h`, `quick_frame_store.cpp`,
`quick_geometry.cpp`, `render.h`, `render_blend.h`, `tint_coat.h`,
`ui_csd_tabs_ut.cpp`, `ui_sidebar_tabs.mm`, `ui_sidebar_tabs_ut.cpp`.

Замена делалась скриптом по списку из 19 переехавших заголовков и один раз
промахнулась — по `ext/libstd/std/dbg/{color,panic}.cpp`, у которых
`"color.h"` и `"fatal.h"` **свои собственные, соседние**. Откачено до сборки;
`ext/libstd` — вендорная библиотека и в зону переезда не входит.

### 3.3 Доказательство, что разрешение конфликтов ничего не убавило

Приём с `M1`: сравнение `git diff --numstat` нашего мержа с апстримным
коммитом, поимённо и построчно.

```
git diff --numstat da8d6ba3 9a5f67c8      # апстрим: 108 файлов, +166/-146
git diff --numstat 9e7f2fb8 854e1246      # наш мерж: 119 файлов, +179/-158
```

Разница **строго аддитивна**: ни одна из 108 апстримных записей не пропала и
ни у одной не уменьшились числа. Расхождения ровно три вида:

- **+10 файлов** — наши десять из §3.2, по `1/1` каждый;
- `session.cpp` `13/13` → `14/14` и `session_ut.cpp` `1/1` → `2/2` — наши
  лишние включения в переупорядоченных блоках;
- **+`.gitignore` `1/0`** — см. находку 2.

### 3.4 Проверки

| Что | Результат |
|---|---|
| `./build st -j 10` | exit 0 |
| `./build -j 10` | exit 0 |
| `./build unit_tests st_test pt_test toml_dump pty_test_helper -j 10` | exit 0 |
| `.build/unit_tests` | **OK: 955, 0 ERR** |
| `./build vterm_boundary …` | `[VB]` исполнен, exit 0 — граница включений держится на реальном коде |

---

## 4. `T2.1` — гварды

Три причины поломки лечатся тремя разными правками, и к ним добавлена
четвёртая — самозащита, без которой три первых можно сделать формально.

### 4.1 Что изменено в `build.py`

| Что | Было | Стало |
|---|---|---|
| `guard_scan_roots` | `("lib/shitty", "ext/plt", "bin")` | `("lib/shitty", "lib/vterm", "ext/plt", "bin")` |
| `pane_grid_names` | 4 имени (`composer.columns` и род.) | 8 имён — те же плюс `composer.geometry.columns`/`rows` и формы `composer_.` |
| `border_pixels_guard` | `str.count` подстроки по сырому тексту | `blanked()` + `\bимя\s*\(` — считается **вызов**, а не подстрока |
| `border_pixels_allowance` | `composer.h: 5`, `grid_geometry.h: 1`, … | `composer.h: 2`, `grid_geometry.h` **удалён**, остальное без изменений |
| `mouse_geometry_allowance` | `lib/shitty/mouse_frontend.{h,cpp}: 1` | **без изменений** |
| самозащита | только у `darwin_call_guard` (`if not tracked`) | у всех четырёх |

`guard_source_reader` перенесён выше `border_pixels_guard_program` — он ему
теперь нужен. Перенос механический, тело не менялось.

**Разрешения не расширены.** Изменений в разрешениях ровно два, и оба —
сужение: `composer.h` с `5` до `2` и удаление ключа `grid_geometry.h`. Ни
одного нового ключа, ни одного выросшего числа, `mouse_geometry_allowance` не
тронут вовсе. Точка остановки №3 плана не наступила.

### 4.2 Почему `composer.h` стал `2`, а `grid_geometry.h` исчез

Не послабление, а следствие смены сопоставления. Пять старых срабатываний в
`composer.h` — это два объявления и **три упоминания в комментариях**;
единственное срабатывание `grid_geometry.h` — комментарий целиком:

```
lib/shitty/composer.h:54:  // layout geometry; borderPixels() stays for reading the option itself.
lib/shitty/composer.h:60:  // contentScale before it lands in an Insets field, exactly as borderPixels()
lib/shitty/composer.h:126:     u16 borderPixels() const;
lib/shitty/composer.h:129: // Insets above). borderPixels() is its first caller and the chrome
lib/shitty/composer.h:145:     u16 scaledPixels(u16 points) const;
lib/shitty/grid_geometry.h:16: // that used to spell `2 * borderPixels()` around a grid goes through
```

Это вторая находка `G1` с другой стороны: файл мог израсходовать разрешение
прозой и пропустить настоящий вызов бесплатно. После перехода на
`blanked()` числа — это вызовы и только вызовы. Побочный выигрыш: запрет
«объяснять `A1` в прозе внутри сканируемого файла» снят, `grid_geometry.h`
не придётся раздваивать в `T5.2`.

### 4.3 Самозащита — что именно проверяет каждый гвард

Не «файлов ноль», а «то, что я обязан читать, я прочитал»:

- `border_pixels_guard` и `mouse_geometry_guard` — **каждый ключ разрешения
  обязан оказаться среди просканированных файлов.** Устаревший ключ (файл
  уехал) — красный гвард с текстом «re-key the allowance onto where these
  live now», а не зелёная пустота. Это ровно тот отказ, который `T0.3`
  доказала пробой для `mouse_geometry_guard`;
- `pane_grid_guard` разрешений не имеет, поэтому он требует, чтобы скан
  дошёл до **всех трёх рендер-бэкендов** — `render_metal.mm`,
  `render_reference.cpp`, `render_vk.cpp`. Слабее — «хотя бы один файл
  `render*`» — не годится: этому удовлетворяет один `render_blend.h`, и
  гвард остался бы зелёным, потеряв из виду все три настоящих бэкенда;
- `darwin_call_guard` — уже имел `if not tracked`, не тронут. Он же
  единственный, кого переезд не мог ослепить.

### 4.4 Пять доказательств

Все прогоны — **прямым исполнением текста программы гварда, вытащенного из
`build.py` через `ast`**, а не через `./build`: `.build` адресуется
содержимым, штамп возвращается симлинком из `.build/cas/` без исполнения.
Скрипт: `<scratchpad>/w3-rung.py` (по рабочему дереву) и `w3-rung2.py`
(по произвольному дереву и произвольному `build.py`).

#### Доказательство 1. Каждый из четырёх краснеет на нарушении **в `lib/vterm`**

Четыре отдельные пробы, каждая — один временный файл, после каждой откат.

```
--- lib/vterm/zz_guard_probe.h: return composer.borderPixels();
== border_pixels_guard_program: RED (rc=1)
borderPixels()/scaledPixels() are the border option and its scale, not the layout (A1): contentInsets() is what layout reads.
Unallowed uses:
  lib/vterm/zz_guard_probe.h:2

--- lib/vterm/zz_guard_probe.h: return mouseGeometry(composer);
== mouse_geometry_guard_program: RED (rc=1)
mouseGeometry(const Composer&) is the pane that fills the window (A8), which production no longer gets to assume: pass the pane's origin.
Unallowed uses:
  lib/vterm/zz_guard_probe.h:2

--- lib/vterm/render_zz_guard_probe.h: return composer_.geometry.columns;
== pane_grid_guard_program: RED (rc=1)
A renderer takes the grid of the pane it is drawing from the update that carries its cells (A9: TerminalUpdate::gridColumns/gridRows), never from the window - the composer's grid is the window with one pane in it.
Unallowed uses:
  lib/vterm/render_zz_guard_probe.h:2

--- lib/vterm/zz_guard_probe.cpp: createMetalRenderer();  (вне всякого #if)
== darwin_call_guard_program: RED (rc=1)
A darwin-only symbol is called where a non-Apple build reaches it, which is an unresolved symbol on every platform but macOS (R2-test, L1).
Tracked: applyQuickFrameToWindow createCsdTabsUi createMetalRenderer createQuickHotkey createSidebarTabsUi
Unguarded calls:
  lib/vterm/zz_guard_probe.cpp:2  createMetalRenderer
```

В каждой пробе остальные три гварда оставались зелёными — то есть краснел
именно адресуемый. `Tracked:` по-прежнему **ровно пять символов**: добавление
корня `lib/vterm` не сузило множество, которое стережёт `darwin_call_guard`.

#### Доказательство 2. Самозащита: `lib/shitty` временно убран из корней

```
--- self-defence: lib/shitty dropped from the scan roots ---
== border_pixels_guard_program: RED (rc=1)
the border audit is allowing files the scan never reached, so it is guarding a tree that no longer exists: re-key the allowance onto where these live now, or drop them.
Unreachable:
  lib/shitty/composer.cpp
  lib/shitty/composer.h
  lib/shitty/composer_ut.cpp
  lib/shitty/mouse_frontend_ut.cpp
  lib/shitty/test_mode.cpp
== mouse_geometry_guard_program: RED (rc=1)
the pointer-geometry audit is allowing files the scan never reached, so the form it exists to meter now lives somewhere it cannot see: re-key the allowance onto where these live now.
Unreachable:
  lib/shitty/mouse_frontend.cpp
  lib/shitty/mouse_frontend.h
== pane_grid_guard_program: RED (rc=1)
the renderer grid audit never reached a renderer it is meant to cover, so it passed by reading nothing: point the scan roots at where these live now.
Unreachable:
  render_metal.mm
  render_reference.cpp
  render_vk.cpp
== darwin_call_guard_program: RED (rc=1)
the darwin call audit tracks nothing at all, which means it stopped working
```

Все четыре. `build.py` восстановлен из копии, `git diff --stat` показывает
только правку `T2.1`.

#### Доказательство 3. `pane_grid_guard` краснеет на `geometry.columns` в рендерере

Слабое доказательство — искусственная проба; сильное — **прогон по чистому
дереву `origin/master`**, где три рендерера читают
`composer_.geometry.columns`/`rows` **47 раз** (из них 24 в
`render_reference.cpp`, как измерила `T0.3`):

```
=== OLD guards (pre-T2.1) on a clean origin/master tree ===
== pane_grid_guard_program: rc=0  reported lines=0  files=0

=== NEW guards (T2.1) on the same tree ===
== pane_grid_guard_program: rc=1  reported lines=47  files=3
     lib/shitty/render_metal.mm
     lib/shitty/render_reference.cpp
     lib/shitty/render_vk.cpp
```

Старый словарь на этом дереве **зелёный, `rc=0`, пустой вывод** — воспроизведена
худшая из трёх поломок; новый видит все 47. Правка корней здесь не помогла бы
ничем: рендереры никуда не уезжают.

Отдельная проба подтвердила, что **старые имена не сломаны**: файл
`lib/shitty/render_zz_guard_probe.h` со строкой `composer.columns` (стр. 2) и
строкой `composer_.geometry.rows` (стр. 3) даёт две записи — обе.

#### Доказательство 4. `border_pixels_guard`: поле — зелёный, вызов — красный

Парная проба, один и тот же временный файл в `lib/shitty`:

```
--- A: geometry.borderPixels (upstream field) in lib/shitty layout ---
== border_pixels_guard_program: GREEN (rc=0)

--- B: composer.borderPixels() call in lib/shitty layout ---
== border_pixels_guard_program: RED (rc=1)
borderPixels()/scaledPixels() are the border option and its scale, not the layout (A1): contentInsets() is what layout reads.
Unallowed uses:
  lib/shitty/zz_guard_probe.h:4
```

И то же самое в масштабе, на чистом `origin/master`:

```
=== OLD guards (pre-T2.1) on a clean origin/master tree ===
== border_pixels_guard_program: rc=1  reported lines=37  files=8
     bin/main_fuzz/main.cpp, lib/shitty/application.cpp, render_metal.mm,
     render_reference.cpp, render_reference_ut.cpp, render_vk.cpp,
     span_shaper_ut.cpp, test_mode.cpp

=== NEW guards (T2.1) on the same tree ===
== border_pixels_guard_program: rc=0  reported lines=0  files=0
```

**37 ложных срабатываний в 8 файлах → ноль, и ни одно разрешение при этом не
выросло.** Число и список файлов совпадают с измерением `T0.3` §3.2 до
единицы. Зелёный здесь настоящий, а не от пустоты: все пять ключей разрешения
на упомянутом дереве существуют, поэтому самозащита не срабатывает и скан
действительно читал эти файлы.

#### Доказательство 5. Разрешения не расширены

Диффа разрешений всего две строки, обе — сужение:

```
-    "lib/shitty/composer.h": 5,
+    "lib/shitty/composer.h": 2,
-    "lib/shitty/grid_geometry.h": 1,
```

`mouse_geometry_allowance` в диффе отсутствует.

#### Побочное доказательство. Гвард уже говорит вслух то, о чём раньше молчал

Тот же прогон новых гвардов по `origin/master`:

```
== mouse_geometry_guard_program: rc=1
the pointer-geometry audit is allowing files the scan never reached, so the form it exists to meter now lives somewhere it cannot see: re-key the allowance onto where these live now.
Unreachable:
  lib/shitty/mouse_frontend.cpp
  lib/shitty/mouse_frontend.h
```

На апстримном дереве `mouse_frontend.{h,cpp}` лежат в `lib/vterm`, а
`mouse_frontend_ut.cpp` — в `lib/shitty` (та самая асимметрия из критериев).
Старый гвард на этом дереве возвращал `rc=0`. Новый требует перекеивания — и
это ровно то, что должно случиться в волне, где файл переедет; сегодня он
ещё в `lib/shitty`, поэтому ключи не тронуты.

---

## 5. Числа против эталона

### 5.1 `unit_tests`

| | Эталон (после волны 2) | После `M2` | После `M3` | После `T2.1` |
|---|---|---|---|---|
| OK | 955 | **955** | **955** | **955** |
| ERR | 0 | **0** | **0** | **0** |

**Сверка поимённая, а не по числу.** Списки имён тестов, снятые из вывода
прогонов после `M3` и после `T2.1`, — `diff` пуст, 955 = 955. Это существенно
именно здесь: `M3` увёз 20 файлов `*_ut.cpp` в `lib/vterm`, и «955 = 955»
могло бы скрыть «потеряли три, приобрели три». Приобрести было нечего —
коммит не добавляет ни одного теста, только переносит, — а потерять не
потеряли: `build.py` подхватывает `lib/vterm/*_ut.cpp` (правка апстрима в
`unit_sources`), и число не дрогнуло ни на одном из трёх шагов.

### 5.2 Питоновский набор

| Показатель | Эталон (после волны 2) | Волна 3 | Расхождение |
|---|---|---|---|
| прогнано | 6398 | **6398** | нет |
| `failures` | 6 | **6** | нет |
| `errors` | 14 | **14** | нет |
| `skipped` | 17 | **17** | нет |
| `expected failures` | 549 | **549** | нет |
| `unexpected successes` | 0 | **0** | нет |

**Сверка поимённая.** Двадцать строк `FAIL:`/`ERROR:` из логов двадцати групп
совпадают с разделами 4 и 5 эталона построчно — те же шесть падений и те же
четырнадцать ошибок, включая дубль
`test_font_resolver.FontResolverTest.test_font_file_path_is_not_treated_as_a_family`
(находка 2 эталона; дубль на месте).

**Сверка состава набора, а не только итогов.** Полный список идентификаторов
тестов снят обходом `unittest.defaultTestLoader.discover("tst")` и сравнён со
списком, снятым тем же способом после волны 2
(`<scratchpad>/post-ids.txt`, 6398 строк):

```
diff post-ids.txt w3-ids.txt   →  IDENTICAL
```

Ни один тест не появился, не исчез и не переименовался. Числа не изменились —
и это ожидаемо: `M3` из питоновского набора трогает единственную строку в
`tst/test_unicode_data_generator.py` (путь к генератору), новых тестов не
приносит.

### 5.3 Отдельно затребованные файлы

| Файл | Результат |
|---|---|
| `tst/test_pane_alt_screen.py` | **OK** — фикс плана `pane-frame-stall` пережил обе мержа |
| `tst/test_panes_protocol.py` | **OK** — протокол панелей пережил |
| `tst/test_unicode_data_generator.py` | **OK** — единственный питоновский файл, который правил `M3` |
| `tst/test_build_metadata.py` | **OK** (skipped=2 — те же два strace-пропуска эталона) |

---

## 6. Обнаружено

### 1. `./build` не замечает смены путей включений — «зелёная сборка» после `M2` ничего не значила

`build.includes += [..., "$(S)", ...]` — единственная правка `M2`, влияющая на
компиляцию, — **не инвалидировала ни одного узла**. Первый `./build st -j 10`
после мержа вернул `exit 0` с **пустым выводом**, включая `-v`; `.build/st`
при этом был перевыпущен симлинком.

Проверено контролем: временный `-DW3_PROBE=1` в `build.cppflags` пересобрал 73
узла. То есть **`cppflags` в ключ узла входят, а `includes` — нет**. Проба
откачена.

Практический вывод для следующих волн: после мержа, который трогает только
`build.py`, «зелёная сборка» доказывается `./build … --clear` (или изменением
содержимого входов), а не пустым выводом. Это тот же класс ловушки, что и
штампы гвардов из `.build/cas/`, только на компиляции, и в отчёте `T0.1` его
нет. `M3` от этого не пострадала: там менялось содержимое файлов.

### 2. Новая цель `vterm_boundary` оставляет в корне симлинк, которого нет в `.gitignore`

`./build` создаёт `./vterm_boundary → .build/vterm-boundary.stamp`, а
`.gitignore` о нём не знает — ровно как было с `core_perf` в волне 2. `git add
-A` при разрешении `M3` затянул его в коммит; поймано сверкой `--numstat`
(лишняя запись `vterm_boundary 1 0`), симлинк снят с индекса, `/vterm_boundary`
дописан в `.gitignore` рядом с четырьмя гвардами, коммит `M3` поправлен через
`--amend`. `.gitignore` числится за `T6.4` волны 7 — там осталось свести
апстримную и нашу версии, эта строка уже на месте.

### 3. Апстримный `grapheme_source` в `build.py` указывает в пустоту

`build.py:726` — `grapheme_source = "$(S)/lib/shitty/grapheme.cpp"`, а файл
уехал в `lib/vterm`. Переменная **нигде больше не используется** (одно
вхождение и у нас, и в `build.py` самого `9a5f67c8`), поэтому ничего не
ломает. Это апстримный недосмотр, не наш; трогать не стал — правка ушла бы в
расхождение ради мёртвой строки. Кандидат в `T6.1`, где `build.py` и так
сводится.

### 4. Остаточная щель `pane_grid_guard`, названная явно

Словарь квалифицирован (`composer.geometry.columns`, а не голое
`geometry.columns`) сознательно: голое имя покраснеет на законном чтении
геометрии **панели** из `TerminalUpdate`, когда такое поле появится, — то есть
запретит ровно то, чего `A9` требует. Цена: рендерер, который свяжет
`const auto& g = composer.geometry;` и прочитает `g.columns`, гвард не увидит.
Сегодня такого нет ни у нас, ни в `origin/master`; записано, чтобы `T4.*` и
`T6.1` знали, куда смотреть.

### 5. Скрипт замены включений и вендорный `ext/libstd`

Массовая замена `#include "X.h"` → `#include <lib/vterm/X.h>` по списку имён
задевает `ext/libstd/std/dbg/{color,panic}.cpp`, у которых `color.h` и
`fatal.h` **свои соседние**. Совпадение имён, не более, но следующий такой
проход по `ext/` наступит на то же. Откачено до сборки.

---

## 7. Риски и на что смотреть ревьюеру

1. **`border_pixels_allowance` пересчитан.** Числа теперь означают вызовы, а не
   подстроки. Если ревьюер сверяет их с прошлым отчётом — они обязаны
   отличаться, и §4.2 объясняет чем. Ошибкой было бы вернуть `5` и `1`.
2. **Самозащита сделает следующие переезды шумными, и это замысел.** Когда
   `mouse_frontend.{h,cpp}` уедут в `lib/vterm` (волна 6, `T5.1`),
   `mouse_geometry_guard` покраснеет с текстом «re-key the allowance». Это не
   регрессия, а сигнал — перекеить ключи на новые пути, счётчики не трогать.
   То же для `border_pixels_allowance`, если `composer.h` когда-нибудь поедет.
3. **`pane_grid_backends` — жёсткий список из трёх имён.** Переименование или
   переезд любого из трёх рендереров уронит гвард. Тоже замысел: молчаливая
   потеря бэкенда из скана — это и есть отказ, который закрывает `T2.1`.
4. **`guard_source_reader` переехал выше по файлу.** Перенос механический,
   тело не менялось, но в диффе он выглядит как большое удаление и большая
   вставка — стоит смотреть `git diff -M`.
5. `M2` и `M3` — мерж-коммиты `--no-ff`; их содержательная часть целиком в
   разрешении конфликтов, а доказательство полноты — сверка `--numstat` из
   §3.3, а не чтение диффа построчно.

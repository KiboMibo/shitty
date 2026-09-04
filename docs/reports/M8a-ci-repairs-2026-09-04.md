# `M8a` — первая порция остатка апстрима: починки CI и шесть ушедших красных

Шаг: `git merge 0a7905ac`, шесть коммитов от 2026-08-24. Ветка
`wave/upstream-merge-w8` от `master` (`9c5eded2`). Мерж-коммит — `79b08500`.

**Короткий ответ:** из двадцати унаследованных красных питоновского набора ушли
**шесть**, новых нет, `unit_tests` остались `OK: 968` с побайтово тем же списком
имён, `st` — те же 229 узлов. Четыре конфликта, во всех взята наша сторона; один
апстримный хунк **отклонён** с обоснованием (§3).

**Главная оговорка, которую нельзя пропустить:** самый крупный коммит порции —
`3c91f408`, +99/−25 в `lib/shitty/font_freetype.cpp` — **на этой машине не
компилируется вовсе**. FreeType выключен `optional_pkg()` на darwin, и файла нет
ни в одном локальном узле. Ради чего порция шла первой (гвард «глиф-бомбы» и
`test_wide_ligature_overflow`) проверяется только Linux-CI. Подробно — §5.

---

## 1. Что принесли шесть коммитов

| Коммит | Файлы | Что делает |
|---|---|---|
| `69b04fe8` | `build.py` | libstd после libplt в четырёх строках линковки embed: `ld.bfd` разрешает архивы одним проходом |
| `3c91f408` | `build.py`, `font_freetype.cpp` | оверсайз-глифы через `FT_Outline_Get_Bitmap` мимо гварда FreeType 2.14.2; simdutf в зависимостях embed |
| `c049a672` | `ui_csd_tabs.mm`, `pty_test_helper.c`, `test_contour_input_generator.py`, `test_soft_render.py` | четыре починки Darwin-шардов |
| `ee9a576a` | `build.py`, `pty_ut.cpp`, `pretty_binary_branding.py`, `test_ghostty_key_encoding_tail.py`, `test_italic_overhang.py` | четыре места, где тест был откалиброван по Linux |
| `18fc9a20` | `build.py`, `pty_ut.cpp`, `test_italic_overhang.py` | флуд без калибровки под буферы хоста; полная опись чтений для песочницы |
| `0a7905ac` | `platform_cocoa.mm` | запасной заголовок меню — нейтральный бренд |

Множество файлов снято командой по диапазону, а не по ожидаемому каталогу
(ошибка `M7`):

```console
$ git diff --stat 44d61bfc 0a7905ac
 11 files changed, 162 insertions(+), 73 deletions(-)
```

Одиннадцать файлов апстримной стороны против девяти нашей: два файла
(`platform_cocoa.mm`, `ui_csd_tabs.mm`) у нас **не изменились ни на строку** —
обе правки уже стояли, сделанные по-своему. Это §2.

---

## 2. Каждый конфликт: чья сторона и почему

Четыре конфликтных файла, тринадцать конфликтных блоков.

### 2.1. `lib/shitty/ui_csd_tabs.mm` — девять блоков, все наши

Апстрим переименовывает `ShittyTabBarView` → `CsdTabBarView` и две константы
`shittyTab*` → `csdTab*`. Причина у него та же, что у нашей задачи `G5`: имена
классов ObjC переживают `strip` в метаданных рантайма, и `pt` не проходит
`tst/pretty_binary_branding.py`.

**У нас этого переименования нет — потому что оно уже сделано, и шире.** В нашей
версии `ShittyTabBarView` не встречается ни разу:

| | база | апстрим | у нас |
|---|---|---|---|
| класс полосы вкладок | `ShittyTabBarView` | `CsdTabBarView` | `TerminalTabBarView` |
| константы | `shittyTabPlusWidth/CloseZone` | `csdTab…` | `tabPlusWidth/tabCloseZone` |
| заливка титлбара | — | — | `TerminalTitlebarFillView` |
| трекер наведения | — | — | `TerminalChromeHoverView` |

Взята наша сторона во всех девяти. Два довода, оба проверяемые:

1. `Terminal` — это ровно тот нейтральный литерал, которым отвечает
   `GenericBrand` в `lib/shitty/brand.cpp`; `Csd` не отвечает ничему.
2. Наша ветка завела **ещё два** класса в этом файле, которых у апстрима нет, и
   назвала их `Terminal*`. Приняв `CsdTabBarView`, мы получили бы один класс из
   трёх с чужим префиксом.

Проверено, что разрешение ничего не потеряло: результат **побайтово равен**
нашей предмержевой версии.

```console
$ diff <(git show 9c5eded2:lib/shitty/ui_csd_tabs.mm) lib/shitty/ui_csd_tabs.mm
(пусто)
```

### 2.2. `ext/plt/platform_cocoa.mm` — один блок, наш

Строка кода у обеих сторон **одна и та же**:

```objc
[NSApp setMainMenu:cocoaBuildMainMenu(name != nil ? name : @"Terminal")];
```

Конфликтует только комментарий над ней. Наш называет механизм
(`GenericBrand`, `tst/pretty_binary_branding.py`, «этот слой обслуживает каждый
бренд из этого дерева»), апстримный — следствие. Взят наш.

**Отсюда следует, что плановая задача `T6.2`** («`platform_cocoa.mm`: наш
Accessory + апстримный fallback») **уже `NO-OP`**: `setActivationPolicy` наш и
никем не тронут, fallback совпал буквально.

### 2.3. `tst/pty_test_helper.c` — два блока, наши по существу

Это плановая задача `T6.3`, и она разрешилась ровно так, как её и записали:
«Darwin sigwait + наш `catch_signal`».

**Блок 1.** Апстримный `wait_for_winsize()` — это блокировка SIGWINCH,
`sigwait()` и чтение `TIOCGWINSZ`. Наш файл эту функцию давно расщепил:
`report_winsize()` только читает размер (её зовут три режима `main`, включая
`winsize-now-hold`), а `wait_for_winsize()` несёт машинерию, которой у апстрима
нет вовсе.

**Блок 2.** `wait_for_hangup()` у нас переводит tty в raw-режим (`cfmakeraw`),
потому что канонический pty на macOS писателя не блокирует — `ttyinput()`
выбрасывает всё за `TTYHOG` и звонит в звонок. Апстрим здесь только
переформатировал условие в одну строку.

Разрешение: **наш блок кода + апстримное форматирование условия**. Итог отличается
от нашей предмержевой версии ровно тремя перестроениями строк, которые апстрим
сделал своим форматтером, и ничем больше.

**Плюс одно снятие, которого мерж не просил.** Апстрим добавляет
`#define _DARWIN_C_SOURCE` перед `_POSIX_C_SOURCE`; этот блок слился
**автоматически**, и файл получил **два** определения одного макроса — наше
стоит ниже с 2-й волны и объясняет, почему файл раньше не компилировался на
macOS вовсе (`R2-test`, `I11`). Апстримная копия удалена: определение
идентичное, а комментарий у `_DEFAULT_SOURCE` ссылается на «`_DARWIN_C_SOURCE`
**below**» и со второй копией выше читался бы неверно.

### 2.4. `build.py` — один блок, наша структура и все пять апстримных правок

Конфликт чисто структурный: задача «А» обернула весь embed-блок в
`if embed_facade_links:` (флаг `True`, 37 тестов идут), апстрим правил тот же
блок без обёртки, и git не сложил сдвиг отступа с правками внутри.

Взята **наша обёртка** и **все пять апстримных изменений внутри неё**:

1. `libshitty_vt_core.deps` += `simdutf`
2. `example.deps` → `[libshitty_vt_core, plt_headless, libstd_pic, simdutf]`
3. `shitty_vt_a`: `plt_headless.output` перед `libstd_pic.output` во входах и в команде
4. `shitty_vt_so`: то же плюс `*simdutf.ldflags` в команде
5. `python_test_inputs` += `ext/fonts/*`, `ext/LICENSE.*`, `tst/corpus/*`

Ноль расхождений по существу — доказано нормализацией отступа:

```console
$ diff <(наш дифф build.py, отступы сняты) <(апстримный дифф build.py, отступы сняты)
(пусто)          наших строк: 18, апстримных: 18
```

**Риск `69b04fe8` не реализовался.** Перестановка `libstd`/`libplt` конфликта с
фасадом задачи «А» не дала: «А» переиндентировала блок, но порядок аргументов не
трогала. Проверка — §7, критерий 6.

---

## 3. Отклонённый апстримный хунк: гвард `__APPLE__` над `ResizeReachesChildAsWinch`

Единственное место, где взято **не то, что принёс апстрим**, и не по причине
«у нас уже есть». Выделено в свой раздел, потому что это отказ, а не выбор из двух.

`ee9a576a` оборачивает тест `Pty::ResizeReachesChildAsWinch` в
`#if !defined(__APPLE__)` с комментарием:

> Undiagnosed: either the runner's sandbox suppresses the signal … or our spawn
> path leaves the child outside the foreground process group there … Needs a
> real Mac to tell the two apart.

**Диагноз у нас есть, и он третий.** `tst/pty_test_helper.c:70-90` (наш, со
2-й волны) описывает механизм: у SIGWINCH диспозиция по умолчанию —
«игнорировать», а XNU выбрасывает такой сигнал **в момент генерации** —
`psignal_internal()` смотрит `p_sigignore` раньше, чем заблокированную маску.
Поэтому `sigwait()` по SIGWINCH не вернётся никогда, пока процесс не снимет
сигнал с `SIG_IGN`. Наш хелпер это делает, апстримный — нет:

```console
$ git show 0a7905ac:tst/pty_test_helper.c | grep -c "catch_signal\|sigaction"
0
$ grep -c "catch_signal\|sigaction" tst/pty_test_helper.c
2
```

Значит апстримный гвард — обход дефекта, которого в нашем дереве нет. Цена его
принятия измерена, а не оценена: тест **зелёный** на этой машине до мержа —

```console
$ grep "ResizeReachesChildAsWinch" <эталонный вывод unit_tests>
+ Pty::ResizeReachesChildAsWinch
```

— и `__APPLE__` — единственная платформа, на которой гвард срабатывает. То есть
принять его = потерять проходящий тест ровно там, где он проходит.

Гвард снят, на его месте — комментарий с этим разбором, чтобы следующий мерж не
вернул его молча.

**Вторая половина `18fc9a20` принята целиком**: писатель флуда стал потоком без
границы (`for (;;) sendAll(...)`) вместо мегабайта, откалиброванного под Linux.
Это ортогонально нашему `cfmakeraw` и лучше него в смысле переносимости — обе
правки работают на один результат, и обе оставлены.

---

## 4. simdutf и свойство `G3`

Отдельный раздел по прямому требованию брифа.

### 4.1. Свойство цело, и оно в одном месте

`HAVE_SIMDUTF` во всём дереве встречается пять раз, из них **ровно одно**
присваивание:

```console
$ grep -rn "HAVE_SIMDUTF" build.py lib/ ext/ bin/
build.py:226:    # translation unit can only see HAVE_SIMDUTF where the library is also on
build.py:231:    simdutf.public_cppflags += ["-DHAVE_SIMDUTF=1"]
build.py:236:    # HAVE_SIMDUTF does above: the runner walks a disabled target out of the
lib/vterm/base64.cpp:15:// build.py hangs HAVE_SIMDUTF on the same dependency that carries the link
lib/vterm/base64.cpp:17:#if defined(HAVE_SIMDUTF)
lib/vterm/base64.cpp:169:#if defined(HAVE_SIMDUTF)

$ grep -n "build\.\(cpp\|cxx\|c\)flags.*SIMDUTF" build.py
(пусто)
```

Define висит на `simdutf.public_cppflags` — на том самом объекте, из чьих
`ldflags` берётся `-lsimdutf`. В глобальных `build.cppflags` его нет (там только
`SHITTY_VERSION`, `HAVE_CORETEXT`, `HAVE_METAL_RENDERER`, `HAVE_VULKAN_WAYLAND`).

### 4.2. Проверено артефактами, с обеими сторонами

Одного `grep` мало: он показывает намерение, а не то, что раннер действительно
проносит define и `-l` вместе. Положительный контроль — временная подмена
`optional_pkg` на `pkg_config` (simdutf 9.1.0 на этой машине есть), сборка
`example` в отдельный каталог `-B`, проба снята сразу после:

```console
--- simdutf ВКЛЮЧЁН ---
$ nm -u <probebuild>/obj/libshitty_vt_core/lib/vterm/base64.cpp.o | grep simdutf
__ZN7simdutf16binary_to_base64EPKcmPcNS_14base64_optionsE
$ otool -L <probebuild>/example | grep simdutf
	/opt/homebrew/opt/simdutf/lib/libsimdutf.35.dylib

--- simdutf ВЫКЛЮЧЕН (штатно на darwin) ---
$ otool -L .build/example | grep -c simdutf
0
```

Компиляция породила ссылку на `simdutf::binary_to_base64` **и** линковка
принесла библиотеку — в одной сборке. С выключенной зависимостью нет ни того, ни
другого. Состояние «define есть, библиотеки нет» невыразимо и в embed-графе тоже.

### 4.3. Что `3c91f408` для нас делает на самом деле

Апстрим пишет: «only the realm's blanket LDFLAGS kept the example linking
locally». Это про **их** дерево, где `base64.cpp` решал по `__has_include`.

У нас после `G3` embed-цели были защищены **иначе**: `simdutf` не значился в их
`deps` вовсе, значит `-DHAVE_SIMDUTF=1` до `base64.cpp` в `libshitty_vt_core` не
доходил, и embed-ядро всегда шло **портируемой веткой**. Ломаться было нечему.

Поэтому для нас эта половина коммита — **не починка, а включение**: с этого мержа
base64 embed-ядра берёт быстрый путь simdutf везде, где библиотека есть. Разница
поведения реальная, и её стоит назвать вслух: раньше `libshitty_vt_core`
кодировал base64 портируемым кодом на всех платформах без исключения.

---

## 5. Какие красные ушли

Эталон снят **на этом же дереве до первой правки**, тем же режимом, которым
мерился результат: одним процессом, `--group=0 --group-count=1`, окружение из
`build.py:1275-1284`.

```
эталон (9c5eded2): Ran 6437 tests in 132.468s
                   FAILED (failures=6, errors=14, skipped=17, expected failures=549)
итог (79b08500):   Ran 6437 tests in 123.267s
                   FAILED (errors=14, skipped=19, expected failures=549)
```

`failures` — **6 → 0**. Поимённо ушли ровно шесть:

| Тест | Кем починен | Как |
|---|---|---|
| `test_soft_render.SoftRenderTest.test_soft_zero_departs_from_the_hinted_grid` | `c049a672` | класс пропущен на cocoa: `-soft` крутит растеризатор FreeType, а первым в цепочке стоит CoreText |
| `test_soft_render.SoftRenderTest.test_darkening_scales_with_the_option` | `c049a672` | то же |
| `test_contour_input_generator…test_legacy_arrow_modifier_matrix (key=262, modifiers=8)` | `c049a672` | Cmd+Left/Right — резервация обхода вкладок macOS (issue 82), до pty не доходит |
| `test_contour_input_generator…test_legacy_arrow_modifier_matrix (key=263, modifiers=8)` | `c049a672` | то же |
| `test_ghostty_key_encoding_tail…test_russian_shift_ctrl_c_has_no_legacy_control_sequence` | `ee9a576a` | на macOS аккорд не привязан, кодировщик отдаёт `\x03` базовой раскладки |
| `test_italic_overhang…test_sheared_tail_lands_in_the_captured_blank` | `ee9a576a` + `18fc9a20` | CoreText наклоняет дальше, чем FreeType; проверяется граница — следующая клетка пуста |

```console
$ comm -13 <эталон: 20 красных> <итог: 14 красных>
(пусто)   — НОВЫХ КРАСНЫХ НЕТ
```

`skipped 17 → 19` — это ровно два теста `SoftRenderTest`, названные в выводе
(`skipped 'CoreText renders here; -soft is a FreeType knob'`).

### 5.1. Остались четырнадцать, и все одного рода

`test_bitmap_font_render` ×9, `test_synthesized_symbols` ×2, `test_font_resolver`
×2, `test_color_font_render` ×1 — все `ERROR`, все средовые: fontconfig, freetype
и harfbuzz выключены `optional_pkg()` на darwin. Ни один из шести коммитов их не
адресует, и адресовать не мог.

### 5.2. `test_wide_ligature_overflow` на этой машине никогда не был красным

Бриф называл `3c91f408` починкой средового отказа `test_wide_ligature_overflow`.
Это верно **для Linux**. Локально все три теста файла зелены и до, и после:

```console
$ grep -c "test_wide_ligature_overflow.*ok" <итог>
3
```

Более того — код, который его чинит, **на этой машине не компилируется**:

```console
$ grep -l "font_freetype" <все пять логов сборки>
(пусто — ни в одном узле)
$ grep -o "DHAVE_FREETYPE[^ ]*" <лог сборки всех целей>
(пусто — фритайп выключен)
```

Значит `strikeFromOutline()`, `FT_Outline_Get_Bitmap` и граница в 4096 пикселей
**этой порцией локально не проверены ничем**. Единственное, что за них можно
сказать здесь: `FT_OUTLINE_H` уже включён (`font_freetype.cpp:30`), так что
`FT_Outline_Get_CBox`/`FT_Outline_Get_Bitmap`/`FT_BBox` объявлены и отсутствием
включения сборка не упадёт. Всё остальное — за Linux-CI. **Это самое слабое
место порции, и это ограничение среды, а не пропуск.**

---

## 6. Сверка диффов с апстримным

Метод: своя сторона против предмержевой головы, апстримная — против
**merge-base**, а не `origin/master` (приём `T5.6`).

```console
$ git merge-base 9c5eded2 0a7905ac
44d61bfc8143aaf31b81624abbee89cc3d74790e
```

| Файл | Совпадение | Отнесение расхождения |
|---|---|---|
| `lib/shitty/font_freetype.cpp` | **построчно** | — |
| `tst/pretty_binary_branding.py` | **построчно** | — |
| `tst/test_contour_input_generator.py` | **построчно** | — |
| `tst/test_ghostty_key_encoding_tail.py` | **построчно** | — |
| `tst/test_italic_overhang.py` | **построчно** | — |
| `tst/test_soft_render.py` | **построчно** | — |
| `build.py` | построчно **с точностью до отступа** | обёртка `if embed_facade_links:` задачи «А» (§2.4) |
| `lib/shitty/pty_ut.cpp` | одно расхождение | отклонённый гвард `__APPLE__` (§3) |
| `tst/pty_test_helper.c` | два расхождения | наш `report_winsize`/`wait_for_winsize` и raw-режим (`R2-test` `I11`, `G4`); снятая дублирующая `_DARWIN_C_SOURCE` (§2.3) |
| `ext/plt/platform_cocoa.mm` | нашей стороны нет | правка уже стояла, `G5` (§2.2) |
| `lib/shitty/ui_csd_tabs.mm` | нашей стороны нет | переименование уже сделано, шире, `G5` (§2.1) |

Шесть файлов из одиннадцати — ноль расхождений. Остальные пять отнесены к
названным решениям.

---

## 7. Критерии приёмки

| № | Критерий | Итог |
|---|---|---|
| 1 | `./build st --clear` зелёная, 229 узлов | ✅ **229**, `EXIT=0` |
| 2 | `unit_tests` ≥ `OK: 968`, `EXIT=0` | ✅ **968**, `EXIT=0`, список имён побайтово тот же |
| 3 | Совпадение диффов показано, расхождения отнесены | ✅ §6 |
| 4 | Питоновский набор: часть красных ушла, поимённо | ✅ **20 → 14**, ушли шесть, новых ноль — §5 |
| 5 | Пять гвардов, четыре — пробой **кодом**, `vterm_boundary` с пустым разрешением | ✅ |
| 6 | `./build example` + 37 тестов `test_embed_example.py` | ✅ 37/37, `EXIT=0`, `skipped` ноль |
| 7 | Свойство `G3` цело | ✅ §4, проверено артефактами |
| 8 | Сломанных целей две, третьей нет | ✅ `st_memprofile`, `main_fuzz` |

### Критерий 1

```console
$ ./build st --clear
[CC] {226/229} $(B)/obj/libshitty/lib/shitty/font_embedded.cpp.o
[CC] {227/229} $(B)/obj/libshitty/lib/vterm/parser.cpp.o
[AR] {228/229} $(B)/libshitty_prod.a
[LD] {229/229} $(B)/st
EXIT=0
```

229 узлов — ровно эталон задачи «А». Порция узлов не добавляет и не убирает:
`font_freetype.cpp` в графе на этой платформе нет (§5.2), а правки `build.py`
меняют зависимости embed-целей, которых в `st` нет.

### Критерий 2

```console
$ ./build unit_tests pty_test_helper
EXIT=0
$ SHITTY_PTY_TEST_HELPER="$PWD/.build/pty_test_helper" ./.build/unit_tests --threads=1 < /dev/null
+ WindowSizingRequests::TheMinimumSizeAndTheResizeUnitPairEachAxisWithItsOwnInsets
OK: 968
EXIT=0
```

Сверка поимённо с эталоном, снятым на этом дереве до мержа:

```console
$ diff <эталон: 968 имён> <итог: 968 имён>
(пусто)
$ grep -c "ResizeReachesChildAsWinch" <итог>
1
```

Ни один тест не исчез, ни один не появился, ни один не покраснел — и
`ResizeReachesChildAsWinch` на месте, чем измеряется решение §3.

`EXIT=0`, а не только строка `OK` — код возврата проверен прямо (ловушка `F6`:
регрессия даёт `EXIT=139` вообще без итоговой строки).

### Критерий 4

```console
$ sh <прогон питоновского набора>
Ran 6437 tests in 123.267s
FAILED (errors=14, skipped=19, expected failures=549)
EXIT=1
$ comm -23 <эталон 20> <итог 14>     → шесть имён, §5
$ comm -13 <эталон 20> <итог 14>     → (пусто)
$ grep -c "test_embed_example" <итог> → 37
```

Симлинк `.build/st_test` перед прогоном проверен живым — иначе набор молча
отвечает `Ran 6065 … errors=6709`.

### Критерий 5

Четыре сканирующих гварда запущены **напрямую**, минуя `./build` (кеш адресуется
содержимым и подставляет готовый штамп — `CLAUDE.md`). Программы извлечены из
`build.py` разбором `ast`, без исполнения файла:

```console
=== border_pixels_guard_program:  EXIT=0
=== mouse_geometry_guard_program: EXIT=0
=== pane_grid_guard_program:      EXIT=0
=== darwin_call_guard_program:    EXIT=0
$ python3 lib/vterm/check_includes.py lib/vterm <stamp>   ; EXIT=0
$ python3 -c "…ast.literal_eval(ALLOWANCE)…"              → {}
```

`ALLOWANCE` **пуст** — задача «А» закрыла все шесть переходов, порция ни одного
не вернула.

Пробы — **кодом**, каждая в файле, который соответствующий гвард действительно
читает (`pane_grid_guard` читает только `render*`, `mouse_geometry_guard`
пропускает `*_ut.cpp`):

| Гвард | Проба | Результат |
|---|---|---|
| `border_pixels` | `const u16 borderProbe = composer.borderPixels();` в `lib/shitty/vt_headless.cpp` | `EXIT=1`, `vt_headless.cpp:261` |
| `mouse_geometry` | `mouseGeometry(composer.geometry);` там же | `EXIT=1`, `vt_headless.cpp:261` |
| `pane_grid` | `const u16 paneGridProbe = composer.geometry.columns;` в `lib/shitty/render_reference.cpp` | `EXIT=1`, `render_reference.cpp:1163` |
| `darwin_call` | `createMetalRenderer(composer);` вне `#if __APPLE__` | `EXIT=1`, `vt_headless.cpp:261`, `createMetalRenderer` |
| `vterm_boundary` | `#include "composer.h"` в `lib/vterm/vt_grid.cpp` | `EXIT=1`, `vt_grid.cpp:10` |

Все пять после снятия проб снова `EXIT=0`, `git diff --stat` против индекса пуст.

### Критерий 6

```console
$ ./build example
[LD] {174/174} $(B)/example
EXIT=0

$ SHITTY_EMBED_EXAMPLE_BINARY=… python3 -m unittest discover -s tst -p 'test_embed_example.py' -v
Ran 37 tests in 0.789s
OK
EXIT=0
$ grep -c "skipped" <вывод>
0
```

37 тестов **исполняются и проходят**, пропусков ноль. Главный риск порции —
перестановка `libstd`/`libplt` в строках линковки embed (`69b04fe8`) — не
реализовался: обёртка задачи «А» и порядок аргументов лежат в разных местах.

### Критерий 8

```console
$ ./build -k st pt st_memprofile st_test pt_test main_fuzz st_test_prod_parser \
           pt_test_prod_parser pty_test_helper unit_tests toml_dump parser_perf \
           core_perf example                                        ; EXIT=1
FAIL $(B)/obj/st_memprofile/lib/shitty/heap_profile.cpp.o
FAIL $(B)/main_fuzz
build: 3 node(s) failed, 2 requested target(s) broken

$ grep " error: " <лог> | sort -u
lib/shitty/heap_profile.cpp:18:10: fatal error: 'gperftools/heap-profiler.h' file not found
clang++: error: linker command failed with exit code 1
```

Ровно две, те же самые. Третьей не появилось. Симлинки, снятые `-k`, после
проверки восстановлены пересборкой целей явно.

---

## Обнаружено

**1. Самый крупный коммит порции локально не компилируется, и это не видно из
цифр.** `3c91f408` — +99/−25 в `font_freetype.cpp`, единственный продуктовый код
всей порции. На darwin `optional_pkg()` выключает freetype, файла нет ни в одном
локальном узле, и все восемь критериев остались бы зелёными, даже если бы правка
не компилировалась вовсе. Любая задача, чей код лежит за `HAVE_FREETYPE`,
`HAVE_VULKAN_WAYLAND` или `HAVE_SIMDUTF`, на этой машине проверяется только
чтением. Стоит держать список таких файлов явным, а не выводить его заново каждый
раз.

**2. Апстрим гвардит тесты по платформе там, где у нас есть починка, — и это
будет повторяться.** `ResizeReachesChildAsWinch` — первый случай: апстрим не
знает про диспозицию `SIG_IGN` у SIGWINCH в XNU, а мы починили это ещё во второй
волне. Каждый следующий мерж будет пытаться вернуть `#if !defined(__APPLE__)`.
Комментарий на месте гварда — защита слабая; сильной был бы сторож, но сторожить
«тест не обёрнут в `__APPLE__`» нечем: гварды читают исходники, а не тесты.
Заявка на рассмотрение.

**3. Две из шести «ушедших» красных ушли пропуском, а не починкой.** Оба
`SoftRenderTest` теперь `skipIf(cocoa)`. Это правильное решение апстрима — `-soft`
управляет растеризатором FreeType, которого на cocoa в цепочке нет, — но в
сводке «20 → 14» это неотличимо от четырёх настоящих исправлений ожиданий.
`skipped 17 → 19` — единственное место, где разница видна.

**4. Плановая задача `T6.2` закрылась мержем целиком** (§2.2): fallback совпал
буквально, наш Accessory никем не тронут. `T6.3` закрылась разрешением конфликта
(§2.3). Обе можно снимать с доски — это четвёртый и пятый случай закономерности
волны 6 («работа уходит в мерж-шаги»).

**5. `embed`-ядро с этого мержа кодирует base64 через simdutf.** До порции
`simdutf` не значился в `deps` embed-целей, и `libshitty_vt_core` всегда шёл
портируемой веткой — на всех платформах. Свойство `G3` это не нарушает (§4.2), но
поведение embed-ядра изменилось, и ни один из 37 тестов `test_embed_example.py`
на base64 не смотрит. Стоит проверить, чем это охраняется, прежде чем кто-то
обопрётся на побайтовую воспроизводимость вывода embed-ядра.

---

## Что осталось следующим порциям

| Что | Где ждать |
|---|---|
| Конфликт «изменён/удалён» по `lib/vterm/vt_headless.{h,cpp}` | порция с `73cd2b78` — пункт 1 хвоста задачи «А»; принять апстримные файлы не думая нельзя, они попадут в embed-глоб и переопределят `VtermHeadless::create` |
| `font_optical.{h,cpp}` и опция `-optical` (`1cc50043`) | у нас отсутствуют целиком, в §7 плана не значатся; пробел, найденный `T5.8` |
| `saveLines` не меняется перезагрузкой конфига | закрывается коммитом `7e2a3c15` (бюджет скроллбэка), вне диапазона волны 6 |
| `shitty_vt_memory_usage`, `shitty_vt_set_save_lines` | приходят позже `M7`; если `T6.1` зафиксировала список экспортов по состоянию `M7`, следующая порция его сломает |
| **Восемнадцать** коммитов остатка, меняющих `lib/embed` (`M7` насчитала 24 от `44d61bfc`; шесть из них — эта порция и её соседи) | скроллбэк через фасад, alternate scroll, кодирование ввода, превью композиции, источник цвета клетки, `build tgz` |

Остаток апстрима после этой порции — **79 коммитов** (было 85).

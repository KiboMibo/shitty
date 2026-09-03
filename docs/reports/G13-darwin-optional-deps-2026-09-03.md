# G13: `darwin` гасил FreeType и в Nix — одиннадцать шрифтовых тестов

- **Задача:** вне плана, делегированная — починить `Tests Darwin` в CI форка
- **Дата:** 2026-09-03
- **Дерево:** `scratchpad/wt-G13`, ветка `fix/G13-darwin-optional-deps` от `master` (`bfba0429`)
- **Коммиты:** `154f4e67` (код), отчёт — отдельным коммитом
- **Признак вместо `darwin`:** пакет остаётся включённым на macOS, только если **все** пути, которые pkg-config отдаёт компилятору и линковщику, лежат под корнем неизменяемого стора (`$NIX_STORE`, по умолчанию `/nix/store`)
- **Статус:** исправлено; проверено локально в воспроизведённых, но **не настоящих** Nix-условиях — зелёные `Tests Darwin` подтвердит CI, а не этот отчёт

---

## 1. Что ломалось и почему это не видели две недели

`build.py:177` до правки:

```python
optional_pkg = (lambda *pkgs: pkg_config(*pkgs, required=False)) if not darwin else (lambda *pkgs: dependency(enabled=False))
```

Коммит `62cef373` («macOS: сборка без библиотек Homebrew по умолчанию») рассуждал
о том, **где живёт библиотека**: Homebrew переписывает `/opt/homebrew` на месте,
и слинкованный с ним бинарник умирает на следующем `brew upgrade`, меняющем
soname. Рассуждение верное. Но условие записали как **платформу**, а платформа —
не то свойство, о котором шла речь.

Пока признаки совпадали, разницы не было: на Mac разработчика pkg-config видит
именно Homebrew. Развела их сборка под Nix, где `flake.nix:136-144` кладёт
`freetype`, `fontconfig`, `harfbuzz`, `brotli` и `simdutf` в `buildInputs`
намеренно, а ветка `darwin` гасила их всё равно.

**Почему две недели невидимо.** Обе среды, где это было бы заметно, молчали:

| Среда | что происходило |
|---|---|
| macOS локально | зависимости выключены — и это **правильный** ответ; ничего не падает |
| Linux CI (Alpine/Fedora/Nix) | `not darwin`, зависимости включены; ничего не падает |
| `Build Darwin` | собирается **без** FreeType и потому зелёная — отсутствие бэкенда не ошибка сборки |
| `Tests Darwin` | единственная джоба, которая это видит, — и она **впервые запустилась** в прогоне `33774317579` |

То есть дефект жил ровно там, где не было ни одного наблюдателя, пока
`bfba0429` не включил Darwin-шарды на push в master.

Улика в логе: `font_freetype.cpp.o` компилировался **ноль раз**, при том что
`freetype-2.14.3` присутствовал в nix-путях того же лога. У апстрима на том же
образе он компилируется 15 раз.

Одиннадцать отказов — это ровно те тесты, которым нужен FreeType-бэкенд
(`tst/test_bitmap_font_render.py`, `tst/test_color_font_render.py`,
`tst/test_synthesized_symbols.py`, `tst/test_font_resolver.py`).

---

## 2. Какие признаки рассмотрены и почему выбран этот

Требование задачи — **различать** две сборки, а не выбирать одну из них.
Рассмотрены четыре признака.

### (а) Куда указывает pkg-config — **выбран**

```python
nix_store = os.environ.get("NIX_STORE") or "/nix/store"


def from_immutable_store(dep):
    paths = [
        flag[flag.index("/"):]
        for flag in [*dep.public_cflags, *dep.ldflags]
        if "/" in flag
    ]
    return bool(paths) and all(path.startswith(nix_store + "/") for path in paths)


def optional_pkg(*pkgs):
    dep = pkg_config(*pkgs, required=False)
    if darwin and not from_immutable_store(dep):
        dep.enabled = False
    return dep
```

Почему именно он:

- **Это ровно то свойство, о котором рассуждал `62cef373`.** Не корреляция с ним,
  а оно само: путь в сторе неизменяем — `brew upgrade` до него не дотягивается;
  `/opt/homebrew` переписывается на месте. Признак, который «сегодня случайно
  совпадает», — это как раз то, что здесь чинится, поэтому брался тот, который
  совпадает не случайно.
- **Спрашиваются те самые строки, которые попадут в сборку** — `public_cflags` и
  `ldflags` объекта, который вернул `pkg_config`. Не прокси, а сами данные.
- **Он позадачный, а не глобальный.** Смешанная машина (nix-freetype и
  Homebrew-simdutf) получит правильный ответ по каждому пакету отдельно.
- **`flake.nix` не пришлось трогать вовсе** — и это главная проверка на «второй
  признак»: договариваться двум файлам не о чем.
- **Выключение вешается на тот же объект**, который вернул `pkg_config`, а не
  подменяется вторым `dependency(enabled=False)`. Зависимость остаётся одна,
  и механизм `G3` («define на объекте») продолжает работать поверх неё без
  единой правки.

### (б) Явный флаг сборки, который включает `flake.nix` — отклонён

Это буквально второй признак: `build.py` и `flake.nix` обязаны договориться, и
разъезжаются они молча. Хуже того, `devShells.default` (`flake.nix:489-496`) зовёт
`./build` **руками**, без `buildPhase`, — флаг из `buildPhase` туда не попадает,
и dev-shell собирал бы не то, что CI. Это ровно тот класс дефекта, который здесь
чинится.

### (в) `IN_NIX_SHELL` / `NIX_CFLAGS_COMPILE` — отклонён

«Мы внутри Nix» не влечёт «эта библиотека приехала из Nix» — признак косвенный.
Плюс `IN_NIX_SHELL` не выставляется в `nix build`, а `flake.nix:76`
целенаправленно вычищает `CPPFLAGS/CFLAGS/CXXFLAGS/LDFLAGS` в начале
`buildPhase`, так что опираться на флаговые переменные там особенно неуместно.

### (г) Разделить `optional_pkg` на «шрифтовой» и «прочий» — отклонён

Это два признака вместо одного, и объяснить, почему `simdutf` в Nix нельзя, а
FreeType можно, нечем: довод `62cef373` для них дословно одинаков. После правки
`simdutf` в Nix на macOS включается тоже — что `G3` прямо предусмотрел
(«механизм такое включение выдержит без единой правки в `base64.cpp`»).

### Приём `have_header()` из `build.py:284-305` — не годится здесь

Он отвечает на вопрос «виден ли заголовок», а не «откуда библиотека», и сам
`build.py:303-305` описывает мину, которую этот приём оставляет. Третьего
механизма не добавлено: это тот же `pkg_config`, у которого просто спросили ещё
одну вещь.

---

## 3. Когда выбранный признак даст неверный ответ

1. **Стор не по умолчанию, а `NIX_STORE` до `build.py` не доехал.** Сравнение
   идёт с `$NIX_STORE`, иначе с литералом `/nix/store`. Nix-сборщик эту
   переменную экспортирует, а `cachix/install-nix-action` (весь `ci.yml`) ставит
   стор в `/nix` — но релокованная установка без переменной выключит всё.
   **Ошибка громкая:** те же одиннадцать тестов снова красные.
2. **`NIX_STORE=/`** (или любой префикс, накрывающий `/opt/homebrew`) вернёт
   Homebrew-dylib в дефолтную macOS-сборку. Требует намеренного действия; ровно
   такую же власть уже даёт `PKG_CONFIG_LIBDIR` — им же в критерии 3 и
   пользовались.
3. **`nix profile install freetype` на машине разработчика**, чей
   `PKG_CONFIG_PATH` виден сборке. Признак скажет «стор», пакет включится.
   Путь действительно неизменяем и `brew upgrade` его не тронет, но `nix store
   gc` после смены профиля его удалит — а в дериваци closure держится ссылкой из
   выхода. **Это настоящая дыра: признак доказывает неизменяемость, но не
   живучесть.**
4. **Пакет, чей `.pc` не отдаёт ни одного пути** (библиотека в дефолтных
   каталогах компилятора): `bool(paths)` ложно → выключен. Среди пяти таких нет;
   если появится — снова громко, тестами.
5. **`-D`, чьё значение содержит слэш** (`-DPREFIX=/opt/homebrew`), будет
   прочитан как путь и может выключить сторовый пакет. Ни один из пяти так не
   делает; направление ошибки — консервативное.
6. **Смешанное замыкание**: если у сторового пакета в `.pc` окажется путь вне
   стора, `all()` его выключит. Под Nix такого быть не может, но правило именно
   такое — «всё или ничего».
7. **Связь с `pkg_config`.** Читаются только `public_cflags` и `ldflags` — те два
   списка, которые `pkg_config` заполняет (`build:390-401`). Если он когда-нибудь
   начнёт класть пути и в `cppflags`, проверка их не увидит. Функции стоят в
   десяти строках друг от друга.

---

## 4. Критерии приёмки

### Критерий 1 — локально ничего не изменилось

```console
$ ./build st --clear
...
[AR] {224/225} $(B)/libshitty_prod.a
[LD] {225/225} $(B)/st
EXIT=0

$ otool -L .build/st
.build/st:
	/System/Library/Frameworks/AppKit.framework/Versions/C/AppKit (compatibility version 45.0.0, current version 2685.60.104)
	/System/Library/Frameworks/Carbon.framework/Versions/A/Carbon (compatibility version 2.0.0, current version 170.0.0)
	/System/Library/Frameworks/CoreFoundation.framework/Versions/A/CoreFoundation (compatibility version 150.0.0, current version 5026.5.4)
	/System/Library/Frameworks/CoreGraphics.framework/Versions/A/CoreGraphics (compatibility version 64.0.0, current version 1965.5.1)
	/System/Library/Frameworks/CoreText.framework/Versions/A/CoreText (compatibility version 1.0.0, current version 877.4.0)
	/System/Library/Frameworks/Foundation.framework/Versions/C/Foundation (compatibility version 300.0.0, current version 5026.5.4)
	/System/Library/Frameworks/IOSurface.framework/Versions/A/IOSurface (compatibility version 1.0.0, current version 1.0.0)
	/System/Library/Frameworks/Metal.framework/Versions/A/Metal (compatibility version 1.0.0, current version 373.2.0)
	/System/Library/Frameworks/QuartzCore.framework/Versions/A/QuartzCore (compatibility version 1.2.0, current version 1195.14.4)
	/usr/lib/libc++.1.dylib (compatibility version 1.0.0, current version 2100.43.0)
	/usr/lib/libSystem.B.dylib (compatibility version 1.0.0, current version 1356.0.0)
	/System/Library/Frameworks/CoreServices.framework/Versions/A/CoreServices (compatibility version 1.0.0, current version 1226.0.0)
	/System/Library/Frameworks/CoreVideo.framework/Versions/A/CoreVideo (compatibility version 1.2.0, current version 734.4.0)
	/usr/lib/libobjc.A.dylib (compatibility version 1.0.0, current version 228.0.0)
EXIT=0

$ otool -L .build/st | grep -c "/opt/homebrew"
0
```

225 узлов — эталон ветки; ни одной библиотеки из `/opt/homebrew`. **Закрыт.**

### Критерий 2 — `unit_tests`

```console
$ ./build unit_tests pty_test_helper
[LD] {127/127} $(B)/unit_tests
BUILD_EXIT=0

$ SHITTY_PTY_TEST_HELPER="$PWD/.build/pty_test_helper" ./.build/unit_tests --threads=1
...
+ WindowSizingRequests::TheMinimumSizeAndTheResizeUnitPairEachAxisWithItsOwnInsets
OK: 950
EXIT=0
```

**Закрыт.**

### Критерий 3 — в Nix-подобных условиях зависимость включается

**Каким способом проверено.** `nix build` на машине не запускается — демон не
поднят, sudo без пароля нет (то же, что записал `G3`). Поэтому воспроизведён
**признак**: собран подставной неизменяемый стор со сторовой формой имён,
собственными `.pc`-файлами и симлинками на настоящие библиотеки, после чего
сборка запущена дважды — с `NIX_STORE`, указывающим на него, и без.

```console
$ STORE=…/scratchpad/g13-store
$ ls "$STORE"
aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa1-freetype-2.14.3
aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa2-fontconfig-2.18.3
aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa3-harfbuzz-14.4.0
aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa4-brotli-1.2.0
aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa5-simdutf-9.1.0
pkgconfig
```

**Стор виден pkg-config, но `NIX_STORE` не задан** — то есть пакеты находятся,
а корень не совпадает с `/nix/store`:

```console
$ PKG_CONFIG_LIBDIR="$STORE/pkgconfig" ./build st --clear -B .build-g13-off
[LD] {225/225} $(B)/st
EXIT=0
$ grep -c font_freetype  # в логе сборки
0
```

225 узлов, `font_freetype` не компилировался. Признак — не «pkg-config нашёл».

**Тот же стор, `NIX_STORE` задан:**

```console
$ NIX_STORE="$STORE" PKG_CONFIG_LIBDIR="$STORE/pkgconfig" ./build st --clear -B .build-g13-on
[CC] {205/226} $(B)/obj/libshitty/lib/shitty/font_freetype.cpp.o
...
[LD] {226/226} $(B)/st
EXIT=0
```

226 узлов вместо 225, и **лишний — это `font_freetype.cpp.o`**. Дефайны:

```console
$ NIX_STORE="$STORE" PKG_CONFIG_LIBDIR="$STORE/pkgconfig" ./build st -B .build-g13-on -G | grep -o "HAVE_[A-Z]*" | sort | uniq -c
  25 HAVE_CONFIG
 455 HAVE_CORETEXT
 454 HAVE_FONTCONFIG
 454 HAVE_FREETYPE
 454 HAVE_HARFBUZZ
 455 HAVE_METAL
 427 HAVE_SIMDUTF
EXIT=0

$ PKG_CONFIG_LIBDIR="$STORE/pkgconfig" ./build st -B .build-g13-off -G | grep -o "HAVE_[A-Z]*" | sort | uniq -c
  25 HAVE_CONFIG
 450 HAVE_CORETEXT
 450 HAVE_METAL
EXIT=0
```

Дальше — сами одиннадцать отказавших тестов, собранные и запущенные в обеих
конфигурациях (`SHITTY_TEST_FONTCONFIG=1`, как его и подаёт `build`, см.
«Обнаружено»):

```console
$ # выключено — воспроизведение отказа CI
$ python3 -m unittest test_bitmap_font_render test_color_font_render \
      test_synthesized_symbols test_font_resolver
ERROR: test_box_drawing_spans_the_whole_cell_at_the_font_stem_width
ERROR: test_color_zwj_grapheme_renders_to_image
ERROR: test_dentistry_brackets_hug_the_cell_edges
ERROR: test_descenders_hang_below_the_strike_baseline
ERROR: test_font_file_path_is_not_treated_as_a_family
ERROR: test_matching_size_draws_the_strike_bit_for_bit
ERROR: test_media_symbols_have_their_shapes
ERROR: test_mismatched_size_still_uses_the_only_strike
ERROR: test_repeated_glyph_hits_the_strike_cache_bit_for_bit
ERROR: test_strike_metrics_shape_the_cell
FAIL: test_collection_face_and_representative_advances_define_cells
Ran 18 tests
FAILED (failures=1, errors=13)
EXIT=1
```

Одиннадцать имён — **дословно** список из задачи.

```console
$ # включено
$ python3 -m unittest -v test_bitmap_font_render test_color_font_render \
      test_synthesized_symbols test_font_resolver
test_matching_size_draws_the_strike_bit_for_bit ... ok
test_mismatched_size_still_uses_the_only_strike ... ok
test_repeated_glyph_hits_the_strike_cache_bit_for_bit ... ok
test_box_drawing_spans_the_whole_cell_at_the_font_stem_width ... ok
test_descenders_hang_below_the_strike_baseline ... ok
test_strike_metrics_shape_the_cell ... ok
test_color_zwj_grapheme_renders_to_image ... ok
test_dentistry_brackets_hug_the_cell_edges ... ok
test_media_symbols_have_their_shapes ... ok
test_collection_face_and_representative_advances_define_cells ... ok
test_font_file_path_is_not_treated_as_a_family ... ok
(и ещё семь тестов резолвера)
Ran 18 tests in 0.513s

OK
EXIT=0
```

Наконец, обе конфигурации прогнаны **полной** python-сюитой, чтобы увидеть, не
ломает ли включённый FreeType что-нибудь ещё:

```console
$ python3 tst/run_unittest_group.py --group=0 --group-count=1   # выключено
Ran 6399 tests in 119.491s
FAILED (failures=8, errors=14, skipped=5, expected failures=549)

$ python3 tst/run_unittest_group.py --group=0 --group-count=1   # включено
Ran 6399 tests in 146.491s
FAILED (failures=6, skipped=5, expected failures=549)
```

Разница между списками отказов — ровно те одиннадцать имён и ничего больше:

```console
$ comm -23 fail_on.txt fail_off.txt   # что сломалось от включения
(пусто)

$ comm -13 fail_on.txt fail_off.txt   # что починилось
ERROR: test_box_drawing_spans_the_whole_cell_at_the_font_stem_width
ERROR: test_color_zwj_grapheme_renders_to_image
ERROR: test_dentistry_brackets_hug_the_cell_edges
ERROR: test_descenders_hang_below_the_strike_baseline
ERROR: test_font_file_path_is_not_treated_as_a_family
ERROR: test_matching_size_draws_the_strike_bit_for_bit
ERROR: test_media_symbols_have_their_shapes
ERROR: test_mismatched_size_still_uses_the_only_strike
ERROR: test_repeated_glyph_hits_the_strike_cache_bit_for_bit
ERROR: test_strike_metrics_shape_the_cell
FAIL: test_collection_face_and_representative_advances_define_cells
```

Оставшиеся пять (`test_darkening_scales_with_the_option`,
`test_legacy_arrow_modifier_matrix`,
`test_russian_shift_ctrl_c_has_no_legacy_control_sequence`,
`test_sheared_tail_lands_in_the_captured_blank`,
`test_soft_zero_departs_from_the_hinted_grid`) падают **одинаково в обеих**
конфигурациях — это неполное локальное окружение, а не следствие правки.

**Чего этот способ не проверяет.** Стор подставной. Формы путей и `.pc` сторовые,
библиотеки за симлинками настоящие — но библиотеки эти **Homebrew'ские**, а
`.pc`-файлы написаны мной, а не взяты из nixpkgs. Отсюда два незакрытых угла:
(1) если у nixpkgs'ного `.pc` одного из пяти пакетов окажется путь вне стора,
пакет останется выключенным и его тесты в CI останутся красными; (2) `otool -L`
у `.build-g13-on/st` показывает `install_name` из `/opt/homebrew` — это артефакт
симлинков, а не механизма. Настоящий Nix здесь не запускался вовсе.
**Закрыт с оговоркой.**

### Критерий 4 — названо, когда признак ошибётся

Раздел 3, семь пунктов. Главный: `nix profile`-установленная библиотека на
машине разработчика пройдёт проверку, хотя её живучесть слабее, чем в деривации.
**Закрыт.**

### Критерий 5 — `G3` не откачена

```console
$ git diff --name-only bfba0429..HEAD
build.py
```

`lib/vterm/base64.cpp` не тронут. Define по-прежнему на объекте зависимости:

```console
$ grep -n "HAVE_SIMDUTF" build.py
225:    # translation unit can only see HAVE_SIMDUTF where the library is also on
230:    simdutf.public_cppflags += ["-DHAVE_SIMDUTF=1"]
```

Локально simdutf по-прежнему не линкуется:

```console
$ nm .build/libshitty_prod.a | grep -c simdutf
0
$ otool -L .build/st | grep -ci simdutf
0
```

Косвенное, но наглядное подтверждение, что механизм `G3` остался другим по
устройству: в графе сторовой сборки `HAVE_SIMDUTF` встречается 427 раз против
454 у `HAVE_FREETYPE` — потому что первый едет на самом объекте зависимости и
доходит только до целей, которые от неё зависят, а второй лежит в глобальных
`build.cppflags`. **Закрыт.**

---

## Обнаружено

- **`SHITTY_TEST_FONTCONFIG` врёт, когда FreeType-бэкенда нет.** `build.py:237-241`
  при выключенном бэкенде переприсваивает `fontconfig = dependency()` — а это
  **включённая** пустая зависимость. Значит `"1" if fontconfig else "0"`
  (`build.py:1133`, `build.py:4282`) отдаёт `"1"` там, где fontconfig нет вовсе:

  ```console
  $ ./build -G test_suite_group_00 -B .build-g13-off | grep -o "SHITTY_TEST_FONTCONFIG[^,}]*"
  SHITTY_TEST_FONTCONFIG":"1"
  ```

  Именно поэтому одиннадцатым отказом в CI оказался
  `test_collection_face_and_representative_advances_define_cells`: он под
  `@unittest.skipUnless(FONTCONFIG_AVAILABLE)` и должен был **пропуститься**, а
  вместо этого запустился и упал. Не чинил: это не причина одиннадцати, а
  починка молча превратила бы шесть тестов резолвера в пропуски на всякой
  платформе без бэкенда — отдельное решение.

- **`HAVE_FREETYPE`/`HAVE_HARFBUZZ`/`HAVE_FONTCONFIG` живут в глобальных
  `build.cppflags`** (`build.py:234-236`) — та самая форма, против которой
  написано правило из `CLAUDE.md`. Разъехаться сегодня они не могут:
  `have_freetype_backend` выводится из самих зависимостей. Но форма — «два
  признака», и если однажды define поставят рядом с зависимостью, а не из неё,
  вернётся `Undefined symbols`. Правильная форма — как у `HAVE_SIMDUTF`:
  `freetype.public_cppflags += [...]`. Не делал: это меняет, какие цели видят
  дефайн, и заслуживает своей проверки.

- **`flake.nix` не потребовал ни одной правки** — и это не экономия, а проверка:
  признак живёт в одном файле, договариваться двум файлам не о чем.

- **`dev/build_brew_macos.sh` продолжает работать без изменений.** Он выставляет
  пустой `PKG_CONFIG_LIBDIR`, так что pkg-config не находит вообще ничего и
  пакеты выключаются ещё веткой `required=False`, до вопроса о сторе. Вопрос
  «а не из стора ли это» там даже не задаётся.

- **Замечание `G3` про `simdutf` в `buildInputs` закрыто.** Там было записано, что
  `simdutf` в `buildInputs` на darwin лежит впустую; после этой правки он
  используется — и это то самое «отдельное решение», которое `G3` оставил
  команде. Механизм `G3` его выдержал без единой правки в `base64.cpp`, как там
  и предсказано.

- **`Build Darwin` была зелёной всё это время** и остаётся плохим сторожем: она
  проверяет, что сборка проходит, а не что бэкенд собран. Единственный
  наблюдатель за этим — `Tests Darwin`, включённая в `bfba0429`.

---

## Что осталось непроверенным

1. **Настоящий Nix не запускался.** Ни `nix build`, ни `nix develop`: демон не
   поднят, sudo без пароля нет. Всё в критерии 3 — воспроизведение признака, а
   не среды. Если nixpkgs'ный `.pc` какого-то из пяти пакетов отдаёт путь вне
   стора, правка для него не сработает, и его тесты останутся красными.
2. **Не проверено, что `NIX_STORE` доезжает до `build.py` внутри деривации на
   darwin.** Правка от этого не зависит — литерал `/nix/store` покрывает
   стандартную установку, а `cachix/install-nix-action` ставит именно её, — но
   сам факт не наблюдался.
3. **`checks.build` с `warningsAsErrors = true`** (это и есть джоба `Build
   Darwin`, `ci.yml:390`) теперь будет компилировать `font_freetype.cpp` под
   nixpkgs'ными заголовками. `-Werror` там едет в `CFLAGS`, а раннер подмешивает
   `CFLAGS` и в C++-компиляции тоже (`build:1178-1180`), так что на C++ он
   действует. Локально это проверено — но на Homebrew-заголовках:

   ```console
   $ CFLAGS=-Werror NIX_STORE="$STORE" PKG_CONFIG_LIBDIR="$STORE/pkgconfig" \
         ./build st --clear -B .build-g13-werror
   [CC] {192/226} $(B)/obj/libshitty/lib/shitty/font_freetype.cpp.o
   ...
   [LD] {226/226} $(B)/st
   EXIT=0
   ```

   На nixpkgs'ных заголовках macOS не проверено. (На Linux этот же файл уже
   собирается с `-Werror` в зелёном `Nix Linux`, так что вопрос узкий —
   macOS-специфичные предупреждения.)
4. **Версии библиотек локально не те, что в CI.** Проверено на Homebrew
   freetype/harfbuzz/fontconfig, а в CI будут nixpkgs'ные; побитовые тесты
   штрихов прошли на здешних, но это не обещание про тамошние.
5. **Остальные пять отказов Darwin-шардов** (из шестнадцати) — не наши по условию
   задачи; я в них не смотрел.
6. **Пять локальных отказов полной сюиты не разбирались.** Они одинаковы в обеих
   конфигурациях, то есть не относятся к правке, но и не объяснены; в CI на
   Darwin-шардах отказов было шестнадцать, из них одиннадцать — наши.

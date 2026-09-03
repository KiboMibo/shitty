# `G12`: чьи отказы на Darwin — и почему их не шесть, а шестнадцать

- **Задача:** `G12` — установить происхождение, а не чинить
- **Дата:** 2026-09-03
- **Ветка:** `fix/G12-darwin-font-triage`
- **Разбираемый прогон:** `KiboMibo/shitty`, run `33774317579`, `workflow_dispatch`,
  2026-09-03 15:43 UTC. **Голова прогона — `e7e4be92`, а не `393f2c4e`**: в
  постановке названа текущая голова `master`, но прогон запускался до мержа
  `R5-test`. `e7e4be92` — предок `393f2c4e`; ни один из разбираемых файлов между
  ними не менялся (проверено), так что на вердикты это не влияет.
- **Эталон сравнения:** `pg83/shitty`, run `33579959738` (push в `master`
  `1b8279f0` = наш `origin/master`, 2026-09-02 01:34 UTC, **success**), джобы
  `Tests Darwin 0/4`…`3/4` — **зелёные**
- **Статус:** все вердикты закрыты с указанием источника или фактора; **локального
  прогона не было ни одного**, см. §6

---

## 0. Три вещи, которые надо сказать до вердиктов

**Первое. Отказов не шесть, а шестнадцать.** Шесть — это то, что видно на шарде
`0/5`. На пяти шардах падают 16 различных тестовых методов в 9 классах. Разбор —
§4.

**Второе. Апстрим `Tests Darwin` гоняет, и они зелёные.** Это снимает главную
неопределённость постановки. Более того, апстрим прошёл через **ровно ту же
историю**, что и мы: 24 августа кто-то у них впервые запустил Darwin-матрицу
руками, она посыпалась, они её починили тремя коммитами и 25 августа сняли гейт
ручного запуска. Из сообщения `c049a672`:

> The Darwin test matrix only runs on manual dispatch, so several weeks of
> breakage piled up unseen

Нашего прогона `33774317579` это описание касается слово в слово, с той разницей,
что мы отстали на 85 коммитов и починку не забрали.

**Третье. Гипотеза из постановки про `3c91f408` не подтвердилась.** Отказ
`test_wide_ligature_overflow` — линуксовый; на Darwin все три его теста зелёные и у
нас, и у апстрима (глиф-бомбу FreeType обходить не приходится, потому что рисует
CoreText). Мержем апстрима лечатся другие пять отказов, а не этот, и по другой
причине.

---

## 1. Вердикты одной строкой

Два источника, ничего третьего.

| | Отказ | Вердикт | Источник / фактор |
|---|---|---|---|
| 1 | `test_bitmap_font_render.BitmapFontRenderTest.test_matching_size_draws_the_strike_bit_for_bit` | **наш** | `62cef373` |
| 2 | `test_bitmap_font_render.BitmapFontRenderTest.test_mismatched_size_still_uses_the_only_strike` | **наш** | `62cef373` |
| 3 | `test_bitmap_font_render.BitmapFontRenderTest.test_repeated_glyph_hits_the_strike_cache_bit_for_bit` | **наш** | `62cef373` |
| 4 | `test_bitmap_font_render.RealBitmapFontTest.test_box_drawing_spans_the_whole_cell_at_the_font_stem_width` | **наш** | `62cef373` |
| 5 | `test_bitmap_font_render.RealBitmapFontTest.test_descenders_hang_below_the_strike_baseline` | **наш** | `62cef373` |
| 6 | `test_bitmap_font_render.RealBitmapFontTest.test_strike_metrics_shape_the_cell` | **наш** | `62cef373` |
| 7 | `test_color_font_render.ColorFontRenderTest.test_color_zwj_grapheme_renders_to_image` | **наш** | `62cef373` |
| 8 | `test_font_resolver.FontResolverTest.test_font_file_path_is_not_treated_as_a_family` | **наш** | `62cef373` |
| 9 | `test_font_resolver.FontResolverTest.test_collection_face_and_representative_advances_define_cells` | **наш** | `62cef373` |
| 10 | `test_synthesized_symbols.SynthesizedSymbolTest.test_dentistry_brackets_hug_the_cell_edges` | **наш** | `62cef373` |
| 11 | `test_synthesized_symbols.SynthesizedSymbolTest.test_media_symbols_have_their_shapes` | **наш** | `62cef373` |
| 12 | `test_soft_render.SoftRenderTest.test_soft_zero_departs_from_the_hinted_grid` | **унаследованный** | нет `c049a672` |
| 13 | `test_soft_render.SoftRenderTest.test_darkening_scales_with_the_option` | **унаследованный** | нет `c049a672` |
| 14 | `test_contour_input_generator.ContourInputGeneratorTest.test_legacy_arrow_modifier_matrix` | **унаследованный** | нет `c049a672` |
| 15 | `test_ghostty_key_encoding_tail.GhosttyKeyEncodingTailTest.test_russian_shift_ctrl_c_has_no_legacy_control_sequence` | **унаследованный** | нет `ee9a576a` |
| 16 | `test_italic_overhang.ItalicOverhangTest.test_sheared_tail_lands_in_the_captured_blank` | **унаследованный** | нет `ee9a576a`, `18fc9a20` |

Шесть отказов из постановки — это строки 4, 5, 6, 7, 16, 12 (в таком порядке).
Из них пять — наши, один — унаследованный.

**Оговорка к слову «унаследованный».** По букве постановки унаследованный — это
«есть и на чистом `origin/master`». На **текущем** `origin/master` эти пять
зелёные: апстрим их починил. Но починил **после** нашей точки расхождения, и
чинил он ровно ту же поломку, которую видим мы. Мы в этих пяти не внесли ничего:
три из четырёх файлов не трогались у нас с `afc001c1` («Rename tests/ to tst/»),
четвёртый — с `f042000f`. Поэтому «наш» здесь был бы неправдой, и вердикт стоит
читать как **«унаследованный от точки расхождения; апстрим уже исправил»** — тот
же класс, что `test_wide_ligature_overflow` в `G6`. Средовым он тоже не является:
поломка в коде тестов, а не в окружении.

**Средовых вердиктов нет ни одного.** Окружение прогона у нас и у апстрима
совпадает — это измерено, а не предположено, см. §2.2.

---

## 2. Отказы 1–11: FreeType на Darwin выключен нашим коммитом

### 2.1. Вердикт

**Наш.** Источник — `62cef373` («macOS: сборка без библиотек Homebrew по
умолчанию», Nikolay Pyankov, 2026-08-25), `build.py`.

Коммит выключает пять опциональных зависимостей **на любой сборке darwin**:

```python
optional_pkg = (lambda *pkgs: pkg_config(*pkgs, required=False)) if not darwin else (lambda *pkgs: dependency(enabled=False))

freetype = optional_pkg("freetype2")
fontconfig = optional_pkg("fontconfig")
harfbuzz = optional_pkg("harfbuzz")
```

У апстрима на том же месте:

```python
freetype = pkg_config("freetype2", required=False)
fontconfig = pkg_config("fontconfig", required=False)
harfbuzz = pkg_config("harfbuzz", required=False)
```

Замысел коммита — не линковать дилибы Homebrew, которые умирают на следующем
`brew upgrade`. Замысел верный. Но условие повешено на `darwin`, а не на
«библиотека пришла из Homebrew», и поэтому оно гасит FreeType **и в Nix-сборке
CI**, где библиотеки приходят из flake и никакого Homebrew нет. Это тот самый
класс, который `CLAUDE.md` описывает в разделе «Два признака одной зависимости»:
признак «нам не нужен Homebrew» подменён признаком «мы на macOS».

Одиннадцать отказов — прямое следствие. Все они грузят конкретный файл шрифта
(`fixture-8x8.bdf`, `spleen-8x16.bdf`, `cozette.bdf`, `.ttc`-коллекция, цветной
эмодзи-шрифт) или резолвят семейство через fontconfig. Без FreeType и fontconfig
`Fontpack::create` до них не добирается, `FONT_LOAD`/`RENDER_IMAGE` не отвечают
`OK`, и харнесс поднимает `RuntimeError: invalid font load response` /
`invalid render image response` (`tst/harness.py:340` и ниже).

Один из одиннадцати выглядит иначе и стоит отдельной строки:
`test_collection_face_and_representative_advances_define_cells` даёт не ошибку, а
`AssertionError: 10 != 8`. Тест кладёт `.ttc` во временный каталог, подсовывает
свой `FONTCONFIG_FILE` и ждёт ширину ячейки 8 от фикстуры. Без fontconfig
подстановка не работает, разрешается системный шрифт, ячейка выходит 10.

### 2.2. Чем подтверждено

**(1) Окружение прогона у нас и у апстрима одно и то же.** В логах Darwin-джобов
обоих репозиториев ровно те же версии из nix store:

```
$ grep -ohE '(freetype|fontconfig|harfbuzz)-[0-9][0-9.]*' up-*.log | sort | uniq -c
  36 fontconfig-2.18.1
  16 freetype-2.14.3
  16 harfbuzz-13.2.1
$ grep -ohE '(freetype|fontconfig|harfbuzz)-[0-9][0-9.]*' job-*.log | sort | uniq -c
  45 fontconfig-2.18.1
  20 freetype-2.14.3
  20 harfbuzz-13.2.1
```

То есть **FreeType в нашей среде есть**. Средовым отказ быть не может.

**(2) `flake.nix` подаёт эти библиотеки одинаково.** `buildInputs` в нашем
`flake.nix` (строки 136–145) и в апстримном совпадают дословно: `brotli`,
`fontconfig`, `freetype`, `harfbuzz`, `simdutf` — без всякого
`lib.optionals … isLinux`. Библиотеки предоставлены обеим сборкам; отличается
только то, спрашивает ли их `build.py`.

**(3) Прямая улика — что компилируется.** Счёт вхождений объектных файлов в логах
Darwin-джобов:

| Единица трансляции | у нас | у апстрима |
|---|---|---|
| `font_freetype.cpp.o` | **0** | 15 |
| `font_coretext.cpp.o` | 18 | 15 |
| `font_fontconfig.cpp.o` | 18 | 15 |

`font_freetype.cpp` в нашей Darwin-сборке **не компилируется ни разу**.
`font_fontconfig.cpp` компилируется, но внутри он под `HAVE_FONTCONFIG`, который
`build.py` выставляет только при живой зависимости.

**(4) Тесты объявляют свою зависимость сами.** Докстринг
`tst/test_bitmap_font_render.py`:

> A non-scalable bitmap strike **through the FreeType backend**: the strike's own
> ascender places the baseline…

**(5) Все восемь файлов этих тестов совпадают с апстримом побайтово.**

```
$ git diff --numstat origin/master HEAD -- tst/test_bitmap_font_render.py \
      tst/test_color_font_render.py tst/test_font_resolver.py tst/test_synthesized_symbols.py
(пусто)
```

`tst/harness.py` расходится (141 строка добавлена), но `load_font` и
`render_image` в нём совпадают с апстримными дословно — расхождения в другом
месте файла (наши `chord_*` и прочее).

Итого: одинаковая среда, одинаковые тесты, одинаковый харнесс в нужной части,
разный результат. Различие — в нашем `build.py`.

**(6) Апстрим эти же одиннадцать тестов на том же джобе проходит.** Из логов
`Tests Darwin 0/4`…`3/4` прогона `33579959738`: каждый из шестнадцати наших
падающих методов там присутствует и оканчивается `... ok` (кроме двух, которые
апстрим осознанно пропускает, — см. §3). Ни `skip`, ни `expected failure`.

### 2.3. Побочное следствие, которое стоит знать

`build.py` прокидывает в питоновский набор
`SHITTY_TEST_FONTCONFIG = "1" if fontconfig else "0"`. На нашей Darwin-сборке это
`0`, поэтому тесты, помеченные
`@unittest.skipUnless(FONTCONFIG_AVAILABLE, "Fontconfig is not available")`,
честно пропускаются. Оба падающих теста `FontResolverTest` этой пометки не несут
— у апстрима она им и не нужна, там fontconfig собран. То есть механизм
самоотключения в репозитории есть, но он рассчитан на конфигурацию, которой у
апстрима не бывает, и покрывает не всё.

### 2.4. Рекомендация

**Чинить нам.** Сужать условие в `62cef373`, а не откатывать его: замысел коммита
(не тащить дилибы Homebrew в дев-сборку) остаётся верным.

Порядок, который я бы предложил:

1. Развесить признак по источнику библиотеки, а не по платформе. Дешёвый и
   точный вариант — оставить `pkg_config(..., required=False)` как у апстрима, а
   дев-сборке на маке отдать пустой `PKG_CONFIG_LIBDIR`, как это уже делает
   `dev/build_brew_macos.sh`. Тогда «на маке без Homebrew» и «в Nix с флейком»
   различаются сами собой, и состояние «библиотека есть, но мы её не спросили»
   становится невыразимым.
2. Если признак всё же остаётся в `build.py` — вешать его на присутствие
   Homebrew-префикса в выводе `pkg-config --variable=prefix`, а не на `darwin`.
3. Помечать эти одиннадцать как ожидаемые отказы **не следует**. Они ловят
   реальную деградацию: продуктовая сборка на macOS теряет возможность загрузить
   произвольный файл шрифта, включая битмапные и цветные. Если решение «на macOS
   FreeType не нужен» принимается сознательно, то честный ход — не глушить тесты,
   а поставить им платформенный гвард по образцу апстримного
   `TEST_PLATFORM == "cocoa"` и записать это решение как продуктовое.

Ждать апстрима здесь смысла нет: у апстрима это место не сломано, и наш вариант
`build.py` ему неизвестен.

---

## 3. Отказы 12–16: платформенные гварды, которых мы не забрали

### 3.1. Вердикт

**Унаследованный от точки расхождения; апстрим уже исправил.** Мы не удаляли
ничего — апстрим **добавил** платформенные гварды в трёх коммитах, которых у нас
нет:

| Коммит | Дата | Что закрывает |
|---|---|---|
| `c049a672` «Repair the Darwin test shards» | 2026-08-24 21:40 | отказы 12, 13, 14 |
| `ee9a576a` «Make the Darwin shards honest about platform semantics» | 2026-08-24 22:43 | отказы 15, 16 |
| `18fc9a20` «Unpark the flood test from its Linux calibration…» | 2026-08-24 22:53 | уточняет 16 |

```
$ for c in c049a672 ee9a576a 18fc9a20; do git merge-base --is-ancestor $c master && echo YES || echo NO; done
NO
NO
NO
```

### 3.2. Чем подтверждено

**(1) История файлов показывает направление правки.** Наши версии стоят на
состоянии *до* гвардов, апстримные — после:

| Файл | последний коммит у нас | у апстрима |
|---|---|---|
| `tst/test_soft_render.py` | `9b29c84a` | `c049a672` |
| `tst/test_contour_input_generator.py` | `afc001c1` | `c049a672` |
| `tst/test_ghostty_key_encoding_tail.py` | `afc001c1` | `ee9a576a` |
| `tst/test_italic_overhang.py` | `f042000f` | `18fc9a20` |

`afc001c1` — это «Rename tests/ to tst/», то есть два файла у нас не менялись со
времён переименования каталога. Внести регрессию мы в них не могли.

**(2) Сами гварды — ровно про наблюдаемые отказы.** Дифф
`origin/master..HEAD` показывает, чего нам недостаёт:

- `test_soft_render.py`: у апстрима на классе висит
  `@unittest.skipIf(TEST_PLATFORM == "cocoa", "CoreText renders here; -soft is a FreeType knob")`.
  В апстримном логе так и написано:
  `test_soft_zero_departs_from_the_hinted_grid ... skipped 'CoreText renders here; -soft is a FreeType knob'`.
  У нас гварда нет, тесты исполняются и падают
  (`AssertionError: 60412 not greater than 60412` — тьма не поменялась, потому что
  `-soft` рулит рантаймом, которого в сборке нет).
- `test_contour_input_generator.py`: апстрим пропускает `Cmd+Left/Right`,
  потому что на macOS это зарезервированный чорд перелистывания вкладок (issue 82)
  и до pty он не доходит. Наш отказ — буквально это:
  `AssertionError: b'' != b'\x1b[1;9C'` при `key=262, modifiers=8` (`modifiers=8` = Super).
- `test_ghostty_key_encoding_tail.py`: апстрим ждёт
  `expected = b"\x03" if TEST_PLATFORM == "cocoa" else b""`, потому что на Linux
  тест проходит лишь оттого, что `Ctrl+Shift+C` занят копированием и до кодировщика
  не доходит. Наш отказ — `AssertionError: b'\x03' != b''`, то есть мы получили
  ровно тот байт, который апстрим и объявил правильным для macOS.
- `test_italic_overhang.py`: апстрим проверяет ширину сдвига по колонкам только
  на Linux (`if TEST_PLATFORM != "cocoa"`), потому что CoreText наклоняет дальше
  FreeType, и оставляет проверку того, что следующая ячейка осталась пустой. Наш
  отказ — `AssertionError: False is not true : [248, 255, 248, 107, 0, …]`: чернила
  в третьей колонке, ровно «CoreText наклоняет дальше».

**(3) Механизм гварда у нас рабочий.** `TEST_PLATFORM` в `tst/harness.py` есть и
совпадает с апстримным, `build.py` выставляет `SHITTY_TEST_PLATFORM=cocoa` на
darwin. Не хватает только самих проверок в этих четырёх файлах — то есть правка
переносится один в один, ничего адаптировать не придётся.

**(4) Апстрим сам описал эту поломку, и его список совпадает с нашим.** Из
`ee9a576a`:

> The dispatched Darwin matrix surfaced four platform gaps … The ghostty
> control-shift scenario passes on Linux only because Ctrl+Shift+C is the Copy
> binding … The italic-overhang capture asserted FreeType's shear width column by
> column; CoreText shears farther.

### 3.3. Рекомендация

**Ждать мержа апстрима.** Три коммита входят в те 85, что уже стоят в очереди на
слияние; отдельной работы задача не требует и сводится к строке плана.

Если волна слияния далеко и хочется зелёный Darwin раньше — cherry-pick
`c049a672`, `ee9a576a`, `18fc9a20` в порядке дат. **Одна ловушка:** `c049a672`
не только вешает `skipIf`, но и разрезает `SoftRenderTest` на два класса, вынося
валидацию опции `-soft` в отдельный `SoftOptionTest`, который продолжает
исполняться везде. У нас эти тесты пока слиты в один класс. Перенести только
строку `skipIf`, не перенеся разрез, — значит заодно погасить на macOS и проверки
`-soft 101 / -2 / x`, которые от рендерера не зависят и падать не должны.

Помечать как ожидаемые отказы не следует ни один из пяти: у апстрима на
исправленном дереве они зелёные, и правка уже написана.

---

## 4. Сколько всего отказов и чем шарды отличаются

### 4.1. Устройство прогона

Питоновский набор режется на **20 групп** по ~320 тестов (`build.py:988`,
`test_group_count = 20`), суммарно 6398 тестов. Групповых целей вдвое больше:
`make_python_test_groups` вызывается дважды (`build.py:1113` и `:1122`) — обычный
набор и `prod_parser`, — то есть **40 целей**.

Эти 40 целей раскладываются по пяти шардам не поровну, а хешем имени цели
(`build.py:64–66`):

```python
digest = hashlib.sha256(test_id.encode()).digest()
if int.from_bytes(digest[:8], "big") % group_count != group_index:
    continue
```

Число шардов задаёт `flake.nix:542`, `darwinTestGroupCount = 5` (у апстрима —
четыре, коммит `9d031337`, которого у нас нет).

### 4.2. Что из этого следует

**Шарды перекрываются по номерам групп.** Обе цели одной группы — `..._group_06`
обычная и `prod_parser` — хешируются независимо и попадают в разные шарды, при
этом гоняют одни и те же 320 тестов. Отсюда картина, которая с первого взгляда
выглядит миганием: один и тот же тест падает в двух шардах. На деле наоборот —
это доказательство **детерминированности**: где группа исполнилась дважды, оба
раза дали один и тот же список отказов, с точностью до сообщения.

Распределение номеров групп по шардам:

| Шард | группы | различных групп |
|---|---|---|
| `0/5` | 0, 6, 7, 8, 18 | 5 |
| `1/5` | 1, 3, 5, 15, 16, 17, 19 | 7 |
| `2/5` | 4, 5, 8, 9, 11, 12, 16, 18, 19 | 9 |
| `3/5` | 2, 3, 4, 7, 9, 11, 13, 14 | 8 |
| `4/5` | 1, 2, 6, 10, 13, 17 | 6 |

Объединение — полные `0..19`, ни одна группа не потеряна. Перекос 5 против 9 —
следствие хеш-раскладки, а не разной нагрузки.

**Шарды не отличаются ни средой, ни сборкой, ни набором.** Все пять — один и тот
же `mkTestCheck` на `aarch64-darwin` с одним и тем же деривейшном; отличается
только подмножество целей. Поэтому «проверить остальные четыре шарда» означало не
«поискать другие причины», а «собрать полный список».

### 4.3. Полный список: 16 методов

| # | Тест | Тип | Шарды | Сообщение |
|---|---|---|---|---|
| 1 | `BitmapFontRenderTest.test_matching_size_draws_the_strike_bit_for_bit` | ERROR | 1, 3 | `invalid font load response` |
| 2 | `BitmapFontRenderTest.test_mismatched_size_still_uses_the_only_strike` | ERROR | 2, 3 | `invalid font load response` |
| 3 | `BitmapFontRenderTest.test_repeated_glyph_hits_the_strike_cache_bit_for_bit` | ERROR | 1, 2 | `invalid font load response` |
| 4 | `RealBitmapFontTest.test_box_drawing_spans_the_whole_cell_at_the_font_stem_width` | ERROR | 0, 4 | `invalid font load response`, ×2 шрифта |
| 5 | `RealBitmapFontTest.test_descenders_hang_below_the_strike_baseline` | ERROR | 0, 3 | `invalid font load response`, ×2 шрифта |
| 6 | `RealBitmapFontTest.test_strike_metrics_shape_the_cell` | ERROR | 0, 2 | `invalid font load response`, ×2 шрифта |
| 7 | `ColorFontRenderTest.test_color_zwj_grapheme_renders_to_image` | ERROR | 0, 2 | `invalid render image response` |
| 8 | `FontResolverTest.test_font_file_path_is_not_treated_as_a_family` | ERROR | 1, 2, 4 | `invalid font load response` |
| 9 | `FontResolverTest.test_collection_face_and_representative_advances_define_cells` | FAIL | 1, 3 | `10 != 8` |
| 10 | `SynthesizedSymbolTest.test_dentistry_brackets_hug_the_cell_edges` | ERROR | 2, 3 | `invalid font load response` |
| 11 | `SynthesizedSymbolTest.test_media_symbols_have_their_shapes` | ERROR | 2 | `invalid font load response` |
| 12 | `SoftRenderTest.test_soft_zero_departs_from_the_hinted_grid` | FAIL | 0, 3 | картинка не изменилась |
| 13 | `SoftRenderTest.test_darkening_scales_with_the_option` | FAIL | 1, 2 | `60412 not greater than 60412` |
| 14 | `ContourInputGeneratorTest.test_legacy_arrow_modifier_matrix` | FAIL | 3, 4 | `b'' != b'\x1b[1;9C'` / `…9D`, Super+←/→ |
| 15 | `GhosttyKeyEncodingTailTest.test_russian_shift_ctrl_c_has_no_legacy_control_sequence` | FAIL | 4 | `b'\x03' != b''` |
| 16 | `ItalicOverhangTest.test_sheared_tail_lands_in_the_captured_blank` | FAIL | 0, 3 | `[248, 255, 248, 107, 0, …]` |

Строка 11 значится в одном шарде, а не в двух, потому что обе цели её группы
попали в шард `2/5`.

### 4.4. Почему локальный прогон давал тринадцать

Отмеченные командиром «тринадцать из тринадцати» в четырёх файлах — это те же
самые отказы: локально на macOS нет ни Homebrew-библиотек, ни Nix, поэтому к
одиннадцати «нашим» добавляются ещё и те, что в Nix проходят. Локальная среда
беднее CI-шной, и вердикт на ней не строится — постановка тут была права.

---

## 5. Отдельно: гейт, из-за которого это копилось

Наш `.github/workflows/ci.yml` держит все пять Darwin-джобов под
`if: ${{ github.event_name == 'workflow_dispatch' && inputs.darwin_tests }}` при
`default: false`. Апстрим гейт снял 25 августа коммитом `2eb6848e`:

> The dispatch gate hid weeks of Darwin breakage until someone flipped the input
> by hand; the shards run unconditionally now, and the `darwin_tests` input goes
> away with nothing left to read it.

Пока гейт стоит, шестнадцать отказов будут копиться дальше ровно тем же способом,
каким накопились эти. `2eb6848e` входит в те же 85 коммитов; отдельной работы не
требует, но при слиянии его стоит не потерять — в отличие от остальных, он
меняет `.github/workflows/ci.yml`, где у нас пять шардов против апстримных
четырёх, и конфликт там вероятен.

---

## 6. Что осталось непроверенным, и почему

Пишу отдельно, потому что разница между «измерено» и «вычитано» здесь
существенная.

**Ни одного прогона я не сделал.** Все вердикты получены из (а) логов CI обоих
репозиториев и (б) чтения кода и диффов. Ни `nix`, ни `docker` не трогал вовсе:
постановка предупредила, что демон `nix` не поднят и sudo без пароля нет, а Darwin
в контейнере не воспроизводится в принципе. Тратить на это время не стал.

Что это значит для надёжности вердиктов:

**Надёжно, потому что измерено (пусть и не мной):**

- присутствие FreeType/fontconfig/harfbuzz в обеих средах — из nix-путей в логах;
- отсутствие `font_freetype.cpp.o` в нашей Darwin-сборке при его наличии в
  апстримной — из логов сборки, счёт вхождений;
- исполнение и зелёный результат всех шестнадцати методов у апстрима на том же
  джобе — из логов апстримного прогона `33579959738`;
- две апстримные пропущенные проверки с текстом причины — оттуда же;
- детерминированность: одиннадцать из шестнадцати методов исполнились в двух
  шардах каждый и оба раза дали одинаковый результат;
- полнота списка: объединение групп по пяти шардам покрывает `0..19`.

**Надёжно, потому что следует из диффа и истории:**

- побайтовое совпадение восьми файлов тестов с апстримом;
- отсутствие трёх апстримных коммитов в нашей истории;
- авторство и содержание `62cef373`.

**Установлено чтением, экспериментом не подтверждено:**

1. **Причинная связь «нет FreeType → `invalid font load response`» не
   воспроизведена.** Она сходится со всем наблюдаемым — все одиннадцать отказов
   грузят файл шрифта или резолвят через fontconfig, ни один другой тест набора
   так не делает, тесты сами объявляют зависимость от FreeType в докстринге, а
   апстрим с собранным FreeType их проходит, — но собранной бинарки без FreeType
   я в руках не держал. Уверенность высокая; строгого доказательства нет.
2. **Не проверено, что после мержа 85 коммитов Darwin станет зелёным.** Пять
   унаследованных отказов правка апстрима закрывает адресно, это видно построчно.
   Но одиннадцать наших мерж **не закроет**: `62cef373` — наш коммит, его в
   апстриме нет и конфликтовать с ним нечему, `build.py` после слияния сохранит
   выключенные зависимости. То есть зелёный Darwin требует обеих работ, а не
   одной.
3. **Не проверено, что список из шестнадцати полон для `393f2c4e`.** Прогон был на
   `e7e4be92`. Разбираемые файлы между `e7e4be92` и `393f2c4e` не менялись, но
   `R5-test` мог добавить новые тесты, которые на Darwin ещё никто не гонял.
4. **Не искал причин в других джобах.** `Tests UBSan`, `Tests ASan`, `Coverage`,
   `Tests Fedora`, `Tests Alpine` в том же прогоне тоже красные; это предмет
   `G6`, `G5` и соседних задач, и сюда я их не тянул.
5. **Не проверял, что было бы у апстрима с нашим `build.py`.** Для вердикта это не
   нужно — апстрим зелёный со своим, — но строго говоря это делает вердикт
   «наш» выводом от противного, а не результатом скрещивания деревьев.

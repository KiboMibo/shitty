Находок нет

# T0: эталон сборки и тестов upstream-stability-sync

**Дата:** 2026-11-19
**Задача:** `T0` плана `docs/plans/2026-11-19-upstream-stability-sync.md`
**Проверяемый код:** `fork/master` `0ac846d530cd76cada88f2eebf870238658e5879`
**Рабочая ветка:** `qa/T0-upstream-stability-baseline` от `wave/upstream-stability-sync-w1` `3e77835555d719a86afd109d6ea4d7c5f910ef04`
**Вердикт:** T0 выполнен; эталон пригоден для поимённого сравнения. Сам baseline **не зелёный**.

## 1. Область и неизменность базы

Ветка wave отличается от `0ac846d5` только двумя документами плана:

```text
docs/plans/2026-11-19-upstream-stability-sync.md
docs/plans/state/upstream-stability-sync-status.md
```

Проверка `git diff --quiet 0ac846d5 HEAD -- . ':!docs'` вернула `0`: production code, тесты и `build.py` совпадают с базой. Отчёт относится к коду `0ac846d5`; более поздний SHA ветки относится только к плану и этому отчёту.

Production code и тесты не изменялись, мутации не делались, унаследованные отказы не исправлялись. Перед первой сборкой `.build` отсутствовал, поэтому первый прогон не использовал чужие локальные штампы как доказательство выполнения. Повтор `./build test` для подтверждения кода возврата отдельно помечен ниже и не заменяет первый прогон.

## 2. Окружение

- macOS `27.2.1`, Darwin `27.2.0`, arm64;
- Apple clang `21.0.0 (clang-2100.4.1.1)`, target `arm64-apple-darwin27.2.0`;
- Python `3.12.5`;
- сборка и прогон сделаны одним заходом 2026-11-19, версия не пересекла смену суток.

Фактические версии production binaries:

```text
$ ./st -version
Shitty 2026.11.19
Copyright (C) 2026 Shitty team
# rc=0; stderr: shitty: /Users/kibomibo/.config/shitty/shitty.toml: unknown option: sidebarGlassTone

$ ./pt -version
Pretty 2026.11.19
Copyright (C) 2026 Pretty team
# rc=0; stderr пуст
```

`build.py` задаёт `shitty_version = date.today().strftime("%Y.%m.%d")`, поэтому `SHITTY_TEST_VERSION=2026.11.19` совпадает с версией, встроенной в binaries.

Окружение, которое выставляет штатный runner по `build.py`:

| Набор | Переменные |
|---|---|
| unit shards и ручной unit run | `SHITTY_PTY_TEST_HELPER=$(B)/pty_test_helper` |
| Python default | `SHITTY_TEST_BINARY=$(B)/st_test`, `SHITTY_PRETTY_TEST_BINARY=$(B)/pt_test` |
| Python prod-parser | `SHITTY_TEST_BINARY=$(B)/st_test_prod_parser`, `SHITTY_PRETTY_TEST_BINARY=$(B)/pt_test_prod_parser` |
| оба Python-набора | `SHITTY_EMBED_EXAMPLE_BINARY=$(B)/example`, `SHITTY_TOML_DUMP_BINARY=$(B)/toml_dump`, `SHITTY_TEST_FONTCONFIG=0`, `SHITTY_TEST_PLATFORM=cocoa`, `SHITTY_TEST_VERSION=2026.11.19` |
| `production_surface` | `SHITTY_PRODUCTION_BINARY=$(B)/st`, `SHITTY_PRETTY_BINARY=$(B)/pt`, `SHITTY_TEST_VERSION=2026.11.19` |

Для Python runner задаёт `cwd=$(S)` и изолированный `TMPDIR` внутри build root. `SHITTY_TEST_FONTCONFIG=0` соответствует фактически выключенной optional dependency на этом darwin-хосте; `pkg-config` для её переопределения не использовался.

## 3. Команды и итоги

| Команда | rc | Итог |
|---|---:|---|
| `./build st pt --clear` | `0` | оба production brand собраны; узлы `255/255` завершены |
| `./build unit_tests pty_test_helper` | `0` | unit binary и helper собраны; узлы `162/162` завершены |
| `SHITTY_PTY_TEST_HELPER="$PWD/.build/pty_test_helper" .build/unit_tests < /dev/null` | `0` | первая попытка, итоговая строка `OK: 1115`; `148`/`139` не было |
| `./build test` | `1` | граф остановлен на `production_surface`: `Ran 6 tests`, `FAILED (failures=2)`; см. раздел 6 |
| `./build -k test_suite test_suite_prod_parser` | `1` | все 40 Python shards получили итог; 19 красных shards, 21 успешный |
| диагностический `HOME=/tmp/upstream-stability-sync-T0-empty-home SHITTY_PRODUCTION_BINARY="$PWD/.build/st" SHITTY_PRETTY_BINARY="$PWD/.build/pt" SHITTY_TEST_VERSION=2026.11.19 python3 tst/production_surface.py` | `0` | `Ran 6 tests`, `OK`; подтверждена причина двух host-local отказов |

Первый `./build test` действительно исполнялся на свежем `.build`. Повтор только для фиксации rc дал тот же `1` и те же два отказа `production_surface`. Полный общий граф `test` **не дошёл до конца**; для Python-эталона поэтому использован штатный runner с `-k`, чтобы красные shards не мешали остальным завершиться. Последующий результат должен сравниваться тем же режимом: `./build -k test_suite test_suite_prod_parser`.

## 4. Unit baseline

Команда для повторения:

```bash
./build unit_tests pty_test_helper
SHITTY_PTY_TEST_HELPER="$PWD/.build/pty_test_helper" .build/unit_tests < /dev/null
```

Результат ручного полного прогона: `EXIT=0`, `OK: 1115`. Итоговая строка присутствует; перезапуск не потребовался. Дополнительно unit shards, входящие в зависимость `test_suite`, завершились внутри команды Python baseline; красные shards этой команды относятся к Python, не к unit tests.

## 5. Python baseline: числа и правило сравнения

Каждый из двух режимов обнаружил `6646` tests, всего по 40 shards выполнено `13292` test invocations:

| Метрика | Значение |
|---|---:|
| shards | 40 (20 default + 20 prod-parser) |
| успешные shards | 21 |
| красные shards | 19 |
| ERROR instances | 30 |
| FAIL instances | 1 |
| уникальные красные test IDs, включая параметры subTest | **16** |
| skipped instances | 58 |
| уникальные skipped test IDs с причиной | **26** |
| expected-failure instances | 992 |
| unexpected successes | 0 |

Числа default и prod-parser вместе складывать с числами одиночного прогона нельзя. Для последующей волны критерий — **поимённое равенство красных**, а не нулевой rc и не сравнение только количества. Параметры subTest являются частью ключа сравнения. Shard номера ниже даны для навигации, но не заменяют test ID: `test_font_file_path_is_not_treated_as_a_family` обнаруживается более чем в одном shard.

### 5.1. Поимённый список унаследованных красных

`occurrences` — число экземпляров в двух 20-shard прогонах вместе.

1. `ERROR`, groups `2,19`, occurrences `4`:
   `test_font_file_path_is_not_treated_as_a_family (test_font_resolver.FontResolverTest.test_font_file_path_is_not_treated_as_a_family)`
2. `ERROR`, group `8`, occurrences `2`:
   `test_strike_metrics_shape_the_cell (test_bitmap_font_render.RealBitmapFontTest.test_strike_metrics_shape_the_cell) (font='spleen-8x16.bdf')`
3. `ERROR`, group `8`, occurrences `2`:
   `test_strike_metrics_shape_the_cell (test_bitmap_font_render.RealBitmapFontTest.test_strike_metrics_shape_the_cell) (font='cozette.bdf')`
4. `ERROR`, group `7`, occurrences `2`:
   `test_descenders_hang_below_the_strike_baseline (test_bitmap_font_render.RealBitmapFontTest.test_descenders_hang_below_the_strike_baseline) (font='spleen-8x16.bdf')`
5. `ERROR`, group `7`, occurrences `2`:
   `test_descenders_hang_below_the_strike_baseline (test_bitmap_font_render.RealBitmapFontTest.test_descenders_hang_below_the_strike_baseline) (font='cozette.bdf')`
6. `ERROR`, group `5`, occurrences `2`:
   `test_repeated_glyph_hits_the_strike_cache_bit_for_bit (test_bitmap_font_render.BitmapFontRenderTest.test_repeated_glyph_hits_the_strike_cache_bit_for_bit)`
7. `ERROR`, group `2`, occurrences `2`:
   `test_color_zwj_grapheme_renders_to_image (test_color_font_render.ColorFontRenderTest.test_color_zwj_grapheme_renders_to_image)`
8. `ERROR`, group `1`, occurrences `1`:
   `test_pretty_child_gets_pretty_version (test_startup.StartupTest.test_pretty_child_gets_pretty_version)`
9. `ERROR`, group `4`, occurrences `2`:
   `test_mismatched_size_still_uses_the_only_strike (test_bitmap_font_render.BitmapFontRenderTest.test_mismatched_size_still_uses_the_only_strike)`
10. `ERROR`, group `4`, occurrences `2`:
    `test_media_symbols_have_their_shapes (test_synthesized_symbols.SynthesizedSymbolTest.test_media_symbols_have_their_shapes)`
11. `ERROR`, group `6`, occurrences `2`:
    `test_box_drawing_spans_the_whole_cell_at_the_font_stem_width (test_bitmap_font_render.RealBitmapFontTest.test_box_drawing_spans_the_whole_cell_at_the_font_stem_width) (font='spleen-8x16.bdf')`
12. `ERROR`, group `6`, occurrences `2`:
    `test_box_drawing_spans_the_whole_cell_at_the_font_stem_width (test_bitmap_font_render.RealBitmapFontTest.test_box_drawing_spans_the_whole_cell_at_the_font_stem_width) (font='cozette.bdf')`
13. `ERROR`, group `3`, occurrences `2`:
    `test_matching_size_draws_the_strike_bit_for_bit (test_bitmap_font_render.BitmapFontRenderTest.test_matching_size_draws_the_strike_bit_for_bit)`
14. `ERROR`, group `3`, occurrences `2`:
    `test_dentistry_brackets_hug_the_cell_edges (test_synthesized_symbols.SynthesizedSymbolTest.test_dentistry_brackets_hug_the_cell_edges)`
15. `FAIL`, group `18`, occurrences `1`:
    `test_build_rejects_every_other_descr_shape (test_build_metadata.BuildMetadataTests.test_build_rejects_every_other_descr_shape) (descr='ABC')`
16. `ERROR`, group `10`, occurrences `1`:
    `test_child_signal_exit_uses_shell_status_convention (test_pty.PtyTest.test_child_signal_exit_uses_shell_status_convention)`

Наблюдаемые классы отказов, без починки и без приписывания их текущей волне:

- 12 font/bitmap/symbol cases завершаются `RuntimeError: invalid font load response` в `tst/harness.py`;
- `test_color_zwj_grapheme_renders_to_image` — `RuntimeError: invalid render image response`;
- `test_pretty_child_gets_pretty_version` и `test_child_signal_exit_uses_shell_status_convention` — `RuntimeError: test child tty has no path`;
- `test_build_rejects_every_other_descr_shape` получает `PermissionError: [Errno 1] Operation not permitted` из `os.setpgid(0, 0)` при вложенном запуске `build`, поэтому ожидаемого сообщения `descr must be exactly two ASCII letters` нет. Это отказ тестового окружения/sandbox, а не подтверждённый дефект валидации `descr`.

### 5.2. Skipped baseline

Ниже `occurrences` также относится к двум режимам вместе. Причина — часть ключа наблюдения.

1. occurrences `2`: `test_a_bar_inside_the_cell_leaves_the_cell_alone (test_font_descent.UnderscoreDescentTest.test_a_bar_inside_the_cell_leaves_the_cell_alone)` — `the cell rule under test is the FreeType backend's`.
2. occurrences `2`: `test_child_inherits_no_stray_descriptors (test_pty.PtyTest.test_child_inherits_no_stray_descriptors)` — `needs /proc/self/fd`.
3. occurrences `4`: `test_collection_face_and_representative_advances_define_cells (test_font_resolver.FontResolverTest.test_collection_face_and_representative_advances_define_cells)` — `Fontconfig is not available`.
4. occurrences `1` в каждом режиме: `test_ctrl_shift_b_puts_the_list_away (test_wayland_chrome.WaylandChromeTest.test_ctrl_shift_b_puts_the_list_away)` — нужны Linux/lavapipe и `sway`, `swaymsg`, `grim`, `wtype`; отсутствуют все четыре программы.
5. occurrences `2`: `test_darkening_scales_with_the_option (test_soft_render.SoftRenderTest.test_darkening_scales_with_the_option)` — `CoreText renders here; -soft is a FreeType knob`.
6. occurrences `4`: `test_extra_fallback_fonts_keep_primary_metrics (test_font_resolver.FontResolverTest.test_extra_fallback_fonts_keep_primary_metrics)` — `Fontconfig is not available`.
7. occurrences `4`: `test_fontconfig_family_loads_without_a_search_path (test_font_resolver.FontResolverTest.test_fontconfig_family_loads_without_a_search_path)` — `Fontconfig is not available`.
8. occurrences `4`: `test_fontconfig_loads_all_four_style_faces_when_available (test_font_resolver.FontResolverTest.test_fontconfig_loads_all_four_style_faces_when_available)` — `Fontconfig is not available`.
9. occurrences `4`: `test_fontconfig_resolves_family_and_alias (test_font_resolver.FontResolverTest.test_fontconfig_resolves_family_and_alias)` — `Fontconfig is not available`.
10. occurrences `2`: `test_non_utf8_and_unknown_locales_warn (test_options.OptionTest.test_non_utf8_and_unknown_locales_warn)` — `musl silently maps every locale to UTF-8`.
11. occurrences `4`: `test_overlay_width_is_independent_but_vertical_metrics_must_match (test_font_resolver.FontResolverTest.test_overlay_width_is_independent_but_vertical_metrics_must_match)` — `Fontconfig is not available`.
12. occurrences `2`: `test_soft_zero_departs_from_the_hinted_grid (test_soft_render.SoftRenderTest.test_soft_zero_departs_from_the_hinted_grid)` — `CoreText renders here; -soft is a FreeType knob`.
13. occurrences `1` в каждом режиме: `test_st_is_a_bare_window_on_linux (test_wayland_chrome.WaylandChromeTest.test_st_is_a_bare_window_on_linux)` — нужны Linux/lavapipe и `sway`, `swaymsg`, `grim`, `wtype`; отсутствуют все четыре программы.
14. occurrences `2`: `test_strace_accepts_inputs_from_the_dependency_closure (test_build_metadata.BuildMetadataTests.test_strace_accepts_inputs_from_the_dependency_closure)` — `strace is only available on Linux`.
15. occurrences `2`: `test_strace_rejects_undeclared_read_on_a_cached_node (test_build_metadata.BuildMetadataTests.test_strace_rejects_undeclared_read_on_a_cached_node)` — `strace is only available on Linux`.
16. occurrences `2`: `test_symbol_font_defaults_to_private_use_pictograms (test_font_fallback.FontFallbackTest.test_symbol_font_defaults_to_private_use_pictograms)` — `Core Text precedes the Fontconfig fixture on Darwin`.
17. occurrences `2`: `test_symbol_font_range_beats_primary_coverage (test_font_fallback.FontFallbackTest.test_symbol_font_range_beats_primary_coverage)` — `Core Text precedes the Fontconfig fixture on Darwin`.
18. occurrences `2`: `test_symbol_font_that_misses_the_range_leaves_it_alone (test_font_fallback.FontFallbackTest.test_symbol_font_that_misses_the_range_leaves_it_alone)` — `Core Text precedes the Fontconfig fixture on Darwin`.
19. occurrences `2`: `test_system_face_outranks_the_embedded_fallback (test_font_fallback.SystemFallbackTest.test_system_face_outranks_the_embedded_fallback)` — `Core Text precedes the Fontconfig fixture on Darwin`.
20. occurrences `2`: `test_the_bar_below_the_descender_is_drawn (test_font_descent.UnderscoreDescentTest.test_the_bar_below_the_descender_is_drawn)` — `the cell rule under test is the FreeType backend's`.
21. occurrences `2`: `test_the_underscore_has_ink_at_every_size (test_font_descent.DejaVuUnderscoreTest.test_the_underscore_has_ink_at_every_size)` — `the cell rule under test is the FreeType backend's`.
22. occurrences `1` в каждом режиме: `test_the_window_is_drawn_with_its_buttons_and_a_round_panel (test_wayland_chrome.WaylandChromeTest.test_the_window_is_drawn_with_its_buttons_and_a_round_panel)` — нужны Linux/lavapipe и `sway`, `swaymsg`, `grim`, `wtype`; отсутствуют все четыре программы.
23. occurrences `2`: `test_uncovered_script_comes_from_the_system_chain (test_font_fallback.SystemFallbackTest.test_uncovered_script_comes_from_the_system_chain)` — `Core Text precedes the Fontconfig fixture on Darwin`.

В исходном извлечении списка 26 уникальных записей включают разные причины одного и того же Wayland test ID для `st_test` и `st_test_prod_parser`; выше три такие пары объединены в пункты 4, 13 и 22. Поэтому пронумеровано 23 test ID, но уникальных пар `(test ID, reason)` остаётся 26, а суммарно skipped instances — 58.

## 6. Отдельный host-local отказ `./build test`

На машине есть `/Users/kibomibo/.config/shitty/shitty.toml` с неизвестной базовому binary опцией `sidebarGlassTone`. Поэтому `production_surface` краснит два subTest:

- `test_version_probe_exits_before_window_startup (__main__.ProductionSurfaceTest.test_version_probe_exits_before_window_startup) (argument='-v')`;
- `test_version_probe_exits_before_window_startup (__main__.ProductionSurfaceTest.test_version_probe_exits_before_window_startup) (argument='-version')`.

Ожидание `stderr == b""` не выполняется из-за предупреждения о пользовательском конфиге. Диагностический прогон с пустым `HOME` и обязательными переменными из `build.py` дал `6/6 OK`, rc `0`. Эти два subTest не входят в 16 красных Python group baseline выше; их надо учитывать отдельно при запуске общего `./build test` с тем же пользовательским HOME. Не следует записывать их как регрессию T1–T3.

## 7. Ограничения и инструкция для R1/V1

- Этот отчёт — macOS baseline. Он ничего не доказывает о Linux/libstdc++ или Vulkan; Linux baseline должен сниматься на `arch-mac` отдельно.
- Полный `./build test` на исходном HOME остановлен на `production_surface`; успешное завершение общего графа не заявляется. Python baseline при этом полон благодаря `-k`.
- Основной режим сравнения для волны: `./build -k test_suite test_suite_prod_parser`, та же машина/платформа и то же окружение runner. Сравнивать полные test IDs из раздела 5.1, skipped отдельно.
- `OK: 1115` относится к ручному полному `unit_tests`; отсутствие итоговой строки в будущем прогоне нельзя считать успехом, rc `148` надо перезапускать.
- Старые числа из `CLAUDE.md` (`20` красных, четыре группы, другое общее количество tests) относятся к прежнему дереву и не заменяют этот свежий список.
- Не сравнивать режимы по одному количеству failures/errors: expected failures и duplicated discovery/subTests делают поимённый список обязательным.
- Снимки экрана не делались, Linux-стенд не использовался, `plt_unit_tests` в T0 не входил.
- Push и PR не выполнялись.

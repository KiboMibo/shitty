# T8. Новые умолчания и флаг печати конфига

- **Дата:** 2026-09-06
- **Ветка:** `feat/T8-defaults-and-print`, от `master` `d8f2b244`
- **Коммиты:** `2b5159dd` (часть 1), `14036b51` (часть 2), отчёт — третий
- **Машина:** macOS 26.5.2, arm64, Apple clang 21

Пометки: **[и]** измерено — запущено и увидено; **[п]** прочитано — написано в
нашем коде, со ссылкой на строку; **[в]** выведено — рассуждение.

## Ответ одной строкой

Флаг — **`-printConfig`**, `cliOnly`, `NoArg`; печатает `bin/<бренд>/<бренд>.toml`
побайтово, встроенный в бинарник на сборке.

## Почему `-printConfig`, а не `-defaultConfig`

Ряд, в который он встаёт, — `-listres`, `-listColorSchemes`, `-version`: имя
начинается с глагола, называющего действие, и ни одно из них нельзя прочесть
как «установить». `-defaultConfig` читается ровно так — как путь к файлу,
который надо назначить умолчанием, тем более рядом с существующим `-config
PATH`. `-listConfig` был бы третьим в ряду `-list*`, но «list» обещает
перечисление имён, а печатается целый файл.

## Часть 1. Пятнадцать умолчаний

Все пятнадцать — одна колонка `hardDefault` в `optionsTable`
(`lib/shitty/options.cpp:80`–`:143`), кроме `uriScheme`, у которого список
живёт в коде (`:1308`), потому что колонка держит один скаляр и не может
держать список **[п]**.

| Опция | Было | Стало | Где |
|---|---|---|---|
| `colorScheme` | `default` | `Catppuccin Mocha` | таблица |
| `fontsize` | `16` | `15` | таблица |
| `naturalEditing` | `false` | `true` | таблица |
| `saveLines` | `500` | `50000` | таблица |
| `uriScheme` | `http https file` | `+ mailto gemini` | `options.cpp:1308` |
| `tabBar` | `top` | `sidebar` | таблица |
| `panes` | `false` | `true` | таблица |
| `paneDividerColor` | схемный bright black | `#00cd00` | таблица |
| `autoHideChrome` | `false` | `true` | таблица |
| `transparentTitlebar` | `false` | `true` | таблица |
| `backgroundBlur` | `off` | `glass` | таблица |
| `backgroundOpacity` | `100` | `60` | таблица |
| `quickGeometry` | `100%x40%+0+0` | `90%x75%+5%+10%` | таблица |
| `quickCornerRadius` | `0` | `12` | таблица |
| `quickRememberFrame` | `false` | `true` | таблица |

### Три решения по ходу, которые стоит назвать

**1. `paneDividerColor` перестал быть производным.** Было `nullptr` в колонке и
ветка `else paneDividerColor = vt.palette[8]`. Стало `hardDefault "#00cd00"`, и
ветка убрана: `get()` доходит до таблицы только после командной строки и
конфига, поэтому явное значение перебивает без всякого `if` **[п]**
(`options.cpp:851`–`:882`, порядок источников). Комментарий над местом
переписан — он объяснял ровно ту логику, которой больше нет.

**2. `-help` стал печатать умолчание булевых опций.** Раньше хвост
`(default: …)` выводился только для не-`NoArg`; молчание не несло информации,
пока каждое булево умолчание было `false`. Теперь пять из них `true`, и вопрос
«`-panes` уже включён?» справка обязана уметь ответить. Флаги-действия
(`-help`, `-version`, `-listres`, `-listColorSchemes`, `-printConfig`,
`-vulkanInfo`, `-vulkanBlit`) исключены по признаку `cliOnly && NoArg`: они не
переключатели и в конфиг не попадают.

**3. Примерные конфиги перестали присваивать `bg`, `fg`, `cr` и шестнадцать
слотов палитры, и начали присваивать `colorScheme`.** Это выход за пределы
таблицы из пятнадцати строк, и вот почему он был нужен.

`applyColorOption()` (`options.cpp:1362`) применяет явный цвет, если его
источник **не ниже** источника `colorScheme`. Пока файл присваивал `bg` и
шестнадцать `colorN`, живое `colorScheme = "Catppuccin Mocha"` в том же файле
не имело бы никакого эффекта: оба источника `Config`, и цвета выигрывают
**[п]**. То есть примерный конфиг с новым умолчанием схемы был бы ловушкой —
пользователь меняет `colorScheme` и не видит ничего.

Второе следствие того же: **примерный конфиг стал равен умолчаниям по ключам и
значениям**, чего до T8 не было (`fontsize = 15` при `hardDefault "16"`, `bg =
"#2e3440"` при схеме на чёрном). Именно это делает часть 2 честной — печатать
файл как «конфиг с умолчаниями» можно только когда он им является.

Закомментированные строки несут значения самой схемы (`# bg = "#1e1e2e"`,
`# color0 = "#45475a"` и далее), так что раскомментировать любую — значит
ничего не изменить, и только потом править. Это лучше, чем оставленный Nord:
раскомментировать `# bg = "#2e3440"` под схемой Catppuccin означало бы
неожиданно перекрасить фон.

### Чего в `options.h` **не** сделано, и почему

Бриф просил держать поля `Options` согласованными с `hardDefault`, «если они
дублируют» его. Я сначала так и сделал — девять полей — и **измерил**: девять
инициализаторов уронили **36** узлов `unit_tests`, из которых **32** не имели
к продукту никакого отношения **[и]**. Полный список первого прогона —
`scratchpad/t8-ut-1.log`.

Разбор, и он же довод:

- Продукт **никогда не читает** эти инициализаторы. `OptionsParser::parse()`
  присваивает каждое из девяти полей безусловно, а второй и последний
  экземпляр в дереве — заглушка `Composer` (`composer.cpp:53`), которую
  `configuration.cpp:148` заменяет разобранной на старте **[п]**.
- Репозиторий уже держит противоположное правило и держит его явно:
  `border = 0` при `hardDefault "2"`, `fontsize = 0` при `"16"`,
  `sidebarWidth = 0` при `"220"`, `vt.saveLines = 0` при `"500"` **[п]**.
  Инициализатор — **инертное** значение узла, а не умолчание продукта.
- Все девять моих полей были ровно инертными: непрозрачный фон, нет подложки,
  нет сайдбара, нет панелей, радиус 0. `sidebarTabTint = 65`, на который бриф
  ссылается как на образец, — единственное поле, чей инициализатор **выбран**
  (инертным был бы `0`), и потому единственное, которое действительно надо
  вести за таблицей руками.

Инициализаторы возвращены к прежним значениям; вместо девяти правок над
`struct Options` стоит один абзац, называющий это правило и `sidebarTabTint`
как его единственное исключение (`options.h:79`–`:97`). После отката из 36
красных остались **4** — все в `options_ut.cpp` и все про **разобранные**
умолчания, то есть ровно те, что и должны были покраснеть **[и]**.

### `ext/plt` не тронут

`plt::QuickGeometry` (`ext/plt/window.h:97`) несёт инициализатор
`100%x40%+0+0` — дословно **прежнее** умолчание `quickGeometry`. Это
единственное место, где старое значение осталось написанным. Не менял: продукт
его не читает (`getQuickGeometry()` зовётся безусловно, `options.cpp:1461`), а
файл лежит за границей, которую бриф просил не переходить **[п]**. Отмечено
комментарием в `options.h:160`–`:172` и доложено командиру по ходу.

## Часть 2. `-printConfig`

`{"printConfig", NoArg, "true", "false", …, cliOnly}` в таблице; в
`handlePrintOpts()` — четвёртым, рядом с `-listColorSchemes`;
`OptionsParser::printConfig()` пишет `brand.exampleConfig()` в stdout.

Встраивание — **тем же генератором, что уже встраивает иконки**:
`lib/shitty/generate_font_data.py` принимает любой файл и `name=path`, так что
двух новых узлов в `build.py` хватило. По бренду, а не один общий: `pt` обязан
печатать `pretty.toml`, а `shitty.toml` внутри `pt` — запрещённая подстрока.

| Файл | Что |
|---|---|
| `build.py` | `shitty_config_data`, `pretty_config_data`; заголовок добавлен во входы семи узлов, компилирующих `bin/{st,pt}/main.cpp` |
| `lib/shitty/generate_font_data.py` | `EmbeddedFontData` под именованным guard'ом |
| `lib/shitty/brand.h`, `brand.cpp` | `exampleConfig()`; у `GenericBrand` — пусто |
| `bin/st/main.cpp`, `bin/pt/main.cpp` | реализация на встроенном массиве |
| `lib/shitty/options.cpp` | строка таблицы, `handlePrintOpts()`, `printConfig()` |
| `bin/st/shitty.toml`, `bin/pt/pretty.toml` | строка `# CLI: -printConfig` |
| `README.md` | абзац с командой сохранения |
| `tst/test_config.py` | два теста |

**Почему не генерация из таблицы.** Таблица — не то место, где живут
действующие умолчания, и это проверяемо **[п]**:

- `bg` и `fg` несут `hardDefault "#000"` и `"#fff"`, которых никто никогда не
  видит: `applyColorOption()` применяет их только при источнике выше
  `HardDefault`, а схема в силе всегда;
- `cr` и шестнадцать `colorN` жёсткого умолчания не имеют вовсе и тоже идут
  от схемы;
- `uriScheme` — список, колонка держит скаляр;
- `title` подставляется из бренда особым случаем (`options.cpp:869`).

Генератор по таблице напечатал бы четыре из них неверно и промолчал бы об
этом. Примерный конфиг, наоборот, — артефакт, который **уже обязан** быть
верным: на нём стартует `test_config.py`, он сверяется с `-help`, и две
брендовые копии сверяются друг с другом. Встраивание превращает расхождение из
обнаружимого в невыразимое.

**Сбой на первой сборке [и].** `bin/st/main.cpp` стал первой единицей
трансляции, включающей два заголовка от этого генератора: `#pragma once`
защищает файл, а не имя, и `struct EmbeddedFontData` оказался определён
дважды — `error: redefinition of 'EmbeddedFontData'`. Генератор теперь ставит
`#ifndef SHITTY_EMBEDDED_FONT_DATA`.

## Десять критериев

| № | Критерий | Результат |
|---|---|---|
| 1 | пятнадцать умолчаний в `-help` и в дампе | **сходится [и]** |
| 2 | оба конфига обновлены, сторож чётности зелёный | **сходится [и]** |
| 3 | поимённый список починенных тестов | **ниже**, 13 файлов |
| 4 | `unit_tests` `OK: N`, `EXIT=0`, потеряно 0 | **сходится [и]** с оговоркой: 4 переименования, названы |
| 5 | питоновский набор, поимённый diff | **сходится [и]**, новых 0, ушедших 0 |
| 6 | предупреждение при `backgroundOpacity 100` | **сходится [и]** |
| 7 | флаг: `-help`, `toml_dump`, `-config`, `pt` без бренда | **сходится [и]** |
| 8 | `README.md` | **сходится [и]** |
| 9 | `./build st pt --clear` | **сходится [и]**, **237** узлов, `EXIT=0` |
| 10 | брендирование `pt` зелёный, `st` красный | **сходится [и]** |

### 1. Пятнадцать умолчаний **[и]**

`-help`, обрезано по ширине:

```
  -backgroundBlur      … (default: glass)
  -backgroundOpacity   … (default: 60)
  -colorScheme         Named terminal color scheme (default: Catppuccin Mocha)
  -fontsize            Font size (default: 15)
  -naturalEditing      Bind the macOS natural text editing chords (default: true)
  -quickGeometry       … (default: 90%x75%+5%+10%)
  -quickCornerRadius   … (default: 12)
  -quickRememberFrame  … (default: true)
  -tabBar              Where the tab bar lives: top or sidebar (default: sidebar)
  -autoHideChrome      Hide the titlebar chrome and reveal it on hover (default: true)
  -panes               Allow splitting a tab's terminal into multiple panes (default: true)
  -paneDividerColor    Color of the seam between panes… (default: #00cd00)
  -saveLines           Lines of scrollback history (default: 50000)
  -transparentTitlebar … (default: true)
  -uriScheme           …, default http https file mailto gemini
```

`uriScheme` — единственный, у кого умолчание в тексте описания, а не в хвосте:
колонка не держит список.

Дамп режима теста, без единой опции (`-saveLines` снят с argv, который
`harness.py` подставляет всегда, иначе виден он, а не умолчание):

```
auto_hide_chrome=1        panes=1                      save_lines=50000
background_blur=glass     pane_divider_color=52480     sidebar_tabs=1
background_opacity=60     quick_corner_radius=12       transparent_titlebar=1
bg=1973806                quick_geometry_w=90 (%)      natural_editing=1
fg=13489908               quick_geometry_h=75 (%)      fontsize=15
cr=13489908               quick_geometry_x=5  (%)
uri_schemes=http,https,file,mailto,gemini              quick_geometry_y=10 (%)
                          quick_remember_frame=1
```

`52480` = `0x00cd00`; `1973806` = `0x1e1e2e` и `13489908` = `0xcdd6f4` — фон и
текст Catppuccin Mocha, то есть `colorScheme` виден через них. Имени схемы в
дампе нет и не заводилось: оно содержит пробел, а `harness.options()` делит
строку по пробелам **[п]**.

Три поля дампа заведены этой задачей — `natural_editing`,
`pane_divider_color`, `uri_schemes` (`test_mode.cpp:2586`–`:2600`). Список
склеен запятыми и никогда пробелами по той же причине; контракт
`harness.options()` не расширялся — числовые поля числовые, `uri_schemes`
падает в ветку `except ValueError` вместе с `background_blur`.

### 2. Оба конфига и сторож чётности **[и]**

`bin/pt/pretty.toml` получен из `bin/st/shitty.toml` подстановкой
`Shitty→Pretty`, `shitty→pretty`, `SHITTY→PRETTY` и `` `st `` → `` `pt ``.
Проверено, что **до** правки то же правило воспроизводит нынешний
`pretty.toml` побайтово **[и]** — то есть подстановка не изобретена, а
измерена, и она же — правило, которое кодирует `BRAND_WORDS` в
`ExampleConfigParityTest`.

Три теста `ExampleConfigParityTest` зелены; `grep -ic shitty bin/pt/pretty.toml`
даёт `0` **[и]**.

### 3. Починенные тесты, поимённо

**`lib/shitty/options_ut.cpp` — четыре красных, все про разобранные умолчания.**

| Тест | Что изменилось |
|---|---|
| `QuickTransparentTitlebarAndHotkeyDefaultToDisabled` → **`QuickTheTransparentTitlebarAndTheHotkeyTakeTheirDefaults`** | `!opts->transparentTitlebar` → `opts->transparentTitlebar`. Имя утверждало старое умолчание, поэтому переименован; `quick` и `quickHotkey` рядом остались прежними и держат тест от чтения «всё true» |
| `TabBarPlacementDefaultsToTheTitleBarAndTakesTwoNames` → **`TabBarPlacementDefaultsToTheSidebarAndTakesTwoNames`** | умолчание `!sidebarTabs` → `sidebarTabs`; обе ветки с явными `top`/`sidebar` не тронуты, и `top` теперь единственное место в наборе, где непокрытая раскладка проверяется |
| `TheDividerDefaultsToOnePixelOfTheSchemesBrightBlack` → **`TheDividerDefaultsToOnePixelOfItsOwnGreen`** | три сверки с `palette[8]` заменены на литерал `#00cd00`, **и добавлена премисса**: сначала утверждается, что цвет шва и `palette[8]` вообще различимы. Без неё «это не палитра» прошло бы на дереве, где опция тихо вернулась к выводу из схемы, а схема оказалась зелёной |
| `TranslucencyDefaultsToTheOpaqueWindow` → **`TranslucencyDefaultsToGlassOverASixtyPercentBackground`** | `100`/`Off` → `60`/`Glass`. Обе половины, потому что каждая по отдельности выполнима случайно |

**Заведён один:** `Options::TheFifteenDefaultsTaskEightChose`. Девять из
пятнадцати умолчаний **не имели в наборе ни одного наблюдателя** — именно
поэтому покраснели ровно четыре. Тест называет все пятнадцать литералами (не
чтением таблицы: таблица согласилась бы сама с собой), и несёт премиссу —
`unparsed` экземпляр обязан отличаться от разобранного по трём полям, иначе
тест прошёл бы на инстансе, которого парсер не касался.

**`tst/` — двадцать восемь красных, тринадцать файлов.**

| Файл, тест | Причина | Правка |
|---|---|---|
| `harness.py` | `naturalEditing = true` перехватывает Option/Command-аккорды до pty; **13** тестов в 7 файлах меряют именно эти байты | пин `+naturalEditing` в argv по умолчанию, параметр `pin_natural_editing`, как `pin_vga` |
| `test_tmux_regress_tty_keys_keypad_cursor` `alt_application_left/right` | то же | снят пином |
| `test_tmux_regress_input_keys_extended_cursor_head` `alt_left/right` | то же | снят пином |
| `test_tmux_regress_tty_keys_cursor_home_rxvt` `alt_normal_left/right` | то же | снят пином |
| `test_foot_keyboard` `kitty_swedish_alt_backspace` | то же | снят пином |
| `test_contour_input_generator` `legacy_arrow_modifier_matrix` (key=262, 263; mod=4) | то же | снят пином |
| `test_xtermjs_keyboard` `mac/non_mac_alt_left/right_uses_modifier_three` (4 шт.) | то же | снят пином |
| `test_keyboard.test_kitty_ctrl_base_layout_requires_control` | **не** naturalEditing: `tabBar = sidebar` забирает `cmd+B` (V3) | пин `-tabBar top` в самом тесте, комментарий называет причину |
| `test_config.test_example_config_is_accepted_by_the_application` | конфиг перестал присваивать `fg`/`bg`/`cr` | ожидания → цвета Catppuccin Mocha, литералами: это единственное место, где путь «файл → схема → палитра → дамп» пройден целиком |
| `test_config.test_quick_and_transparent_titlebar_default_to_disabled` → **`…_take_their_defaults`** | `transparentTitlebar` теперь `1` | обе половины пары, `quick` остался `0` |
| `test_config.test_translucency_defaults_to_the_opaque_window` → **`…_to_glass_over_a_translucent_window`** | `60`/`glass` | обе половины |
| `test_config.test_blur_without_translucency_warns_and_still_starts` | предупреждение больше не следует из умолчаний | конфиг теста говорит `backgroundOpacity = 100` явно |
| `test_config.test_the_backdrop_warning_speaks_of_showing_and_covers_both_modes` (2 подслучая) | то же | то же; премисса теста переформулирована — она ссылалась на «умолчание 100» |
| `test_options.test_background_blur_is_listed_in_help_with_its_values_and_default` | `-help` стал печатать хвост булевым | свидетели переставлены: `altScroll` теперь **с** хвостом, `config` (без умолчания вовсе) — без; добавлен `help` как свидетель флага-действия |
| `test_options.test_font_size_source_priority_is_cli_then_env_then_default` | `16` → `15` | одно число |
| `test_color_scheme.test_the_default_scheme_is_retro_legends_on_black` → **`…_is_catppuccin_mocha`** | схема | `fg`/`bg` и запрос палитры OSC 4 — обе половины, потому что схема, подключённая только одной, прошла бы на любой из них |
| `test_color_scheme.test_explicit_colors_override_the_default_scheme` | `bg` без явного значения теперь схемный | `0x000000` → `0x1E1E2E`, комментарий |
| `test_panes_protocol.test_a_split_is_declined_without_the_panes_option` | `panes = true` | `extra_arguments=("+panes",)`; имя оставлено — «без опции panes» по-прежнему описывает то, что тест делает |
| `test_dynamic_colors.test_border_follows_dynamic_background` | фон приходит умноженным на 0.6: `(153,0,0)` вместо `(255,0,0)` | пин `-backgroundOpacity 100 -backgroundBlur off` |
| `test_sixel.test_transparent_pixels_show_pen_background` | то же: `102 = 170·0.6` | тот же пин |
| `test_mouse_frontend_pointer.test_selection_drag_finishes_after_pointer_leaves_window` | **дефект, см. «Обнаружено»** | пин `+panes`, комментарий называет дефект |

**Заведены два** (`test_options.py`): `test_the_natural_editing_preset_is_on_by_default`
— единственный наблюдатель умолчания, которое пин прячет от всего набора,
поэтому утверждает **обе** стороны пина; и
`test_the_divider_colour_and_uri_schemes_carry_their_defaults` — два умолчания,
у которых в наборе больше нет наблюдателя, плюс проверка, что заданный список
схем **заменяет** умолчание, а не дополняет.

**Вырожденных фикстур не заведено, и это проверено адресно.** Три места, где
они были бы естественны, несут премиссу внутри теста: `TheFifteenDefaults…`
(разобранный ≠ неразобранный), `TheDividerDefaults…` (шов ≠ `palette[8]`),
`test_the_natural_editing_preset…` (пин действительно действует). Приём — из
`CLAUDE.md`, найденный `F7`.

### 4. `unit_tests` **[и]**

```
SHITTY_PTY_TEST_HELPER=$PWD/.build/pty_test_helper .build/unit_tests < /dev/null
OK: 986      EXIT=0
```

Эталон, снятый в этом же дереве на `master` `d8f2b244` до единой правки:
`OK: 985`, `EXIT=0`. Поимённый diff 985 против 986:

- **переименовано 4** — все четыре названы в критерии 3 вместе с причиной;
  каждое имя утверждало умолчание, которого больше нет;
- **добавлено 1** — `Options::TheFifteenDefaultsTaskEightChose`;
- **потеряно 0**, красных 0.

Код возврата проверен отдельно от последней строки, stdin перенаправлен: при
`EXIT=139` и `EXIT=148` итоговой строки нет вовсе (`CLAUDE.md`).

### 5. Питоновский набор **[и]**

Одним процессом, `SHITTY_TEST_FONTCONFIG=0`, `SHITTY_TEST_VERSION=2026.09.06`,
все четыре симлинка проверены живыми до прогона:

```
Ran 6613 tests in 124.962s
FAILED (errors=14, skipped=23, expected failures=549)
```

Эталон снят **тем же режимом в этом же дереве на `master`** до правок:
`Ran 6609`, `errors=14`, `skipped=23`, и поимённо **совпал с
`scratchpad/reds-master.txt` до строки** **[и]** — то есть эталон брифа
подтверждён независимо, а не принят на веру.

Поимённый diff финального прогона с эталоном: **новых 0, ушедших 0**. Разница
`6613 − 6609 = 4` — четыре заведённых теста (два в `test_options.py`, два в
`test_config.py`); переименования на счёт не влияют. `skipped` сошёлся:
23 = 23.

`tst/production_surface.py` со своим окружением — **5/5**, `OK` **[и]**.

### 6. Предупреждение при `backgroundOpacity 100` **[и]**

```
$ st -backgroundOpacity 100 …
shitty: -backgroundBlur has nothing to show while -backgroundOpacity is 100;
        lower -backgroundOpacity to let the desktop show through
```

На умолчаниях (`glass` + `60`) молчит **[и]**. То есть смысл предупреждения не
только уцелел, но и перевернулся правильной стороной: раньше оно срабатывало
на умолчаниях у всякого, кто включил подложку, теперь — только у того, кто
руками поднял непрозрачность обратно. Два теста в `test_config.py` (четыре
подслучая) теперь говорят `100` явно и покрывают ровно этот случай.

### 7. Флаг **[и]**

```
$ st -printConfig | diff - bin/st/shitty.toml      → пусто, rc=0
$ pt -printConfig | diff - bin/pt/pretty.toml      → пусто, rc=0
$ st -printConfig | .build/toml_dump /dev/stdin    → "ok", EXIT=0
$ st -config <(st -printConfig) -version           → Shitty 2026.09.06, rc=0
$ pt -config <(pt -printConfig) -version           → Pretty 2026.09.06, rc=0
$ pt -printConfig | grep -ic shitty                → 0
$ st -help | grep printConfig                      → строка 31
```

Плюс два теста в `test_config.py`, которые повторяют это в наборе и несут
премиссу (`len(stdout) > 4000`): пустой вывод сошёлся бы с пустым файлом, а
`Brand::generic()` действительно возвращает пусто.

Второй тест не ограничивается «стартовало»: он поднимает терминал на
напечатанном файле и сверяет шесть значений из дампа — конфиг, который парсер
принял, проигнорировав каждый ключ, прошёл бы первую половину.

### 8. `README.md` **[и]**

Абзац в разделе «Config file», сразу за ссылками на оба примерных конфига:

> `-printConfig` writes that file to standard output, so a configuration can be
> started from the shipped one without hunting for the repository:
>
> ```sh
> mkdir -p ~/.config/shitty && ./st -printConfig > ~/.config/shitty/shitty.toml
> ```
>
> What it prints is the example config itself, embedded in the binary at build
> time — every option at its default, with the comment that explains each one,
> and `pt -printConfig` writing Pretty's copy rather than Shitty's. …

Кроме него README правлен в четырёх местах, где он называл прежние умолчания:
абзац «Every option … is **off by default**» (был прямо ложен), шов «in the
colour scheme's bright black», список схем URI, и строка про `-backgroundBlur`.
Плюс `-printConfig` добавлен в перечень `-v`/`-help`/`-listres`.

### 9. Сборка **[и]**

`./build st pt --clear` — **237** узлов, `EXIT=0`. Эталон `T1`/`T6`/`T7` —
235; +2 — два новых узла встраивания конфигов. Предупреждение одно и
унаследованное: `sprintf` в `ext/libstd/std/str/fmt.cpp:51`.

После `--clear` симлинки остальных целей пересобраны явно и проверены на
существование до каждого прогона (`CLAUDE.md`: битый `.build/st_test`
заставляет набор врать).

### 10. Брендирование **[и]**

Прямым запуском, минуя штамп `./build`:

```
python3 tst/pretty_binary_branding.py .build/pt   →  EXIT=0
python3 tst/pretty_binary_branding.py .build/st   →  EXIT=1   (отрицательный контроль)
```

Проверено **после** встраивания: в `pt` теперь лежит целый текстовый файл,
который мог бы нести чужой бренд, — не несёт.

## Форматтер

`dev/style.py` на пяти тронутых C++-файлах переписывает: `enum class
BackdropMode: u8` → `: u8` в `options.h`, порядок включений и два пустых ряда
в `options_ut.cpp`, порядок включений в `brand.cpp` — **всё чужое и
дореформенное, ни одной из добавленных задачей строк**; откачено, как делали
`T5`, `T6`, `T7`. Единственное, что принято, — склейка двух групп включений в
`bin/st/main.cpp` и `bin/pt/main.cpp`: она задета ровно тем, что задача
добавила туда второе включение. `options.cpp` и `test_mode.cpp` форматтер не
трогает вовсе.

## Обнаружено

### 1. `panes = true` теряет выделение, если перетаскивание кончилось вне окна

Самое дорогое из найденного, и это **дефект, а не фикстура**. Измерено
адресно **[и]**:

```
panes=off leave=no    выделение (0,0,5,0)   отпускание → b'abcde'
panes=off leave=yes   выделение (0,0,5,0)   отпускание → b'abcde'
panes=on  leave=no    выделение (0,0,5,0)   отпускание → b'abcde'
panes=on  leave=yes   выделение (0,0,5,0)   отпускание → b''
```

То есть: снапшот выделения после движения верен во всех четырёх случаях, и
теряется именно **отпускание**, и только когда перед ним прошёл
`pointer_presence(False)`. Пользовательски это «протянул выделение за край
окна, отпустил — копировать нечего».

Дефект **не заведён этой задачей**: он живёт за `-panes` и на `master`, а T8
лишь вывела его на дефолтный путь. Довод — по устройству **[в]**: диff задачи
не касается ни маршрутизации указателя, ни `SessionSet`; `panes` — один бит,
и путь под ним на `master` тот же самый.

Не чинил: место — в маршрутизации мыши/сессий, вне файлов задачи. Тест
пришпилен `+panes` с комментарием, называющим дефект, чтобы он продолжал
задавать свой вопрос. Доложено командиру по ходу.

### 2. `tabBar = sidebar` забирает `cmd+B` у pty

Штатное поведение (V3: аккорд переключает сайдбар), но с новым умолчанием
`cmd+B` перестаёт доходить до приложения **по умолчанию**. Поймано
`test_keyboard.test_kitty_ctrl_base_layout_requires_control`, который выбрал
`cmd+B` именно как «Super-аккорд, который никто не забирает» — комментарий в
нём это прямо говорит про `cmd+C`. Пришпилен `-tabBar top`.

Стоит знать: список аккордов, отбираемых у pty, теперь зависит от умолчания
раскладки табов, и наблюдателя у этого зависимости нет ни одного, кроме этого
теста.

### 3. Девять из пятнадцати умолчаний не имели наблюдателя вовсе

Покраснели четыре, а изменилось пятнадцать. `colorScheme`, `fontsize`,
`saveLines`, `naturalEditing`, `panes`, `autoHideChrome`, `quickGeometry`,
`quickCornerRadius`, `quickRememberFrame` не проверял ни один тест —
`unit_tests` молчал бы, если бы любое из них было выставлено неверно. Закрыто
`Options::TheFifteenDefaultsTaskEightChose` и двумя питоновскими тестами.

Это тот же класс, что «место, чей единственный наблюдатель уже красный»
(`CLAUDE.md`, `R6-test`), только вырожденный до нуля наблюдателей.

### 4. Примерный конфиг не был равен умолчаниям, и молча

До T8 `bin/st/shitty.toml` нёс `fontsize = 15` при `hardDefault "16"` и
`bg = "#2e3440"` при схеме на чистом чёрном. Ни один тест на это не смотрел:
`ExampleConfigParityTest` сверяет два файла друг с другом, а не с таблицей, а
`test_example_config_is_accepted_by_the_application` утверждал именно те
расхождения как ожидаемые значения.

Расхождение закрыто (файл стал равен умолчаниям), но **сторожа у равенства
по-прежнему нет**: если завтра поменять `hardDefault` и забыть файл, покраснеет
только то, что случайно на него смотрит. Кандидат на отдельную задачу — тест,
сверяющий дамп на `-config <(st -printConfig)` с дампом без конфига. Половина
такого сторожа уже стоит: `test_print_config_output_starts_the_terminal`
сверяет шесть значений, но шесть, а не все.

### 5. `plt::QuickGeometry` держит прежнее умолчание в `ext/plt`

`ext/plt/window.h:97`–`:100` — `100%x40%+0+0`, дословно старое умолчание
`quickGeometry`, единственное место в дереве, где оно осталось написанным.
Продукт его не читает. Оставлено намеренно, за границей, названо
комментарием в `options.h`.

### 6. `#pragma once` не защищает от повторного определения между заголовками

`generate_font_data.py` печатал `struct EmbeddedFontData` в каждый
сгенерированный заголовок. Пока такой заголовок был в единице трансляции один,
это работало; второй — и `error: redefinition`. Лечится именованным guard'ом.
Мина ждала любого, кто встроит в один бинарник два файла, и ждала с тех пор,
как генератор стал общим для шрифтов и иконок.

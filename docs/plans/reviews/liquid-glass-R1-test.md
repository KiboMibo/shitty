# R1-test. Тесты волны 1 «Liquid Glass»

**PASS_WITH_FINDINGS**

- **Дата:** 2026-09-05
- **Ветка волны:** `wave/liquid-glass-w1`, вершина `f2062151`
- **Ветка ревью:** `review/R1-test`
- **Заведено наблюдателей:** 5 (3 в `unit_tests`, 2 в питоновском наборе)
- **Поставлено мутаций:** 14, из них 13 пойманы поимённо, 1 не поймана — находка

Волна принята: три места, названные `T1` как непокрытые, закрыты и доказаны
мутацией; сторож чётности `T2` проверен не по слову автора, а тремя
собственными мутациями, включая опустошение `BRAND_WORDS`. Находки ниже —
непреграждающие: ни одна не про правку `T1`/`T2`, все про то, что осталось
неохваченным вокруг них.

## Как измерялось

Собственное дерево, цели собраны заранее и все симлинки живые:
`./build st_test pt_test toml_dump example unit_tests pty_test_helper`.

| Прогон | До моих тестов | После |
|---|---|---|
| `unit_tests` (`SHITTY_PTY_TEST_HELPER=…`, `< /dev/null`) | `OK: 982`, `EXIT=0` | `OK: 985`, `EXIT=0` |
| питоновский набор, одним процессом | `Ran 6607`, `errors=14`, `skipped=23` | `Ran 6609`, `errors=14`, `skipped=23` |

Список красных питона **поимённо идентичен** эталону `reds-master.txt` до и
после — сверено `diff`, не количеством. Числа снимались одним и тем же
режимом (один процесс), как требует `CLAUDE.md`.

Каждая мутация прогонялась **обоими** наборами целиком, и красные питона
сверялись с эталоном через `comm`, а не глазами. Ни одна мутация не сдвинула
унаследованных четырнадцати.

## Что заведено и чем доказано

Все пять наблюдателей новые. Столбец «был ли наблюдатель» — состояние
`f2062151` до этой ветки.

| Место | Был наблюдатель | Что заведено | Мутация | Что покраснело поимённо |
|---|---|---|---|---|
| **Подсказка формы в отбивках `SepArg`** — хвост `; -backgroundBlur takes a value, as in -backgroundBlur off` у обеих отбивок | **Нет.** `tst/test_options.py:32-33` читает только `missing value` и `'+' is invalid here`; обе проверки на базовой жалобе | `Options::TheBackdropOptionSpelledAsAFlagIsToldWhatShapeToUse` (`lib/shitty/options_ut.cpp`) | **M1**: обе отбивки вернулись к двухаргументному `raiseError`, хвост выброшен целиком | `unit_tests`: `Options::TheBackdropOptionSpelledAsAFlagIsToldWhatShapeToUse`, `OK: 984, ERR: 1`. Питон — **ни одного** нового красного |
| **Смысловая часть предупреждения** — «has nothing **to show**» вместо «to blur» | **Нет.** `tst/test_config.py` проверяет в этом сообщении только два имени опций, они переживают любую переформулировку | `ConfigFileTest.test_the_backdrop_warning_speaks_of_showing_and_covers_both_modes` (`tst/test_config.py`) | **M2**: текст возвращён к `has nothing to blur` | питон: тот тест, **обе** подпробы (`mode='blur'`, `mode='glass'`). `unit_tests` — `OK: 985` |
| **Предупреждение для обоих непустых режимов** — затвор `!= BackdropMode::Off` | **Нет.** Проверялось только для `blur` | тот же тест, подпроба `mode='glass'` | **M3**: затвор сужен до `== BackdropMode::Blur` | питон: тот тест, подпроба `mode='glass'`, и только она |
| **Хвост `(default: off)` в справке** | **Нет.** Тела строки справки не читает ни один тест набора — четыре существующих смотрят только на присутствие имени | `OptionTest.test_background_blur_is_listed_in_help_with_its_values_and_default` (`tst/test_options.py`) | **M4**: в `printUsage()` условие `parseType != NoArg` перевёрнуто на `== NoArg` | питон: тот тест, и только он. Покраснел **на премиссе** (`altScroll` получил `(default: false)`, `backgroundOpacity` потерял свой) — ровно то, ради чего премисса есть |
| **Три значения в тексте справки** | **Нет**, там же | тот же тест | **M5**: из `helpDescr` опции убрано слово `glass` | питон: тот тест, и только он |
| **Имя `glass` в дампе режима теста** | **Нет.** `test_config.py` проверял дамп только для `off` и `blur`; `Glass` мог печататься как угодно | `EveryBackdropSpellingParsesAndTheNamesPrintBackUnchanged` + подпроба `mode='glass'` питоновского теста | **M6**: `backdropModeName(Glass)` возвращает `"blur"` | `unit_tests`: `Options::EveryBackdropSpellingParsesAndTheNamesPrintBackUnchanged`; питон: `…speaks_of_showing…` (`mode='glass'`) |
| **Разбор `glass`** | Частично — `T1` не пинил значение | `EveryBackdropSpellingParsesAndTheNamesPrintBackUnchanged` | **M7**: `glass` разбирается в `BackdropMode::Blur` | те же два, и только они |
| **Псевдоним `false`** | **Нет.** Ни в `unit_tests`, ни в питоне | `EveryBackdropSpellingParsesAndTheNamesPrintBackUnchanged` | **M8**: псевдоним `false` убран из разбора | `unit_tests`: `Options::EveryBackdropSpelling…`. Питоновский набор **целиком зелёный** — подтверждение, что наблюдателя не было |
| **Псевдоним `true`** | Да, четыре | тот же тест | **M9**: псевдоним `true` убран | `unit_tests`: `Options::EveryBackdropSpelling…`, затем набор **обрывается**, `EXIT=255`, итоговой строки нет; питон: `test_translucency_comes_from_the_config`, `test_blur_without_translucency_warns_and_still_starts`, `test_a_translucent_background_leaves_the_blur_warning_unsaid` |
| **Отказ на неизвестном значении** | Нет | `AnUnknownBackdropModeIsRejectedAndTheThreeNamesAreOffered` | покрыт M7 косвенно; прямая проба — пять отвергаемых написаний (`on`, `1`, `yes`, `blurred`, пустая строка), каждое обязано назвать все три допустимых имени | — |

### Сторож чётности `T2`, проверенный не по слову автора

`T2` утверждает, что премиссы по приёму `F7` у неё есть. Проверено тремя
собственными мутациями, каждая с полным прогоном:

| Мутация | Что покраснело |
|---|---|
| **M10**: ключ `backgroundBlur` удалён из `bin/pt/pretty.toml` | `ExampleConfigParityTest.test_both_brands_assign_the_same_option_keys`, и только он |
| **M11**: значение в `pretty.toml` разошлось не по бренду (`"off"` → `"blur"`) | `…test_both_brands_assign_the_same_values_up_to_the_brand_name`, и только он |
| **M12**: `BRAND_WORDS = ()` — попытка выхолостить правило бренда | `…test_both_brands_assign_the_same_values_up_to_the_brand_name`, **на премиссе**: «no differing value is touched by rebrand()». Тихо зазеленеть не удалось |
| **M13**: строка `# CLI: -vulkanBlit …` убрана из `pretty.toml` | `…test_both_brands_document_the_same_options`, и только он |

Премиссы в `parsed()` (файл существует и непуст, нет дублей ключей, нет живой
таблицы, полы `>= 20`, три якорных имени) прочитаны и признаны достаточными
против вырожденного разбора: пустой разбор не сравнится «равным» ни в одном
из трёх тестов.

## Обнаружено

### 1. Ключ, удалённый из **обоих** конфигов, по-прежнему не имеет наблюдателя

**Измерено, M14.** `backgroundBlur = "off"` удалён из `bin/st/shitty.toml`
**и** из `bin/pt/pretty.toml` одновременно: `unit_tests` — `OK: 985`,
питоновский набор — `Ran 6609`, красные поимённо равны эталону. Ни одного
покраснения.

Сторож `T2` симметричен по построению и ловит расхождение, а не потерю.
`test_example_config_documents_every_public_cli_option` сверяет с `-help`
только строки `# CLI:`, то есть **комментарии**, а не присвоения: удаление
присвоения с сохранением комментария невидимо.

Дешёвого правила для закрытия нет — **измерено**: гипотеза «всё
документированное и не помеченное `command line only` должно быть присвоено»
не держится, у неё 12 законных исключений в каждом файле (`colorScheme`,
`dump`, `import`, `paneDividerColor`, `quickCompanion`,
`quickFullscreenHotkey`, `remap`, `shell`, `sidebarColor`, `soft`,
`symbolFont`, `uriScheme` — опции без осмысленного умолчания). Сторож на этом
правиле потребовал бы списка из двенадцати имён, поддерживаемого руками, —
ровно того, против чего `T2` привела довод в своём комментарии.

Поэтому дыра **названа, а не закрыта**: решение о её цене за командиром.
Волны 1 она не касается — `T1` правила оба файла и правку `T2` бы поймала.

### 2. Ветка «значения нет» в `getBackdropMode` недостижима

**Выведено чтением, `lib/shitty/options.cpp:1195-1198` против `:862`.**

```cpp
if (!get(name, option)) {
    out = BackdropMode::Off;
    return;
}
```

`get()` при отсутствии значения на командной строке и в конфиге отдаёт
`hardDefault` из таблицы (`options.cpp:862`) и возвращает **true**. У
`backgroundBlur` жёсткое умолчание — `"off"`, непустое. Значит ранний выход
не исполняется никогда, и наблюдателя у него быть не может в принципе.

Дефекта нет: обе дороги дают `Off`. Но если жёсткое умолчание когда-нибудь
станет `nullptr`, ветка оживёт молча, и никакой тест этого не заметит —
потому что заметить нечего. Отмечено как факт, правки не требует.

### 3. Регрессия в псевдониме `true` убивает набор, а не краснит его

**Измерено, M9.** Существующий наблюдатель
`Options::TranslucencyComesFromTheConfigAndTheCommandLine` ловит потерю
псевдонима тем, что `Options::create` бросает наружу теста: набор печатает
`Error: -backgroundBlur: expected off, blur or glass!` и обрывается,
`EXIT=255`, **итоговой строки `OK: N` нет вовсе**.

Это тот же класс, что описан в `CLAUDE.md` («Регрессия может дать `SIGSEGV`,
а не красный тест»): автоматика, читающая последнюю строку, такую регрессию
пропустит. Новый `EveryBackdropSpellingParsesAndTheNamesPrintBackUnchanged`
краснеет штатно, `ERR: 1`, потому что проверяет разбор псевдонимов через
`STD_INSIST` на значении, а не через успешность конструирования.

Правку существующего теста не предлагаю: он ловит своё, и его форма старше
волны.

### 4. Псевдонимы `true`/`false` принимаются и с командной строки

**Измерено** новым `EveryBackdropSpelling…`: `-backgroundBlur true` даёт
`Blur`, `-backgroundBlur false` даёт `Off`.

Раздел 4B плана говорит только про TOML: «В TOML `backgroundBlur = true` и
`false` продолжают работать как псевдонимы». Разбор един для обоих
источников, поэтому командная строка их тоже принимает. Это надмножество
контракта, вреда нет, но раньше оно нигде не было записано. Теперь записано
тестом: если волна 2 решит сузить приём до конфига, тест покраснеет и
заставит сказать об этом вслух.

### 5. Хвост подсказки достался **всем** опциям `SepArg`

**Измерено** чтением `options.cpp:1126-1137` и прогоном: правка `T1` живёт в
общей ветке `case OptionKind::SepArg`, поэтому `-fontsize` без значения тоже
отвечает `; -fontsize takes a value, as in -fontsize 12`. Существующие
проверки `tst/test_options.py:32-33` на `-fontsize` и `+fontsize` это
переживают — они на базовой жалобе.

Расширение полезное и, судя по комментарию в коде, намеренное. Наблюдатель у
механизма один — мой тест на `-backgroundBlur`; отдельных наблюдателей у
хвоста прочих опций нет, и заводить их я не стал: код один.

### 6. Мелочь в комментарии `T2`

Комментарий в `parsed()` говорит «today's 57 assignments and 79 documented
names». **Измерено:** присвоений сейчас **58** в каждом файле (документированных
имён — 79, как и сказано). На поведение не влияет: пол проверки — 20.

## Файлы

- `lib/shitty/options_ut.cpp` — три новых теста после
  `TranslucencyComesFromTheConfigAndTheCommandLine`.
- `tst/test_config.py` — `test_the_backdrop_warning_speaks_of_showing_and_covers_both_modes`.
- `tst/test_options.py` — `test_background_blur_is_listed_in_help_with_its_values_and_default`,
  плюс `import re`.

Продуктовый код не тронут: `git diff` против `wave/liquid-glass-w1` содержит
только эти три файла и этот отчёт.

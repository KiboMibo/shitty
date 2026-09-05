# F1 — сведение волны 1: четыре красных и одно расхождение конфигов

Дата: 2026-09-05. Ветка `wave/liquid-glass-w1`, база — мерж `d0e2218e`.
Правка — `a46fd7ed`, отчёт — отдельным коммитом.

Пометки: **измерено** — снято командой в этом дереве; **прочитано** — прочитано
в коде; **выведено** — заключение из первых двух.

## Что было сведено

`T1` перевела `-backgroundBlur` с `NoArg`/`bool` на `SepArg`/`BackdropMode` и
сменила формат дампа: `background_blur=` печатает имя режима, а не число
(**прочитано**: `lib/shitty/options.cpp` — `backdropModeName()`;
`lib/shitty/test_mode.cpp:2590` — единственное место печати;
`tst/harness.py:316` — `options()` больше не прогоняет `int()` по всем полям).
`T2` завела в `tst/test_config.py` сторожа чётности примерных конфигов.
Правило «две задачи одной волны не владеют одним файлом» оставило обе половины
несведёнными; они сведены здесь.

### Половина первая — четыре утверждения

Все четыре в `ConfigFileTest`, `tst/test_config.py`:

| Тест | Строка | Было | Стало |
|---|---|---|---|
| `test_translucency_defaults_to_the_opaque_window` | `:387` | `0` | `"off"` |
| `test_translucency_comes_from_the_config` | `:402` | `1` | `"blur"` |
| `test_the_command_line_beats_a_configured_opacity` | `:422` | `0` | `"off"` |
| `test_blur_without_translucency_warns_and_still_starts` | `:434` | `1` | `"blur"` |

Пятое место, `assertNotIn(b"-backgroundBlur", stderr)` в
`test_a_translucent_background_leaves_the_blur_warning_unsaid`, правки не
потребовало и не тронуто — как и было измерено `T1`.

Отдельно `:418`: `extra_arguments` несли `+backgroundBlur`, форму, которую
`SepArg` теперь отказывает (**прочитано**: `options.cpp`, ветка
`if (!enabled) raiseError(... "'+' is invalid here" ...)`). Переписано на
`("-backgroundOpacity", "70", "-backgroundBlur", "off")`.

Конфиг в этом тесте оставлен со старым написанием `backgroundBlur = true`
намеренно: `true` уцелел алиасом `blur` (**прочитано**:
`OptionsParser::getBackdropMode`), поэтому пара «конфиг во флаговом написании +
командная строка в новом» теперь заодно закрепляет, что конфиг, написанный до
`T1`, по-прежнему стартует, и что командная строка его перебивает. Причина
записана комментарием в самом тесте — иначе следующий читатель примет старое
написание за недосмотр и «починит» его, потеряв покрытие алиаса.

### Половина вторая — расхождение конфигов и снятие разрешения

`bin/st/shitty.toml:87` нёс строку `# CLI: -vulkanBlit / +vulkanBlit — …`,
а `bin/pt/pretty.toml` — нет. Опция не брендозависима, текст строки не содержит
ни одного брендового слова, поэтому строка добавлена дословно, на то же место в
порядке файла — сразу за `-vulkanInfo`. **Измерено**: после правки строка стоит
в обоих файлах на строке `87`, а число строк `# CLI:` в обоих — `77`
(до правки `77` и `76`).

`DOCUMENTATION_ALLOWANCE` опустошён той же правкой. Это не косметика: обе
половины обязаны идти вместе, потому что каждая по отдельности краснит.

- строка без снятия ключа → краснеет проверка осиротевшего ключа (мутация 3);
- снятие ключа без строки → краснеет сама чётность (мутация 4).

Комментарий над `DOCUMENTATION_ALLOWANCE` переписан: он больше не описывает
конкретное расхождение (его нет), а держит форму ключа и правило, что запись
не переживает своей причины.

## Доказательство: четыре мутации

Все мутации ставились в `bin/pt/pretty.toml` и `tst/test_config.py` и
откатывались из копии файла до правки. Дерево после каждой пары
«мутация → откат» сверялось `git status --short`.

### 1. Прямое — правка снимает ровно те красные, что были названы

**Измерено.** Полный питоновский набор в дереве с правкой:

```
Ran 6607 tests in 132.618s
FAILED (errors=14, skipped=23, expected failures=549)
```

`failures=0`. Поимённая сверка с эталоном
`reds-master.txt` (снят на `master` тем же окружением, 14 имён):

```
diff <(sort reds-master.txt) F1-reds-after.txt   →   пусто
```

Список красных **совпал с эталоном имя в имя**. `Ran` больше эталонного `6604`
на три — ровно три теста сторожа `T2`.

`test_config` целиком: `Ran 31 tests … OK`. Пять тестов `translucency`
поимённо — все `ok`.

`DOCUMENTATION_ALLOWANCE = {}` — `tst/test_config.py:53`.

### 2. Сторож ключей не ослеп

Убран ключ `login = false` из `bin/pt/pretty.toml`.

**Измерено** — покраснел `test_both_brands_assign_the_same_option_keys`,
и назвал ключ:

```
AssertionError: Items in the first set but not the second:
'login' : example configs disagree on which options they set;
only in bin/st/shitty.toml: ['login']; only in bin/pt/pretty.toml: []
```

Два других теста сторожа остались зелёными (`F..`). Откачено.

### 3. Разрешение работоспособно после опустошения

В `DOCUMENTATION_ALLOWANCE` заведён ключ на несуществующее отсутствие:
`{"bin/pt/pretty.toml": {"fontsize"}}` — `fontsize` документирован в обоих
файлах, то есть в `missing` его нет.

**Измерено** — покраснел `test_both_brands_document_the_same_options`
именно осиротевшим ключом, а не чётностью:

```
AssertionError: Lists differ: ['fontsize'] != []
… DOCUMENTATION_ALLOWANCE names ['fontsize'] as missing from
bin/pt/pretty.toml, but they are documented there now - delete the entry
```

Это и был смысл мутации: снятие ключа не отключило саму защиту от
осиротевших ключей. Откачено.

### 4. Обратное к добавленной строке

Из `bin/pt/pretty.toml` убрана добавленная строка `# CLI: -vulkanBlit …`
при пустом разрешении.

**Измерено** — покраснел `test_both_brands_document_the_same_options`,
и назвал имя:

```
AssertionError: Lists differ: ['vulkanBlit'] != []
… bin/pt/pretty.toml documents fewer options than its counterpart;
undocumented there: ['vulkanBlit']
```

Откачено. Контрольный прогон после отката: `Ran 31 tests … OK`.

## Прочие проверки

**`unit_tests`.** Собран `./build unit_tests pty_test_helper`, прогнан с
`SHITTY_PTY_TEST_HELPER` и `< /dev/null`:

```
OK: 982      EXIT=0
```

Не изменился относительно эталона задачи.

**`toml_dump` на обоих конфигах.** Оба `EXIT=0`, оба отвечают `ok`. Дампы
различаются **ровно одним** полем — `"title"`: `Shitty` против `Pretty`.
`backgroundBlur` в обоих: `{"type":"string","value":"off"}`.

**Симлинки.** Перед прогоном проверены все шесть (`st_test`, `pt_test`,
`toml_dump`, `example`, `unit_tests`, `pty_test_helper`) — живые. Ловушка из
`CLAUDE.md` («битый `.build/*_test` даёт лавину ложных ошибок молча») здесь не
сработала бы: числа сошлись с эталоном.

## Что осталось за границами задачи

- `lib/shitty/**` не тронут — сведение чисто тестовое и конфигурационное.
- `bin/st/shitty.toml` не тронут: расхождение закрывалось со стороны `pt`.
- Ветка не пушилась и не мержилась в `master`.

## Наблюдение на будущее

Комментарий `T2` над разрешением предсказал ровно ту связку, которая
подтвердилась мутациями 3 и 4: разрешение, снимаемое отдельно от правки,
краснеет с обеих сторон. Форма «ключ = файл + имя», взятая из
`lib/vterm/check_includes.py`, оказалась дешевле счётчика и здесь: мутация 3
проверяема **только** потому, что ключ именной — на счётчике «одно разрешённое
отсутствие в файле» подстановка `vulkanBlit → fontsize` прошла бы молча.

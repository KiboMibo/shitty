# T1. Опция `backgroundBlur` со значением

- **Дата:** 2026-09-05
- **Ветка:** `feat/T1-backdrop-mode`, от `master` `73fb61f3`
- **Коммит правки:** `49bef3fe`
- **План:** `docs/plans/2026-09-05-liquid-glass.md`, раздел 7, задача `T1`

Пометки: **[и]** измерено, **[п]** прочитано, **[в]** выведено.

## Что сделано

`backgroundBlur` из `OptionKind::NoArg` стал `OptionKind::SepArg` и несёт `BackdropMode` — `Off` | `Blur` | `Glass`. Разбор принимает `off`/`false`, `blur`/`true`, `glass`; прочее отвергает сообщением, называющим все три значения.

Псевдонимы `true`/`false` — не украшение: каждый конфиг, написанный со времён `T10`, пишет этот ключ булевым, и отказ от псевдонимов превратил бы рабочий конфиг в отказ стартовать.

| Файл | Что изменено |
|---|---|
| `lib/shitty/options.h` | `enum class BackdropMode: u8`, поле сменило тип, объявлен `backdropModeName()` |
| `lib/shitty/options.cpp` | строка таблицы → `SepArg`, `hardDefault` `"off"`, справка называет три значения; `getBackdropMode()`; предупреждение расширено на оба непустых режима; обе отбивки `SepArg` получили подсказку формы; определение `backdropModeName()` |
| `lib/shitty/test_mode.cpp` | дамп `background_blur=` печатает имя |
| `lib/shitty/application.cpp` | мост в слой платформы: `!= BackdropMode::Off` |
| `lib/shitty/options_ut.cpp` | компилируемость + запрещённая форма `+backgroundBlur` в argv переписана |
| `tst/harness.py` | `options()` больше не зовёт `int()` на нечисловом поле |
| `bin/st/shitty.toml`, `bin/pt/pretty.toml` | значение, комментарий, строка `# CLI:` — **одинаково в обоих** |

Слой платформы **не тронут вовсе**. `ext/plt/platform_cocoa.mm:1281` читает `WindowOptions::backgroundBlur` (`ext/plt/window.h:170`), а не `Options`; мост между ними — `application.cpp:1171`. Правка моста оставляет границу `lib/shitty` → `ext/plt` нетронутой. Решение подтверждено командиром (`ae123f7c`).

## Три решения, которые стоит назвать

**1. Подсказка формы сделана общей, а не про `backgroundBlur`.** Вчера `+backgroundBlur` и голое `-backgroundBlur` были законны, сегодня нет; сообщение `'+' is invalid here` само по себе оставляет пользователя без выхода. Обе отбивки `SepArg` теперь дочитывают `hardDefault` самой опции:

```
Error: +backgroundBlur: '+' is invalid here; -backgroundBlur takes a value, as in -backgroundBlur off
Error: -backgroundBlur: missing value; -backgroundBlur takes a value, as in -backgroundBlur off
```

Частный случай не заводился: подсказка выводится из таблицы и работает для любой `SepArg`-опции с жёстким умолчанием. `tst/test_options.py:32-33` проверяет эти строки через `assertIn` на подстроку — обе живут **[и]**.

**2. Текст предупреждения переписан.** Было «has nothing **to blur**», что неверно для `glass`. Стало «has nothing **to show**». Оба утверждения `test_config.py:220-222` — `assertIn(b"-backgroundBlur")` и `assertIn(b"-backgroundOpacity")` — переживают правку **[и]**.

**3. `WindowOptions` остался булевым.** До волны 2 на той стороне нечего различать: `platform_cocoa.mm` умеет ровно одну подложку. Расширение `window.h` отдано `T3` — так волна 2 меняет слой платформы один раз, а не дважды.

## Одиннадцать критериев

| № | Критерий | Результат |
|---|---|---|
| 1 | три значения разбираются | **сходится [и]** |
| 2 | неизвестное значение — ошибка, называющая три | **сходится [и]** |
| 3 | `true` → `Blur`, `false` → `Off` в TOML | **сходится [и]** |
| 4 | голое `-backgroundBlur` и `+backgroundBlur` — внятная ошибка с подсказкой | **сходится [и]** |
| 5 | `toml_dump` на обоих конфигах | **сходится [и]**, `EXIT=0` оба |
| 6 | дамп печатает `off\|blur\|glass` | **сходится [и]** |
| 7 | оба конфига несут ключ | **сходится [и]**, обе строки — `:24` |
| 8 | `./build st pt --clear` | **сходится [и]**, **235** узлов, `EXIT=0` |
| 9 | `unit_tests` | **сходится [и]**, `OK: 982`, `EXIT=0`, эталон `OK: 982` |
| 10 | питоновский набор | **не сходится, ожидаемо [и]** — 4 красных сверх эталона, все четыре в `tst/test_config.py`, отданы `F1` |
| 11 | `tst/pretty_binary_branding.py` | **сходится [и]**, `EXIT=0` прямым запуском |

### 1. Три значения на командной строке **[и]**

```
-backgroundBlur off   -> background_blur=off
-backgroundBlur blur  -> background_blur=blur
-backgroundBlur glass -> background_blur=glass
(без опции)           -> background_blur=off
```

### 2. Неизвестное значение **[и]**

```
$ st -backgroundBlur нечто
Error: -backgroundBlur: expected off, blur or glass!
Try -help for usage options.
rc=255
```

### 3. Псевдонимы в TOML **[и]**

```
backgroundBlur = true     -> background_blur=blur
backgroundBlur = false    -> background_blur=off
backgroundBlur = "glass"  -> background_blur=glass
```

### 4. Обе сломанные формы **[и]**

```
$ st -backgroundBlur
Error: -backgroundBlur: missing value; -backgroundBlur takes a value, as in -backgroundBlur off
rc=1

$ st +backgroundBlur
Error: +backgroundBlur: '+' is invalid here; -backgroundBlur takes a value, as in -backgroundBlur off
rc=1
```

### 5. `toml_dump` **[и]**

Оба файла — `EXIT=0`, строка `ok`. Ключ виден в обоих дампах одинаково:

```
"backgroundBlur":{"type":"string","value":"off"}
```

### 6. Дамп режима теста **[и]**

`background_blur=off`, `background_blur=blur`, `background_blur=glass` — по одному на режим, имя, не число.

### 7. Оба конфига **[и]**

```
bin/st/shitty.toml:24:backgroundBlur = "off"
bin/pt/pretty.toml:24:backgroundBlur = "off"
```

Блок комментария выше — байт в байт одинаковый в обоих файлах; проверено `diff` двух файлов с вычетом брендовых строк **[и]**. Это та мина, у которой, по измерению `M8c`, нет ни одного наблюдателя: зелёный прогон про неё не говорит ничего, поэтому обе строки показаны здесь глазами.

### 8. Сборка **[и]**

`./build st pt --clear` — **235** узлов, `EXIT=0`. Эталон брифа `231` снят для `./build st --clear` (одна цель), сравнивать напрямую нельзя.

`./build unit_tests pty_test_helper` — 133 узла, `EXIT=0`.

### 9. `unit_tests` **[и]**

```
OK: 982
EXIT=0
```

Эталон `master` `73fb61f3` — `OK: 982`, `EXIT=0`. Потеряно **0**, добавлено **0**, красных **0**.

Поимённая часть **[в]**: единственный тронутый `*_ut.cpp` — `options_ut.cpp`, и `git diff master -- '*_ut.cpp' | grep STD_TEST` не даёт ни одной строки. Ни одно имя теста не заведено и не удалено; счёт совпал с эталоном.

Код возврата проверен явно, не последняя строка: `EXIT=139` и `EXIT=148` итоговой строки не печатают вовсе.

### 10. Питоновский набор **[и]**

```
Ran 6604 tests
failures=3, errors=15, skipped=23, expected failures=549
```

Эталон `master`: `Ran 6604`, `errors=14`, `failures=0`, `skipped=23`.

Поимённая сверка с `reds-master.txt` — **потеряно 0, добавлено 4**:

| Тест | Разряд | Почему |
|---|---|---|
| `test_config.ConfigFileTest.test_translucency_defaults_to_the_opaque_window` | **ожидание переписано** | `:174` ждёт `0`, дамп даёт `"off"` |
| `test_config.ConfigFileTest.test_translucency_comes_from_the_config` | **ожидание переписано** | `:189` ждёт `1`, дамп даёт `"blur"` |
| `test_config.ConfigFileTest.test_the_command_line_beats_a_configured_opacity` | **ожидание переписано** | `:202` ждёт `0` **и** строит `extra_arguments` с `+backgroundBlur` — форма, которую `SepArg` запрещает |
| `test_config.ConfigFileTest.test_blur_without_translucency_warns_and_still_starts` | **ожидание переписано** | `:214` ждёт `1`, дамп даёт `"blur"` |

Починено **0**, пропущено **0**. Все четыре — в `tst/test_config.py`, который в волне 1 принадлежит `T2`; сведение отдано `F1` (`ae123f7c`, `e9b4ac21`). Пятое место, `:228` `assertNotIn(b"-backgroundBlur", stderr)`, переписки не потребовало и осталось зелёным **[и]** — предсказание подтвердилось.

`skipped` совпал: 23 = 23. Число `expected failures=549` эталоном не снималось.

`test_embed_example.py` отдельным прогоном — **109/109**, `OK` **[и]**.

`tst/production_surface.py` со своим окружением — **5/5**, `OK` **[и]**.

### 11. Брендирование **[и]**

```
$ python3 tst/pretty_binary_branding.py .build/pt
EXIT=0
```

Прямым запуском, не по штампу `./build`: кеш адресуется содержимым и подставил бы готовый штамп.

## Обнаружено

**1. Путь опции длиннее шести мест — три файла сверх плана.** Найдено чтением до первой правки, доложено командиру, подтверждено им (`ae123f7c`, раздел «Поправки по ходу»). Кратко:

- `platform_cocoa.mm` читает `WindowOptions`, не `Options`; мост в `application.cpp:1171`;
- `options_ut.cpp:480,512` не собирается с `enum class`, и один тест строит argv с `+backgroundBlur`;
- `tst/harness.py:324` делает `int(value)` для **каждого** поля дампа.

**2. Третья находка — дефект контракта, а не правки, и он был опаснее двух других.** «Дамп печатает имя» уронил бы не тест про размытие, а **каждый** тест, зовущий `terminal.options()`: `int("off")` — `ValueError` в самом разборе ответа. Контракт сохранён (имя читаемее числа), расширен `harness.py`. Числовые поля ведут себя как прежде.

**3. Наблюдателя у подсказки формы нет.** Строки `; -NAME takes a value, as in -NAME off` не проверяет ни один тест: `tst/test_options.py:32-33` смотрит только на подстроки `missing value` и `'+' is invalid here`, которые уцелели бы и при выброшенной подсказке. Мутация «убрать хвост» не покраснит ничего. Кандидат для `R1-test`.

**4. `hardDefault` опции сменился с `"false"` на `"off"` и виден в справке.** `-help` печатает `(default: off)`, чего не печатал раньше: строка `printUsage()` (`options.cpp`) выводит умолчание только для не-`NoArg`. То есть смена вида опции сама по себе добавила строке справки хвост. Наблюдателя у текста справки в наборе нет — `R1-qa` проверяет его глазами по своему пункту.

**5. Предупреждение переписано, и его половина не проверяется.** `test_config.py` проверяет в тексте только два имени опций. Смысловая часть — «has nothing **to show**» вместо «to blur» — не покрыта ничем; мутация обратно в «to blur» не покраснит ни одного теста. Не блокирует: неверно оно было бы только для `glass`, который до волны 2 всё равно ведёт себя как `blur`.

**6. `bin/pt/pretty.toml` не несёт строку `# CLI: -vulkanBlit`,** и это верно: опция помечена «command line only». Замечено при `diff` двух конфигов; расхождением не является, но сторожу `T2`, если он сравнивает **комментарии**, а не ключи, эта строка попадётся. Ключей `vulkanBlit` нет ни в одном файле.

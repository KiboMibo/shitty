Исходный commit-level coverage report взят из успешного run `31348904198` для `14217426`. GitHub ZIP требует авторизацию, поэтому связанный отчёт был скачан через публичный Codecov API в `/tmp/shitty-codecov-14217426.json`. Сам CI run и HTML-артефакт видны [здесь](https://github.com/pg83/shitty/actions/runs/31348904198), исходные данные — в [Codecov API](https://api.codecov.io/api/v2/github/pg83/repos/shitty/report/?sha=14217426dc908dac07c2031e1574a4a550435037).

Цифры ниже — исходный snapshot до агрегации профилей всех test executables. Актуальные цифры публикует CI; вручную переносить их сюда не нужно.

Итог:

- 79 файлов;
- 19 439 исполняемых строк;
- 15 772 покрыто;
- 2 077 не исполнено;
- 1 590 строк покрыто частично;
- line coverage: 81.13%.

### Реальные дыры

| Файл | Coverage | Miss / partial | Оценка |
|---|---:|---:|---|
| `font_freetype.cpp` | 70.72% | 83 / 78 | Средний приоритет |
| `session.cpp` | 85.55% | 20 / 18 | В целом хорошо |
| `screen.cpp` | 85.00% | 180 / 234 | Огромный файл, нужны свойства |
| `vterm.cpp` | 88.50% | 320 / 282 | Хорошее поведенческое покрытие |
| `parser.cpp` | 96.56% | 20 / 37 | Трогать последним |

### Рекомендуемый порядок

1. `screen.cpp` и `vterm.cpp`

У них хорошие проценты, но много абсолютных partial branches. Здесь выгоднее property/model tests:

- resize/reflow + wide grapheme + hyperlink + selection;
- rectangle scrolling с защищёнными и wide cells;
- случайные последовательности resize/scroll/write с проверкой инвариантов;
- сравнение с медленной reference model.

Добавлять отдельный тест на каждую красную строку не нужно.

### Coverage policy

- завести отдельные components: `core`, `platform-wayland`, `renderer-vulkan`;
- для `core` сделать patch coverage обязательным;
- платформенные compile/version branches оставить informational и проверять distro matrix;
- не исключать `pty.cpp` или `input.cpp` ради красивого общего процента;
- обновить [tests/COVERAGE.md](/home/pg/monorepo/shitty/tests/COVERAGE.md:1): он всё ещё утверждает, что Wayland/Vulkan требуют будущей platform boundary, хотя fake Wayland compositor и Vulkan harness уже существуют.

Главный следующий пробел — property/model tests для `screen.cpp` и `vterm.cpp`.

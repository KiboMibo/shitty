Вывод: покрытие у Zutty уже широкое, но заметно поверхностное. Многие возможности проверены одним happy-path, а матрицы параметров, переходы состояний и взаимодействия между подсистемами почти не зафиксированы. Удвоение здесь оправдано и получится содержательным.

## Текущая база

Сейчас:

- 416 тестов;
- 31 файл `test_*.py`;
- 5025 строк непосредственно тестов;
- 846 assertions;
- 5810 строк всего Python в `tests/`, включая harness и утилиты.

Сборка уже использует `tests/*.py` и `unittest discover`, поэтому количество файлов ничем не ограничено: [build.py](/home/pg/monorepo/zutty/build.py:50). Список будущих имён фиксировать не буду. Останется только требование плоской `tests/`.

Сильнее всего покрыт scrollback — 28 тестов и 520 строк. Слабее всего:

- PTY — 4 теста;
- resize — 4;
- renderer/present contract — 4;
- selection — 5;
- OSC — 5;
- printer — 3.

При этом [COVERAGE.md](/home/pg/monorepo/zutty/tests/COVERAGE.md) несколько переоценивает покрытие: перечисление «поддержано» часто означает один проверенный вариант.

## Главные пробелы

1. PTY, presentation и lifecycle

Сейчас практически отсутствуют:

- последовательность, разрезанная между чтениями;
- drain до `EAGAIN`;
- граница 20 MiB и fairness;
- failed present, после которого пришло ещё damage;
- cursor-only и selection-only damage;
- synchronized update через несколько PTY reads;
- timeout synchronized output;
- resize между failed present и retry;
- отсутствие spurious frames при неполной escape sequence.

Synchronized output должен продолжать менять модель терминала, сохраняя предыдущую представленную картинку до `2026l`: [protocol specification](https://github.com/contour-terminal/vt-extensions/blob/master/synchronized-output.md).

2. Options, fonts и startup

Это почти белое пятно:

- приоритет CLI над `ZUTTY_FONT_SIZE`, env над default;
- значения 1/255 и выход за границы;
- trailing garbage вроде `16wat`;
- geometry, colors, boolean `+/-`, abbreviations и ambiguity;
- `-e` как окончание разбора;
- shell/login argv;
- `TERM`, `ZUTTY_VERSION`, winsize и SIGWINCH;
- font path traversal;
- fontconfig fallback;
- regular/bold/italic/bold-italic selection;
- несовместимые metrics;
- PCF/PCF.gz;
- double-width fallback;
- отсутствие font directory.

## Что потребуется от harness

Большую часть протокольных тестов можно писать уже сейчас. Для остального добавлю узкие platform-neutral seams и control-команды:

- clipboard ownership;
- damage snapshot;
- present success/failure;
- PTY fault/backpressure injection;
- option parsing без запуска GLFW;
- font resolver с временным деревом файлов.

Offscreen Vulkan на этом проходе не нужен: логический raster contract можно фиксировать отдельно, пиксельные golden tests оставить графическому этапу.

## Количественная цель

Оставшийся ориентировочный прирост:

| Область | Новых тестов |
|---|---:|
| Charsets | 20 |
| Resize/selection/scrollback interactions | 25 |
| PTY/present/options/fonts/startup | 30 |
| Итого | около 75 |

То есть итог, вероятно, будет ближе к 570 тестам, а не ровно к формальному удвоению.

Имена и существующая раскладка файлов не являются ограничением. Рабочая единица — строка спецификации, состояние автомата или внешний контракт. На каждую такую единицу: минимальный тест, варианты параметров и хотя бы один граничный либо ошибочный сценарий. Уже вижу несколько мест, где новые тесты должны сразу покраснеть: полный UAX #29 и строгий разбор числовых опций.

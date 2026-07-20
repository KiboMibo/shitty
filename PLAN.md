Вывод: покрытие у Zutty уже широкое, но заметно поверхностное. Многие возможности проверены одним happy-path, а матрицы параметров, переходы состояний и взаимодействия между подсистемами почти не зафиксированы. Удвоение здесь оправдано и получится содержательным.

## Текущая база

Сейчас:

- 408 тестов;
- 30 файлов `test_*.py`;
- 4893 строки непосредственно тестов;
- 833 assertions;
- 5678 строк всего Python в `tests/`, включая harness и утилиты.

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

1. DCS, OSC и window operations

Каждая ветка сейчас требует собственной таблицы:

- OSC 133: A/B/C/D, параметры и неправильный порядок;
- XTWINOPS: все операции 11–23, оба варианта 14, title selectors, пустой stack и rollover после десяти элементов.

Текущий OSC 99 реализует только часть опубликованного протокола; тесты должны чётко зафиксировать заявленный subset и не позволять отвечать поддержкой того, чего нет: [kitty notifications](https://sw.kovidgoyal.net/kitty/desktop-notifications/).

2. Keyboard и mouse

24 keyboard-теста — мало относительно таблицы реализации.

Нужны:

- все F1–F20 и позднее весь реально поддерживаемый диапазон;
- arrows/navigation/keypad в normal/application/VT52;
- каждый modifier и сочетания;
- Ctrl mapping всей ASCII-таблицы;
- Alt/eight-bit/`altSendsEscape`;
- modifyOtherKeys resources и значения;
- kitty flags отдельно и в комбинациях;
- press/repeat/release;
- shifted/base-layout/associated text;
- primary и alternate kitty stacks;
- stack depth, overflow и pop count.

Kitty требует согласованной реализации progressive enhancements, а не нескольких показательных клавиш: [официальная keyboard specification](https://sw.kovidgoyal.net/kitty/keyboard-protocol/).

Критичная дыра: wheel-тесты обходят frontend. Они вызывают `Vterm::mouseWheelUp/Down()` напрямую, поэтому не проверяют код дробных дельт и накопления в [main.cpp](/home/pg/monorepo/zutty/main.cpp:1251). Нужны control-команды уровня реальных событий:

- `SCROLL x y`;
- движение pointer;
- button press/release;
- modifiers;
- время между кликами;
- content scale и framebuffer coordinates.

Так мы закроем fractional scrolling, смену reporting/local scrolling, Shift override, cell dedupe, horizontal wheel и double/triple click.

3. Unicode и charsets

Текущий grapheme-код реализует выбранный subset правил, но не полный UAX #29. Нужны представительные официальные vectors:

- Prepend;
- SpacingMark;
- Hangul;
- regional indicators;
- emoji modifiers;
- Extended Pictographic + ZWJ;
- variation selectors;
- keycaps;
- orphan combining/ZWJ/VS;
- malformed UTF-8;
- cluster на правой границе;
- insert/delete/resize/selection внутри cluster.

Unicode публикует и правила, и официальный `GraphemeBreakTest.txt`, поэтому варианты не придётся придумывать вручную: [UAX #29](https://unicode.org/reports/tr29/), [Unicode 17 test data](https://www.unicode.org/Public/17.0.0/ucd/auxiliary/).

Отдельная матрица нужна для G0–G3, GL/GR, locking/single shifts, NRC sets, DEC Special/Technical и возврата из VT52.

4. Resize, selection и scrollback

Scrollback дальше раздувать просто ради числа не надо. Добавлять только новые взаимодействия:

- wide/grapheme cells при resize;
- selection через wrapped lines;
- reverse/rectangular selection;
- word snapping с Unicode и пунктуацией;
- selection после insert/delete/erase;
- selection на primary/alternate;
- cursor/autowrap при shrink/grow;
- margins и tab stops после resize;
- history capacity при повторяющихся resize;
- resize при поднятом viewport;
- same-grid pixel-only resize;
- in-band resize response.

5. PTY, presentation и lifecycle

Сейчас практически отсутствуют:

- EOF/HUP/error;
- child exit status и signal exit;
- partial output writes и backpressure;
- порядок `POLLIN/POLLOUT`;
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

6. Options, fonts и startup

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

- raw frontend key/mouse/scroll events;
- virtual clock для click counting и blink;
- clipboard ownership;
- window info/iconified/content scale;
- damage snapshot;
- present success/failure;
- PTY fault/backpressure injection;
- option parsing без запуска GLFW;
- font resolver с временным деревом файлов.

Offscreen Vulkan на этом проходе не нужен: логический raster contract можно фиксировать отдельно, пиксельные golden tests оставить графическому этапу.

## Количественная цель

Я бы зафиксировал не потолок, а нижнюю границу:

- не менее 450 тестов, ожидаемо около 500;
- не менее 5500 строк именно `test_*.py`;
- не менее 900 содержательных assertions;
- сколько потребуется плоских файлов — столько и будет.

Ориентировочный прирост:

| Область | Новых тестов |
|---|---:|
| DCS/OSC/window protocols | 14 |
| Keyboard/keypad | 45 |
| Mouse/frontend events | 35 |
| Unicode/charsets | 30 |
| Resize/selection/scrollback interactions | 25 |
| PTY/present/options/fonts/startup | 30 |
| Итого | около 179 |

То есть итог, вероятно, будет ближе к 550–560 тестам, а не ровно к формальному удвоению.

Имена и существующая раскладка файлов не являются ограничением. Рабочая единица — строка спецификации, состояние автомата или внешний контракт. На каждую такую единицу: минимальный тест, варианты параметров и хотя бы один граничный либо ошибочный сценарий. Уже вижу несколько мест, где новые тесты должны сразу покраснеть: полный UAX #29, frontend wheel accumulation и строгий разбор числовых опций. Исходники в этом проходе не менял.

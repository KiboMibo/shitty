# Протокольный аудит Zutty

Итог: у Zutty уже широкий рабочий VT/xterm-фундамент, достаточный для Codex и большинства обычных TUI. Но до честной совместимости с `xterm-256color` ещё далеко. Главная проблема не количество отсутствующих протоколов, а parser и несколько неверно реализованных уже заявленных функций.

Аудировал parser, CSI/ESC/DCS/OSC, DEC modes, SGR, keyboard, mouse, ответы терминала и текущие 125 тестов. Подозрительные случаи отдельно воспроизвёл через headless harness. Код не менял.

## 1. Что реализовано плохо или не закончено

### Критично: небезопасный индекс палитры

`SGR 38;5;N` и `48;5;N` не проверяют, что `N <= 255`. Значение напрямую становится индексом `palette256[N]` в [vterm.icc](/home/pg/monorepo/zutty/vterm.icc:1680).

Последствия:

- чтение за границей массива;
- возможный crash терминала от вывода недоверенной программы;
- потенциальная утечка прочитанных байтов через ошибочный ответ `OSC 10;?`.

Это граница безопасности: escape sequences могут прийти через SSH, `cat` чужого файла или лог.

### Критично: parser печатает остатки неизвестных протоколов

Parser построен как набор специализированных состояний, а не как общий ECMA-48 parser параметров/intermediates/final.

Примеры, подтверждённые тестовым harness:

- `CSI ? 1 $ p` — неподдержанный DECRQM печатает букву `p`.
- `CSI ... $ x` — неподдержанная прямоугольная операция печатает `x`.
- Kitty graphics `ESC _ G... ESC \` печатает на экран `G` и base64 payload.
- PM `ESC ^ ... ST` и SOS `ESC X ... ST` печатают содержимое как текст.
- При переполнении 4095-байтного OSC/DCS буфера parser возвращается в normal state, поэтому хвост строки тоже попадает на экран.

Правильная архитектура: собирать parameter bytes, intermediate bytes и final byte отдельно; неизвестную полную последовательность молча игнорировать. Неизвестные строковые протоколы надо пропускать до ST.

Это необходимо сделать раньше расширения списка протоколов.

### Критично: `TERM=xterm-256color` обещает несуществующие функции

Zutty безусловно выставляет `TERM=xterm-256color` в [main.cpp](/home/pg/monorepo/zutty/main.cpp:359). Локальная terminfo при этом рекламирует:

- `dim`, `blink`, `invis`;
- изменение и сброс палитры `initc`/`oc`;
- visual bell через `DECSCNM`;
- printer controls;
- meta mode `?1034`;
- другие xterm-функции.

В Zutty они игнорируются, логируются как unimplemented или работают иначе. Например, `SGR 2` не делает dim, а просто снимает bold.

Нужно либо довести обязательства `xterm-256color`, либо завести честную `zutty` terminfo и отдельно решить проблему её доступности на удалённых хостах.

### Неверные базовые операции

Подтверждённые расхождения:

- `CSI 3 J` должен удалить scrollback, но сейчас заодно очищает live screen из-за fallthrough в `case 2`.
- `RIS` внутри alternate screen очищает alt, возвращается в primary и оставляет старый primary-контент. Hard reset должен сбросить весь терминал.
- `DECSTR` не восстанавливает вертикальные и горизонтальные margins.
- `DECRC` и SCO `CSI u` одноразовые: restore удаляет сохранённое состояние. Повторный restore уже ничего не делает.
- `IL` и `DL` выполняют лишний carriage return и меняют колонку курсора.
- `DECSTBM` не поддерживает `CSI top r`, где bottom должен по умолчанию стать последней строкой.
- Аналогичный дефект есть у `DECSLRM` с одним параметром.
- CPR при origin mode учитывает верхний margin, но не вычитает левый. При левом margin 3 home-position сообщается как колонка 3 вместо 1.
- Большие CSI-параметры сначала сужаются до `uint16_t`, а затем ограничиваются; вместо saturation получается wraparound.

Спецификация xterm явно описывает ED3, DECRQM, margins, rectangular operations и другие базовые последовательности: [XTerm Control Sequences](https://www.invisible-island.net/xterm/ctlseqs/ctlseqs.html).

### Неверные ответы терминала

`DECRQSS` реализован фактически только для `$q"p`, причём ответ неправильный:

```text
DCS 1 $ r 64;1;9;15;21;22c ST
```

Это Primary DA, заканчивающийся `c`. Ответ на запрос DECSCL должен описывать DECSCL и заканчиваться `"p`.

Не реализованы обычные DECRQSS-запросы:

- `m` — текущий SGR;
- `r` — vertical margins;
- `s` — horizontal margins;
- `SP q` — cursor style;
- `"q` — protection attribute.

DA тоже противоречив:

- Primary DA объявляет VT420 и возможности, часть которых фиктивна.
- Заявлены 132 columns, но DECCOLM окно и сетку не меняет.
- Secondary DA отвечает `>64;0;0c`, где `64` означает VT520, то есть identity не совпадает с Primary DA.
- Версия Zutty через протокол не сообщается.

### SGR и цвета

Рабочая часть: обычные 16/256 цветов, semicolon truecolor, bold, italic, underline, inverse.

Проблемы:

- colon syntax полностью отбрасывается: `38:2:...`, `48:2:...`, `4:3`.
- Нет underline styles и underline color `58/59`.
- `SGR 2` реализован как bold-off, а не faint.
- `SGR 10–19` ошибочно используются для переключения bold/italic; по стандарту это выбор альтернативного шрифта.
- Нет blink, conceal, strike, overline, faint.
- RGB-компоненты не валидируются.
- `OSC 4` умеет только один query; set, несколько пар и `OSC 104` отсутствуют.
- `OSC 10/11/12/17/19` умеют только query.
- `OSC 10;?` возвращает текущий SGR foreground, а должен возвращать default dynamic foreground. После `SGR 31` Zutty отвечает красным как «default foreground».
- `OSC 17/19` подменены обычными fg/bg вместо selection colors.

Актуальные xterm-варианты colon SGR и dynamic colors описаны в той же [спецификации xterm](https://www.invisible-island.net/xterm/ctlseqs/ctlseqs.html).

### Wide characters и Unicode

- Unicode выше BMP заменяется missing glyph.
- Combining characters полностью теряются.
- Нет grapheme clusters, variation selectors и ZWJ.
- Wide character в последней колонке сохраняется как одиночная широкая glyph без корректного переноса.
- Перезапись второй половины wide character не очищает первую: получается `界X`, где первая клетка всё ещё помечена double-width.
- Editing operations могут разрезать wide cells.
- UTF-8 decoder не полностью проверяет surrogates, overlong encodings и диапазон `> U+10FFFF`.

### OSC

Хорошо работают базовые `0/1/2`, `7`, `8` и `52`, но:

- OSC command искусственно ограничен `<= 120`. Поэтому `OSC 133`, `777`, `1337` отклоняются ещё parser-ом.
- OSC 8 игнорирует параметр `id=` и объединяет ссылки только по URI.
- Таблицы уникальных hyperlinks никогда не очищаются при выпадении строк из истории — память растёт всю сессию.
- OSC 52 `s` ошибочно трактуется как одновременная запись в primary и clipboard. У xterm это configurable selection.
- Ответ OSC 52 всегда возвращает пустой selector.
- Invalid base64 молча декодируется до первого плохого символа.
- Лимит OSC около 4 КиБ сильно ограничивает clipboard.
- Чтение clipboard через OSC 52 разрешено без подтверждения пользователя — это риск утечки clipboard удалённому процессу.
- Большой clipboard-response или paste пишется синхронно в PTY и способен заморозить UI.

### Keyboard

Legacy keyboard и первые три Kitty-флага в целом рабочие.

Kitty keyboard protocol поддержан только частично:

- флаги маскируются через `& 0x07`;
- нет flag 8 — report all keys;
- нет flag 16 — associated text;
- modifier keys не передаются;
- Enter/Tab/Backspace release обрабатываются не полностью по правилам progressive enhancement;
- alternate/base-layout keys завязаны на приближённую US-layout таблицу;
- `modifyOtherKeys` реализован только для resource 4, остальные XTMODKEYS фактически игнорируются;
- query/reset semantics XTMODKEYS отсутствуют.

Полный набор флагов 1, 2, 4, 8 и 16 определён в [Kitty keyboard protocol](https://sw.kovidgoyal.net/kitty/keyboard-protocol/).

### Mouse

Работают X10, VT200, button/any-event, UTF-8, SGR, URXVT, focus и wheel.

Не закончено:

- mode 1001 highlight tracking — пустая заглушка;
- mode 1016 SGR pixels отсутствует;
- extended buttons 10/11 разрешаются frontend-ом, но encoder возвращает пустую строку;
- legacy и UTF-8 coordinates не ограничиваются допустимым диапазоном;
- координаты за пределами grid могут отправляться приложению;
- нет mouse leave reporting.

### Synchronized output

`?2026h/l` работает и нужен Codex. Но нет watchdog: процесс, умерший после `?2026h`, может навсегда оставить окно без redraw. Спецификация признаёт отсутствие общего консенсуса по timeout, но прямо описывает этот риск: [Synchronized Output](https://contour-terminal.org/vt-extensions/synchronized-output/).

## 2. Какие протоколы отсутствуют

### Важные для современных TUI

- DECRQM/DECRPM — запрос состояния ANSI/DEC modes.
- Полный DECRQSS.
- XTGETTCAP.
- XTVERSION.
- Tertiary DA.
- C1 8-bit controls и реальные S7C1T/S8C1T.
- XTWINOPS: сейчас `CSI t` — пустая функция; нет query размеров окна/cell grid и resize requests.
- OSC 133 semantic prompt/shell integration.
- OSC 9/99 notifications и progress reporting.
- Mouse 1016 pixel coordinates.
- In-band resize notifications.
- Полные Kitty keyboard flags.
- Xterm palette/dynamic-color set/reset.
- Title stack/query.

OSC 133 и семейство shell-integration описаны в [документации iTerm2](https://iterm2.com/documentation-escape-codes.html).

### Графика отсутствует полностью

- Sixel.
- Kitty graphics.
- iTerm2 inline images / multipart images.
- ReGIS.

Kitty graphics особенно важно сначала хотя бы корректно игнорировать: сейчас его APC payload отображается как текст. Сам протокол включает chunking, shared-memory/file transport, placements и quotas: [Kitty graphics protocol](https://sw.kovidgoyal.net/kitty/graphics-protocol/). iTerm2 images описаны отдельно в [официальной документации](https://iterm2.com/documentation-images.html).

### DEC/xterm completeness, низший приоритет

- DECSCA и selective erase DECSED/DECSEL.
- Rectangular operations: DECFRA, DECCRA, DECERA, DECCARA, DECRARA, checksum.
- Double-width/double-height line attributes.
- User-defined keys.
- Locator protocol.
- Printer controls.
- LED controls.
- Full NRCS family и DECNRCM.
- Reverse wraparound.
- ReGIS/Tektronix emulation.

Большую часть этого не обязательно реализовывать. Но нельзя рекламировать через DA/terminfo то, чего нет.

## Состояние тестов

125 headless-тестов дают хороший фундамент: streaming boundaries, CSI/OSC/DCS, basic editing, scrollback, resize, mouse, keyboard, Kitty 1/2/4, OSC 8/52.

Но сейчас нет:

- parser conformance для неизвестных intermediates и string types;
- C1;
- oversized sequences;
- invalid/огромных параметров;
- ASan/UBSan/fuzz;
- wide-cell boundary tests;
- reset внутри alt screen;
- повторного cursor restore;
- полного default-parameter matrix;
- differential traces против xterm/foot/kitty;
- актуального автоматического `vttest`.

Некоторые тесты фиксируют неправильное поведение как правильное:

- неверный DECRQSS reply;
- OSC 52 selector `s`;
- ED3 проверяет только исчезновение scrollback и не замечает очистку live screen.

## Рекомендуемый порядок работ

1. Закрыть OOB палитры и добавить sanitizer/fuzz tests.
2. Переписать parser на общий ECMA-48 dispatcher с корректным ignore/recovery.
3. Зафиксировать честную compatibility matrix и решить судьбу `TERM=xterm-256color`.
4. Исправить ED3, reset, cursor save/restore, IL/DL, margins, CPR, wide cells.
5. Исправить DA/DECRQSS и добавить DECRQM, XTGETTCAP, XTVERSION.
6. Доделать SGR/colors/OSC и Kitty keyboard 8/16.
7. Добавить OSC 133, mouse 1016 и window reports.
8. Отдельно решить, нужна ли графика. Если да — я бы сначала делал Kitty graphics; Sixel вторым. ReGIS, printer и locator можно не делать.

Главный вывод: добавлять новые протоколы поверх текущего parser пока рано. Сначала нужен корректный protocol core и честная декларация возможностей.

Идея правильная. Более того, текущий код уже дошёл до точки, где Ragel заменит не только большой `switch`, но и несколько последовательно работающих парсеров и промежуточные owning-строки.

Главное условие: Ragel должен стать единственным владельцем синтаксического состояния протокола.

## Что именно переносить

В одну Ragel statechart должны войти:

- ground/text;
- C0/C1;
- ESC и intermediates;
- CSI вместе с параметрами, `;`, `:`, private prefix и final byte;
- DCS вместе с DECRQSS, XTGETTCAP, DECUDK;
- OSC вместе с номерами команд и внутренними полями;
- SOS/PM/APC;
- VT52;
- выбор charset;
- printer controller;
- CAN/SUB, ST, BEL и перезапуск одной незавершённой последовательности другой;
- распознавание UTF-8 внутри строк, чтобы continuation byte из диапазона C1 не принимался за управляющий символ.

То есть нынешние `InputState`, `setState()`, `processCsiByte()`, `dispatchCsi()`, `handle_OSC()`, `handle_DCS()` и повторный разбор их аргументов должны исчезнуть.

Сейчас FSM занимает огромный участок [vterm.cpp](/home/pg/monorepo/shitty/vterm.cpp:9101), а после завершения DCS/OSC накопленный буфер превращается в `std::string` и ещё раз разбирается через `find`, `substr`, `from_chars`, `stringstream` — например [DCS](/home/pg/monorepo/shitty/vterm.cpp:5492) и [OSC](/home/pg/monorepo/shitty/vterm.cpp:5786). Именно этого при полной переделке быть не должно.

## Где проходит граница Ragel

Ragel делает:

- распознавание байтов;
- разделение полей;
- накопление чисел;
- hex framing;
- выбор команды;
- контроль лимитов;
- вызов действия в момент завершения законченной сущности.

Обычный C++ делает только семантику:

- поставить курсор;
- изменить атрибуты;
- записать клетки;
- сменить palette;
- послать ответ в PTY;
- уведомить host.

Например, обработчик `OSC 9;4;state;percent` получает уже два проверенных числа. Он не ищет `;` и не вызывает `from_chars`. Обработчик `OSC 8` получает уже разобранные параметры и URI. DECRQSS получает enum запроса, а не исходную строку.

Это всё ещё полная Ragel-машина: C++-обработчики не принимают решений о том, как интерпретировать входные байты.

## Потоковая модель

Состояние между `consume()` будет примерно таким:

```cpp
struct ProtocolParser {
    int cs;

    CsiParameter parameters[32];
    u8 parameterCount;

    u32 number;
    u32 payloadSize;

    Buffer scratch;
    // Небольшие POD-аккумуляторы текущей OSC/DCS-команды.
};
```

Никаких сохранённых указателей в PTY input и никаких `StringView` между вызовами. `p` и `pe` существуют только во время текущего `consume()`. `cs`, числа и незаконченная сущность переживают границу вызова.

`pe` нельзя считать концом последовательности: это только конец текущего PTY chunk. EOF-actions Ragel здесь почти нигде не нужны. `ESC` в конце одного вызова и `\` в начале следующего должны завершать ST как единая последовательность.

Один `scratch` допустим: одновременно парсится ровно одна протокольная сущность. После dispatch/cancel он сбрасывается. Но известные команды лучше разбирать потоково и вообще не складывать целиком в него.

OSC 99 multipart — отдельный случай: состояние notification между несколькими законченными OSC принадлежит семантике notification, а не parser scratch.

## Bulk fast path

Согласен: сохранить его несложно.

В ground-состоянии мы ищем границу обычного текста и передаём весь диапазон в `placeAsciiRun()` либо `placeAsciiLines()`. Ragel получает первый специальный байт и продолжает с него.

Только `memchr(ESC)` сам по себе недостаточен: останавливать блок нужно также на:

- C0, прежде всего CR/LF;
- DEL;
- байте `>= 0x7f`;
- иногда на границе, зависящей от charset/UTF-8 состояния.

Нынешний `printableAsciiPrefix()` уже SIMD-ом ищет первый байт вне `0x20..0x7e` [здесь](/home/pg/monorepo/shitty/vterm.cpp:8987). Его можно оставить как ускоренную реализацию Ragel-перехода `printable+`.

Для OSC/DCS будет аналогичный bulk scanner по стоп-байтам данного состояния: ESC, BEL для OSC, CAN/SUB, C0/C1 и потенциальные разделители конкретной подграмматики. В простых состояниях это действительно может быть `memchr`; при нескольких разделителях — SIMD range/mask scan.

Это не параллельный парсер: scanner не меняет состояние и не интерпретирует протокол, а лишь быстро исполняет повторяющийся переход DFA.

## Как организовать грамматику

Не стоит писать одну гигантскую регулярку. Нужна одна Ragel machine, собранная из именованных подмашин:

```text
ground
 ├─ escape
 │   ├─ csi
 │   ├─ dcs
 │   ├─ osc
 │   ├─ charset
 │   └─ vt52
 ├─ c0/c1
 ├─ utf8/text
 └─ printer
```

OSC лучше разводить непосредственно по литеральным префиксам:

```text
"8;"   → osc_hyperlink
"99;"  → osc_notification
"133;" → osc_shell
digit+ ";" → osc_unknown
```

Так не понадобится сначала собрать номер, затем сделать ещё один ручной parser switch.

CSI разумно оставить с общей машиной параметров, но final transitions привязать к семантическим actions. Дублировать грамматику параметров для каждой команды нельзя — раздуется DFA. Ragel разбирает общую структуру и на финальном переходе вызывает нужное действие по комбинации prefix/intermediate/final.

## Что обязательно удалить из семантических обработчиков

Сейчас многие `csi_*`, `esc_*` и `handle_*` сами вызывают `setState(Normal)`. Это создаёт два владельца автомата.

После переделки:

- только Ragel меняет `cs`;
- semantic action не знает состояния parser;
- reset/cancel выполняются Ragel actions;
- semantic handler не может оставить автомат в неожиданном состоянии.

Это одно из главных архитектурных улучшений, не только оптимизация.

## Производительность

Ожидаемые выигрыши:

- исчезнет огромный ручной control-flow;
- исчезнет повторное сканирование OSC/DCS;
- исчезнут `std::string`, `substr`, `stringstream` и большинство временных аллокаций;
- CSI будет разбираться одним проходом;
- escape-heavy workloads станут заметно дешевле;
- длинные OSC/DCS перестанут сначала копироваться целиком, а затем повторно читаться.

Но сам Ragel не даст двукратного ускорения обычного `cat`: printable path уже работает блоками, а основная цена дальше лежит в Screen. Основной выигрыш там обеспечит сохранённый bulk path. На управляющих последовательностях и OSC/DCS потенциал существенно выше.

Для генерации я бы измерил как минимум `-G2`, `-G1` и `-T1`. `-G2` даёт быстрый goto-driven код, но большая полная грамматика может раздуть `.text` и проиграть из-за I-cache. Выбирать режим надо по `st perf`, branch misses, instruction count и размеру generated function, а не по репутации `-G2`.

## Главные риски

- Неправильный action ordering при CAN/SUB/ESC/C1.
- Ошибки на границе двух `consume()`.
- Принятие UTF-8 continuation `0x80..0x9f` за C1.
- Взрыв размера DFA из-за наивного копирования CSI-грамматики.
- Потеря нынешнего ASCII batching.
- Неограниченное накопление неизвестных OSC.
- Смешение parser scratch с долгоживущим состоянием OSC 99.
- Незаметное изменение поведения malformed sequences.

Тестовая база для перехода уже хорошая: есть all-splits тесты, byte-by-byte OSC/DCS/CSI, UTF-8/C1, лимиты и deterministic fuzz, например [test_parser_states.py](/home/pg/monorepo/shitty/tests/test_parser_states.py:140) и [fuzz_parser.py](/home/pg/monorepo/shitty/tests/fuzz_parser.py:1).

## Итог

Я бы делал.

Но критерием завершения должно быть не «главный switch сгенерирован Ragel», а:

- в проекте больше нет `InputState`;
- нет `processCsiByte`/`dispatchCsi`;
- OSC/DCS не буферизуются для последующего строкового парсинга;
- никакие semantic handlers не управляют состоянием parser;
- один Ragel `cs` описывает весь ECMA-48/xterm/DEC/VT52 framing;
- bulk text по-прежнему обрабатывается диапазонами;
- старая FSM остаётся только временным differential oracle и удаляется перед завершением работы.

Иначе получится дорогой гибрид из двух автоматов, а не тот цельный дизайн, который ты предлагаешь.

---

## Детальный план реализации

### 0. Зафиксировать контракт старого parser

До первой замены кода нужно сохранить функциональный контракт и снять performance baseline:

- результат полного test target;
- результат `parser_fuzz`;
- performance baseline на packed corpus с текущей нарезкой 8 KiB, на `cat ~/2 ~/2 ~/2 ~/2` и на отдельном escape-heavy потоке.

Performance на этом этапе является диагностикой, а не блокирующим условием первой версии. Фиксируются:

- MiB/s;
- cycles и instructions;
- branches и branch-misses;
- icache misses;
- размер `.text`;
- flat/cumulative profile `VtermImpl::processInput`.

Существующие проверки разрезания последовательности между PTY chunks расширяются до таблицы обязательных границ:

- после каждого байта ESC, CSI, OSC и DCS introducer;
- между ESC и `\` в ST;
- между повторными ESC;
- внутри CSI-числа и перед каждым `;`/`:`;
- между байтами UTF-8, особенно когда continuation попадает в `0x80..0x9f`;
- внутри hex, base64 и percent encoding;
- внутри printer-controller terminator.

Старый parser служит differential oracle только во время разработки. В конечном дереве его копии не остаётся.

### 1. Добавить Ragel в build graph

Исходником автомата становится `parser.rl`.

Build graph создаёт два generated fragment из одной grammar:

- production: `$(B)/parser.rl.h`, backend `-G1`;
- tests/fuzz: `$(B)/parser_test.rl.h`, компактный backend `-T1`.

Они имеют разные outputs и никогда не перетирают друг друга. Production, memprofile и обычный `st` зависят от быстрого автомата. `st_test`, unit tests и fuzz зависят от компактного.

Схема сборки:

```text
                ┌─ ragel -G1 ─→ $(B)/parser.rl.h ──────→ libshitty_prod
parser.rl ──────┤
                └─ ragel -T1 ─→ $(B)/parser_test.rl.h ─→ libshitty_test
```

Один generated fragment включается в трёх режимах:

```cpp
namespace {
    #define SHITTY_PARSER_DATA
    #include SHITTY_PARSER_GENERATED
}

ParserImpl::ParserImpl(...) {
    #define SHITTY_PARSER_INIT
    #include SHITTY_PARSER_GENERATED
}

void ParserImpl::feed(StringView bytes) {
    #include SHITTY_PARSER_GENERATED // action helpers
    while (p != pe) {
        // C++ bulk fast paths
        #define SHITTY_PARSER_EXEC
        #include SHITTY_PARSER_GENERATED
    }
}
```

`write init` выполняется ровно один раз в конструкторе без runtime-флага. Цикл и bulk fast paths принадлежат `parser.cpp`; в exec-ветке `parser.rl` остаётся только `write exec`. Точный набор локальных имён задаётся требованиями Ragel. Generated fragment:

- загружает `cs` и остальные persistent поля из `ProtocolParser`;
- исполняет `write init` только при создании parser;
- исполняет `write exec` над `[p, pe)`;
- сохраняет `cs` и незаконченные accumulators перед выходом;
- вызывает распознанные semantic operations через `ParserIface`.

`processInputImpl()` после переделки имеет только presentation-обвязку:

```cpp
const PresentationState before = capturePresentationState();
hideCursor();
parser->feed(StringView(input, inputSize));
syncPresentationCursor();
const bool changed = presentationChanged(before);
if (refresh && changed) {
    redraw();
}
return changed;
```

`Parser` является отдельным внутренним компонентом с интерфейсом `ParserIface`. `VtermImpl` реализует terminal semantics, но не владеет синтаксическим состоянием и не содержит Ragel include. Шаблонизирован только `ParserImpl<traced>`; `VtermImpl`, input adapter и test API существуют в одном экземпляре независимо от трассировки.

Generated include:

- не форматируется `style.py`;
- не редактируется вручную;
- детерминированно пересоздаётся;
- является явной зависимостью соответствующего варианта `libshitty`.

`Parser` выделяется из того же `ObjPool`, что и Vterm, но не регистрируется в `Composer`: это private dependency Vterm, а не общесистемная роль.

### 2. Ввести единое persistent-состояние parser

В `parser.cpp` появляется внутренняя структура `ProtocolParser`. Она хранится по значению в `ParserImpl<traced>`:

```cpp
struct ProtocolParser {
    int state;

    u32 parameters[32];
    u8 separators[32];
    bool present[32];
    u8 parameterCount;

    u32 number;
    u32 command;
    u32 payloadBytes;
    u32 mark;

    u8 utf8Remaining;
    bool overflow;

    Buffer scratch;
};
```

Это не окончательная побитовая раскладка, а перечень необходимого состояния. После написания грамматики совпадающие поля объединяются.

Инварианты:

- `state` — единственное состояние синтаксического автомата;
- `parameters`, `separators` и `present` записывает только Ragel;
- semantic handlers могут читать параметры только во время action;
- `scratch` содержит только текущую протокольную сущность;
- между вызовами `consume()` не сохраняются `p`, `pe`, raw pointers и `StringView`;
- offsets в `scratch` допустимы, но живут только до commit/cancel текущей последовательности;
- при commit/cancel/overflow `scratch` сбрасывается;
- DCS ограничен 4095 байтами, OSC — текущим лимитом 1 MiB;
- достижение лимита переводит текущую строку в discard-state без дальнейших аллокаций.

Никаких `std::string`, `std::vector`, `stringstream` или `from_chars` в новом parser state и actions не появляется. Используются `Buffer`, `Vector`, `StringView` и POD.

### 3. Развязать syntax actions и terminal semantics

До переключения на Ragel semantic handlers очищаются от управления parser state.

Удаляются вызовы `setState()` из:

- `esc_*`;
- `csi_*`;
- `dcs_*`;
- `osc_*`;
- charset handlers;
- printer mode handlers.

Пока работает старый parser, возврат в `Normal` временно выполняет его dispatch-site. Благодаря этому каждый semantic handler становится обычной операцией над Vterm и Screen.

После развязки действует правило:

```text
Ragel решает, какая последовательность распознана и куда перейти.
Semantic handler выполняет команду и ничего не знает про parser state.
```

Параметры сначала можно оставить в фиксированных массивах Vterm, чтобы не переписывать одновременно все 79 CSI handlers. После переключения они переезжают в `ProtocolParser`; handlers получают к ним read-only доступ на время action.

Отдельные значения, которые нужны семантике дольше одного action, копируются в принадлежащее соответствующему компоненту состояние. Parser-owned `StringView` наружу не выходит.

Одновременно существенно перестраивается сам `vterm.cpp`. Цель не в том, чтобы заменить последние две тысячи строк одним include и оставить окружающий исторический слой нетронутым.

Перетряхиваются:

- declaration `VtermImpl`: parser state и parser actions собираются в одном месте, terminal state отделяется от них;
- `processInputImpl()`: превращается в короткую presentation-обвязку;
- все `setState()` из semantic handlers;
- CSI handlers: остаётся семантика команд, разбор prefix/intermediate/final уходит;
- OSC/DCS helpers: остаётся применение уже разобранных значений, строковые parsers исчезают;
- OSC 52 path в `ApplicationImpl` и `TestDisplay`: повторный parsing исчезает;
- printer controller: отдельный ручной автомат поглощается Ragel;
- `VtermTrace`: перестаёт повторно разбирать DCS header;
- порядок функций в `vterm.cpp`: рядом располагаются parser actions соответствующей группы и вызываемая ими семантика.

Ожидаемо будет переписана значительная часть `vterm.cpp`, а не только текущий `processInputImpl()`. При этом наружная поверхность `Vterm` не расширяется.

### 4. Построить верхнеуровневую statechart

Одна Ragel machine содержит именованные подмашины:

```text
ground
escape
escape_intermediate
escape_ignore
csi_entry
csi_parameter
csi_intermediate
csi_ignore
dcs_entry
dcs_parameter
dcs_intermediate
dcs_payload
dcs_ignore
osc_command
osc_payload
osc_ignore
sos_pm_apc
string_escape
vt52_escape
vt52_cup_row
vt52_cup_column
charset_select
printer
printer_escape
printer_csi
```

Cross-cutting transitions задаются один раз и подключаются к нужным состояниям:

- CAN/SUB отменяют текущую последовательность;
- DEL игнорируется;
- исполняемые C0 выполняются, не разрушая CSI/DCS/ignored string;
- новый ESC или C1 introducer отменяет несовместимую незавершённую последовательность и начинает новую;
- ST завершает только подходящую string-state;
- BEL завершает OSC, но внутри DCS исполняется как control;
- C1-байт внутри корректной UTF-8 последовательности является данными, а не control.

Malformed input никогда не вызывает `STD_VERIFY` и не выходит в error state Ragel как ошибка программы. Он переходит в соответствующий ignore-state и восстанавливается на final byte, ST, CAN/SUB или новом introducer.

### 5. Сохранить bulk execution как macro-transition

Обычный текст не прогоняется через generated DFA по одному байту.

Внешний driver действует так:

1. Если `ProtocolParser::state` равен Ragel ground-state, вызывается существующий `placeAsciiLines()`.
2. Если этот путь неприменим, `printableAsciiPrefix()` находит первый байт вне `0x20..0x7e`.
3. Найденный диапазон передаётся одним вызовом в `placeAsciiRun()`.
4. Для UTF-8 сохраняется `placeUtf8Run()`.
5. Первый специальный байт и последующая управляющая последовательность отдаются Ragel.
6. При возврате автомата в ground generated action делает `fbreak`, чтобы внешний driver снова получил возможность выполнить bulk scan.

`placeAsciiLines()` остаётся допустимым укрупнённым переходом: он работает только из ground, оставляет parser в ground и самостоятельно выдаёт правильный trace для текста и CR/LF.

Для string payload вводится аналогичный scanner. Он ищет только байты, значимые в текущей подмашине. Для сырого OSC это ESC, BEL, CAN/SUB, DEL, C0/C1 и границы UTF-8; после выбора command-specific grammar добавляются её разделители.

Сначала используется существующий SIMD range scan. Замена отдельных вариантов на `memchr` делается только после измерения.

### 6. Перенести ESC, C0/C1, charset и VT52

Первой полностью переносится короткая часть протокола:

- ground controls;
- 7-bit ESC;
- 8-bit C1;
- ESC intermediates;
- `ESC SPC`, `ESC #`, `ESC %`;
- SCS charset selection;
- VT52 commands и двухбайтовый VT52 CUP.

После этого из C++ исчезают соответствующие `InputState` arms, но старый CSI/OSC/DCS ещё может временно вызываться через внутренние переходные actions. Это промежуточное рабочее состояние ветки, а не допустимый конечный результат.

Gate этапа:

- all-splits тесты этой группы совпадают со старым parser;
- trace совпадает;
- bulk ASCII path сохранился архитектурно и не превратился в заведомо побайтовый цикл.

### 7. Перенести CSI целиком

Общая CSI parameter machine:

- принимает максимум 32 параметра;
- различает отсутствующий параметр и явный ноль;
- сохраняет разделитель `;` или `:`;
- насыщает numeric overflow значением `UINT32_MAX`;
- допускает private prefix только в разрешённой позиции;
- принимает до четырёх intermediate bytes ради корректного consume;
- отправляет последовательность с лишними параметрами/intermediates в `csi_ignore`;
- исполняет разрешённые C0 внутри CSI;
- на final byte выполняет ровно один dispatch action.

79 нынешних `case csiKey(...)` описываются таблицей Ragel final transitions. Общая grammar параметров не копируется для каждой команды.

Для неоднозначных final bytes, например `T`, action выбирает семантическую операцию по уже готовым параметрам и terminal mode. Это semantic decision, а не повторный разбор байтов.

После этапа удаляются:

- `beginCsi()`;
- `processCsiByte()`;
- `dispatchCsi()`;
- `csiKey()`;
- `csiPrivatePrefix`;
- `csiIntermediates`;
- `csiPrefixAllowed`;
- `IgnoreSequence` как C++ enum-state.

Gate этапа:

- все CSI split/malformed/overflow тесты;
- DEC/xterm/kitty CSI suites;
- одинаковые PTY replies;
- одинаковая последовательность trace events.

### 8. Перенести framing всех string protocols

Ragel начинает целиком владеть:

- DCS introducer/header/payload/ST;
- OSC command/payload/BEL/ST;
- SOS/PM/APC ignore payload/ST;
- ESC-pending внутри строки;
- повторным ESC;
- C1 ST;
- UTF-8 continuation в диапазоне C1;
- cancel/restart другой строкой.

Ключевой инвариант — транзакционность. Кроме исполняемых C0, никакой OSC/DCS command не изменяет terminal до корректного terminator.

Если строка:

- отменена CAN/SUB;
- вытеснена новым introducer;
- превышает лимит;
- синтаксически невалидна;

то все накопленные command-specific операции отбрасываются.

Это запрещает, например, менять palette по мере чтения незаконченного OSC или устанавливать UDK до получения ST.

### 9. Перенести DCS payload grammars

DCS сначала разбирает стандартный header: параметры, intermediates и final byte. По нему выбирается payload submachine.

#### DECRQSS

`DCS $ q` выбирает запрос во время чтения payload. Для известных запросов сохраняется компактный enum; неизвестный запрос заканчивается ответом `0$r`. `std::string query` не создаётся.

#### XTGETTCAP

`DCS + q` читает последовательность hex-encoded имён, разделённых `;`.

- hex декодируется по паре nibble;
- имя сравнивается с поддерживаемыми capabilities;
- ответ строится через общий `Buffer`/`StringBuilder`;
- raw `substr` и повторное hex-сканирование исчезают.

#### DECUDK

`DCS ... |` разбирает clear/lock и список `key/hex-value`.

- код клавиши и hex value разбираются потоково;
- изменения складываются как одна транзакция текущего DCS;
- commit происходит только на ST;
- cancel/overflow уничтожает всю транзакцию;
- payload остаётся ограничен 4095 байтами.

После этапа удаляются `handle_DCS()`, `dcs_DECRQSS(const std::string&)`, `dcs_XTGETTCAP(const std::string&)` и `dcs_DECUDK(const std::string&)`.

### 10. Перенести OSC payload grammars

OSC command выбирается непосредственно grammar по его десятичному префиксу. Не создаётся промежуточная строка вида `command;argument`.

Группы переносятся в следующем порядке.

#### Простые команды

- 0/1/2 — title/icon title;
- 7 — current working directory;
- 9 — notification/progress;
- 133 — shell integration;
- неизвестные commands.

Для неизвестной команды payload сохраняется в bounded `scratch` и передаётся host одним локальным `StringView` только после terminator.

#### Цвета

- 4 — пары palette index/color;
- 5 — special color query;
- 6/106 — special color modes;
- 10–19 — dynamic colors;
- 104–119 — reset operations.

X color syntax (`?`, `#rgb`, `#rrggbb`, `rgb:...` и поддерживаемые варианты) оформляется как переиспользуемая Ragel submachine. Palette changes накапливаются в фиксированной транзакции и применяются на commit.

#### Hyperlinks

OSC 8 разбирает список параметров и URI одним проходом. Identity и payload интернируются только в commit action. При cancel никакой `CellExtra` не создаётся.

#### Clipboard

OSC 52 перестаёт повторно парситься в `osc_protocol.cpp`, `ApplicationImpl` и `TestDisplay`.

- selectors разбираются Ragel;
- query распознаётся без буфера;
- base64 декодируется потоковым codec action;
- decoded content лежит в parser scratch до terminator;
- commit action работает с `Clipboard` через Composer либо вызывает один typed semantic handler;
- protocol response строится через `Buffer` и `StringBuilder`.

После этого `osc_protocol.cpp` больше не содержит input parser. Оставшиеся response encoders либо переводятся на `Buffer&`, либо поглощаются semantic actions Vterm.

#### Notifications

OSC 99 разбирает metadata `key=value:` и payload type без `find`/`substr`.

- identifier и metadata валидируются при чтении;
- base64 decoder переживает границы PTY chunks;
- состояние одной законченной multipart notification принадлежит notification subsystem, не `ProtocolParser`;
- parser передаёт туда только commit события законченного OSC;
- закрытие notification является отдельным typed commit.

После этапа удаляются:

- `handle_OSC()`;
- все `osc_*(const std::string&)` parser helpers;
- `setHyperlink(const std::string&)`;
- production-вызовы `parseOsc52()`;
- `argBuf` и `argBufOverflowed`.

### 11. Включить printer controller в ту же machine

`printerControllerMode` перестаёт обходить parser внешним `if`.

CSI `5 i` переводит Ragel в `printer`; CSI `4 i` внутри printer возвращает его в ground. Состояния `printer_escape` и `printer_csi` сохраняют ложные префиксы как данные.

Bulk printer path ищет `ESC` и C1 CSI через `memchr`/SIMD и передаёт весь промежуток одним вызовом host. Граница terminator может находиться между двумя `consume()`.

После этапа удаляются:

- `PrinterControllerState`;
- `consumePrinterController()`;
- внешний `if (printerControllerMode)` в `processInputImpl()`.

Сам terminal mode может остаться как semantic flag, если он нужен для отчётов, но он не управляет отдельным ручным автоматом.

### 12. Перенести tracing на actions Ragel

Trace не должен повторно разбирать DCS header, как это происходит сейчас.

Ragel actions непосредственно сообщают:

- text span;
- C0/C1 control;
- ESC begin/byte/end/cancel;
- готовые CSI prefix/params/intermediates/final;
- string begin/data/end/cancel.

`VtermTrace::consumeDcsHeader()` удаляется. Parser уже знает точную границу DCS header.

Для `ParserImpl<false>` trace actions полностью исчезают через `if constexpr`. Runtime-проверки trace pointer в hot path не добавляются. `VtermImpl` от trace больше не шаблонизирован.

Parser-facing API trace переводится со `std::string` на `StringView` и массивы POD. Внутреннее тестовое хранение trace можно менять отдельно, но новый production parser не использует STL.

### 13. Выполнить атомарное переключение

Когда Ragel покрывает все группы:

- `processInputImpl()` переключается на новую machine;
- полный старый state switch удаляется;
- временный differential selector удаляется;
- `InputState` удаляется;
- ручные pre-dispatch `kStateHit`/`kByteHit` удаляются;
- `setState()` и `stringUtf8Continuation()` удаляются;
- старые OSC/DCS parsers удаляются;
- все parser-owned STL buffers удаляются.

В production не остаётся режима выбора parser, runtime flag, `#ifdef` на каждое состояние или fallback к старому parser.

### 14. Differential и fuzz verification

До удаления oracle один и тот же поток прогоняется через старый и новый test binary. Сравниваются:

- cells и CellExtra;
- cursor, margins, modes и active screen;
- damage/output;
- PTY responses;
- host actions;
- clipboard;
- hyperlinks;
- notifications;
- printer output;
- trace;
- parser recovery после CAN/SUB и RIS.

Наборы входов:

- существующий deterministic fuzz;
- все имеющиеся upstream parser corpora;
- grammar-generated valid sequences;
- mutations каждой valid sequence;
- random binary input;
- adversarial длинные OSC/DCS около лимитов;
- каждая последовательность целиком, по всем одиночным split points и byte-by-byte.

Отдельный invariant fuzz проверяет, что результат не зависит от разбиения одного byte stream на PTY chunks.

После удаления старого parser differential target также удаляется; остаются corpus, chunk-invariance и semantic tests.

### 15. Performance tuning после функционального завершения

Оптимизация выполняется только на полностью новой и понятной machine. Первая функционально полная версия может быть немного медленнее старой; это принимается сознательно и не задерживает архитектурную переделку.

Последовательность измерений:

1. Ragel `-G2`, `-G1`, `-T1`.
2. Сравнение generated `.text` и icache misses.
3. Проверка, что ground bulk path доминирует над generated transitions на текстовом workload.
4. Проверка размера средних ASCII/UTF-8 spans.
5. Проверка OSC/DCS allocation count.
6. Проверка escape-heavy instructions/byte.
7. Просмотр annotated assembly наиболее горячих actions.

Результаты этих измерений формируют следующий цикл оптимизаций. На первой версии блокируется только очевидная алгоритмическая поломка, например случайная побайтовая обработка обычного ASCII вместо сохранённого bulk path.

Решение о `memchr` против текущего SIMD scanner принимается отдельно для:

- ground ASCII;
- raw OSC;
- DCS;
- ignored strings;
- printer.

У них разные stop sets, поэтому один универсальный scanner не навязывается.

### 16. Критерии завершения

Перед коммитом финальной архитектуры одновременно истинны все условия:

- один Ragel `state` владеет всем input protocol framing;
- `InputState` отсутствует;
- отсутствует ручной parser switch;
- отсутствуют `processCsiByte()` и `dispatchCsi()`;
- отсутствуют `handle_OSC()` и `handle_DCS()`;
- OSC/DCS не проходят через «накопить строку, затем распарсить»;
- OSC 52 не парсится повторно в Application/TestDisplay;
- printer controller является состоянием той же machine;
- semantic handlers не меняют parser state;
- incomplete sequence не даёт semantic side effects;
- chunking не влияет на результат;
- parser не хранит `StringView`/raw pointer между вызовами;
- parser не использует `std::`;
- лимиты не приводят к неограниченным аллокациям;
- полный test target и все upstream suites проходят;
- parser fuzz и chunk-invariance fuzz проходят;
- performance baseline новой версии снят и возможная небольшая регрессия локализована для последующей доводки;
- generated source полностью воспроизводится build graph.

## Результат реализации

План завершён 2026-07-27.

Архитектурный результат:

- `Parser` выделен в `parser.h`/`parser.cpp`, а `VtermImpl` реализует его semantic interface `ParserIface`;
- `ProtocolParser`, generated include и trace specialization полностью удалены из `vterm.cpp`;
- `VtermImpl`, `VtermInput`, listeners и `TestApiImpl` больше не шаблонизированы и не дублируются для trace/no-trace;
- `ProtocolParser::state` и сгенерированная Ragel statechart являются единственным владельцем protocol framing;
- ground/text, C0/C1, ESC, CSI, OSC, DCS, SOS/PM/APC, VT52, charset и printer controller входят в одну machine;
- semantic methods получают уже распознанные POD-параметры и локальные `StringView`, не меняют состояние parser и вызываются из Ragel actions через `ParserIface`;
- parser не хранит raw pointers или `StringView` между `consume()`, не использует `std::` и ограничивает незаконченные OSC/DCS;
- OSC 52 и OSC 99 декодируются во время parsing, DECUDK, XTGETTCAP и DECRQSS коммитятся транзакционно;
- bulk paths сохранены для обычного текста и сырых string payloads;
- старые `InputState`, `processCsiByte`, `dispatchCsi`, `handle_OSC`, `handle_DCS`, `consumePrinterController`, `setState`, `argBuf` и повторные string subparsers удалены.

Размер handwritten C++ действительно уменьшился:

- `vterm.cpp`: 10 255 → 8 045 строк;
- parser component: `parser.cpp` 212 строк и `parser.h` 254 строки;
- `color_spec.cpp`: 375 → 251 строк;

`parser.rl` содержит 5 460 строк. Он не заменяет terminal semantics: его объём — явная grammar, transitions и короткие actions, которые вызывают semantic methods Vterm через узкий interface.

Финальная проверка:

- весь build/test graph: 2 552 из 2 552 целей;
- 167 unit tests;
- parser fuzz, chunk-invariance, upstream DEC/xterm/libvterm и vtebench suites;
- отдельная регрессия для неизвестных C1 final bytes после `ESC` в VT52;
- production `-G1` на packed corpus: 1 190.8 MiB, два контрольных запуска 112.4 и 114.0 MiB/s, без parser error-state, зависания или роста памяти.

Сравнение Ragel 6 backend’ов на одном исходнике и одном clang:

| Backend | Generated header | parser function | ELF `.text` | Packed corpus | `~/2` × 4 |
| --- | ---: | ---: | ---: | ---: | ---: |
| `-T1 -L` | 788 224 B | 106 245 B | 27 143 476 B | 97.9 MiB/s | 308.6 MiB/s |
| `-G1 -L` | 1 303 074 B | 196 593 B | 28 571 944 B | 116.6 MiB/s | 311.9 MiB/s |
| `-G2 -L` | 4 569 942 B | 990 189 B | 30 522 008 B | 120.6 MiB/s | 311.7 MiB/s |

`-G1 -L` выбран для production. Он ускоряет mixed packed corpus примерно на 19% относительно `-T1`, совпадает с `-G2` на text-heavy workload и уступает ему около 3% на packed corpus. Компактный `-T1 -L` остаётся отдельным backend для test/fuzz binaries. При этом `-G2` компилировал generated parser около 18 минут и потреблял примерно 1.8 GiB RSS; `-G1` собирает тот же шаг примерно за минуту.

Контрольный `perf stat` на packed corpus подтверждает выбор: `-T1` исполнил 237.4 млрд instructions и 56.5 млрд cycles, `-G1` — 185.8 и 46.8 млрд, `-G2` — 190.8 и 44.8 млрд соответственно. Небольшой остаточный runtime-выигрыш `-G2` не оправдывает десятикратный размер parser function и восемнадцатиминутную компиляцию.

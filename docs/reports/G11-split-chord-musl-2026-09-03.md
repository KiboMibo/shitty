# G11: сплит-чорд не делит — но дело не в musl

- **Задача:** `G11` внеплановой починки CI
- **Дата:** 2026-09-03
- **Ветка:** `fix/G11-split-chord-musl` (не пушилась)
- **Прогон-повод:** `33717810922`, джоба `Tests Alpine` (ID `100530825624`)
- **Владение:** `lib/shitty/session_ut.cpp`, `session.{h,cpp}`, `input_bindings.{h,cpp}`
- **Статус:** **готово**, правка в одном файле — `lib/shitty/session_ut.cpp`

Имя файла отчёта задано постановкой и сохраняет исходную гипотезу. **musl тут
ни при чём**, см. §2.

**Причина одной строкой:** строки сплит-чордов лежат только в
`#if defined(__APPLE__)`-половине таблицы привязок, а тест волны `F9` —
единственный в наборе, кто проверяет macOS-only чорд без платформенного
ветвления.

---

## 1. Воспроизведение

### 1.1 Постановка «падает только Alpine» — неверна

Первое же измерение — не рассуждение — по логам того же прогона. Скачал
`--log-failed` каждой из пяти красных джоб и поискал в них строку отказа:

```
$ for j in 100530825624 100530825924 100530825722 100530825725 100530825745; do
    gh run view --job $j --log-failed > /tmp/g11logs/$j.log
    echo "=== $j ==="; grep -o "session_ut.cpp:[0-9]*" /tmp/g11logs/$j.log | sort -u
  done
=== 100530825624 ===   # Tests Alpine   (musl,  gcc 15.2)
session_ut.cpp:2283
=== 100530825924 ===   # Tests Fedora   (glibc, gcc)
                       # — строки нет
=== 100530825722 ===   # Tests ASan     (nix, glibc)
session_ut.cpp:2283
=== 100530825725 ===   # Tests UBSan    (nix, glibc)
session_ut.cpp:2283
=== 100530825745 ===   # Coverage       (nix, glibc)
session_ut.cpp:2283
```

Отказ есть в **четырёх** джобах из пяти, три из них — **glibc**. Fedora не
исключение: она до `unit-tests/group-10` просто не дошла — в её логе этой цели
нет вовсе (`grep -c "unit-tests/group-10" → 0`), сборка встала раньше на
`python-tests/group-08`.

Строка из ASan, для сверки, что это тот же отказ, а не однофамилец:

```
terminal-tests-asan> panes.length() == 2 failed, at /build/source/lib/shitty/session_ut.cpp:2283
```

**Отказ детерминированный и общий для всех Linux-джоб**, а не свойство Alpine.

### 1.2 Что именно на строке 2283

```
$ git show HEAD:lib/shitty/session_ut.cpp | sed -n '2278,2283p'
            Harness harness;
            harness.options.panes = true;
            harness.keyPress(plt::InputKey::Printable, plt::InputSuper, 'd');
            Vector<SessionPane> panes;
            harness.sessions->visiblePanes(panes);
            STD_INSIST(panes.length() == 2);
```

Положительный контроль: `cmd+d` без Shift не делит вкладку.

### 1.3 Отказ воспроизведён локально, в контейнере, до правки

Дерево на `HEAD` ветки, `fedora:44` (glibc 2.43) на arm64, шаги установки — из
`ci.yml` (плюс обход, находка 3), компилятор — `clang 22.1.8` (почему не gcc,
см. находку 1):

```
$ ./.build-clang/unit_tests SessionSet::BothFormsOfTheSplitChordsReachTheirActionThroughTheBindings --threads=1
panes.length() == 2 failed, at /src/lib/shitty/session_ut.cpp:2283
- SessionSet::BothFormsOfTheSplitChordsReachTheirActionThroughTheBindings
OK: 0, ERR: 1, SKIP: 917
EXIT=1
```

Та же строка, что в CI. И тем же шардом, каким его гоняет CI:

```
$ ./.build-clang/unit_tests --group=10 --group-count=20 --threads=1
panes.length() == 2 failed, at /src/lib/shitty/session_ut.cpp:2283
OK: 45, ERR: 1
```

Весь набор до правки в этом контейнере:

```
$ ./.build-clang/unit_tests --threads=1 | grep -E "failed, at|OK:" | sort | uniq -c
      4 helper != nullptr failed, at /src/lib/shitty/pty_ut.cpp:317
      1 helper != nullptr failed, at /src/lib/shitty/pty_ut.cpp:617
      1 panes.length() == 2 failed, at /src/lib/shitty/session_ut.cpp:2283
      1 OK: 912, ERR: 6
```

Пять `pty_ut` — неполное окружение (не собран `pty_test_helper`), то же, что на
macOS без `SHITTY_PTY_TEST_HELPER`. Наш отказ — шестой.

---

## 2. Какие гипотезы отсеяны и чем

| Гипотеза | Отсеяна чем |
|---|---|
| **musl против glibc** (локаль, `towupper`, регистр) | Воспроизведено на **glibc 2.43** локально (§1.3) и в трёх glibc-джобах CI (§1.1). Кроме того, на пути чорда нет ни одной функции локали: `sameChordKey()` сравнивает `u32` на равенство (`input_bindings.cpp:203-208`), регистр не приводится нигде — обе формы, `'d'` и `'D'`, заданы **отдельными строками таблицы**, ровно чтобы его не приводить. |
| **Порядок тестов в процессе** | Перебор **всех** шардов при `--group-count=19` и `21`, до правки, в контейнере: отказ ровно один на каждое разбиение и переезжает **вместе с тестом**, а не остаётся у соседей. Вывод — ниже. |
| **Геометрия** (панелям некуда влезть) | Харнесс задаёт окно `{.width = 80, .height = 24}` и `setGlyphSize(1, 1)` константами в конструкторе (`session_ut.cpp:213-219`); из среды туда не попадает ничего. И тот же харнесс делит вкладку в семи других тестах файла (через `splitVertical()`) — на тех же 80×24, на тех же джобах, зелено. Делят все, кроме пути через таблицу привязок. |
| **Компилятор** | Отказ снят под тремя разными: gcc 15.2 (Alpine, CI), тулчейн nix (ASan/UBSan/Coverage, CI) и clang 22.1.8 (локально). Один и тот же. |

Перебор шардов, дерево до правки:

```
$ for gc in 19 21; do for g in $(seq 0 $((gc-1))); do
    ./.build-clang/unit_tests --group=$g --group-count=$gc --threads=1 | grep 2283 ...
  done; done
group-count=19 group=10 -> panes.length() == 2 failed, at /src/lib/shitty/session_ut.cpp:2283
group-count=21 group=14 -> panes.length() == 2 failed, at /src/lib/shitty/session_ut.cpp:2283
sweep done
```

Отказ — один на разбиение, и его номер меняется (10 из 19, 14 из 21) ровно
потому, что переезжает сам тест. Сосед по процессу ни при чём.

Ни одна из четырёх гипотез не выжила.

---

## 3. Причина

**Строки сплит-чордов существуют только в macOS-половине таблицы привязок.**

`lib/shitty/input_bindings.cpp`: `defaultBindings[]` разбит на
`#if defined(__APPLE__)` (строки 58-118) и `#elif defined(__linux__)`
(строки 119-131). Три строки сплитов — 70, 78, 79 — лежат в первой половине:

```cpp
{InputActions::SplitVertical,   {.key = InputKey::Printable, .modifiers = InputSuper,              .baseCodepoint = 'd', .panes = true}},
{InputActions::SplitHorizontal, {.key = InputKey::Printable, .modifiers = InputSuper | InputShift, .baseCodepoint = 'd', .panes = true}},
{InputActions::SplitHorizontal, {.key = InputKey::Printable, .modifiers = InputSuper | InputShift, .baseCodepoint = 'D', .panes = true}},
```

В Linux-половине сплитов нет ни одной строки. Значит на Linux
`InputBindingsImpl::find()` не находит ничего, `key()` возвращает `false`,
`splitVerticalListeners` не публикуются, вкладка остаётся с одной панелью — и
`panes.length() == 2` падает.

**Это не оплошность, а область плана.** `docs/plans/2026-08-18-panes-and-window-chrome.md:29`
открывается словами «Готово, когда **на macOS** верно всё перечисленное», а
`:243` прямо предупреждает: платформенное оборачивать в `#if defined(__APPLE__)`,
иначе сломается Linux-сборка. Отсутствие чордов на Linux уже зафиксировано
тестом с обратной стороны — `input_bindings_ut.cpp:184-186`:

```cpp
#else
        STD_INSIST(!consumedVertical);
        STD_INSIST(vertical.calls == 0);
#endif
```

**Дефект — в тесте волны `F9`.** Он единственный во всём наборе проверяет
macOS-only чорд **без платформенного ветвления**, тогда как соседний
`input_bindings_ut.cpp` ветвится и по модификаторам (`copyModifiers`,
`tabModifiers`, …, строки 25-40), и по самому наличию сплит-строк.

Продуктовый код чинить нечего: он ведёт себя ровно так, как решено планом, и на
macOS — платформе, где пользователь и увидел молчащий `cmd+shift+d`, — обе формы
чорда работают (§6, критерий 4).

---

## 4. Правка и почему именно она

Один файл, `lib/shitty/session_ut.cpp`, тест разведён по платформам по образцу
`input_bindings_ut.cpp`:

- **macOS-ветка — байт в байт прежняя.** Положительный контроль, обе формы
  шифт-чорда, оси разделения, размер, переданный новой оболочке. На платформе,
  где дыра была, тест ловит её ровно как ловил.
- **Linux-ветка утверждает отсутствие**, а не «не упало». Нажатия идут **через ту
  же дверь** — `harness.keyPress()`, то есть через таблицу привязок, — и после
  каждого вкладка обязана остаться с одной панелью и одной оболочкой.
  Перебираются две формы кодпоинта (`'d'`, `'D'`) на трёх формах чорда: `cmd`,
  `cmd+shift` (форма macOS) и `ctrl+shift` — тот вид, каким записаны **все
  остальные** действия Linux-половины таблицы. Третья форма — не украшение:
  без неё ветка не краснела на подставленной Linux-строке (§5, подстановка 3).

Чего сознательно **не** сделано: не заведены Linux-строки сплитов
(`ctrl+shift+d`). Это расширение продукта на платформу, которую план `A4` не
охватывает, и отъём чорда у программы внутри терминала — решение уровня плана,
не починки CI. Вынесено в «Что осталось непроверенным».

---

## 5. Разборчивость: подстановка дефекта

Главный критерий — что тест краснеет, а не молчит. Три подстановки, все
откачены.

**Подстановка 1 — ровно дефект пользователя** (macOS): у `cmd+shift+d` убрана
строчная форма кодпоинта (`input_bindings.cpp:78`), остаётся только `'D'`. Это
тот самый вид поломки, из-за которого чорд «не делал ничего».

```
$ SHITTY_PTY_TEST_HELPER=... ./.build/unit_tests --threads=1
panes.length() == 2 failed, at lib/shitty/session_ut.cpp:2303
composer.inputBindings->key({... InputSuper | InputShift, 0, 'd'}) failed, at lib/shitty/input_bindings_ut.cpp:181
OK: 953, ERR: 2
EXIT=2
```

**Подстановка 2 — положительный контроль жив** (macOS): `cmd+d` требует ещё и
Alt (`.modifiers = InputSuper | InputAlt`), то есть не совпадает ни с чем.

```
$ SHITTY_PTY_TEST_HELPER=... ./.build/unit_tests --threads=1
panes.length() == 2 failed, at lib/shitty/session_ut.cpp:2291
consumedVertical failed, at lib/shitty/input_bindings_ut.cpp:173
OK: 953, ERR: 2
EXIT=2
```

**Подстановка 3 — новая Linux-ветка тоже разборчива** (контейнер): в
Linux-половину таблицы добавлена строка `SplitVertical` на `ctrl+shift+d` —
чорд, который никакое решение туда не клало.

```
$ ./.build-clang/unit_tests SessionSet::BothFormsOfTheSplitChordsReachTheirActionThroughTheBindings --threads=1
panes.length() == 1 failed, at /src/lib/shitty/session_ut.cpp:2334
- SessionSet::BothFormsOfTheSplitChordsReachTheirActionThroughTheBindings
OK: 0, ERR: 1, SKIP: 917
EXIT=1
```

Первая редакция Linux-ветки эту подстановку **пропускала** — она щупала только
`cmd`-форму чорда. Отсюда третья форма в переборе (§4). `input_bindings_ut.cpp`
её тоже пропускает: `OK: 2` на обоих тестах до расширения перебора.

После отката всех трёх подстановок изменённым остаётся только
`lib/shitty/session_ut.cpp`.

---

## 6. Критерии приёмки

| # | Критерий | Итог |
|---|---|---|
| 1 | Отказ воспроизведён в контейнере до правки | **да**, §1.3 |
| 2 | Причина названа и доказана | **да**, §2 и §3 |
| 3 | После правки тест проходит в контейнере | **да**, ниже |
| 4 | macOS: `OK: 955` | **да**, ниже |
| 5 | Тест разборчив (подстановка дефекта) | **да**, §5 |
| 6 | Поведение при смене `--group-count` | **да**, §2 |

### Критерий 3 — контейнер после правки

`fedora:44` + clang, дерево с правкой, пересобран один объектный файл:

```
$ ./.build-clang/unit_tests SessionSet::BothFormsOfTheSplitChordsReachTheirActionThroughTheBindings --threads=1
+ SessionSet::BothFormsOfTheSplitChordsReachTheirActionThroughTheBindings
OK: 1, SKIP: 917
EXIT=0

$ ./.build-clang/unit_tests --group=10 --group-count=20 --threads=1
OK: 46

$ ./.build-clang/unit_tests --threads=1 | grep -E "failed, at|OK:" | sort | uniq -c
      4 helper != nullptr failed, at /src/lib/shitty/pty_ut.cpp:317
      1 helper != nullptr failed, at /src/lib/shitty/pty_ut.cpp:617
      1 OK: 913, ERR: 5
```

Шард `group-10` — `OK: 45, ERR: 1` до, `OK: 46` после. По всему набору
`ERR: 6 → ERR: 5`, и оставшиеся пять — неполное окружение `pty_ut`, не наши.

Дополнительно, `alpine:3.24` со стоковым тулчейном (gcc 15.2/musl): правка
**компилируется**, `BUILD_EXIT=0`, один объектный файл. Исполнить там
`SessionSet::` нельзя ни до, ни после — находка 1.

### Критерий 4 — macOS

До правки и после — одинаково:

```
$ ./build unit_tests pty_test_helper
$ SHITTY_PTY_TEST_HELPER="$PWD/.build/pty_test_helper" ./.build/unit_tests --threads=1
OK: 955
EXIT=0
```

---

## Обнаружено

1. **GCC на Linux/aarch64 ломает переключение волокон.** Любой тест,
   поднимающий планировщик (`SessionSet::` целиком), падает сегфолтом в
   `ext/libstd/std/thr/context.cpp`, aarch64-ветка `swapContext`. Второй
   аргумент приходит нулём:

   ```
   => 0x58fadc <ContextImpl::swapContext(u64*, u64*)+56>:  ldr x2, [x1]
   sp 0xffffa952e260   x0 0xffffa952f898   x1 0x0   x2 0xffffa952e260
   #3 SchedulerImpl::create(stl::ObjPool&, stl::Runable&, unsigned long)
   #4 SessionSet::create(Composer&)
   ```

   Одинаково на `alpine:3.24` (musl, gcc 15.2) и `fedora:44` (glibc 2.43,
   gcc 16.2.1) — то есть **не musl**. Гипотезу «BTI/PAC Fedora» отсеял
   пересборкой с `-mbranch-protection=none`: сегфолт тот же. **Clang 22.1.8 на
   том же контейнере и том же дереве работает**, что и дало воспроизведение в
   §1.3. Похоже на пролог, который GCC всё-таки выпускает для
   `__attribute__((naked))`-функции на aarch64 (до `ldr x2, [x1]` смещение
   `+56` вместо ожидаемых `+48` — две лишние инструкции перед асм-блоком).
   CI это не задевает: Linux-джобы там x86_64, у которого своя асм-ветка. Файл
   вендорный (`ext/libstd`), вне владения `G11` — не трогал.

2. **Формулировка «падает только Alpine» ошибочна** и стоила бы времени любому,
   кто примет её на веру: красны все Linux-джобы, дошедшие до цели, а Fedora
   выглядит невиновной лишь потому, что упала раньше на `python-tests/group-08`.
   Надёжный способ прочесть такой прогон — искать строку отказа в логах
   **каждой** джобы, а не сверять галочки.

3. **`fedora:44` в контейнере не ставится по инструкции из `ci.yml`** на этой
   машине: `dnf` валится на `openh264` из репозитория Cisco
   (`Cannot download Packages/o/openh264-2.6.0-3.fc44.aarch64.rpm: All mirrors
   were tried`). Обходится `--disablerepo="*openh264*"`.

4. **Дыра, которую волна `F9` закрыла, закрыта только на macOS.** На Linux
   таблица привязок по-прежнему не лежит ни на одном тестовом пути, ведущем к
   реальному делению: единственный тест, который её туда клал, там теперь
   проверяет отсутствие чорда. Это честно — чорда там и нет, — но означает, что
   Linux-путь `keyPress → InputBindings → splitFocused` не покрыт ничем, и
   покрыть его нечем, пока чорда нет.

5. **`InputBindings::SplitsTheFocusedPaneOnTheTwoChordsAndOnlyWithTheOption`
   на Linux щупает только `cmd`-форму** и молча пропускает появление
   `ctrl+shift+d` в таблице (подстановка 3, `OK: 2`). Расширять его я не стал —
   файл не мой; но если Linux-чорды когда-нибудь заведут, этот тест не
   заметит.

---

## Что осталось непроверенным

- **Зелёный прогон на Linux/x86_64 после правки не снят** — локально доступен
  только arm64. Что правка собирается под gcc 15.2/musl и что весь набор с ней
  зелёный под clang/glibc — проверено; совпадение с CI покажет CI.
- **Прогон под ASan/UBSan/Coverage не выполнен** — тулчейн nix локально не
  поднимался. Отказ там был тот же, но сама правка в этих конфигурациях не
  проверялась.
- **Стоит ли заводить сплит-чорды на Linux** — не решено и не проверено. Сейчас
  `-panes` на Linux включается, но разделить вкладку нечем: чорд — не
  единственный, а единственный существующий путь. Это вопрос к плану `A4`, а не
  к починке CI.
- **Ручная приёмка на живом `st -panes`** в рамках `G11` не проводилась:
  поведение обеих форм кодпоинта у реального Cocoa по-прежнему подтверждено
  только таблицей и юнит-тестами.
- **Причина находки 1 названа по косвенному признаку** (смещение `+56` и
  разница gcc/clang), дизассемблер целиком я не читал и в `ext/libstd` не лез.

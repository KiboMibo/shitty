# G4 — компиляция на Linux

Ветка `fix/G4-linux-compile`. Чинит компиляцию четырёх джоб CI (`Tests Alpine`,
`Tests Fedora`, `Tests UBSan`, `Tests ASan`), падавших на двух независимых
дефектах. Оба внесены планом `pane-frame-stall`, задача `T5` (контракт `К3`,
размер pty до `fork`), и оба невидимы на macOS.

Общее у них одно: локальный компилятор молчит не потому, что код верен, а
потому, что libc++ и Darwin-заголовки случайно закрывают собой ошибку. Ниже —
что именно и почему.

---

## Дефект 1 — `cfmakeraw` не объявлен

### Что сломано

`tst/pty_test_helper.c` вызывает `cfmakeraw()` в `wait_for_hangup()`. Файл
открывается строкой `#define _POSIX_C_SOURCE 200809L`, а `cfmakeraw()` — не
POSIX, это BSD-расширение. На gcc/musl и gcc/glibc функция при этом не
объявлена, и C23-компилятор считает это ошибкой, а не предупреждением:

```
tst/pty_test_helper.c:127:5: error: implicit declaration of function 'cfmakeraw' [-Wimplicit-function-declaration]
```

Механика в обеих libc одинаковая по смыслу и разная по имени макроса:
glibc прячет `cfmakeraw()` за `__USE_MISC`, musl — за `_BSD_SOURCE`. Оба этих
макроса выставляются по умолчанию — но только пока исходник **не назвал сам**
ни одного макроса тестируемых возможностей. Строка `_POSIX_C_SOURCE 200809L`
именно это и делает: она отключает умолчание и оставляет ровно POSIX, из
которого `cfmakeraw()` вычеркнут.

### Почему невидимо на macOS

Darwin вообще не использует ни `__USE_MISC`, ни `_BSD_SOURCE`. Там уровень
видимости задаёт `__DARWIN_C_LEVEL`, и файл уже поднимает его обратно
`#define _DARWIN_C_SOURCE` (это было сделано раньше, чтобы вернуть `SIGWINCH`).
Тот же `_DARWIN_C_SOURCE` заодно открывает и `cfmakeraw()` — поэтому на macOS
файл собирался, а дефект существовал только за пределами Darwin.

### Что изменено

`tst/pty_test_helper.c`: `#define _DEFAULT_SOURCE` **до первого `#include`**,
рядом с уже существующим `_POSIX_C_SOURCE`.

Почему именно так, а не флагом сборки: `_DEFAULT_SOURCE` — единственный макрос,
который понимают обе целевые libc. glibc из него выводит `__USE_MISC`; musl в
`features.h` делает `#if defined(_DEFAULT_SOURCE) ... #define _BSD_SOURCE 1`.
На Darwin он не значит ничего и просто игнорируется, так что отдельной ветки
`#if` он не требует. Флаг в `build.py` был бы хуже трижды: он принадлежит
задаче `G3`, он размазал бы требование одного файла по всему графу сборки, и
он не был бы виден тому, кто читает сам `.c`-файл.

---

## Дефект 2 — неоднозначный `forward`

### Что сломано

`stl::ObjPool::make<T>(A&&... a)` разворачивает аргументы неквалифицированным
вызовом `forward<A>(a)`. Обычный поиск имени находит `stl::forward` из
`ext/libstd/std/typ/support.h`. Но вызов неквалифицированный, значит работает и
ADL: если хотя бы один аргумент имеет тип из пространства имён `std`, к
кандидатам добавляется `std::forward`. Обе перегрузки после подстановки имеют
ровно одну и ту же сигнатуру, ни одна не специальнее другой — вызов
неоднозначен.

Триггером стал `T5`. `lib/shitty/pty_ut.cpp` завёл фикстуру `BornSizePty` с
полем `std::string heard[2]` и строкой

```cpp
return owner.make<TeeHandle>(*inner, heard[spawns++]);
```

`std::string&` во втором аргументе — это и есть то, что притаскивает `std` в
список ассоциированных пространств имён. До `T5` ни один вызов `make()` не
передавал в пул ничего из `std`, поэтому мина лежала в `libstd` не срабатывая.

Диагностика gcc называет обоих кандидатов явно:

```
ext/libstd/std/mem/obj_pool.h:51:54: error: call of overloaded 'forward<...basic_string...>(...)' is ambiguous
  note: candidate 1: 'constexpr T&& stl::forward(rem_ref<T>&) ...'
  note: candidate 2: 'constexpr _Tp&& std::forward(typename remove_reference<_Tp>::type&) ...'
```

clang даёт ту же ошибку короче — и ровно ту строку и колонку, что в логе CI:
`ext/libstd/std/mem/obj_pool.h:51:44: error: call to 'forward' is ambiguous`.

### Почему невидимо на macOS

Не из-за версии clang: локальный компилятор — Apple clang 21.0.0, он принимает
`-std=c++26`, то есть ровно тот же фронтенд, что и в джобах с санитайзерами.
Разница в стандартной библиотеке, и притом в одной строке её объявления.

libstdc++ пишет параметр `std::forward` через классовый трейт:

```cpp
forward(typename std::remove_reference<_Tp>::type& __t)
```

libc++ — через встроенный в компилятор трейт:

```cpp
forward(__libcpp_remove_reference_t<_Tp>& __t)   // = __remove_reference_t(_Tp)
```

Это измеримая, а не предполагаемая разница. Проба (`scratchpad/probe/sig.cpp`):
два одинаковых по смыслу `forward` в разных namespace, вызов из шаблона с
аргументом из «библиотечного» namespace, всё на одном и том же Apple clang:

| запись `remove_reference` в «библиотечном» `forward` | Apple clang |
| --- | --- |
| классовый трейт (как в libstdc++) | `error: call to 'forward' is ambiguous` |
| builtin `__remove_reference_t(T)` (как в libc++) | компилируется молча |

То есть ADL находит `std::forward` в обоих случаях (это проверено отдельно:
если убрать `stl::forward`, вызов на Apple clang разрешается в `std::forward` и
сопровождается предупреждением `-Wunqualified-std-cast-call`). Молчит не поиск
имени, а разрешение перегрузки: для builtin-формы clang умеет вывести аргумент
и отранжировать кандидатов, для классового трейта — нет, и кандидаты остаются
равными.

Вывод для практики: **любой Linux — что gcc, что clang — берёт libstdc++ и
видит ошибку; macOS берёт libc++ и не видит.** Локальная сборка на Darwin в
принципе не может поймать этот класс дефекта.

### Что изменено

Два файла, шесть строк по существу:

* `ext/libstd/std/mem/obj_pool.h` — три вызова (`makeImpl`, и обе ветки
  `make`) квалифицированы как `stl::forward<A>(a)`.
* `ext/libstd/std/mem/embed.h` — конструктор `Embed`, туда же.

`embed.h` пришлось взять потому, что он — вторая половина того же пути:
`ObjPool::make()` заворачивает `T` в `Embed<T>`, и после починки одного лишь
`obj_pool.h` ошибка просто переехала на `embed.h:12` с той же формулировкой
(это видно в логе итерации). Оба файла из `ext/libstd`, поэтому ниже отдельная
проверка, что остальные пользователи библиотеки не пострадали.

Квалификация имени — минимальная из возможных правок и единственная, которая
не меняет поведение: внутри `namespace stl` `stl::forward` — это ровно тот
кандидат, который и выбирался бы, не будь ADL. Альтернативы отвергнуты:
переименовать `stl::forward` — сломает весь `libstd` и его пользователей;
обернуть аргументы в `static_cast` — многословнее и теряет смысл;
поймать флагом компиляции — нельзя, ADL не выключается.

Комментарий с объяснением стоит один раз в `obj_pool.h`, `embed.h` на него
ссылается — чтобы следующий, кто пишет здесь неквалифицированный `forward`,
понял, почему нельзя, не выясняя это заново через CI.

---

## Критерии приёмки

Способ воспроизведения выбран так: `docker info` отвечает, поэтому взяты
контейнеры — они дают ровно те тулчейны, что в CI, и позволяют прогнать
сравнение «до/после» на одном и том же дереве. `nix` на машине тоже есть, но
`nix build` собирает всю замкнутую среду и на порядок дороже, а
`brew`-компилятора с `-std=c++26` в системе нет вовсе (`brew --prefix llvm`
указывает на несуществующий каталог, `gcc-16` от Homebrew есть, но
контейнер точнее воспроизводит именно CI). Использованы два образа:

* `alpine:3.24` + `gcc g++ musl-dev linux-headers` — тот же образ и тот же
  тулчейн, что в джобе `Tests Alpine` (gcc 15.2.0, musl);
* `debian:trixie-slim` + `g++ clang` — второй libc (glibc) и clang с
  libstdc++, то есть путь джоб `Tests UBSan` / `Tests ASan`.

Компиляция ведётся точечно, по двум затронутым единицам трансляции, с
include-путями из `build.py` (`build.includes += ["$(B)", "$(S)",
"$(S)/lib/shitty", "$(S)/ext"]` плюс `-Iext/libstd`). Полный `./build test` в
контейнере не запускался: он требует vulkan, wayland, harfbuzz, freetype,
ragel и glslang, и его отказ ничего не сказал бы про эти два дефекта.

| # | Критерий | Чем подтверждён |
| --- | --- | --- |
| 1 | Локальная сборка macOS зелёная | ниже, `./build st --clear`, 224 узла, exit 0 |
| 2 | `unit_tests` зелёные | ниже, `OK: 955`, exit 0 |
| 3 | Оба дефекта воспроизведены **до** и отсутствуют **после** | ниже, alpine + debian, gcc и clang |
| — | Правка в `ext/libstd` не сломала прочих пользователей | ниже, сплошной syntax-проход до/после |

### 1. Локальная сборка macOS

```
$ ./build st --clear
...
[CC] {221/224} $(B)/obj/libshitty/lib/shitty/font_embedded.cpp.o
[CC] {222/224} $(B)/obj/libshitty/lib/shitty/parser.cpp.o
[AR] {223/224} $(B)/libshitty_prod.a
[LD] {224/224} $(B)/st
EXIT=0
```

### 2. `unit_tests`

`./build st` не строит тесты, поэтому цель собрана отдельно — и вместе с
`pty_test_helper`, который иначе не попадает в граф:

```
$ ./build unit_tests pty_test_helper
...
[AR] {124/125} $(B)/libshitty_test.a
[LD] {125/125} $(B)/unit_tests
EXIT=0

$ SHITTY_PTY_TEST_HELPER=$PWD/.build/pty_test_helper ./.build/unit_tests
...
+ WindowSizingRequests::TheMinimumSizeAndTheResizeUnitPairEachAxisWithItsOwnInsets
OK: 955
EXIT=0
```

Девять `Pty::*` тестов в прогоне, включая
`Pty::EveryPanesChildIsBornWithThatPanesSize` — тот самый тест из `T5`, чья
фикстура и вызывает `make<TeeHandle>(..., std::string&)`. То есть код,
провоцирующий дефект 2, действительно исполняется, а не просто компилируется.

### 3. Воспроизведение до и после

#### Alpine 3.24, gcc 15.2.0, musl — джоба `Tests Alpine`

**До** (`git stash`, то же дерево без правок):

```
$ docker run --rm -v $PWD:/src -w /src g4-alpine gcc -c -o /tmp/h.o tst/pty_test_helper.c
tst/pty_test_helper.c: In function 'wait_for_hangup':
tst/pty_test_helper.c:127:5: error: implicit declaration of function 'cfmakeraw' [-Wimplicit-function-declaration]
  127 |     cfmakeraw(&term);
      |     ^~~~~~~~~

$ docker run ... g++ -std=c++26 -fsyntax-only -I. -Iext -Iext/libstd -Ilib/shitty -I.build lib/shitty/pty_ut.cpp
ext/libstd/std/mem/obj_pool.h: In instantiation of 'T* stl::ObjPool::make(A&& ...)
  [with T = {anonymous}::TeeHandle; A = {PtyHandle&, std::__cxx11::basic_string<char, ...>&}]':
lib/shitty/pty_ut.cpp:271:41:   required from here
  271 |             return owner.make<TeeHandle>(*inner, heard[spawns++]);
ext/libstd/std/mem/obj_pool.h:51:54: error: call of overloaded
  'forward<std::__cxx11::basic_string<char, ...>&>(std::__cxx11::basic_string<char>&)' is ambiguous
  note: candidate 1: 'constexpr T&& stl::forward(rem_ref<T>&) ...'   [std/typ/support.h:30]
  note: candidate 2: 'constexpr _Tp&& std::forward(...)' ...          [/usr/include/c++/15.2.0/bits/move.h:72]
```

**После:**

```
--- defect 1: pty_test_helper.c (gcc/musl) ---
PASS: compiled clean          # gcc -Wall -Wextra, ни ошибок, ни предупреждений
--- defect 2: pty_ut.cpp (g++ 15 / libstdc++ / -std=c++26) ---
PASS: syntax clean
```

#### Debian trixie, gcc 14.2.0 и clang 19.1.7, glibc — второй libc и путь санитайзеров

Один и тот же контейнер прогнан на дереве после правок и на нём же под
`git stash`:

```
########## ПОСЛЕ ##########
gcc: gcc (Debian 14.2.0-19) 14.2.0   clang: Debian clang version 19.1.7 (3+b1)
--- [1] gcc/glibc: pty_test_helper.c ---
  -> compiles
--- [2] clang/libstdc++: pty_ut.cpp ---
  -> compiles

########## ДО ##########
gcc: gcc (Debian 14.2.0-19) 14.2.0   clang: Debian clang version 19.1.7 (3+b1)
--- [1] gcc/glibc: pty_test_helper.c ---
tst/pty_test_helper.c:127:5: error: implicit declaration of function 'cfmakeraw' [-Wimplicit-function-declaration]
--- [2] clang/libstdc++: pty_ut.cpp ---
ext/libstd/std/mem/obj_pool.h:51:44: error: call to 'forward' is ambiguous
1 error generated.
```

Последняя строка — дословно диагностика из лога CI, вместе с колонкой `51:44`.

### Проверка, что `ext/libstd` не сломан для остальных

Сплошной `-fsyntax-only` по всем `.cpp` в `lib/`, `ext/libstd/` и `bin/` в
контейнере Alpine, на дереве после правок и под `git stash` — до:

```
AFTER:  16 failing TUs
BEFORE: 17 failing TUs

=== ушли (были сломаны, стали собираться) ===
FAIL lib/shitty/pty_ut.cpp
=== появились (регрессии) ===
                                    (пусто)
=== остались сломанными в обоих ===
bin/pt/main.cpp, bin/st/main.cpp, lib/shitty/brand.cpp, font_embedded.cpp,
font_freetype.cpp, heap_profile.cpp, input_remap.cpp, options.cpp, parser.cpp,
render.cpp, render_vk.cpp, startup_ut.cpp, terminal_colors.cpp, toml.cpp,
vterm.cpp, lib/vterm/unicode.cpp
```

Ровно одна единица трансляции сменила состояние, и ровно та, ради которой
делалась правка; новых поломок нет. Шестнадцать оставшихся ломаются в голом
контейнере не из-за кода, а из-за отсутствия того, что даёт настоящая сборка, —
проверено выборочно:

```
lib/shitty/parser.cpp:30:37:  fatal error: parser.rl.h: No such file or directory      # генерируется ragel
lib/vterm/unicode.cpp:9:10:   fatal error: unicode_data.h: No such file or directory   # генерируется сборкой
lib/shitty/render_vk.cpp:43:  fatal error: vulkan/vulkan.h: No such file or directory  # системная зависимость
lib/shitty/startup_ut.cpp:84: error: 'SHITTY_VERSION' was not declared in this scope   # -D из build.py
```

---

## Изменённые файлы

| Файл | Что |
| --- | --- |
| `tst/pty_test_helper.c` | `#define _DEFAULT_SOURCE` до включений — открывает `cfmakeraw()` в glibc и musl |
| `ext/libstd/std/mem/obj_pool.h` | три `forward<A>(a)` → `stl::forward<A>(a)`; комментарий с разбором причины |
| `ext/libstd/std/mem/embed.h` | `forward<A>(a)` → `stl::forward<A>(a)` в конструкторе `Embed` |
| `docs/reports/G4-linux-compile-2026-09-03.md` | этот отчёт |

`build.py` не тронут — он принадлежит задаче `G3`, и для этой починки не
понадобился.

---

## Что осталось за рамками

Проверялись компиляцией только две единицы трансляции из логов CI плюс
сплошной syntax-проход. Ни одна джоба CI целиком локально не воспроизводилась:
`./build test` требует vulkan, wayland, harfbuzz, freetype, ragel и glslang, а
санитайзерные джобы — ещё и своей конфигурации сборки. Остаточный риск в том,
что за этими двумя ошибками в тех же джобах стоят следующие, которые компилятор
просто не успел показать. Судя по логам `gh run view 33314255766 --log-failed`
других ошибок компиляции там не было, но подтвердить это может только зелёный
прогон CI.

---

## Обнаружено

* **Та же мина лежит ещё в шести заголовках `libstd`** — девять
  неквалифицированных `forward<...>` в шаблонах, доступных пользователю:
  `std/sym/h_map.h:62,69`, `std/map/map.h:33,47,76`, `std/ptr/refcount.h:36`,
  `std/mem/obj_list.h:32`, `std/mem/small_obj_allocator.h:31`,
  `std/alg/qsort.h:130`. Каждая сработает в тот день, когда в соответствующий
  шаблон впервые передадут аргумент из `std`. Не трогал: к падению CI они не
  относятся, а правка чужого общего кода без нужды — лишний риск. Стоит закрыть
  отдельной задачей одним проходом.
* **У clang есть предупреждение ровно на этот класс дефекта** —
  `-Wunqualified-std-cast-call`. Оно, правда, срабатывает только когда
  `std::forward` побеждает, а не когда кандидаты равны, так что для дефекта 2 в
  его нынешнем виде оно бесполезно. Полезно как страховка после того, как
  предыдущий пункт будет закрыт.
* **Локальная сборка на Darwin структурно не может поймать дефекты этого
  класса.** Дело не в версии компилятора — Apple clang 21 тот же самый, что в
  CI, — а в libc++ против libstdc++. Пока в CI нет джобы, компилирующей на
  Darwin с чужой стандартной библиотекой (её и не бывает), единственная защита
  от повторения — Linux-джоба на PR.
* **`./build st` не строит тесты.** Из 224 узлов ни один не компилирует
  `*_ut.cpp`; `unit_tests` — отдельная цель со своими 125 узлами, а
  `pty_test_helper` не тянется даже ею и нуждается в явном упоминании в
  командной строке. «Сборка зелёная» после одного `./build st` не значит, что
  тесты вообще компилировались, — это тот же провал измерения, что уже описан
  в комментарии внутри `pty_test_helper.c` (R2-test, I11).
* **`brew --prefix llvm` на этой машине указывает на пустоту** —
  `/opt/homebrew/opt/llvm` не существует, хотя команда отвечает успехом.
  Подсказка в `build.py` («brew install llvm») здесь бы не сработала; сборка
  идёт на Apple clang 21, который `-std=c++26` уже понимает, так что проверка
  в `build.py` проходит.

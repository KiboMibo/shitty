# G3: сборка под Darwin — неопределённая ссылка на `simdutf::binary_to_base64`

- **Задача:** вне плана, делегированная — починить джобу `Build Darwin` в GitHub Actions
- **Дата:** 2026-09-03
- **Дерево:** `scratchpad/wt-G3`, ветка `fix/G3-darwin-link` от `master`
- **Коммиты:** `05f12f35` (код), отчёт — отдельным коммитом
- **Статус:** **исправлено и проверено локально в воспроизведённых условиях CI; зелёный `Build Darwin` подтверждает CI, а не этот отчёт**

---

## 1. Что было сломано и почему это не видно локально

Дефект собран из двух половин, каждая из которых по отдельности выглядит осмысленно.

**Половина первая — исходник решает по заголовку.** `lib/vterm/base64.cpp:13`
выбирал реализацию `base64Encode` так:

```cpp
#if __has_include(<simdutf.h>)
    #include <simdutf.h>
    #define SHITTY_BASE64_SIMDUTF 1
#else
    #define SHITTY_BASE64_SIMDUTF 0
#endif
```

То есть **признак — наличие заголовка на include-пути компилятора**.

**Половина вторая — сборка решает по pkg-config.** `build.py:177` (до правки):

```python
optional_pkg = (lambda *pkgs: pkg_config(*pkgs, required=False)) if not darwin else (lambda *pkgs: dependency(enabled=False))
...
simdutf = optional_pkg("simdutf >= 6.5.0")
```

На darwin **все** необязательные pkg-config-зависимости объявлены выключенными —
это коммит `62cef373` от 2026-08-25 («macOS: сборка без библиотек Homebrew по
умолчанию»). Выключенная зависимость выпадает и из флагов компиляции
(`build:1274`), и из строки линковки (`build:1306`, `build:1322`), поэтому
`-lsimdutf` на darwin не появляется никогда. Комментарий над строкой при этом
говорит только про FreeType-бэкенд — про simdutf там не сказано ничего, и это
второй по важности симптом: решение приняли шире, чем описали.

**Как они складываются.**

| Среда | `simdutf.h` на include-пути | `-lsimdutf` на линковке | Что компилируется | Итог |
|---|---|---|---|---|
| Linux CI (Alpine/Fedora/Nix) | да | да (pkg-config находит) | ветка simdutf | сходится |
| macOS локально | **нет** | нет | запасная ветка | сходится **случайно** |
| macOS под Nix (`Build Darwin`) | **да** | **нет** | ветка simdutf | **`Undefined symbols`** |

Заголовок в CI есть потому, что `flake.nix:143` кладёт `simdutf` в
`buildInputs`, а Nix прописывает include-каталоги всех build inputs в
`NIX_CFLAGS_COMPILE`, то есть в путь поиска компилятора напрямую — минуя
pkg-config, который в этот момент отключён нашей же darwin-веткой.

Локально заголовка на пути поиска нет: Homebrew ставит его в
`/opt/homebrew/Cellar/simdutf/9.1.0/include`, а clang туда без `-I` не ходит.
Проверено прямо:

```console
$ printf '#if __has_include(<simdutf.h>)\n#error HEADER_VISIBLE\n#endif\nint main(){return 0;}\n' > /tmp/probe.cpp
$ c++ -fsyntax-only /tmp/probe.cpp && echo "заголовок НЕ виден"
заголовок НЕ виден
```

Отсюда и невидимость дефекта: локальная macOS-сборка идёт по запасной ветке —
то есть **мимо** сломанного кода. Голая пересборка на Mac никогда бы ничего не
показала, сколько её ни повторяй.

**Корень.** Решение «что компилировать» и решение «что линковать» принимались по
двум разным признакам — по наличию заголовка и по результату pkg-config. Пока
эти признаки совпадают, всё работает; Nix их развёл, и сборка развалилась.

---

## 2. Что изменено и почему именно так

Изменены два файла. Правка — 9 строк кода и 11 строк комментария.

### `build.py`

```python
simdutf = optional_pkg("simdutf >= 6.5.0")
if simdutf:
    simdutf.public_cppflags += ["-DHAVE_SIMDUTF=1"]
```

Define повешен **не в глобальные `build.cppflags`**, а на сам объект
зависимости — на тот самый `Target`, из чьих `ldflags` берётся `-lsimdutf`.
Это и есть механизм из критерия 4: раннер обходит зависимости одним и тем же
способом и для флагов компиляции (`_usage_compile_flags`, `build:1271-1285`), и
для флагов линковки, и в обоих случаях выкидывает `enabled=False` (`build:1274`,
`build:1306`, `build:1322`). Разъехаться нечему: это один объект в двух ролях.

Комментарий над `optional_pkg` дополнен — теперь он описывает и simdutf
(«не шрифтовый пакет, но такой же Homebrew-dylib, а у base64.cpp есть портируемый
путь»), а не только FreeType.

### `lib/vterm/base64.cpp`

```cpp
#if defined(HAVE_SIMDUTF)
    #include <simdutf.h>
#endif
...
Buffer& base64Encode(StringView input, Buffer& output) {
#if defined(HAVE_SIMDUTF)
```

Локальный макрос `SHITTY_BASE64_SIMDUTF` удалён — он был лишним звеном.
`#if defined(HAVE_X)` — принятая в репозитории форма: так же написаны
`HAVE_METAL_RENDERER` (`lib/shitty/render.cpp:14`), `HAVE_VULKAN_WAYLAND`
(`lib/shitty/test_mode.cpp:36`), `HAVE_FREETYPE`/`HAVE_HARFBUZZ`
(`lib/shitty/font.cpp:14`), `HAVE_CORETEXT` (`lib/shitty/font_coretext.cpp:9`).

### Что сознательно **не** сделано

**Не расширен `optional_pkg` на darwin.** Соблазнительная однострочная правка —
исключить `simdutf` из darwin-запрета, чтобы pkg-config его находил и
`-lsimdutf` появлялся. Она бы тоже озеленила CI, но это лечение симптома:
`__has_include` осталось бы на месте, и следующая среда, где заголовок и
библиотека разойдутся, сломает сборку снова. Кроме того, локально
`pkg-config --exists simdutf` **проходит** (Homebrew 9.1.0), так что правка
вернула бы линковку Homebrew-dylib в дефолтную macOS-сборку — ровно то, что
`62cef373` убирал.

**Не тронут `flake.nix`.** Он остаётся корректным: `simdutf` в `buildInputs`
нужен для Linux-сборок из того же списка. На darwin он теперь просто не
используется (см. «Обнаружено»).

**Не добавлен линтер на `__has_include`.** Правило масштаба репозитория было бы
уместно, но семь оставшихся мест — в `ext/libstd/**`, который принадлежит задаче
G4 и подключается как импортированный граф; `dev/style.py` — форматтер, а не
проверялка, и в CI как gate не запускается. Запрет без места, где он
срабатывает, — мёртвый код. Вынесено в «Обнаружено».

---

## 3. Критерии приёмки

### Критерий 1 — локальная сборка на macOS зелёная

```console
$ ./build st --clear
...
[CC] {222/224} $(B)/obj/libshitty/lib/shitty/parser.cpp.o
[AR] {223/224} $(B)/libshitty_prod.a
[LD] {224/224} $(B)/st
EXIT_ST=0
```

224 узла, все зелёные. **Закрыт.**

### Критерий 2 — `unit_tests` зелёные

```console
$ ./build unit_tests pty_test_helper && SHITTY_PTY_TEST_HELPER=$PWD/.build/pty_test_helper ./.build/unit_tests --threads=1
...
+ WindowSizingRequests::TheMinimumSizeAndTheResizeUnitPairEachAxisWithItsOwnInsets
OK: 955
EXIT_UT=0
```

955 тестов, ни одного падения; среди них 10 тестов `Base64::*`
(`EncodesKnownVectors`, `KittyVectorsAcceptPaddedAndUnpaddedInput`,
`DecodesInPlace`, `AcceptsCanonicalUnpaddedTail`, `EncodeReturnsCallerBuffer`
и другие). **Закрыт.**

### Критерий 3 — условие CI воспроизведено локально и сходится

**Способ и его обоснование.** `nix build` на этой машине недоступен:
`nix-daemon` не загружен (`launchctl print system/org.nixos.nix-daemon` →
`Could not find service`), `/nix/store` принадлежит `root:nixbld`, sudo без
пароля не проходит. Поэтому воспроизведено то, что CI делает **по существу**:
Nix добавляет include-каталог simdutf в флаги компилятора помимо pkg-config.
Ровно это даёт `CPPFLAGS`, который `build` подмешивает в каждую компиляцию
(`build:284`, `build:1177`). Каталог берётся у pkg-config, чтобы он содержал
**только** заголовки simdutf — как и store-path в Nix:

```console
$ pkg-config --cflags simdutf
-I/opt/homebrew/Cellar/simdutf/9.1.0/include
```

**До правки** (коммит `ae802cfc`):

```console
$ CPPFLAGS="-I/opt/homebrew/Cellar/simdutf/9.1.0/include" ./build st --clear
...
[AR] {223/224} $(B)/libshitty_prod.a
FAIL $(B)/st: command exited 1: /usr/bin/c++ --target=aarch64-apple-darwin23.6.0 ...
Undefined symbols for architecture arm64:
  "simdutf::binary_to_base64(char const*, unsigned long, char*, simdutf::base64_options)", referenced from:
      base64Encode(stl::StringView, stl::Buffer&) in libshitty_prod.a[49](base64.cpp.o)
ld: symbol(s) not found for architecture arm64
clang++: error: linker command failed with exit code 1
EXIT=1
```

Символ, функция и объектный файл совпадают с логом CI (`gh run view 33314255766`)
дословно.

**После правки** — та же команда:

```console
$ CPPFLAGS="-I/opt/homebrew/Cellar/simdutf/9.1.0/include" ./build st --clear
...
[AR] {223/224} $(B)/libshitty_prod.a
[LD] {224/224} $(B)/st
EXIT=0
```

Заголовок на пути — а бинарь линкуется, потому что `HAVE_SIMDUTF` не определён и
`base64.cpp` компилирует портируемую ветку. **Закрыт.**

### Критерий 4 — дефект не может вернуться тихо

Механизм — **один объект вместо двух признаков**. Define `-DHAVE_SIMDUTF=1`
лежит в `public_cppflags` того же `Target`, чьи `ldflags` содержат `-lsimdutf`;
раннер выкидывает выключенный `Target` и из компиляции, и из линковки. Состояние
«define есть, библиотеки на линковке нет» перестало быть выразимым: чтобы TU
увидел `HAVE_SIMDUTF`, `simdutf` должен быть в замыкании зависимостей цели, а
значит и на её строке линковки.

Проверено обеими сторонами, а не только рассуждением.

**Выключено** (дефолт macOS, `./build st --clear` из критерия 1):

```console
$ nm .build/libshitty_prod.a | grep -c simdutf
0
$ otool -L .build/st | grep -i simdutf
(пусто)
```

**Включено** — darwin-запрет во `optional_pkg` временно снят
(`if not darwin` → `if True`, сборка в отдельный `-B .build-coupled`,
`build.py` восстановлен сразу после; в коммит эта правка не входит):

```console
$ ./build st --clear -B .build-coupled
...
[LD] {224/224} $(B)/st
EXIT=0

$ nm .build-coupled/grb/.../obj/libshitty/lib/vterm/base64.cpp.o | grep simdutf
                 U __ZN7simdutf16binary_to_base64EPKcmPcNS_14base64_optionsE

$ otool -L .build-coupled/st | grep -i simdutf
	/opt/homebrew/opt/simdutf/lib/libsimdutf.35.dylib (compatibility version 35.0.0, current version 35.0.0)
```

Ссылка на символ в объектнике (значит, define дошёл до TU) и dylib на строке
линковки появились **вместе**, одним переключателем.

Остаточное поведение при обратном расхождении — если `HAVE_SIMDUTF` окажется
определён там, где заголовка нет, — это `fatal error: 'simdutf.h' file not
found` на этапе компиляции: внятная ошибка с именем файла, а не `Undefined
symbols` в конце сборки. **Закрыт.**

---

## Обнаружено

- **Тот же класс дефекта живёт в `ext/libstd`, и он не теоретический.** Первая
  попытка воспроизвести условие CI широким `CPPFLAGS=-I/opt/homebrew/include`
  сломала сборку раньше, чем дошла до линковки:
  `ext/libstd/std/net/ssl_socket.cpp:27: fatal error: 'mbedtls/ctr_drbg.h' file
  not found`. `ssl_socket.cpp:25` включает TLS-бэкенд по
  `__has_include(<mbedtls/ssl.h>)`, находит его у Homebrew, а соседний заголовок
  из того же набора не находит. То есть добавление постороннего include-каталога
  ломает сборку libstd. Файл принадлежит G4 — не трогал.

- **Ещё пять мест с тем же признаком**, все в `ext/libstd/**`:
  `std/net/ssl_socket.cpp` (openssl / s2n / mbedtls), `std/dns/ares.cpp` (c-ares),
  `std/str/hash.cpp` (rapidhash / xxhash), `std/thr/io_uring.cpp` и
  `tst/io_uring_msg.cpp` (liburing), `tst/test.cpp` (cpptrace).
  (`std/tst/ctx.cpp` с `<execinfo.h>` — системный заголовок, безобиден.)

- **Для libstd в `build.py` уже есть обходной приём — «зеркало».**
  `build.py:248-269` честно описывает ту же проблему («libstd picks its backends
  with `__has_include`, so which libraries the archive needs at link time depends
  on the headers this target has») и решает её вторым таким же зондом:
  `have_header("liburing.h")` → `-luring`, `have_header("rapidhash.h")` /
  `have_header("xxhash.h")` → `-lxxhash`. Это работает, но признаков по-прежнему
  два, и совпадать они обязаны по договорённости, а не по устройству. Для
  `lib/vterm` этот приём не нужен: граф наш, а не импортированный, — поэтому
  выбран единый источник. Если G4 или кто-то после будет разбирать libstd,
  зеркало — минимально безопасный вариант, единый источник — правильный.

- **Комментарий в `build.py:267-269` описывает мину замедленного действия:**
  TLS- и DNS-бэкенды детектируются так же, но их библиотеки сознательно не
  добавлены в `libstd_backends`, потому что «nothing here references them».
  Как только что-нибудь в форке начнёт использовать `SslSocket` или резолвер,
  сборка упадёт ровно тем же `Undefined symbols` — и снова только в той среде,
  где заголовок есть.

- **`brotli_common` не используется ни одним исходником репозитория.** Ни в
  `lib/`, ни в `bin/`, ни в `ext/plt` нет ни одного упоминания brotli; пакет
  попадает в `libshitty_deps` и сбрасывается в `dependency()`, когда нет
  FreeType-бэкенда (`build.py:194`). Похоже, он там ради статической линковки
  FreeType (woff2). Проверять и трогать не стал.

- **`flake.nix:143` теперь кладёт `simdutf` в `buildInputs` и для darwin впустую.**
  Список `buildInputs` общий для платформ, на Linux пакет нужен, поэтому убирать
  его нельзя без `lib.optionals`. Если команда решит, что под Nix (где нет
  проблемы `brew upgrade` — пути в сторе неизменяемы) macOS-сборка *должна*
  использовать simdutf, это отдельное решение: включить `simdutf` в
  `optional_pkg` и на darwin. Механизм из этой задачи такое включение выдержит
  без единой правки в `base64.cpp` — что, собственно, и проверено в критерии 4.

- **`nix build` на этой машине не запускается** — `nix-daemon` не загружен, sudo
  без пароля недоступен. Следующему агенту, которому понадобится Nix локально:
  начинать с `launchctl print system/org.nixos.nix-daemon`, а не с `nix build`.

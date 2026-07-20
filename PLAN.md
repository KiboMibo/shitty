По коду уже видно главный архитектурный момент: state machine живёт в VtermImpl::processInput, но её публичный вход сейчас обёрнут в тяжёлый Vterm, которому нужны Frame, global
  opts, Pty и host callbacks. Для быстрого in-process fuzzing нужен либо узкий headless factory, либо — лучше — выделенный parser/state объект. Теперь сверяю состояние движков и CI-
  инфраструктуры на сегодня.

◦ Searching the web

• Searched the web for site:llvm.org/docs/LibFuzzer.html libFuzzer maintenance mode official ...

◦ Searching the web

• Searched the web for github google fuzztest README Centipede fuzzing engine C++ official ...

◦ Searching the web

• Searched the web for https://github.com/google/fuzztest

◦ Searching the web

• Searched the web for 'successor'

◦ Searching the web

• Searched the web for site:github.com/google/fuzztest custom features coverage feedback Centipede state machine ...

• Context compacted

─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────

• Вердикт: для нас сейчас лучший первый двигатель — по-прежнему Clang/libFuzzer + ASan/UBSan. Не потому что он самый модный: libFuzzer находится в режиме поддержки без крупных новых
  функций. Но у нас уже Clang, вход естественно представляется массивом байтов, а стандартный LLVMFuzzerTestOneInput затем без переписывания запускается под AFL++ и honggfuzz.
  Документация libFuzzer (https://llvm.org/docs/LibFuzzer.html)

  ## Что сейчас используют

  - libFuzzer — самый простой и надёжный старт для C/C++. In-process, coverage-guided, входит в LLVM. Для нашей задачи подходит почти идеально.
  - AFL++ — активно развивается, хорош для долгих параллельных прогонов, persistent mode, CMPLOG, сложное расписание корпуса и custom mutators. Его драйвер понимает
    LLVMFuzzerTestOneInput, так что отдельный harness не нужен. AFL++ fuzzing guide (https://aflplus.plus/docs/fuzzing_in_depth/)

  - Google FuzzTest — модная современная комбинация property-based и coverage-guided fuzzing, фактически преемник libFuzzer/Centipede внутри Google. Но тянет GoogleTest/Abseil/RE2 и
    ориентирован прежде всего на CMake/Bazel. Для сырого потока VT-байтов сейчас даст нам больше интеграционной возни, чем пользы. FuzzTest (https://github.com/google/fuzztest),
    объединение Centipede с FuzzTest (https://github.com/google/fuzztest/blob/main/centipede/USER_MIGRATION.md)

  - honggfuzz — нормальный третий движок для ensemble-fuzzing, тоже умеет libFuzzer-compatible harness. Не первая интеграция.
  - LibAFL — конструктор для разработки собственных фаззеров на Rust. Нам не надо строить фаззер как исследовательский проект.
  - OSS-Fuzz / ClusterFuzzLite — не движки, а инфраструктура непрерывного фаззинга. Имеет смысл подключить после стабилизации локального target. OSS-Fuzz
    (https://google.github.io/oss-fuzz/), ClusterFuzzLite (https://google.github.io/clusterfuzzlite/)

  Моя схема: libFuzzer локально и в CI → тот же harness под AFL++ для длительных сессий → потом OSS-Fuzz/ClusterFuzzLite.

  ## Что уже есть и чего не хватает

  Нынешний tests/fuzz_parser.py полезен, но это не настоящий coverage-guided fuzzer. Он генерирует фиксированные случаи, запускает целый zutty через процесс/socket и сравнивает
  цельную подачу с нарезанной.

  Его следует оставить как black-box/metamorphic smoke test. Новый фаззер должен:

  - работать в одном процессе;
  - подавать произвольные байты непосредственно в парсер;
  - собирать coverage внутри vterm.cpp;
  - не инициализировать SDL, Vulkan, шрифты, окно и настоящий PTY;
  - пересоздавать терминал для каждого input, но делать несколько feed() внутри одного input.

  ## Как встроить в наш код

  Сейчас state machine находится внутри VtermImpl::processInput() в vterm.cpp, а все переходы сходятся через setState(). Это очень хорошая точка для фаззинга.

  Первый этап я бы сделал без большого рефакторинга:

  1. Выделить CPU-часть в terminal_core:
      - vterm.cpp
      - frame.cpp
      - UTF-8/grapheme
      - terminal types
      - OSC/base64 и необходимые вспомогательные файлы
      - vendored libstd

     В цель не должны попасть main.cpp, SDL, Vulkan и загрузка шрифтов.

  2. Написать fuzz/vterm_parser_fuzz.cpp с обычным интерфейсом:

  extern "C" int
  LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
  {
      FuzzTerminal terminal{16, 8};
      terminal.feed(data, size);
      terminal.verify();
      return 0;
  }

  3. FuzzTerminal создаёт:
      - маленький Frame, например 16×8;
      - фальшивый Pty, который только собирает ответы терминала;
      - VtermHostCallbacks без графики;
      - детерминированные opts;
      - новый ObjPool и новый Vterm на каждую итерацию.

  VtermHost и Pty уже достаточно абстрактны, поэтому графический backend сюда тащить не потребуется. CPU-модель экрана я бы оставил: она даёт намного больше проверяемых инвариантов,
  чем голая проверка «не упало».

  Также стоит добавить перегрузку вроде:

  void Vterm::feedPtyOutput(std::span<const uint8_t>);

  Чтобы фаззер не создавал std::string и не копировал input на каждом запуске.

  ## Какие targets нужны

  Я бы сразу разделил три режима:

  - vterm_bytes_fuzz: весь blob подаётся одним вызовом. Максимальная скорость.
  - vterm_stream_fuzz: первый байт определяет нарезку — целиком, побайтно или чанками 1–32 байта. Проверяет сохранение промежуточного состояния между feed().
  - vterm_chunking_fuzz: два терминала получают одинаковый payload целиком и кусками; затем сравниваются экран, ответы PTY и действия host. Это быстрый in-process вариант нынешнего
    Python-теста.

  Не надо сохранять состояние терминала между отдельными вызовами LLVMFuzzerTestOneInput: результат тогда начнёт зависеть от порядка обхода corpus.

  ## Что проверять кроме падений

  ASan/UBSan сами поймают use-after-free, overflow, OOB и UB. Поверх этого нужен Vterm::verify() или Frame::verify():

  - курсор и margins находятся в допустимых пределах;
  - размеры строк соответствуют экрану;
  - правая половина wide character имеет корректную левую половину;
  - grapheme/cell IDs существуют;
  - primary/alternate frame и scrollback структурно корректны;
  - parser buffers соблюдают свои лимиты;
  - ответы в fake PTY не растут без ограничения;
  - цельная и нарезанная подача дают одинаковое состояние.

  verify() можно компилировать только при ZUTTY_FUZZING/debug либо сделать дешёвую часть обычным assert-контрактом.

  Для переходов LLVM уже инструментирует ветвления, switch и сравнения через SanitizerCoverage. Полезно запускать с -use_value_profile=1: это помогает находить интересные
  CSI-значения и границы числовых параметров. SanitizerCoverage (https://clang.llvm.org/docs/SanitizerCoverage.html)

  Отдельную ручную «coverage matrix (old_state,new_state)» я бы пока не городил. Сначала посмотрим реальный coverage: switch(InputState) и места вызова setState() уже должны хорошо
  различаться. Если упрёмся в плато, тогда можно добавить специальную семантическую instrumentation в setState().

  ## Сборка

  Core должен быть инструментирован, недостаточно навесить sanitizer только на harness:

  core compile:
    -fsanitize=fuzzer-no-link,address,undefined
    -fno-omit-frame-pointer
    -fno-sanitize-recover=all

  fuzzer link:
    -fsanitize=fuzzer,address,undefined

  fuzzer-no-link как раз предназначен для библиотек, которые потом линкуются с libFuzzer. Документация libFuzzer (https://llvm.org/docs/LibFuzzer.html)

  В нашем build.py я бы завёл отдельный инструментированный вариант terminal_core_fuzz, потому что обычный terminal_core.a не будет давать coverage. Через окружение те же sanitizer
  flags должны уйти в импортируемый vendored libstd.

  В Nix отдельная зависимость для libFuzzer не нужна: runtime приходит с Clang/compiler-rt. AFL++ можно добавить отдельным пакетом для fuzz-shell, не заставляя обычного пользователя
  устанавливать его.

  CI запускает только ограниченный replay:

  vterm_parser_fuzz corpus/parser \
      -runs=50000 \
      -max_len=16384 \
      -timeout=5

  Бесконечный -max_total_time=0 остаётся ручной/ночной целью.

  ## Corpus и dictionary

  Начальный corpus надо собрать из последовательностей текущих parser-тестов и добавить отдельные файлы для:

  - ESC, CSI, OSC, DCS и ST;
  - 7-битных и C1-вариантов;
  - CAN/SUB в каждом промежуточном состоянии;
  - неполного UTF-8;
  - CSI prefixes/intermediates;
  - OSC/DCS непосредственно около лимитов;
  - VT52-переходов;
  - неизвестных и обрезанных последовательностей.

  Dictionary: ESC [, ESC ], ESC P, ESC \, BEL, CSI/OSC/DCS C1, ?, >, !, ", $, ;, :, а также популярные финальные байты m/H/J/K/c/n/r/t.

  Структурный custom mutator для цельных escape sequences возможен — AFL++ это поддерживает, включая C/C++ и Python mutators. Но добавлять его стоит только после обычного havoc:
  malformed-последовательности зачастую находят самые интересные баги. AFL++ custom mutators (https://aflplus.plus/docs/custom_mutators/)

  ## Последующий рефакторинг

  После первого работающего phase fuzzing я бы вынес сам автомат:

  bytes → VtParser → VtParserSink → VtermImpl/Frame
               ↘ pure-parser fuzzer

  VtParser владеет только:

  - текущим InputState;
  - UTF-8 decoder;
  - CSI parameters/prefix/intermediates;
  - OSC/DCS buffers.

  А VtermImpl реализует sink: печать, CSI, OSC, DCS и изменение Frame.

  Это даст действительно чистый фаззер автомата с очень высоким exec/s, но начинать с такого рефакторинга рискованно: сначала нынешнюю семантику лучше зафиксировать corpus’ом и in-
  process metamorphic target.

  Итого: не меняем libFuzzer API, берём ASan+UBSan, делаем headless CPU harness и три targets, сохраняем Python-тест как внешний oracle. AFL++ запускаем поверх того же harness.
  FuzzTest пока не тащим.

─ Worked for 5m 27s ─────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────


› не совсем со всем согласен. не надо рефакторить код на части. в build.py собери все, кроме main.cpp, в libzutty. сделай два таргета - один zutty, линкует main.cpp и libzutty, и
  fuzz_zutty, который слинкует libzutty и main_fuzz.cpp. В обычной жизни второй таргет просто не будет строиться. далее флаги можно передать через env, это уже работает


• Принял. Делаю ровно на уровне графа сборки: все production .cpp, кроме main.cpp, в libzutty; zutty = main.cpp + libzutty; fuzz_zutty = main_fuzz.cpp + libzutty, без install/
  default-зависимости. Санитайзеры и fuzzer-no-link оставляю внешним CXXFLAGS/LDFLAGS, раз env уже транзитивно работает. Сначала проверю текущий DSL и минимальный способ поднять
  Vterm без графического запуска.

• Ran git status --short && sed -n '1,280p' build.py && rg -n "class Vterm|Vterm::create|feedPtyOutput|VtermHostCallbacks|class Pty|struct Options|extern.*opts|opts" --glob '*.
  │ {h,cpp}' | head -240
  └ import os

    … +115 lines (ctrl + t to view transcript)
    install(vttest_profile)
    /bin/bash: rg: command not found

• Ran ls -la && grep -RInE "class Vterm|Vterm::create|feedPtyOutput|VtermHostCallbacks|class Pty|struct Options|extern.*opts" --include='*.h' --include='*.cpp' . | head -260 && grep
  │ -RInE "def library|class Library|def program|def install|def glob" build ../std/build.py 2>/dev/null | head -240
  └ total 900
    drwxrwxr-x  7 pg 93618   4096 Jul 20 20:36 .
    … +116 lines (ctrl + t to view transcript)
    build:234:    def program(self, *, srcs, name=None, cflags=(), cxxflags=(), cppflags=(),
    build:278:    def install(self, *targets: Target) -> None:

• Explored
  └ Read build, vterm.h, pty.h, composer.h, vterm_host.h, options.h, test_mode.cpp, main.cpp
    Read build
    Search CXXFLAGS|LDFLAGS|installed|argparse|targets_by|target.name|args.target|ninja|archive|\.a in build
    Read build
    Search Options opts\|void Options::initialize\|HardDefault\|nCols\|saveLines in options.cpp
    Read options.cpp
    Read vterm.cpp
    Read vterm.cpp
    Read vterm.cpp
    Search struct TestPty\|class TestPty in test_mode.cpp
    Read test_mode.cpp
    Search ObjPool::fromMemory in .
    Search VtermImpl::VtermImpl in vterm.cpp
    Read vterm.cpp
    Search opts\. in vterm.cpp
    Read options.cpp
    Read test_mode.cpp
    Read vterm.cpp, vterm_host.cpp, frame.h

■ Conversation interrupted - tell the model what to do differently. Something went wrong? Hit `/feedback` to report the issue.


› мне больше всего понравилась твоя идея про тонкий headless для него


• Да, именно это и есть удачная граница: не вынимать автомат из Vterm, а сделать main_fuzz.cpp тонким headless-фронтендом — маленький ObjPool, fake Pty, callback-host и прямой
  feedPtyOutput(). Тогда фаззим настоящий production-код вместе с Frame, но SDL/Vulkan вообще не запускаются. И этот файл будет единственным fuzz-specific кодом.

• Explored
  └ Search const char\* get\(|std::string get\(|OptionSource|withSource in options.cpp
    Read options.cpp
    Search void VtermImpl::redraw\|host.present in vterm.cpp
    Read vterm.cpp
    Read build
    Read base.h
    Search processInput(const u8\|bool VtermImpl::processInput in vterm.cpp
    Read vterm.cpp
    Search std::string.*uint8\|reinterpret_cast<const char in .
    Read log.h
    Read log.cpp
    Search void VtermImpl::redraw in vterm.cpp
    Search pty\.write\|flushPtyOutput in vterm.cpp

ⓘ This content can't be shown
  We take extra caution with cybersecurity requests. If you’re a security professional, you may be able to apply for Trusted Access.
  Trusted Access: https://openai.com/form/enterprise-trusted-access-for-cyber/
  Learn more: https://help.openai.com/en/articles/20001326


› дальше


ⓘ This content can't be shown
  We take extra caution with cybersecurity requests. If you’re a security professional, you may be able to apply for Trusted Access.
  Trusted Access: https://openai.com/form/enterprise-trusted-access-for-cyber/
  Learn more: https://help.openai.com/en/articles/20001326


› да не надо мне ничего показывать This content can't be shown
    We take extra caution with cybersecurity requests. If you’re a security professional, you may be able to apply for Trusted Access.
    Trusted Access: https://openai.com/form/enterprise-trusted-access-for-cyber/
    Learn more: https://help.openai.com/en/articles/20001326 обойди как-нить. Я не знаю, запиши подробный план в PLAN.md, я в другом окне реализую


• Ок, код не трогаю. Запишу в PLAN.md самодостаточный план под согласованную архитектуру, включая точный граф build.py, устройство тонкого headless harness, env-флаги, corpus и
  критерии готовности — чтобы в другом окне можно было реализовать без восстановления контекста.

• Ran sed -n '1,240p' PLAN.md && git status --short
  └ ## Отложено: графика

    Offscreen Vulkan на этом проходе не нужен: логический raster contract зафиксирован отдельно, пиксельные golden tests остаются графическому этапу.

• Explored
  └ Search handlesPrinter\|host\.print\|printerCommand\|handlesOsc\|host\.osc in vterm.cpp
    Read vterm.cpp
    Read vterm.cpp
    Read vterm.cpp

ⓘ This content can't be shown
  We take extra caution with cybersecurity requests. If you’re a security professional, you may be able to apply for Trusted Access.
  Trusted Access: https://openai.com/form/enterprise-trusted-access-for-cyber/
  Learn more: https://help.openai.com/en/articles/20001326


› пиши PLAN.md


◦ Working (22s • esc to interrupt)


› Use /skills to list available skills

  gpt-5.6-sol xhigh · ~/monorepo/zutty
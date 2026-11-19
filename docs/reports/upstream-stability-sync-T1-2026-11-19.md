# T1. UTF-8 resync и grapheme cluster class

**Статус:** DONE
**Ветка:** `feat/T1-upstream-unicode-fixes`
**База при создании:** `wave/upstream-stability-sync-w1` = `534d1d8cb21bdcc731df5e15b345f29dfac0caf0`
**План:** `docs/plans/2026-11-19-upstream-stability-sync.md`
**Коммиты:** `ac286cf4` (upstream `0842b9ed`), `e04024a6` (upstream `78037448`), `083d9fc3` (regression tests); этот отчёт — отдельным коммитом.
**Push / PR:** не выполнялись по условию задачи.

## Что сделано

Перенесены ровно два названных upstream-изменения. UTF-8 block decoder снова входит в bulk path после восстановления потока, а `GraphemeBreaker` сохраняет уже вычисленный класс правого codepoint на границе кластера. Публичные сигнатуры, границы `lib/vterm` и соседние оптимизации не менялись.

Добавлены два компактных regression/equivalence-теста: corrupted UTF-8 stream с C0 reset boundaries и несколькими повторными входами в multibyte run, а также независимые CJK/Hangul clusters вокруг regional-indicator pair.

## Изменённые файлы

| Файл | Изменение |
|---|---|
| `lib/vterm/utf8.cpp` | чистый cherry-pick `0842b9ed`: повторный вход в `decodeRun` при чистом decoder/trace state |
| `lib/vterm/utf8_ut.cpp` | `Utf8::TextBlockResyncsIntoMultibyteRuns`: codepoints, reset mask, полное потребление и последний decoder state |
| `lib/vterm/grapheme.cpp` | чистый cherry-pick `78037448`: состояние нового кластера заполняется из уже загруженных properties |
| `lib/vterm/grapheme_ut.cpp` | `Grapheme::KeepsIndependentClustersIndependent`: независимые clusters и RI pair |
| `docs/reports/upstream-stability-sync-T1-2026-11-19.md` | этот отчёт |

Новых зависимостей, test hooks, instrumentation и изменений вне разрешённых файлов нет.

## Решение об исходном критерии «красный на parent»

Оба upstream commit message прямо гарантируют **byte-for-byte equivalence**: первый — для decoded stream, второй — для grapheme boundaries. Следовательно, output-тест, обязанный быть красным на родительской реализации и зелёным после точного переноса, противоречит содержимому названных коммитов.

Командир подтвердил ошибку исходного критерия и выбрал equivalence/regression tests без instrumentation или расширения владения, с обязательными адресными mutation checks. Это не замена упавшего теста зелёным: чувствительность каждого нового наблюдателя доказана отдельной мутацией соответствующего состояния/вывода.

## Проверка

| Проверка | Команда / мутация | Код и результат |
|---|---|---|
| Сборка после переноса | `./build unit_tests pty_test_helper` | `0`; собраны `pty_test_helper`, `unit_tests` |
| Полный набор после переноса | `SHITTY_PTY_TEST_HELPER="$PWD/.build/pty_test_helper" .build/unit_tests < /dev/null` | `0`; `OK: 1117`, итоговая строка есть |
| Оба новых теста в полном наборе | фильтр итогового лога по именам | `+ Grapheme::KeepsIndependentClustersIndependent`; `+ Utf8::TextBlockResyncsIntoMultibyteRuns` |
| Equivalence на родительской реализации | временно восстановлены `utf8.cpp`/`grapheme.cpp` из `534d1d8c`, тесты не менялись; `./build unit_tests`, затем `.build/unit_tests Utf8::TextBlockResyncsIntoMultibyteRuns Grapheme::KeepsIndependentClustersIndependent --threads=1 < /dev/null` с обязательным env | build `0`, tests `0`; `OK: 2`, `SKIP: 1115` — ожидаемая эквивалентность, не доказательство исправления output bug |
| UTF-8 mutation check | из повторного bulk entry удалено только `text.resetBefore |= (u64)(reset) << text.count`; собран и запущен `Utf8::TextBlockResyncsIntoMultibyteRuns` | build `0`, test `1`; красный на `utf8_ut.cpp:151`, `ERR: 1` |
| Grapheme mutation check | в boundary seeding `stateClass = right.graphemeClass` заменено на `GraphemeClass::Prepend`; собран и запущен `Grapheme::KeepsIndependentClustersIndependent` | build `0`, test `1`; красный на `grapheme_ut.cpp:54`, `ERR: 1` |
| Восстановление после мутаций | исходники восстановлены через trap; повторены сборка и полный набор | build `0`, tests `0`; `OK: 1117` |
| Diff hygiene | `git diff --check` | `0`, пустой вывод |

Все временные production-мутации и parent-версии восстановлены; в коммиты попали только исправленные production-файлы, тесты и отчёт.

### Штатный benchmark

Сравнение выполнено существующими `core_perf` и `dev/bench_core.py`, без новых файлов в репозитории. Бинарники `before`/`after` собраны из parent/fixed production-реализаций; порядок чередовался раннером.

```bash
python3 dev/bench_core.py \
  --binary before=/tmp/t1-core-perf-parent \
  --binary after=/tmp/t1-core-perf-fixed \
  --work /tmp/t1-core-perf-bench \
  --corpus unicode cjk --size-mib 4 --rounds 3 --save-lines 0
```

Код `0`; медианная пропускная способность, MiB/s:

| Corpus | before | after |
|---|---:|---:|
| `unicode` | 139.10 | 142.77 |
| `cjk` | 326.82 | 327.44 |

Это короткий синтетический smoke-benchmark (4 MiB, 3 rounds), а не статистически значимая гарантия производительности. Он не использовался как основание переноса и не заменяет unit tests.

## Поимённые критерии T1

- [x] **`0842b9ed` виден в diff** — чистый cherry-pick `ac286cf4`, только `lib/vterm/utf8.cpp`, 18 добавленных строк.
- [x] **`78037448` виден в diff** — чистый cherry-pick `e04024a6`, только `lib/vterm/grapheme.cpp`, 8 добавленных и 3 удалённые строки.
- [x] **Есть regression tests на оба пути** — названные `Utf8::TextBlockResyncsIntoMultibyteRuns` и `Grapheme::KeepsIndependentClustersIndependent`; оба зелёные в полном `OK: 1117`.
- [x] **Чувствительность тестов доказана** — отдельные UTF-8 и grapheme mutations дали поимённо красные наблюдатели с кодом `1`; после восстановления полный набор снова `OK: 1117`.
- [x] **Скорректированный командиром критерий parent equivalence выполнен** — на parent оба теста `OK: 2`; невыполнимый исходный запрос «красные на parent» выше описан явно и не выдаётся за выполненный.
- [x] **`./build unit_tests pty_test_helper` успешен** — код `0` в первичном и финальном прогонах.
- [x] **Ручной полный `unit_tests` успешен** — код `0`, `OK: 1117`; `148`/`139` не возникали.
- [x] **Нет performance-only коммитов сверх задания** — перенесены только два явно названных SHA; соседние upstream-оптимизации не включены.
- [x] **Нет изменений вне разрешённых файлов** — production/tests ограничены четырьмя файлами T1, пятый файл — разрешённый отчёт.

## За рамками / незакрытое

Блокирующих незакрытых пунктов в скорректированном T1 нет. Общие `./build st pt --clear`, Python-набор со сверкой T0, `plt_unit_tests`, Linux и итоговая волновая верификация относятся к I1/V1/R1 и здесь не запускались; по ним никакого PASS не заявляется.

Ponytail plugin в сессии недоступен; правила минимализма применены вручную. LSP без project include paths сообщал ложные `std/sys/types.h file not found` на уже существующих include/test macros; фактический проектный компилятор и оба полных прогона успешны, include paths ради этой диагностики не менялись.

## Как откатить

Откатить коммит отчёта, затем `git revert 083d9fc3 e04024a6 ac286cf4`. Миграций, флагов и внешних данных нет.

## Ревьюеру

Сначала проверить, что production diff совпадает именно с двумя upstream-коммитами, затем reset mask в UTF-8 тесте и массив ожидаемых boundaries в grapheme тесте. Главное ограничение — это оптимизации с сохранением поведения, поэтому доказательством служат точный upstream diff и mutation checks, а не выдуманное различие output на parent.

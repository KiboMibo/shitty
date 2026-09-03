# `G10` — негативный тест, который root делает положительным

- **Задача:** `G10` внеплановой починки CI
- **Дата:** 2026-09-03
- **Ветка:** `fix/G10-root-writable` (worktree, не пушилась)
- **Файлы (владеет):** `lib/shitty/quick_frame_store_ut.cpp`
- **Провал, который чинится:** `gh run view 33713974993` — `Tests Alpine`,
  `FAIL $(B)/unit-tests/group-08.stamp`, `!saved failed, at
  lib/shitty/quick_frame_store_ut.cpp:213`
- **Решение одной строкой:** отказ записи вызывается не правами каталога, а
  висячей symlink на месте временного файла — `open()` идёт по ней и получает
  `ENOENT`, а `ENOENT` не обходится привилегией, поэтому проверка остаётся
  достоверной и под root
- **Инвариант на musl:** проверяется, тест выполняется на всех джобах, пропусков
  не добавлено
- **Статус:** **готово**, все пять критериев закрыты

---

## 1. Что падало и почему это увидела Alpine

`FailedWriteLeavesThePreviousFrameIntactAndNoTempFileBehind` вызывал отказ
записи так: сохранял кадр, снимал с каталога право записи (`chmod(dir, 0500)`),
повторял сохранение и требовал `STD_INSIST(!saved)`. Падала именно эта строка —
значит, запись прошла.

Гипотеза командира про root **подтвердилась**, но её вторая половина — «только
Alpine» — **не подтвердилась**. Обе контейнерные джобы работают от root:

| Джоба | Где выполняется | Пользователь |
|---|---|---|
| `Tests Alpine` | `container: alpine:3.24` (`ci.yml:65`) | root |
| `Tests Fedora` | `container: fedora:44` (`ci.yml:116`) | root |
| `Tests UBSan` / `Tests ASan` / `Coverage` | `runs-on: ubuntu-24.04`, без `container:` | `runner` |
| `Tests Darwin 0..4` | `runs-on: macos-15` | `runner` |

Доказательства, что в контейнерах именно root:

- в `ci.yml` ни у одной джобы нет `options: --user`, а строка запуска в логе
  `Initialize containers` его тоже не содержит:
  `docker create --name 267d396866b546b4973abfd00fefaefa_alpine324_7a0a26 …
  -e "HOME=/github/home" …` — образы `alpine:3.24` и `fedora:44` по умолчанию
  стартуют от root;
- шаги ставят пакеты без `sudo`: `apk add --no-cache …` (`ci.yml:69`) и
  `dnf -y … install …` (`ci.yml:127`) — от обычного пользователя они бы не
  прошли;
- воспроизведение (раздел 3) выполнено от `uid=0` и даёт ровно тот же отказ.

Почему тогда `Tests Fedora` не показала того же провала: она **не дошла** до
C++-юнит-тестов. В прогоне 33713974993 она упала раньше, на питоновской группе:

```
FAIL $(B)/python-tests/group-08.stamp: command exited 1: … tst/run_unittest_group.py --group=8 --group-count=20
FAIL: test_a_differently_painted_blank_bounds_the_capture (test_wide_ligature_overflow…)
```

В её логе строка `unit-tests/group` не встречается **ни разу** (`grep -c` → `0`),
в логе Alpine — одиннадцать. То есть дефект теста одинаково смертелен для обеих
контейнерных джоб, и Alpine просто добралась до него первой. После починки
питоновского теста `Tests Fedora` упала бы на том же самом месте.

Продуктовый код при этом ни при чём: под root запись в каталог с режимом `0500`
действительно проходит — это `CAP_DAC_OVERRIDE` ядра, а не поведение
`saveQuickFrame()`. `lib/shitty/quick_frame_store.cpp` не менялся.

---

## 2. Какие варианты рассмотрены

| Вариант | Почему не он |
|---|---|
| Пропуск под root (`geteuid() == 0`) | Инвариант перестал бы проверяться на обеих контейнерных джобах, включая единственную musl-джобу. Запасной вариант по заданию — не понадобился |
| Каталог на месте временного файла (`EISDIR`) | Отказ работает и под root, но занимает то самое имя, по которому тест затем проверяет «временного файла не осталось»: финальную проверку пришлось бы переписывать на «лежит ли там всё ещё наш каталог» |
| Слишком длинное имя (`ENAMETOOLONG`) | Временное имя длиннее целевого, но первичное сохранение идёт через тот же временный файл — не удалось бы даже создать «предыдущий кадр», который тест обязан сохранить нетронутым |
| `ENOSPC` через маленький образ ФС | Требует loop-устройства и прав на монтирование — в юнит-тесте недоступно и само по себе зависит от привилегий |
| **Висячая symlink на месте временного файла (`ENOENT`)** | **Выбран.** Отказ наступает там же, где раньше — на `open()` внутри `saveQuickFrame()`, до `rename()`; `ENOENT` не обходится ни привилегией, ни `CAP_DAC_OVERRIDE`; финальная проверка `access(tmpPath, F_OK) != 0` остаётся дословно той же и сохраняет смысл: висячая ссылка не резолвится, а реально оставленный временный файл — резолвился бы |

Разделение на два теста не понадобилось: «отказ уже после `open()`, временный
файл убран» **уже** проверяет соседний
`WriteFailureAfterOpenRemovesTheTemporaryFile` через `RLIMIT_FSIZE` — механизм,
который действует и на root (проверено, под `uid=0` он зелёный). Так что после
правки набор покрывает обе половины: отказ до создания временного файла и отказ
после него.

Правка (`lib/shitty/quick_frame_store_ut.cpp`, 23 строки добавлено, 7 удалено):
временное имя `<path>.tmp.<pid>` вычисляется до попытки записи, на него кладётся
`symlink()` на `<dir>/absent/frame`, каталога `absent` не существует. Комментарий
над тестом объясняет, почему не `chmod`. Снят ставший ненужным
`#include <sys/stat.h>` — единственным его потребителем был `chmod`.

---

## 3. Критерии

| № | Критерий | Вывод команды | Статус |
|---|---|---|---|
| 1 | Гипотеза про root подтверждена или опровергнута | см. ниже, блок «Критерий 1» | закрыт (подтверждена частично: root — да, «только Alpine» — нет) |
| 2 | Воспроизведение под root: отказ до, отсутствие после | блоки «Критерий 2 (до)» и «Критерий 2 (после)» | закрыт |
| 3 | `./build unit_tests pty_test_helper` зелёная, прогон даёт `OK: 955` | блок «Критерий 3» | закрыт |
| 4 | На macOS тест выполняется и проходит | блок «Критерий 4» | закрыт |
| 5 | Список однородных тестов приложен | раздел 4 | закрыт |

### Критерий 1

```
$ grep -n "container:" .github/workflows/ci.yml
65:    container: alpine:3.24
116:    container: fedora:44
$ grep -c "options:" .github/workflows/ci.yml
0
$ grep -c "unit-tests/group" fedora.log      # лог джобы Tests Fedora
0
$ grep -c "unit-tests/group" alpine.log      # лог джобы Tests Alpine
11
$ docker exec g10-alpine sh -c 'id'
uid=0(root) gid=0(root) groups=0(root),0(root),1(bin),2(daemon),…
```

### Критерий 2 (до правки, alpine:3.24, arm64, uid 0)

```
$ docker exec g10-alpine sh -c 'id -u; cd /src && /tmp/lb/unit_tests --threads=1 QuickFrameStore; echo "EXIT=$?"'
0
…
!saved failed, at /src/lib/shitty/quick_frame_store_ut.cpp:213
- QuickFrameStore::FailedWriteLeavesThePreviousFrameIntactAndNoTempFileBehind
…
OK: 19, ERR: 1, SKIP: 898
EXIT=1
```

Это дословно провал CI: та же строка 213, то же условие `!saved`.

### Критерий 2 (после правки, тот же контейнер, тот же uid)

```
$ docker exec g10-alpine sh -c 'id -u; /tmp/lb/unit_tests --threads=1 QuickFrameStore::FailedWrite > /tmp/after.txt 2>&1; echo "EXIT=$?"; cat /tmp/after.txt'
0
EXIT=0
+ QuickFrameStore::FailedWriteLeavesThePreviousFrameIntactAndNoTempFileBehind
OK: 1, SKIP: 917
```

Группа целиком, как её гоняет CI (`--group=8 --group-count=20`), под root:
тест зелёный (`+ QuickFrameStore::FailedWriteLeaves…`). Сама группа в этом
контейнере обрывается сегфолтом — он воспроизводится и **до** правки, см.
«Обнаружено».

Что тест остался разборчивым под root, проверено подстановкой дефекта в
продуктовый код (в контейнере, не в ветке): `return false` в обработчике
`catch (Exception&)` в `saveQuickFrame()` заменён на `return true`,

```
MUTANT_EXIT=1
!saved failed, at /src/lib/shitty/quick_frame_store_ut.cpp:227
- QuickFrameStore::FailedWriteLeavesThePreviousFrameIntactAndNoTempFileBehind
OK: 0, ERR: 1, SKIP: 917
```

после отката правки — `RESTORED_EXIT=0`, `OK: 20`.

### Критерий 3

```
$ ./build unit_tests pty_test_helper
[CC] {1/2} $(B)/obj/unit_tests/lib/shitty/quick_frame_store_ut.cpp.o
[LD] {2/2} $(B)/unit_tests
BUILD_EXIT=0
$ SHITTY_PTY_TEST_HELPER="$PWD/.build/pty_test_helper" ./.build/unit_tests --threads=1
MAC_EXIT=0
OK: 955
```

### Критерий 4

Из того же прогона:

```
+ QuickFrameStore::FailedWriteLeavesThePreviousFrameIntactAndNoTempFileBehind
```

Тест **выполняется** (строка `+`, не `SKIP`) и проходит. Сюита целиком:

```
$ SHITTY_PTY_TEST_HELPER="$PWD/.build/pty_test_helper" ./.build/unit_tests --threads=1 QuickFrameStore
EXIT=0
…
OK: 20, SKIP: 935
```

---

## 4. Другие тесты, опирающиеся на права файловой системы

Проверены `lib/**`, `ext/**` (`*_ut.cpp`) и `tst/**`, `dev/**` (`*.py`) на
`chmod`, `geteuid`, `access`, `EACCES`, `PermissionError`.

**Однородных с починенным — ни одного.** Полный список найденного:

| Место | Что делает | Ломает ли root |
|---|---|---|
| `lib/shitty/quick_frame_store_ut.cpp:209,212` | тот самый `chmod(0500)` | ломал; **починено этой задачей** |
| `lib/shitty/quick_frame_store_ut.cpp:370` `WriteFailureAfterOpenRemovesTheTemporaryFile` | отказ записи через `RLIMIT_FSIZE`, не через права | нет — лимит действует и на root; проверено под `uid=0`, зелёный |
| `tst/test_options.py:64` | `tool.chmod(0o755)` — делает вспомогательный скрипт исполняемым | нет, это подготовка, а не негативный сценарий |
| `tst/realworld/generate.py:100` | `os.chmod(sample.py, 0o755)` — то же | нет |
| `tst/test_iterm2_terminal_hard_rules.py:101-229` | строки `"chmod -R 777 /"` — данные для правил распознавания опасных команд | нет, файловая система не трогается |
| `tst/test_system_emoji_fallback.py:21` | `skipUnless(os.access(APPLE_COLOR_EMOJI, os.R_OK))` | теоретически: под root `access()` вернул бы успех для нечитаемого файла. Практически нет — на Linux файла нет вовсе, а джобы, где он есть (macOS), от root не работают |

То есть следующих провалов этого класса в наборе не заложено.

---

## Обнаружено

- **`Tests Fedora` тоже работает от root** и после починки своего питоновского
  провала упала бы на этом же тесте. Формулировка «падает только Alpine» верна
  лишь для конкретного прогона: Fedora не дошла до C++-юнит-тестов. Такой же
  порядок «первая упавшая цель прячет остальные» стоит держать в уме при
  следующих триажах — зелёная строка отсутствия провала и недостигнутая цель в
  логе выглядят одинаково.
- **`dev/style.py` локально портит нетронутые файлы.** С `clang-format 23.1.0`
  (homebrew) он переставляет блоки `#include` в файлах, которых правка не
  касалась: на `lib/shitty/quick_companion_ut.cpp` — три строки разницы при
  нулевых изменениях. Прогонять его локально нельзя, иначе в диф попадает шум;
  правка сделана руками, `git status` в ветке содержит один файл. Это тот же
  класс расхождения, что записан в `CLAUDE.md` про локальную сборку.
- **Полный `unit_tests` под alpine:3.24 на arm64 падает сегфолтом**, и группа 8
  тоже — после `ScreenRowSpans::FontChangeResetsStrips`. Воспроизводится
  **одинаково до и после правки** (`BEFORE_GROUP8_EXIT=139`, 37 зелёных строк
  против 38 после — разница ровно в починенном тесте), поэтому к задаче
  отношения не имеет. По отдельности сюиты, идущие следом
  (`FontResolver`, `GlyphCache`, `ReferenceRenderer`, `RendererFrameContract`,
  `Composer`), проходят — то есть дело во взаимодействии, а не в одном тесте. В
  CI (x86, alpine:3.24) группа 8 доходит до конца, так что это свойство
  локального arm64-окружения. Причина не выяснена — отдельная задача, если
  контейнерный прогон на arm64 нужен как рабочий инструмент.
- **Проверка «временного файла не осталось» в этом тесте слабее, чем кажется**:
  и старый `chmod`, и новая symlink валят `open()` до создания временного файла,
  так что удалять там нечего. Настоящую проверку уборки делает соседний
  `WriteFailureAfterOpenRemovesTheTemporaryFile`. Разделять их на два теста
  поэтому и не потребовалось, но имя починенного теста обещает чуть больше, чем
  он проверяет.

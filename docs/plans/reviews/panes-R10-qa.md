# R10-qa — приёмка волны 10: прозрачность окна и размытие подложки

**Статус: в работе.**

Голова приёмки — `0e5089d2` (`master`, снято `git rev-parse master`).
Дерево `/tmp/r10qa`, ветка `review/R10-qa`. Отчёт исполнителя — `docs/reports/T10-translucency-2026-08-21.md`.

Заметка к постановке: названная в задании голова `8ce53bc9` в репозитории **не существует**
(`git cat-file -t 8ce53bc9` → `Not a valid object name`). Приёмка снята с фактической головы `master`.

## Числа — сняты своими руками

Сборка и измерение — разными командами; код возврата взят у сборки.

| прогон | заявлено | снято здесь |
|---|---|---|
| `./build -j 8 st st_test pt unit_tests pty_test_helper` | `exit 0` | **`exit 0`** |
| `SHITTY_PTY_TEST_HELPER=… unit_tests --threads=1` | `OK: 949` | **`OK: 949`**, `exit 0` |
| `MallocScribble=1 MallocPreScribble=1 unit_tests -Pty:: --threads=1` | `OK: 942, SKIP: 7` | **`OK: 942, SKIP: 7`**, `exit 0` |
| `pytest tst/test_config.py tst/test_tabs.py` | `46 passed` | **`46 passed`**, `exit 0` |

Все три числа сошлись.

**Ловушка прибора, стоившая одного ложно-красного прогона.** Без `SHITTY_PTY_TEST_HELPER`
чистый набор даёт `OK: 946, ERR: 3` — три падения `helper != nullptr failed` в
`lib/shitty/pty_ut.cpp:174`. Это не волна: переменную задаёт `build.py:1034`, и запущенный руками
бинарь её не наследует. Класс — окружение, а не код.

Дальше — разбор критериев и находки.

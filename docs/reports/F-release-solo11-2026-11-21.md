# F-release-solo11. SoLo 11 в release toolchain + dlfcn diagnostics

**Ветка:** `fix/release-solo11` · **База:** `fork/master` на `512bce7b712eda564a2514abf4f6d048b90d49f5`
**Коммиты:** `085a9105` (cherry-pick upstream `750edef057715a371cd5de38b3d0f433ba4cee90`), `e179cb72` (cherry-pick upstream `ae2fe8a78c398818babf335144cc4f8758e8812b`), `22b7b64d` (фикс остатков `DL_STUB_DEBUG`).

## Корневые причины

1. Release run 44943479939 (KiboMibo/shitty) упал только на Fedora: `glibc bridge: called unimplemented ABI fegetexcept@GLIBC_2.2.5`. Fedora 44's llvmpipe вызывает `fegetexcept`; старый glibc bridge из SoLo 7 его не реализует, текущий bridge в SoLo 11 — реализует. Upstream commit `750edef0` поднимает IX pin с SoLo 7 на SoLo 11; в upstream run после него Vulkan на Fedora успешно инициализируется и рисует.
2. Upstream commit `ae2fe8a7` меняет `DL_STUB_DEBUG` на `LD_DEBUG=dlfcn` + `LD_DEBUG_OUTPUT`, но оставляет две последние проверки с `"$DL_STUB_DEBUG"`. Upstream log точно содержит `dev/test_release_linux.sh: line 105: DL_STUB_DEBUG: unbound variable` — скрипт под `set -u` (см. шапку `dev/test_release_linux.sh`), переменная больше не экспортируется, финальные grep падают после успешного Wayland smoke test. Переносить `ae2fe8a7` без исправления нельзя.

## Что сделано

- Cherry-pick `750edef0` → `085a9105`: IX pin в `.github/workflows/release.yml` с `487d974011527cf9face187dc2214c271377b508` на `a70273d7892f51a4a7ff2413be1ade97fa0a4c54` (SoLo 7 → SoLo 11).
- Cherry-pick `ae2fe8a7` → `e179cb72`: `dev/test_release_linux.sh` экспортирует `LD_DEBUG=dlfcn` и `LD_DEBUG_OUTPUT="$logs/dlfcn.log"` вместо `DL_STUB_DEBUG`.
- `22b7b64d`: оба финальных grep (строки 105–106) читают `"$LD_DEBUG_OUTPUT"` вместо `"$DL_STUB_DEBUG"`. Поведение больше не менялось, проверки не удалялись. Предупреждения системного glibc про unknown `LD_DEBUG=dlfcn` не тронуты намеренно: SoLo 11 сам понимает этот флаг, warnings приходят от динамических child processes.

macos-15 fix (коммит базы `512bce7b`) не изменялся; рефакторинга нет.

## Изменения

| Файл | Что |
|---|---|
| `.github/workflows/release.yml` | IX pin `ref` обновлён на SoLo 11 (`a70273d7`) |
| `dev/test_release_linux.sh` | финальные grep читают лог dlfcn по `$LD_DEBUG_OUTPUT` |

## Проверка

| Проверка | Команда | Код / результат |
|---|---|---|
| Синтаксис shell | `sh -n dev/test_release_linux.sh` | `0` |
| Использований `$DL_STUB_DEBUG` нет | `grep -n '\$DL_STUB_DEBUG' dev/test_release_linux.sh` | exit `1` (не найдено) |
| `$LD_DEBUG_OUTPUT` используется дважды + export | `grep -n 'LD_DEBUG_OUTPUT' dev/test_release_linux.sh` | строки 36, 105, 106 |
| actionlint release.yml | `actionlint .github/workflows/release.yml` | только 5 pre-existing warnings `[runner-label]` про `ubuntu-26.04` (строки 21, 111, 149, 188, 319); на базе `512bce7b` — ровно те же 5, новых ошибок нет |
| Точный diff фикса | `git show 22b7b64d` | 2 строки: `"$DL_STUB_DEBUG"` → `"$LD_DEBUG_OUTPUT"` в grep |

Полный прогон release-теста возможен только в Release workflow (нужны Fedora runner, Wayland/Vulkan llvmpipe, SoLo 11 toolchain); локально его зелёность не заявлялась.

## За рамками / ограничения

- Комментарий upstream-коммита в `dev/test_release_linux.sh:33-34` исторически упоминает `DL_STUB_DEBUG`; по брифу (`больше поведение не меняй`) не тронут — это текст, а не использование переменной.
- Требуемые паттерны в dlfcn-логе (`try open handle libwayland-client.so.0`, `found handle wayland-client`) — контракт SoLo 11 bridge; подтверждён только upstream run, локально не воспроизводился.

## Как откатить

`git revert 22b7b64d e179cb72 085a9105` (в обратном порядке применения). Миграций и данных нет — только CI-конфиг и тестовый скрипт.

## Ревьюеру

Проверить, что diff ветки против `fork/master` — ровно три коммита и что `22b7b64d` меняет только две строки в двух grep.

## Остаточный риск

- Зелёность на Fedora подтверждается только следующим Release run; если llvmpipe/Fedora 44 поведётся иначе, чем в upstream run, падение останется, но уже не на `fegetexcept` и не на unbound variable.
- Warnings системного glibc про `LD_DEBUG=dlfcn` от child processes остаются в логах — ожидаемо и не фатально.

## Обнаружено

Плагин ponytail в сессии недоступен; его правила минимализма применялись вручную (правка — минимальные две строки, новых абстракций и зависимостей нет).

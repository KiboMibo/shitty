# Статус: upstream stability sync

- **План:** `docs/plans/2026-11-19-upstream-stability-sync.md`
- **Архитектура:** решения Р1–Р5 в плане; инварианты `docs/architecture/2026-08-18-panes-and-window-chrome.md` не меняются
- **Репозиторий:** `KiboMibo/shitty`; upstream `pg83/shitty`
- **База:** `fork/master` `0ac846d5`
- **Рабочая ветка:** `wave/upstream-stability-sync-w1`
- **Начато:** 2026-11-19
- **Обновлено:** 2026-11-21, V1 закрыта, CI зелёный (`44921967183`), отчёт командира обновлён
- **Общий статус:** R1-sec/R1-qa/R1-test влиты, `F1` не нужна; V1 пройдена: `st pt` 255/255, `unit_tests OK: 1128`, `plt OK: 54`, 0 новых красных по T0. Ветка `wave/upstream-stability-sync-w1` запушена в `fork`; CI gate run `44921967183` зелёный, 16/16 jobs на `278b8032`. Найден и устранён refused-fork root-only тест (`278b8032`). Далее по разрешению человека: fast-forward `master`, release workflow, обновление Homebrew tap

## Волны

| Волна | Задачи | Статус | Ветка волны | Смержено в рабочую |
|---|---|---|---|---|
| 1 | T0, T1, T2, T3 | все отчёты DONE, границы проверены | `wave/upstream-stability-sync-w1` | — |
| интеграция | I1 | закрыта: сборки 0, `unit_tests OK: 1125` | `wave/upstream-stability-sync-w1` | да, четыре `--no-ff` merge |
| 1-ревью | R1-sec/R1-qa/R1-test готовы и влиты | R1-test, R1-sec, R1-qa | закрыта | R1-sec `bb2c5fd7`, R1-qa `18ca8cfc`, R1-test `e0f26977` |
| 1-fix | F1 не нужна: 0 блокирующих находок | — | закрыта | — |
| финал | V1 | локальная верификация закрыта; PR/мерж ждут человека | `wave/upstream-stability-sync-w1` | `docs/reports/commander-upstream-stability-sync-2026-11-21.md` |

## Задачи

| ID | Что | Статус | Ветка | Отчёт | Вердикт |
|---|---|---|---|---|---|
| T0 | Эталон на неизменённом fork/master | готово | `qa/T0-upstream-stability-baseline` | `docs/reports/upstream-stability-sync-T0-baseline-2026-11-19.md` | DONE; 16 унаследованных красных ID |
| T1 | Unicode fast-path fixes | готово, ждёт I1 | `feat/T1-upstream-unicode-fixes` | `docs/reports/upstream-stability-sync-T1-2026-11-19.md` | implementation DONE; review впереди |
| T2 | Recoverable new tab | готово, ждёт I1 | `feat/T2-recoverable-new-tab` | `docs/reports/upstream-stability-sync-T2-2026-11-19.md` | implementation DONE; review впереди |
| T3 | Desktop login shell | готово, ждёт I1 | `feat/T3-desktop-login-shell` | `docs/reports/upstream-stability-sync-T3-2026-11-19.md` | implementation DONE; review впереди |
| R1-test | Тесты и чувствительность | готово, влит в волну `e0f26977` | `test/R1-upstream-stability-sync` | `docs/plans/reviews/upstream-stability-sync-R1-test.md` | 0 блок / 0 важн / 1 зам / 1 сущ; PASS_WITH_FINDINGS, `OK: 1128` |
| R1-sec | Security review | готово, влит в волну `bb2c5fd7` | `sec/R1-upstream-stability-sync` | `docs/plans/reviews/upstream-stability-sync-R1-sec.md` | 0 блок / 0 важн / 0 зам / 0 сущ; зелёный |
| R1-qa | Приёмка и Linux | готово, влит в волну `18ca8cfc` | `qa/R1-upstream-stability-sync` | `docs/plans/reviews/upstream-stability-sync-R1-qa.md` | 0 блок / 2 важн / 0 зам / 1 сущ; готово с замечаниями (QA-1 built-in help, QA-2 Linux CI gate) |
| F1 | Исправления блокеров | не нужна: 0 блокирующих находок | — | — | ─ |
| V1 | Общая верификация, PR, итоговый отчёт | DONE: push + CI зелёный; PR/merge ждут решения человека | `wave/upstream-stability-sync-w1` | `docs/reports/commander-upstream-stability-sync-2026-11-21.md` | 0 новых красных, CI `44921967183` зелёный на `278b8032` |

## Решения командира

| Когда | Что решено | На основании |
|---|---|---|
| 2026-11-19 | Скоуп ограничен первым PR: T1–T3; остальные upstream-группы не входят | пользователь принял задачу после рекомендации первого PR |
| 2026-11-19 | Новый план, а не продолжение августовского full-merge плана | прежний план закрывал upstream до `a5f019d5`; новая задача selective port шести коммитов на новой базе |
| 2026-11-19 | Рабочая ветка создана от `fork/master`, локальный stale `master` не используется | локальный `master` отстаёт от `fork/master` на 91 коммит |
| 2026-11-19 | `develop` не создаётся; используется сложившаяся wave/feat модель | существующие ветки и предыдущие планы репозитория |
| 2026-11-19 | Полная тройка review обязательна | одна волна собирает трёх исполнителей и меняет внешне наблюдаемое поведение spawn/login |
| 2026-11-19 | T0–T3 запущены одной параллельной волной, async run `c573ae16-cccf-41da-aad6-86578f33e7f4` | production-файлы задач не пересекаются, каждый исполнитель в изолированном worktree |
| 2026-11-19 | Критерий T1 исправлен: разрешены equivalence tests, зелёные до и после; обязательны mutation checks, instrumentation запрещена | оба upstream commit message гарантируют byte-for-byte unchanged output; исходный критерий «красный на parent» был недостижим и неверен |
| 2026-11-19 | T2 остаётся keyboard + `CsdTabsUi`; форковские sidebar/Wayland/palette routes не расширяют волну | эти файлы вне двух upstream-коммитов; folder popover после spawn вызывает `dropTab`, поэтому нужен отдельный success/failure contract, а не механическая замена на void-listener |
| 2026-11-19 | T1 принят к интеграции: четыре коммита до `993ddd95`, границы файлов соблюдены | отчёт DONE; `unit_tests` `OK: 1117`, оба equivalence-теста чувствительны к адресным мутациям, benchmark приложен как smoke |
| 2026-11-19 | T0 принят: commit `9f18e5c3`; дальнейший Python-режим `./build -k test_suite test_suite_prod_parser` | 16 унаследованных красных ID; общий `./build test` остановлен двумя host-local отказами из пользовательского `sidebarGlassTone`, с пустым HOME production_surface 6/6 зелёный |
| 2026-11-19 | T2 принят к интеграции: HEAD `3d07aa05`; T3 принят: HEAD `eebc156f` | T2 `OK: 1118` и parent mutation controls красные; T3 `OK: 1120`, options/startup Python зелёные, mutation control дал ровно два ожидаемых отказа |
| 2026-11-19 | I1 закрыт на `8b163758`; запущен review run `05a8a349-b162-419d-a243-d1eafaae962b` | четыре merge без конфликтов; `./build st pt --clear` 0, тестовые бинарники 0, полный `unit_tests` `OK: 1125`, `EXIT=0` |
| 2026-11-19 | `arch-mac` недоступен по SSH publickey rc=255; safety net не обходим; Linux-проверка перенесена на обязательный PR CI gate | сервер принимает ключ на query, но отклоняет подпись; это ограничение стенда, не красный тест продукта |

## Отложенные находки

| T2-разведка | Прямой `newSession()` остаётся в sidebar, folder popover, Wayland chrome и command palette; эти routes не получат recoverable behavior в этом PR | вне скоупа | Отдельная задача; для folder popover сначала определить success/failure contract, чтобы не выполнить `dropTab` на старой вкладке после пойманного отказа |

## Возобновление 2026-11-20

Сессия оборвалась после запуска R1-test/R1-sec/R1-qa. Сверка с git показала: R1-sec завершён коммитом `5f07bf45` в своей ветке, но не был влит; R1-test оставил staged три новых теста и логи мутаций в `/private/tmp/r1-*.log`, отчёт не закоммичен; R1-qa оставил готовый отчёт и правки README/конфигов staged, отчёт не закоммичен. R1-sec влит в волну как `bb2c5fd7`. R1-test и R1-qa перезапущены в тех же worktree с явным указанием продолжить, не переделывая готовое.

Linux-стенд `arch-mac` снова доступен: собрал `unit_tests` (GCC 16) и подтвердил refused-fork-тест под uid 1000; в root-контейнерах CI `fork()` не отклоняется `RLIMIT_NPROC`, тест ограничен `euid != 0` в `278b8032`. QA-2 закрыта для ветки зелёным CI `44921967183`; перед merge в `master` обязателен зелёный PR CI.

Дозапуск R1-qa завершился коммитом `5e3ddd02`: 0 блок / 2 важн / 0 зам / 1 сущ, вердикт «готово с замечаниями». R1-qa влит в волну как `18ca8cfc`; R1-test снова перезапущен после серверной ошибки 502 при первом дозапуске — это не дефект кода.

Дозапуск R1-test завершился коммитом `4b84d01d`: 0 блок / 0 важн / 1 зам / 1 сущ, `PASS_WITH_FINDINGS`; полныйyset `OK: 1128` дважды, покрытие 45/56. R1-test влит как `e0f26977`. Проверочная волна закрыта без блокирующих находок; `F1` не создаётся. F1b в истории относится к более ранней работе и к этой волне отношения не имеет.

## Точки остановки и вопросы человеку

| Когда | Вопрос | Ответ |
|---|---|---|
| 2026-11-21 | Push в fork | пользователь разрешил: «мы делаем свой форк»; выполнено |
| 2026-11-21 | Release + Homebrew tap | пользователь разрешил: «пушь и релизь, не забудь обновить тап»; CI зелёный (`44921967183`), можно выполнять |
| при необходимости | Выход за файлы/архитектурные решения плана | остановиться и спросить |

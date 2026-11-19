# Статус: upstream stability sync

- **План:** `docs/plans/2026-11-19-upstream-stability-sync.md`
- **Архитектура:** решения Р1–Р5 в плане; инварианты `docs/architecture/2026-08-18-panes-and-window-chrome.md` не меняются
- **Репозиторий:** `KiboMibo/shitty`; upstream `pg83/shitty`
- **База:** `fork/master` `0ac846d5`
- **Рабочая ветка:** `wave/upstream-stability-sync-w1`
- **Начато:** 2026-11-19
- **Обновлено:** 2026-11-19
- **Общий статус:** I1 зелёный, идут независимые R1-test / R1-sec / R1-qa

## Волны

| Волна | Задачи | Статус | Ветка волны | Смержено в рабочую |
|---|---|---|---|---|
| 1 | T0, T1, T2, T3 | все отчёты DONE, границы проверены | `wave/upstream-stability-sync-w1` | — |
| интеграция | I1 | закрыта: сборки 0, `unit_tests OK: 1125` | `wave/upstream-stability-sync-w1` | да, четыре `--no-ff` merge |
| 1-ревью | R1-test, R1-sec, R1-qa | идёт, три проверяющих запущены | — | — |
| 1-fix | F1 | не начата / возможно пустая | — | — |
| финал | V1 | не начата | — | — |

## Задачи

| ID | Что | Статус | Ветка | Отчёт | Вердикт |
|---|---|---|---|---|---|
| T0 | Эталон на неизменённом fork/master | готово | `qa/T0-upstream-stability-baseline` | `docs/reports/upstream-stability-sync-T0-baseline-2026-11-19.md` | DONE; 16 унаследованных красных ID |
| T1 | Unicode fast-path fixes | готово, ждёт I1 | `feat/T1-upstream-unicode-fixes` | `docs/reports/upstream-stability-sync-T1-2026-11-19.md` | implementation DONE; review впереди |
| T2 | Recoverable new tab | готово, ждёт I1 | `feat/T2-recoverable-new-tab` | `docs/reports/upstream-stability-sync-T2-2026-11-19.md` | implementation DONE; review впереди |
| T3 | Desktop login shell | готово, ждёт I1 | `feat/T3-desktop-login-shell` | `docs/reports/upstream-stability-sync-T3-2026-11-19.md` | implementation DONE; review впереди |
| R1-test | Тесты и чувствительность | идёт | `test/R1-upstream-stability-sync` | `docs/plans/reviews/upstream-stability-sync-R1-test.md` | — |
| R1-sec | Security review | идёт | `sec/R1-upstream-stability-sync` | `docs/plans/reviews/upstream-stability-sync-R1-sec.md` | — |
| R1-qa | Приёмка и Linux | идёт | `qa/R1-upstream-stability-sync` | `docs/plans/reviews/upstream-stability-sync-R1-qa.md` | — |
| F1 | Исправления блокеров | ждёт | `fix/F1-upstream-stability-sync` | `docs/reports/upstream-stability-sync-F1-2026-11-19.md` | — |

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

## Отложенные находки

| T2-разведка | Прямой `newSession()` остаётся в sidebar, folder popover, Wayland chrome и command palette; эти routes не получат recoverable behavior в этом PR | вне скоупа | Отдельная задача; для folder popover сначала определить success/failure contract, чтобы не выполнить `dropTab` на старой вкладке после пойманного отказа |

## Точки остановки и вопросы человеку

| Когда | Вопрос | Ответ |
|---|---|---|
| конец плана | Merge PR в `master` | ожидает решения пользователя |
| при необходимости | Выход за файлы/архитектурные решения плана | остановиться и спросить |

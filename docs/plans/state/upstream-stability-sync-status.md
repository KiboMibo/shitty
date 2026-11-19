# Статус: upstream stability sync

- **План:** `docs/plans/2026-11-19-upstream-stability-sync.md`
- **Архитектура:** решения Р1–Р5 в плане; инварианты `docs/architecture/2026-08-18-panes-and-window-chrome.md` не меняются
- **Репозиторий:** `KiboMibo/shitty`; upstream `pg83/shitty`
- **База:** `fork/master` `0ac846d5`
- **Рабочая ветка:** `wave/upstream-stability-sync-w1`
- **Начато:** 2026-11-19
- **Обновлено:** 2026-11-19
- **Общий статус:** план записан, волна 1 готовится к запуску

## Волны

| Волна | Задачи | Статус | Ветка волны | Смержено в рабочую |
|---|---|---|---|---|
| 1 | T0, T1, T2, T3 | готова к запуску | `wave/upstream-stability-sync-w1` | — |
| интеграция | I1 | не начата | `wave/upstream-stability-sync-w1` | — |
| 1-ревью | R1-test, R1-sec, R1-qa | не начата | — | — |
| 1-fix | F1 | не начата / возможно пустая | — | — |
| финал | V1 | не начата | — | — |

## Задачи

| ID | Что | Статус | Ветка | Отчёт | Вердикт |
|---|---|---|---|---|---|
| T0 | Эталон на неизменённом fork/master | ждёт | `qa/T0-upstream-stability-baseline` | `docs/reports/upstream-stability-sync-T0-baseline-2026-11-19.md` | — |
| T1 | Unicode bug fixes | ждёт | `feat/T1-upstream-unicode-fixes` | `docs/reports/upstream-stability-sync-T1-2026-11-19.md` | — |
| T2 | Recoverable new tab | ждёт | `feat/T2-recoverable-new-tab` | `docs/reports/upstream-stability-sync-T2-2026-11-19.md` | — |
| T3 | Desktop login shell | ждёт | `feat/T3-desktop-login-shell` | `docs/reports/upstream-stability-sync-T3-2026-11-19.md` | — |
| R1-test | Тесты и чувствительность | ждёт | `test/R1-upstream-stability-sync` | `docs/plans/reviews/upstream-stability-sync-R1-test.md` | — |
| R1-sec | Security review | ждёт | — | `docs/plans/reviews/upstream-stability-sync-R1-sec.md` | — |
| R1-qa | Приёмка и Linux | ждёт | — | `docs/plans/reviews/upstream-stability-sync-R1-qa.md` | — |
| F1 | Исправления блокеров | ждёт | `fix/F1-upstream-stability-sync` | `docs/reports/upstream-stability-sync-F1-2026-11-19.md` | — |

## Решения командира

| Когда | Что решено | На основании |
|---|---|---|
| 2026-11-19 | Скоуп ограничен первым PR: T1–T3; остальные upstream-группы не входят | пользователь принял задачу после рекомендации первого PR |
| 2026-11-19 | Новый план, а не продолжение августовского full-merge плана | прежний план закрывал upstream до `a5f019d5`; новая задача selective port шести коммитов на новой базе |
| 2026-11-19 | Рабочая ветка создана от `fork/master`, локальный stale `master` не используется | локальный `master` отстаёт от `fork/master` на 91 коммит |
| 2026-11-19 | `develop` не создаётся; используется сложившаяся wave/feat модель | существующие ветки и предыдущие планы репозитория |
| 2026-11-19 | Полная тройка review обязательна | одна волна собирает трёх исполнителей и меняет внешне наблюдаемое поведение spawn/login |

## Отложенные находки

Пока нет. Все остальные upstream-группы находятся вне скоупа, а не в списке дефектов.

## Точки остановки и вопросы человеку

| Когда | Вопрос | Ответ |
|---|---|---|
| конец плана | Merge PR в `master` | ожидает решения пользователя |
| при необходимости | Выход за файлы/архитектурные решения плана | остановиться и спросить |

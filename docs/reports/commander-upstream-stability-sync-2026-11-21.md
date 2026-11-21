# Отчёт о выполнении: upstream stability sync

- **План:** `docs/plans/2026-11-19-upstream-stability-sync.md`
- **Архитектура:** решения P1–P5 в плане; инварианты `docs/architecture/2026-08-18-panes-and-window-chrome.md` не менялись
- **Борд состояния:** `docs/plans/state/upstream-stability-sync-status.md`
- **Репозиторий:** `KiboMibo/shitty`, upstream `pg83/shitty`
- **Период:** 2026-11-19 — 2026-11-21
- **Итог:** T0, T1–T3, I1 и вся проверочная волна R1 закрыты; `F1` не понадобилась; общая верификация V1 на wave-ветке пройдена. Публикация ветки, PR, Linux CI gate и мерж в `master` требуют решения человека.

## Что получилось

Три перенесённых upstream-изменения работают вместе на macOS: восстановление UTF-8 bulk path после resync, сохранение grapheme class на границе кластера, recoverable new tab и desktop launch как login shell. Инварианты window chrome не тронуты. Локальная сборка и все доступные тесты на итоговой ветке зелёные в пределах унаследованного baseline; регрессий по(names) нет.

## Этапы

| Этап | Кто выполнял | Результат | Ссылка |
|------|--------------|-----------|--------|
| План | `/coding-plan` | 1 волна T0–T3 + интеграция + R1 + финал | `docs/plans/2026-11-19-upstream-stability-sync.md` |
| Архитектура | Commander по плану | P1–P5, без изменения существующих инвариантов | раздел 4 плана |
| Эталон | `/coding-qa` | T0: база `0ac846d5`, 16 унаследованных красных | `docs/reports/upstream-stability-sync-T0-baseline-2026-11-19.md` |
| Реализация | `/coding-writer` ×3 | T1, T2, T3 — DONE поимённо | `docs/reports/upstream-stability-sync-T{1,2,3}-2026-11-19.md` |
| Интеграция | Commander | I1: 4 `--no-ff` merge, `st pt` 255/255, `unit_tests OK: 1125` | `docs/plans/state/upstream-stability-sync-status.md` |
| Проверка R1 | `/coding-secure`, `/coding-qa`, `/coding-test` | 0 блокирующих; 3 отчёта влиты в волну | `docs/plans/reviews/upstream-stability-sync-R1-*.md` |
| Исправления | F1 | не создавалась: 0 блокирующих находок | — |
| Финал | Commander | V1: свежие прогоны на `2776751e`, итоговый отчёт | этот отчёт |

Вердикты проверочной волны:

| Проверяющий | Находок | Вердикт | Смержено |
|-------------|---------|---------|----------|
| R1-sec | 0 блок / 0 важн / 0 зам / 0 сущ | зелёный | `bb2c5fd7` |
| R1-qa | 0 блок / 2 важн / 0 зам / 1 сущ | готово с замечаниями | `18ca8cfc` |
| R1-test | 0 блок / 0 важн / 1 зам / 1 сущ | `PASS_WITH_FINDINGS` | `e0f26977` |

## Общая верификация V1

Прогонялось на чистой wave-ветке `2776751e` (`git status --short` пуст), не по отчётам агентов.

| Команда | Результат |
|---|---|
| `./build st pt --clear` | rc `0`, 255/255 узлов |
| `./build unit_tests pty_test_helper` + `SHITTY_PTY_TEST_HELPER=… .build/unit_tests < /dev/null` | rc `0`, `OK: 1128` |
| `./build plt_unit_tests` + `.build/ext/plt/plt_unit_tests < /dev/null` | rc `0`, `OK: 54` |
| `tst/production_surface.py` с чистым `HOME`, `SHITTY_TEST_VERSION=2026.11.21` | rc `0`, `Ran 6 tests`, `OK` |
| `./build test` | rc `1`: ровно два host-local id `test_version_probe_exits_before_window_startup`, известные по T0 §6 |
| `./build -k test_suite test_suite_prod_parser` | rc `1`: 13 унаследованных красных, **0 новых**; 3 красных T0 стали зелёными |

Поимённое сравнение с T0: из 16 унаследованных красных 13 совпадают по имени и числу вхождений. Три (`test_build_rejects_every_other_descr_shape`, `test_child_signal_exit_uses_shell_status_convention`, `test_pretty_child_gets_pretty_version`) в T0 падали из-за sandbox/`setpgid` и отсутствия test-child tty; в V1 зелёные. Это улучшение, не регрессия.

R1-qa добавил 3 наблюдателя T1–T3; R1-test — ещё 3; полный набор вырос с `OK: 1125` до `OK: 1128`. Linux/libstdc++, Vulkan и реальный Finder/Dock launch локально не проверялись: `arch-mac` отклоняет SSH с rc `255`.

## Ключевые решения по ходу

| Решение | Почему |
|---|---|
| Исходный критерий T1 «красный на parent» заменён на equivalence + mutation sensitivity | upstream коммиты гарантируют byte-for-byte equivalence; output-тест физически не может быть красным на родителе |
| `F1` не создавалась | все три проверяющих дали 0 блокирующих; жёлтые находки вынесены в отложенные |
| QA-1 оставлен в production-коде | правка `lib/shitty/options.cpp` выходит за владение R1-qa; внешние описания исправлены, встроенный help — отдельная задача владельца T3 |
| Linux CI объявлен обязательным gate | недоступность `arch-mac` не позволяет локально подтвердить libstdc++ |

## Отложено и не сделано

| Что | Уровень | Почему | Где искать |
|-----|---------|--------|------------|
| Встроенный `-help` печатает `login default: false` для desktop | важная QA-1 | правка production `options.cpp` вне владения проверяющих; нужна отдельная задача | `docs/plans/reviews/upstream-stability-sync-R1-qa.md` |
| Зелёный Linux/libstdc++ PR CI | важная QA-2 | локальный стенд недоступен (`arch-mac` rc 255); обязателен перед merge в `master` | тот же отчёт, раздел 7 |
| `CsdTabsUi::tabOpened()` недостижим из unit-тестов | замечание R1-test | экспорт production-интерфейса или GUI integration test — вне владения R1-test | `docs/plans/reviews/upstream-stability-sync-R1-test.md` |
| Вложенные build-тесты нестабильны под sandbox | существующая QA-E1 | унаследовано до волны; воспроизводится как env-артефакт, не product regression | `docs/plans/reviews/upstream-stability-sync-R1-qa.md` |
| Linux/vulkan/Finder/Dock | ограничение V1 | нет стенда и GUI-окружения | этот отчёт |

## Что требует решения человека

1. Опубликовать `wave/upstream-stability-sync-w1` в `fork` и открыть PR в `KiboMibo/shitty`.
2. Дождаться зелёного Linux/libstdc++ PR CI как компенсирующего gate QA-2.
3. Мерж PR в `master` — отдельное разрешение; в тачpoints скилла Commander это точка остановки.
4. По желанию — отдельная задача на QA-1 (текст built-in help) до или после мержа; блокирующей её нет.

## Шум проверок

| Проверяющий | Находок | Исправлено | Неверно | Намеренно | Неважно | Существовавшие |
|-------------|---------|------------|---------|-----------|---------|----------------|
| R1-sec | 0 | 0 | 0 | 0 | 0 | 0 |
| R1-qa | 3 | 0 | 0 | 2 | 0 | 1 |
| R1-test | 2 | 0 | 0 | 1 | 0 | 1 |

Кругов `F<N>`: 0. Отдельной перепроверки блокирующих не потребовалось.

## Оговорки

- Локальная зелёность macOS/arm64/libc++ не заменяет Linux/libstdc++ CI.
- В `./build test` два известных host-local отказа сохраняются из-за пользовательского `~/.config/shitty/shitty.toml`; в T0 они же зафиксированы как не-product regression.
- Отчёты R1 относятся к коммиту `8b163758`; после него в ветку вошли только тесты и документация R1-test/R1-qa, production-код не менялся. Свежие прогоны V1 выполнены на `2776751e`.

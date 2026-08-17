# T6. `dev/make_app.sh` перенесён в актуальное дерево

**Ветка:** `feat/window-chrome-upstream` · задача от team-lead, без отдельного файла плана

## Что сделано

`git checkout feat/macos-app-bundle -- dev/make_app.sh` — перенёс скрипт
без переписывания заново, дальше только точечная адаптация под новую
структуру дерева (`bin/st/`, `bin/pt/`, `lib/shitty/` вместо плоского
корня).

### Пути к SVG

`build.py:527` и `:549` (актуальный, проверил именно чтением, не по
памяти) находят иконки жёстко прописанными путями
`$(S)/bin/st/shitty.svg` и `$(S)/bin/pt/pretty.svg` — не через переменную
или соглашение, апстрим сам их не завёл. Продублировал ту же логику
буквально:

```diff
-make_bundle Shitty st shitty "$ROOT_DIR/shitty.svg"
-make_bundle Pretty pt pretty "$ROOT_DIR/pretty.svg"
+make_bundle Shitty st shitty "$ROOT_DIR/bin/st/shitty.svg"
+make_bundle Pretty pt pretty "$ROOT_DIR/bin/pt/pretty.svg"
```

Плюс два комментария, ссылавшихся на старое расположение («repo-root
SVG») и на устаревшую строку `build.py:494` (актуальная — `:501`,
`icon_png()` съехала вниз вместе с остальным файлом) — поправил оба, чтобы
не оставлять неверных подсказок следующему читателю.

Строка `# ./build's default output is .build (see build:2475)` — **не
трогал**: она ссылается на `build` (обёртку-исполняемый файл), не на
`build.py`, и там `.build`-дефолт действительно всё ещё на строке 2475.
Проверил отдельно, чтобы не оставить и эту ссылку сломанной по инерции.

### README.md

Нашёл исходную правку двумя коммитами (`fb200a18`, `f6c4c041`) в ветке
`feat/macos-app-bundle`, сравнил окружающий текст с текущим —
секция `### Homebrew (macOS, Apple silicon)` дошла до текущего дерева
дословно, абзац про GitHub release перед `### Linux` тоже не менялся.
Вставил тот же абзац в то же место без модификаций содержимого — переносить
было прямо, «24 коммита правок» этого конкретного места не задели.

## Изменения

| Файл | Что |
|------|-----|
| `dev/make_app.sh` | путь к SVG (`bin/st/`, `bin/pt/`), два комментария |
| `README.md` | абзац про `dev/make_app.sh` в секции установки на macOS |

## Проверка

| Что | Как | Результат |
|-----|-----|-----------|
| Синтаксис | `sh -n dev/make_app.sh` | OK |
| Сборка бандлов | `./dev/make_app.sh .build <out>` | `Shitty.app`/`Pretty.app` собраны, exit 0 |
| `plutil -lint` | на обоих `Info.plist` | OK / OK |
| `CFBundleIdentifier` | `PlistBuddy -c "Print :CFBundleIdentifier"` | `org.pg83.shitty` / `org.pg83.pretty` |
| **`.icns` генерируется** (смысл задачи) | `file Contents/Resources/*.icns` | `Mac OS X icon, ... "ic12" type` — оба валидные |
| `CFBundleIconFile` в plist | `PlistBuddy -c "Print :CFBundleIconFile"` | `shitty.icns` / `pretty.icns` — не пусто, как было до переезда |
| Ad-hoc подпись | `codesign -dv` | `Signature=adhoc`, `Identifier=org.pg83.*` |
| Деградация без `rsvg-convert` | тот же запуск с `PATH`, из которого убран `rsvg-convert` | `warning: rsvg-convert not found, packaging *.icns without an icon` на stderr, exit 0, `.icns`/`CFBundleIconFile` в бандле отсутствуют, всё остальное на месте |
| Бинарь в бандле — актуальный | `Shitty.app/Contents/MacOS/st -version` | `Shitty 2026.08.17` (дата сегодняшней сборки) |
| Бинарь в бандле знает новые фичи | `Shitty.app/Contents/MacOS/st -help \| grep -E 'transparentTitlebar\|quick'` | все три опции на месте |
| `open Shitty.app` поднимает окно | `System Events`, `count windows of process "st"` | 1 |
| Меню — «Shitty» | `System Events`, `name of menu bar item 2` (item 1 — системное меню ) | `Shitty` |
| То же для Pretty | окно + меню | 1 окно, меню «Pretty» |

## За рамками

- Нотариализация (Developer ID) — как и в исходной задаче, вне скоупа
  скрипта; ad-hoc подписи достаточно для локального запуска.
- Скриншотов не делал — среда без TCC Screen Recording, как и раньше;
  визуальный вид иконки в Dock/Finder не проверен, требует человека.

## Ревьюеру

Диф в `dev/make_app.sh` — четыре строки (два пути, два комментария),
остальное — байт в байт то, что было в `feat/macos-app-bundle`. README —
один цельный абзац, вставленный в неизменившееся место.

## Как собрать бандл (для пользователя)

```sh
./build -j 8 st pt
./dev/make_app.sh
```

Результат — `.build-app/Shitty.app` и `.build-app/Pretty.app`. Если `pt`
на `PATH` уже занят другой формулой (`tcl-tk`), сначала
`brew link --overwrite pretty`.

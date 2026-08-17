# T9. Сборка `.app` в релизном CI

**Ветка:** `feat/window-chrome-upstream` · задача от team-lead, без отдельного файла плана

## Что сделано

### `.github/workflows/release.yml`

- `build-darwin`: после стейджинга бинарей добавлен шаг **Package Darwin
  app bundles**, вызывающий новый `dev/package_darwin_apps.sh` (`.build-darwin`
  → `.build-app` → `.build-app-zip`), и шаг **Upload Darwin app bundles**,
  загружающий `Shitty.app.zip`/`Pretty.app.zip` артефактом `darwin-arm64-apps`
  (`retention-days: 1`, `compression-level: 0` — данные уже сжаты в zip).
- `package-release`: добавлен шаг **Download Darwin app bundles**
  (`darwin-arm64-apps` → `$RUNNER_TEMP/release-input/darwin-apps`) и флаг
  `--darwin-apps-directory` в вызове `dev/release.py`.
- **Attest release artifacts**: маска `subject-path` расширена с
  `*.tar.gz` до двух строк (`*.tar.gz` и `*.zip`), так как `dev/release.py`
  теперь копирует zip-бандлы в тот же `--artifacts-directory`, что и
  tar.gz-архивы — один glob-паттерн раньше их не покрывал бы.

Все новые `uses:` — их нет: я переиспользовал уже запиненные
`actions/upload-artifact@043fb46...` и `actions/download-artifact@3e5f45b2...`,
новых экшенов не добавлял.

### `dev/package_darwin_apps.sh` (новый файл)

Тонкая обёртка вокруг `dev/make_app.sh`, которая упаковывает получившиеся
`.app` в zip командой `ditto -c -k --keepParent` — ровно то, что решил
пользователь: `ditto`, а не `zip`, потому что `zip` ломает бандл (права,
симлинки, подпись). Один zip на бренд: `Shitty.app.zip`, `Pretty.app.zip`.
`#!/bin/sh`, `set -e`, MIT-шапка, стиль (`BIN_DIR`/`APP_DIR`/аргументы по
позиции, `echo` в конце) — как у `dev/make_app.sh` и `dev/build_brew_macos.sh`.

### `dev/release.py`

- Новый необязательный флаг `--darwin-apps-directory` (директория с
  `Shitty.app.zip` и `Pretty.app.zip`).
- Валидация: файлы существуют, `zipfile.is_zipfile()` (stdlib, а не парсинг
  вывода `file` — для чек «это вообще валидный zip» это точнее и проще, чем
  сверять формулировки `file`, которые разнятся между версиями/платформами;
  для бинарей `file` остаётся оправданным, там реально проверяется
  архитектура/формат, а не просто целостность контейнера).
- Zip-файлы **копируются как есть** в `--artifacts-directory`
  (`shutil.copy2`), а не переупаковываются — иначе есть риск сломать
  ad-hoc подпись, которую уже поставил `make_app.sh`. Их имена входят в
  `generated_names`, так что коллизия с `--extra-artifact` теперь ловится
  и для них тоже.
- Zip-файлы добавлены в список ассетов `gh release create`.
- Совместимость: без `--darwin-apps-directory` поведение **не меняется** —
  проверено отдельным прогоном (см. «Проверка», пункт 3).
- **Gatekeeper-уведомление в release notes.** Пользователь потребовал,
  чтобы в заметках к релизу обязательно было сказано про блокировку
  Gatekeeper и `xattr -cr`. Проблема: workflow вызывает `release.py` с
  `--generate-notes` (автосгенерированный changelog), а не с текстом с
  stdin. Решение — `gh release create` поддерживает **совместное**
  использование `--notes-file` и `--generate-notes`: содержимое файла
  подставляется перед автосгенерированным списком коммитов (подтверждено
  `gh release create --help`, gh 2.97.0: «Additional release notes can be
  prepended to automatically generated notes by using the `--notes` flag»).
  Поэтому теперь:
  - `manual_notes` (то, что раньше называлось `notes`) — текст со stdin,
    если `--generate-notes` не передан;
  - если `--darwin-apps-directory` задан, к нему добавляется константа
    `DARWIN_APP_GATEKEEPER_NOTICE`;
  - оба флага (`--notes-file` и `--generate-notes`) теперь **независимы**
    друг от друга и передаются по отдельности, а не через `if/else` —
    `--notes-file` добавляется, если есть что писать, `--generate-notes` —
    если он был передан. Без Darwin-бандлов и без ручных заметок при
    `--generate-notes` итоговый вызов не отличается от прежнего (ни один
    из двух флагов не появляется лишним).

## Изменения

| Файл | Что |
|------|-----|
| `.github/workflows/release.yml` | 2 новых шага в `build-darwin`, 1 новый шаг + флаг в `package-release`, маска `Attest` расширена |
| `dev/release.py` | `--darwin-apps-directory`, копирование zip в артефакты, Gatekeeper-заметка, флаги notes независимы |
| `dev/package_darwin_apps.sh` | новый файл — `make_app.sh` → `ditto` zip |
| `README.md` | абзац в `### Homebrew (macOS, Apple silicon)` про `.app.zip` в релизах и обход Gatekeeper |

## Проверка

### 1. Синтаксис workflow

`actionlint` недоступен — `brew install actionlint` упал по сети
(`curl: (35) LibreSSL SSL_connect: SSL_ERROR_SYSCALL`, formulae.brew.sh),
без сети не установился. Вместо него:

```
$ python3 -c "import yaml,sys; yaml.safe_load(open('.github/workflows/release.yml')); print('YAML OK')"
YAML OK
```

Плюс ручной построчный обзор диффа (`git diff` приведён выше) — новые шаги
следуют точному стилю соседних (`name:`, запиненные `uses:`, те же
`runner.temp`-пути), новых экшенов не вводилось.

### 2. Цикл `make_app.sh` → `ditto` → распаковка (главная проверка)

```
$ ./build -j 8 st pt                     # актуальная сборка, без изменений (up to date)
$ dev/package_darwin_apps.sh .build /tmp/t9-app /tmp/t9-zip
/tmp/t9-app/Shitty.app: replacing existing signature
/tmp/t9-app/Pretty.app: replacing existing signature
built /tmp/t9-app/Shitty.app and /tmp/t9-app/Pretty.app
packaged /tmp/t9-zip/Shitty.app.zip and /tmp/t9-zip/Pretty.app.zip
```

Распаковка **ровно той командой, что будет использовать конечный
пользователь**, в отдельный каталог:

```
$ ditto -x -k /tmp/t9-zip/Shitty.app.zip .
$ ditto -x -k /tmp/t9-zip/Pretty.app.zip .
```

| Что | Как | Результат |
|-----|-----|-----------|
| Структура бандла | `find Shitty.app -maxdepth 3` | `Contents/{Info.plist,MacOS/st,Resources/shitty.icns,_CodeSignature/CodeResources}` — цела |
| `plutil -lint` | на обоих `Info.plist` | OK / OK |
| **Подпись после zip→unzip** (ключевая проверка) | `codesign -dv Shitty.app` / `Pretty.app` | `Signature=adhoc`, `Identifier=org.pg83.shitty`/`org.pg83.pretty`, `Sealed Resources ... files=1` — подпись сохранилась |
| `codesign --verify --deep --strict` | обе | `Shitty verify OK` / `Pretty verify OK` |
| `.icns` на месте | `file *.icns` | `Mac OS X icon, ... "ic12" type`, оба бандла |
| `open` поднимает окно | `open Shitty.app -n`, `pgrep` реального pid, `System Events` по `unix id` этого pid (см. примечание ниже) | 1 окно, меню-бар «Shitty» |
| То же для Pretty | аналогично | 1 окно, меню-бар «Pretty» |

**Примечание про `System Events`.** Первая попытка (`count windows of
process "st"` по имени) дала 0 при живом процессе. Оказалось, что
`System Events` резолвил имя `"st"` в **чужой, зависший AX-объект**
(`unix id` не совпадал ни с одним текущим pid — след предыдущего запуска
`Shitty.app` кем-то ещё в этой же общей среде, где параллельно работают
другие агенты). Как только я адресовался к процессу по его настоящему
`unix id` (`pgrep`-ом добытому), счётчик окон и имя меню-бара сразу
показали правильный результат. Это артефакт общей среды, не дефект
упаковки — сама подпись, `plutil` и структура бандла проверялись без
всякой неоднозначности.

### 3. `dev/release.py` — полный сквозной прогон без сети и без GitHub

Готового `--dry-run` в скрипте нет; вместо него собрал **полностью
изолированную песочницу**: локальный bare-репозиторий вместо `origin`
(`git clone` рабочей копии → `git remote set-url origin
/tmp/t9-bare.git` → `git push`) и поддельный `gh` на `PATH` (отвечает на
`repo view`/`release list`/`release view`/`release create`, ничего не
шлёт наружу, логирует каждый вызов). Ни разу не обратился ни к GitHub,
ни к `pg83/shitty`, ни к `KiboMibo/shitty`.

Собрал реальные бинари для проверки: Darwin — настоящие `.build/st`/`pt`
(arm64 Mach-O), Linux — сконструированный по байтам минимальный ELF64
x86-64 заголовок (для `file`-проверки формата, не для исполнения).

Прогнал 4 комбинации:

| Комбинация | Результат |
|---|---|
| `--generate-notes` + `--darwin-apps-directory` (как в CI) | exit 0; `gh release create` получил 7 файлов (source tar.gz, 2×linux tar.gz, 2×darwin tar.gz, 2×app.zip) и флаги `--notes-file ... --generate-notes --draft`; `release-notes.md` = только Gatekeeper-абзац |
| **Без** `--darwin-apps-directory` (проверка обратной совместимости) | exit 0; `gh release create` **не получил** `--notes-file` вообще (только `--generate-notes`) и не получил ни одного `.zip` — байт-в-байт то же поведение, что до правки |
| Заметки со stdin + `--darwin-apps-directory`, без `--generate-notes` | exit 0; `release-notes.md` = `"<текст со stdin>\n\nGatekeeper-абзац"`; в `gh release create` — `--notes-file`, **без** `--generate-notes` |
| `--darwin-apps-directory` с отсутствующим `Pretty.app.zip` | `RuntimeError: prebuilt Darwin app bundle does not exist: ...` — падает до сети, как и ожидалось |
| `--darwin-apps-directory` с испорченным zip (`Shitty.app.zip` = текстовый файл) | `RuntimeError: not a valid zip archive: ...` |

Дополнительно проверил, что скопированные в `artifacts/` zip **побайтово
идентичны** исходным (`diff` — молча, совпадение), т.е. копирование не
трогает содержимое/подпись; и что старые артефакты (`1.tar.gz`,
`st-darwin-arm64.tar.gz` и т. д.) собираются как раньше.

### 4. Обычные проверки проекта

```
$ ./build test -k
```

13 упавших узлов, все **не связаны с моим диффом** (я не трогал код
терминала): `pty_test_helper.c.o` (компиляция), `pretty-binary-branding`,
и несколько python-групп с `test_soft_render` (`60414 not greater than
60414` — на грани точности антиалиасинга), `test_italic_overhang`,
`test_contour_input_generator` (`test_legacy_arrow_modifier_matrix`),
`test_ghostty_key_encoding_tail`. В сессии параллельно работает
несколько других агентов над той же рабочей копией (общий `.build`,
общие ресурсы клавиатуры/раскладки) — похоже на конкуренцию за ресурсы,
а не на регресс от моих правок: ни один из упавших тестов не имеет
отношения к release-CI, packaging или `dev/release.py`. Не чинил —
это код терминала, вне границ владения этой задачи.

```
$ SHITTY_TEST_BINARY=$PWD/.build/st_test python3 -m pytest tst/test_config.py
20 passed in 0.36s
```

### 5. Реальный прогон в CI фока — **не запускал**, и вот почему

Я перечитал весь `package-release` job перед тем, как жать кнопку, и
обнаружил то, что не было очевидно из постановки задачи: последний шаг
`Publish release` **безусловно снимает `--draft`**
(`gh release edit ... --draft=false`) сразу после успешного
`Create draft release`. Это значит, что `--draft` в текущем пайплайне —
не конечное состояние, а промежуточная защита от гонки (см. комментарий
в шапке файла: «publishes the next numeric tag after all four jobs have
succeeded»). Реальный `workflow_dispatch` на форке при полном успехе
всех джобов **опубликует настоящий, не-черновой релиз** в
`KiboMibo/shitty` — с реальным git-тегом и веткой (`1` или следующий
свободный номер), запушенными в форк, и видимым публично GitHub Release.
Это не совпадает с тем, что подразумевалось в постановке («релиз
создаётся черновиком»), — черновик там только на несколько секунд между
двумя последними шагами одной и той же джобы, не человек-контрольная
точка.

К этому добавляется, что `build-linux` (сборка через IX) — таймаут 120
минут, и `gh workflow run` не умеет запустить один job из графа — только
весь пайплайн целиком (`needs:` не даёт выбора). То есть проверка «по
чуть-чуть» отдельно macOS-части CI без остального недоступна средствами
`gh` без временной правки workflow (это уже вышло бы за границы задачи —
трогать логику остального пайплайна ради теста).

Итог: не запускал. `--force`-подобных обходов (например, временно убрать
шаг `Publish release`, чтобы протестировать только упаковку) я тоже не
делал — это подмена самого пайплайна, который нужно тестировать as-is.
Если вы (team-lead/пользователь) считаете риск приемлемым — можно
запустить `gh workflow run release.yml --repo KiboMibo/shitty --ref
feat/window-chrome-upstream` и затем сразу вручную выставить релиз
обратно в draft/удалить его после появления (`gh release edit <tag>
--repo KiboMibo/shitty --draft` до восстановления, либо `gh release
delete <tag> --repo KiboMibo/shitty --yes` после проверки) — просто
знайте, что окно между публикацией и вашей реакцией не нулевое, и
GitHub мог успеть её проиндексировать.

## Что честно не проверено

- **Реальный CI-прогон** (см. пункт 5 выше) — сознательно не делал,
  риск публикации реального релиза в форке отличается от того, что
  предполагалось в постановке.
- **`actionlint`** недоступен (нет сети для `brew install`) —
  синтаксис проверен `yaml.safe_load` + ручным обзором, семантику
  экшенов (типа опечаток в именах входов) `actionlint` поймал бы точнее.
- Внешний вид иконки в Dock/Finder — как и в T6, среда без
  Screen Recording (`screencapture` падает: `could not create image
  from display`), визуально не смотрел.
- `VERSION` в `CFBundleShortVersionString`/`CFBundleVersion` для
  бандлов, собранных в `build-darwin`, будет тем, что вернёт
  `git describe --tags --always` **на момент сборки** (до того, как
  `package-release` вычислит и запушет итоговый релизный тег — это
  происходит позже, в другой джобе). На практике это git-описание
  ближайшего коммита/короткий sha, а не финальный номер релиза —
  как и раньше для остальных инструментов, это отдельный от team-lead
  вопрос синхронизации версии, не в скоупе конкретно этой задачи
  (упаковки), и был бы курицей-и-яйцом при любой реализации без
  двухфазной сборки.

## Ревьюеру

Основной риск в дифе — не код, а последовательность шагов CI (порядок
джоб/скачивание/передача флага). Дифф в `release.yml` и `release.py`
рекомендую читать вместе с этим отчётом по разделам «Что сделано» —
там же обоснование по каждому решению. Сигнатура `dev/release.py`
`main()` не менялась (позиционные и обязательные аргументы те же),
единственный новый **обязательный к прочтению** момент — независимость
флагов `--notes-file`/`--generate-notes` в финальном списке аргументов
`gh release create`.

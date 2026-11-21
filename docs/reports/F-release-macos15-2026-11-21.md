# F-release-macos15: release workflow на поддерживаемом macOS runner

Дата: 2026-11-21
Ветка: `fix/release-macos15`
База: `fork/master` @ 33f1eb10

## Что сделано и зачем

Release run 44943032058 не смог запустить Darwin jobs: GitHub отклонил
label `macos-14` (`The configuration 'macos-14-arm64-us-default' is not
supported`). Официально `macos-14` retired 2026-11-02; разрешённый
replacement `macos-15` уже используется в `.github/workflows/ci.yml`.
Run был отменён, тег/релиз не создан.

Заменены ровно два `runs-on: macos-14` в `.github/workflows/release.yml`:

- `build-darwin` (строка 224) → `runs-on: macos-15`
- `test-darwin` (строка 266) → `runs-on: macos-15`

Другого поведения не менялось, ничего не рефакторилось.

## Проверено

- `git diff` — ровно 2 изменённые строки, обе `runs-on: macos-14` → `macos-15`.
- `grep -rn 'macos-14' .github/` после правки — совпадений `runs-on` нет.
  Единственное оставшееся упоминание: комментарий в
  `.github/workflows/darwin-preview.yml:19` (поясняет, почему preview
  использует другой runner). Не трогал — вне владения задачи.
- YAML: `python3 -c "import yaml; yaml.safe_load(...)"` — файл парсится,
  `build-darwin` и `test-darwin` имеют `runs-on: macos-15`.
- `actionlint .github/workflows/release.yml` — на изменённых строках
  замечаний нет (`macos-15` — известный label). Единственные находки —
  pre-existing `runner-label` для `ubuntu-26.04` (строки 21, 111, 149,
  188, 319), существовавшие до правки и вне её области; actionlint
  считает их неизвестными из-за устаревшей встроенной базы labels.

## Отвергнутые альтернативы

- `macos-15-intel` / `macos-15-xlarge` — в ci.yml и ранее в release.yml
  используется дефолтный `macos-15` (arm64); сохранён тот же вариант.

## Риски и внимание для ревьюера

- Полноценной проверкой будет следующий release run: локально нельзя
  подтвердить, что GitHub примет label и Darwin jobs пройдут. Зелёного
  релиза не заявляю.
- Если build-darwin собирает arm64-артефакты через кросс-компиляцию на
  intel-runner, поведение при смене на arm64 runner стоит глянуть в
  следующем run (в ci.yml `macos-15` уже работает, оснований для риска мало).

## Откат

`git revert <SHA коммита>` — обратная миграция не нужна.

## SHA

См. `git log -1 fix/release-macos15` (коммит `ci: use supported macOS
runner for releases`).

# G7: `pt` config fell behind `-tabBar`

## Что разошлось и когда

Commit `6ca8fcf2` ("V3: the tab bar's placement is a named option, not a
boolean feature switch") replaced the boolean CLI option `-sidebarTabs` with
the named option `-tabBar top|sidebar`, and updated `bin/st/shitty.toml`
accordingly (`tabBar = "top"`). It did not touch `bin/pt/pretty.toml`, which
kept the stale `# CLI: -sidebarTabs...` comment and `sidebarTabs = false`.
`sidebarTabs` no longer exists as an option (`lib/shitty/options.cpp` computes
it as `tabBar == "sidebar"`; the options table only knows `tabBar`), so `pt`
rejected its own shipped config with `unknown option: sidebarTabs`, failing
`tst/production_surface.py::test_pretty_surface_has_only_pretty_branding` on
`Tests Fedora`.

A later commit, `f805e538` ("R9-qa: the shipped sample config had gone stale,
and its guard was red"), explicitly fixed this same `sidebarTabs`→`tabBar`
drift *and* added missing `paneDividerWidth`/`paneDividerColor` documentation
— but only in `bin/st/shitty.toml`. `bin/pt/pretty.toml` was left out of that
fix too, so it was still missing the `paneDividerWidth`/`paneDividerColor`
block entirely (not even present as a commented-out example, unlike every
other optional key in the file).

## Что исправлено

`bin/pt/pretty.toml`:
1. Replaced the stale `# CLI: -sidebarTabs / +sidebarTabs...` comment and
   `sidebarTabs = false` with `# CLI: -tabBar top|sidebar ...` and
   `tabBar = "top"`, matching `bin/st/shitty.toml`'s wording and default
   (no sidebar tab list — same behavior as the old `sidebarTabs = false`).
2. Added the `paneDividerWidth = 1` / `# paneDividerColor = "#555753"` block
   (with the same documentation comments as `shitty.toml`), which was
   entirely absent from `pretty.toml` — the same class of "st was fixed,
   pt wasn't" drift as the `tabBar` bug, from the same historical commit.

`bin/st/shitty.toml` was not modified — it already matched the options table.

## Критерии приёмки

| # | Критерий | Результат |
|---|----------|-----------|
| 1 | Падающий тест находится, падает до правки, проходит после | See below — **PASS** |
| 2 | `./build st pt --clear` зелёная | **PASS** — 228/228 nodes, EXIT=0 |
| 3 | `unit_tests`/`pty_test_helper` build + run gives `OK: 955` | **PASS** — EXIT=0, `OK: 955` |
| 4 | Список расхождений toml ↔ options table (обе стороны) | See `## Обнаружено` |

### 1. Test found and reproduced

```
$ grep -rn "test_pretty_surface_has_only_pretty_branding" tst/
tst/production_surface.py:64:    def test_pretty_surface_has_only_pretty_branding(self):
```

**Before the fix** (built from the pre-fix worktree):

```
$ SHITTY_PRODUCTION_BINARY="$PWD/.build/st" SHITTY_PRETTY_BINARY="$PWD/.build/pt" \
  SHITTY_TEST_VERSION="$(date +%Y.%m.%d)" python3 tst/production_surface.py -v
...
FAIL: test_pretty_surface_has_only_pretty_branding (...)
AssertionError: b'pretty: /private/tmp/.../wt-G7/bin/pt/pretty.toml: unknown option: sidebarTabs\n' != b''

Ran 5 tests in 0.113s
FAILED (failures=1)
EXIT=0
```
(the harness itself exits 0; unittest reported one failure — matches the
`FAIL:` seen in the Fedora CI log, same message modulo path prefix)

**After the fix** (rebuilt `st`/`pt` with the corrected `pretty.toml`):

```
$ SHITTY_PRODUCTION_BINARY="$PWD/.build/st" SHITTY_PRETTY_BINARY="$PWD/.build/pt" \
  SHITTY_TEST_VERSION="$(date +%Y.%m.%d)" python3 tst/production_surface.py -v
...
test_pretty_surface_has_only_pretty_branding (...) ... ok
...
Ran 5 tests in 0.094s

OK
EXIT=0
```

### 2. `./build st pt --clear`

```
$ ./build st pt --clear
...
[LD] {227/228} $(B)/st
[LD] {228/228} $(B)/pt
EXIT=0
```

### 3. `unit_tests` / `pty_test_helper`

```
$ ./build unit_tests pty_test_helper
...
[LD] {113/125} $(B)/pty_test_helper
...
[LD] {125/125} $(B)/unit_tests
EXIT=0

$ SHITTY_PTY_TEST_HELPER="$PWD/.build/pty_test_helper" ./.build/unit_tests --threads=1
...
OK: 955
EXIT=0
```

## Обнаружено

Method: extracted the configurable-option set from `lib/shitty/options.cpp`
(`optionsTable` minus `cliOnly` entries, union `resourceTable`), then diffed
each `.toml`'s top-level keys against it, both directions.

**`bin/st/shitty.toml` — extra keys not in the options table:** none.

**`bin/st/shitty.toml` — configurable options absent from the file:**
`colorScheme`, `dump`, `paneDividerColor`, `quickCompanion`,
`quickFullscreenHotkey`, `remap`, `shell`, `sidebarColor`, `soft`,
`uriScheme`. All ten are intentionally present as commented-out examples
(no forced default makes sense for these) rather than active lines — not a
gap.

**`bin/pt/pretty.toml`, before the fix in this task — extra keys not in the
options table:** `sidebarTabs` (the bug above).

**`bin/pt/pretty.toml`, before the fix — configurable options absent from the
file:** the same ten as `st` above, plus `paneDividerWidth` and `tabBar`
(both fixed by this change; `paneDividerWidth` was missing outright, not even
as a comment, while `tabBar` was shadowed by the stale `sidebarTabs` line).

**After this task's fix, `bin/pt/pretty.toml` matches `bin/st/shitty.toml`'s
key set exactly** (verified with `diff` on both files after normalizing
brand-name text). The only remaining line-level differences between the two
files are branding text (`Shitty`/`Pretty`, `st`/`pt`, `SHITTY_FONT_SIZE`/
`PRETTY_FONT_SIZE`) and one pre-existing, purely cosmetic gap noted below.

**Out of scope, left untouched:** `bin/pt/pretty.toml` is also missing the
comment line `# CLI: -vulkanBlit / +vulkanBlit — ...` that sits next to
`vulkanInfo`'s comment in `shitty.toml`. `vulkanBlit`/`vulkanInfo` are
`cliOnly` (not configurable), so this is a doc-comment gap only — no key is
missing or unknown, no test exercises it (`tst/test_config.py`'s
documentation-completeness check only reads `bin/st/shitty.toml`), and fixing
`lib/shitty/options.cpp`/`.h` is explicitly outside this task's ownership.
Flagging it here rather than fixing it, per the "don't inflate scope" brief.

## Ownership / no out-of-bounds changes

Only `bin/pt/pretty.toml` was modified. `bin/st/shitty.toml` was inspected
but needed no change. `lib/shitty/options.cpp`, `lib/shitty/options.h`,
`lib/shitty/render_vk.cpp`, and `tst/test_gpu_parity.py` were not touched.

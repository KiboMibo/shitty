# Tab support: design

Upstream issue: [#31](https://github.com/pg83/shitty/issues/31).
Baseline: `a37d285e`.
Status: design, not approved upstream. See "Maintainer buy-in" below.

## Summary

Run N terminal sessions inside one window, one renderer, one surface and one
fontpack, switched by keyboard. `Composer` keeps its exact field layout;
`composer.vterm`, `composer.pty` and `composer.columns` continue to mean *the
active session's*. A new `SessionSet` owns the sessions and performs the
activate/deactivate transition.

The work is phased. Phase 0 restructures the test harness so the feature is
testable at all. Phase 1 delivers keyboard-driven sessions with no chrome.
Phase 2 adds the tab bar and is deliberately out of scope here.

## Scope

In scope (Phase 0 and Phase 1):

- N sessions, each owning a `Vterm`, a `Pty`, a child process and a title.
- `Cmd+T` / `Ctrl+Shift+T` new tab, appended at the right end.
- `Cmd+W` / `Ctrl+Shift+W` close the active tab.
- `Cmd+Shift+[` / `]` (Linux: `Ctrl+Shift+[` / `]`) to switch, subject to the
  chord verification in "Open questions".
- Active tab position surfaced in the window title until the bar exists.
- Font size, fontpack, window and renderer stay global.

Out of scope:

- The tab bar (Phase 2). No pixel band, no chrome, no hit testing.
- Mouse interaction with tabs.
- Drag reordering, tab close buttons, tab titles rendered on screen.
- Runtime-configurable bindings. The binding table is compile-time and per-OS.
- Panes and splits.

## Why the obvious mechanism does not work

The first design reused `Composer::resize`'s commit-then-notify walk for the
session switch. That is wrong for three independently verified reasons.

1. **The walk never fires.** `Composer::resize` early-returns when `columns`,
   `rows`, `pixelWidth` and `pixelHeight` all match (`composer.cpp:102-104`).
   With one window, one grid and a global font, a session switch changes none
   of the four, so the notification is a no-op.

2. **The renderer is not a listener.** `resizedListeners` contains exactly two
   members, the `Pty` (`pty.cpp:464`) and the `Vterm` (`vterm.cpp:9328`). Both
   renderers register only on `fontChangedListeners` (`render_vk.cpp:2105`,
   `render_metal.mm:785`) and learn geometry by pulling `composer.columns` and
   `composer.rows` at present time.

3. **Forcing the walk is harmful.** `VtermImpl::resizeGrid`'s unchanged-geometry
   path calls `setSynchronizedOutput(false)` and `reportInBandResize()`, which
   writes `CSI 48` into the child's input stream (`vterm.cpp:8400-8408`), and
   `PtyImpl::onListen` re-issues an unconditional `TIOCSWINSZ` whose failure
   calls `exit(1)` (`pty.cpp:455-461`, `530-532`). Every switch would
   `SIGWINCH` every child and inject a phantom resize report into every
   application using DEC private mode 2048.

What survives is the *shape*: commit every field, then notify, caching
`node->next` so members may unlink during the walk (`composer.cpp:106-115`).
What changes is that the transition owns a dedicated notification path instead
of borrowing the resize one.

## Architecture

### Session and SessionSet

`Session` (new `session.h` / `session.cpp`) owns one terminal session: its own
`stl::ObjPool` following the `rendererPool` precedent (`composer.h:57-59`), a
`Vterm*`, a `Pty*`, the child pid, and a cached title.

`SessionSet` owns the vector of sessions, the active index, and the transition.
`Composer` gains a single `SessionSet* sessions` field. `composer.vterm`,
`composer.pty` and `composer.ptyOutput` remain, and remain the active session's
— this is what keeps roughly ninety `composer.columns` / `composer.rows` readers
in `vterm.cpp` untouched.

### The activate transition

`SessionSet::activate(index)` performs, in order:

1. Call `deactivate()` on the outgoing `Vterm`.
2. Repoint `composer.vterm`, `composer.pty`, `composer.ptyOutput`.
3. Unlink the outgoing `Vterm` from `composer.inputHandlers` and `pushBack` the
   incoming one. This list is a first-accepts-wins dispatch chain
   (`input_router.cpp:40-48`), not a broadcast, so membership is the switch.
4. Relink the five terminal-action listener nodes (see "InputBindings").
5. Call `activate()` on the incoming `Vterm`.

It does not touch `resizedListeners` and does not call `Composer::resize`.

### Vterm::activate

`activate()` must, in order:

1. `cf->expose()` — public at `screen.h:159`. This is the existing precedent:
   the DEC alternate-screen swap pairs every `cf = frame_alt` assignment with
   exactly this call (`vterm.cpp:3337`, `3374`, `3391`), and
   `VtermImpl::fontChanged` uses the same shape (`vterm.cpp:8392-8395`).
2. `redraw()`.
3. Re-publish the session's title to the window.
4. Re-assert window-global state that is pushed rather than cached: pointer
   icon, IME caret position, focus reporting.

Two traps, both verified:

- `Vterm::expose()` (`vterm.h:136`) is **not** the fix. It only calls
  `redraw()` (`vterm.cpp:2065-2067`), which marks no rows. The screen-level
  `Screen::expose()` is the one that matters.
- `redraw()` early-returns while `synchronizedOutputMode` is set
  (`vterm.cpp:2916-2922`). A session parked in DECSET 2026 needs explicit
  handling or its first post-switch frame never arrives.

`deactivate()` records that the session is inactive so its timer fibers stop
requesting frames (see "Cost").

### Renderer

The renderer needs no structural change, but the claim that it needs *nothing*
is false. `spanGeneration()`'s contract (`screen.h:106-113`) covers the strip
arenas only. The retained `GpuCell` array is guarded on grid shape alone
(`render_vk.cpp:1950`), which a switch never changes, so undamaged rows would
render the incoming session's glyph strips with the outgoing session's colors,
attributes and line attributes. `cf->expose()` in `activate()` is what prevents
this, which is why step 1 above is load-bearing rather than defensive.

The headless `ReferenceRenderer` shares the same retention (`render_reference.cpp:602`
seeds from `cells_`), so without the expose the Python suite would assert on
corrupted output rather than catching it.

## Components

Ordered by dependency. Sizes are relative.

| Component | Size | Touches |
|---|---|---|
| Test-harness session model | large | `test_mode.cpp:1637-2767`, `tst/harness.py` |
| `registerShapeListeners` fix (prerequisite) | trivial | `screen.cpp:845-850`, `1030` |
| `Session` + `SessionSet` | medium | new `session.h/.cpp`, `composer.h`, `application.cpp:520-528` |
| `Vterm` activate/deactivate/title | medium | `vterm.h:133-161`, `vterm.cpp` |
| Per-session PTY ownership | large | `pty.h:25-38`, `pty.cpp:119,152,165-178,258,269` |
| Vterm-owned pty write path | large | ~20 sites in `vterm.cpp`, 6 of them deferred lambdas |
| Pty teardown | large | `pty.cpp:278-306,317-364,374-412,436-440,463-485` |
| CellExtraStore multi-session root set | large | `cell_extra_store.cpp:482-523`, `vterm.cpp:2924-2967` |
| InputBindings re-targeting | medium | `composer.h`, `vterm.cpp:8380-8390`, `input_bindings.cpp:87-101` |
| Child lifetime rework | medium | `pty.cpp:536-542,597`, `application.cpp:351-368,414-421` |
| Tab actions + bindings | small | `input_bindings.h:19-29`, `input_bindings.cpp:32-53` |
| Session switch input plumbing | small | `vterm.cpp:9331`, `input_router.cpp:40-48` |
| Title accessor + notification | small | `vterm.cpp:993-1005,6002-6015` |

### Prerequisite: registerShapeListeners

`registerShapeListeners()` has one call site (`screen.cpp:1030`), inside the
geometry constructor. Screens rebuilt by `resized()` go
`makePrimaryFromState` → `makePrimaryScreenFromState` → `makeScreen` → the
two-argument constructor (`screen.cpp:845-850`), which does not call it.
Consequently a screen rebuilt by any window resize is never registered on
`composer.fontChangedListeners` or `composer.cellExtrasChangedListeners`, and is
thereafter deaf to font changes and extras collections.

This is a pre-existing defect in the shipped single-session product, not a
tabs defect. It is listed here because the activate path must be built on
registered screens. It should land independently, with its own test and commit,
and may be relevant to issue #46.

### Known-broken paths this design must repair

Each is verified in the tree at `a37d285e` and each becomes a correctness bug
the moment a second session exists.

- **`PtyStreamOutput` resolves the wrong mutex.** `writeImpl` reads
  `pty->composer_.ptyMutex` (`pty.cpp:167`) — the globally active mutex, not the
  one guarding its own pty. With two sessions the lock and the resource are
  different objects. Fix: `PtyImpl` gains its own `plt::FiberMutex` member and
  uses it; remove `composer.ptyMutex` (`composer.h:69`) entirely rather than
  leaving the trap in place. Mirror in `TestPty` before any tab test is written.

- **`CellExtraStore::collect` is a cross-session use-after-free.** It migrates
  only the caller's cells and then deletes the old pool
  (`cell_extra_store.cpp:482-523`), while the root set is fed only from the
  calling vterm's screens (`vterm.cpp:2955-2965`). Every other session's
  `TerminalCell`s retain stale `extraRef`s into freed memory. Separately,
  `setCellCount` is last-writer-wins (`vterm.cpp:2924-2930`), so N sessions
  undersize the GC budget by roughly N. Fix: hoist the root walk to
  `SessionSet` so every live session's primary and alternate screens contribute,
  and sum the cell counts. The failure mode is silent corruption of graphemes,
  underline colors and hyperlink targets, so its unit test is written first.

- **Deferred transactions re-read the pty after resuming.** Clipboard, OSC 52,
  kitty clipboard and drop handlers take a `LockGuard` and then read
  `composer.pty->output()` *after* parking (`vterm.cpp:1382-1394`, `6309`,
  `6318`, `6381-6382`, `2276-2277`, `2341`, `2365`). A switch mid-paste delivers
  the tail of the payload to the wrong shell, and the window is up to 30 seconds
  (`platform_wayland.cpp:135`). Fix: capture the session's `Pty*`, `Output*` and
  mutex by value at spawn time. The payload-copy precedent already exists
  (`vterm.cpp:1315-1323`, `2231-2241`).

- **Child lifetime is hardwired to one pty.** `childPid` is a file-scope global
  overwritten by each `Pty::create` (`pty.cpp:597`); the `SIGCHLD` handler reaps
  every child with `waitpid(-1)` but acts on that one pid and `_exit()`s the
  process (`application.cpp:357-365`). Independently the PTY EOF path calls
  `window->requestClose()`, which is `_exit(0)` in production
  (`pty.cpp:269`, `application.cpp:414-421`), so any tab's shell exiting kills
  the application. Fix: per-`Pty` pid, a handler that maps the reaped pid to a
  session and closes only that tab, and window close only when the last session
  goes.

- **There is no PTY teardown.** `~PtyImpl` is empty with a comment warning that
  closing a tty fd on Darwin blocks while a thread sleeps in `read` on it
  (`pty.cpp:436-440`); the master fd is never closed; all three threads are
  detached with no exit path (`pty.cpp:463-485`), and the writer's `return
  nullptr` is unreachable (`pty.cpp:411`). The reader does a plain blocking
  `::read` (`pty.cpp:281`) — the "the poll makes the read synchronous" comment
  at `pty.cpp:272` describes code that is not there. `Cmd+W` is in scope, so
  this must be built. Ordering, chosen so no thread is interrupted mid-syscall:
  `kill(pid, SIGHUP)` first so the reader's `read` returns on its own; set a
  stop flag and broadcast `gatherData_`, `feedSpace_` and `outData_` so the
  coalescer and writer leave `cond_wait`; join, which requires they stop being
  detached; unpark the feed fiber; unlink from `resizedListeners`; release both
  `LoopWake`s; close the fd.

- **`InputBindings::add` is one-shot and stores a raw list pointer.**
  `STD_ASSERT(!registered_[index])` (`input_bindings.cpp:91`) aborts on a second
  `Vterm::create` in debug; under `NDEBUG` it appends a duplicate row that the
  first-match loop (`input_bindings.cpp:116-123`) never selects, so every tab's
  `Cmd+C` would copy from tab 1 forever. Destroying a tab also leaves
  `InputBindings` holding a pointer to a dead list head
  (`vterm.cpp:8380-8390` registers members of `VtermImpl`). Fix: move the five
  terminal-action lists onto `Composer`, register once at startup, and relink
  each session's node on activate.

- **Fork now happens from a multithreaded process.** Today the single `fork`
  (`pty.cpp:565`) precedes any thread. Opening a second tab forks while three
  threads per existing pty are running (`pty.cpp:463-485`) and the child
  allocates before `exec`. Fix: precompute the child environment as a `char*[]`
  before forking, or use `posix_spawn`. The failure mode is a shell that hangs
  before `exec` with no timeout.

## Cost

The project's stated goal is speed and predictable resource use, and issue #43
is open because urxvt currently outperforms it. Costs are stated explicitly.

**Per-byte: unaffected.** The parse path is untouched. Binding lookup grows
from roughly nine entries to fourteen on a linear scan that runs once per
keystroke (`input_bindings.cpp:117-122`), which is human-rate.

**Per-frame: must be actively prevented.** Each session permanently spawns a
blink fiber and a sync watchdog that call `composer.window->requestFrame()` on
their own timers with no visibility check (`vterm.cpp:2869`, `2886`), and
`VtermImpl::flush` does the same (`vterm.cpp:2097-2100`). A background tab
running blinking text would force a present for pixels nobody sees. Gate all
three call sites on the session being active. This is correctness, not polish.

**Per-switch: real and must be measured.** Every switch is a full arena
re-upload from byte zero (`render_vk.cpp:1447-1453`), a full-grid reshape
because `assignStrips` sets everything on generation mismatch
(`render_vk.cpp:1514`), and a full-grid redispatch (`render_vk.cpp:2003-2020`).
On Metal it additionally issues `waitFrames()` — `waitUntilCompleted` on every
in-flight command buffer plus a `sched_yield` spin (`render_metal.mm:376-381`,
`515-528`). This is unavoidable given a different `Screen` always carries a
different `spanGeneration`. The switch latency must be measured and published
as a number, not assumed free.

**Throughput risk: background parsing shares the platform thread.** Each
session's feed fiber parses up to 256 KiB then yields (`pty.cpp:56`, `265`), and
the poller resumes every yielded fiber once per loop round. N flooding
background tabs put up to N × 256 KiB of parsing between poll rounds on the
thread that also renders. There is no visibility-based backpressure; the reader
keeps reading until its own 1 MiB bound (`pty.cpp:288-290`). A `yes` in a
background tab consumes full CPU and grows scrollback without limit. If
throughput regresses, this is the first place to look.

**Memory per background session** is dominated by what nothing reclaims: 96 KiB
of inline timer-fiber stacks (`vterm.cpp:902-910`), a ~34 KB `UnicodeMap` page
table (`unicode_map.h:17,37`), a 256 KiB feed-fiber stack inside `PtyImpl`
(`pty.cpp:156`), up to ~2.5 MiB of retained `Buffer` capacity, three pthread
stacks, and the screen's `ObjPool` row storage — a bump allocator with doubling
chunks and no free (`mem_pool.cpp:63-84`), which at 200×50 with `saveLines`
10000 is tens of megabytes.

Background strip arenas are **not** collected in v1. `Screen` has no public
release hook, `collectStrips` is O(rowCapacity) with an O(rows) scan inside it
(`screen.cpp:1228-1235`, `1253-1258`), reactivation would re-rasterize a full
viewport through HarfBuzz and FreeType because there is no glyph-bitmap cache
(`font_freetype.cpp:414-431`), and the arena is budgeted at 3× viewport pixels
per plane (`screen.cpp:1301-1306`) — the smaller half of per-tab memory. Measure
the real floor first, then decide whether to bound the tab count.

## Test plan

Test-driven, ordered so each test is writable before the code it exercises.
Phase 0 exists because tests 7 onward are impossible without it: `runTestMode`
hard-wires one pty pair, one `TestPty`, one `Vterm` and one `ReferenceRenderer`
across roughly 120 dispatch branches (`test_mode.cpp:1697-1720`, `1844-2767`),
`SPAWN` refuses a second child (`test_mode.cpp:1991-1993`), and no command can
observe an inactive session.

`SNAPSHOT`'s 14-field header and 50/82-character record layout must stay frozen;
it is parsed positionally in Python (`tst/harness.py:990-991`, `1010`).

### C++ unit, first

1. `InputBindings`: re-registering an action re-targets it; the old list stops
   receiving publishes.
2. `InputBindings`: an unlinked listener node stops receiving publishes.
3. The four tab actions resolve in the platform's default table.
4. `CellExtraStore::collect` with roots from two independent screen sets
   preserves both sets' `extraRef`s. Fails today.
5. Two `Vterm`s coexist on one `Composer`, both on `inputHandlers`; unlinking
   one leaves the other receiving keys.
6. `PtyImpl` writes take the mutex owned by that pty, not `composer.ptyMutex`.
   Requires a new `pty_ut.cpp`.

### Python black-box

7. `SESSION_STATE` returns `(count=1, active=0)` on a fresh terminal. The test
   that forces the harness restructure.
8. The new-tab chord is consumed by the binding and never reaches the child;
   state becomes `(2, 1)`. Drive press *and* release — `InputBindings` marks
   consumed on press and swallows the release.
9. A new tab starts blank; tab 0's content does not leak into it.
10. Switching back restores session 0 verbatim, at a cost of exactly one full
    expose rather than a repeated redraw.
11. Switching to an *idle* background tab repaints it rather than leaving the
    previous tab's pixels on screen.
12. A background session keeps parsing into its own model while inactive, and
    its output never appears in the active snapshot.
13. A background session's DA/DSR/in-band-resize reply lands in *its* pty.
14. Close removes the active tab, the remainder becomes active with content
    intact, and the application does not exit.
15. Closing the last tab shuts the window down exactly once.
16. Closing a tab returns fd and thread counts to the single-session baseline.
17. OSC 2 in a background tab updates that tab's title but not the OS window
    title; switching to it does.

## Effort

One experienced engineer, TDD as specified.

- Phase 0, prerequisites: 1 to 1.5 weeks. Nothing observable ships.
- Phase 1, sessions without chrome: 3 to 4 weeks. The Pty shutdown handshake
  and the extras GC root-set change dominate; each is a week on its own if the
  first attempt is wrong.
- Phase 2, tab bar: 2 to 3 weeks, deferred.

Phase 0 and 1 total 4 to 5.5 weeks.

Phase 2 is deferred because there is no non-grid drawing anywhere — every pixel
write is an offset from `outputOrigin = (border + col*gw, border + row*gh)`
(`render.comp:439-441`, mirrored on the CPU at `render_reference.cpp:419-420`).
A band requires a top-inset push-constant, a shader edit, regeneration of 17
SPIR-V variants and the MSL source, and emission in three backends with three
different damage models. The same floor-division appears four times with four
clamping styles (`composer.cpp:99-100`, `mouse_frontend.cpp:30-34`,
`vterm.cpp:2173-2177`, `vterm.cpp:9161-9165`) and cell-to-pixel origin three
times. Updating eleven of twelve sites yields a silent off-by-band-height in
mouse reporting that passes every existing test, because every geometry test is
keyed on `2*opts.border`.

## Open questions

**The macOS switch chord is unverified.** `find()` requires exact
`baseCodepoint` equality (`input_bindings.cpp:116-122`), and Cocoa fills
`baseCodepoint` from `charactersIgnoringModifiers`. Whether that yields `[` or
`{` under `Cmd+Shift+[` is not determinable from source: Apple documents that
`charactersIgnoringModifiers` retains Shift, while the comment at
`platform_cocoa.mm:1584` states it strips Shift. If it yields `{`, the chord is
silently dead on macOS. Verify on hardware before committing; the fallback is
`Cmd+Shift+Left/Right` or `Cmd+<digit>`.

**Maintainer buy-in is absent.** Issue #31 has no comment from the maintainer;
both comments are the reporter's. The issue proposes two options, and this
design rejects the one the reporter called minimal — native `NSWindow` tabbing —
because each tab would be a full window with its own swapchain, renderer and
fontpack, which multiplies the footprint investigated in issue #3, taxes the
stated fast-startup goal, and is macOS-only against a macOS-and-Linux platform
badge. That rejection, the phasing, and the `Composer`-as-active-session model
should be agreed upstream before Phase 1 begins.

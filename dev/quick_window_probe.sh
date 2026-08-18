#!/usr/bin/env bash
# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.
#
# Drives and observes a live quick-terminal window without System
# Events/AppleScript. That route was tried first for the
# panes-and-window-chrome plan's T3 and was unreliable in this
# environment: it could not enumerate the window at all, and synthetic
# key presses landed inconsistently. R2-qa found a route that works
# (docs/plans/reviews/panes-R2-qa.md, "Что проверено") - this script is
# that recipe, so the next wave does not have to rediscover it.
#
# Compiles dev/quick_window_probe.swift once (cached under .build/) and
# exposes it as shell functions. Source this file to use them in your
# own script:
#
#   . dev/quick_window_probe.sh
#   ST_PID=<pid of a running -quick st>
#   qwp_geometry "$ST_PID"                 # "x y width height layer onscreen", one line per window
#   qwp_chord 50 control                   # ctrl+grave - the default quickHotkey
#   qwp_move "$ST_PID" 400 300             # AX: move the focused window (top-left origin, points)
#   qwp_resize "$ST_PID" 1000 500          # AX: resize the focused window (points)
#   qwp_ptysize "$ST_PID"                  # "rows cols" of the child shell's tty
#
# Or run it directly for a self-contained smoke check against a build:
#
#   dev/quick_window_probe.sh --demo /path/to/.build/st
#
# which launches quick-terminal with a throwaway config, exercises a
# show/hide cycle, a manual move+resize, and a fullscreen chord (only
# when the binary was built with quickFullscreenHotkey), and prints
# what it measured. It is a smoke check, not a substitute for the full
# manual scenarios in the R2-qa report's "Требует человека" section -
# corner rounding and flicker still need a human's eyes (no Screen
# Recording permission in this environment either way).
#
# Requirements: Accessibility granted to whatever terminal runs this
# script (System Settings -> Privacy & Security -> Accessibility) - the
# same permission System Events itself would have needed for AX calls,
# requested directly instead. `swiftc` on PATH (ships with Xcode/CLT).
#
# Known limits (carried over from R2-qa, not fixed by this script):
# series longer than ~20 geometry probes can make
# CGWindowListCopyWindowInfo return garbage for one reading (an
# implausible width far past the screen's own) - do not average such a
# reading in, and do not run this alongside `./build test -k`, which
# spins up its own terminal instances and steals the synthetic chords.
#
# Found while building this for F2, not seen by R2-qa - environment-
# dependent, so treat both as "try it, do not assume":
#   - `qwp_chord` posting a real, valid CGEvent (verified: geometry
#     changed exactly as expected once it landed) was unreliable in the
#     sandboxed session this was written in - single presses sometimes
#     took ten retries a second apart, sometimes none in ten. R2-qa's
#     own report claims 26/26 reliable after one warmup chord; that may
#     depend on running outside whatever sandboxes this Bash tool's
#     commands. Budget for retries and do not treat one missed press as
#     a product bug before confirming with `qwp_geometry` a few more
#     times.
#   - `qwp_move`/`qwp_resize` (AX) failed outright against a bare `st`
#     binary launched from a shell in this session:
#     kAXFocusedWindowAttribute had no value and kAXWindowsAttribute
#     returned an empty array, even with a window visibly shown and
#     CGWindowListCopyWindowInfo finding it fine - despite
#     AXIsProcessTrusted() and CGPreflightPostEventAccess() both true.
#     Likely an unbundled-executable-vs-Accessibility-tree quirk (no
#     Info.plist/bundle), not fixed here since it blocked verification
#     rather than being this plan's own bug. If you hit the same thing,
#     try packaging the binary as a minimal .app bundle first
#     (dev/package_darwin_apps.sh) before spending time on the AX calls
#     themselves.

set -euo pipefail

QWP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
QWP_SWIFT_SRC="$QWP_DIR/quick_window_probe.swift"
QWP_BIN="$QWP_DIR/../.build/quick_window_probe"

qwp_build() {
    if [[ -x "$QWP_BIN" && "$QWP_BIN" -nt "$QWP_SWIFT_SRC" ]]; then
        return 0
    fi
    mkdir -p "$(dirname "$QWP_BIN")"
    swiftc -O -o "$QWP_BIN" "$QWP_SWIFT_SRC"
}

qwp_geometry() {
    qwp_build
    "$QWP_BIN" geometry "$1"
}

qwp_chord() {
    qwp_build
    "$QWP_BIN" chord "$1" "$2"
}

qwp_move() {
    qwp_build
    "$QWP_BIN" move "$1" "$2" "$3"
}

qwp_resize() {
    qwp_build
    "$QWP_BIN" resize "$1" "$2" "$3"
}

# The grid size a TUI inside the quick window would see (`tput
# lines`/`tput cols`), from outside and without typing into the
# terminal: find the child shell's tty and ask the tty driver directly.
qwp_ptysize() {
    local st_pid="$1"
    local child_pid
    child_pid="$(pgrep -P "$st_pid" | head -n1)"
    if [[ -z "$child_pid" ]]; then
        echo "qwp_ptysize: no child process of pid $st_pid" >&2
        return 1
    fi
    local tty
    tty="$(ps -o tty= -p "$child_pid" | tr -d ' ')"
    if [[ -z "$tty" || "$tty" == "??" ]]; then
        echo "qwp_ptysize: pid $child_pid has no controlling tty" >&2
        return 1
    fi
    stty -f "/dev/$tty" size
}

# Returns once qwp_geometry reports the given pid onscreen (layer >= 0,
# onscreen true) or the timeout (seconds) elapses.
qwp_wait_shown() {
    local st_pid="$1"
    local timeout="${2:-3}"
    local deadline=$((SECONDS + timeout))
    while ((SECONDS < deadline)); do
        if qwp_geometry "$st_pid" 2>/dev/null | awk '$6 == "true" { found = 1 } END { exit !found }'; then
            return 0
        fi
        sleep 0.2
    done
    return 1
}

qwp_demo() {
    local binary="$1"
    if [[ ! -x "$binary" ]]; then
        echo "quick_window_probe.sh --demo: $binary is not executable" >&2
        exit 1
    fi
    qwp_build

    local tmp
    tmp="$(mktemp -d)"
    trap 'kill "$st_pid" 2>/dev/null || true; rm -rf "$tmp"' EXIT
    cat >"$tmp/config.toml" <<'EOF'
quick = true
quickHotkey = "ctrl+grave"
quickFullscreenHotkey = "ctrl+shift+f"
quickRememberFrame = true
EOF

    "$binary" -config "$tmp/config.toml" -verbose >"$tmp/stderr.log" 2>&1 &
    local st_pid=$!
    sleep 1

    echo "== warmup chord (first press after start is reliably swallowed) =="
    qwp_chord 50 control
    sleep 0.5

    echo "== show =="
    qwp_chord 50 control
    if ! qwp_wait_shown "$st_pid"; then
        echo "window never reported onscreen; see $tmp/stderr.log" >&2
        exit 1
    fi
    qwp_geometry "$st_pid"

    echo "== move to 400,300 + resize to 1000x500 =="
    qwp_move "$st_pid" 400 300
    qwp_resize "$st_pid" 1000 500
    sleep 0.3
    qwp_geometry "$st_pid"

    echo "== hide, show: should return to 400,300 1000x500 (B2 regression check) =="
    qwp_chord 50 control
    sleep 0.3
    qwp_chord 50 control
    qwp_wait_shown "$st_pid" || true
    sleep 0.3
    qwp_geometry "$st_pid"

    echo "== fullscreen chord: expand =="
    qwp_chord 3 control,shift
    sleep 0.5
    qwp_geometry "$st_pid"

    echo "== fullscreen chord: fold back (B3 regression check) =="
    qwp_chord 3 control,shift
    sleep 0.5
    qwp_geometry "$st_pid"

    echo "== state file =="
    cat "$tmp/config-quick-frame" 2>/dev/null || echo "(no state file written)"

    echo "done; stderr log at $tmp/stderr.log (kept until this shell exits, see the trap above)"
}

if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    if [[ "${1:-}" == "--demo" && -n "${2:-}" ]]; then
        qwp_demo "$2"
    else
        echo "usage: $0 --demo /path/to/.build/st" >&2
        echo "or source this file and call qwp_geometry/qwp_chord/qwp_move/qwp_resize/qwp_ptysize directly" >&2
        exit 1
    fi
fi

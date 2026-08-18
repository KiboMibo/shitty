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
#   qwp_screens                            # "frame=... visible=... scale=..." per display
#   qwp_warp 2000 500                      # move the pointer (top-left origin, points)
#
# Or run it directly for a self-contained smoke check against a build:
#
#   dev/quick_window_probe.sh --demo /path/to/.build/st_test
#
# which launches quick-terminal with a throwaway config, exercises a
# show/hide cycle and a manual move+resize, and prints what it measured.
# It needs .build/st_test rather than .build/st: the show/hide driver is
# the SIGUSR2 entry point described below, which only a SHITTY_FOR_TESTS
# build carries. It is a smoke check, not a substitute for the full
# manual scenarios in the R2-qa report's "Требует человека" section -
# corner rounding and flicker still need a human's eyes (no Screen
# Recording permission in this environment either way).
#
# Requirements: Accessibility granted to whatever terminal runs this
# script (System Settings -> Privacy & Security -> Accessibility) - the
# same permission System Events itself would have needed for AX calls,
# requested directly instead. `swiftc` on PATH (ships with Xcode/CLT).
# And an unlocked screen: with the screen locked the window still appears
# in the CG window list, but it never becomes key (so no hide ever
# persists a frame) and AX refuses to set position or size (-25205).
#
# Any hand-written config for these runs needs a chord the grammar
# actually knows (lib/shitty/quick_hotkey_chord.cpp): ctrl/shift/alt/super
# plus one of the named keys or f1..f12. `cmd+shift+f12` is the usual
# choice here - free, and clear of the user's own quick terminal on
# ctrl+grave. There is no f13 and up: a chord naming one is rejected
# outright ("unrecognized chord"), the hotkey is disabled, and the window
# is then shown normally - so a recipe with one has you measuring an
# ordinary window instead of a quick one (R2-qa round 3, I11).
#
# Known limits (carried over from R2-qa, not fixed by this script):
# series longer than ~20 geometry probes can make
# CGWindowListCopyWindowInfo return garbage for one reading (an
# implausible width far past the screen's own) - do not average such a
# reading in, and do not run this alongside `./build test -k`, which
# spins up its own terminal instances and steals the synthetic chords.
#
# Do not drive the window with `qwp_chord` - use the signal below.
# Three sessions in a row have now failed to land a synthetic Carbon
# chord in a test binary (F2: 0 of 15, R2-qa round 2: 0 of 22, both with
# AXIsProcessTrusted(), CGPreflightPostEventAccess() and
# CGPreflightListenEventAccess() all true and secure input off), while
# R2-qa round 1 reported 26 of 26 on the same machine. Whatever the
# difference is, it is not something a script can arrange. Two things
# make it worse and are worth knowing anyway: a chord that misses your
# process still lands in whatever app is frontmost, and the user's own
# /Applications/Shitty.app registers the same chord - Carbon accepts a
# duplicate registration silently and delivers to one process only.
# Check `pgrep -fl "Shitty.app"` before assuming a press was lost.
#
# What does work, every time: a binary built with SHITTY_FOR_TESTS
# (.build/st_test) toggles the quick window on SIGUSR2, handled on the
# main loop exactly where the hotkey would have been handled
# (lib/shitty/application.cpp, installQuickToggleSignal). R2-qa round 2
# had to patch configuration.cpp by hand to get this; it is a permanent
# entry point now, and it is not in the shipped terminal.
#
#   .build/st_test -config /tmp/q.toml & ST_PID=$!
#   kill -USR2 "$ST_PID"                 # show
#   qwp_wait_shown "$ST_PID" && qwp_geometry "$ST_PID"
#   kill -USR2 "$ST_PID"                 # hide
#
# `qwp_move`/`qwp_resize` (AX) do work against a bare, unbundled `st`,
# contrary to what F2 recorded here: kAXWindowsAttribute is empty only
# while the window is hidden. Show the window first (and wait for
# `qwp_wait_shown`) and the AX lookup finds it.

# Only when this file is being run, not sourced. `set -e` set inside a
# sourced file belongs to the shell that sourced it, so the first non-zero
# return from any probe silently killed the caller's measurement halfway
# through - and sourcing is the documented way to use this (R2-qa round 3,
# I13). Also note that this is bash, not zsh: source it from `bash -c`.
QWP_SOURCE="${BASH_SOURCE[0]:-$0}"
if [[ "$QWP_SOURCE" == "${0}" ]]; then
    set -euo pipefail
fi

QWP_DIR="$(cd "$(dirname "$QWP_SOURCE")" && pwd)"
QWP_SWIFT_SRC="$QWP_DIR/quick_window_probe.swift"
QWP_BIN="$QWP_DIR/../.build/quick_window_probe"
QWP_DEMO_PID=""
QWP_DEMO_TMP=""

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

qwp_screens() {
    qwp_build
    "$QWP_BIN" screens
}

qwp_warp() {
    qwp_build
    "$QWP_BIN" warp "$1" "$2"
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

    # Not locals: the EXIT trap runs after this function has already
    # returned, where a `local` no longer exists and `set -u` turns the
    # cleanup into an error.
    QWP_DEMO_TMP="$(mktemp -d)"
    local tmp="$QWP_DEMO_TMP"
    trap 'kill "$QWP_DEMO_PID" 2>/dev/null || true; rm -rf "$QWP_DEMO_TMP"' EXIT
    cat >"$tmp/config.toml" <<'EOF'
quick = true
quickHotkey = "ctrl+grave"
quickFullscreenHotkey = "ctrl+shift+f"
quickRememberFrame = true
EOF

    "$binary" -config "$tmp/config.toml" -verbose >"$tmp/stderr.log" 2>&1 &
    local st_pid=$!
    QWP_DEMO_PID="$st_pid"
    sleep 1

    echo "== show =="
    kill -USR2 "$st_pid"
    if ! qwp_wait_shown "$st_pid"; then
        echo "window never reported onscreen; see $tmp/stderr.log" >&2
        echo "(a binary without SHITTY_FOR_TESTS ignores SIGUSR2 - use .build/st_test)" >&2
        exit 1
    fi
    qwp_geometry "$st_pid"

    echo "== move to 400,300 + resize to 1000x500 =="
    qwp_move "$st_pid" 400 300
    qwp_resize "$st_pid" 1000 500
    sleep 0.3
    qwp_geometry "$st_pid"

    echo "== hide, show: should return to 400,300 1000x500 (B2 regression check) =="
    kill -USR2 "$st_pid"
    sleep 0.5
    kill -USR2 "$st_pid"
    qwp_wait_shown "$st_pid" || true
    sleep 0.3
    qwp_geometry "$st_pid"

    echo "== state file =="
    cat "$tmp/config-quick-frame" 2>/dev/null || echo "(no state file written)"

    echo "done; stderr log at $tmp/stderr.log (kept until this shell exits, see the trap above)"
}

if [[ "$QWP_SOURCE" == "${0}" ]]; then
    if [[ "${1:-}" == "--demo" && -n "${2:-}" ]]; then
        qwp_demo "$2"
    else
        echo "usage: $0 --demo /path/to/.build/st_test" >&2
        echo "or source this file and call qwp_geometry/qwp_move/qwp_resize/qwp_ptysize/qwp_screens/qwp_warp directly" >&2
        exit 1
    fi
fi

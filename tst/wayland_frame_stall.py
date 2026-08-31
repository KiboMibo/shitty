# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

# Child output must keep flowing while the compositor withholds
# wl_surface.frame callbacks (issue #104): frame callbacks pace
# presentation only, so PTY draining, parsing and damage coalescing
# continue while the surface is invisible.
#
# The production binary runs a bounded-progress PTY workload inside a
# headless sway session; powering the sway output off makes wlroots stop
# scheduling frames, which withholds frame callbacks exactly the way a
# session lock or a fully obscured surface does. The workload must keep
# completing 2 MiB blocks while the output is off, and again after it
# comes back on.
#
# Without sway (or a Vulkan device for the terminal's renderer) the test
# skips, unless SHITTY_TEST_WAYLAND_REQUIRED=1 - the CI environment sets
# it so a broken compositor setup fails instead of rotting silently.

import argparse
import json
import os
import shutil
import signal
import stat
import subprocess
import sys
import tempfile
import time
from pathlib import Path


REQUIRED = os.environ.get("SHITTY_TEST_WAYLAND_REQUIRED") == "1"


class Skip(Exception):
    pass


def executable(value, fallback):
    candidate = value or shutil.which(fallback)
    if candidate is None:
        raise Skip(f"{fallback} executable was not found")
    resolved = Path(candidate).resolve()
    if not resolved.is_file() or not os.access(resolved, os.X_OK):
        raise RuntimeError(f"not an executable: {resolved}")
    return resolved


def runtime_root():
    # struct sockaddr_un caps the socket path libwayland will bind, and
    # the build runner's per-command TMPDIR is far too deep - pick the
    # first writable base short enough to hold the sockets.
    candidates = ["/tmp", os.environ.get("NIX_BUILD_TOP"), tempfile.gettempdir()]
    for candidate in candidates:
        if not candidate or not os.path.isdir(candidate) or not os.access(candidate, os.W_OK):
            continue
        probe = os.path.join(candidate, "shitty-frame-stall-XXXXXXXX", "wayland-9")
        if len(probe) <= 100:
            return candidate
    raise RuntimeError("no writable directory short enough for a Wayland socket path")


def wait_for_socket(root, process, pattern, timeout=10):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        for candidate in root.glob(pattern):
            try:
                if stat.S_ISSOCK(candidate.stat().st_mode):
                    return candidate
            except FileNotFoundError:
                continue
        if process.poll() is not None:
            raise RuntimeError(f"sway exited with status {process.returncode}")
        time.sleep(0.05)
    raise RuntimeError(f"sway did not publish a {pattern} socket")


def read_iteration(progress):
    deadline = time.monotonic() + 2
    while time.monotonic() < deadline:
        try:
            fields = progress.read_text().split()
        except FileNotFoundError:
            fields = []
        if fields:
            try:
                return int(fields[0])
            except ValueError:
                pass
        time.sleep(0.02)
    raise RuntimeError("workload did not publish valid progress")


def wait_for_progress(progress, process, timeout=30):
    deadline = time.monotonic() + timeout
    first = None
    first_time = None
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"Shitty exited with status {process.returncode}")
        try:
            current = read_iteration(progress)
        except RuntimeError:
            continue
        if first is None:
            first = current
            first_time = time.monotonic()
        elif current >= first + 2:
            elapsed = time.monotonic() - first_time
            return first, current, elapsed / (current - first)
        time.sleep(0.1)
    raise RuntimeError("workload made no baseline progress")


def wait_for_continued_progress(progress, process, start, timeout):
    deadline = time.monotonic() + timeout
    target = start + 2
    current = start
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"Shitty exited with status {process.returncode}")
        current = read_iteration(progress)
        if current >= target:
            return current
        time.sleep(0.05)
    return current


def capture_threads(process, destination):
    lines = []
    task_root = Path(f"/proc/{process.pid}/task")
    try:
        tasks = sorted(task_root.iterdir(), key=lambda item: int(item.name))
    except FileNotFoundError:
        tasks = []
    for task in tasks:
        try:
            comm = (task / "comm").read_text().strip()
            status_lines = (task / "status").read_text().splitlines()
            state = next(
                (
                    line.split(":", 1)[1].strip()
                    for line in status_lines
                    if line.startswith("State:")
                ),
                "unknown",
            )
            wchan = (task / "wchan").read_text().strip()
        except (FileNotFoundError, PermissionError):
            continue
        lines.append(f"tid={task.name} comm={comm} state={state} wchan={wchan}")
    destination.write_text("\n".join(lines) + ("\n" if lines else ""))


def reap(process):
    try:
        process.wait(timeout=3)
    except subprocess.TimeoutExpired as error:
        raise RuntimeError(f"process {process.pid} did not exit") from error


def stop_group(process):
    if process is None:
        return
    process_group = process.pid
    try:
        os.killpg(process_group, signal.SIGTERM)
    except ProcessLookupError:
        if process.poll() is None:
            reap(process)
        return
    if process.poll() is None:
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            pass
    try:
        os.killpg(process_group, signal.SIGKILL)
    except ProcessLookupError:
        if process.poll() is None:
            reap(process)
        return
    if process.poll() is None:
        reap(process)
    deadline = time.monotonic() + 3
    while time.monotonic() < deadline:
        try:
            os.killpg(process_group, 0)
        except ProcessLookupError:
            return
        time.sleep(0.05)
    raise RuntimeError(f"process group {process_group} survived SIGKILL")


def swaymsg_run(swaymsg, ipc_socket, *arguments):
    result = subprocess.run(
        [str(swaymsg), "-s", str(ipc_socket), *arguments],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=10,
        check=False,
    )
    if result.returncode != 0:
        command = " ".join(arguments)
        output = result.stdout.decode(errors="replace").strip()
        raise RuntimeError(f"swaymsg {command} failed: {output}")
    return result.stdout


def output_power(swaymsg, ipc_socket):
    outputs = json.loads(swaymsg_run(swaymsg, ipc_socket, "-t", "get_outputs"))
    if not outputs:
        raise RuntimeError("sway reports no outputs")
    return all(entry.get("power", False) for entry in outputs)


def parse_args():
    parser = argparse.ArgumentParser(
        description="Verify PTY progress while a headless sway output withholds frame callbacks."
    )
    parser.add_argument("--binary", default=os.environ.get("SHITTY_PRODUCTION_BINARY"))
    parser.add_argument("--sway", default=os.environ.get("SWAY_BINARY"))
    parser.add_argument("--swaymsg", default=os.environ.get("SWAYMSG_BINARY"))
    parser.add_argument("--progress-timeout", type=float, default=5.0)
    return parser.parse_args()


def run(args):
    if not args.binary:
        raise RuntimeError("--binary or SHITTY_PRODUCTION_BINARY is required")
    shitty = executable(args.binary, "st")
    sway = executable(args.sway, "sway")
    swaymsg = executable(args.swaymsg, "swaymsg")
    artifacts = Path(tempfile.mkdtemp(prefix="shitty-frame-stall-", dir=runtime_root()))
    artifacts.chmod(0o700)
    print(f"binary: {shitty}")
    print(f"artifacts: {artifacts}")

    compositor = None
    terminal = None
    logs = []
    try:
        sway_config = artifacts / "sway.config"
        sway_config.write_text("xwayland disable\n")
        sway_environment = os.environ.copy()
        sway_environment.pop("DISPLAY", None)
        sway_environment.pop("WAYLAND_DISPLAY", None)
        sway_environment.update(
            {
                "XDG_RUNTIME_DIR": str(artifacts),
                "WLR_BACKENDS": "headless",
                "WLR_HEADLESS_OUTPUTS": "1",
                "WLR_LIBINPUT_NO_DEVICES": "1",
                "WLR_RENDERER": "pixman",
            }
        )
        # Without a session bus the nixpkgs sway wrapper execs
        # dbus-run-session, and the sandbox has no dbus-daemon; a dummy
        # address makes it exec sway directly, which needs no bus here.
        sway_environment.setdefault("DBUS_SESSION_BUS_ADDRESS", "unix:path=/dev/null")
        sway_log = (artifacts / "sway.log").open("wb")
        logs.append(sway_log)
        compositor = subprocess.Popen(
            [str(sway), "-c", str(sway_config)],
            env=sway_environment,
            stdout=sway_log,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        wayland_socket = wait_for_socket(artifacts, compositor, "wayland-*[0-9]")
        ipc_socket = wait_for_socket(artifacts, compositor, "sway-ipc.*.sock")
        if not output_power(swaymsg, ipc_socket):
            raise RuntimeError("the headless output did not come up powered")

        client_environment = os.environ.copy()
        client_environment.pop("DISPLAY", None)
        client_environment.update(
            {
                "XDG_RUNTIME_DIR": str(artifacts),
                "WAYLAND_DISPLAY": wayland_socket.name,
            }
        )
        progress = artifacts / "progress"
        workload = (
            "i=0; while :; do "
            "yes 0123456789abcdef | head -c 2097152; "
            'i=$((i + 1)); date +"$i %s.%N" > "$1"; '
            "done"
        )
        terminal_log_path = artifacts / "shitty.log"
        terminal_log = terminal_log_path.open("wb")
        logs.append(terminal_log)
        terminal = subprocess.Popen(
            [
                str(shitty),
                "-title",
                "shitty-frame-stall-test",
                "-e",
                "/bin/sh",
                "-c",
                workload,
                "shitty-frame-stall-workload",
                str(progress),
            ],
            env=client_environment,
            stdout=subprocess.DEVNULL,
            stderr=terminal_log,
            start_new_session=True,
        )
        try:
            baseline_begin, baseline_end, baseline_iteration_seconds = wait_for_progress(
                progress,
                terminal,
            )
        except RuntimeError:
            log_tail = terminal_log_path.read_bytes()[-2048:]
            if b"No Vulkan device" in log_tail and not REQUIRED:
                raise Skip("no Vulkan device for the terminal's renderer") from None
            sys.stdout.buffer.write(log_tail)
            raise
        print(f"baseline progress: {baseline_begin} -> {baseline_end}")

        swaymsg_run(swaymsg, ipc_socket, "output * power off")
        if output_power(swaymsg, ipc_socket):
            raise RuntimeError("the output stayed powered after power off")
        print("output powered off; frame callbacks withheld")
        withheld_begin = read_iteration(progress)
        progress_timeout = max(
            args.progress_timeout,
            min(baseline_iteration_seconds * 10, 30),
        )
        print(f"withheld progress deadline: {progress_timeout:.3f}s")
        withheld_end = wait_for_continued_progress(
            progress,
            terminal,
            withheld_begin,
            progress_timeout,
        )
        print(f"withheld progress: {withheld_begin} -> {withheld_end}")
        if withheld_end < withheld_begin + 2:
            capture_threads(terminal, artifacts / "shitty.threads.txt")
            print("FAIL: child output stalled while frame callbacks were withheld")
            return 1

        swaymsg_run(swaymsg, ipc_socket, "output * power on")
        resumed_begin = read_iteration(progress)
        resumed_end = wait_for_continued_progress(
            progress,
            terminal,
            resumed_begin,
            progress_timeout,
        )
        print(f"resumed progress: {resumed_begin} -> {resumed_end}")
        if resumed_end < resumed_begin + 2:
            capture_threads(terminal, artifacts / "shitty.threads.txt")
            print("FAIL: child output stalled after the output came back on")
            return 1
        print("PASS: child output continued while frame callbacks were withheld")
        return 0
    finally:
        cleanup_errors = []
        for process in (terminal, compositor):
            try:
                stop_group(process)
            except RuntimeError as error:
                cleanup_errors.append(str(error))
        for log in logs:
            log.close()
        if cleanup_errors:
            raise RuntimeError("; ".join(cleanup_errors))


def main():
    args = parse_args()
    if args.progress_timeout <= 0:
        print("error: --progress-timeout must be positive", file=sys.stderr)
        return 2
    try:
        return run(args)
    except Skip as reason:
        if REQUIRED:
            print(f"error: required but skipped: {reason}", file=sys.stderr)
            return 2
        print(f"SKIP: {reason}")
        return 0
    except RuntimeError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())

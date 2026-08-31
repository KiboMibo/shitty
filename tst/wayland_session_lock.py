# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import argparse
import os
import shutil
import signal
import stat
import subprocess
import sys
import tempfile
import time
from pathlib import Path


def executable(value, fallback):
    candidate = value or shutil.which(fallback)
    if candidate is None:
        raise RuntimeError(f"{fallback} executable was not found")
    resolved = Path(candidate).resolve()
    if not resolved.is_file() or not os.access(resolved, os.X_OK):
        raise RuntimeError(f"not an executable: {resolved}")
    return resolved


def wait_for_socket(root, process, timeout=10):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        for candidate in root.glob("wayland-*"):
            try:
                if stat.S_ISSOCK(candidate.stat().st_mode):
                    return candidate
            except FileNotFoundError:
                continue
        if process.poll() is not None:
            raise RuntimeError(f"nested Qtile exited with status {process.returncode}")
        time.sleep(0.05)
    raise RuntimeError("nested Qtile did not publish a Wayland socket")


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


def wait_for_progress(progress, process, timeout=15):
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


def wait_for_lock(log, process, timeout=10):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"swaylock exited with status {process.returncode}")
        try:
            content = log.read_bytes()
        except FileNotFoundError:
            content = b""
        if any(
            b"ext_session_lock_v1#" in line and b".locked()" in line
            for line in content.splitlines()
        ):
            return
        time.sleep(0.02)
    raise RuntimeError("swaylock did not receive the session-lock locked event")


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


def parse_args():
    parser = argparse.ArgumentParser(
        description="Reproduce PTY backpressure while a nested Wayland session is locked."
    )
    parser.add_argument("--binary", default=os.environ.get("SHITTY_PRODUCTION_BINARY"))
    parser.add_argument("--qtile", default=os.environ.get("QTILE_BINARY"))
    parser.add_argument("--swaylock", default=os.environ.get("SWAYLOCK_BINARY"))
    parser.add_argument("--progress-timeout", type=float, default=5.0)
    return parser.parse_args()


def run(args):
    if not args.binary:
        raise RuntimeError("--binary or SHITTY_PRODUCTION_BINARY is required")
    shitty = executable(args.binary, "st")
    qtile = executable(args.qtile, "qtile")
    swaylock = executable(args.swaylock, "swaylock")
    artifacts = Path(tempfile.mkdtemp(prefix="shitty-session-lock-"))
    artifacts.chmod(0o700)
    print(f"binary: {shitty}")
    print(f"artifacts: {artifacts}")

    nested = None
    terminal = None
    locker = None
    logs = []
    try:
        nested_environment = os.environ.copy()
        nested_environment.pop("DISPLAY", None)
        nested_environment.pop("WAYLAND_DISPLAY", None)
        nested_environment.update(
            {
                "XDG_RUNTIME_DIR": str(artifacts),
                "WLR_BACKENDS": "headless",
                "WLR_HEADLESS_OUTPUTS": "1",
                "WLR_LIBINPUT_NO_DEVICES": "1",
            }
        )
        qtile_stdout = (artifacts / "qtile.stdout.log").open("wb")
        qtile_stderr = (artifacts / "qtile.stderr.log").open("wb")
        logs.extend((qtile_stdout, qtile_stderr))
        nested = subprocess.Popen(
            [
                str(qtile),
                "start",
                "-d",
                "-n",
                "-b",
                "wayland",
                "-s",
                str(artifacts / "qtile.sock"),
                "-l",
                "INFO",
                "-p",
                str(artifacts / "qtile.log"),
            ],
            env=nested_environment,
            stdout=qtile_stdout,
            stderr=qtile_stderr,
            start_new_session=True,
        )
        wayland_socket = wait_for_socket(artifacts, nested)

        client_environment = os.environ.copy()
        client_environment.update(
            {
                "XDG_RUNTIME_DIR": str(artifacts),
                "WAYLAND_DISPLAY": wayland_socket.name,
                "WAYLAND_DEBUG": "client",
            }
        )
        progress = artifacts / "progress"
        workload = (
            "i=0; while :; do "
            "yes 0123456789abcdef | head -c 2097152; "
            'i=$((i + 1)); date +"$i %s.%N" > "$1"; '
            "done"
        )
        wayland_log = (artifacts / "shitty.wayland.log").open("wb")
        logs.append(wayland_log)
        terminal = subprocess.Popen(
            [
                str(shitty),
                "-title",
                "shitty-session-lock-test",
                "-geometry",
                "80x24",
                "-e",
                "/bin/sh",
                "-c",
                workload,
                "shitty-session-lock-workload",
                str(progress),
            ],
            env=client_environment,
            stdout=subprocess.DEVNULL,
            stderr=wayland_log,
            start_new_session=True,
        )
        baseline_begin, baseline_end, baseline_iteration_seconds = wait_for_progress(
            progress,
            terminal,
        )
        print(f"baseline progress: {baseline_begin} -> {baseline_end}")

        swaylock_log_path = artifacts / "swaylock.log"
        swaylock_log = swaylock_log_path.open("wb")
        logs.append(swaylock_log)
        locker = subprocess.Popen(
            [str(swaylock), "-c", "000000"],
            env=client_environment,
            stdout=swaylock_log,
            stderr=subprocess.STDOUT,
            start_new_session=True,
        )
        lock_started = time.monotonic()
        wait_for_lock(swaylock_log_path, locker)
        print(f"session lock active after {time.monotonic() - lock_started:.3f}s")
        locked_begin = read_iteration(progress)
        progress_timeout = max(
            args.progress_timeout,
            min(baseline_iteration_seconds * 10, 30),
        )
        print(f"post-lock progress deadline: {progress_timeout:.3f}s")
        locked_end = wait_for_continued_progress(
            progress,
            terminal,
            locked_begin,
            progress_timeout,
        )
        print(f"locked progress: {locked_begin} -> {locked_end}")
        if locked_end < locked_begin + 2:
            capture_threads(terminal, artifacts / "shitty.threads.txt")
            print("FAIL: child output stalled while the session lock was active")
            return 1
        print("PASS: child output continued while the session lock was active")
        return 0
    finally:
        cleanup_errors = []
        for process in (locker, terminal, nested):
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
    except RuntimeError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())

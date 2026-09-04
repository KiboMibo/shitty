# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

# Issue #108's invariant on the production binary: while the running
# application sets no title, the toplevel shows the pty's foreground
# process name; a title the application did set is not disturbed while
# that application stays in the foreground.
#
# Runs headless sway like wayland_frame_stall.py, whose helpers this
# reuses, and reads the toplevel titles back through swaymsg. Skips
# without sway or a Vulkan device unless SHITTY_TEST_WAYLAND_REQUIRED=1.

import argparse
import json
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

from wayland_frame_stall import (
    REQUIRED,
    Skip,
    executable,
    runtime_root,
    stop_group,
    swaymsg_run,
    wait_for_socket,
)


def window_names(swaymsg, ipc_socket):
    tree = json.loads(swaymsg_run(swaymsg, ipc_socket, "-t", "get_tree"))
    names = []

    def visit(node):
        for child in node.get("nodes", []) + node.get("floating_nodes", []):
            # Real views carry a pid; workspaces and outputs do not.
            if child.get("pid") and child.get("name"):
                names.append(child["name"])
            visit(child)

    visit(tree)
    return names


def wait_for_name(swaymsg, ipc_socket, expected, process, timeout=15):
    deadline = time.monotonic() + timeout
    seen = []
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"Shitty exited with status {process.returncode}")
        seen = window_names(swaymsg, ipc_socket)
        for name in expected:
            if name in seen:
                return name
        time.sleep(0.2)
    raise RuntimeError(f"no toplevel named {expected!r}; saw {seen!r}")


def parse_args():
    parser = argparse.ArgumentParser(
        description="Verify the foreground-process title fallback under headless sway."
    )
    parser.add_argument("--binary", default=os.environ.get("SHITTY_PRODUCTION_BINARY"))
    parser.add_argument("--sway", default=os.environ.get("SWAY_BINARY"))
    parser.add_argument("--swaymsg", default=os.environ.get("SWAYMSG_BINARY"))
    return parser.parse_args()


def launch_terminal(shitty, environment, artifacts, label, script):
    log = (artifacts / f"shitty.{label}.log").open("wb")
    terminal = subprocess.Popen(
        [str(shitty), "-e", "/bin/sh", "-c", script],
        env=environment,
        stdout=subprocess.DEVNULL,
        stderr=log,
        start_new_session=True,
    )
    return terminal, log


def run(args):
    if not args.binary:
        raise RuntimeError("--binary or SHITTY_PRODUCTION_BINARY is required")
    shitty = executable(args.binary, "st")
    sway = executable(args.sway, "sway")
    swaymsg = executable(args.swaymsg, "swaymsg")
    artifacts = Path(tempfile.mkdtemp(prefix="shitty-title-", dir=runtime_root()))
    artifacts.chmod(0o700)
    print(f"binary: {shitty}")
    print(f"artifacts: {artifacts}")

    compositor = None
    plain = None
    titled = None
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

        client_environment = os.environ.copy()
        client_environment.pop("DISPLAY", None)
        client_environment.update(
            {
                "XDG_RUNTIME_DIR": str(artifacts),
                "WAYLAND_DISPLAY": wayland_socket.name,
            }
        )

        # A child that never touches the title: the fallback must name
        # it. The shell execs its last command, so the observed name
        # settles on the sleep itself.
        plain, plain_log = launch_terminal(
            shitty, client_environment, artifacts, "plain", "sleep 300"
        )
        logs.append(plain_log)
        try:
            # Whether the shell execs its last command or forks it and
            # waits differs by shell; either name proves the fallback.
            fallback = wait_for_name(swaymsg, ipc_socket, ("sleep", "sh"), plain)
        except RuntimeError:
            log_tail = (artifacts / "shitty.plain.log").read_bytes()[-2048:]
            if b"No Vulkan device" in log_tail and not REQUIRED:
                raise Skip("no Vulkan device for the terminal's renderer") from None
            sys.stdout.buffer.write(log_tail)
            raise
        print(f"fallback title: {fallback}")

        # A shell that sets a title and stays in the foreground - the
        # loop keeps it from exec'ing away: the poller must not dethrone
        # it, however long it watches.
        titled, titled_log = launch_terminal(
            shitty,
            client_environment,
            artifacts,
            "titled",
            'printf "\\033]2;STALE-CHECK\\007"; while :; do sleep 1; done',
        )
        logs.append(titled_log)
        wait_for_name(swaymsg, ipc_socket, ("STALE-CHECK",), titled)
        time.sleep(2)
        if "STALE-CHECK" not in window_names(swaymsg, ipc_socket):
            print("FAIL: the standing foreground's own title was dethroned")
            return 1
        print("application title survives its foreground: STALE-CHECK")
        print("PASS: foreground process name fills the title fallback")
        return 0
    finally:
        cleanup_errors = []
        for process in (plain, titled, compositor):
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

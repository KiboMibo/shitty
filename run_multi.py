#!/usr/bin/env python3
# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.


import os
import signal
import subprocess
import sys
import time


def worker_count():
    value = os.environ.get("FUZZ_JOBS")
    if value is None:
        return os.cpu_count() or 1
    try:
        count = int(value)
    except ValueError:
        raise SystemExit("FUZZ_JOBS must be a positive integer")
    if count < 1:
        raise SystemExit("FUZZ_JOBS must be a positive integer")
    return count


def signal_group(process, signum):
    if process.poll() is None:
        try:
            os.killpg(process.pid, signum)
        except ProcessLookupError:
            pass


def stop_workers(processes):
    for process in processes:
        signal_group(process, signal.SIGTERM)

    deadline = time.monotonic() + 2
    while time.monotonic() < deadline:
        if all(process.poll() is not None for process in processes):
            break
        time.sleep(0.01)

    for process in processes:
        signal_group(process, signal.SIGKILL)
    for process in processes:
        process.wait()


def exit_code(status):
    return status if status >= 0 else 128 - status


def main():
    if len(sys.argv) < 2:
        raise SystemExit(f"usage: {sys.argv[0]} PROGRAM [ARGUMENT ...]")

    command = sys.argv[1:]
    processes = []
    try:
        for _ in range(worker_count()):
            processes.append(subprocess.Popen(command, start_new_session=True))

        while True:
            for process in processes:
                status = process.poll()
                if status is not None:
                    stop_workers(processes)
                    return exit_code(status)
            time.sleep(0.01)
    except KeyboardInterrupt:
        stop_workers(processes)
        return 130
    except OSError as error:
        stop_workers(processes)
        raise SystemExit(error)


if __name__ == "__main__":
    sys.exit(main())

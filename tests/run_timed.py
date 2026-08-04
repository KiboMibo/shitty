"""Run a command with a hard timeout, killing its whole process group.

Guards test invocations in build.py. timeout(1) is not usable here: the
build runner resolves argv[0] through symlinks, which breaks multi-call
coreutils, and macOS has no timeout(1) at all.
"""
import os
import signal
import subprocess
import sys
import time


def main():
    limit = float(sys.argv[1])
    argv = sys.argv[2:]
    started = time.monotonic()
    process = subprocess.Popen(argv, start_new_session=True)
    try:
        returncode = process.wait(limit)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        try:
            process.wait(10)
        except subprocess.TimeoutExpired:
            # A test wedged in an uninterruptible kernel sleep (macOS pty
            # driver) survives even SIGKILL; report and move on rather
            # than hang the whole build behind one corpse.
            print(
                f"unreapable after SIGKILL: {' '.join(argv)}",
                file=sys.stderr,
            )
        elapsed = time.monotonic() - started
        print(
            f"timed out after {elapsed:.3f}s: {' '.join(argv)}",
            file=sys.stderr,
        )
        return 124

    elapsed = time.monotonic() - started
    print(
        f"test command finished in {elapsed:.3f}s "
        f"(exit {returncode}): {' '.join(argv)}",
        file=sys.stderr,
    )
    return returncode


if __name__ == "__main__":
    sys.exit(main())

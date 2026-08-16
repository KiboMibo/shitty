#!/usr/bin/env python3
# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

"""Prefix streamed log lines with monotonic elapsed time."""

import sys
import time


started = time.monotonic_ns()
for line in sys.stdin.buffer:
    elapsed_ms = (time.monotonic_ns() - started) // 1_000_000
    hours, remainder = divmod(elapsed_ms, 3_600_000)
    minutes, remainder = divmod(remainder, 60_000)
    seconds, milliseconds = divmod(remainder, 1_000)
    prefix = f"[+{hours:02d}:{minutes:02d}:{seconds:02d}.{milliseconds:03d}] ".encode()
    sys.stdout.buffer.write(prefix)
    sys.stdout.buffer.write(line)
    sys.stdout.buffer.flush()

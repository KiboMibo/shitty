#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

import sys
from pathlib import Path


TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))

from harness import Shitty
from semantic_cases import run_case


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: semantic_adapter.py CASE STAMP")
    name = sys.argv[1]
    stamp = Path(sys.argv[2])
    try:
        with Shitty(
            columns=80,
            rows=24,
            save_lines=100,
            extra_arguments=("+boldColors",),
        ) as terminal:
            run_case(name, terminal)
    except Exception as error:
        print(f"FAIL Mosh/{name}: {error}", file=sys.stderr)
        raise
    print(f"PASS Mosh/{name}")
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

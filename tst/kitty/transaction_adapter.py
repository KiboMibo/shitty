#!/usr/bin/env python3

import sys
from pathlib import Path


TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))

from transaction_cases import CASES


def main():
    if len(sys.argv) != 3:
        raise SystemExit("usage: transaction_adapter.py CASE STAMP")
    name = sys.argv[1]
    try:
        case = CASES[name]
    except KeyError:
        raise RuntimeError(f"unknown Kitty transaction {name}") from None
    case()
    print(f"PASS Kitty transaction/{name}")
    stamp = Path(sys.argv[2])
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

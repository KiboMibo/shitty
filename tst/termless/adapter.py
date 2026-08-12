#!/usr/bin/env python3

import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent
TESTS = ROOT.parent
sys.path.insert(0, str(TESTS))
sys.path.insert(0, str(ROOT))

from cases import CASES


def load_xfails(path):
    return {
        line.strip()
        for line in Path(path).read_text().splitlines()
        if line.strip() and not line.startswith("#")
    }


def main():
    if len(sys.argv) != 4:
        raise SystemExit("usage: adapter.py CASE XFAIL STAMP")
    case, xfail_path, stamp_path = sys.argv[1:]
    catalog = {case_id for case_id, _, _ in json.loads((ROOT / "cases.json").read_text())}
    if case not in catalog or case not in CASES:
        raise SystemExit(f"unknown Termless case: {case}")

    failure = ""
    try:
        CASES[case]()
    except Exception as error:
        failure = f"{type(error).__name__}: {error}"

    expected = case in load_xfails(xfail_path)
    if failure:
        if expected:
            stamp = Path(stamp_path)
            stamp.parent.mkdir(parents=True, exist_ok=True)
            stamp.touch()
            print(f"XFAIL termless/{case}: {failure}")
            return 0
        print(f"FAIL termless/{case}: {failure}", file=sys.stderr)
        return 1
    if expected:
        print(f"XPASS termless/{case}", file=sys.stderr)
        return 1

    stamp = Path(stamp_path)
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    print(f"PASS termless/{case}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

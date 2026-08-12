#!/usr/bin/env python3

import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parent


def test_titles(path):
    source = path.read_text()
    return re.findall(r'\btest(?:\.skipIf\([^\n]*\))?\("([^"]+)"', source)


def main():
    cases = json.loads((ROOT / "cases.json").read_text())
    ids = [case[0] for case in cases]
    if len(ids) != len(set(ids)):
        print("duplicate Termless case ids")
        return 1

    sources = {
        "cross-backend.test.ts": ROOT / "upstream/tests/cross-backend.test.ts",
        "window-ops-probes.test.ts": ROOT / "upstream/tests/window-ops-probes.test.ts",
        "color-scheme-probes.test.ts": ROOT / "upstream/packages/vterm/tests/color-scheme-probes.test.ts",
    }
    available = {name: set(test_titles(path)) for name, path in sources.items()}
    failures = []
    for case_id, title, source in cases:
        if source not in available:
            failures.append(f"{case_id}: unknown source {source}")
        elif title not in available[source]:
            failures.append(f"{case_id}: title absent from {source}: {title!r}")
    xfails = {
        line.strip()
        for line in (ROOT / "xfail.txt").read_text().splitlines()
        if line.strip() and not line.startswith("#")
    }
    unknown = xfails - set(ids)
    if unknown:
        failures.append("unknown XFAIL cases: " + ", ".join(sorted(unknown)))
    if failures:
        print("\n".join(failures))
        return 1
    print(f"PASS termless/catalog ({len(cases)} cases, all offline)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

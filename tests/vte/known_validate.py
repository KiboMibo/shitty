#!/usr/bin/env python3

import hashlib
from pathlib import Path

from known_cases import CASES, FILES, KINDS


EXPECTED = {
    "escape": (48, 20, "a1a7767612f26e1219ad289b410a5c91b159e91d2bfb45c1aeb412cef0cbba37"),
    "csi": (204, 77, "78335c4fd2fd1d119d0daef128e812dad75080017268cdf9b3e6a7a2ea3eba32"),
    "dcs": (22, 3, "bd519fd630d1f214ed0f3017b6c34d7bfacc7f1919361e09e29d8d808b86a389"),
}


def main():
    manifest = tuple(
        line.strip()
        for line in (Path(__file__).with_name("known_file_names.txt"))
        .read_text().splitlines()
        if line.strip()
    )
    if manifest != KINDS:
        raise SystemExit(f"known manifest mismatch: {manifest!r}")
    signatures = set()
    for kind in KINDS:
        cases = CASES[kind]
        count, active, digest = EXPECTED[kind]
        if len(cases) != count:
            raise SystemExit(
                f"{kind}: expected {count} rows, found {len(cases)}"
            )
        if sum(not case.nop for case in cases) != active:
            raise SystemExit(
                f"{kind}: expected {active} active commands, found "
                f"{sum(not case.nop for case in cases)}"
            )
        actual_digest = hashlib.sha256(FILES[kind].read_bytes()).hexdigest()
        if actual_digest != digest:
            raise SystemExit(
                f"{kind}: upstream digest {actual_digest} != {digest}"
            )
        for case in cases:
            signature = (case.kind, case.event[1])
            if signature in signatures:
                raise SystemExit(
                    f"duplicate known signature {case.command}: {signature!r}"
                )
            signatures.add(signature)
    print(
        "PASS VTE known catalog: "
        f"{sum(len(CASES[kind]) for kind in KINDS)} commands, "
        f"{sum(case.nop for kind in KINDS for case in CASES[kind])} NOP"
    )


if __name__ == "__main__":
    main()

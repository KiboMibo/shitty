#!/usr/bin/env python3

import json
import sys
from pathlib import Path

from corpus import color_style_count, verify_snapshot_contract
from zstd_codec import decompress


ROOT = Path(__file__).resolve().parent


def main():
    names = (ROOT / "file_names.txt").read_text().split()
    if len(names) != 282:
        raise SystemExit(f"expected 282 real-world cases, found {len(names)}")
    if len(names) != len(set(names)):
        raise SystemExit("duplicate real-world case names")
    manifest = json.loads((ROOT / "cases.json").read_text())
    if set(names) != set(manifest):
        missing = set(manifest) - set(names)
        extra = set(names) - set(manifest)
        raise SystemExit(
            f"real-world manifest mismatch: unlisted={sorted(missing)}, "
            f"unknown={sorted(extra)}"
        )
    expected_inputs = {f"{name}.input.zst" for name in names}
    expected_screens = {f"{name}.screen.json" for name in names}
    actual_inputs = {path.name for path in (ROOT / "input").glob("*.input.zst")}
    actual_screens = {path.name for path in (ROOT / "screen").glob("*.screen.json")}
    if actual_inputs != expected_inputs:
        raise SystemExit(
            f"real-world input files mismatch: expected={sorted(expected_inputs)}, "
            f"actual={sorted(actual_inputs)}"
        )
    if actual_screens != expected_screens:
        raise SystemExit(
            f"real-world screen files mismatch: expected={sorted(expected_screens)}, "
            f"actual={sorted(actual_screens)}"
        )
    contracted_cases = 0
    cases_with_three_colors = 0
    cases_with_five_colors = 0
    for name in names:
        case = manifest[name]
        for field in ("columns", "rows", "save_lines"):
            if not isinstance(case.get(field), int) or case[field] < 0:
                raise SystemExit(f"{name}: invalid {field}")
        if case.get("terminal_environment") != {
            "TERM": "xterm-256color",
            "COLORTERM": "truecolor",
            "LANG": "C.UTF-8",
        }:
            raise SystemExit(f"{name}: invalid terminal environment")
        trace = decompress(
            (ROOT / "input" / f"{name}.input.zst").read_bytes())
        if not trace:
            raise SystemExit(f"{name}: empty PTY trace")
        screen = json.loads(
            (ROOT / "screen" / f"{name}.screen.json").read_text())
        if screen.get("format") != 1:
            raise SystemExit(f"{name}: unsupported screen format")
        if screen.get("geometry") != [case["columns"], case["rows"]]:
            raise SystemExit(f"{name}: screen geometry differs from manifest")
        try:
            verify_snapshot_contract(
                screen,
                case.get("expected_text", ()),
                case.get("minimum_color_styles", 0),
            )
        except ValueError as error:
            raise SystemExit(f"{name}: {error}") from error
        contracted_cases += int(bool(case.get("expected_text")))
        styles = color_style_count(screen)
        cases_with_three_colors += int(styles >= 3)
        cases_with_five_colors += int(styles >= 5)
    if contracted_cases < 25:
        raise SystemExit(
            f"expected at least 25 rich application contracts, "
            f"found {contracted_cases}"
        )
    if cases_with_three_colors < 160 or cases_with_five_colors < 100:
        raise SystemExit(
            "real-world color coverage is too small: "
            f">=3 styles in {cases_with_three_colors}, "
            f">=5 styles in {cases_with_five_colors}"
        )
    print(f"validated {len(names)} real-world cases")


if __name__ == "__main__":
    sys.exit(main())

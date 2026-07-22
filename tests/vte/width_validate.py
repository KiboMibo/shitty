#!/usr/bin/env python3

from pathlib import Path

from width_catalog import case_names, width_cases


def main():
    root = Path(__file__).resolve().parent
    expected = case_names()
    manifest = tuple((root / "width_file_names.txt").read_text().split())
    if manifest != expected:
        raise RuntimeError("width_file_names.txt does not match VTE width tests")
    if len(set(manifest)) != len(manifest):
        raise RuntimeError("duplicate VTE width case name")
    known = {
        line.strip() for line in (root / "width_xfail.txt").read_text().splitlines()
        if line.strip() and not line.startswith("#")
    }
    unknown = known - set(manifest)
    if unknown:
        raise RuntimeError("unknown VTE width XFAIL: "
                           + ", ".join(sorted(unknown)))
    vectors = sum(len(case) for _name, case in width_cases())
    print(f"PASS VTE width catalog: {len(manifest)} cases, {vectors} codepoints, "
          f"{len(known)} XFAIL")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

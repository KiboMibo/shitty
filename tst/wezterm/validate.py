#!/usr/bin/env python3

from pathlib import Path

from catalog import case_names


def main():
    root = Path(__file__).resolve().parent
    expected = case_names()
    manifest = tuple((root / "file_names.txt").read_text().split())
    if manifest != expected:
        raise RuntimeError("file_names.txt does not match upstream WezTerm tests")
    if len(set(manifest)) != len(manifest):
        raise RuntimeError("duplicate WezTerm case name")
    known = {
        line.strip() for line in (root / "xfail.txt").read_text().splitlines()
        if line.strip() and not line.startswith("#")
    }
    unknown = known - set(manifest)
    if unknown:
        raise RuntimeError("unknown WezTerm XFAIL: " + ", ".join(sorted(unknown)))
    print(f"PASS WezTerm catalog: {len(manifest)} cases, {len(known)} XFAIL")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

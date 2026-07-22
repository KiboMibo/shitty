#!/usr/bin/env python3

from pathlib import Path

from screen_catalog import case_names


def main():
    root = Path(__file__).resolve().parent
    expected = case_names()
    manifest = tuple((root / "screen_file_names.txt").read_text().split())
    if manifest != expected:
        raise RuntimeError("screen_file_names.txt does not match WezTerm assertions")
    if len(set(manifest)) != len(manifest):
        raise RuntimeError("duplicate WezTerm screen case name")
    known = {
        line.strip() for line in (root / "screen_xfail.txt").read_text().splitlines()
        if line.strip() and not line.startswith("#")
    }
    unknown = known - set(manifest)
    if unknown:
        raise RuntimeError("unknown WezTerm screen XFAIL: "
                           + ", ".join(sorted(unknown)))
    print(f"PASS WezTerm screen catalog: {len(manifest)} cases, "
          f"{len(known)} XFAIL")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

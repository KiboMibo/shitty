#!/usr/bin/env python3

from pathlib import Path

from screen_catalog import case_names


def main():
    root = Path(__file__).resolve().parent
    expected = case_names()
    manifest = tuple((root / "screen_file_names.txt").read_text().split())
    if manifest != expected:
        raise RuntimeError("screen_file_names.txt does not match Kitty assertions")
    if len(set(manifest)) != len(manifest):
        raise RuntimeError("duplicate Kitty screen case name")
    known = {
        line.strip() for line in (root / "screen_xfail.txt").read_text().splitlines()
        if line.strip() and not line.startswith("#")
    }
    unknown = known - set(manifest)
    if unknown:
        raise RuntimeError("unknown Kitty screen XFAIL: "
                           + ", ".join(sorted(unknown)))
    print(f"PASS Kitty screen catalog: {len(manifest)} cases, {len(known)} XFAIL")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

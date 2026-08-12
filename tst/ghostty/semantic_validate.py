#!/usr/bin/env python3

from pathlib import Path

from semantic_catalog import case_names


def main():
    root = Path(__file__).resolve().parent
    expected = case_names()
    manifest = tuple((root / "semantic_file_names.txt").read_text().split())
    if manifest != expected:
        raise RuntimeError("semantic_file_names.txt does not match Ghostty tests")
    if len(set(manifest)) != len(manifest):
        raise RuntimeError("duplicate Ghostty semantic case name")
    known = {
        line.strip() for line in (root / "semantic_xfail.txt").read_text().splitlines()
        if line.strip() and not line.startswith("#")
    }
    unknown = known - set(manifest)
    if unknown:
        raise RuntimeError("unknown Ghostty semantic XFAIL: "
                           + ", ".join(sorted(unknown)))
    print(f"PASS Ghostty semantic catalog: {len(manifest)} cases, "
          f"{len(known)} XFAIL")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

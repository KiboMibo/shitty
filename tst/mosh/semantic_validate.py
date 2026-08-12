#!/usr/bin/env python3

# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

from pathlib import Path

from semantic_cases import case_names


def main():
    root = Path(__file__).resolve().parent
    expected = case_names()
    manifest = tuple((root / "semantic_file_names.txt").read_text().split())
    if manifest != expected:
        raise RuntimeError("semantic_file_names.txt does not match Mosh cases")
    if len(set(manifest)) != len(manifest):
        raise RuntimeError("duplicate Mosh semantic case")
    print(f"PASS Mosh semantic catalog: {len(manifest)} cases")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

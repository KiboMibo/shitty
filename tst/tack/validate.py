#!/usr/bin/env python3

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parent
BLOCK = re.compile(
    r"(?:static\s+)?TestList\s+\w+\s*\[.*?\]\s*=\s*\{(.*?)\n\};",
    re.DOTALL,
)
ITEM = re.compile(
    r'\{\s*[^,\n]+,\s*[^,\n]+,\s*'
    r'(?:(?:"([^"]*)")|NULL),\s*'
    r'(?:(?:"([^"]*)")|NULL),',
)


def upstream_capabilities():
    result = set()
    for source in (ROOT / "upstream").glob("*.c"):
        for block in BLOCK.findall(source.read_text()):
            for completed, tested in ITEM.findall(block):
                for names in (completed, tested):
                    result.update(re.findall(r"[^ ()]+", names))
    return result


def main():
    manifest = set((ROOT / "file_names.txt").read_text().split())
    upstream = upstream_capabilities()
    if manifest != upstream:
        missing = sorted(upstream - manifest)
        stale = sorted(manifest - upstream)
        if missing:
            print("missing capabilities: " + ", ".join(missing))
        if stale:
            print("stale capabilities: " + ", ".join(stale))
        return 1
    print(f"PASS tack/catalog ({len(manifest)} capabilities)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

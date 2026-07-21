#!/usr/bin/env python3

from pathlib import Path

from catalog import all_categories, category_cases


def main():
    root = Path(__file__).resolve().parent
    known = {
        line.strip() for line in (root / "xfail.txt").read_text().splitlines()
        if line.strip() and not line.startswith("#")
    }
    identifiers = set()
    counts = {}
    for category in all_categories():
        cases = list(category_cases(category))
        counts[category] = len(cases)
        for identifier, _, _ in cases:
            if identifier in identifiers:
                raise RuntimeError(f"duplicate case identifier: {identifier}")
            identifiers.add(identifier)

    unknown = known - identifiers
    if unknown:
        raise RuntimeError("unknown XFAIL: " + ", ".join(sorted(unknown)))

    covered = {category: 0 for category in counts}
    for line in (root / "shards.txt").read_text().splitlines():
        category, start, end = line.split()
        start, end = int(start), int(end)
        if category not in counts or start != covered[category] or end <= start:
            raise RuntimeError(f"invalid shard: {line}")
        covered[category] = end
    if covered != counts:
        raise RuntimeError(f"incomplete shards: covered={covered}, cases={counts}")

    print(
        f"PASS ucs-detect catalog: {sum(counts.values())} cases, "
        f"{len(known)} XFAIL"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

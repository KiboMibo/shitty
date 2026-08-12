#!/usr/bin/env python3

from pathlib import Path

from catalog import all_categories, category_cases
from probe_cases import case_names


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

    probe_names = (root / "probe_names.txt").read_text().split()
    expected_probe_names = list(case_names())
    if probe_names != expected_probe_names:
        raise RuntimeError("probe_names.txt does not match probe_cases.py")
    known_probes = {
        line.strip()
        for line in (root / "probe_xfail.txt").read_text().splitlines()
        if line.strip() and not line.startswith("#")
    }
    unknown_probes = known_probes - set(probe_names)
    if unknown_probes:
        raise RuntimeError(
            "unknown probe XFAIL: " + ", ".join(sorted(unknown_probes))
        )

    print(
        f"PASS ucs-detect catalog: {sum(counts.values())} cases, "
        f"{len(known)} width XFAIL, {len(probe_names)} probes, "
        f"{len(known_probes)} probe XFAIL"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

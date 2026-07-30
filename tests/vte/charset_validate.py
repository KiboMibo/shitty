#!/usr/bin/env python3

from pathlib import Path

from charset_cases import CASE_NAMES, cases


EXPECTED_COUNTS = {
    "94": 2528,
    "96": 1659,
    "94_n": 2531,
    "96_n": 2133,
    "control": 158,
    "other": 237,
}


def main():
    root = Path(__file__).resolve().parent
    source = (root / "upstream" / "parser-test.cc").read_text()
    tables = (root / "upstream" / "parser-charset-tables.hh").read_text()
    markers = {
        "94": "test_seq_esc_charset_94(void)",
        "96": "test_seq_esc_charset_96(void)",
        "94_n": "test_seq_esc_charset_94_n(void)",
        "96_n": "test_seq_esc_charset_96_n(void)",
        "control": "test_seq_esc_charset_control(void)",
        "other": "test_seq_esc_charset_other(void)",
    }
    missing = [marker for marker in markers.values() if marker not in source]
    if missing:
        raise RuntimeError(
            "missing upstream VTE charset markers: " + ", ".join(missing)
        )
    table_markers = (
        "charset_graphic_94[]",
        "charset_graphic_96[]",
        "charset_graphic_94_n[]",
        "charset_control_c0[]",
        "charset_control_c1[]",
        "charset_ocs[]",
    )
    missing_tables = [marker for marker in table_markers if marker not in tables]
    if missing_tables:
        raise RuntimeError(
            "missing upstream VTE charset tables: " + ", ".join(missing_tables)
        )

    manifest = tuple((root / "charset_file_names.txt").read_text().split())
    if manifest != CASE_NAMES:
        raise RuntimeError("charset_file_names.txt does not match catalog")
    counts = {name: sum(1 for _ in cases(name)) for name in CASE_NAMES}
    if counts != EXPECTED_COUNTS:
        raise RuntimeError(f"VTE charset counts changed: {counts!r}")
    print(
        f"PASS VTE charset catalog: {sum(counts.values())} designators "
        f"in {len(counts)} families"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

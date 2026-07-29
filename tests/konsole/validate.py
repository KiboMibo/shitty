#!/usr/bin/env python3

from pathlib import Path

from catalog import case_names, parser_cases


def main():
    root = Path(__file__).resolve().parent
    expected = case_names()
    manifest = tuple((root / "file_names.txt").read_text().split())
    if manifest != expected:
        raise RuntimeError("file_names.txt does not match upstream tokenizer tests")
    if len(set(manifest)) != len(manifest):
        raise RuntimeError("duplicate Konsole case name")
    cases = tuple(parser_cases())
    if len(cases) != 146:
        raise RuntimeError(
            f"expected 146 Konsole tokenizer rows, found {len(cases)}"
        )
    recognized = {
        "ctl", "esc", "esc_cs", "esc_de", "vt52",
        "csi_ps", "csi_pn", "csi_pr", "csi_pg", "csi_pe",
        "csi_sp", "csi_psp", "csi_pq",
    }
    unknown = {
        kind
        for _name, _label, _mode, _payload, tokens in cases
        for kind, _arguments, _p, _q in tokens
        if kind not in recognized
    }
    if unknown:
        raise RuntimeError(
            "unknown Konsole token constructors: "
            + ", ".join(sorted(unknown))
        )
    known = {
        line.strip() for line in (root / "xfail.txt").read_text().splitlines()
        if line.strip() and not line.startswith("#")
    }
    unknown_xfails = known - set(manifest)
    if unknown_xfails:
        raise RuntimeError(
            "unknown Konsole XFAIL: " + ", ".join(sorted(unknown_xfails))
        )
    print(f"PASS Konsole catalog: {len(manifest)} cases, {len(known)} XFAIL")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

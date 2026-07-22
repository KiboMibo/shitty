#!/usr/bin/env python3

from pathlib import Path

from catalog import case_names


def main():
    root = Path(__file__).resolve().parent
    source = (root / "upstream" / "parser-test.cc").read_text()
    required = (
        "test_seq_esc_nF", "test_seq_esc_Fpes", "test_seq_csi(void)",
        "test_seq_csi_param(void)", "test_seq_csi_max(void)",
        "test_seq_csi_misc(void)", "test_seq_dcs(void)",
        "test_seq_dcs_misc(void)", "test_seq_osc(void)",
    )
    missing = [marker for marker in required if marker not in source]
    if missing:
        raise RuntimeError("missing upstream VTE source markers: " + ", ".join(missing))
    manifest = tuple((root / "file_names.txt").read_text().split())
    if manifest != case_names():
        raise RuntimeError("file_names.txt does not match VTE catalog")
    known = {
        line.strip() for line in (root / "xfail.txt").read_text().splitlines()
        if line.strip() and not line.startswith("#")
    }
    unknown = known - set(manifest)
    if unknown:
        raise RuntimeError("unknown VTE XFAIL: " + ", ".join(sorted(unknown)))
    print(f"PASS VTE catalog: {len(manifest)} targets, {len(known)} XFAIL")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

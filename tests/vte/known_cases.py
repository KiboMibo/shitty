#!/usr/bin/env python3

import ast
import re
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parent
UPSTREAM = ROOT / "upstream"
KINDS = ("escape", "csi", "dcs")
FILES = {
    "escape": UPSTREAM / "parser-esc.hh",
    "csi": UPSTREAM / "parser-csi.hh",
    "dcs": UPSTREAM / "parser-dcs.hh",
}
PREFIXES = {
    "NONE": b"",
    "EQUAL": b"=",
    "GT": b">",
    "WHAT": b"?",
}
INTERMEDIATES = {
    "NONE": b"",
    "SPACE": b" ",
    "BANG": b"!",
    "DQUOTE": b'"',
    "HASH": b"#",
    "CASH": b"$",
    "PERCENT": b"%",
    "AND": b"&",
    "SQUOTE": b"'",
    "PCLOSE": b")",
    "MULT": b"*",
    "PLUS": b"+",
    "COMMA": b",",
    "MINUS": b"-",
}
ROW = re.compile(
    r"^_VTE_(SEQ|NOQ)\("
    r"([^,]+),\s*([^,]+),\s*([^,]+),\s*([^,]+),\s*"
    r"(\d+),\s*([^,]+),\s*([^)]+)\s*\)"
)


@dataclass(frozen=True)
class KnownSequence:
    command: str
    kind: str
    sequence: bytes
    event: tuple[str, bytes]
    nop: bool


def parse_final(value):
    value = value.strip()
    if value.startswith("'"):
        decoded = ast.literal_eval(value)
        if len(decoded) != 1:
            raise ValueError(f"invalid final {value!r}")
        return ord(decoded)
    return int(value, 0)


def parse_file(kind):
    result = []
    for number, line in enumerate(FILES[kind].read_text().splitlines(), 1):
        match = ROW.match(line)
        if match is None:
            if line.startswith("_VTE_"):
                raise ValueError(f"{FILES[kind]}:{number}: malformed row")
            continue
        macro, command, source_kind, final, prefix, count, intermediate, _ = (
            value.strip() for value in match.groups()
        )
        expected_source_kind = "ESCAPE" if kind == "escape" else kind.upper()
        if source_kind != expected_source_kind:
            raise ValueError(
                f"{FILES[kind]}:{number}: {source_kind} != "
                f"{expected_source_kind}"
            )
        prefix_bytes = PREFIXES[prefix]
        intermediate_bytes = INTERMEDIATES[intermediate]
        if int(count) != len(intermediate_bytes):
            raise ValueError(
                f"{FILES[kind]}:{number}: intermediate count mismatch"
            )
        payload = prefix_bytes + intermediate_bytes + bytes((parse_final(final),))
        if kind == "escape":
            sequence = b"\x1b" + payload
        elif kind == "csi":
            sequence = b"\x1b[" + payload
        else:
            sequence = b"\x1bP" + payload + b"\x1b\\"
        result.append(KnownSequence(
            command=command,
            kind=kind,
            sequence=sequence,
            event=(kind, payload),
            nop=macro == "NOQ",
        ))
    return tuple(result)


CASES = {kind: parse_file(kind) for kind in KINDS}

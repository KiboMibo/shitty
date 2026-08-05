#!/usr/bin/env python3
# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

"""Update and generate the built-in terminal color scheme catalog.

The normalized terminal_colors.json is the source of truth for the C++
table. `update` imports the Windows Terminal exports from the
iTerm2-Color-Schemes repository, while `generate OUTPUT` is the offline,
deterministic build-time code generator.
"""

import argparse
import io
import json
import subprocess
import urllib.request
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent
CATALOG = ROOT / "terminal_colors.json"
UPSTREAM_LICENSE = ROOT / "LICENSE.iTerm2-Color-Schemes"
UPSTREAM_REPOSITORY = "https://github.com/mbadolato/iTerm2-Color-Schemes"
UPSTREAM_API = "https://api.github.com/repos/mbadolato/iTerm2-Color-Schemes"
ANSI_KEYS = (
    "black", "red", "green", "yellow",
    "blue", "purple", "cyan", "white",
    "brightBlack", "brightRed", "brightGreen", "brightYellow",
    "brightBlue", "brightPurple", "brightCyan", "brightWhite",
)


def request_bytes(url):
    request = urllib.request.Request(
        url,
        headers={"User-Agent": "shitty-terminal-colors-updater"},
    )
    with urllib.request.urlopen(request) as response:
        return response.read()


def normalize_color(value, context):
    if not isinstance(value, str):
        raise ValueError(f"{context}: color must be a string")
    color = value.lower()
    if len(color) != 7 or color[0] != "#":
        raise ValueError(f"{context}: expected #rrggbb, got {value!r}")
    try:
        int(color[1:], 16)
    except ValueError as error:
        raise ValueError(f"{context}: expected #rrggbb, got {value!r}") from error
    return color


def normalize_scheme(raw, context):
    name = raw.get("name")
    if not isinstance(name, str) or not name or "\0" in name:
        raise ValueError(f"{context}: invalid scheme name {name!r}")
    return {
        "name": name,
        "foreground": normalize_color(raw.get("foreground"), f"{context}: foreground"),
        "background": normalize_color(raw.get("background"), f"{context}: background"),
        "ansi": [
            normalize_color(raw.get(key), f"{context}: {key}")
            for key in ANSI_KEYS
        ],
    }


def validate_schemes(schemes):
    if not isinstance(schemes, list):
        raise ValueError("schemes must be a list")
    normalized = []
    names = set()
    for index, raw in enumerate(schemes):
        if not isinstance(raw, dict):
            raise ValueError(f"scheme {index}: expected an object")
        scheme = {
            "name": raw.get("name"),
            "foreground": normalize_color(
                raw.get("foreground"), f"scheme {index}: foreground"
            ),
            "background": normalize_color(
                raw.get("background"), f"scheme {index}: background"
            ),
            "ansi": [
                normalize_color(color, f"scheme {index}: ansi[{color_index}]")
                for color_index, color in enumerate(raw.get("ansi", []))
            ],
        }
        name = scheme["name"]
        if not isinstance(name, str) or not name or "\0" in name:
            raise ValueError(f"scheme {index}: invalid name {name!r}")
        if len(scheme["ansi"]) != 16:
            raise ValueError(f"{name}: expected exactly 16 ANSI colors")
        folded = name.casefold()
        if folded in names:
            raise ValueError(f"duplicate scheme name ignoring case: {name}")
        names.add(folded)
        normalized.append(scheme)
    expected = sorted(normalized, key=lambda scheme: (scheme["name"].casefold(), scheme["name"]))
    if normalized != expected:
        raise ValueError("schemes are not sorted by name")
    return normalized


def load_catalog(path=CATALOG):
    catalog = json.loads(Path(path).read_text())
    if catalog.get("schema") != 1:
        raise ValueError("unsupported terminal_colors.json schema")
    source = catalog.get("source")
    if not isinstance(source, dict):
        raise ValueError("catalog source metadata is missing")
    for key in ("repository", "revision", "license"):
        if not isinstance(source.get(key), str) or not source[key]:
            raise ValueError(f"catalog source.{key} is missing")
    validate_schemes(catalog.get("schemes"))
    return catalog


def color_initializer(color):
    value = int(color[1:], 16)
    return f"{{0x{value >> 16:02x}, 0x{(value >> 8) & 0xff:02x}, 0x{value & 0xff:02x}}}"


def cxx_string(value):
    encoded = value.encode("utf-8")
    parts = []
    for byte in encoded:
        if 0x20 <= byte <= 0x7e and byte not in (ord('"'), ord('\\')):
            parts.append(chr(byte))
        elif byte == ord('"'):
            parts.append('\\"')
        elif byte == ord('\\'):
            parts.append('\\\\')
        else:
            parts.append(f"\\{byte:03o}")
    return '"' + "".join(parts) + '"'


def generate_header(catalog):
    schemes = validate_schemes(catalog["schemes"])
    revision = catalog["source"]["revision"]
    lines = [
        "/* Generated by terminal_colors.py from terminal_colors.json; do not edit. */",
        f"/* iTerm2-Color-Schemes revision {revision}. */",
        "",
        "static constexpr TerminalColorScheme terminalColorSchemes[] = {",
    ]
    for scheme in schemes:
        lines.extend([
            "    {",
            f"        {cxx_string(scheme['name'])},",
            f"        {color_initializer(scheme['foreground'])},",
            f"        {color_initializer(scheme['background'])},",
            "        {",
        ])
        lines.extend(
            f"            {color_initializer(color)},"
            for color in scheme["ansi"]
        )
        lines.extend(["        },", "    },"])
    lines.extend(["};", ""])
    return "\n".join(lines)


def catalog_text(revision, schemes):
    catalog = {
        "schema": 1,
        "source": {
            "repository": UPSTREAM_REPOSITORY,
            "revision": revision,
            "license": "MIT",
        },
        "schemes": sorted(
            schemes,
            key=lambda scheme: (scheme["name"].casefold(), scheme["name"]),
        ),
    }
    validate_schemes(catalog["schemes"])
    return json.dumps(catalog, indent=2, ensure_ascii=False) + "\n"


def import_checkout(path):
    root = Path(path)
    revision = subprocess.run(
        ["git", "-C", root, "rev-parse", "HEAD"],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    schemes = []
    for source in sorted((root / "windowsterminal").glob("*.json")):
        schemes.append(normalize_scheme(json.loads(source.read_text()), str(source)))
    if not schemes:
        raise ValueError(f"no Windows Terminal schemes found under {root}")
    return revision, schemes, (root / "LICENSE").read_text()


def import_remote():
    revision_data = json.loads(request_bytes(f"{UPSTREAM_API}/commits/master"))
    revision = revision_data["sha"]
    archive_data = request_bytes(
        f"{UPSTREAM_REPOSITORY}/archive/{revision}.zip"
    )
    schemes = []
    license_text = None
    with zipfile.ZipFile(io.BytesIO(archive_data)) as archive:
        for member in sorted(archive.namelist()):
            relative = member.split("/", 1)[-1]
            if relative == "LICENSE":
                license_text = archive.read(member).decode()
            if not relative.startswith("windowsterminal/") or not relative.endswith(".json"):
                continue
            raw = json.loads(archive.read(member))
            schemes.append(normalize_scheme(raw, relative))
    if not schemes or license_text is None:
        raise ValueError("downloaded upstream archive is incomplete")
    return revision, schemes, license_text


def update(source=None):
    revision, schemes, license_text = (
        import_checkout(source) if source else import_remote()
    )
    CATALOG.write_text(catalog_text(revision, schemes))
    normalized_license = "\n".join(
        line.rstrip() for line in license_text.splitlines()
    ) + "\n"
    UPSTREAM_LICENSE.write_text(normalized_license)
    print(f"imported {len(schemes)} schemes from {revision}")


def generate(output):
    Path(output).write_text(generate_header(load_catalog()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    update_parser = subparsers.add_parser("update")
    update_parser.add_argument(
        "--source",
        help="import an existing iTerm2-Color-Schemes checkout instead of downloading",
    )
    generate_parser = subparsers.add_parser("generate")
    generate_parser.add_argument("output")
    arguments = parser.parse_args()
    if arguments.command == "update":
        update(arguments.source)
    else:
        generate(arguments.output)


if __name__ == "__main__":
    main()

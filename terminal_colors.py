#!/usr/bin/env python3
# Copyright (C) 2026 Shitty team
# MIT licensed
# See the file LICENSE.MIT for the full license.

"""Update and generate the built-in terminal color scheme catalog.

The normalized terminal_colors.json is the source of truth for the C++
table. `update` imports several upstream collections and records their exact
revisions, while `generate OUTPUT` is the offline, deterministic build-time
code generator.
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
ANSI_KEYS = (
    "black", "red", "green", "yellow",
    "blue", "purple", "cyan", "white",
    "brightBlack", "brightRed", "brightGreen", "brightYellow",
    "brightBlue", "brightPurple", "brightCyan", "brightWhite",
)

ALACRITTY_PRIMARY = {
    "foreground": "#d8d8d8",
    "background": "#181818",
}
ALACRITTY_NORMAL = {
    "black": "#181818",
    "red": "#ac4242",
    "green": "#90a959",
    "yellow": "#f4bf75",
    "blue": "#6a9fb5",
    "magenta": "#aa759f",
    "cyan": "#75b5aa",
    "white": "#d8d8d8",
}
ALACRITTY_BRIGHT = {
    "black": "#6b6b6b",
    "red": "#c55555",
    "green": "#aac474",
    "yellow": "#feca88",
    "blue": "#82b8c8",
    "magenta": "#c28cb8",
    "cyan": "#93d3c3",
    "white": "#f8f8f8",
}

COLLECTIONS = (
    {
        "id": "iterm2-color-schemes",
        "repository": "https://github.com/mbadolato/iTerm2-Color-Schemes",
        "api": "https://api.github.com/repos/mbadolato/iTerm2-Color-Schemes",
        "branch": "master",
        "license": "MIT",
        "licenseFile": "ext/LICENSE.iTerm2-Color-Schemes",
        "prefix": "",
    },
    {
        "id": "gogh",
        "repository": "https://github.com/Gogh-Co/Gogh",
        "api": "https://api.github.com/repos/Gogh-Co/Gogh",
        "branch": "master",
        "license": "MIT",
        "licenseFile": "ext/LICENSE.Gogh",
        "prefix": "Gogh: ",
    },
    {
        "id": "alacritty-theme",
        "repository": "https://github.com/alacritty/alacritty-theme",
        "api": "https://api.github.com/repos/alacritty/alacritty-theme",
        "branch": "master",
        "license": "Apache-2.0",
        "licenseFile": "ext/LICENSE.alacritty-theme",
        "prefix": "Alacritty: ",
    },
    {
        "id": "kitty-themes",
        "repository": "https://github.com/kovidgoyal/kitty-themes",
        "api": "https://api.github.com/repos/kovidgoyal/kitty-themes",
        "branch": "master",
        "license": "GPL-3.0-only",
        "licenseFile": "ext/LICENSE.kitty-themes",
        "prefix": "Kitty: ",
    },
    {
        "id": "terminal-sexy",
        "repository": "https://github.com/stayradiated/terminal.sexy",
        "api": "https://api.github.com/repos/stayradiated/terminal.sexy",
        "branch": "master",
        "license": "MIT",
        "licenseFile": "ext/LICENSE.terminal-sexy",
        "prefix": "terminal.sexy: ",
    },
)


def terminal_source(source_id, repository, revision, license_name, *files):
    return {
        "id": source_id,
        "kind": "terminal-default",
        "repository": repository,
        "revision": revision,
        "license": license_name,
        "files": list(files),
    }


# These are intentionally curated separately from the theme collections above.
# Every entry is the default palette in the cited terminal revision.
TERMINAL_DEFAULT_SOURCES = (
    terminal_source(
        "alacritty",
        "https://github.com/alacritty/alacritty",
        "1b2b36a64e88068ad02c95fad00ee2fad31c00bf",
        "Apache-2.0",
        "alacritty/src/config/color.rs",
    ),
    terminal_source(
        "foot",
        "https://codeberg.org/dnkl/foot",
        "8db88cceb758b5be23e7db1fe74a48102ab07dc0",
        "MIT",
        "config.c",
    ),
    terminal_source(
        "ghostty",
        "https://github.com/ghostty-org/ghostty",
        "5944ab286d825b48b68d0e99088273cf435d6870",
        "MIT",
        "src/config/Config.zig",
        "src/terminal/color.zig",
    ),
    terminal_source(
        "gnome-terminal",
        "https://gitlab.gnome.org/GNOME/gnome-terminal",
        "5ba7dfb204848043cbdc5a8a489ee025dc55477f",
        "GPL-3.0-or-later",
        "src/org.gnome.Terminal.gschema.xml",
    ),
    terminal_source(
        "kitty",
        "https://github.com/kovidgoyal/kitty",
        "51b0993b5cc90b0fc353bc9d8082f874fedcc681",
        "GPL-3.0-only",
        "kitty/options/definition.py",
    ),
    terminal_source(
        "konsole",
        "https://invent.kde.org/utilities/konsole",
        "7e2fbead2f57771af66ad51aed37e92aa3f665cf",
        "GPL-2.0-or-later",
        "data/color-schemes/Breeze.colorscheme",
    ),
    terminal_source(
        "wezterm",
        "https://github.com/wezterm/wezterm",
        "4b1c3c151eb530e569f867e1461693c56fe89695",
        "MIT",
        "term/src/color.rs",
    ),
    terminal_source(
        "windows-terminal",
        "https://github.com/microsoft/terminal",
        "e74649d5f18b1c123556461b30f763407aea558f",
        "MIT",
        "src/cascadia/TerminalSettingsModel/defaults.json",
    ),
)


def scheme(name, source, foreground, background, ansi):
    return {
        "name": name,
        "source": source,
        "foreground": foreground,
        "background": background,
        "ansi": ansi,
    }


TERMINAL_DEFAULT_SCHEMES = (
    scheme(
        "Alacritty", "alacritty", "#d8d8d8", "#181818",
        (
            "#181818", "#ac4242", "#90a959", "#f4bf75",
            "#6a9fb5", "#aa759f", "#75b5aa", "#d8d8d8",
            "#6b6b6b", "#c55555", "#aac474", "#feca88",
            "#82b8c8", "#c28cb8", "#93d3c3", "#f8f8f8",
        ),
    ),
    scheme(
        "foot", "foot", "#ffffff", "#242424",
        (
            "#242424", "#f62b5a", "#47b413", "#e3c401",
            "#24acd4", "#f2affd", "#13c299", "#e6e6e6",
            "#616161", "#ff4d51", "#35d450", "#e9e836",
            "#5dc5f8", "#feabf2", "#24dfc4", "#ffffff",
        ),
    ),
    scheme(
        "Ghostty", "ghostty", "#ffffff", "#282c34",
        (
            "#1d1f21", "#cc6666", "#b5bd68", "#f0c674",
            "#81a2be", "#b294bb", "#8abeb7", "#c5c8c6",
            "#666666", "#d54e53", "#b9ca4a", "#e7c547",
            "#7aa6da", "#c397d8", "#70c0b1", "#eaeaea",
        ),
    ),
    scheme(
        "GNOME Terminal", "gnome-terminal", "#1e1e1e", "#ffffff",
        (
            "#1e1e1e", "#c01c28", "#26a269", "#a2734c",
            "#12488b", "#a347ba", "#2aa1b3", "#cfcfcf",
            "#5d5d5d", "#f66151", "#33d17a", "#e9ad0c",
            "#2a7bde", "#c061cb", "#33c7de", "#ffffff",
        ),
    ),
    scheme(
        "kitty", "kitty", "#dddddd", "#000000",
        (
            "#000000", "#cc0403", "#19cb00", "#cecb00",
            "#0d73cc", "#cb1ed1", "#0dcdcd", "#dddddd",
            "#767676", "#f2201f", "#23fd00", "#fffd00",
            "#1a8fff", "#fd28ff", "#14ffff", "#ffffff",
        ),
    ),
    scheme(
        "Konsole", "konsole", "#fcfcfc", "#232627",
        (
            "#232627", "#ed1515", "#11d116", "#f67400",
            "#1d99f3", "#9b59b6", "#1abc9c", "#fcfcfc",
            "#7f8c8d", "#c0392b", "#1cdc9a", "#fdbc4b",
            "#3daee9", "#8e44ad", "#16a085", "#ffffff",
        ),
    ),
    scheme(
        "WezTerm", "wezterm", "#b2b2b2", "#000000",
        (
            "#000000", "#cc5555", "#55cc55", "#cdcd55",
            "#5455cb", "#cc55cc", "#7acaca", "#cccccc",
            "#555555", "#ff5555", "#55ff55", "#ffff55",
            "#5555ff", "#ff55ff", "#55ffff", "#ffffff",
        ),
    ),
    scheme(
        "Windows Terminal", "windows-terminal", "#cccccc", "#0c0c0c",
        (
            "#0c0c0c", "#c50f1f", "#13a10e", "#c19c00",
            "#0037da", "#881798", "#3a96dd", "#cccccc",
            "#767676", "#e74856", "#16c60c", "#f9f1a5",
            "#3b78ff", "#b4009e", "#61d6d6", "#f2f2f2",
        ),
    ),
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


def normalize_scheme(raw, context, source, name=None):
    name = raw.get("name") if name is None else name
    if not isinstance(name, str) or not name or "\0" in name:
        raise ValueError(f"{context}: invalid scheme name {name!r}")
    return {
        "name": name,
        "source": source,
        "foreground": normalize_color(raw.get("foreground"), f"{context}: foreground"),
        "background": normalize_color(raw.get("background"), f"{context}: background"),
        "ansi": [
            normalize_color(raw.get(key), f"{context}: {key}")
            for key in ANSI_KEYS
        ],
    }


def validate_sources(sources):
    if not isinstance(sources, list) or not sources:
        raise ValueError("catalog sources metadata is missing")
    source_ids = set()
    for index, source in enumerate(sources):
        if not isinstance(source, dict):
            raise ValueError(f"source {index}: expected an object")
        for key in ("id", "kind", "repository", "revision", "license"):
            if not isinstance(source.get(key), str) or not source[key]:
                raise ValueError(f"source {index}.{key} is missing")
        source_id = source["id"]
        if source_id in source_ids:
            raise ValueError(f"duplicate source id: {source_id}")
        if len(source["revision"]) != 40:
            raise ValueError(f"{source_id}: revision is not a full commit hash")
        if source["kind"] == "terminal-default":
            files = source.get("files")
            if not isinstance(files, list) or not files or not all(
                isinstance(path, str) and path for path in files
            ):
                raise ValueError(f"{source_id}: source files are missing")
        source_ids.add(source_id)
    expected = sorted(sources, key=lambda source: source["id"])
    if sources != expected:
        raise ValueError("sources are not sorted by id")
    return source_ids


def validate_schemes(schemes, source_ids=None):
    if not isinstance(schemes, list):
        raise ValueError("schemes must be a list")
    normalized = []
    names = set()
    used_sources = set()
    for index, raw in enumerate(schemes):
        if not isinstance(raw, dict):
            raise ValueError(f"scheme {index}: expected an object")
        scheme_source = raw.get("source")
        if not isinstance(scheme_source, str) or not scheme_source:
            raise ValueError(f"scheme {index}: source is missing")
        if source_ids is not None and scheme_source not in source_ids:
            raise ValueError(f"scheme {index}: unknown source {scheme_source!r}")
        scheme = {
            "name": raw.get("name"),
            "source": scheme_source,
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
        used_sources.add(scheme_source)
        normalized.append(scheme)
    expected = sorted(normalized, key=lambda item: (item["name"].casefold(), item["name"]))
    if normalized != expected:
        raise ValueError("schemes are not sorted by name")
    if source_ids is not None and used_sources != source_ids:
        unused = sorted(source_ids - used_sources)
        raise ValueError(f"sources without schemes: {', '.join(unused)}")
    return normalized


def load_catalog(path=CATALOG):
    catalog = json.loads(Path(path).read_text())
    if catalog.get("schema") != 2:
        raise ValueError("unsupported terminal_colors.json schema")
    source_ids = validate_sources(catalog.get("sources"))
    validate_schemes(catalog.get("schemes"), source_ids)
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
    source_ids = validate_sources(catalog["sources"])
    schemes = validate_schemes(catalog["schemes"], source_ids)
    lines = [
        "/* Generated by terminal_colors.py from terminal_colors.json; do not edit. */",
        f"/* {len(schemes)} schemes from {len(source_ids)} pinned sources. */",
        "",
        "static constexpr TerminalColorScheme terminalColorSchemes[] = {",
    ]
    for item in schemes:
        lines.extend([
            "    {",
            f"        {cxx_string(item['name'])},",
            f"        {color_initializer(item['foreground'])},",
            f"        {color_initializer(item['background'])},",
            "        {",
        ])
        lines.extend(
            f"            {color_initializer(color)},"
            for color in item["ansi"]
        )
        lines.extend(["        },", "    },"])
    lines.extend(["};", ""])
    return "\n".join(lines)


def catalog_text(sources, schemes):
    catalog = {
        "schema": 2,
        "sources": sorted(sources, key=lambda source: source["id"]),
        "schemes": sorted(
            schemes,
            key=lambda item: (item["name"].casefold(), item["name"]),
        ),
    }
    source_ids = validate_sources(catalog["sources"])
    validate_schemes(catalog["schemes"], source_ids)
    return json.dumps(catalog, indent=2, ensure_ascii=False) + "\n"


def checkout_revision(root):
    return subprocess.run(
        ["git", "-C", root, "rev-parse", "HEAD"],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()


def collection_source(spec, revision):
    return {
        "id": spec["id"],
        "kind": "collection",
        "repository": spec["repository"],
        "revision": revision,
        "license": spec["license"],
        "licenseFile": spec["licenseFile"],
    }


def import_iterm_files(files, spec):
    schemes = []
    for name, data in sorted(files.items()):
        if not name.startswith("windowsterminal/") or not name.endswith(".json"):
            continue
        schemes.append(normalize_scheme(json.loads(data), name, spec["id"]))
    return schemes


def import_gogh_files(files, spec):
    raw_schemes = json.loads(files["data/themes.json"])
    schemes = []
    for index, raw in enumerate(raw_schemes):
        converted = {
            "foreground": raw.get("foreground"),
            "background": raw.get("background"),
        }
        for color_index, key in enumerate(ANSI_KEYS, start=1):
            converted[key] = raw.get(f"color_{color_index:02d}")
        name = f"{spec['prefix']}{raw.get('name', '')}"
        schemes.append(normalize_scheme(
            converted, f"data/themes.json entry {index}", spec["id"], name
        ))
    return schemes


def import_alacritty_files(files, spec):
    import tomllib

    schemes = []
    for name, data in sorted(files.items()):
        if not name.startswith("themes/") or not name.endswith(".toml"):
            continue
        raw = tomllib.loads(data)
        colors = raw.get("colors", {})
        primary = colors.get("primary", {})
        normal = colors.get("normal", {})
        bright = colors.get("bright", {})
        converted = {
            "foreground": primary.get("foreground", ALACRITTY_PRIMARY["foreground"]),
            "background": primary.get("background", ALACRITTY_PRIMARY["background"]),
        }
        for key in ANSI_KEYS[:8]:
            raw_key = "magenta" if key == "purple" else key
            converted[key] = normal.get(raw_key, ALACRITTY_NORMAL[raw_key])
        for key in ANSI_KEYS[8:]:
            raw_key = key.removeprefix("bright")
            raw_key = raw_key[0].lower() + raw_key[1:]
            raw_key = "magenta" if raw_key == "purple" else raw_key
            converted[key] = bright.get(raw_key, ALACRITTY_BRIGHT[raw_key])
        stem = Path(name).stem
        schemes.append(normalize_scheme(
            converted, name, spec["id"], f"{spec['prefix']}{stem}"
        ))
    return schemes


def import_kitty_files(files, spec):
    metadata = json.loads(files["themes.json"])
    schemes = []
    defaults = next(item for item in TERMINAL_DEFAULT_SCHEMES if item["source"] == "kitty")
    for index, item in enumerate(metadata):
        path = item.get("file")
        name = item.get("name")
        if not isinstance(path, str) or path not in files:
            raise ValueError(f"themes.json entry {index}: invalid file {path!r}")
        if not isinstance(name, str) or not name:
            raise ValueError(f"themes.json entry {index}: invalid name {name!r}")
        settings = {}
        for line in files[path].splitlines():
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            fields = line.split()
            if len(fields) >= 2:
                settings[fields[0]] = fields[1]
        converted = {
            "foreground": settings.get("foreground", defaults["foreground"]),
            "background": settings.get("background", defaults["background"]),
        }
        for color_index, key in enumerate(ANSI_KEYS):
            converted[key] = settings.get(f"color{color_index}", defaults["ansi"][color_index])
        schemes.append(normalize_scheme(
            converted, path, spec["id"], f"{spec['prefix']}{name}"
        ))
    return schemes


def import_terminal_sexy_files(files, spec):
    names = json.loads(files["dist/schemes/index.json"])
    schemes = []
    for index, path_stem in enumerate(names):
        path = f"dist/schemes/{path_stem}.json"
        if path not in files:
            raise ValueError(f"dist/schemes/index.json entry {index}: missing {path}")
        raw = json.loads(files[path])
        name = raw.get("name")
        if not isinstance(name, str) or not name:
            raise ValueError(f"{path}: invalid name {name!r}")
        colors = raw.get("color")
        if not isinstance(colors, list) or len(colors) != 16:
            raise ValueError(f"{path}: expected exactly 16 ANSI colors")
        converted = {
            "foreground": raw.get("foreground"),
            "background": raw.get("background"),
        }
        for key, color in zip(ANSI_KEYS, colors):
            converted[key] = color
        collection = path_stem.split("/", 1)[0]
        schemes.append(normalize_scheme(
            converted, path, spec["id"], f"{spec['prefix']}{name} ({collection})"
        ))
    return schemes


IMPORTERS = {
    "iterm2-color-schemes": import_iterm_files,
    "gogh": import_gogh_files,
    "alacritty-theme": import_alacritty_files,
    "kitty-themes": import_kitty_files,
    "terminal-sexy": import_terminal_sexy_files,
}


def checkout_files(root, spec):
    if spec["id"] == "iterm2-color-schemes":
        paths = sorted((root / "windowsterminal").glob("*.json"))
    elif spec["id"] == "gogh":
        paths = [root / "data" / "themes.json"]
    elif spec["id"] == "alacritty-theme":
        paths = sorted((root / "themes").glob("*.toml"))
    elif spec["id"] == "kitty-themes":
        paths = [root / "themes.json", *sorted((root / "themes").glob("*.conf"))]
    else:
        index = root / "dist" / "schemes" / "index.json"
        paths = [index, *sorted((root / "dist" / "schemes").glob("**/*.json"))]
    return {str(path.relative_to(root)): path.read_text() for path in paths}


def import_checkout(path, spec):
    root = Path(path)
    revision = checkout_revision(root)
    files = checkout_files(root, spec)
    schemes = IMPORTERS[spec["id"]](files, spec)
    if not schemes:
        raise ValueError(f"no schemes found under {root}")
    return collection_source(spec, revision), schemes, (root / "LICENSE").read_text()


def archive_files(archive, spec):
    files = {}
    for member in sorted(archive.namelist()):
        relative = member.split("/", 1)[-1]
        if not relative or member.endswith("/"):
            continue
        wanted = relative == "LICENSE"
        if spec["id"] == "iterm2-color-schemes":
            wanted = wanted or (
                relative.startswith("windowsterminal/")
                and relative.endswith(".json")
            )
        elif spec["id"] == "gogh":
            wanted = wanted or relative == "data/themes.json"
        elif spec["id"] == "alacritty-theme":
            wanted = wanted or (
                relative.startswith("themes/")
                and relative.endswith(".toml")
            )
        elif spec["id"] == "kitty-themes":
            wanted = wanted or relative == "themes.json" or (
                relative.startswith("themes/")
                and relative.endswith(".conf")
            )
        else:
            wanted = wanted or (
                relative.startswith("dist/schemes/")
                and relative.endswith(".json")
            )
        if not wanted:
            continue
        files[relative] = archive.read(member).decode()
    return files


def import_remote(spec):
    revision_data = json.loads(request_bytes(f"{spec['api']}/commits/{spec['branch']}"))
    revision = revision_data["sha"]
    archive_data = request_bytes(f"{spec['repository']}/archive/{revision}.zip")
    with zipfile.ZipFile(io.BytesIO(archive_data)) as archive:
        files = archive_files(archive, spec)
    schemes = IMPORTERS[spec["id"]](files, spec)
    license_text = files.get("LICENSE")
    if not schemes or license_text is None:
        raise ValueError(f"downloaded {spec['id']} archive is incomplete")
    return collection_source(spec, revision), schemes, license_text


def update(checkouts):
    sources = list(TERMINAL_DEFAULT_SOURCES)
    schemes = [dict(item) for item in TERMINAL_DEFAULT_SCHEMES]
    licenses = {}
    for spec in COLLECTIONS:
        checkout = checkouts.get(spec["id"])
        source, imported, license_text = (
            import_checkout(checkout, spec) if checkout else import_remote(spec)
        )
        sources.append(source)
        schemes.extend(imported)
        licenses[spec["licenseFile"]] = "\n".join(
            line.rstrip() for line in license_text.splitlines()
        ) + "\n"
        print(f"imported {len(imported)} schemes from {spec['id']} {source['revision']}")
    text = catalog_text(sources, schemes)
    CATALOG.write_text(text)
    for path, license_text in licenses.items():
        (ROOT / path).write_text(license_text)
    print(f"wrote {len(schemes)} schemes from {len(sources)} sources")


def generate(output):
    Path(output).write_text(generate_header(load_catalog()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    update_parser = subparsers.add_parser("update")
    update_parser.add_argument(
        "--source", "--iterm-source", dest="iterm_source",
        help="use an existing iTerm2-Color-Schemes checkout",
    )
    update_parser.add_argument(
        "--gogh-source", help="use an existing Gogh checkout",
    )
    update_parser.add_argument(
        "--alacritty-source", help="use an existing alacritty-theme checkout",
    )
    update_parser.add_argument(
        "--kitty-source", help="use an existing kitty-themes checkout",
    )
    update_parser.add_argument(
        "--terminal-sexy-source", help="use an existing terminal.sexy checkout",
    )
    generate_parser = subparsers.add_parser("generate")
    generate_parser.add_argument("output")
    arguments = parser.parse_args()
    if arguments.command == "update":
        update({
            "iterm2-color-schemes": arguments.iterm_source,
            "gogh": arguments.gogh_source,
            "alacritty-theme": arguments.alacritty_source,
            "kitty-themes": arguments.kitty_source,
            "terminal-sexy": arguments.terminal_sexy_source,
        })
    else:
        generate(arguments.output)


if __name__ == "__main__":
    main()

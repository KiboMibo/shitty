#!/usr/bin/env python3

import re
import sys
from pathlib import Path


TESTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TESTS))

from harness import Zutty
from probe_cases import DEC_MODES, DECRQSS_SETTINGS, XTGETTCAP_CAPABILITIES


DA1_EXPECTED = b"\x1b[?64;1;9;15;21;22c"
def query(terminal, request):
    terminal.write(request)
    return terminal.read_input()


def probe_xtgettcap(terminal, capability):
    encoded = capability.encode().hex().encode()
    response = query(terminal, b"\x1bP+q" + encoded + b"\x1b\\")
    match = re.fullmatch(
        rb"\x1bP1\+r" + re.escape(encoded) + rb"=([0-9a-fA-F]*)\x1b\\",
        response,
    )
    if match:
        return bytes.fromhex(match.group(1).decode())
    if response in (b"", b"\x1bP0+r" + encoded + b"\x1b\\"):
        return None
    return response


def probe_decrqss(terminal, setting):
    response = query(terminal, b"\x1bP$q" + setting + b"\x1b\\")
    if response == b"\x1bP0$r\x1b\\" or not response:
        return None
    prefix, suffix = b"\x1bP1$r", b"\x1b\\"
    if response.startswith(prefix) and response.endswith(suffix):
        return response[len(prefix):-len(suffix)]
    return response


def screen_state(terminal):
    snapshot = terminal.snapshot()
    return snapshot.cursor_x, snapshot.cursor_y, snapshot.lines


def software_version(response):
    match = re.fullmatch(rb"\x1bP>\|(.+)\x1b\\", response)
    if not match:
        return None
    text = match.group(1).decode().strip()
    parenthesized = re.fullmatch(r"([^()]+)\(([^()]*)\)", text)
    if parenthesized:
        return parenthesized.group(1).strip().lower(), parenthesized.group(2)
    fields = text.rsplit(maxsplit=1)
    if len(fields) == 2 and any(character.isdigit() for character in fields[1]):
        return fields[0].lower(), fields[1]
    return text.lower(), ""


def run_case(name):
    with Zutty(columns=80, rows=24, save_lines=0) as terminal:
        if name == "device_attributes":
            return query(terminal, b"\x1b[c"), DA1_EXPECTED
        if name == "software":
            identity = software_version(query(terminal, b"\x1b[>q"))
            return identity[0] if identity is not None else None, "zutty"
        if name == "foreground_color":
            return query(terminal, b"\x1b]10;?\x1b\\"), (
                b"\x1b]10;rgb:ffff/ffff/ffff\x1b\\"
            )
        if name == "background_color":
            return query(terminal, b"\x1b]11;?\x1b\\"), (
                b"\x1b]11;rgb:0000/0000/0000\x1b\\"
            )
        if name == "cell_size":
            return query(terminal, b"\x1b[16t"), b""
        if name == "pixel_size":
            return query(terminal, b"\x1b[14t"), b""
        if name == "tab_stop_width":
            return query(terminal, b"\r\x1b[6n\t\x1b[6n"), (
                b"\x1b[1;1R\x1b[1;9R"
            )
        if name == "kitty_keyboard":
            return query(terminal, b"\x1b[?u"), b""
        if name == "color_scheme":
            return query(terminal, b"\x1b[?996n"), b""
        if name == "decrqss_truecolor":
            original = probe_decrqss(terminal, b"m")
            if original is None:
                return False, False
            terminal.write(b"\x1b[48:2:1:2:3m")
            probed = probe_decrqss(terminal, b"m")
            terminal.write(b"\x1b[" + original + b"m")
            matched = probed is not None and re.search(
                rb"48[:;]2[:;]*1[:;]2[:;]3", probed
            ) is not None
            return matched, False
        if name == "decrqcra":
            terminal.write(b"\x1b[1;1HA")
            response = query(
                terminal,
                b"\x1b[1;1;1;1;1*y\x1b[2;1;1;1;2*y",
            )
            checksums = re.findall(rb"\x1bP[12]!~([0-9A-Fa-f]{4})\x1b\\", response)
            return len(checksums) == 2 and checksums[0] != checksums[1], False
        if name == "osc52_clipboard":
            da1 = query(terminal, b"\x1b[c")
            extension = b";52" in da1 or b"?52;" in da1
            return extension or probe_xtgettcap(terminal, "Ms") is not None, False
        if name == "styled_underlines":
            supported = any(
                probe_xtgettcap(terminal, capability) is not None
                for capability in ("Smulx", "Setulc")
            )
            return supported, False
        if name in ("screenleak_xtversion", "screenleak_xtgettcap"):
            before = screen_state(terminal)
            request = (
                b"\x1b[>q" if name.endswith("xtversion")
                else b"\x1bP+q544e\x1b\\"
            )
            query(terminal, request)
            return screen_state(terminal) == before, True
        if name.startswith("mode_"):
            mode = DEC_MODES[name.removeprefix("mode_")]
            return query(terminal, f"\x1b[?{mode}$p".encode()), b""
        if name.startswith("decrqss_"):
            setting_name = name.removeprefix("decrqss_")
            actual = probe_decrqss(terminal, DECRQSS_SETTINGS[setting_name])
            expected = (
                b"64;1;9;15;21;22c" if setting_name == "decscl" else None
            )
            return actual, expected
        if name.startswith("xtgettcap_"):
            capability = name.removeprefix("xtgettcap_")
            if capability not in XTGETTCAP_CAPABILITIES:
                raise RuntimeError(f"unknown XTGETTCAP capability: {capability}")
            return probe_xtgettcap(terminal, capability), None
    raise RuntimeError(f"unknown probe: {name}")


def main():
    if len(sys.argv) != 4:
        raise SystemExit("usage: probe_adapter.py CASE XFAIL_FILE STAMP")
    name = sys.argv[1]
    known = {
        line.strip() for line in Path(sys.argv[2]).read_text().splitlines()
        if line.strip() and not line.startswith("#")
    }
    actual, expected = run_case(name)
    mismatch = actual != expected
    if mismatch != (name in known):
        status = "FAIL" if mismatch else "XPASS"
        print(
            f"{status} ucs-detect/{name}: expected {expected!r}, got {actual!r}",
            file=sys.stderr,
        )
        return 1
    if mismatch:
        print(f"XFAIL ucs-detect/{name}: expected {expected!r}, got {actual!r}")
    else:
        print(f"PASS ucs-detect/{name}")
    stamp = Path(sys.argv[3])
    stamp.parent.mkdir(parents=True, exist_ok=True)
    stamp.touch()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

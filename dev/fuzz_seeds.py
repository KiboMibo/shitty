#!/usr/bin/env python3
"""Generate directed seeds for main_fuzz: [op, len, payload] records whose
feed payloads contain valid-but-hostile terminal sequences."""

import random
import sys
from pathlib import Path

rng = random.Random(20260729)
out_dir = Path(sys.argv[1])
out_dir.mkdir(parents=True, exist_ok=True)

CSI = b"\x1b["
OSC = b"\x1b]"
ST = b"\x1b\\"


def r(n):
    return rng.randrange(n)


def ps(lo, hi):
    return str(rng.randint(lo, hi)).encode()


def block_text():
    words = [b"hello", b"\xe4\xb8\xad\xe6\x96\x87", b"\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7", b"e\xcc\x81", b"\xd7\x90", b"\xff", b"\x80\xfe", b"a" * r(200)]
    return rng.choice(words) + rng.choice([b"\r\n", b"\n", b"\t", b"\x0b", b"\x0c", b"\x85"])


def block_sgr():
    parts = []
    for _ in range(r(5) + 1):
        kind = r(8)
        if kind == 0:
            parts.append(b"38;2;" + ps(0, 255) + b";" + ps(0, 255) + b";" + ps(0, 255))
        elif kind == 1:
            parts.append(b"48;5;" + ps(0, 255))
        elif kind == 2:
            parts.append(b"58;5;" + ps(0, 255))
        elif kind == 3:
            parts.append(b"4:" + ps(0, 5))
        else:
            parts.append(ps(0, 107))
    return CSI + b";".join(parts) + b"m"


def block_move():
    return CSI + ps(1, 30) + b";" + ps(1, 200) + rng.choice([b"H", b"f"]) + CSI + ps(1, 100) + rng.choice([b"A", b"B", b"C", b"D", b"E", b"F", b"G", b"`", b"a", b"b", b"d", b"e"])


def block_margins():
    return (
        CSI + rng.choice([b"?69h", b"?69l"])
        + CSI + ps(1, 20) + b";" + ps(20, 40) + b"r"
        + CSI + ps(1, 60) + b";" + ps(60, 130) + b"s"
        + CSI + rng.choice([b"?6h", b"?6l", b"?45h", b"?45l"])
    )


def block_rect():
    args = b";".join(ps(1, 40) for _ in range(5))
    op = rng.choice([b"$x", b"$v", b"$u", b"${", b"$t", b"$z", b"$y"])
    head = b""
    if op in (b"$t", b"$z", b"$y"):
        head = ps(0, 31) + b";" + ps(0, 15) + b";" + ps(0, 15) + b";"
    if op == b"$y":
        head = ps(1, 100) + b";"
    return CSI + head + args + op


def block_erase():
    return CSI + rng.choice([b"", b"?", b"!"]) + ps(0, 3) + rng.choice([b"J", b"K"]) + CSI + ps(0, 40) + rng.choice([b"X", b"L", b"M", b"@", b"P", b"S", b"T", b"Z", b"I"])


def block_lines():
    return rng.choice([b"\x1b#3", b"\x1b#4", b"\x1b#5", b"\x1b#6", b"\x1b#8"]) + block_text()


def block_charset():
    return rng.choice([
        b"\x1b(0", b"\x1b(B", b"\x1b(A", b"\x1b(4", b"\x0e", b"\x0f",
        b"\x1b%G", b"\x1b%@", b"\x1b%8", b"\x1b" + rng.choice([b"(", b")", b"*", b"+"]) + rng.choice([b"0", b"B", b"<", b"5", b"K", b"R", b"Y"]),
        CSI + b"?42" + rng.choice([b"h", b"l"]),
    ])


def block_modes():
    mode = rng.choice([b"1", b"3", b"5", b"6", b"7", b"12", b"25", b"40", b"45", b"66", b"67", b"69", b"95", b"1000", b"1001", b"1002", b"1003", b"1004", b"1005", b"1006", b"1007", b"1015", b"1016", b"1034", b"1036", b"1045", b"1047", b"1048", b"1049", b"2004", b"2026", b"2031", b"2048", b"47"])
    return CSI + b"?" + mode + rng.choice([b"h", b"l"]) + CSI + rng.choice([b"4", b"6", b"20"]) + rng.choice([b"h", b"l"])


def block_osc():
    kind = r(6)
    if kind == 0:
        return OSC + b"8;id=" + ps(1, 1000) + b";https://example.com/" + ps(1, 1000) + ST + b"LINK" + OSC + b"8;;" + ST
    if kind == 1:
        return OSC + b"52;c;" + b"QUJD" * (r(20) + 1) + ST
    if kind == 2:
        return OSC + str(rng.choice([0, 1, 2, 4, 10, 11, 104, 110, 111])).encode() + b";" + rng.choice([b"?", b"red", b"#aabbcc", b"rgb:12/34/56"]) + ST
    if kind == 3:
        return OSC + b"133;" + rng.choice([b"A", b"B", b"C", b"D"]) + ST
    if kind == 4:
        return OSC + b"9;" + b"x" * r(400) + ST
    return OSC + b"777;notify;title;body" + ST


def block_query():
    return rng.choice([
        CSI + b"c", CSI + b">c", CSI + b"5n", CSI + b"6n", CSI + b"?6n", b"\x1bZ", b"\x9a",
        CSI + ps(1, 24) + b";" + ps(1, 80) + b"R",
        CSI + rng.choice([b"14", b"18", b"19", b"15", b"16"]) + b"t",
        CSI + b"?2026$p", CSI + b"?1049$p", CSI + b"4;2$p",
        CSI + b"?u", CSI + b">0u", CSI + b"=1;1u", CSI + b"=15;3u",
        b"\x1bP$q\x1b[0m\x1b\\", b"\x1bP+q544e\x1b\\",
        OSC + b"4;" + ps(0, 255) + b";?" + ST,
    ])


def block_tabs():
    return b"\x1bH" + CSI + ps(0, 3) + b"g" + CSI + ps(1, 100) + b"I" + CSI + ps(1, 100) + b"Z" + b"\t" * r(10)


def block_window():
    kind = r(4)
    if kind == 0:
        return CSI + b"8;" + ps(1, 60) + b";" + ps(1, 250) + b"t"
    if kind == 1:
        return CSI + b"4;" + ps(1, 2000) + b";" + ps(1, 2000) + b"t"
    if kind == 2:
        return CSI + str(rng.choice([1, 2, 3, 5, 6, 7, 9, 10, 11, 13, 20, 21, 22, 23, 24])).encode() + b"t"
    return CSI + b"9;" + ps(0, 3) + b"t"


def block_kitty():
    return CSI + rng.choice([b">1u", b">15u", b"<u", b"?u", b"=2;1u", b"=31;2u", b"=7;0u"])


def block_sync():
    return CSI + b"?2026h" + block_text() + block_sgr() + rng.choice([CSI + b"?2026l", b""])


def block_reset():
    return rng.choice([b"\x1bc", CSI + b"!p", b"\x1b[?3l"])


GENS = [block_text] * 8 + [block_sgr] * 3 + [block_move] * 3 + [block_margins] * 2 + [block_rect] * 3 + [block_erase] * 2 + [block_lines] * 2 + [block_charset] * 2 + [block_modes] * 3 + [block_osc] * 2 + [block_query] * 2 + [block_tabs] + [block_window] * 2 + [block_kitty] + [block_sync] + [block_reset]


def feed(payload):
    return bytes([0, len(payload)]) + payload


def action(op, payload):
    return bytes([op, len(payload)]) + payload


def make_seed(index):
    records = []
    for _ in range(rng.randint(2, 7)):
        payload = b"".join(rng.choice(GENS)() for _ in range(rng.randint(1, 6)))
        while len(payload) > 255:
            payload = payload[:255]
        records.append(feed(payload))
        roll = r(14)
        if roll == 0:
            records.append(action(177, bytes([1 + r(200), 1 + r(60)])))
        elif roll == 1:
            records.append(action(rng.choice([166, 167, 168, 169]), bytes([r(256), r(256)])))
        elif roll == 2:
            records.append(action(rng.choice([170, 171, 172, 173, 174]), bytes([r(256), r(256), r(256), r(256), r(256)])))
        elif roll == 3:
            records.append(action(176, bytes(r(256) for _ in range(r(40)))))
        elif roll == 4:
            records.append(action(rng.choice([160, 161]), bytes([r(150), r(256)])))
        elif roll == 5:
            records.append(action(rng.choice([163, 164, 165]), bytes(r(256) for _ in range(9))))
        elif roll == 6:
            records.append(action(178, bytes([r(256)])))
    return b"".join(records)


for i in range(int(sys.argv[2]) if len(sys.argv) > 2 else 4000):
    (out_dir / f"seed-{i:05d}").write_bytes(make_seed(i))
print(f"wrote seeds to {out_dir}", file=sys.stderr)

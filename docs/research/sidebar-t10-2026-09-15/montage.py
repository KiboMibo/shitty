#!/usr/bin/env python3
"""Builds the frames T10 keeps out of the probe's own sRGB captures.

  sidebar-tone.png   both fields: today (seam, no tone) / after (tone, no seam)
                     / |today - after| x8, the strip and the first 120 pt of
                     terminal beside it
  edge-fade.png      both fields: the 60 pt around the strip's trailing edge,
                     today and after, blown up 4x, with the per-column
                     luminance profile drawn under each
"""
import sys
import numpy as np
from PIL import Image, ImageDraw

SRC, DEST, AFTER = sys.argv[1], sys.argv[2], sys.argv[3]   # srgb dir, out dir, tag of the "after" frame
SCALE = 2
BG = (18, 18, 20)


def load(p):
    return Image.open(f"{SRC}/{p}.png").convert("RGB")


def crop(img, box, pad=0):
    x, y, w, h = [v * SCALE for v in box]
    return img.crop((x - pad, y - pad, x + w + pad, y + h + pad))


def label(img, text, height=22):
    out = Image.new("RGB", (img.width, img.height + height), BG)
    out.paste(img, (0, height))
    ImageDraw.Draw(out).text((6, 5), text, fill=(210, 212, 218))
    return out


def amplified(a, b, gain=8):
    d = np.abs(np.asarray(a).astype(np.int16) - np.asarray(b).astype(np.int16))
    return Image.fromarray(np.clip(d * gain, 0, 255).astype("uint8"))


def row(images, gap=10):
    w = sum(i.width for i in images) + gap * (len(images) - 1)
    h = max(i.height for i in images)
    out = Image.new("RGB", (w, h), BG)
    x = 0
    for i in images:
        out.paste(i, (x, 0))
        x += i.width + gap
    return out


def column(images, gap=10):
    w = max(i.width for i in images)
    h = sum(i.height for i in images) + gap * (len(images) - 1)
    out = Image.new("RGB", (w, h), BG)
    y = 0
    for i in images:
        out.paste(i, (0, y))
        y += i.height + gap
    return out


def profile_chart(img, x0, x1, y0, y1, width, height=80):
    g = np.asarray(img).astype(np.float64)[y0 * SCALE:y1 * SCALE, x0 * SCALE:x1 * SCALE].mean(axis=(0, 2))
    out = Image.new("RGB", (width, height), BG)
    d = ImageDraw.Draw(out)
    lo, hi = g.min() - 2, g.max() + 2
    pts = [(int(i * width / len(g)), int(height - 1 - (v - lo) / (hi - lo) * (height - 1))) for i, v in enumerate(g)]
    d.line(pts, fill=(120, 200, 255), width=2)
    d.text((4, 2), f"per-column mean, bytes {g.min():.1f}..{g.max():.1f}", fill=(210, 212, 218))
    return out


STRIP = (0, 0, 340, 300)           # the strip and 120 pt of terminal, the first six rows
EDGE = (190, 100, 60, 60)          # 60x60 pt around the trailing edge, plain rows

rows = []
for f, name in (("0.10", "dark field 0.10"), ("0.82", "light field 0.82")):
    today = crop(load(f"f{f}-today"), STRIP)
    after = crop(load(f"f{f}-{AFTER}"), STRIP)
    rows.append(label(row([label(today, "today: 1px seam, no tone"),
                           label(after, f"after: tone, fade, no seam ({AFTER})"),
                           label(amplified(today, after), "|today - after| x8")]), name))
column(rows).save(f"{DEST}/sidebar-tone.png")

rows = []
for f, name in (("0.10", "dark field 0.10"), ("0.82", "light field 0.82")):
    cells = []
    for tag, txt in (("today", "today"), (AFTER, "after")):
        img = load(f"f{f}-{tag}")
        big = crop(img, EDGE).resize((EDGE[2] * SCALE * 4, EDGE[3] * SCALE * 4), Image.NEAREST)
        chart = profile_chart(img, EDGE[0], EDGE[0] + EDGE[2], 110, 500, big.width)
        cells.append(label(column([big, chart], gap=4), f"{txt}: x {EDGE[0]}..{EDGE[0]+EDGE[2]} pt, 4x"))
    rows.append(label(row(cells), name))
column(rows).save(f"{DEST}/edge-fade.png")
print("written")

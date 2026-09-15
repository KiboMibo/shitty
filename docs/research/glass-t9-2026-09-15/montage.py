#!/usr/bin/env python3
"""Builds the three frames T9 keeps, out of the probe's own captures."""
import numpy as np
from PIL import Image, ImageDraw

OUT = "out/"
DEST = "/private/tmp/claude-502/-Users-kibomibo-Projects-github-com-shitty/194388b0-0215-4546-8bce-e6e371a17ad1/scratchpad/t9/keep/"
PILL = (6, 6 + 46 * 1 + 2, 208, 42)     # window points, top-left origin
SCALE = 2


def load(p):
    return Image.open(OUT + p).convert("RGB")


def crop_pill(img, pad=10):
    x, y, w, h = [v * SCALE for v in PILL]
    p = pad * SCALE
    return img.crop((x - p, y - p, x + w + p, y + h + p))


def label(img, text, height=22):
    out = Image.new("RGB", (img.width, img.height + height), (18, 18, 20))
    out.paste(img, (0, height))
    ImageDraw.Draw(out).text((6, 5), text, fill=(210, 212, 218))
    return out


def amplified(a, b, gain=8):
    d = np.abs(np.asarray(a).astype(np.int16) - np.asarray(b).astype(np.int16))
    return Image.fromarray(np.clip(d * gain, 0, 255).astype("uint8"))


def row(images, gap=10, bg=(18, 18, 20)):
    w = sum(i.width for i in images) + gap * (len(images) - 1)
    h = max(i.height for i in images)
    out = Image.new("RGB", (w, h), bg)
    x = 0
    for i in images:
        out.paste(i, (x, 0))
        x += i.width + gap
    return out


def column(images, gap=10, bg=(18, 18, 20)):
    w = max(i.width for i in images)
    h = sum(i.height for i in images) + gap * (len(images) - 1)
    out = Image.new("RGB", (w, h), bg)
    y = 0
    for i in images:
        out.paste(i, (0, y))
        y += i.height + gap
    return out


# 1. the corner-configuration matrix, both fields
a = load("corners-dark.png").resize((900, 561))
b = load("corners-light.png").resize((900, 561))
column([label(a, "cornerConfiguration, dark field"),
        label(b, "cornerConfiguration, light field")]).save(DEST + "corner-configuration.png")

# 2. effectIsInteractive, the pill under a press, both fields
rows = []
for f, name in (("0.10", "dark field"), ("0.82", "light field")):
    no = crop_pill(load(f"pill-f{f}-i0-top1-press.png"))
    yes = crop_pill(load(f"pill-f{f}-i1-top1-press.png"))
    rest = crop_pill(load(f"pill-f{f}-i1-top1-rest.png"))
    rows.append(label(row([label(rest, "rest, YES"),
                           label(no, "press, NO"),
                           label(yes, "press, YES"),
                           label(amplified(no, yes), "|press NO - press YES| x8")]),
                      name))
column(rows).save(DEST + "interactive-press.png")

# 3. the pill's shape, fixed radius against capsule, both fields
rows = []
for f, name in (("0.10", "dark field"), ("0.82", "light field")):
    old = crop_pill(load(f"shape-f{f}-n0-rest.png"))
    new = crop_pill(load(f"shape-f{f}-n1-rest.png"))
    rows.append(label(row([label(old, "cornerRadius 6 (macOS 26)"),
                           label(new, "capsule (macOS 27)"),
                           label(amplified(old, new), "difference x8")]), name))
column(rows).save(DEST + "pill-capsule.png")
print("written")

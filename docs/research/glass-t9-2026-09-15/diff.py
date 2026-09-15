#!/usr/bin/env python3
"""Pixel diff between two probe frames, restricted to a named region.

Reports max, mean and the share of pixels over 2 - the same three numbers T3
used, because the third is the one that tells a real change from dither.
"""
import sys
import numpy as np
from PIL import Image

H = 561.0          # root bounds height in points, as the probe logs it
SCALE = 2

# The pill of row 1, in window points, bottom-left origin. Same arithmetic as
# t9PillFor() in the probe: the panel is flipped, so the row-1 pill sits
# listTop + rowH below the panel top.
# The frame is top-left origin and so is the panel, which is flipped: the
# pill's box in the image is the pill's box in the panel, with no flip at all.
PILL = (6, 6 + 46 * 1 + 2, 220 - 12, 46 - 4)


def load(p):
    return np.asarray(Image.open(p).convert("RGB")).astype(np.int16)


def crop(img, box):
    x, y, w, h = [int(round(v * SCALE)) for v in box]
    return img[y:y + h, x:x + w]


def stats(a, b, box=None, label=""):
    if box is not None:
        a, b = crop(a, box), crop(b, box)
    d = np.abs(a.astype(np.int32) - b.astype(np.int32))
    m = d.max(axis=2)
    return (f"{label:44s} max={m.max():3d}  mean={d.mean():7.4f}  "
            f">2: {100.0 * (m > 2).mean():6.3f}%")


if __name__ == "__main__":
    a, b, label = sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else ""
    A, B = load(a), load(b)
    print(stats(A, B, None, label + " [whole frame]"))
    print(stats(A, B, PILL, label + " [pill only]"))

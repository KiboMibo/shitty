#!/usr/bin/env python3
"""Corner-shape reader for the T9 probe frames.

The field this probe stands on is striped - a black hairline every 8 points -
so a tile mask taken against one global field colour fires on every hairline.
T5 paid for exactly that once ("29.94 units of difference on a frame with no
element in it"). Here the reference is taken PER ROW, from the padding to the
left and right of the tile on that same row, so the stripes cancel.

The reported radius is the inset of the first tile pixel on the tile's topmost
row: on a rounded rect of radius r that inset is r, on a square one it is 0.
"""
import sys
import numpy as np
from PIL import Image


def load(path):
    return np.asarray(Image.open(path).convert("RGB")).astype(np.int16)


def tile_mask(img, box, scale=2, pad=16, thr=12):
    x, y, w, h = [int(round(v * scale)) for v in box]
    y0, x0 = max(0, y - pad), max(0, x - pad)
    sub = img[y0:y + h + pad, x0:x + w + pad].astype(np.float32)
    left = np.median(sub[:, :pad], axis=1)          # (rows, 3) per-row reference
    right = np.median(sub[:, -pad:], axis=1)
    ref = ((left + right) / 2.0)[:, None, :]
    return np.abs(sub - ref).max(axis=2) > thr, (x - x0, y - y0, w, h)


def tile_report(img, box, scale=2, name=""):
    mask, (ix, iy, w, h) = tile_mask(img, box, scale)
    rows = np.where(mask.sum(axis=1) > 6)[0]
    cols = np.where(mask.sum(axis=0) > 6)[0]
    if len(rows) == 0 or len(cols) == 0:
        return f"{name:38s} NO CONTENT"
    r0, r1, c0, c1 = rows[0], rows[-1], cols[0], cols[-1]

    def inset(row_index, from_left=True):
        row = mask[row_index, c0:c1 + 1]
        nz = np.where(row)[0]
        if not len(nz):
            return -1
        return int(nz[0]) if from_left else int(len(row) - 1 - nz[-1])

    tl = inset(r0, True)
    tr = inset(r0, False)
    bl = inset(r1, True)
    br = inset(r1, False)
    return (f"{name:38s} box={c1 - c0 + 1:4d}x{r1 - r0 + 1:<4d}px  "
            f"inset TL={tl:3d} TR={tr:3d} BL={bl:3d} BR={br:3d} px  "
            f"=> r ~ {tl / scale:5.1f} pt")


TILES = [
    ("1 plain capsule, NOT applied", (30, 446, 260, 84)),
    ("2 plain capsule, applied", (320, 446, 260, 84)),
    ("3 plain fixed 24, applied", (610, 446, 260, 84)),
    ("4 glass capsule", (30, 336, 260, 84)),
    ("5 glass capsule + cornerRadius 6", (320, 336, 260, 84)),
    ("6 glass cornerRadius 6 (control)", (610, 336, 260, 84)),
    ("7 container fixed 28", (30, 162, 550, 144)),
    ("7i glass concentric(min 6) in r=28", (44, 176, 522, 116)),
    ("8 container fixed 0", (610, 162, 260, 144)),
    ("8i glass concentric(min 6) in r=0", (624, 176, 232, 116)),
]

if __name__ == "__main__":
    img = load(sys.argv[1])
    H = 561.0
    for name, (x, ybl, w, h) in TILES:
        print(tile_report(img, (x, H - (ybl + h), w, h), name=name))

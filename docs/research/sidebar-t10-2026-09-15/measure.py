#!/usr/bin/env python3
"""Reads the T10 probe frames: the strip's tone against the terminal, the
fade profile, the seam column, and WCAG contrast of the title text on the
strip and on the pill.

Every frame is converted to sRGB first (sips --matchTo; T6: screencapture
writes in the display's space and nominal bytes never appear in the file).
Regions are in window points, top-left origin, and all lie in the part of
the strip to the right of the text, so a surface mean is a mean over pixels
the text never touches. Row 1 carries the pill; rows 2+ are plain strip.

Lift figures are all WITHIN one frame - the strip against the terminal on
the same rows - so the control frame (no tone, no seam) must read zero on
every one of them before any other row of the table means anything.
"""
import sys, json
import numpy as np
from PIL import Image

SCALE = 2
FG = (0.92, 0.93, 0.95)          # nominal title colour, t10Fg(1.0)
SIDEBAR_W, FADE = 220, 20

# x ranges (points): text-free strip, terminal, fade zone around the edge
STRIP_X = (130, 195)
TERM_X = (240, 400)
PROFILE_X = (170, 250)
ROWS_Y = (110, 500)              # rows 2..5 and below, no pill
PILL_Y = (58, 92)                # inside row 1's pill (54..96), clear of its rim
TITLE2 = (14, 110, 105, 119)     # row 2 title glyph box
TITLE1 = (14, 110, 59, 73)       # row 1 title glyph box (on the pill)


def load(path):
    return np.asarray(Image.open(path).convert("RGB")).astype(np.float64) / 255.0


def lin(c):
    c = np.asarray(c, dtype=np.float64)
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def lum(rgb):
    l = lin(rgb)
    return 0.2126 * l[..., 0] + 0.7152 * l[..., 1] + 0.0722 * l[..., 2]


def region(img, x0, x1, y0, y1):
    return img[y0 * SCALE:y1 * SCALE, x0 * SCALE:x1 * SCALE]


def contrast(a, b):
    hi, lo = max(a, b), min(a, b)
    return (hi + 0.05) / (lo + 0.05)


def surface(img, xr, yr):
    r = region(img, xr[0], xr[1], yr[0], yr[1])
    mean_rgb = r.reshape(-1, 3).mean(axis=0)
    return {
        "bytes": [round(float(v) * 255, 1) for v in mean_rgb],
        "Y": float(lum(r).mean()),
    }


def measure(path):
    img = load(path)
    strip = surface(img, STRIP_X, ROWS_Y)
    term = surface(img, TERM_X, ROWS_Y)
    pill = surface(img, STRIP_X, PILL_Y)
    term1 = surface(img, TERM_X, PILL_Y)
    # fade profile: per-column luminance over the plain rows
    prof = lum(region(img, PROFILE_X[0], PROFILE_X[1], ROWS_Y[0], ROWS_Y[1])).mean(axis=0)
    prof_b = region(img, PROFILE_X[0], PROFILE_X[1], ROWS_Y[0], ROWS_Y[1]).mean(axis=(0, 2)) * 255
    steps = np.diff(prof_b)
    # the seam column: x = 219 pt -> px 438, 439
    sx = (SIDEBAR_W - 1 - PROFILE_X[0]) * SCALE
    seam_cols = prof_b[sx:sx + 2]
    seam_nbrs = np.concatenate([prof_b[sx - 4:sx - 1], prof_b[sx + 3:sx + 6]])
    # text: nominal, and the 99th percentile of the glyph box
    y_fg = float(lum(np.array(FG)))
    t2 = lum(region(img, TITLE2[0], TITLE2[1], TITLE2[2], TITLE2[3]))
    t1 = lum(region(img, TITLE1[0], TITLE1[1], TITLE1[2], TITLE1[3]))
    return {
        "frame": path.split("/")[-1],
        "strip": strip, "term": term, "pill": pill, "term_row1": term1,
        "lift_contrast": contrast(strip["Y"], term["Y"]) - 1.0,
        "lift_Y": strip["Y"] / term["Y"] - 1.0,
        "lift_bytes": float(np.mean(strip["bytes"]) / np.mean(term["bytes"])) - 1.0,
        "pill_vs_term_contrast": contrast(pill["Y"], term1["Y"]) - 1.0,
        "profile_bytes": [round(float(v), 2) for v in prof_b],
        "profile_max_step": float(np.abs(steps).max()),
        "profile_monotone": bool((steps <= 0.5).all()) if strip["Y"] >= term["Y"] else bool((steps >= -0.5).all()),
        "seam_cols": [round(float(v), 2) for v in seam_cols],
        "seam_dip": float(np.mean(seam_nbrs) - np.min(seam_cols)),
        "title_contrast_nominal": contrast(y_fg, strip["Y"]),
        "title_contrast_pixels": contrast(float(np.percentile(t2, 99)), strip["Y"]),
        "pill_title_contrast_nominal": contrast(y_fg, pill["Y"]),
        "pill_title_contrast_pixels": contrast(float(np.percentile(t1, 99)), pill["Y"]),
        "text_p99_Y": float(np.percentile(t2, 99)),
        "text_nominal_Y": y_fg,
    }


if __name__ == "__main__":
    rows = [measure(p) for p in sys.argv[1:]]
    print(f"{'frame':34s} {'strip B':>8s} {'term B':>8s} {'lift c':>7s} {'lift Y':>7s} {'lift b':>7s} {'pill/t':>7s} "
          f"{'title':>6s} {'t.px':>6s} {'pill':>6s} {'p.px':>6s} {'step':>5s} {'mono':>4s} {'seam dip':>8s}")
    for r in rows:
        print(f"{r['frame']:34s} {np.mean(r['strip']['bytes']):8.2f} {np.mean(r['term']['bytes']):8.2f} "
              f"{100*r['lift_contrast']:6.1f}% {100*r['lift_Y']:6.1f}% {100*r['lift_bytes']:6.1f}% {100*r['pill_vs_term_contrast']:6.1f}% "
              f"{r['title_contrast_nominal']:6.2f} {r['title_contrast_pixels']:6.2f} "
              f"{r['pill_title_contrast_nominal']:6.2f} {r['pill_title_contrast_pixels']:6.2f} "
              f"{r['profile_max_step']:5.2f} {'yes' if r['profile_monotone'] else 'NO':>4s} {r['seam_dip']:8.2f}")
    with open("measure.json", "w") as f:
        json.dump(rows, f, indent=1)

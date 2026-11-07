#!/usr/bin/env python3
"""Compare two folders of same-named PNG screenshots (tools/test_native.sh).

The two renders do the same sums in a different place (the CPU's float
transform and divide against the GPU's), so edges land a fraction of a pixel
apart and a texel or fog step can tip either way. What must not happen is a
real difference: missing or extra geometry, inside-out karts, wrong depth or
fog. So each picture passes when its differences are thin, never an area:

  - "far" pixels, any channel more than FAR levels apart, are at most
    MAX_FAR of the picture, and
  - after eroding the far pixels by ERODE pixels each way (a pixel stays
    only if its whole (2 * ERODE + 1) square is far), at most MAX_SOLID
    pixels are left. Edges a fraction of a pixel apart, and the seams
    where two surfaces in the same plane fight over depth (Neon Skyway's
    gate halos over their bands: rounding picks the winner, on the PVR too),
    are lines a few pixels wide and erode away; a missing kart, an
    inside-out model or wrong fog is an area and does not.

Usage: compare_shots.py A_DIR B_DIR DIFF_DIR. Writes a difference picture to
DIFF_DIR for each failure. Exit code is the verdict."""
import sys
from pathlib import Path

import numpy as np
from PIL import Image

FAR = 48
MAX_FAR = 0.01
ERODE = 3
MAX_SOLID = 8   # pixels; measured 2026-11-07: every CPU/GPU pair left 0, a 9 px square leaves 9


def erode(mask, r):
    h, w = mask.shape
    out = mask.copy()
    for dy in range(-r, r + 1):
        for dx in range(-r, r + 1):
            shifted = np.zeros_like(mask)
            shifted[max(dy, 0):h + min(dy, 0), max(dx, 0):w + min(dx, 0)] = \
                mask[max(-dy, 0):h + min(-dy, 0), max(-dx, 0):w + min(-dx, 0)]
            out &= shifted
    return out


def main():
    a_dir, b_dir, diff_dir = (Path(p) for p in sys.argv[1:4])
    names = sorted(p.name for p in a_dir.glob("*.png"))
    if not names:
        print("compare_shots: no pictures in", a_dir)
        return 1
    fail = 0
    for name in names:
        bp = b_dir / name
        if not bp.exists():
            print(f"{name}: FAIL (missing from {b_dir})")
            fail = 1
            continue
        a = np.asarray(Image.open(a_dir / name).convert("RGB")).astype(np.int16)
        b = np.asarray(Image.open(bp).convert("RGB")).astype(np.int16)
        d = np.abs(a - b).max(axis=2)
        far = d > FAR
        share = far.mean()
        solid = int(erode(far, ERODE).sum())
        ok = share <= MAX_FAR and solid <= MAX_SOLID
        print(f"{name}: {'PASS' if ok else 'FAIL'} (far pixels {share * 100:.3f}%, "
              f"left after eroding {solid} px, mean difference {d.mean():.2f})")
        if not ok:
            fail = 1
            diff_dir.mkdir(parents=True, exist_ok=True)
            Image.fromarray(np.clip(d * 4, 0, 255).astype(np.uint8)).save(diff_dir / name)
    return fail


if __name__ == "__main__":
    sys.exit(main())

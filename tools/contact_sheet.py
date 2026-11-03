#!/usr/bin/env python3
"""Contact sheet of the host autotest's screenshots (PORT_PLAN.md M2).

    tools/contact_sheet.py [captures/host] [out.png]

One row per track (t<track>-f<frame>.png), frames left to right, each
scaled to half size and labelled. Default output: <dir>/contact.png.
"""
import re
import sys
from pathlib import Path

from PIL import Image, ImageDraw

SCALE = 2
LABEL_H = 14


def main():
    src = Path(sys.argv[1] if len(sys.argv) > 1 else "captures/host")
    out = Path(sys.argv[2]) if len(sys.argv) > 2 else src / "contact.png"
    rows = {}
    for p in sorted(src.glob("t*-f*.png")):
        m = re.fullmatch(r"t(\d+)-f(\d+)\.png", p.name)
        if m:
            rows.setdefault(int(m[1]), []).append((int(m[2]), p))
    if not rows:
        sys.exit(f"contact_sheet: no t*-f*.png in {src}")
    cw, ch = 640 // SCALE, 480 // SCALE
    ncol = max(len(v) for v in rows.values())
    sheet = Image.new("RGB", (cw * ncol, (ch + LABEL_H) * len(rows)), (24, 24, 24))
    draw = ImageDraw.Draw(sheet)
    for r, track in enumerate(sorted(rows)):
        y = r * (ch + LABEL_H)
        for c, (frame, p) in enumerate(sorted(rows[track])):
            img = Image.open(p).convert("RGB").resize((cw, ch), Image.BILINEAR)
            sheet.paste(img, (c * cw, y + LABEL_H))
            draw.text((c * cw + 4, y + 1), f"track {track}  frame {frame}", fill=(220, 220, 220))
    sheet.save(out)
    print(f"contact_sheet: {out} ({len(rows)} tracks)")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Contact sheet of the host autotest's screenshots (PORT_PLAN.md M2).

    tools/contact_sheet.py [captures/host] [out.png]

One row per track (t<track>-f<frame>.png), then the menus and split
screen (menu-f<frame>.png, tools/capture_menus.sh) over as many rows as
they need; frames left to right, each scaled to half size and labelled.
Default output: <dir>/contact.png.
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
    tracks, menus = {}, []
    for p in sorted(src.glob("*-f*.png")):
        m = re.fullmatch(r"t(\d+)-f(\d+)\.png", p.name)
        if m:
            tracks.setdefault(int(m[1]), []).append((int(m[2]), p))
        m = re.fullmatch(r"menu-f(\d+)\.png", p.name)
        if m:
            menus.append((int(m[1]), p))
    if not tracks and not menus:
        sys.exit(f"contact_sheet: no t*-f*.png or menu-f*.png in {src}")
    ncol = max([len(v) for v in tracks.values()] + [min(len(menus), 4)])
    rows = [(f"track {t}", sorted(tracks[t])) for t in sorted(tracks)]
    menus.sort()
    rows += [("menus", menus[i:i + ncol]) for i in range(0, len(menus), ncol)]
    cw, ch = 640 // SCALE, 480 // SCALE
    sheet = Image.new("RGB", (cw * ncol, (ch + LABEL_H) * len(rows)), (24, 24, 24))
    draw = ImageDraw.Draw(sheet)
    for r, (label, shots) in enumerate(rows):
        y = r * (ch + LABEL_H)
        for c, (frame, p) in enumerate(shots):
            img = Image.open(p).convert("RGB").resize((cw, ch), Image.BILINEAR)
            sheet.paste(img, (c * cw, y + LABEL_H))
            draw.text((c * cw + 4, y + 1), f"{label}  frame {frame}", fill=(220, 220, 220))
    sheet.save(out)
    print(f"contact_sheet: {out} ({len(tracks)} tracks, {len(menus)} menu frames)")


if __name__ == "__main__":
    main()

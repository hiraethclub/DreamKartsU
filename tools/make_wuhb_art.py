#!/usr/bin/env python3
"""The .wuhb's icon and boot splashes from upstream's assets/icon.png.

    tools/make_wuhb_art.py [out_dir]   (default port/wuhb)

icon.png 128x128; tv.png 1280x720 and drc.png 854x480: the icon centred
on a dark blue field with the title under it.
"""
import sys
from pathlib import Path

from PIL import Image, ImageDraw

SRC = Path("upstream/assets/icon.png")
BG = (12, 24, 64)


def splash(icon, w, h, path):
    im = Image.new("RGB", (w, h), BG)
    side = h // 2
    ic = icon.resize((side, side), Image.LANCZOS)
    im.paste(ic, ((w - side) // 2, h // 5), ic)
    d = ImageDraw.Draw(im)
    text = "DreamKartsU - unofficial port of DreamKarts by Rich Stokes"
    tw = d.textlength(text)
    d.text(((w - tw) / 2, h // 5 + side + h // 12), text, fill=(230, 230, 240))
    im.save(path)


def main():
    out = Path(sys.argv[1] if len(sys.argv) > 1 else "port/wuhb")
    out.mkdir(parents=True, exist_ok=True)
    icon = Image.open(SRC).convert("RGBA")
    icon.resize((128, 128), Image.LANCZOS).save(out / "icon.png")
    splash(icon, 1280, 720, out / "tv.png")
    splash(icon, 854, 480, out / "drc.png")
    print(f"make_wuhb_art: {out}")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""
Produces res/hddsynthlogo.bmp from HDDSynthLogoSmall.png (repo root), for
display in the About dialog.

Classic Win9x GDI has no PNG support (BITMAP resources only), and BITMAP
resources have no alpha channel -- so this composites the source PNG's
transparency onto the classic Win95 dialog face gray (192,192,192) rather
than leaving it transparent (which would render as garbage/black on real
Win9x GDI), and resizes it to a size that fits comfortably in the About
dialog.

Requires Pillow: python3 -m pip install --break-system-packages pillow
"""
from PIL import Image

SOURCE_PATH = "HDDSynthLogoSmall.png"
OUTPUT_PATH = "res/hddsynthlogo.bmp"
TARGET_WIDTH = 160
DIALOG_FACE_GRAY = (192, 192, 192)

if __name__ == "__main__":
    src = Image.open(SOURCE_PATH).convert("RGBA")

    bg = Image.new("RGB", src.size, DIALOG_FACE_GRAY)
    bg.paste(src, mask=src.split()[3])

    target_height = round(TARGET_WIDTH * src.size[1] / src.size[0])
    resized = bg.resize((TARGET_WIDTH, target_height), Image.LANCZOS)

    resized.save(OUTPUT_PATH, "BMP")
    print(f"wrote {OUTPUT_PATH} ({resized.size[0]}x{resized.size[1]}, 24-bit)")

#!/usr/bin/env python3
"""
Produces the res/menu_*.bmp icons shown against each tray context-menu item
(src/tray.cpp, via SetMenuItemBitmaps).

SetMenuItemBitmaps -- unlike the newer MENUITEMINFO::hbmpItem, which
Windows 2000 introduced and Win9x/ME never got -- just BitBlts the bitmap
directly onto the menu with no masking, so there's no way to make the
background transparent across every possible color scheme. These are
generated against plain white, matching the default classic Windows Menu
background color; a user running a heavily customized color scheme would
see a faint white box around each icon, the same tradeoff most classic
Win9x-era software with bitmap menu items made.

Drawn at 4x supersampling and downsized with Lanczos for a lightly
anti-aliased edge (same technique as tools/make_about_logo.py), then saved
as a classic (non-V4/V5) 24-bit BMP -- verify with `file` that it still
reports "Windows 3.x format" after any changes here.

Requires Pillow: python3 -m pip install --break-system-packages pillow
"""
import math
from PIL import Image, ImageDraw

SUPERSAMPLE = 8
SIZE = 16
CANVAS = SIZE * SUPERSAMPLE
WHITE = (255, 255, 255)
OUTPUT_DIR = "res/"


def _canvas():
    return Image.new("RGB", (CANVAS, CANVAS), WHITE)


def _save(img, name):
    path = f"{OUTPUT_DIR}{name}.bmp"
    img.resize((SIZE, SIZE), Image.LANCZOS).save(path, "BMP")
    print(f"wrote {path} ({SIZE}x{SIZE}, 24-bit)")


def gear(color=(90, 90, 90)):
    """Settings: an 8-tooth cog with a hollow center."""
    img = _canvas()
    d = ImageDraw.Draw(img)
    cx = cy = CANVAS / 2
    n_teeth = 8
    r_out = CANVAS * 0.42
    r_in = CANVAS * 0.30
    r_hole = CANVAS * 0.16
    pts = []
    for i in range(n_teeth * 2):
        angle = math.radians(i * (360 / (n_teeth * 2)) - 90)
        r = r_out if i % 2 == 0 else r_in
        pts.append((cx + r * math.cos(angle), cy + r * math.sin(angle)))
    d.polygon(pts, fill=color)
    d.ellipse([cx - r_hole, cy - r_hole, cx + r_hole, cy + r_hole], fill=WHITE)
    return img


def info(circle_color=(50, 90, 170)):
    """About: the classic circled lowercase "i"."""
    img = _canvas()
    d = ImageDraw.Draw(img)
    cx = cy = CANVAS / 2
    r = CANVAS * 0.42
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=circle_color)
    dot_r = CANVAS * 0.06
    d.ellipse([cx - dot_r, cy - CANVAS * 0.24 - dot_r, cx + dot_r, cy - CANVAS * 0.24 + dot_r], fill=WHITE)
    stem_w = CANVAS * 0.09
    d.rectangle([cx - stem_w, cy - CANVAS * 0.06, cx + stem_w, cy + CANVAS * 0.28], fill=WHITE)
    return img


def exit_icon(door_color=(90, 90, 90), arrow_color=(190, 40, 40)):
    """Exit: an open doorway with an arrow passing through it."""
    img = _canvas()
    d = ImageDraw.Draw(img)
    x0, y0, x1, y1 = CANVAS * 0.14, CANVAS * 0.14, CANVAS * 0.52, CANVAS * 0.86
    d.rectangle([x0, y0, x1, y1], outline=door_color, width=max(1, int(CANVAS * 0.09)))
    kx, ky, kr = x1 - CANVAS * 0.09, (y0 + y1) / 2, CANVAS * 0.035
    d.ellipse([kx - kr, ky - kr, kx + kr, ky + kr], fill=door_color)
    ay = CANVAS * 0.5
    ax0, ax1 = CANVAS * 0.58, CANVAS * 0.86
    w = CANVAS * 0.09
    d.line([(ax0, ay), (ax1, ay)], fill=arrow_color, width=int(w))
    d.polygon(
        [
            (ax1 - CANVAS * 0.03, ay - CANVAS * 0.16),
            (ax1 + CANVAS * 0.14, ay),
            (ax1 - CANVAS * 0.03, ay + CANVAS * 0.16),
        ],
        fill=arrow_color,
    )
    return img


def note(color=(60, 60, 60)):
    """Sample pack submenu: a single eighth note."""
    img = _canvas()
    d = ImageDraw.Draw(img)
    nx, ny, nrx, nry = CANVAS * 0.34, CANVAS * 0.74, CANVAS * 0.18, CANVAS * 0.14
    d.ellipse([nx - nrx, ny - nry, nx + nrx, ny + nry], fill=color)
    stem_x = nx + nrx * 0.85
    d.line([(stem_x, ny), (stem_x, CANVAS * 0.14)], fill=color, width=max(1, int(CANVAS * 0.07)))
    d.polygon(
        [(stem_x, CANVAS * 0.14), (stem_x + CANVAS * 0.30, CANVAS * 0.28), (stem_x, CANVAS * 0.42)],
        fill=color,
    )
    return img


def power(color):
    """Run at Windows Startup: the standard power symbol, colored per state."""
    img = _canvas()
    d = ImageDraw.Draw(img)
    cx = cy = CANVAS / 2
    r = CANVAS * 0.34
    w = max(1, int(CANVAS * 0.09))
    d.arc([cx - r, cy - r, cx + r, cy + r], start=-55, end=235, fill=color, width=w)
    d.line([(cx, cy - r * 1.15), (cx, cy - r * 0.15)], fill=color, width=w)
    return img


if __name__ == "__main__":
    _save(gear(), "menu_settings")
    _save(info(), "menu_about")
    _save(exit_icon(), "menu_exit")
    _save(note(), "menu_sample")
    _save(power((40, 140, 60)), "menu_autostart_on")
    _save(power((150, 150, 150)), "menu_autostart_off")

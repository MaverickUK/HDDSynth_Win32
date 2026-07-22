#!/usr/bin/env python3
"""
Produces the res/menu_*.bmp icons shown against each tray context-menu item
(src/tray.cpp, via SetMenuItemBitmaps).

Plain black-on-white silhouettes, deliberately not colored -- confirmed on
real Windows 98 hardware that color fills (a blue info circle, a red exit
arrow) dither noticeably on a limited-color display, and a flat single-color
shape reads far more clearly at this size regardless of color depth.

13x13, not 16x16: that's the classic Win9x SM_CXMENUCHECK default, and the
first version of these (16x16) was overflowing that and getting clipped by
the menu's own icon-column margin on real hardware. Drawn with generous
margins even at 13x13, since exactly matching a system metric here is a
guess, not a guarantee, across every classic/theme configuration.

SetMenuItemBitmaps -- unlike the newer MENUITEMINFO::hbmpItem, which
Windows 2000 introduced and Win9x/ME never got -- just BitBlts the bitmap
directly onto the menu with no masking, so these are generated against
plain white to match the default classic Windows Menu background color.

Drawn at 8x supersampling and downsized with Lanczos for a lightly
anti-aliased edge (same technique as tools/make_about_logo.py), then saved
as a classic (non-V4/V5) 24-bit BMP -- verify with `file` that it still
reports "Windows 3.x format" after any changes here.

Requires Pillow: python3 -m pip install --break-system-packages pillow
"""
import math
from PIL import Image, ImageDraw

SUPERSAMPLE = 8
SIZE = 13
CANVAS = SIZE * SUPERSAMPLE
WHITE = (255, 255, 255)
BLACK = (0, 0, 0)
OUTPUT_DIR = "res/"


def _canvas():
    return Image.new("RGB", (CANVAS, CANVAS), WHITE)


def _save(img, name):
    path = f"{OUTPUT_DIR}{name}.bmp"
    img.resize((SIZE, SIZE), Image.LANCZOS).save(path, "BMP")
    print(f"wrote {path} ({SIZE}x{SIZE}, 24-bit)")


def gear(color=BLACK):
    """Settings: a round cog body with radiating teeth and a hollow center.

    The body is a true circle (not a low-vertex polygon) specifically so it
    still reads as round at 13x13 -- an earlier version built the whole
    silhouette from an 8-point alternating-radius polygon, which looked like
    a diamond/star instead of a gear on real hardware.
    """
    img = _canvas()
    d = ImageDraw.Draw(img)
    cx = cy = CANVAS / 2
    r_body = CANVAS * 0.30
    r_hole = CANVAS * 0.13
    d.ellipse([cx - r_body, cy - r_body, cx + r_body, cy + r_body], fill=color)
    n_teeth = 8
    tooth_w = CANVAS * 0.11
    tooth_len = CANVAS * 0.10
    for i in range(n_teeth):
        angle = math.radians(i * (360 / n_teeth))
        mx = cx + math.cos(angle) * (r_body + tooth_len * 0.35)
        my = cy + math.sin(angle) * (r_body + tooth_len * 0.35)
        perp = angle + math.pi / 2
        dx, dy = math.cos(perp) * tooth_w / 2, math.sin(perp) * tooth_w / 2
        ldx, ldy = math.cos(angle) * tooth_len / 2, math.sin(angle) * tooth_len / 2
        pts = [
            (mx - dx - ldx, my - dy - ldy),
            (mx + dx - ldx, my + dy - ldy),
            (mx + dx + ldx, my + dy + ldy),
            (mx - dx + ldx, my - dy + ldy),
        ]
        d.polygon(pts, fill=color)
    d.ellipse([cx - r_hole, cy - r_hole, cx + r_hole, cy + r_hole], fill=WHITE)
    return img


def info(color=BLACK):
    """About: a solid circle with the "i" cut out in white, silhouette-style."""
    img = _canvas()
    d = ImageDraw.Draw(img)
    cx = cy = CANVAS / 2
    r = CANVAS * 0.32
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=color)
    dot_r = CANVAS * 0.045
    d.ellipse([cx - dot_r, cy - CANVAS * 0.16 - dot_r, cx + dot_r, cy - CANVAS * 0.16 + dot_r], fill=WHITE)
    stem_w = CANVAS * 0.06
    d.rectangle([cx - stem_w, cy - CANVAS * 0.03, cx + stem_w, cy + CANVAS * 0.19], fill=WHITE)
    return img


def exit_icon(color=BLACK):
    """Exit: an open doorway with an arrow passing through it."""
    img = _canvas()
    d = ImageDraw.Draw(img)
    x0, y0, x1, y1 = CANVAS * 0.20, CANVAS * 0.18, CANVAS * 0.52, CANVAS * 0.82
    d.rectangle([x0, y0, x1, y1], outline=color, width=max(1, int(CANVAS * 0.075)))
    kx, ky, kr = x1 - CANVAS * 0.08, (y0 + y1) / 2, CANVAS * 0.03
    d.ellipse([kx - kr, ky - kr, kx + kr, ky + kr], fill=color)
    ay = CANVAS * 0.5
    ax0, ax1 = CANVAS * 0.58, CANVAS * 0.82
    w = CANVAS * 0.075
    d.line([(ax0, ay), (ax1, ay)], fill=color, width=int(w))
    d.polygon(
        [
            (ax1 - CANVAS * 0.02, ay - CANVAS * 0.13),
            (ax1 + CANVAS * 0.12, ay),
            (ax1 - CANVAS * 0.02, ay + CANVAS * 0.13),
        ],
        fill=color,
    )
    return img


def note(color=BLACK):
    """Sample pack submenu: a single eighth note."""
    img = _canvas()
    d = ImageDraw.Draw(img)
    nx, ny, nrx, nry = CANVAS * 0.32, CANVAS * 0.70, CANVAS * 0.15, CANVAS * 0.12
    d.ellipse([nx - nrx, ny - nry, nx + nrx, ny + nry], fill=color)
    stem_x = nx + nrx * 0.85
    d.line([(stem_x, ny), (stem_x, CANVAS * 0.20)], fill=color, width=max(1, int(CANVAS * 0.06)))
    d.polygon(
        [(stem_x, CANVAS * 0.20), (stem_x + CANVAS * 0.24, CANVAS * 0.32), (stem_x, CANVAS * 0.44)],
        fill=color,
    )
    return img


def power(on, color=BLACK):
    """Run at Windows Startup: the standard power symbol for both states,
    plus a small solid corner badge when enabled.

    An earlier version tried to distinguish on/off with color (green vs
    gray) and then, once forced to one color, by inverting the glyph to a
    filled disc -- the inverted-fill version doesn't read as a power symbol
    at all at this size. A badge dot keeps the same recognizable glyph in
    both states and just adds an unambiguous "enabled" marker.
    """
    img = _canvas()
    d = ImageDraw.Draw(img)
    cx = cy = CANVAS / 2
    r = CANVAS * 0.26
    w = max(1, int(CANVAS * 0.09))
    d.arc([cx - r, cy - r, cx + r, cy + r], start=-55, end=235, fill=color, width=w)
    d.line([(cx, cy - r * 1.2), (cx, cy - r * 0.1)], fill=color, width=w)
    if on:
        br = CANVAS * 0.11
        bx, by = cx + r * 0.95, cy + r * 0.95
        d.ellipse([bx - br, by - br, bx + br, by + br], fill=color)
    return img


if __name__ == "__main__":
    _save(gear(), "menu_settings")
    _save(info(), "menu_about")
    _save(exit_icon(), "menu_exit")
    _save(note(), "menu_sample")
    _save(power(on=True), "menu_autostart_on")
    _save(power(on=False), "menu_autostart_off")

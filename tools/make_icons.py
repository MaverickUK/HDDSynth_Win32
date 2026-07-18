#!/usr/bin/env python3
"""
Generates res/gray.ico and res/green.ico: 16x16, 32bpp with real per-pixel
alpha, plus a classic 1bpp AND mask derived from that alpha (thresholded)
for backward compatibility.

Windows 2000+ renders the alpha channel directly, giving smooth anti-
aliased edges. Win9x-era shell32 ignores the alpha channel entirely and
falls back to the AND mask, giving the same hard-edged look this file
used to produce outright -- so one icon file serves both build targets,
each rendering at its own OS's ceiling.

Draws a simple filled disc (drive platter look) in the given RGB color
with a dark outline, transparent outside the circle.
"""
import struct
import sys

SIZE = 16


def make_icon_image(rgb):
    r, g, b = rgb
    outline = (40, 40, 40)
    cx = cy = (SIZE - 1) / 2.0
    radius = SIZE / 2.0 - 0.5

    # pixels[y][x] = (r, g, b, alpha 0-255)
    pixels = [[(0, 0, 0, 0)] * SIZE for _ in range(SIZE)]
    for y in range(SIZE):
        for x in range(SIZE):
            dx = x - cx
            dy = y - cy
            dist = (dx * dx + dy * dy) ** 0.5
            # Soft falloff over ~1px at the edge instead of a hard cutoff,
            # so Windows 2000+ (which respects this alpha channel) shows a
            # genuinely anti-aliased edge rather than the old binary one.
            alpha = max(0.0, min(1.0, radius - dist + 0.5))
            if alpha <= 0.0:
                continue
            color = outline if dist >= radius - 1.2 else (r, g, b)
            pixels[y][x] = (color[0], color[1], color[2], round(alpha * 255))

    # XOR (color) data: bottom-up rows, BGRA, no padding needed at 16px*4=64 bytes/row
    xor_data = bytearray()
    for y in range(SIZE - 1, -1, -1):
        for x in range(SIZE):
            pr, pg, pb, pa = pixels[y][x]
            xor_data += bytes((pb, pg, pr, pa))

    # AND mask: bottom-up rows, 1bpp, padded to 4-byte boundary per row.
    # Thresholded from alpha rather than a binary inside/outside test --
    # this is what makes the same file degrade correctly on Win9x.
    row_bytes = ((SIZE + 31) // 32) * 4
    and_data = bytearray()
    for y in range(SIZE - 1, -1, -1):
        row = bytearray(row_bytes)
        for x in range(SIZE):
            if pixels[y][x][3] < 128:
                row[x // 8] |= 0x80 >> (x % 8)
        and_data += row

    header = struct.pack(
        "<IiiHHIIiiII",
        40,              # biSize
        SIZE,            # biWidth
        SIZE * 2,        # biHeight (XOR + AND)
        1,                # biPlanes
        32,               # biBitCount
        0,                # biCompression (BI_RGB)
        len(xor_data) + len(and_data),  # biSizeImage
        0, 0,             # biXPelsPerMeter, biYPelsPerMeter
        0, 0,             # biClrUsed, biClrImportant
    )
    return bytes(header) + bytes(xor_data) + bytes(and_data)


def write_ico(path, rgb):
    image = make_icon_image(rgb)
    dir_header = struct.pack("<HHH", 0, 1, 1)
    entry = struct.pack(
        "<BBBBHHII",
        SIZE, SIZE, 0, 0,   # width, height, colorCount, reserved
        1, 32,               # planes, bitcount
        len(image),
        6 + 16,               # offset: 6-byte ICONDIR + one 16-byte ICONDIRENTRY
    )
    with open(path, "wb") as f:
        f.write(dir_header)
        f.write(entry)
        f.write(image)


if __name__ == "__main__":
    write_ico("res/gray.ico", (140, 140, 140))
    write_ico("res/green.ico", (40, 200, 60))
    print("wrote res/gray.ico and res/green.ico")

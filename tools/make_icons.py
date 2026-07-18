#!/usr/bin/env python3
"""
Generates res/gray.ico and res/green.ico: 16x16, 24bpp, 1bpp AND mask.
Deliberately hand-rolled (no Pillow dependency) and kept to the classic
ICO structure (no alpha channel) since that's what Win9x-era shell32
expects -- a modern PNG-chunked ICO would not render there.

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

    # pixels[y][x] = None (transparent) or (r,g,b)
    pixels = [[None] * SIZE for _ in range(SIZE)]
    for y in range(SIZE):
        for x in range(SIZE):
            dx = x - cx
            dy = y - cy
            dist = (dx * dx + dy * dy) ** 0.5
            if dist <= radius:
                pixels[y][x] = outline if dist >= radius - 1.2 else (r, g, b)

    # XOR (color) data: bottom-up rows, BGR, no padding needed at 16px*3=48 bytes/row
    xor_data = bytearray()
    for y in range(SIZE - 1, -1, -1):
        for x in range(SIZE):
            px = pixels[y][x]
            if px is None:
                xor_data += bytes((0, 0, 0))
            else:
                pr, pg, pb = px
                xor_data += bytes((pb, pg, pr))

    # AND mask: bottom-up rows, 1bpp, padded to 4-byte boundary per row
    row_bytes = ((SIZE + 31) // 32) * 4
    and_data = bytearray()
    for y in range(SIZE - 1, -1, -1):
        row = bytearray(row_bytes)
        for x in range(SIZE):
            if pixels[y][x] is None:
                row[x // 8] |= 0x80 >> (x % 8)
        and_data += row

    header = struct.pack(
        "<IiiHHIIiiII",
        40,              # biSize
        SIZE,            # biWidth
        SIZE * 2,        # biHeight (XOR + AND)
        1,                # biPlanes
        24,               # biBitCount
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
        1, 24,               # planes, bitcount
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

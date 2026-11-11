#!/usr/bin/env python3
"""Convert a raw /dev/graphics/fb0 dump into a PNG.

The Vita panel path is 960x544 with 32 bpp pixels.  The kernel console and
Mesa both write RGBA in memory, but the sysfs `bits_per_pixel` only reports
32, not the channel order, so the order is an option.

Usage:
    python scripts/fb0-to-png.py build/ui-test/<stamp>/fb0.raw out.png
    python scripts/fb0-to-png.py fb0.raw out.png --format bgra

This uses only the standard library; Pillow is not required.
"""

import argparse
import struct
import sys
import zlib


def png_chunk(tag, data):
    payload = tag + data
    return (
        struct.pack(">I", len(data))
        + payload
        + struct.pack(">I", zlib.crc32(payload) & 0xFFFFFFFF)
    )


def pixel_to_rgb(pixel, order):
    """Return (r, g, b) for one 4-byte pixel."""
    a, b, c, d = pixel
    if order == "rgba":
        return a, b, c
    if order == "bgra":
        return c, b, a
    if order == "rgbx":
        return a, b, c
    if order == "xbgr":
        return c, b, a
    raise ValueError(order)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("raw", help="raw framebuffer dump")
    parser.add_argument("png", help="output PNG path")
    parser.add_argument("--width", type=int, default=960)
    parser.add_argument("--height", type=int, default=544)
    parser.add_argument(
        "--format",
        choices=("rgba", "bgra", "rgbx", "xbgr"),
        default="rgba",
        help="channel order in memory (default: rgba)",
    )
    args = parser.parse_args()

    expected = args.width * args.height * 4
    with open(args.raw, "rb") as handle:
        data = handle.read()
    if len(data) < expected:
        sys.exit(
            "fb0-to-png: {} is {} bytes, need {} for {}x{}x4".format(
                args.raw, len(data), expected, args.width, args.height
            )
        )

    rows = []
    stride = args.width * 4
    for y in range(args.height):
        row = data[y * stride : (y + 1) * stride]
        rgb = bytearray(args.width * 3)
        out = 0
        for x in range(args.width):
            r, g, b = pixel_to_rgb(row[x * 4 : x * 4 + 4], args.format)
            rgb[out] = r
            rgb[out + 1] = g
            rgb[out + 2] = b
            out += 3
        rows.append(b"\x00" + bytes(rgb))

    ihdr = struct.pack(">IIBBBBB", args.width, args.height, 8, 2, 0, 0, 0)
    png = (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", ihdr)
        + png_chunk(b"IDAT", zlib.compress(b"".join(rows), 9))
        + png_chunk(b"IEND", b"")
    )
    with open(args.png, "wb") as handle:
        handle.write(png)
    print(
        "fb0-to-png: {} {}x{} {} -> {}".format(
            args.raw, args.width, args.height, args.format, args.png
        )
    )


if __name__ == "__main__":
    main()

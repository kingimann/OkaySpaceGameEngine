#!/usr/bin/env python3
"""Regenerates the sample pictures in this folder (our own procedural pictures, no outside assets).
Usage: python3 examples/texture/make_samples.py   (standard library only)"""
import math, os, struct, zlib

HERE = os.path.dirname(os.path.abspath(__file__))

def png(path, w, h, px):
    raw = b"".join(b"\x00" + bytes(c for p in px[y * w:(y + 1) * w] for c in p) for y in range(h))
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) \
        + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    open(os.path.join(HERE, path), "wb").write(data)

# 128x128 colour checker with thin lines: the kind of picture that shows how well a filter keeps detail
px = []
for y in range(128):
    for x in range(128):
        cell = ((x // 16) + (y // 16)) & 1
        line = (x % 16 == 0) or (y % 16 == 0)
        if line: px.append((20, 20, 20, 255))
        elif cell: px.append((230, 90, 40, 255))
        else: px.append((40, 120, 230, 255))
png("checker_color.png", 128, 128, px)

# 64x64 fence: opaque bars with transparent gaps (alpha test)
px = []
for y in range(64):
    for x in range(64):
        bar = (x % 8) < 3 or (y % 16) < 2
        px.append((120, 80, 40, 255) if bar else (120, 80, 40, 0))
png("fence_cutout.png", 64, 64, px)

# 64x64 tangent-space normal map of a few round bumps
px = []
for y in range(64):
    for x in range(64):
        fx = math.cos(x * 0.5) * 0.5; fy = math.cos(y * 0.5) * 0.5
        nx, ny, nz = fx, fy, 1.0
        l = math.sqrt(nx * nx + ny * ny + nz * nz)
        px.append(tuple(int(round((c / l * 0.5 + 0.5) * 255)) for c in (nx, ny, nz)) + (255,))
png("bumps_normal.png", 64, 64, px)

# three small icons of odd sizes for the atlas
def icon(name, w, h, col):
    px = []
    for y in range(h):
        for x in range(w):
            dx, dy = (x - (w - 1) / 2) / (w / 2), (y - (h - 1) / 2) / (h / 2)
            inside = dx * dx + dy * dy <= 1.0
            px.append(col + (255,) if inside else (0, 0, 0, 0))
    png(name, w, h, px)
icon("icon_a.png", 20, 14, (220, 40, 60))
icon("icon_b.png", 31, 31, (40, 200, 90))
icon("icon_c.png", 9, 40, (60, 80, 230))
print("wrote the sample pictures in", HERE)

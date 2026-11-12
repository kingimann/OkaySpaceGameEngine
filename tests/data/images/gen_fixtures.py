#!/usr/bin/env python3
"""Regenerates the image decoder fixtures in this directory.

Needs Pillow (libjpeg/libpng based) and ImageMagick's `convert`; the committed output is
what the tests use, so the tools are only needed to refresh or extend the set:

    python3 gen_fixtures.py <output-dir>

Every fixture is tiny (a few hundred bytes to a few KB) and deliberately awkward: odd
sizes, every PNG colour type / bit depth, interlacing, tRNS, all five PNG filters,
stored and fixed-Huffman deflate blocks, JPEG baseline/progressive with each chroma
subsampling and restart markers, BMP/TGA/GIF/HDR/PSD variants.
"""
import os
import struct
import subprocess
import sys

import numpy as np
from PIL import Image

OUT = sys.argv[1] if len(sys.argv) > 1 else "."
os.makedirs(OUT, exist_ok=True)
rng = np.random.default_rng(20240607)


def path(name):
    return os.path.join(OUT, name)


def pattern(w, h, noise=24, alpha=False):
    """Smooth gradients plus a hard-edged rectangle plus mild noise (RGBA uint8)."""
    y, x = np.mgrid[0:h, 0:w]
    r = x * 255 // max(w - 1, 1)
    g = y * 255 // max(h - 1, 1)
    b = (x * 5 + y * 9) % 256
    img = np.stack([r, g, b], -1).astype(np.int32)
    img[h // 4: h // 2 + 1, w // 4: w // 2 + 1] = [250, 20, 40]
    img += rng.integers(0, noise + 1, size=img.shape)
    a = np.full((h, w), 255)
    if alpha:
        a = (x * 255 // max(w - 1, 1) + y * 3) % 256
    return np.dstack([np.clip(img, 0, 255), a]).astype(np.uint8)


def im(args):
    subprocess.run(["convert"] + args, check=True)


SIZES = [(1, 1), (5, 3), (16, 16), (31, 17)]

# ---------------------------------------------------------------------------------------------
# PNG
# ---------------------------------------------------------------------------------------------
for (w, h) in SIZES:
    p = pattern(w, h, alpha=True)
    Image.fromarray(p[..., :3], "RGB").save(path(f"png_rgb8_{w}x{h}.png"))
    Image.fromarray(p, "RGBA").save(path(f"png_rgba8_{w}x{h}.png"))
    g = Image.fromarray(p[..., :3], "RGB").convert("L")
    g.save(path(f"png_gray8_{w}x{h}.png"))
    Image.merge("LA", (g, Image.fromarray(p[..., 3], "L"))).save(path(f"png_graya8_{w}x{h}.png"))
    pal = Image.fromarray(p[..., :3], "RGB").quantize(256)
    pal.save(path(f"png_pal8_{w}x{h}.png"))
    for bits, colors in ((1, 2), (2, 4), (4, 16)):
        q = Image.fromarray(p[..., :3], "RGB").quantize(colors)
        q.save(path(f"png_pal{bits}_{w}x{h}.png"), bits=bits)
    g.convert("1").save(path(f"png_gray1_{w}x{h}.png"))

# tRNS: palette alpha list, grey key, RGB key
p = pattern(16, 16)
q = Image.fromarray(p[..., :3], "RGB").quantize(16)
q.save(path("png_pal4_trns_16x16.png"), bits=4, transparency=bytes([0, 40, 80, 120, 160, 200, 255, 10]))
Image.fromarray(p[..., :3], "RGB").quantize(256).save(path("png_pal8_trns1_16x16.png"), transparency=3)
gs = Image.fromarray(p[..., :3], "RGB").convert("L")
gs.save(path("png_gray8_trns_16x16.png"), transparency=int(np.array(gs)[8, 8]))
rg = Image.fromarray(p[..., :3], "RGB")
rg.save(path("png_rgb8_trns_16x16.png"), transparency=tuple(int(v) for v in np.array(rg)[8, 8]))

# Exotic depths through ImageMagick: 16-bit, 2/4-bit grey, grey+alpha, interlaced, filters, deflate modes
src8 = path("_src.png")
for (w, h) in [(5, 3), (16, 16), (31, 17)]:
    p = pattern(w, h, alpha=True)
    Image.fromarray(p, "RGBA").save(src8)
    rgbsrc = path("_srcrgb.png")
    Image.fromarray(p[..., :3], "RGB").save(rgbsrc)
    im([rgbsrc, "-colorspace", "Gray", "-depth", "16", "-define", "png:color-type=0", "-define", "png:bit-depth=16",
        path(f"png_gray16_{w}x{h}.png")])
    im([rgbsrc, "-depth", "16", "-define", "png:color-type=2", "-define", "png:bit-depth=16", path(f"png_rgb16_{w}x{h}.png")])
    im([src8, "-depth", "16", "-define", "png:color-type=6", "-define", "png:bit-depth=16", path(f"png_rgba16_{w}x{h}.png")])
    im([src8, "-colorspace", "Gray", "-depth", "16", "-define", "png:color-type=4", "-define", "png:bit-depth=16",
        path(f"png_graya16_{w}x{h}.png")])
    im([src8, "-colorspace", "Gray", "-depth", "8", "-define", "png:color-type=4", "-define", "png:bit-depth=8",
        path(f"png_graya8_im_{w}x{h}.png")])
    for d in (2, 4):
        im([rgbsrc, "-colorspace", "Gray", "-depth", str(d), "-define", "png:color-type=0", "-define", f"png:bit-depth={d}",
            path(f"png_gray{d}_{w}x{h}.png")])
    # interlaced (Adam7) across types
    im([rgbsrc, "-interlace", "PNG", "-define", "png:color-type=2", path(f"png_rgb8_adam7_{w}x{h}.png")])
    im([src8, "-interlace", "PNG", "-define", "png:color-type=6", path(f"png_rgba8_adam7_{w}x{h}.png")])
    im([rgbsrc, "-colorspace", "Gray", "-interlace", "PNG", "-define", "png:color-type=0", "-define", "png:bit-depth=8",
        path(f"png_gray8_adam7_{w}x{h}.png")])
    im([rgbsrc, "-colors", "16", "-interlace", "PNG", "-define", "png:color-type=3", "-define", "png:bit-depth=4",
        path(f"png_pal4_adam7_{w}x{h}.png")])
    im([rgbsrc, "-colorspace", "Gray", "-depth", "2", "-interlace", "PNG", "-define", "png:color-type=0",
        "-define", "png:bit-depth=2", path(f"png_gray2_adam7_{w}x{h}.png")])
    im([src8, "-colorspace", "Gray", "-depth", "16", "-interlace", "PNG", "-define", "png:color-type=4",
        "-define", "png:bit-depth=16", path(f"png_graya16_adam7_{w}x{h}.png")])
    im([src8, "-depth", "16", "-interlace", "PNG", "-define", "png:color-type=6", "-define", "png:bit-depth=16",
        path(f"png_rgba16_adam7_{w}x{h}.png")])
    if (w, h) == (31, 17):
        for f, name in enumerate(["none", "sub", "up", "avg", "paeth"]):
            im([rgbsrc, "-define", f"png:compression-filter={f}", "-define", "png:color-type=2", path(f"png_rgb8_filter_{name}_{w}x{h}.png")])
            im([src8, "-define", f"png:compression-filter={f}", "-define", "png:color-type=6", path(f"png_rgba8_filter_{name}_{w}x{h}.png")])
        im([rgbsrc, "-define", "png:compression-level=0", "-define", "png:color-type=2", path(f"png_rgb8_stored_{w}x{h}.png")])
        im([rgbsrc, "-define", "png:compression-strategy=4", "-define", "png:color-type=2", path(f"png_rgb8_fixedhuff_{w}x{h}.png")])
        im([rgbsrc, "-define", "png:compression-level=9", "-define", "png:color-type=2", path(f"png_rgb8_best_{w}x{h}.png")])
# 16-bit grey with a tRNS key and an RGB16 key (hand-patched below)
# Larger image so the deflate stream has long matches and several blocks.
big = np.zeros((96, 128, 4), np.uint8)
big[..., 0] = np.arange(128)[None, :] * 2
big[..., 1] = np.arange(96)[:, None] * 2
big[..., 2] = ((np.arange(128)[None, :] // 8 + np.arange(96)[:, None] // 8) % 2) * 255
big[..., 3] = 255
Image.fromarray(big, "RGBA").save(path("png_rgba8_big_128x96.png"), optimize=True)


def png_chunk(t, d):
    import zlib
    return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)


def png_raw(w, h, ctype, depth, rows, plte=None, trns=None):
    """Hand-built PNG with filter 0 rows (list of bytes)."""
    import zlib
    raw = b"".join(b"\x00" + r for r in rows)
    out = b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, depth, ctype, 0, 0, 0))
    if plte:
        out += png_chunk(b"PLTE", plte)
    if trns:
        out += png_chunk(b"tRNS", trns)
    return out + png_chunk(b"IDAT", zlib.compress(raw)) + png_chunk(b"IEND", b"")


# 16-bit grey with key, 16-bit RGB with key (4x2)
rows = [struct.pack(">4H", 0, 1000, 65535, 40000), struct.pack(">4H", 40000, 0, 1000, 65535)]
open(path("png_gray16_trns_4x2.png"), "wb").write(png_raw(4, 2, 0, 16, rows, trns=struct.pack(">H", 40000)))
rows = [struct.pack(">12H", 1, 2, 3, 40000, 50000, 60000, 65535, 0, 7, 9, 9, 9),
        struct.pack(">12H", 40000, 50000, 60000, 1, 2, 3, 40000, 50000, 60001, 10, 20, 30)]
open(path("png_rgb16_trns_4x2.png"), "wb").write(png_raw(4, 2, 2, 16, rows, trns=struct.pack(">3H", 40000, 50000, 60000)))
# 1-bit grey with key, 2-bit palette (partial tRNS), unknown ancillary chunk + gAMA + PLTE with fewer entries than 2^depth
rows = [bytes([0b10110000]), bytes([0b01001000])]
open(path("png_gray1_trns_5x2.png"), "wb").write(png_raw(5, 2, 0, 1, rows, trns=struct.pack(">H", 1)))
rows = [bytes([0b00011011]), bytes([0b11100100])]
open(path("png_pal2_partial_4x2.png"), "wb").write(
    png_raw(4, 2, 3, 2, rows, plte=bytes([255, 0, 0, 0, 255, 0, 0, 0, 255, 9, 9, 9]), trns=bytes([128, 64])))

# ---------------------------------------------------------------------------------------------
# BMP
# ---------------------------------------------------------------------------------------------
for (w, h) in [(1, 1), (5, 3), (7, 6), (31, 17)]:
    p = pattern(w, h, alpha=True)
    Image.fromarray(p[..., :3], "RGB").save(path(f"bmp_rgb24_{w}x{h}.bmp"))
    Image.fromarray(p, "RGBA").save(path(f"bmp_rgba32_{w}x{h}.bmp"))
    pimg = Image.fromarray(p[..., :3], "RGB")
    pimg.quantize(256).save(path(f"bmp_pal8_{w}x{h}.bmp"))
    pimg.convert("L").save(path(f"bmp_gray8_{w}x{h}.bmp"))
    pimg.convert("1").save(path(f"bmp_mono1_{w}x{h}.bmp"))
    pimg.quantize(16).save(path(f"bmp_pal4_{w}x{h}.bmp"))
    rgbsrc = path("_srcrgb.png")
    pimg.save(rgbsrc)
    im([rgbsrc, "-colors", "16", "-depth", "8", "BMP3:" + path(f"bmp_pal4_im_{w}x{h}.bmp")])


def bmp_make(w, h, bpp, pix_rows, masks=None, hsz=40, topdown=False, palette=None, compression=0):
    """pix_rows: list of bytes per row in file order (already padded)."""
    palbytes = b"".join(bytes([b, g, r, 0]) for (r, g, b) in (palette or []))
    extra = b""
    if compression == 3 and hsz == 40:
        extra = struct.pack("<3I", *masks[:3])
    off = 14 + hsz + len(extra) + len(palbytes)
    data = b"".join(pix_rows)
    hdr = struct.pack("<2sIHHI", b"BM", off + len(data), 0, 0, off)
    dib = struct.pack("<IiiHHIIiiII", hsz, w, -h if topdown else h, 1, bpp, compression, len(data), 2835, 2835,
                      len(palette or []), 0)
    if hsz >= 108:
        dib += struct.pack("<4I", *masks)
        dib += struct.pack("<I", 0x73524742) + bytes(36) + struct.pack("<3I", 0, 0, 0)
        dib += bytes(hsz - len(dib))
    return hdr + dib[:hsz] + extra + palbytes + data


def pad4(b):
    return b + bytes((-len(b)) % 4)


w, h = 7, 5
p = pattern(w, h, alpha=True)
# 16-bit 565 (BI_BITFIELDS) and 555 (BI_RGB), bottom-up
def px565(r, g, b): return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
def px555(r, g, b): return ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)
rows565 = [pad4(b"".join(struct.pack("<H", px565(*p[y, x, :3])) for x in range(w))) for y in range(h - 1, -1, -1)]
open(path("bmp_rgb565_7x5.bmp"), "wb").write(bmp_make(w, h, 16, rows565, masks=(0xF800, 0x07E0, 0x001F), compression=3))
rows555 = [pad4(b"".join(struct.pack("<H", px555(*p[y, x, :3])) for x in range(w))) for y in range(h - 1, -1, -1)]
open(path("bmp_rgb555_7x5.bmp"), "wb").write(bmp_make(w, h, 16, rows555))
# 32-bit V4 header with alpha, 32-bit plain with zero alpha (treated as opaque), top-down 24-bit and 8-bit
rows32 = [b"".join(bytes([p[y, x, 2], p[y, x, 1], p[y, x, 0], p[y, x, 3]]) for x in range(w)) for y in range(h - 1, -1, -1)]
open(path("bmp_v4_rgba32_7x5.bmp"), "wb").write(
    bmp_make(w, h, 32, rows32, masks=(0xFF0000, 0xFF00, 0xFF, 0xFF000000), hsz=108, compression=3))
rows32z = [b"".join(bytes([p[y, x, 2], p[y, x, 1], p[y, x, 0], 0]) for x in range(w)) for y in range(h - 1, -1, -1)]
open(path("bmp_rgb32_zeroalpha_7x5.bmp"), "wb").write(bmp_make(w, h, 32, rows32z))
rows24 = [pad4(b"".join(bytes([p[y, x, 2], p[y, x, 1], p[y, x, 0]]) for x in range(w))) for y in range(h)]
open(path("bmp_rgb24_topdown_7x5.bmp"), "wb").write(bmp_make(w, h, 24, rows24, topdown=True))
pal = [(i * 3 % 256, i * 7 % 256, i * 11 % 256) for i in range(256)]
rows8 = [pad4(bytes((x * 5 + y * 9) % 256 for x in range(w))) for y in range(h)]
open(path("bmp_pal8_topdown_7x5.bmp"), "wb").write(bmp_make(w, h, 8, rows8, topdown=True, palette=pal))
# Not supported (must be rejected): RLE8 header claim, 8bpp with compression 1
open(path("bmp_rle8_reject.bmp"), "wb").write(bmp_make(w, h, 8, rows8, palette=pal, compression=1))

# ---------------------------------------------------------------------------------------------
# TGA
# ---------------------------------------------------------------------------------------------
for (w, h) in [(1, 1), (5, 3), (31, 17)]:
    p = pattern(w, h, alpha=True)
    rgb = Image.fromarray(p[..., :3], "RGB")
    for rle in (False, True):
        tag = "rle" if rle else "raw"
        rgb.save(path(f"tga_rgb24_{tag}_{w}x{h}.tga"), rle=rle)
        Image.fromarray(p, "RGBA").save(path(f"tga_rgba32_{tag}_{w}x{h}.tga"), rle=rle)
        rgb.convert("L").save(path(f"tga_gray8_{tag}_{w}x{h}.tga"), rle=rle)
    Image.fromarray(p, "RGBA").save(path(f"tga_rgba32_topleft_{w}x{h}.tga"), orientation=-1)
    rgb.quantize(64).save(path(f"tga_pal8_{w}x{h}.tga"))


def tga_make(w, h, imtype, bpp, data, desc=0, cmap=None, cmbits=0):
    ncm = len(cmap) // (cmbits // 8) if cmap else 0
    hdr = struct.pack("<BBBHHBHHHHBB", 0, 1 if cmap else 0, imtype, 0, ncm, cmbits if cmap else 0, 0, 0, w, h, bpp, desc)
    return hdr + (cmap or b"") + data


w, h = 6, 4
p = pattern(w, h, alpha=True)
d16 = b"".join(struct.pack("<H", ((p[y, x, 0] >> 3) << 10) | ((p[y, x, 1] >> 3) << 5) | (p[y, x, 2] >> 3)) for y in range(h - 1, -1, -1) for x in range(w))
open(path("tga_rgb16_raw_6x4.tga"), "wb").write(tga_make(w, h, 2, 16, d16))
# 16-bit RLE: one repeat packet then raw packets
d = b""
rowsx = [b"".join(struct.pack("<H", ((p[y, x, 0] >> 3) << 10) | ((p[y, x, 1] >> 3) << 5) | (p[y, x, 2] >> 3)) for x in range(w)) for y in range(h)]
for y in range(h - 1, -1, -1):
    d += bytes([0x80 | 1]) + rowsx[y][:2] + bytes([w - 3]) + rowsx[y][4:]  # run of 2 identical? (first px twice, then raw tail)
open(path("tga_rgb16_rle_6x4.tga"), "wb").write(tga_make(w, h, 10, 16, d))
# grey+alpha 16-bit
dga = b"".join(bytes([p[y, x, 0], p[y, x, 3]]) for y in range(h - 1, -1, -1) for x in range(w))
open(path("tga_graya16_6x4.tga"), "wb").write(tga_make(w, h, 3, 16, dga))
# colour-mapped, 16-bit palette entries, 8-bit indices
cm = b"".join(struct.pack("<H", (i * 2049) & 0x7FFF) for i in range(16))
idx = bytes((x + y * 3) % 16 for y in range(h - 1, -1, -1) for x in range(w))
open(path("tga_pal16bit_6x4.tga"), "wb").write(tga_make(w, h, 1, 8, idx, cmap=cm, cmbits=16))
# colour-mapped, 24-bit entries, 16-bit indices, RLE (type 9)
cm = b"".join(bytes([i * 5 % 256, i * 9 % 256, i * 13 % 256]) for i in range(32))
d = b""
for y in range(h - 1, -1, -1):
    d += bytes([w - 1]) + b"".join(struct.pack("<H", (x + y) % 32) for x in range(w))
open(path("tga_pal24_idx16_rle_6x4.tga"), "wb").write(tga_make(w, h, 9, 16, d, cmap=cm, cmbits=24))

# ---------------------------------------------------------------------------------------------
# GIF (static, transparent, interlaced, animated with every disposal method and sub-rect frames)
# ---------------------------------------------------------------------------------------------
for (w, h) in [(1, 1), (7, 5), (32, 20)]:
    p = pattern(w, h)
    pi = Image.fromarray(p[..., :3], "RGB").quantize(32)
    pi.save(path(f"gif_static_{w}x{h}.gif"))
    pi.save(path(f"gif_trans_{w}x{h}.gif"), transparency=3)
    pi.save(path(f"gif_interlaced_{w}x{h}.gif"), interlace=True)
    pi.save(path(f"gif_noninterlaced_{w}x{h}.gif"), interlace=False)
for disp in (0, 1, 2, 3):
    frames = []
    for i in range(4):
        base = np.zeros((20, 24, 4), np.uint8)
        base[..., 3] = 0
        x0 = i * 4
        base[4:12, x0:x0 + 8] = [255 - i * 60, 40 + i * 50, 90, 255]
        base[14:18, 2:6 + i] = [20, 200, 100, 255]
        frames.append(Image.fromarray(base, "RGBA"))
    frames[0].save(path(f"gif_anim_disposal{disp}.gif"), save_all=True, append_images=frames[1:], duration=[40, 70, 100, 130],
                   loop=0, disposal=disp, transparency=0)
fr = [Image.fromarray(pattern(24, 20, noise=60)[..., :3], "RGB").quantize(64) for _ in range(3)]
fr[0].save(path("gif_anim_opaque.gif"), save_all=True, append_images=fr[1:], duration=[20, 50, 80], loop=0, optimize=False)

# ---------------------------------------------------------------------------------------------
# HDR (Radiance) and PSD through ImageMagick
# ---------------------------------------------------------------------------------------------
for (w, h) in [(5, 3), (8, 4), (33, 17)]:
    p = pattern(w, h, noise=30)
    Image.fromarray(p[..., :3], "RGB").save(path("_srcrgb.png"))
    im([path("_srcrgb.png"), "-colorspace", "RGB", path(f"hdr_{w}x{h}.hdr")])
for (w, h) in [(5, 3), (16, 16)]:
    p = pattern(w, h, noise=30, alpha=True)
    Image.fromarray(p, "RGBA").save(path("_src.png"))
    Image.fromarray(p[..., :3], "RGB").save(path("_srcrgb.png"))
    for comp in ("None", "RLE"):
        im([path("_srcrgb.png"), "-compress", comp, "-depth", "8", path(f"psd_rgb8_{comp.lower()}_{w}x{h}.psd")])
        im([path("_srcrgb.png"), "-compress", comp, "-depth", "16", path(f"psd_rgb16_{comp.lower()}_{w}x{h}.psd")])
        im([path("_src.png"), "-compress", comp, "-depth", "8", path(f"psd_rgba8_{comp.lower()}_{w}x{h}.psd")])

# ---------------------------------------------------------------------------------------------
# JPEG: PIL (libjpeg) and ImageMagick
# ---------------------------------------------------------------------------------------------
jsizes = [(1, 1), (7, 5), (17, 13), (40, 30), (33, 31)]
for (w, h) in jsizes:
    rgb = Image.fromarray(pattern(w, h, noise=10)[..., :3], "RGB")
    for sub, name in ((0, "444"), (1, "422"), (2, "420")):
        rgb.save(path(f"jpg_base_{name}_{w}x{h}.jpg"), quality=85, subsampling=sub)
        rgb.save(path(f"jpg_prog_{name}_{w}x{h}.jpg"), quality=85, subsampling=sub, progressive=True)
    rgb.convert("L").save(path(f"jpg_gray_{w}x{h}.jpg"), quality=90)
    rgb.convert("L").save(path(f"jpg_gray_prog_{w}x{h}.jpg"), quality=90, progressive=True)
rgb = Image.fromarray(pattern(40, 30, noise=10)[..., :3], "RGB")
rgb.save(path("jpg_q10_420_40x30.jpg"), quality=10, subsampling=2)
rgb.save(path("jpg_q100_444_40x30.jpg"), quality=100, subsampling=0)
rgb.save(path("jpg_optimized_420_40x30.jpg"), quality=75, subsampling=2, optimize=True)
rgb.save(path("jpg_restart_blocks_420_40x30.jpg"), quality=85, subsampling=2, restart_marker_blocks=2)
rgb.save(path("jpg_restart_rows_444_40x30.jpg"), quality=85, subsampling=0, restart_marker_rows=1)
rgb.save(path("jpg_restart_prog_420_40x30.jpg"), quality=85, subsampling=2, progressive=True, restart_marker_blocks=3)
Image.fromarray(pattern(40, 30, noise=10)[..., :3], "RGB").convert("L").save(path("jpg_restart_gray_40x30.jpg"), quality=85, restart_marker_blocks=2)
big = Image.fromarray(pattern(97, 71, noise=10)[..., :3], "RGB")
big.save(path("jpg_prog_420_97x71.jpg"), quality=80, subsampling=2, progressive=True)
rgb.convert("CMYK").save(path("jpg_cmyk_reject_40x30.jpg"), quality=85)
rgb.save(path("jpg_icc_exif_420_40x30.jpg"), quality=85, subsampling=2, icc_profile=b"\x00" * 300, exif=Image.Exif().tobytes())
# Uncommon sampling factors via ImageMagick: 4:1:1, 4:4:0, 4:2:0 with 2x2 luma only, and a
# 3x1 style non-power-of-two ratio.
Image.fromarray(pattern(40, 30, noise=10)[..., :3], "RGB").save(path("_srcrgb.png"))
for samp, name in (("4x1,1x1,1x1", "411"), ("1x2,1x1,1x1", "440"), ("2x2,1x1,1x1", "420im"), ("2x1,1x1,1x1", "422im"),
                   ("4x2,1x1,1x1", "4x2"), ("1x1,1x1,1x1", "444im"), ("2x2,2x1,1x2", "mixed"), ("3x1,1x1,1x1", "3x1")):
    for prog in (False, True):
        args = [path("_srcrgb.png"), "-quality", "85", "-sampling-factor", samp]
        if prog:
            args += ["-interlace", "Plane"]
        try:
            im(args + [path(f"jpg_{'prog' if prog else 'base'}_{name}_40x30.jpg")])
        except subprocess.CalledProcessError:
            pass
# Non-square tall/wide, multi-MCU
Image.fromarray(pattern(8, 200, noise=10)[..., :3], "RGB").save(path("jpg_prog_420_8x200.jpg"), quality=85, subsampling=2, progressive=True)
Image.fromarray(pattern(200, 8, noise=10)[..., :3], "RGB").save(path("jpg_base_420_200x8.jpg"), quality=85, subsampling=2)

# Files the decoder must refuse: arithmetic coding (SOF9), 12-bit precision, truncated header.
data = open(path("jpg_base_420_40x30.jpg"), "rb").read()
i = data.index(b"\xff\xc0")
open(path("jpg_arith_reject.jpg"), "wb").write(data[:i + 1] + b"\xc9" + data[i + 2:])
open(path("jpg_12bit_reject.jpg"), "wb").write(data[:i + 4] + b"\x0c" + data[i + 5:])

# Adobe APP14 (transform 0 = RGB) without JFIF: components are RGB, not YCbCr.
j = data.index(b"\xff\xdb")
adobe = b"\xff\xee\x00\x0e" + b"Adobe" + struct.pack(">HHHB", 100, 0, 0, 0)
open(path("jpg_adobe_rgb_40x30.jpg"), "wb").write(data[:2] + adobe + data[j:])
adobe1 = b"\xff\xee\x00\x0e" + b"Adobe" + struct.pack(">HHHB", 100, 0, 0, 1)
open(path("jpg_adobe_ycc_40x30.jpg"), "wb").write(data[:2] + adobe1 + data[j:])

for f in os.listdir(OUT):
    if f.startswith("_src"):
        os.remove(path(f))
print(len(os.listdir(OUT)), "files")

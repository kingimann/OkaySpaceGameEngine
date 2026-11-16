#!/usr/bin/env python3
"""Recompress PNG files in place with zlib (the picture tools write uncompressed PNGs). Usage: recompress_png.py files..."""
import struct, sys, zlib

def chunks(data):
    p = 8
    while p < len(data):
        n, = struct.unpack(">I", data[p:p + 4])
        yield data[p + 4:p + 8], data[p + 8:p + 8 + n]
        p += 12 + n

def pack(t, d):
    return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)

for path in sys.argv[1:]:
    data = open(path, "rb").read()
    out, idat = [data[:8]], b""
    for t, d in chunks(data):
        if t == b"IDAT":
            idat += d
        elif t == b"IEND":
            out.append(pack(b"IDAT", zlib.compress(zlib.decompress(idat), 9)))
            out.append(pack(t, d))
        else:
            out.append(pack(t, d))
    open(path, "wb").write(b"".join(out))

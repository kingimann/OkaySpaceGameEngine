#!/usr/bin/env python3
"""Generate and check the embedded font data (shared/RobotoFontOwn.h, shared/JetBrainsMonoFontOwn.h).

The fonts are stored in our own small container, "OKF1" (documented in shared/OkfDecode.hpp):
    16-byte header  magic "OKF1", method (0 stored, 1 LZ), 3 zero bytes, raw size u32 LE, CRC-32 of the raw bytes u32 LE
    payload         the raw bytes (method 0) or an LZ4-style token stream (method 1)
then base64 (RFC 4648, with padding), cut into lines of 100 characters that become an array of string literals
(a single literal could exceed a compiler's string length limit).

Modes
  --from-legacy   One-time migration: read the old Dear ImGui base85 arrays (shared/RobotoFont.h and
                  shared/JetBrainsMonoFont.h), decode them with the reader below, print size and SHA-256 of
                  each font, and write the OKF1 headers. (The legacy headers were deleted once the editor and launcher
                  switched to OKF1 and AddFontFromMemoryTTF; to rerun this, restore them from git history.)
  --from-ttf NAME FILE   Encode a TrueType file as shared/<NAME>FontOwn.h (NAME = Roboto or JetBrainsMono).
  --check         Decode the generated headers with an independent Python decoder, compare size, CRC-32 and
                  SHA-256 with the pins in assets/fonts/embedded_fonts.sha256, and check that re-encoding gives the
                  same file (so the generator is deterministic). Exit status 1 on any mismatch. Also compares with the
                  legacy arrays when they are still present.
Run from the repository root:  python3 scripts/gen_embedded_fonts.py --check
"""
import base64
import hashlib
import os
import re
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONTS = {
    # name: (legacy header, legacy symbol, new header, C++ symbol)
    "Roboto": ("shared/RobotoFont.h", "RobotoMedium_compressed_data_base85", "shared/RobotoFontOwn.h", "kRobotoMediumOkf"),
    "JetBrainsMono": ("shared/JetBrainsMonoFont.h", "JetBrainsMono_compressed_data_base85", "shared/JetBrainsMonoFontOwn.h", "kJetBrainsMonoOkf"),
}
PINS = os.path.join(ROOT, "assets/fonts/embedded_fonts.sha256")
LINE = 100


# ---- reader for the legacy Dear ImGui format (base85 text -> compressed stream -> TTF); written from the format ----
def legacy_read(path, symbol):
    text = open(os.path.join(ROOT, path), encoding="utf-8").read()
    m = re.search(re.escape(symbol) + r"\[\d+\+1\]\s*=\s*((?:\s*\"(?:[^\"\\]|\\.)*\")+)\s*;", text)
    if not m:
        raise SystemExit("cannot find %s in %s" % (symbol, path))
    parts = re.findall(r"\"((?:[^\"\\]|\\.)*)\"", m.group(1))
    s = "".join(parts)
    s = re.sub(r"\\(.)", lambda mm: {"n": "\n", "t": "\t"}.get(mm.group(1), mm.group(1)), s)   # \\ and \" only occur
    raw = s.encode("latin-1")

    def d85(c):
        return c - 36 if c >= ord("\\") else c - 35
    comp = bytearray()
    for i in range(0, len(raw) - len(raw) % 5, 5):
        v = d85(raw[i]) + 85 * (d85(raw[i + 1]) + 85 * (d85(raw[i + 2]) + 85 * (d85(raw[i + 3]) + 85 * d85(raw[i + 4]))))
        comp += struct.pack("<I", v & 0xFFFFFFFF)
    return legacy_decompress(bytes(comp))


def legacy_decompress(i):
    be16 = lambda p: (i[p] << 8) | i[p + 1]
    be24 = lambda p: (i[p] << 16) | be16(p + 1)
    be32 = lambda p: (i[p] << 24) | be24(p + 1)
    if be32(0) != 0x57BC0000 or be32(4) != 0:
        raise SystemExit("not a Dear ImGui compressed stream")
    olen = be32(8)
    p = 16
    out = bytearray()

    def lit(pos, n):
        out.extend(i[pos:pos + n])

    def match(dist, n):
        if dist > len(out):
            raise SystemExit("bad match distance")
        for _ in range(n):
            out.append(out[-dist])
    while True:
        c = i[p]
        if c >= 0x20:
            if c >= 0x80:
                match(i[p + 1] + 1, c - 0x80 + 1); p += 2
            elif c >= 0x40:
                match(be16(p) - 0x4000 + 1, i[p + 2] + 1); p += 3
            else:
                n = c - 0x20 + 1; lit(p + 1, n); p += 1 + n
        else:
            if c >= 0x18:
                match(be24(p) - 0x180000 + 1, i[p + 3] + 1); p += 4
            elif c >= 0x10:
                match(be24(p) - 0x100000 + 1, be16(p + 3) + 1); p += 5
            elif c >= 0x08:
                n = be16(p) - 0x0800 + 1; lit(p + 2, n); p += 2 + n
            elif c == 0x07:
                n = be16(p + 1) + 1; lit(p + 3, n); p += 3 + n
            elif c == 0x06:
                match(be24(p + 1) + 1, i[p + 4] + 1); p += 5
            elif c == 0x04:
                match(be24(p + 1) + 1, be16(p + 4) + 1); p += 6
            else:
                if c == 0x05 and i[p + 1] == 0xFA and len(out) == olen:
                    return bytes(out)
                raise SystemExit("bad token / end marker")
        if len(out) > olen:
            raise SystemExit("output overrun")


# ---- OKF1 -------------------------------------------------------------------------------------------------------
def lz_compress(data):
    """Greedy LZ with hash chains, emitting the LZ4-style token stream described in shared/OkfDecode.hpp."""
    n = len(data)
    out = bytearray()
    head = {}
    prev = [-1] * n
    i = 0
    anchor = 0

    def emit(lits_from, lits_to, offset, mlen):
        ll = lits_to - lits_from
        tok = (min(ll, 15) << 4) | (min(mlen - 4, 15) if offset else 0)
        out.append(tok)
        if ll >= 15:
            r = ll - 15
            while r >= 255:
                out.append(255); r -= 255
            out.append(r)
        out.extend(data[lits_from:lits_to])
        if offset:
            out.extend(struct.pack("<H", offset))
            if mlen - 4 >= 15:
                r = mlen - 4 - 15
                while r >= 255:
                    out.append(255); r -= 255
                out.append(r)

    def key(p):
        return data[p:p + 4]
    while i + 4 <= n:
        k = key(i)
        best_len, best_off = 0, 0
        cand = head.get(k, -1)
        tries = 0
        while cand >= 0 and i - cand <= 65535 and tries < 64:
            l = 0
            while i + l < n and data[cand + l] == data[i + l] and l < 65535:
                l += 1
            if l > best_len:
                best_len, best_off = l, i - cand
            cand = prev[cand]
            tries += 1
        if best_len >= 4:
            emit(anchor, i, best_off, best_len)
            for p in range(i, min(i + best_len, n - 3)):
                kk = key(p)
                prev[p] = head.get(kk, -1)
                head[kk] = p
            i += best_len
            anchor = i
        else:
            prev[i] = head.get(k, -1)
            head[k] = i
            i += 1
    emit(anchor, n, 0, 0)      # trailing literals (possibly none): the last token has no match
    return bytes(out)


def pack(raw):
    crc = zlib.crc32(raw) & 0xFFFFFFFF
    lz = lz_compress(raw)
    if len(lz) < len(raw):
        method, payload = 1, lz
    else:
        method, payload = 0, raw
    return b"OKF1" + bytes([method, 0, 0, 0]) + struct.pack("<II", len(raw), crc) + payload


def unpack(buf):
    """Independent reader for OKF1 (the C++ one is shared/OkfDecode.hpp)."""
    if len(buf) < 16 or buf[:4] != b"OKF1" or buf[5:8] != b"\0\0\0":
        raise SystemExit("bad OKF1 header")
    method = buf[4]
    rawsize, crc = struct.unpack("<II", buf[8:16])
    body = buf[16:]
    if method == 0:
        out = bytearray(body)
    elif method == 1:
        out = bytearray()
        p = 0
        while len(out) < rawsize:
            t = body[p]; p += 1
            ll = t >> 4
            if ll == 15:
                while True:
                    b = body[p]; p += 1; ll += b
                    if b != 255:
                        break
            out += body[p:p + ll]; p += ll
            if len(out) >= rawsize:
                break
            off = body[p] | (body[p + 1] << 8); p += 2
            ml = (t & 15) + 4
            if (t & 15) == 15:
                while True:
                    b = body[p]; p += 1; ml += b
                    if b != 255:
                        break
            if off < 1 or off > len(out):
                raise SystemExit("bad offset")
            for _ in range(ml):
                out.append(out[-off])
        if p != len(body):
            raise SystemExit("trailing bytes")
    else:
        raise SystemExit("unknown method")
    if len(out) != rawsize or (zlib.crc32(bytes(out)) & 0xFFFFFFFF) != crc:
        raise SystemExit("size or CRC mismatch")
    return bytes(out)


def header_text(name, symbol, raw, packed, sha):
    b64 = base64.b64encode(packed).decode("ascii")
    lines = [b64[i:i + LINE] for i in range(0, len(b64), LINE)]
    out = []
    out.append("// GENERATED by scripts/gen_embedded_fonts.py - do not edit by hand.")
    out.append("// %s (TrueType, %d bytes, SHA-256 %s) in our own container, OKF1:" % (name, len(raw), sha))
    out.append("// a 16-byte header, then the bytes stored or LZ-packed, all of it base64 (RFC 4648) cut into lines.")
    out.append("// The format and its decoder are in shared/OkfDecode.hpp. The font's licence: see docs/THIRD_PARTY.md.")
    out.append("#pragma once")
    out.append("")
    out.append("namespace okf_data {")
    out.append("inline constexpr const char* const %s[] = {" % symbol)
    for l in lines:
        out.append('    "%s",' % l)
    out.append("};")
    out.append("inline constexpr unsigned %sLines = %d;" % (symbol, len(lines)))
    out.append("inline constexpr unsigned %sRawBytes = %d;" % (symbol, len(raw)))
    out.append("} // namespace okf_data")
    out.append("")
    return "\n".join(out)


def read_new(path, symbol):
    text = open(os.path.join(ROOT, path), encoding="utf-8").read()
    m = re.search(re.escape(symbol) + r"\[\]\s*=\s*\{(.*?)\};", text, re.S)
    if not m:
        raise SystemExit("cannot find %s in %s" % (symbol, path))
    return base64.b64decode("".join(re.findall(r'"([^"]*)"', m.group(1))), validate=True)


def write_font(name, raw):
    _, _, path, symbol = FONTS[name]
    sha = hashlib.sha256(raw).hexdigest()
    packed = pack(raw)
    open(os.path.join(ROOT, path), "w", encoding="utf-8", newline="\n").write(header_text(name, symbol, raw, packed, sha))
    print("%-14s raw %7d bytes  sha256 %s  packed %7d bytes (%.1f%%)  -> %s" % (name, len(raw), sha, len(packed), 100.0 * len(packed) / len(raw), path))
    return sha


def read_pins():
    pins = {}
    if os.path.exists(PINS):
        for ln in open(PINS, encoding="utf-8"):
            ln = ln.split("#")[0].strip()
            if ln:
                sha, size, nm = ln.split()
                pins[nm] = (sha, int(size))
    return pins


def main(argv):
    if len(argv) >= 2 and argv[1] == "--from-legacy":
        pins = []
        for name, (lpath, lsym, _, _) in FONTS.items():
            raw = legacy_read(lpath, lsym)
            if raw[:4] not in (b"\x00\x01\x00\x00", b"OTTO", b"true"):
                raise SystemExit("%s does not decode to a TrueType file" % name)
            sha = write_font(name, raw)
            pins.append("%s %d %s" % (sha, len(raw), name))
        open(PINS, "w", encoding="utf-8", newline="\n").write(
            "# SHA-256 and size of each embedded font's decoded TrueType bytes (what rendering sees). Written by\n"
            "# scripts/gen_embedded_fonts.py --from-legacy from the original Dear ImGui arrays; checked by --check and by\n"
            "# tests/test_embedded_font.cpp. If a font is ever replaced on purpose, change this file in the same commit.\n"
            + "\n".join(pins) + "\n")
        return 0
    if len(argv) == 4 and argv[1] == "--from-ttf":
        if argv[2] not in FONTS:
            raise SystemExit("name must be one of %s" % ", ".join(FONTS))
        write_font(argv[2], open(argv[3], "rb").read())
        return 0
    if len(argv) == 2 and argv[1] == "--check":
        pins = read_pins()
        bad = 0
        for name, (lpath, lsym, path, symbol) in FONTS.items():
            packed = read_new(path, symbol)
            raw = unpack(packed)
            sha = hashlib.sha256(raw).hexdigest()
            ok = name in pins and pins[name] == (sha, len(raw))
            again = pack(raw) == packed
            line = "%-14s %7d bytes sha256 %s pins:%s regenerates-identically:%s" % (name, len(raw), sha[:16] + "...", "ok" if ok else "MISMATCH", "yes" if again else "NO")
            if os.path.exists(os.path.join(ROOT, lpath)):
                same = legacy_read(lpath, lsym) == raw
                line += " equals-legacy-array:%s" % ("yes" if same else "NO")
                ok = ok and same
            print(line)
            if not ok or not again:
                bad = 1
        return bad
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))

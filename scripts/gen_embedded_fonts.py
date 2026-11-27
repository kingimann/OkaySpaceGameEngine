#!/usr/bin/env python3
"""Generate and check the embedded font data (shared/RobotoFont.h, shared/JetBrainsMonoFont.h).

The fonts are stored in our own small container, "OKF1" (documented in shared/OkfDecode.hpp):
    16-byte header  magic "OKF1", method (0 stored, 1 LZ), 3 zero bytes, raw size u32 LE, CRC-32 of the raw bytes u32 LE
    payload         the raw bytes (method 0) or an LZ4-style token stream (method 1)
then base64 (RFC 4648, with padding), cut into lines of 100 characters that become an array of string literals
(a single literal could exceed a compiler's string length limit).

Modes
  --from-ttf NAME FILE   Encode a TrueType file as shared/<NAME>Font.h (NAME = Roboto or JetBrainsMono).
(The old Dear ImGui base85 arrays these were first converted from, and the one-time --from-legacy mode, are gone:
every program now reads the OKF1 data, shared/EmbeddedFonts.hpp.)
  --check         Decode the generated headers with an independent Python decoder, compare size, CRC-32 and
                  SHA-256 with the pins in assets/fonts/embedded_fonts.sha256, and check that re-encoding gives the
                  same file (so the generator is deterministic). Exit status 1 on any mismatch.
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
    # name: (header, C++ symbol)
    "Roboto": ("shared/RobotoFont.h", "kRobotoMediumOkf"),
    "JetBrainsMono": ("shared/JetBrainsMonoFont.h", "kJetBrainsMonoOkf"),
}
PINS = os.path.join(ROOT, "assets/fonts/embedded_fonts.sha256")
LINE = 100


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
    out.append("// The format and its decoder are in shared/OkfDecode.hpp. The font's licence: see docs/status/THIRD_PARTY.md.")
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
    path, symbol = FONTS[name]
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
    if len(argv) == 4 and argv[1] == "--from-ttf":
        if argv[2] not in FONTS:
            raise SystemExit("name must be one of %s" % ", ".join(FONTS))
        write_font(argv[2], open(argv[3], "rb").read())
        return 0
    if len(argv) == 2 and argv[1] == "--check":
        pins = read_pins()
        bad = 0
        for name, (path, symbol) in FONTS.items():
            packed = read_new(path, symbol)
            raw = unpack(packed)
            sha = hashlib.sha256(raw).hexdigest()
            ok = name in pins and pins[name] == (sha, len(raw))
            again = pack(raw) == packed
            line = "%-14s %7d bytes sha256 %s pins:%s regenerates-identically:%s" % (name, len(raw), sha[:16] + "...", "ok" if ok else "MISMATCH", "yes" if again else "NO")
            print(line)
            if not ok or not again:
                bad = 1
        return bad
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))

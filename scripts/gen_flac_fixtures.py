#!/usr/bin/env python3
"""Write the FLAC test files in tests/data/audio/flac/ with our own encoder (no outside program or library).

Written from the FLAC format description (xiph.org "FLAC format", the same content as RFC 9639): the stream
header and STREAMINFO / VORBIS_COMMENT / PADDING blocks, frame headers with every block-size, sample-rate and
sample-size code form, CRC-8 (polynomial 0x07) and CRC-16 (polynomial 0x8005), the CONSTANT, VERBATIM, FIXED and
LPC subframes, wasted bits, the four channel assignments, and the Rice residual with 4- and 5-bit parameters,
partition orders and escape partitions. The LPC coefficients come from the textbook autocorrelation and
Levinson-Durbin recursion; any quantised coefficients give a lossless stream, because the residual is exactly the
difference to the prediction.

The signals are our own (sines, chirps, a fixed pseudo-random sequence) and are written next to each .flac as the
expected samples (.s16: 16-bit little endian; .s32: 24-bit values in 32-bit little endian). FLAC is lossless, so
the decoder must give back exactly these numbers (tests/test_flac_import.cpp; tests/test_flac_hostile.cpp damages
these files). Each file is built so that, between them, every form above appears at least once (listed in FILES).

Run from the repository root:  python3 scripts/gen_flac_fixtures.py          (writes the files)
                                python3 scripts/gen_flac_fixtures.py --check  (exit 1 if a file would change)
"""
import math
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "tests", "data", "audio", "flac")


# ---- bits and checksums -------------------------------------------------------------------------------------------
class Bits:
    def __init__(self):
        self.out = bytearray()
        self.acc = 0
        self.n = 0

    def put(self, value, nbits):
        """Unsigned `value` in `nbits` bits, most significant first."""
        if nbits == 0:
            return
        assert 0 <= value < (1 << nbits), (value, nbits)
        self.acc = (self.acc << nbits) | value
        self.n += nbits
        while self.n >= 8:
            self.n -= 8
            self.out.append((self.acc >> self.n) & 0xFF)
        self.acc &= (1 << self.n) - 1

    def signed(self, value, nbits):
        assert -(1 << (nbits - 1)) <= value < (1 << (nbits - 1)), (value, nbits)
        self.put(value & ((1 << nbits) - 1), nbits)

    def unary(self, zeros):
        """`zeros` zero bits, then a one."""
        for _ in range(zeros):
            self.put(0, 1)
        self.put(1, 1)

    def align(self):
        if self.n:
            self.put(0, 8 - self.n)

    def data(self):
        assert self.n == 0
        return bytes(self.out)


def crc8(data):
    c = 0
    for b in data:
        c ^= b
        for _ in range(8):
            c = ((c << 1) ^ 0x07) & 0xFF if c & 0x80 else (c << 1) & 0xFF
    return c


def crc16(data):
    c = 0
    for b in data:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x8005) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    return c


def utf8_number(v):
    """The frame or sample number in FLAC's extended UTF-8 form (up to 36 bits, 7 bytes)."""
    if v < 0x80:
        return bytes([v])
    for nbytes in range(2, 8):
        payload = 6 * (nbytes - 1) + (7 - nbytes if nbytes < 7 else 0)
        if v < (1 << payload):
            out = []
            for _ in range(nbytes - 1):
                out.append(0x80 | (v & 0x3F))
                v >>= 6
            lead = (0xFF << (8 - nbytes)) & 0xFF
            out.append(lead | v)
            return bytes(reversed(out))
    raise ValueError("number too large")


# ---- residual -----------------------------------------------------------------------------------------------------
def zigzag(e):
    return (e << 1) if e >= 0 else ((-e) << 1) - 1


def rice_bits(part, k):
    return sum((zigzag(e) >> k) + 1 + k for e in part)


def write_residual(bw, res, order, block, opt):
    """Rice-coded residual. opt: 'method' (0: 4-bit parameters, 1: 5-bit), 'porder' (partition order),
    'escape' (index of a partition written raw, or None)."""
    method = opt.get("method", 0)
    porder = opt.get("porder", 0)
    while porder > 0 and (block % (1 << porder) or (block >> porder) <= order):
        porder -= 1
    pbits = 4 if method == 0 else 5
    esc = (1 << pbits) - 1
    bw.put(method, 2)
    bw.put(porder, 4)
    nparts = 1 << porder
    per = block >> porder
    pos = 0
    for p in range(nparts):
        count = per - order if p == 0 else per
        part = res[pos:pos + count]
        pos += count
        if opt.get("escape") == p:
            width = 1
            while any(not (-(1 << (width - 1)) <= e < (1 << (width - 1))) for e in part):
                width += 1
            bw.put(esc, pbits)
            bw.put(width, 5)
            for e in part:
                bw.signed(e, width)
            continue
        best = min(range(esc), key=lambda k: rice_bits(part, k))
        bw.put(best, pbits)
        for e in part:
            u = zigzag(e)
            bw.unary(u >> best)
            bw.put(u & ((1 << best) - 1), best)
    assert pos == len(res)


# ---- prediction ---------------------------------------------------------------------------------------------------
FIXED = [[], [1], [2, -1], [3, -3, 1], [4, -6, 4, -1]]


def lpc_coefs(x, order, precision):
    """Quantised LPC coefficients (most recent sample first) and the shift, from autocorrelation + Levinson-Durbin."""
    n = len(x)
    r = [sum(float(x[i]) * x[i - lag] for i in range(lag, n)) for lag in range(order + 1)]
    r[0] *= 1.0 + 1e-9
    r[0] += 1.0
    a = [0.0] * (order + 1)
    err = r[0]
    for i in range(1, order + 1):
        acc = r[i] - sum(a[j] * r[i - j] for j in range(1, i))
        k = acc / err if err else 0.0
        k = max(-0.9999, min(0.9999, k))
        new = a[:]
        new[i] = k
        for j in range(1, i):
            new[j] = a[j] - k * a[i - j]
        a = new
        err *= (1.0 - k * k)
    coefs = a[1:]
    cmax = max(abs(c) for c in coefs) or 1.0
    limit = (1 << (precision - 1)) - 1
    shift = 0
    while shift < 15 and cmax * (1 << (shift + 1)) <= limit:
        shift += 1
    q = [max(-limit - 1, min(limit, int(round(c * (1 << shift))))) for c in coefs]
    return q, shift


def predict_residual(x, coefs, shift):
    order = len(coefs)
    return [x[i] - (sum(c * x[i - 1 - j] for j, c in enumerate(coefs)) >> shift) for i in range(order, len(x))]


# ---- subframes ----------------------------------------------------------------------------------------------------
def write_subframe(bw, x, bps, opt):
    kind = opt["kind"]
    wasted = 0
    if opt.get("wasted") and any(x):
        while all(v % (1 << (wasted + 1)) == 0 for v in x):
            wasted += 1
    y = [v >> wasted for v in x]
    width = bps - wasted
    if kind == "constant":
        assert all(v == y[0] for v in y)
        code = 0
    elif kind == "verbatim":
        code = 1
    elif kind == "fixed":
        code = 8 + opt["order"]
    else:
        code = 32 + opt["order"] - 1
    bw.put(0, 1)
    bw.put(code, 6)
    if wasted:
        bw.put(1, 1)
        bw.unary(wasted - 1)
    else:
        bw.put(0, 1)
    if kind == "constant":
        bw.signed(y[0], width)
    elif kind == "verbatim":
        for v in y:
            bw.signed(v, width)
    elif kind == "fixed":
        order = opt["order"]
        for v in y[:order]:
            bw.signed(v, width)
        write_residual(bw, predict_residual(y, FIXED[order], 0), order, len(y), opt)
    else:
        order, precision = opt["order"], opt.get("precision", 14)
        coefs, shift = lpc_coefs(y, order, precision)
        for v in y[:order]:
            bw.signed(v, width)
        bw.put(precision - 1, 4)
        bw.signed(shift, 5)
        for c in coefs:
            bw.signed(c, precision)
        write_residual(bw, predict_residual(y, coefs, shift), order, len(y), opt)


# ---- frames and the stream ----------------------------------------------------------------------------------------
BLOCK_CODES = {192: 1, 576: 2, 1152: 3, 2304: 4, 4608: 5, 256: 8, 512: 9, 1024: 10, 2048: 11, 4096: 12, 8192: 13,
               16384: 14, 32768: 15}
RATE_CODES = {88200: 1, 176400: 2, 192000: 3, 8000: 4, 16000: 5, 22050: 6, 24000: 7, 32000: 8, 44100: 9, 48000: 10,
              96000: 11}
BPS_CODES = {8: 1, 12: 2, 16: 4, 20: 5, 24: 6, 32: 7}


def frame(spec, number, first_sample, chans, fopt):
    block = len(chans[0])
    head = bytearray()
    bw = Bits()
    bw.put(0x7FFC, 15)
    variable = spec.get("variable", False)
    bw.put(1 if variable else 0, 1)
    bform = fopt.get("block_form", "table")
    if bform == "table" and block in BLOCK_CODES:
        bcode, bextra = BLOCK_CODES[block], b""
    elif block <= 256 and bform != "16":
        bcode, bextra = 6, bytes([block - 1])
    else:
        bcode, bextra = 7, struct.pack(">H", block - 1)
    rform = fopt.get("rate_form", "table")
    rate = spec["rate"]
    if rform == "streaminfo":
        rcode, rextra = 0, b""
    elif rform == "khz":
        rcode, rextra = 12, bytes([rate // 1000])
    elif rform == "hz":
        rcode, rextra = 13, struct.pack(">H", rate)
    elif rform == "tens":
        rcode, rextra = 14, struct.pack(">H", rate // 10)
    else:
        rcode, rextra = RATE_CODES[rate], b""
    bw.put(bcode, 4)
    bw.put(rcode, 4)
    assign = fopt.get("assign", "independent")
    ccode = {"independent": len(chans) - 1, "left_side": 8, "right_side": 9, "mid_side": 10}[assign]
    bw.put(ccode, 4)
    bw.put(0 if fopt.get("bps_form") == "streaminfo" else BPS_CODES[spec["bps"]], 3)
    bw.put(0, 1)
    head += bw.data()
    head += utf8_number(first_sample if variable else number)
    head += bextra + rextra
    head.append(crc8(head))

    bps = spec["bps"]
    if assign == "independent":
        subs = [(c, bps) for c in chans]
    else:
        left, right = chans
        side = [a - b for a, b in zip(left, right)]
        if assign == "left_side":
            subs = [(left, bps), (side, bps + 1)]
        elif assign == "right_side":
            subs = [(side, bps + 1), (right, bps)]
        else:
            subs = [([(a + b) >> 1 for a, b in zip(left, right)], bps), (side, bps + 1)]
    bw = Bits()
    for ci, (x, sbps) in enumerate(subs):
        opts = fopt["sub"][ci] if isinstance(fopt["sub"], list) else fopt["sub"]
        write_subframe(bw, x, sbps, opts)
    bw.align()
    body = bytes(head) + bw.data()
    return body + struct.pack(">H", crc16(body))


def stream(spec, chans):
    total = len(chans[0])
    blocks = spec["blocks"]
    assert sum(blocks) == total, (spec["name"], sum(blocks), total)
    frames = []
    pos = 0
    for i, n in enumerate(blocks):
        frames.append(frame(spec, i, pos, [c[pos:pos + n] for c in chans], spec["frames"][i]))
        pos += n
    out = bytearray(b"fLaC")
    bmin = min(blocks[:-1]) if len(blocks) > 1 else blocks[0]
    bmax = max(blocks)
    fmin = min(len(f) for f in frames)
    fmax = max(len(f) for f in frames)
    info = Bits()
    info.put(bmin if spec.get("variable") else bmax, 16)
    info.put(bmax, 16)
    info.put(fmin, 24)
    info.put(fmax, 24)
    info.put(spec["rate"], 20)
    info.put(len(chans) - 1, 3)
    info.put(spec["bps"] - 1, 5)
    info.put(total, 36)
    info.put(0, 64)        # MD5 of the audio: 0 means "not computed" (allowed by the format)
    info.put(0, 64)
    vendor = b"OkaySpace scripts/gen_flac_fixtures.py"
    comment = struct.pack("<I", len(vendor)) + vendor + struct.pack("<I", 1)
    tag = b"TITLE=" + spec["name"].encode()
    comment += struct.pack("<I", len(tag)) + tag
    blocks_meta = [(0, info.data()), (4, comment), (1, bytes(spec.get("padding", 64)))]
    for i, (t, body) in enumerate(blocks_meta):
        last = 0x80 if i == len(blocks_meta) - 1 else 0
        out += bytes([last | t]) + struct.pack(">I", len(body))[1:] + body
    for f in frames:
        out += f
    return bytes(out)


# ---- the signals (ours) -------------------------------------------------------------------------------------------
class Lcg:
    def __init__(self, seed):
        self.s = seed

    def next(self):
        self.s = (self.s * 1664525 + 1013904223) & 0xFFFFFFFF
        return self.s >> 8


def clamp(v, bps):
    lim = (1 << (bps - 1)) - 1
    return max(-lim - 1, min(lim, v))


def sine_mono():
    x = [clamp(int(round(9000 * math.sin(2 * math.pi * 440 * i / 44100) + 4000 * math.sin(2 * math.pi * 1250 * i / 44100 + 0.3))), 16)
         for i in range(11025)]
    return [x]


def tones_stereo():
    n = 6615
    left, right = [], []
    for i in range(n):
        t = i / 22050.0
        l = 7000 * math.sin(2 * math.pi * 330 * t) + 2500 * math.sin(2 * math.pi * 2100 * t)
        r = 6500 * math.sin(2 * math.pi * 330 * t + 0.4) + 3000 * math.sin(2 * math.pi * 523 * t)
        a, b = int(round(l)), int(round(r))
        if 4096 <= i < 5120:          # the wasted-bits frame: every sample a multiple of 8
            a, b = a & ~7, b & ~7
        left.append(clamp(a, 16))
        right.append(clamp(b, 16))
    return [left, right]


def noise_mono():
    g = Lcg(12345)
    x = []
    prev = 0
    for i in range(1600):
        v = int((g.next() % 20001) - 10000)
        prev = (prev * 3 + v) // 4     # some correlation, so prediction helps
        x.append(clamp(prev, 16))
    for i in range(960, 1152):        # one block of silence-like constant samples (a CONSTANT subframe)
        x[i] = -123
    return [x]


def pcm24():
    g = Lcg(777)
    x = []
    for i in range(1800):
        t = i / 16000.0
        f = 200 + 3000 * t
        v = 3000000 * math.sin(2 * math.pi * f * t) + ((g.next() % 4001) - 2000)
        x.append(clamp(int(round(v)), 24))
    return [x]


FILES = [
    # name, signal, spec. Between them: block codes 1 to 7 and 10; rate codes 0, 4, 5, 6, 9, 12, 13, 14; bps codes
    # 0, 4, 6; channel assignments 0, 1, 8, 9, 10; CONSTANT, VERBATIM, FIXED 0-4, LPC 1-32; wasted bits; Rice
    # methods 0 and 1; partition orders 0-4; escape partitions; fixed and variable blocking (2-byte sample number).
    ("sine_mono_44k", sine_mono, {
        "rate": 44100, "bps": 16, "blocks": [4608, 2304, 2304, 1809],
        "frames": [{"sub": {"kind": "lpc", "order": 8, "porder": 2}},
                   {"sub": {"kind": "lpc", "order": 16, "precision": 15, "porder": 4}},
                   {"sub": {"kind": "lpc", "order": 24, "precision": 13, "porder": 3}},
                   {"sub": {"kind": "fixed", "order": 2, "porder": 0}, "block_form": "16"}]}),
    ("tones_stereo_22k", tones_stereo, {
        "rate": 22050, "bps": 16, "blocks": [1024] * 6 + [471],
        "frames": [{"sub": {"kind": "fixed", "order": 2, "porder": 3}, "assign": "independent", "rate_form": "streaminfo"},
                   {"sub": {"kind": "lpc", "order": 6, "porder": 2}, "assign": "left_side"},
                   {"sub": {"kind": "lpc", "order": 4, "porder": 1}, "assign": "right_side"},
                   {"sub": {"kind": "fixed", "order": 3, "porder": 2}, "assign": "mid_side"},
                   {"sub": {"kind": "lpc", "order": 10, "porder": 2, "wasted": True}, "assign": "independent"},
                   {"sub": [{"kind": "fixed", "order": 4}, {"kind": "verbatim"}], "assign": "mid_side"},
                   {"sub": {"kind": "lpc", "order": 3, "porder": 0}, "assign": "left_side", "bps_form": "streaminfo"}]}),
    ("noise_mono_8k_fixed", noise_mono, {
        "rate": 8000, "bps": 16, "blocks": [192] * 8 + [64],
        "frames": [{"sub": {"kind": "fixed", "order": 0, "porder": 1}},
                   {"sub": {"kind": "fixed", "order": 1, "porder": 2}},
                   {"sub": {"kind": "fixed", "order": 2, "porder": 3}, "bps_form": "streaminfo"},
                   {"sub": {"kind": "fixed", "order": 3, "porder": 2, "escape": 1}},
                   {"sub": {"kind": "fixed", "order": 4, "porder": 0}},
                   {"sub": {"kind": "constant"}},
                   {"sub": {"kind": "verbatim"}},
                   {"sub": {"kind": "fixed", "order": 1, "method": 1, "porder": 1, "escape": 0}},
                   {"sub": {"kind": "fixed", "order": 2}}]}),
    ("noise_mono_8k_lpc", noise_mono, {
        "rate": 8000, "bps": 16, "blocks": [576, 576, 448],
        "frames": [{"sub": {"kind": "lpc", "order": 2, "porder": 3}, "rate_form": "khz"},
                   {"sub": {"kind": "lpc", "order": 1, "precision": 5}, "rate_form": "khz"},
                   {"sub": {"kind": "lpc", "order": 32, "precision": 12, "method": 1, "porder": 1}, "rate_form": "khz"}]}),
    ("pcm24_mono_16k", pcm24, {
        "rate": 16000, "bps": 24, "blocks": [1152, 576, 72], "variable": True,
        "frames": [{"sub": {"kind": "lpc", "order": 12, "method": 1, "porder": 3}, "rate_form": "hz"},
                   {"sub": {"kind": "lpc", "order": 7, "method": 1, "porder": 3, "escape": 2}, "rate_form": "tens"},
                   {"sub": {"kind": "fixed", "order": 3, "method": 1, "porder": 1}}]}),
]


def pcm_bytes(chans, bps):
    n = len(chans[0])
    out = bytearray()
    for i in range(n):
        for c in chans:
            out += struct.pack("<h", c[i]) if bps == 16 else struct.pack("<i", c[i])
    return bytes(out)


def build():
    files = {}
    for name, signal, spec in FILES:
        spec = dict(spec, name=name)
        chans = signal()
        files[name + ".flac"] = stream(spec, chans)
        ref = ("noise_mono_8k" if name.startswith("noise_mono_8k") else name) + (".s16" if spec["bps"] == 16 else ".s32")
        files[ref] = pcm_bytes(chans, spec["bps"])
    return files


def main(argv):
    files = build()
    if len(argv) > 1 and argv[1] == "--check":
        bad = 0
        for n, data in sorted(files.items()):
            p = os.path.join(OUT, n)
            same = os.path.exists(p) and open(p, "rb").read() == data
            print("%-28s %6d bytes %s" % (n, len(data), "same" if same else "DIFFERENT"))
            bad |= not same
        return 1 if bad else 0
    os.makedirs(OUT, exist_ok=True)
    for n, data in sorted(files.items()):
        with open(os.path.join(OUT, n), "wb") as f:
            f.write(data)
        print("%-28s %6d bytes" % (n, len(data)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

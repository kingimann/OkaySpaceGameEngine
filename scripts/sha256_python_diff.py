#!/usr/bin/env python3
"""Make random SHA-256 / HMAC / PBKDF2 cases with Python's hashlib and hmac, then have tests/sha256_compare check every
implementation against them. Usage: sha256_python_diff.py <path to sha256_compare> [digests] [seed]"""
import hashlib, hmac, os, random, struct, subprocess, sys, tempfile

def main():
    tool = sys.argv[1]
    nd = int(sys.argv[2]) if len(sys.argv) > 2 else 2000
    r = random.Random(int(sys.argv[3]) if len(sys.argv) > 3 else 1)
    fd, path = tempfile.mkstemp(suffix=".bin")
    with os.fdopen(fd, "wb") as f:
        edge = [0, 1, 55, 56, 57, 63, 64, 65, 119, 120, 127, 128, 129, 191, 192, 1000, 65535, 65536, 70000]
        for i in range(nd):
            n = edge[i] if i < len(edge) else r.choice([r.randrange(0, 200), r.randrange(0, 70001)])
            m = r.randbytes(n)
            f.write(b"\0" + struct.pack("<I", n) + m + hashlib.sha256(m).digest())
        for i in range(1000):
            kl = r.choice([0, 1, 31, 32, 63, 64, 65, 100, 131, 150, r.randrange(0, 151)])
            ml = r.choice([0, 1, 55, 56, 63, 64, 65, r.randrange(0, 600)])
            k, m = r.randbytes(kl), r.randbytes(ml)
            f.write(b"\1" + struct.pack("<II", kl, ml) + k + m + hmac.new(k, m, hashlib.sha256).digest())
        for i in range(400):
            pl = r.choice([0, 1, 63, 64, 65, 100, 150, r.randrange(0, 151)])
            sl = r.choice([0, 1, 16, 55, 56, 64, r.randrange(0, 80)])
            it = r.randrange(1, 300)
            dl = r.choice([32, 32, 32, 1, 16, 31, 33, 64, 100])
            p, s = r.randbytes(pl), r.randbytes(sl)
            f.write(b"\2" + struct.pack("<IIII", pl, sl, it, dl) + p + s + hashlib.pbkdf2_hmac("sha256", p, s, it, dl))
    try:
        sys.exit(subprocess.call([tool, "check", path]))
    finally:
        os.unlink(path)

main()

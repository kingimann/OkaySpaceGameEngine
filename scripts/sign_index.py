#!/usr/bin/env python3
"""Sign and check the release index (versions.json) with ed25519. Docs: docs/updater.md "Signed releases".

    sign_index.py genkey                          make a key pair: prints the seed (PRIVATE) and the public key, nothing is written
    sign_index.py pubkey                          print the public key of the seed in $OKAY_UPDATE_SIGNING_SEED
    sign_index.py sign   --file F --out F.sig     sign F with the seed in $OKAY_UPDATE_SIGNING_SEED
    sign_index.py verify --file F --sig F.sig --pub <hex>[,<hex>]   exit 0 only when F.sig verifies under one of the keys

The private seed (64 hex digits) comes ONLY from the environment variable: never from a file or the command line, and it is never
printed or logged (errors say that it is missing or malformed, never what it was). The signature file is one line,
"okay-sig1 <128 hex>", over the message "okayspace-versions-json-v1\\n" + the file's bytes; it is what shared/UpdateSignature.hpp checks.

Pure Python 3 (RFC 8032 reference arithmetic, hashlib only): no third-party package, so it runs on any publisher machine. It is slow
(a few milliseconds per operation is not the goal), which does not matter for one small file.
"""
import argparse
import hashlib
import os
import re
import sys

SEED_ENV = "OKAY_UPDATE_SIGNING_SEED"
DOMAIN = b"okayspace-versions-json-v1\n"
TAG = "okay-sig1"

P = 2 ** 255 - 19
Q = 2 ** 252 + 27742317777372353535851937790883648493
D = -121665 * pow(121666, P - 2, P) % P
SQRT_M1 = pow(2, (P - 1) // 4, P)


def _inv(x):
    return pow(x, P - 2, P)


def _recover_x(y, sign):
    if y >= P:
        return None
    x2 = (y * y - 1) * _inv(D * y * y + 1) % P
    if x2 == 0:
        return None if sign else 0
    x = pow(x2, (P + 3) // 8, P)
    if (x * x - x2) % P != 0:
        x = x * SQRT_M1 % P
    if (x * x - x2) % P != 0:
        return None
    if (x & 1) != sign:
        x = P - x
    return x


_GY = 4 * _inv(5) % P
_GX = _recover_x(_GY, 0)
G = (_GX, _GY, 1, _GX * _GY % P)
ZERO = (0, 1, 1, 0)


def _add(p, q):
    a = (p[1] - p[0]) * (q[1] - q[0]) % P
    b = (p[1] + p[0]) * (q[1] + q[0]) % P
    c = 2 * p[3] * q[3] * D % P
    d = 2 * p[2] * q[2] % P
    e, f, g, h = b - a, d - c, d + c, b + a
    return (e * f % P, g * h % P, f * g % P, e * h % P)


def _mul(s, p):
    q = ZERO
    while s > 0:
        if s & 1:
            q = _add(q, p)
        p = _add(p, p)
        s >>= 1
    return q


def _compress(p):
    zi = _inv(p[2])
    x, y = p[0] * zi % P, p[1] * zi % P
    return int.to_bytes(y | ((x & 1) << 255), 32, "little")


def _decompress(b):
    if len(b) != 32:
        return None
    y = int.from_bytes(b, "little")
    sign = y >> 255
    y &= (1 << 255) - 1
    x = _recover_x(y, sign)
    return None if x is None else (x, y, 1, x * y % P)


def _equal(p, q):
    return (p[0] * q[2] - q[0] * p[2]) % P == 0 and (p[1] * q[2] - q[1] * p[2]) % P == 0


def _expand(seed):
    h = hashlib.sha512(seed).digest()
    a = int.from_bytes(h[:32], "little")
    a &= (1 << 254) - 8
    a |= 1 << 254
    return a, h[32:]


def public_key(seed):
    a, _ = _expand(seed)
    return _compress(_mul(a, G))


def sign(seed, msg):
    a, prefix = _expand(seed)
    pub = _compress(_mul(a, G))
    r = int.from_bytes(hashlib.sha512(prefix + msg).digest(), "little") % Q
    rb = _compress(_mul(r, G))
    h = int.from_bytes(hashlib.sha512(rb + pub + msg).digest(), "little") % Q
    return rb + int.to_bytes((r + h * a) % Q, 32, "little")


def verify(pub, msg, sig):
    if len(sig) != 64 or len(pub) != 32:
        return False
    a = _decompress(pub)
    r = _decompress(sig[:32])
    s = int.from_bytes(sig[32:], "little")
    if a is None or r is None or s >= Q:
        return False
    h = int.from_bytes(hashlib.sha512(sig[:32] + pub + msg).digest(), "little") % Q
    return _equal(_mul(s, G), _add(r, _mul(h, a)))


def _die(msg):
    sys.stderr.write("sign_index: " + msg + "\n")
    sys.exit(1)


def _seed_from_env():
    v = os.environ.get(SEED_ENV, "").strip()
    if not v:
        _die("%s is not set (the signing seed comes from the environment only)" % SEED_ENV)
    if not re.fullmatch(r"[0-9a-fA-F]{64}", v):
        _die("%s is not 64 hex digits" % SEED_ENV)   # never echo the value
    return bytes.fromhex(v)


def _read(path):
    try:
        with open(path, "rb") as f:
            return f.read()
    except OSError as e:
        _die("cannot read %s: %s" % (path, e.strerror))


def signature_text(seed, body):
    return "%s %s\n" % (TAG, sign(seed, DOMAIN + body).hex())


def parse_signature(text):
    m = re.fullmatch(r"\s*" + TAG + r" ([0-9a-f]{128})\s*", text)
    return bytes.fromhex(m.group(1)) if m else None


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("genkey")
    sub.add_parser("pubkey")
    s = sub.add_parser("sign")
    s.add_argument("--file", required=True)
    s.add_argument("--out", required=True)
    v = sub.add_parser("verify")
    v.add_argument("--file", required=True)
    v.add_argument("--sig", required=True)
    v.add_argument("--pub", required=True)
    a = ap.parse_args(argv)
    if a.cmd == "genkey":
        seed = os.urandom(32)
        print("%s=%s" % (SEED_ENV, seed.hex()))
        print("OKAY_UPDATE_TRUSTED_KEYS=%s" % public_key(seed).hex())
        return 0
    if a.cmd == "pubkey":
        print(public_key(_seed_from_env()).hex())
        return 0
    if a.cmd == "sign":
        seed = _seed_from_env()
        body = _read(a.file)
        text = signature_text(seed, body)
        # The signature is checked against the key it claims before it is written: a bug here must not publish a bad file.
        if not verify(public_key(seed), DOMAIN + body, parse_signature(text)):
            _die("internal error: the new signature does not verify")
        with open(a.out, "w", encoding="ascii", newline="\n") as f:
            f.write(text)
        print("signed %s (key %s...)" % (os.path.basename(a.file), public_key(seed).hex()[:16]))
        return 0
    # verify
    sig = parse_signature(_read(a.sig).decode("ascii", "replace"))
    if sig is None:
        _die("%s is not an okay-sig1 signature file" % a.sig)
    body = _read(a.file)
    for i, k in enumerate(a.pub.split(",")):
        if not re.fullmatch(r"[0-9a-fA-F]{64}", k):
            _die("--pub must be 64-digit hex keys, comma-separated")
        if verify(bytes.fromhex(k), DOMAIN + body, sig):
            print("signature OK (key %d)" % i)
            return 0
    _die("signature does not verify under any given key")


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))

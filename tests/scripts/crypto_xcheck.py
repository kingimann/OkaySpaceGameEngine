#!/usr/bin/env python3
"""Cross-check okay::crypto (ours, UNREVIEWED) against Python's `cryptography` (OpenSSL) and hashlib.

  crypto_xcheck.py <path to crypto_xcheck_tool> [cases-per-primitive]

Both directions: Python seals/signs and our tool opens/verifies, and our tool seals/signs and
Python opens/verifies. Inputs are random (fixed seed unless OKAY_CRYPTO_SEED is set), with sizes
biased to block edges. Tamper cases (a flipped bit) must be rejected by both. Exit 0 = all agree.
OpenSSL has no XChaCha20, so XChaCha20-Poly1305 is cross-checked against libsodium instead
(tests/test_crypto_sodium_xcheck.cpp) and, here, through HChaCha20-free identities only:
its seal output must open with the IETF cipher under the HChaCha20 subkey, which Python can
compute with its own ChaCha20 block function (a 4-line construction below).
"""
import hashlib, os, random, subprocess, sys

from cryptography.exceptions import InvalidSignature, InvalidTag
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey, Ed25519PublicKey
from cryptography.hazmat.primitives.asymmetric.x25519 import X25519PrivateKey, X25519PublicKey
from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305
from cryptography.hazmat.primitives.poly1305 import Poly1305

tool_path = sys.argv[1]
N = int(sys.argv[2]) if len(sys.argv) > 2 else 2000
seed = int(os.environ.get("OKAY_CRYPTO_SEED", "20240601"))
rng = random.Random(seed)

proc = subprocess.Popen([tool_path], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
def tool(*args):
    proc.stdin.write(" ".join(a if a else "-" for a in args) + "\n")
    proc.stdin.flush()
    return proc.stdout.readline().strip()

def rb(n): return bytes(rng.getrandbits(8) for _ in range(n))
def length():
    return rng.choice([rng.randrange(0, 17), rng.randrange(60, 70), rng.randrange(120, 140), rng.randrange(0, 600)])
def hx(b): return b.hex() if b else "-"
def unhx(s): return b"" if s == "-" else bytes.fromhex(s)

fails = 0
counts = {}
def check(name, ok, info=""):
    global fails
    counts[name] = counts.get(name, 0) + 1
    if not ok:
        fails += 1
        if fails <= 20: print("MISMATCH", name, info)

def flip(b):
    i = rng.randrange(len(b) * 8); x = bytearray(b); x[i // 8] ^= 1 << (i % 8); return bytes(x)

# ChaCha20-Poly1305 IETF, both directions (+ tamper)
for _ in range(N):
    key, nonce, aad, pt = rb(32), rb(12), rb(rng.choice([0, 0, rng.randrange(0, 70)])), rb(length())
    theirs = ChaCha20Poly1305(key).encrypt(nonce, pt, aad or None)
    check("aead ours-seals-theirs-opens", tool("aead_seal", hx(key), hx(nonce), hx(aad), hx(pt)) == hx(theirs))
    check("aead theirs-seals-ours-opens", tool("aead_open", hx(key), hx(nonce), hx(aad), hx(theirs)) == hx(pt))
    ours = unhx(tool("aead_seal", hx(key), hx(nonce), hx(aad), hx(pt)))
    try: ok = ChaCha20Poly1305(key).decrypt(nonce, ours, aad or None) == pt
    except InvalidTag: ok = False
    check("aead python opens ours", ok)
    bad = flip(theirs)
    check("aead tamper rejected by ours", tool("aead_open", hx(key), hx(nonce), hx(aad), hx(bad)) == "FAIL")
    try: ChaCha20Poly1305(key).decrypt(nonce, bad, aad or None); rej = False
    except InvalidTag: rej = True
    check("aead tamper rejected by python", rej)

# XChaCha20-Poly1305: subkey by HChaCha20 (computed here with plain ChaCha rounds), then the IETF AEAD
def rotl(x, c): return ((x << c) & 0xffffffff) | (x >> (32 - c))
def hchacha(key, n16):
    import struct
    s = list(struct.unpack("<4I", b"expand 32-byte k")) + list(struct.unpack("<8I", key)) + list(struct.unpack("<4I", n16))
    def qr(a, b, c, d):
        s[a] = (s[a] + s[b]) & 0xffffffff; s[d] = rotl(s[d] ^ s[a], 16)
        s[c] = (s[c] + s[d]) & 0xffffffff; s[b] = rotl(s[b] ^ s[c], 12)
        s[a] = (s[a] + s[b]) & 0xffffffff; s[d] = rotl(s[d] ^ s[a], 8)
        s[c] = (s[c] + s[d]) & 0xffffffff; s[b] = rotl(s[b] ^ s[c], 7)
    for _ in range(10):
        qr(0, 4, 8, 12); qr(1, 5, 9, 13); qr(2, 6, 10, 14); qr(3, 7, 11, 15)
        qr(0, 5, 10, 15); qr(1, 6, 11, 12); qr(2, 7, 8, 13); qr(3, 4, 9, 14)
    return struct.pack("<8I", *(s[0:4] + s[12:16]))
for _ in range(N // 2):
    key, nonce, aad, pt = rb(32), rb(24), rb(rng.choice([0, rng.randrange(0, 70)])), rb(length())
    sub = hchacha(key, nonce[:16])
    theirs = ChaCha20Poly1305(sub).encrypt(b"\0\0\0\0" + nonce[16:], pt, aad or None)
    check("xchacha ours-seals-python-opens", tool("xaead_seal", hx(key), hx(nonce), hx(aad), hx(pt)) == hx(theirs))
    check("xchacha python-seals-ours-opens", tool("xaead_open", hx(key), hx(nonce), hx(aad), hx(theirs)) == hx(pt))

# Poly1305
for _ in range(N):
    key, m = rb(32), rb(length())
    if rng.random() < 0.1: m = b"\xff" * len(m)
    check("poly1305", tool("poly", hx(key), hx(m)) == hx(Poly1305.generate_tag(key, m)))

# SHA-2, BLAKE2b
for _ in range(N):
    m = rb(length())
    check("sha256", tool("sha256", hx(m)) == hashlib.sha256(m).hexdigest())
    check("sha512", tool("sha512", hx(m)) == hashlib.sha512(m).hexdigest())
    olen, klen = rng.randrange(1, 65), rng.choice([0, rng.randrange(1, 65)])
    key = rb(klen)
    check("blake2b", tool("blake2b", str(olen), hx(key), hx(m)) == hashlib.blake2b(m, digest_size=olen, key=key).hexdigest())

# X25519 both directions
raw = serialization.Encoding.Raw, serialization.PublicFormat.Raw
for _ in range(N):
    s = rb(32)
    sk = X25519PrivateKey.from_private_bytes(s)
    pub = sk.public_key().public_bytes(*raw)
    check("x25519 public key", tool("x25519_base", hx(s)) == hx(pub))
    other = X25519PrivateKey.from_private_bytes(rb(32)).public_key().public_bytes(*raw)
    shared = sk.exchange(X25519PublicKey.from_public_bytes(other))
    check("x25519 shared secret", tool("x25519", hx(s), hx(other)) == hx(shared))

# Ed25519 both directions
for _ in range(max(1, N // 5)):
    seedk, m = rb(32), rb(length())
    sk = Ed25519PrivateKey.from_private_bytes(seedk)
    pub = sk.public_key().public_bytes(*raw)
    sig = sk.sign(m)
    check("ed25519 public key", tool("ed_pub", hx(seedk)) == hx(pub))
    check("ed25519 ours-signs == python-signs", tool("ed_sign", hx(seedk), hx(m)) == hx(sig))
    check("ed25519 ours verifies python", tool("ed_verify", hx(pub), hx(m), hx(sig)) == "1")
    ours = unhx(tool("ed_sign", hx(seedk), hx(m)))
    try: Ed25519PublicKey.from_public_bytes(pub).verify(ours, m); ok = True
    except InvalidSignature: ok = False
    check("ed25519 python verifies ours", ok)
    bad = flip(sig)
    check("ed25519 tamper rejected by ours", tool("ed_verify", hx(pub), hx(m), hx(bad)) == "0")

proc.stdin.close(); proc.wait()
for k, v in sorted(counts.items()): print(f"{k}: {v} cases")
print("crypto_xcheck:", "ALL AGREE" if fails == 0 else f"{fails} MISMATCHES", f"(seed {seed})")
sys.exit(0 if fails == 0 else 1)

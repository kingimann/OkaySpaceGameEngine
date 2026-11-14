// Line-oriented front end to okay::crypto (UNREVIEWED) for tests/scripts/crypto_xcheck.py,
// which checks it against Python's `cryptography`/hashlib/OpenSSL in both directions.
// Each stdin line: `op hex hex ...`; each answer line: lowercase hex, or `FAIL`.
//   aead_seal key nonce(12) aad pt        -> ct||tag        aead_open key nonce aad ct||tag -> pt | FAIL
//   xaead_seal / xaead_open               (24-byte nonce)
//   poly key msg -> tag                   blake2b outlen key msg -> digest
//   sha256 msg, sha512 msg                x25519 scalar point -> out | FAIL (all-zero)
//   x25519_base scalar                    ed_pub seed -> pk    ed_sign seed msg -> sig
//   ed_verify pk msg sig -> 1 | 0         chacha key nonce(12) counter(8 hex) data -> data^ks
// An empty hex field is written as `-`.
#include <okay/Crypto/Crypto.hpp>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace okay::crypto;
using Bytes = std::vector<std::uint8_t>;

static Bytes Hex(const std::string& s) {
    Bytes o;
    if (s == "-") return o;
    auto nib = [](char c) { return c <= '9' ? c - '0' : (c | 32) - 'a' + 10; };
    for (std::size_t i = 0; i + 1 < s.size(); i += 2) o.push_back(static_cast<std::uint8_t>(nib(s[i]) << 4 | nib(s[i + 1])));
    return o;
}
static std::string ToHex(const Bytes& b) {
    static const char* d = "0123456789abcdef";
    if (b.empty()) return "-";
    std::string o;
    for (auto x : b) { o += d[x >> 4]; o += d[x & 15]; }
    return o;
}

int main() {
    std::ios::sync_with_stdio(false);
    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream in(line);
        std::string op;
        in >> op;
        std::vector<std::string> a;
        for (std::string t; in >> t;) a.push_back(t);
        Bytes out;
        bool fail = false;
        if (op == "aead_seal" || op == "xaead_seal") {
            Bytes key = Hex(a[0]), nonce = Hex(a[1]), aad = Hex(a[2]), pt = Hex(a[3]);
            out.resize(pt.size() + 16);
            if (op[0] == 'x') XChaCha20Poly1305Seal(out.data(), pt.data(), pt.size(), aad.data(), aad.size(), nonce.data(), key.data());
            else ChaCha20Poly1305Seal(out.data(), pt.data(), pt.size(), aad.data(), aad.size(), nonce.data(), key.data());
        } else if (op == "aead_open" || op == "xaead_open") {
            Bytes key = Hex(a[0]), nonce = Hex(a[1]), aad = Hex(a[2]), ct = Hex(a[3]);
            if (ct.size() < 16) fail = true;
            else {
                out.resize(ct.size() - 16);
                fail = !(op[0] == 'x' ? XChaCha20Poly1305Open(out.data(), ct.data(), ct.size(), aad.data(), aad.size(), nonce.data(), key.data())
                                      : ChaCha20Poly1305Open(out.data(), ct.data(), ct.size(), aad.data(), aad.size(), nonce.data(), key.data()));
            }
        } else if (op == "poly") {
            Bytes key = Hex(a[0]), m = Hex(a[1]);
            out.resize(16);
            Poly1305Mac(out.data(), m.data(), m.size(), key.data());
        } else if (op == "blake2b") {
            const std::size_t outLen = std::stoul(a[0]);
            Bytes key = Hex(a[1]), m = Hex(a[2]);
            out.resize(outLen);
            Blake2bHash(out.data(), outLen, m.data(), m.size(), key.empty() ? nullptr : key.data(), key.size());
        } else if (op == "sha256") {
            Bytes m = Hex(a[0]); out.resize(32); Sha256Hash(out.data(), m.data(), m.size());
        } else if (op == "sha512") {
            Bytes m = Hex(a[0]); out.resize(64); Sha512Hash(out.data(), m.data(), m.size());
        } else if (op == "x25519") {
            Bytes s = Hex(a[0]), p = Hex(a[1]); out.resize(32); fail = !X25519(out.data(), s.data(), p.data());
        } else if (op == "x25519_base") {
            Bytes s = Hex(a[0]); out.resize(32); X25519Base(out.data(), s.data());
        } else if (op == "ed_pub") {
            Bytes s = Hex(a[0]); out.resize(32); Ed25519PublicFromSeed(out.data(), s.data());
        } else if (op == "ed_sign") {
            Bytes seed = Hex(a[0]), m = Hex(a[1]), pk(32), sk(64);
            Ed25519SeedKeypair(pk.data(), sk.data(), seed.data());
            out.resize(64);
            Ed25519Sign(out.data(), m.data(), m.size(), sk.data());
        } else if (op == "ed_verify") {
            Bytes pk = Hex(a[0]), m = Hex(a[1]), sig = Hex(a[2]);
            std::cout << (Ed25519Verify(sig.data(), m.data(), m.size(), pk.data()) ? "1" : "0") << "\n";
            continue;
        } else if (op == "chacha") {
            Bytes key = Hex(a[0]), nonce = Hex(a[1]), ctr = Hex(a[2]), d = Hex(a[3]);
            const std::uint32_t c = ctr[0] << 24 | ctr[1] << 16 | ctr[2] << 8 | ctr[3];
            out.resize(d.size());
            ChaCha20Xor(out.data(), d.data(), d.size(), key.data(), nonce.data(), c);
        } else {
            std::cout << "FAIL unknown op\n";
            continue;
        }
        std::cout << (fail ? std::string("FAIL") : ToHex(out)) << "\n";
    }
    return 0;
}

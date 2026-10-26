#pragma once
// Anti-cheat crypto: ed25519 signatures (and the SHA-512 they're built on) for
// the build's integrity manifest (AntiCheatIntegrity.hpp).
//
// With libsodium in the build (OKAY_HAVE_SODIUM, the engine's OKAY_USE_SODIUM
// option) every call goes to libsodium. Without it (the launcher, which doesn't
// link the engine, or a -DOKAY_USE_SODIUM=OFF build) the portable code below is
// used: a C++ port of TweetNaCl's public-domain ed25519 (Bernstein et al.),
// checked against the RFC 8032 vectors in tests/test_anticheat_integrity.cpp on
// every build. On top of TweetNaCl it makes the checks libsodium 1.0.18's
// crypto_sign_verify_detached makes: S must be canonical (< L), R must not be a
// small-order point, and the public key must be canonical and not small-order.
// (A test pins the identity-key forgery TweetNaCl alone would accept.) It isn't
// claimed to match libsodium on every malformed input beyond those checks.
// Signing never needs randomness (ed25519 is deterministic), and no
// key is ever made here: the owner makes them (docs/anticheat.md).
//
// SHA-256 for file hashes is the engine's bundled one (okayofficial::FileSha256),
// so the manifest and the official game's updater hash files the same way.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

#ifdef OKAY_HAVE_SODIUM
#include <sodium.h>
#endif

namespace okayac {

// ---- Hex -------------------------------------------------------------------
inline std::string ToHex(const std::uint8_t* d, std::size_t n) {
    static const char* hx = "0123456789abcdef";
    std::string o;
    o.reserve(n * 2);
    for (std::size_t i = 0; i < n; ++i) { o += hx[d[i] >> 4]; o += hx[d[i] & 15]; }
    return o;
}

// Strict: an even count of hex digits, with surrounding whitespace (a key file's
// trailing newline) ignored and none inside.
inline bool FromHex(const std::string& s, std::vector<std::uint8_t>& out) {
    const std::size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) { out.clear(); return false; }
    const std::size_t b = s.find_last_not_of(" \t\r\n");
    const std::string t = s.substr(a, b - a + 1);
    if (t.size() % 2) return false;
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::vector<std::uint8_t> v(t.size() / 2);
    for (std::size_t i = 0; i < v.size(); ++i) {
        const int h = nib(t[2 * i]), l = nib(t[2 * i + 1]);
        if (h < 0 || l < 0) return false;
        v[i] = (std::uint8_t)(h << 4 | l);
    }
    out = std::move(v);
    return true;
}

// Zero key material (not optimised away).
inline void Wipe(void* p, std::size_t n) {
#ifdef OKAY_HAVE_SODIUM
    sodium_memzero(p, n);
#else
    volatile std::uint8_t* v = static_cast<volatile std::uint8_t*>(p);
    while (n--) *v++ = 0;
#endif
}

// ---- SHA-512 (FIPS 180-4) ----------------------------------------------------
namespace sha512 {
using Digest = std::array<std::uint8_t, 64>;

namespace detail {
inline std::uint64_t Rot(std::uint64_t x, int c) { return (x >> c) | (x << (64 - c)); }
inline void Block(std::uint64_t h[8], const std::uint8_t* p) {
    static const std::uint64_t K[80] = {
        0x428a2f98d728ae22ull, 0x7137449123ef65cdull, 0xb5c0fbcfec4d3b2full, 0xe9b5dba58189dbbcull, 0x3956c25bf348b538ull,
        0x59f111f1b605d019ull, 0x923f82a4af194f9bull, 0xab1c5ed5da6d8118ull, 0xd807aa98a3030242ull, 0x12835b0145706fbeull,
        0x243185be4ee4b28cull, 0x550c7dc3d5ffb4e2ull, 0x72be5d74f27b896full, 0x80deb1fe3b1696b1ull, 0x9bdc06a725c71235ull,
        0xc19bf174cf692694ull, 0xe49b69c19ef14ad2ull, 0xefbe4786384f25e3ull, 0x0fc19dc68b8cd5b5ull, 0x240ca1cc77ac9c65ull,
        0x2de92c6f592b0275ull, 0x4a7484aa6ea6e483ull, 0x5cb0a9dcbd41fbd4ull, 0x76f988da831153b5ull, 0x983e5152ee66dfabull,
        0xa831c66d2db43210ull, 0xb00327c898fb213full, 0xbf597fc7beef0ee4ull, 0xc6e00bf33da88fc2ull, 0xd5a79147930aa725ull,
        0x06ca6351e003826full, 0x142929670a0e6e70ull, 0x27b70a8546d22ffcull, 0x2e1b21385c26c926ull, 0x4d2c6dfc5ac42aedull,
        0x53380d139d95b3dfull, 0x650a73548baf63deull, 0x766a0abb3c77b2a8ull, 0x81c2c92e47edaee6ull, 0x92722c851482353bull,
        0xa2bfe8a14cf10364ull, 0xa81a664bbc423001ull, 0xc24b8b70d0f89791ull, 0xc76c51a30654be30ull, 0xd192e819d6ef5218ull,
        0xd69906245565a910ull, 0xf40e35855771202aull, 0x106aa07032bbd1b8ull, 0x19a4c116b8d2d0c8ull, 0x1e376c085141ab53ull,
        0x2748774cdf8eeb99ull, 0x34b0bcb5e19b48a8ull, 0x391c0cb3c5c95a63ull, 0x4ed8aa4ae3418acbull, 0x5b9cca4f7763e373ull,
        0x682e6ff3d6b2b8a3ull, 0x748f82ee5defb2fcull, 0x78a5636f43172f60ull, 0x84c87814a1f0ab72ull, 0x8cc702081a6439ecull,
        0x90befffa23631e28ull, 0xa4506cebde82bde9ull, 0xbef9a3f7b2c67915ull, 0xc67178f2e372532bull, 0xca273eceea26619cull,
        0xd186b8c721c0c207ull, 0xeada7dd6cde0eb1eull, 0xf57d4f7fee6ed178ull, 0x06f067aa72176fbaull, 0x0a637dc5a2c898a6ull,
        0x113f9804bef90daeull, 0x1b710b35131c471bull, 0x28db77f523047d84ull, 0x32caab7b40c72493ull, 0x3c9ebe0a15c9bebcull,
        0x431d67c49c100d4cull, 0x4cc5d4becb3e42b6ull, 0x597f299cfc657e2aull, 0x5fcb6fab3ad6faecull, 0x6c44198c4a475817ull};
    std::uint64_t w[80];
    for (int i = 0; i < 16; ++i) {
        w[i] = 0;
        for (int j = 0; j < 8; ++j) w[i] = (w[i] << 8) | p[i * 8 + j];
    }
    for (int i = 16; i < 80; ++i) {
        const std::uint64_t s0 = Rot(w[i - 15], 1) ^ Rot(w[i - 15], 8) ^ (w[i - 15] >> 7);
        const std::uint64_t s1 = Rot(w[i - 2], 19) ^ Rot(w[i - 2], 61) ^ (w[i - 2] >> 6);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::uint64_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 80; ++i) {
        const std::uint64_t S1 = Rot(e, 14) ^ Rot(e, 18) ^ Rot(e, 41);
        const std::uint64_t ch = (e & f) ^ (~e & g);
        const std::uint64_t t1 = hh + S1 + ch + K[i] + w[i];
        const std::uint64_t S0 = Rot(a, 28) ^ Rot(a, 34) ^ Rot(a, 39);
        const std::uint64_t mj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint64_t t2 = S0 + mj;
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}
} // namespace detail

// One-shot over several pieces (ed25519 hashes R || A || M without copying M).
inline Digest Hash(std::initializer_list<std::pair<const std::uint8_t*, std::size_t>> parts) {
    std::uint64_t h[8] = {0x6a09e667f3bcc908ull, 0xbb67ae8584caa73bull, 0x3c6ef372fe94f82bull, 0xa54ff53a5f1d36f1ull,
                          0x510e527fade682d1ull, 0x9b05688c2b3e6c1full, 0x1f83d9abfb41bd6bull, 0x5be0cd19137e2179ull};
    std::uint8_t buf[128];
    std::size_t fill = 0;
    std::uint64_t total = 0;
    for (const auto& part : parts) {
        const std::uint8_t* p = part.first;
        std::size_t n = part.second;
        total += n;
        while (n) {
            const std::size_t take = std::min(n, sizeof(buf) - fill);
            std::memcpy(buf + fill, p, take);
            fill += take; p += take; n -= take;
            if (fill == sizeof(buf)) { detail::Block(h, buf); fill = 0; }
        }
    }
    buf[fill++] = 0x80;
    if (fill > 112) { std::memset(buf + fill, 0, 128 - fill); detail::Block(h, buf); fill = 0; }
    std::memset(buf + fill, 0, 128 - fill);
    const std::uint64_t bits = total * 8;   // (messages here are far below 2^61 bytes: high word 0)
    for (int i = 0; i < 8; ++i) buf[127 - i] = (std::uint8_t)(bits >> (8 * i));
    detail::Block(h, buf);
    Digest d{};
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 8; ++j) d[i * 8 + j] = (std::uint8_t)(h[i] >> (56 - 8 * j));
    return d;
}
inline Digest Hash(const std::uint8_t* p, std::size_t n) { return Hash({{p, n}}); }
} // namespace sha512

// ---- ed25519 -----------------------------------------------------------------
namespace ed25519 {
using PublicKey = std::array<std::uint8_t, 32>;
using Seed = std::array<std::uint8_t, 32>;        // the private key (RFC 8032's 32-byte secret)
using Signature = std::array<std::uint8_t, 64>;

// The portable implementation: always compiled (and tested), used when the
// build has no libsodium.
namespace portable {
namespace detail {
typedef std::int64_t gf[16];

inline void Set(gf r, const gf a) { for (int i = 0; i < 16; ++i) r[i] = a[i]; }
inline void Carry(gf o) {
    for (int i = 0; i < 16; ++i) {
        o[i] += (std::int64_t)1 << 16;
        const std::int64_t c = o[i] >> 16;   // arithmetic shift (every supported compiler)
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= c * 65536;
    }
}
inline void Sel(gf p, gf q, int b) {
    const std::int64_t c = ~((std::int64_t)b - 1);
    for (int i = 0; i < 16; ++i) { const std::int64_t t = c & (p[i] ^ q[i]); p[i] ^= t; q[i] ^= t; }
}
inline void Pack(std::uint8_t* o, const gf n) {
    gf m, t;
    Set(t, n);
    Carry(t); Carry(t); Carry(t);
    for (int j = 0; j < 2; ++j) {
        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; ++i) { m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1); m[i - 1] &= 0xffff; }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        const int b = (int)((m[15] >> 16) & 1);
        m[14] &= 0xffff;
        Sel(t, m, 1 - b);
    }
    for (int i = 0; i < 16; ++i) { o[2 * i] = (std::uint8_t)(t[i] & 0xff); o[2 * i + 1] = (std::uint8_t)(t[i] >> 8); }
}
inline int Verify32(const std::uint8_t* x, const std::uint8_t* y) {
    unsigned d = 0;
    for (int i = 0; i < 32; ++i) d |= (unsigned)(x[i] ^ y[i]);
    return (1 & ((d - 1) >> 8)) - 1;   // 0 equal, -1 not
}
inline int Neq(const gf a, const gf b) {
    std::uint8_t c[32], d[32];
    Pack(c, a); Pack(d, b);
    return Verify32(c, d);
}
inline std::uint8_t Par(const gf a) { std::uint8_t d[32]; Pack(d, a); return d[0] & 1; }
inline void Unpack(gf o, const std::uint8_t* n) {
    for (int i = 0; i < 16; ++i) o[i] = n[2 * i] + ((std::int64_t)n[2 * i + 1] << 8);
    o[15] &= 0x7fff;
}
inline void A(gf o, const gf a, const gf b) { for (int i = 0; i < 16; ++i) o[i] = a[i] + b[i]; }
inline void Z(gf o, const gf a, const gf b) { for (int i = 0; i < 16; ++i) o[i] = a[i] - b[i]; }
inline void M(gf o, const gf a, const gf b) {
    std::int64_t t[31] = {0};
    for (int i = 0; i < 16; ++i)
        for (int j = 0; j < 16; ++j) t[i + j] += a[i] * b[j];
    for (int i = 0; i < 15; ++i) t[i] += 38 * t[i + 16];
    for (int i = 0; i < 16; ++i) o[i] = t[i];
    Carry(o); Carry(o);
}
inline void S(gf o, const gf a) { M(o, a, a); }
inline void Inv(gf o, const gf i) {
    gf c;
    Set(c, i);
    for (int a = 253; a >= 0; --a) { S(c, c); if (a != 2 && a != 4) M(c, c, i); }
    Set(o, c);
}
inline void Pow2523(gf o, const gf i) {
    gf c;
    Set(c, i);
    for (int a = 250; a >= 0; --a) { S(c, c); if (a != 1) M(c, c, i); }
    Set(o, c);
}

struct Consts {
    gf gf0 = {0}, gf1 = {1};
    gf D = {0x78a3, 0x1359, 0x4dca, 0x75eb, 0xd8ab, 0x4141, 0x0a4d, 0x0070,
            0xe898, 0x7779, 0x4079, 0x8cc7, 0xfe73, 0x2b6f, 0x6cee, 0x5203};
    gf D2 = {0xf159, 0x26b2, 0x9b94, 0xebd6, 0xb156, 0x8283, 0x149a, 0x00e0,
             0xd130, 0xeef3, 0x80f2, 0x198e, 0xfce7, 0x56df, 0xd9dc, 0x2406};
    gf X = {0xd51a, 0x8f25, 0x2d60, 0xc956, 0xa7b2, 0x9525, 0xc760, 0x692c,
            0xdc5c, 0xfdd6, 0xe231, 0xc0a4, 0x53fe, 0xcd6e, 0x36d3, 0x2169};
    gf Y = {0x6658, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666,
            0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666};
    gf I = {0xa0b0, 0x4a0e, 0x1b27, 0xc4ee, 0xe478, 0xad2f, 0x1806, 0x2f43,
            0xd7a7, 0x3dfb, 0x0099, 0x2b4d, 0xdf0b, 0x4fc1, 0x2480, 0x2b83};
};
inline const Consts& K() { static const Consts k; return k; }

// The group order L, little-endian.
inline const std::int64_t* L() {
    static const std::int64_t l[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7,
                                       0xa2, 0xde, 0xf9, 0xde, 0x14, 0,    0,    0,    0,    0,    0,
                                       0,    0,    0,    0,    0,    0,    0,    0,    0,    0x10};
    return l;
}

inline void Add(gf p[4], gf q[4]) {
    gf a, b, c, d, t, e, f, g, h;
    Z(a, p[1], p[0]); Z(t, q[1], q[0]); M(a, a, t);
    A(b, p[0], p[1]); A(t, q[0], q[1]); M(b, b, t);
    M(c, p[3], q[3]); M(c, c, K().D2);
    M(d, p[2], q[2]); A(d, d, d);
    Z(e, b, a); Z(f, d, c); A(g, d, c); A(h, b, a);
    M(p[0], e, f); M(p[1], h, g); M(p[2], g, f); M(p[3], e, h);
}
inline void CSwap(gf p[4], gf q[4], std::uint8_t b) { for (int i = 0; i < 4; ++i) Sel(p[i], q[i], b); }
inline void PackPoint(std::uint8_t* r, gf p[4]) {
    gf tx, ty, zi;
    Inv(zi, p[2]); M(tx, p[0], zi); M(ty, p[1], zi);
    Pack(r, ty);
    r[31] ^= (std::uint8_t)(Par(tx) << 7);
}
inline void ScalarMult(gf p[4], gf q[4], const std::uint8_t* s) {
    Set(p[0], K().gf0); Set(p[1], K().gf1); Set(p[2], K().gf1); Set(p[3], K().gf0);
    for (int i = 255; i >= 0; --i) {
        const std::uint8_t b = (s[i / 8] >> (i & 7)) & 1;
        CSwap(p, q, b); Add(q, p); Add(p, p); CSwap(p, q, b);
    }
}
inline void ScalarBase(gf p[4], const std::uint8_t* s) {
    gf q[4];
    Set(q[0], K().X); Set(q[1], K().Y); Set(q[2], K().gf1); M(q[3], K().X, K().Y);
    ScalarMult(p, q, s);
}
inline void ModL(std::uint8_t* r, std::int64_t x[64]) {
    const std::int64_t* l = L();
    std::int64_t carry;
    for (int i = 63; i >= 32; --i) {
        carry = 0;
        int j;
        for (j = i - 32; j < i - 12; ++j) {
            x[j] += carry - 16 * x[i] * l[j - (i - 32)];
            carry = (x[j] + 128) >> 8;
            x[j] -= carry * 256;
        }
        x[j] += carry;
        x[i] = 0;
    }
    carry = 0;
    for (int j = 0; j < 32; ++j) { x[j] += carry - (x[31] >> 4) * l[j]; carry = x[j] >> 8; x[j] &= 255; }
    for (int j = 0; j < 32; ++j) x[j] -= carry * l[j];
    for (int i = 0; i < 32; ++i) { x[i + 1] += x[i] >> 8; r[i] = (std::uint8_t)(x[i] & 255); }
}
inline void Reduce(std::uint8_t* r) {
    std::int64_t x[64];
    for (int i = 0; i < 64; ++i) x[i] = (std::int64_t)r[i];
    for (int i = 0; i < 64; ++i) r[i] = 0;
    ModL(r, x);
}
inline int UnpackNeg(gf r[4], const std::uint8_t p[32]) {
    gf t, chk, num, den, den2, den4, den6;
    Set(r[2], K().gf1);
    Unpack(r[1], p);
    S(num, r[1]); M(den, num, K().D); Z(num, num, r[2]); A(den, r[2], den);
    S(den2, den); S(den4, den2); M(den6, den4, den2); M(t, den6, num); M(t, t, den);
    Pow2523(t, t); M(t, t, num); M(t, t, den); M(t, t, den); M(r[0], t, den);
    S(chk, r[0]); M(chk, chk, den);
    if (Neq(chk, num)) M(r[0], r[0], K().I);
    S(chk, r[0]); M(chk, chk, den);
    if (Neq(chk, num)) return -1;
    if (Par(r[0]) == (p[31] >> 7)) Z(r[0], K().gf0, r[0]);
    M(r[3], r[0], r[1]);
    return 0;
}
// S < L (a canonical scalar), as libsodium insists: no malleable signatures.
inline bool ScalarCanonical(const std::uint8_t* s) {
    const std::int64_t* l = L();
    for (int i = 31; i >= 0; --i) {
        if (s[i] < l[i]) return true;
        if (s[i] > l[i]) return false;
    }
    return false;   // == L
}
// The encoded point has small order (8P is the identity): the 8 small-order
// points in all their encodings, i.e. libsodium's ge25519_has_small_order
// blocklist. An encoding that isn't a curve point isn't small-order (it fails
// elsewhere).
inline bool HasSmallOrder(const std::uint8_t enc[32]) {
    gf p[4];
    if (UnpackNeg(p, enc)) return false;          // -P: same order as P
    for (int i = 0; i < 3; ++i) Add(p, p);        // 8P
    std::uint8_t x[32], y[32], z[32], zero[32] = {0};
    Pack(x, p[0]); Pack(y, p[1]); Pack(z, p[2]);
    return Verify32(x, zero) == 0 && Verify32(y, z) == 0;   // (0 : Z : Z) is the identity
}
// y < p (ignoring the sign bit), as libsodium's ge25519_is_canonical.
inline bool CanonicalPoint(const std::uint8_t s[32]) {
    unsigned c = (s[31] & 0x7f) ^ 0x7f;
    for (int i = 30; i > 0; --i) c |= s[i] ^ 0xffu;
    return !(c == 0 && s[0] >= 0xed);
}
inline void Expand(const Seed& seed, std::uint8_t d[64]) {
    const sha512::Digest h = sha512::Hash(seed.data(), seed.size());
    std::memcpy(d, h.data(), 64);
    d[0] &= 248; d[31] &= 127; d[31] |= 64;
}
} // namespace detail

inline PublicKey PublicKeyFromSeed(const Seed& seed) {
    std::uint8_t d[64];
    detail::Expand(seed, d);
    detail::gf p[4];
    PublicKey pk{};
    detail::ScalarBase(p, d);
    detail::PackPoint(pk.data(), p);
    Wipe(d, sizeof(d));
    return pk;
}

inline Signature Sign(const std::uint8_t* m, std::size_t n, const Seed& seed) {
    std::uint8_t d[64];
    detail::Expand(seed, d);
    const PublicKey pk = PublicKeyFromSeed(seed);
    sha512::Digest r = sha512::Hash({{d + 32, 32}, {m, n}});
    detail::Reduce(r.data());
    detail::gf p[4];
    Signature sig{};
    detail::ScalarBase(p, r.data());
    detail::PackPoint(sig.data(), p);
    sha512::Digest h = sha512::Hash({{sig.data(), 32}, {pk.data(), 32}, {m, n}});
    detail::Reduce(h.data());
    std::int64_t x[64] = {0};
    for (int i = 0; i < 32; ++i) x[i] = (std::int64_t)r[i];
    for (int i = 0; i < 32; ++i)
        for (int j = 0; j < 32; ++j) x[i + j] += (std::int64_t)h[i] * (std::int64_t)d[j];
    detail::ModL(sig.data() + 32, x);
    Wipe(d, sizeof(d)); Wipe(r.data(), r.size()); Wipe(x, sizeof(x));
    return sig;
}

// A public key no signature should be accepted for: a non-canonical encoding or
// a small-order point (the identity key "verifies" R = identity, S = 0 for any
// message). libsodium refuses both; so do Verify and ParseKeyList.
inline bool IsWeakKey(const PublicKey& pk) {
    return !detail::CanonicalPoint(pk.data()) || detail::HasSmallOrder(pk.data());
}

inline bool Verify(const std::uint8_t* m, std::size_t n, const Signature& sig, const PublicKey& pk) {
    if (!detail::ScalarCanonical(sig.data() + 32)) return false;
    if (detail::HasSmallOrder(sig.data()) || IsWeakKey(pk)) return false;
    detail::gf p[4], q[4];
    if (detail::UnpackNeg(q, pk.data())) return false;
    sha512::Digest h = sha512::Hash({{sig.data(), 32}, {pk.data(), 32}, {m, n}});
    detail::Reduce(h.data());
    detail::ScalarMult(p, q, h.data());
    detail::ScalarBase(q, sig.data() + 32);
    detail::Add(p, q);
    std::uint8_t t[32];
    detail::PackPoint(t, p);
    return detail::Verify32(sig.data(), t) == 0;
}
} // namespace portable

// The backend every caller uses.
inline const char* Backend() {
#ifdef OKAY_HAVE_SODIUM
    return "libsodium";
#else
    return "portable";
#endif
}

#ifdef OKAY_HAVE_SODIUM
inline bool SodiumReady() {
    static const bool ok = sodium_init() >= 0;
    return ok;
}
#endif

inline PublicKey PublicKeyFromSeed(const Seed& seed) {
#ifdef OKAY_HAVE_SODIUM
    if (SodiumReady()) {
        PublicKey pk{};
        unsigned char sk[crypto_sign_SECRETKEYBYTES];
        crypto_sign_seed_keypair(pk.data(), sk, seed.data());
        Wipe(sk, sizeof(sk));
        return pk;
    }
#endif
    return portable::PublicKeyFromSeed(seed);
}

inline Signature Sign(const std::uint8_t* m, std::size_t n, const Seed& seed) {
#ifdef OKAY_HAVE_SODIUM
    if (SodiumReady()) {
        PublicKey pk{};
        unsigned char sk[crypto_sign_SECRETKEYBYTES];
        crypto_sign_seed_keypair(pk.data(), sk, seed.data());
        Signature sig{};
        crypto_sign_detached(sig.data(), nullptr, m, (unsigned long long)n, sk);
        Wipe(sk, sizeof(sk));
        return sig;
    }
#endif
    return portable::Sign(m, n, seed);
}

inline bool Verify(const std::uint8_t* m, std::size_t n, const Signature& sig, const PublicKey& pk) {
#ifdef OKAY_HAVE_SODIUM
    if (SodiumReady()) return crypto_sign_verify_detached(sig.data(), m, (unsigned long long)n, pk.data()) == 0;
#endif
    return portable::Verify(m, n, sig, pk);
}

inline Signature Sign(const std::string& m, const Seed& seed) {
    return Sign(reinterpret_cast<const std::uint8_t*>(m.data()), m.size(), seed);
}
inline bool Verify(const std::string& m, const Signature& sig, const PublicKey& pk) {
    return Verify(reinterpret_cast<const std::uint8_t*>(m.data()), m.size(), sig, pk);
}
} // namespace ed25519
} // namespace okayac

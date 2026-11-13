#pragma once
// Portable field arithmetic in GF(2^255 - 19): sixteen 16-bit limbs in int64_t, no __int128.
// This is the fallback Fe25519.hpp selects when the compiler has no 128-bit integer (MSVC,
// WebAssembly, 32-bit targets) or when OKAY_CRYPTO_FE16 is defined; the 51-bit version is
// Fe25519_64.hpp. Both have the same interface and are compiled side by side in
// tests/test_crypto_field.cpp, which checks them against each other. UNREVIEWED (see Crypto.hpp).
//
// An element is sixteen signed 64-bit limbs holding 16 bits each (value =
// sum v[i] * 2^(16 i)); 2^256 = 38 (mod p) is how the top folds back. Everything
// is fixed-length loops and masks: no branch or table index depends on a value.
// Limbs may be "loose" (a few bits over 16, or negative) between operations; Mul
// and Square take inputs up to about 2^20 in magnitude and return limbs that are
// 16 bits to within a small excess; Freeze gives the canonical form.
#include <cstdint>

namespace okay::crypto::fe16 {

struct Fe { std::int64_t v[16]; };

inline void Zero(Fe& r) { for (int i = 0; i < 16; ++i) r.v[i] = 0; }
inline void One(Fe& r) { Zero(r); r.v[0] = 1; }
inline void Copy(Fe& r, const Fe& a) { for (int i = 0; i < 16; ++i) r.v[i] = a.v[i]; }
// r = x for a small public constant (x < 2^31).
inline void SetSmall(Fe& r, std::uint32_t x) { Zero(r); r.v[0] = x & 0xffff; r.v[1] = x >> 16; }

inline void Add(Fe& r, const Fe& a, const Fe& b) { for (int i = 0; i < 16; ++i) r.v[i] = a.v[i] + b.v[i]; }
inline void Sub(Fe& r, const Fe& a, const Fe& b) { for (int i = 0; i < 16; ++i) r.v[i] = a.v[i] - b.v[i]; }

// One carry pass: limbs to [0, 2^16), the overflow of limb 15 folded back times 38.
inline void Carry(Fe& r) {
    for (int i = 0; i < 15; ++i) {
        const std::int64_t c = r.v[i] >> 16;          // arithmetic shift: floor division
        r.v[i + 1] += c;
        r.v[i] -= c * 65536;
    }
    const std::int64_t c = r.v[15] >> 16;
    r.v[15] -= c * 65536;
    r.v[0] += 38 * c;
}

inline void Mul(Fe& r, const Fe& a, const Fe& b) {
    std::int64_t t[31];
    for (int i = 0; i < 31; ++i) t[i] = 0;
    for (int i = 0; i < 16; ++i)
        for (int j = 0; j < 16; ++j) t[i + j] += a.v[i] * b.v[j];
    for (int i = 0; i < 15; ++i) t[i] += 38 * t[i + 16];
    Fe o;
    for (int i = 0; i < 16; ++i) o.v[i] = t[i];
    Carry(o);
    Carry(o);
    Copy(r, o);
}
inline void Square(Fe& r, const Fe& a) { Mul(r, a, a); }
// r = a * c for a small public constant c < 2^16.
inline void MulSmall(Fe& r, const Fe& a, std::uint32_t c) { Fe k; SetSmall(k, c); Mul(r, a, k); }
inline void Neg(Fe& r, const Fe& a) { Fe z; Zero(z); Sub(r, z, a); }

// Fully reduce: the unique representative in [0, p), little-endian bytes.
inline void ToBytes(std::uint8_t out[32], const Fe& a) {
    Fe t;
    Copy(t, a);
    Carry(t); Carry(t); Carry(t);
    // Fold bit 255 (2^255 = 19) until the value is below 2^255.
    for (int k = 0; k < 3; ++k) {
        const std::int64_t c = t.v[15] >> 15;
        t.v[15] -= c * 32768;
        t.v[0] += 19 * c;
        for (int i = 0; i < 15; ++i) {
            const std::int64_t d = t.v[i] >> 16;
            t.v[i + 1] += d;
            t.v[i] -= d * 65536;
        }
    }
    // Now 0 <= t < 2^255 with 16-bit limbs. Subtract p once if t >= p.
    std::int64_t u[16];
    std::int64_t borrow = 0;
    for (int i = 0; i < 16; ++i) {
        const std::int64_t pi = (i == 0) ? 0xffed : (i == 15 ? 0x7fff : 0xffff);
        const std::int64_t d = t.v[i] - pi - borrow;
        borrow = (d >> 63) & 1;
        u[i] = d + borrow * 65536;
    }
    const std::int64_t keepT = -borrow;               // all ones when the subtraction borrowed (t < p)
    for (int i = 0; i < 16; ++i) t.v[i] = (t.v[i] & keepT) | (u[i] & ~keepT);
    for (int i = 0; i < 16; ++i) {
        out[2 * i] = static_cast<std::uint8_t>(t.v[i] & 0xff);
        out[2 * i + 1] = static_cast<std::uint8_t>((t.v[i] >> 8) & 0xff);
    }
}

// Load 32 little-endian bytes (all 256 bits; callers mask bit 255 where the spec says to).
inline void FromBytes(Fe& r, const std::uint8_t in[32]) {
    for (int i = 0; i < 16; ++i)
        r.v[i] = static_cast<std::int64_t>(in[2 * i]) | static_cast<std::int64_t>(in[2 * i + 1]) << 8;
}

// bit must be 0 or 1: swaps a and b when it is 1, in constant time.
inline void CSwap(Fe& a, Fe& b, std::int64_t bit) {
    const std::int64_t mask = -bit;
    for (int i = 0; i < 16; ++i) {
        const std::int64_t t = mask & (a.v[i] ^ b.v[i]);
        a.v[i] ^= t; b.v[i] ^= t;
    }
}
// r = bit ? b : a
inline void Select(Fe& r, const Fe& a, const Fe& b, std::int64_t bit) {
    const std::int64_t mask = -bit;
    for (int i = 0; i < 16; ++i) r.v[i] = a.v[i] ^ (mask & (a.v[i] ^ b.v[i]));
}

inline bool IsZero(const Fe& a) {
    std::uint8_t b[32];
    ToBytes(b, a);
    std::uint32_t acc = 0;
    for (int i = 0; i < 32; ++i) acc |= b[i];
    return acc == 0;
}
inline bool Equal(const Fe& a, const Fe& b) { Fe d; Sub(d, a, b); return IsZero(d); }
inline int IsNegative(const Fe& a) { std::uint8_t b[32]; ToBytes(b, a); return b[0] & 1; }

// r = a^e for a PUBLIC exponent (32 little-endian bytes): the loop and the
// multiply pattern depend only on e, never on a.
inline void PowPublic(Fe& r, const Fe& a, const std::uint8_t e[32]) {
    Fe acc;
    One(acc);
    for (int bit = 255; bit >= 0; --bit) {
        Square(acc, acc);
        if ((e[bit >> 3] >> (bit & 7)) & 1) Mul(acc, acc, a);
    }
    Copy(r, acc);
}

#include "Fe25519Chain.inc"

// sqrt(-1) = 2^((p-1)/4) = 2^(2^253 - 5)
inline const Fe& SqrtMinusOne() {
    static const Fe k = [] {
        std::uint8_t e[32];
        for (int i = 0; i < 32; ++i) e[i] = 0xff;
        e[0] = 0xfb; e[31] = 0x1f;
        Fe two; SetSmall(two, 2);
        Fe r; PowPublic(r, two, e);
        return r;
    }();
    return k;
}

} // namespace okay::crypto::fe16

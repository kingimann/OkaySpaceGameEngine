#pragma once
// Field arithmetic in GF(2^255 - 19) with five 51-bit limbs in uint64_t and a 128-bit
// product (radix 2^51). Used when the compiler has __int128 (GCC and Clang on 64-bit
// targets); Fe25519.hpp falls back to the 16-limb Fe25519_16.hpp otherwise. Same interface
// as that file. UNREVIEWED (see ../../include/okay/Crypto/Crypto.hpp).
//
// Representation: value = v[0] + v[1] 2^51 + ... + v[4] 2^204, limbs unsigned. 2^255 = 19.
// Limb bounds (the invariants every function relies on):
//   * Mul, Square, MulSmall take limbs below 2^54 and return limbs <= 2^51 + 2^13.
//   * Add does not reduce: the sum of two reduced elements has limbs < 2^52 + 2^14.
//   * Sub computes a + 4p - b limb by limb (so no limb goes negative while b's limbs are below
//     2^53 - 76), then does one weak carry; it returns limbs <= 2^51 + 2^13 again.
//   So Add of up to four reduced elements may feed Mul, and anything may feed Sub's `a`; the
//   second operand of Sub must be a Mul/Square/Sub output or a sum of at most two of them.
// Everything is fixed-length arithmetic and masks: no branch or table index depends on a value.
#include <cstdint>

namespace okay::crypto::fe64 {

__extension__ typedef unsigned __int128 U128;   // (__extension__: quiet under -Wpedantic)
typedef std::uint64_t U64;

constexpr U64 kMask51 = (static_cast<U64>(1) << 51) - 1;

struct Fe { U64 v[5]; };

inline void Zero(Fe& r) { for (int i = 0; i < 5; ++i) r.v[i] = 0; }
inline void One(Fe& r) { Zero(r); r.v[0] = 1; }
inline void Copy(Fe& r, const Fe& a) { for (int i = 0; i < 5; ++i) r.v[i] = a.v[i]; }
// r = x for a small public constant (x < 2^31).
inline void SetSmall(Fe& r, std::uint32_t x) { Zero(r); r.v[0] = x; }

inline void Add(Fe& r, const Fe& a, const Fe& b) { for (int i = 0; i < 5; ++i) r.v[i] = a.v[i] + b.v[i]; }

// One weak carry pass: limbs to 51 bits, the overflow of limb 4 folded back times 19.
inline void Carry(Fe& r) {
    U64 c = r.v[0] >> 51; r.v[0] &= kMask51; r.v[1] += c;
    c = r.v[1] >> 51; r.v[1] &= kMask51; r.v[2] += c;
    c = r.v[2] >> 51; r.v[2] &= kMask51; r.v[3] += c;
    c = r.v[3] >> 51; r.v[3] &= kMask51; r.v[4] += c;
    c = r.v[4] >> 51; r.v[4] &= kMask51; r.v[0] += 19 * c;
}

inline void Sub(Fe& r, const Fe& a, const Fe& b) {
    // 4p = (2^53 - 76, 2^53 - 4, 2^53 - 4, 2^53 - 4, 2^53 - 4) limb-wise.
    r.v[0] = a.v[0] + 0x1fffffffffffb4ull - b.v[0];
    for (int i = 1; i < 5; ++i) r.v[i] = a.v[i] + 0x1ffffffffffffcull - b.v[i];
    Carry(r);
}
inline void Neg(Fe& r, const Fe& a) { Fe z; Zero(z); Sub(r, z, a); }

namespace detail {
// Carry the five 128-bit column sums down to limbs (each column below 2^115).
inline void Finish(Fe& r, U128 t0, U128 t1, U128 t2, U128 t3, U128 t4) {
    U64 c = static_cast<U64>(t0 >> 51); const U64 r0 = static_cast<U64>(t0) & kMask51;
    t1 += c; c = static_cast<U64>(t1 >> 51); const U64 r1 = static_cast<U64>(t1) & kMask51;
    t2 += c; c = static_cast<U64>(t2 >> 51); const U64 r2 = static_cast<U64>(t2) & kMask51;
    t3 += c; c = static_cast<U64>(t3 >> 51); const U64 r3 = static_cast<U64>(t3) & kMask51;
    t4 += c; c = static_cast<U64>(t4 >> 51); const U64 r4 = static_cast<U64>(t4) & kMask51;
    U64 s0 = r0 + c * 19;                              // c < 2^60 for inputs below 2^54, so 19c < 2^64
    c = s0 >> 51; s0 &= kMask51;
    r.v[0] = s0; r.v[1] = r1 + c; r.v[2] = r2; r.v[3] = r3; r.v[4] = r4;
}
} // namespace detail

inline void Mul(Fe& r, const Fe& a, const Fe& b) {
    const U64 a0 = a.v[0], a1 = a.v[1], a2 = a.v[2], a3 = a.v[3], a4 = a.v[4];
    const U64 b0 = b.v[0], b1 = b.v[1], b2 = b.v[2], b3 = b.v[3], b4 = b.v[4];
    const U64 b1_19 = 19 * b1, b2_19 = 19 * b2, b3_19 = 19 * b3, b4_19 = 19 * b4;
    const U128 t0 = static_cast<U128>(a0) * b0 + static_cast<U128>(a4) * b1_19 + static_cast<U128>(a3) * b2_19 +
                    static_cast<U128>(a2) * b3_19 + static_cast<U128>(a1) * b4_19;
    const U128 t1 = static_cast<U128>(a1) * b0 + static_cast<U128>(a0) * b1 + static_cast<U128>(a4) * b2_19 +
                    static_cast<U128>(a3) * b3_19 + static_cast<U128>(a2) * b4_19;
    const U128 t2 = static_cast<U128>(a2) * b0 + static_cast<U128>(a1) * b1 + static_cast<U128>(a0) * b2 +
                    static_cast<U128>(a4) * b3_19 + static_cast<U128>(a3) * b4_19;
    const U128 t3 = static_cast<U128>(a3) * b0 + static_cast<U128>(a2) * b1 + static_cast<U128>(a1) * b2 +
                    static_cast<U128>(a0) * b3 + static_cast<U128>(a4) * b4_19;
    const U128 t4 = static_cast<U128>(a4) * b0 + static_cast<U128>(a3) * b1 + static_cast<U128>(a2) * b2 +
                    static_cast<U128>(a1) * b3 + static_cast<U128>(a0) * b4;
    detail::Finish(r, t0, t1, t2, t3, t4);
}

inline void Square(Fe& r, const Fe& a) {
    const U64 a0 = a.v[0], a1 = a.v[1], a2 = a.v[2], a3 = a.v[3], a4 = a.v[4];
    const U64 d0 = 2 * a0, d1 = 2 * a1, d2 = 2 * a2, d3 = 2 * a3;
    const U64 a3_19 = 19 * a3, a4_19 = 19 * a4;
    const U128 t0 = static_cast<U128>(a0) * a0 + static_cast<U128>(d1) * a4_19 + static_cast<U128>(d2) * a3_19;
    const U128 t1 = static_cast<U128>(d0) * a1 + static_cast<U128>(d2) * a4_19 + static_cast<U128>(a3) * a3_19;
    const U128 t2 = static_cast<U128>(d0) * a2 + static_cast<U128>(a1) * a1 + static_cast<U128>(d3) * a4_19;
    const U128 t3 = static_cast<U128>(d0) * a3 + static_cast<U128>(d1) * a2 + static_cast<U128>(a4) * a4_19;
    const U128 t4 = static_cast<U128>(d0) * a4 + static_cast<U128>(d1) * a3 + static_cast<U128>(a2) * a2;
    detail::Finish(r, t0, t1, t2, t3, t4);
}

// r = a * c for a small public constant c < 2^17.
inline void MulSmall(Fe& r, const Fe& a, std::uint32_t c) {
    detail::Finish(r, static_cast<U128>(a.v[0]) * c, static_cast<U128>(a.v[1]) * c, static_cast<U128>(a.v[2]) * c,
                   static_cast<U128>(a.v[3]) * c, static_cast<U128>(a.v[4]) * c);
}

// Fully reduce: the unique representative in [0, p), little-endian bytes.
inline void ToBytes(std::uint8_t out[32], const Fe& a) {
    Fe t;
    Copy(t, a);
    Carry(t); Carry(t);                                // now every limb is below 2^51 (value < 2^255)
    // q = 1 exactly when t >= p, i.e. when t + 19 carries out of bit 255.
    U64 q = (t.v[0] + 19) >> 51;
    q = (t.v[1] + q) >> 51;
    q = (t.v[2] + q) >> 51;
    q = (t.v[3] + q) >> 51;
    q = (t.v[4] + q) >> 51;
    t.v[0] += 19 * q;                                  // t + 19q, then drop bit 255: t - p when q = 1
    U64 c = t.v[0] >> 51; t.v[0] &= kMask51; t.v[1] += c;
    c = t.v[1] >> 51; t.v[1] &= kMask51; t.v[2] += c;
    c = t.v[2] >> 51; t.v[2] &= kMask51; t.v[3] += c;
    c = t.v[3] >> 51; t.v[3] &= kMask51; t.v[4] += c;
    t.v[4] &= kMask51;
    const U64 w0 = t.v[0] | (t.v[1] << 51);
    const U64 w1 = (t.v[1] >> 13) | (t.v[2] << 38);
    const U64 w2 = (t.v[2] >> 26) | (t.v[3] << 25);
    const U64 w3 = (t.v[3] >> 39) | (t.v[4] << 12);
    const U64 w[4] = {w0, w1, w2, w3};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 8; ++j) out[8 * i + j] = static_cast<std::uint8_t>(w[i] >> (8 * j));
}

// Load 32 little-endian bytes; bit 255 is ignored (every caller masks it anyway, as the specs say).
inline void FromBytes(Fe& r, const std::uint8_t in[32]) {
    U64 w[4];
    for (int i = 0; i < 4; ++i) {
        w[i] = 0;
        for (int j = 7; j >= 0; --j) w[i] = (w[i] << 8) | in[8 * i + j];
    }
    r.v[0] = w[0] & kMask51;
    r.v[1] = ((w[0] >> 51) | (w[1] << 13)) & kMask51;
    r.v[2] = ((w[1] >> 38) | (w[2] << 26)) & kMask51;
    r.v[3] = ((w[2] >> 25) | (w[3] << 39)) & kMask51;
    r.v[4] = (w[3] >> 12) & kMask51;
}

// bit must be 0 or 1: swaps a and b when it is 1, in constant time.
inline void CSwap(Fe& a, Fe& b, std::int64_t bit) {
    const U64 mask = 0 - static_cast<U64>(bit);
    for (int i = 0; i < 5; ++i) {
        const U64 t = mask & (a.v[i] ^ b.v[i]);
        a.v[i] ^= t; b.v[i] ^= t;
    }
}
// r = bit ? b : a
inline void Select(Fe& r, const Fe& a, const Fe& b, std::int64_t bit) {
    const U64 mask = 0 - static_cast<U64>(bit);
    for (int i = 0; i < 5; ++i) r.v[i] = a.v[i] ^ (mask & (a.v[i] ^ b.v[i]));
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
// multiply pattern depend only on e, never on a. (Reference for the chains; used for sqrt(-1).)
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

} // namespace okay::crypto::fe64

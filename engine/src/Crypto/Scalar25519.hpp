#pragma once
// Scalars modulo the Ed25519 group order L = 2^252 + 27742317777372353535851937790883648493,
// internal to okay::crypto. UNREVIEWED (see ../../include/okay/Crypto/Crypto.hpp).
//
// Barrett reduction on 32-bit limbs with 64-bit intermediate products (no __int128, so it is the same
// code on every compiler). Only fixed-length loops and masks: the work done does not depend on the
// value, so it is safe on secret inputs (the signing nonce, the secret scalar). With
// mu = floor(2^512 / L), q = floor(x mu / 2^512) is floor(x / L) or one less for every x < 2^512, so
// x - q L is in [0, 2L) and two conditional subtractions finish it (the second is belt and braces).
// tests/test_crypto_field.cpp checks it against a bit-by-bit reference, at the edges and on random input.
#include <cstddef>
#include <cstdint>

namespace okay::crypto::sc {

namespace detail {
constexpr std::uint32_t kL[9] = {0x5cf5d3edu, 0x5812631au, 0xa2f79cd6u, 0x14def9deu, 0u, 0u, 0u, 0x10000000u, 0u};
constexpr std::uint32_t kMu[9] = {0x0a2c131bu, 0xed9ce5a3u, 0x086329a7u, 0x2106215du, 0xffffffebu,
                                  0xffffffffu, 0xffffffffu, 0xffffffffu, 0x0000000fu};

// out[0..na+nb) = a * b (schoolbook; out must not alias a or b).
inline void MulLimbs(std::uint32_t* out, const std::uint32_t* a, int na, const std::uint32_t* b, int nb) {
    for (int i = 0; i < na + nb; ++i) out[i] = 0;
    for (int i = 0; i < na; ++i) {
        std::uint64_t carry = 0;
        for (int j = 0; j < nb; ++j) {
            const std::uint64_t t = static_cast<std::uint64_t>(a[i]) * b[j] + out[i + j] + carry;   // <= 2^64 - 1
            out[i + j] = static_cast<std::uint32_t>(t);
            carry = t >> 32;
        }
        out[i + nb] = static_cast<std::uint32_t>(carry);
    }
}

// r (9 limbs) -= L when r >= L, in constant time.
inline void CondSubL(std::uint32_t r[9]) {
    std::uint32_t d[9];
    std::uint64_t borrow = 0;
    for (int i = 0; i < 9; ++i) {
        const std::uint64_t t = static_cast<std::uint64_t>(r[i]) - kL[i] - borrow;
        d[i] = static_cast<std::uint32_t>(t);
        borrow = (t >> 32) & 1;
    }
    const std::uint32_t keepR = 0u - static_cast<std::uint32_t>(borrow);   // all ones when r < L
    for (int i = 0; i < 9; ++i) r[i] = (r[i] & keepR) | (d[i] & ~keepR);
}

// x = 16 little-endian 32-bit limbs (< 2^512) -> 32 bytes of x mod L.
inline void Barrett(std::uint8_t out[32], const std::uint32_t x[16]) {
    std::uint32_t q1[25], ql[17], r[9];
    MulLimbs(q1, x, 16, kMu, 9);
    MulLimbs(ql, q1 + 16, 9, kL, 8);                  // q (9 limbs) * L (8 limbs), only the low 9 limbs matter
    std::uint64_t borrow = 0;
    for (int i = 0; i < 9; ++i) {
        const std::uint64_t t = static_cast<std::uint64_t>(x[i]) - ql[i] - borrow;
        r[i] = static_cast<std::uint32_t>(t);
        borrow = (t >> 32) & 1;
    }
    CondSubL(r);
    CondSubL(r);
    for (int i = 0; i < 8; ++i)
        for (int j = 0; j < 4; ++j) out[4 * i + j] = static_cast<std::uint8_t>(r[i] >> (8 * j));
    for (int i = 0; i < 25; ++i) q1[i] = 0;
    for (int i = 0; i < 17; ++i) ql[i] = 0;
    for (int i = 0; i < 9; ++i) r[i] = 0;
}

inline void LoadLimbs(std::uint32_t* out, const std::uint8_t* in, int nbytes) {
    for (int i = 0; i < nbytes / 4; ++i)
        out[i] = static_cast<std::uint32_t>(in[4 * i]) | static_cast<std::uint32_t>(in[4 * i + 1]) << 8 |
                 static_cast<std::uint32_t>(in[4 * i + 2]) << 16 | static_cast<std::uint32_t>(in[4 * i + 3]) << 24;
}
} // namespace detail

// out = (the 64-byte little-endian integer in) mod L
inline void Reduce64(std::uint8_t out[32], const std::uint8_t in[64]) {
    std::uint32_t x[16];
    detail::LoadLimbs(x, in, 64);
    detail::Barrett(out, x);
    for (int i = 0; i < 16; ++i) x[i] = 0;
}

// out = (a * b + c) mod L for 32-byte little-endian a, b, c with a * b + c < 2^512
// (true when a < 2^256 and b < 2^255, or both below 2^255.5 or so: callers pass a < L, b < 2^255).
inline void MulAdd(std::uint8_t out[32], const std::uint8_t a[32], const std::uint8_t b[32], const std::uint8_t c[32]) {
    std::uint32_t la[8], lb[8], lc[8], x[16];
    detail::LoadLimbs(la, a, 32); detail::LoadLimbs(lb, b, 32); detail::LoadLimbs(lc, c, 32);
    detail::MulLimbs(x, la, 8, lb, 8);
    std::uint64_t carry = 0;
    for (int i = 0; i < 16; ++i) {
        const std::uint64_t t = static_cast<std::uint64_t>(x[i]) + (i < 8 ? lc[i] : 0u) + carry;
        x[i] = static_cast<std::uint32_t>(t);
        carry = t >> 32;
    }
    detail::Barrett(out, x);
    for (int i = 0; i < 8; ++i) { la[i] = lb[i] = lc[i] = 0; }
    for (int i = 0; i < 16; ++i) x[i] = 0;
}

// Public data: is the 32-byte little-endian s below L?
inline bool IsCanonical(const std::uint8_t s[32]) {
    std::uint32_t l[8];
    detail::LoadLimbs(l, s, 32);
    std::uint64_t borrow = 0;
    for (int i = 0; i < 8; ++i) {
        const std::uint64_t t = static_cast<std::uint64_t>(l[i]) - detail::kL[i] - borrow;
        borrow = (t >> 32) & 1;
    }
    return borrow == 1;
}

} // namespace okay::crypto::sc

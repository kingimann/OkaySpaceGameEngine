// Internal arithmetic of okay::crypto, tested against itself and against slow references:
//  * the 51-bit field (fe64) and the portable 16-bit field (fe16) must agree on every operation,
//    on edge values and on long random operation chains; the inversion and square-root-exponent
//    chains must equal the bit-by-bit power;
//  * the Barrett scalar reduction and multiply-add modulo L must equal a bit-by-bit reference
//    (the first implementation this code had), at the edges and on random inputs.
// The code under test is UNREVIEWED (see okay/Crypto/Crypto.hpp). This test includes the internal
// headers directly (engine/src/Crypto), so it only builds where __int128 exists.
#include "test_framework.hpp"
#include "../engine/src/Crypto/Fe25519_16.hpp"
#include "../engine/src/Crypto/Fe25519_64.hpp"
#include "../engine/src/Crypto/Scalar25519.hpp"
#include <cstring>
#include <vector>

using namespace okay::crypto;
using Bytes = std::vector<std::uint8_t>;

static std::uint64_t g_s = 0x243f6a8885a308d3ull;
static std::uint32_t Rnd() { g_s ^= g_s << 13; g_s ^= g_s >> 7; g_s ^= g_s << 17; return static_cast<std::uint32_t>(g_s >> 16); }

// 32 bytes of a field element below 2^255 (not necessarily below p).
static void RndFe(std::uint8_t b[32]) { for (int i = 0; i < 32; ++i) b[i] = static_cast<std::uint8_t>(Rnd()); b[31] &= 0x7f; }

static void EdgeFe(std::uint8_t b[32], int which) {
    std::memset(b, 0, 32);
    switch (which) {
        case 0: break;                                                      // 0
        case 1: b[0] = 1; break;                                            // 1
        case 2: std::memset(b, 0xff, 32); b[31] = 0x7f; b[0] = 0xec; break; // p - 1
        case 3: std::memset(b, 0xff, 32); b[31] = 0x7f; b[0] = 0xed; break; // p
        case 4: std::memset(b, 0xff, 32); b[31] = 0x7f; b[0] = 0xee; break; // p + 1
        case 5: std::memset(b, 0xff, 32); b[31] = 0x7f; break;              // 2^255 - 1
        case 6: b[31] = 0x40; break;                                        // 2^254
        case 7: b[6] = 0x08; break;                                         // 2^51
        case 8: std::memset(b, 0xff, 31); b[31] = 0x3f; break;              // 2^254 - 1
        default: b[(which * 5) % 32] = static_cast<std::uint8_t>(0x80 >> (which % 8)); break;
    }
}

template <class F> static typename F::Fe Load(const std::uint8_t b[32]) { typename F::Fe e; F::FromBytes(e, b); return e; }
struct A16 { using Fe = fe16::Fe; static void FromBytes(Fe& r, const std::uint8_t* b) { fe16::FromBytes(r, b); } };
struct A64 { using Fe = fe64::Fe; static void FromBytes(Fe& r, const std::uint8_t* b) { fe64::FromBytes(r, b); } };

static bool SameBytes(const fe16::Fe& a, const fe64::Fe& b) {
    std::uint8_t x[32], y[32];
    fe16::ToBytes(x, a); fe64::ToBytes(y, b);
    return std::memcmp(x, y, 32) == 0;
}

static void CompareOnce(const std::uint8_t xa[32], const std::uint8_t xb[32]) {
    fe16::Fe a16, b16, r16; fe64::Fe a64, b64, r64;
    fe16::FromBytes(a16, xa); fe16::FromBytes(b16, xb);
    fe64::FromBytes(a64, xa); fe64::FromBytes(b64, xb);
    CHECK(SameBytes(a16, a64) && SameBytes(b16, b64));
    fe16::Mul(r16, a16, b16); fe64::Mul(r64, a64, b64); CHECK(SameBytes(r16, r64));
    fe16::Square(r16, a16); fe64::Square(r64, a64); CHECK(SameBytes(r16, r64));
    fe16::Sub(r16, a16, b16); fe64::Sub(r64, a64, b64); CHECK(SameBytes(r16, r64));
    fe16::Add(r16, a16, b16); fe64::Add(r64, a64, b64); CHECK(SameBytes(r16, r64));
    fe16::Neg(r16, a16); fe64::Neg(r64, a64); CHECK(SameBytes(r16, r64));
    fe16::MulSmall(r16, a16, 121665); fe64::MulSmall(r64, a64, 121665); CHECK(SameBytes(r16, r64));
    fe16::Invert(r16, a16); fe64::Invert(r64, a64); CHECK(SameBytes(r16, r64));
    fe16::Pow22523(r16, a16); fe64::Pow22523(r64, a64); CHECK(SameBytes(r16, r64));
    CHECK(fe16::IsZero(a16) == fe64::IsZero(a64));
    CHECK(fe16::IsNegative(a16) == fe64::IsNegative(a64));
    CHECK(fe16::Equal(a16, b16) == fe64::Equal(a64, b64));
    // Invert is the inverse; a * a^-1 = 1 for a != 0, and 0 maps to 0.
    fe64::Fe inv, one, prod;
    fe64::Invert(inv, a64); fe64::Mul(prod, a64, inv); fe64::One(one);
    CHECK(fe64::IsZero(a64) ? fe64::IsZero(prod) : fe64::Equal(prod, one));
    // The addition chain equals the bit-by-bit power (a public exponent).
    std::uint8_t e[32];
    for (int i = 0; i < 32; ++i) e[i] = 0xff;
    e[0] = 0xeb; e[31] = 0x7f;
    fe64::PowPublic(prod, a64, e); fe64::Invert(inv, a64); CHECK(fe64::Equal(prod, inv));
    for (int i = 0; i < 32; ++i) e[i] = 0xff;
    e[0] = 0xfd; e[31] = 0x0f;
    fe64::PowPublic(prod, a64, e); fe64::Pow22523(inv, a64); CHECK(fe64::Equal(prod, inv));
    // CSwap / Select
    fe64::Fe s1 = a64, s2 = b64, s3;
    fe64::CSwap(s1, s2, 0); CHECK(fe64::Equal(s1, a64) && fe64::Equal(s2, b64));
    fe64::CSwap(s1, s2, 1); CHECK(fe64::Equal(s1, b64) && fe64::Equal(s2, a64));
    fe64::Select(s3, a64, b64, 0); CHECK(fe64::Equal(s3, a64));
    fe64::Select(s3, a64, b64, 1); CHECK(fe64::Equal(s3, b64));
}

// ---- scalar reference: the bit-by-bit reduction the code started with -------------------------
static const std::uint8_t kL[32] = {
    0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10};

static void RefCondSubL(std::uint8_t r[32]) {
    std::uint8_t u[32];
    std::uint32_t borrow = 0;
    for (int i = 0; i < 32; ++i) {
        const std::uint32_t d = static_cast<std::uint32_t>(r[i]) - kL[i] - borrow;
        u[i] = static_cast<std::uint8_t>(d);
        borrow = (d >> 31) & 1;
    }
    const std::uint8_t keepR = static_cast<std::uint8_t>(0u - borrow);
    for (int i = 0; i < 32; ++i) r[i] = static_cast<std::uint8_t>((r[i] & keepR) | (u[i] & ~keepR));
}
static void RefReduce(std::uint8_t out[32], const std::uint8_t* in, std::size_t n) {
    std::uint8_t r[32] = {0};
    for (std::size_t bit = n * 8; bit-- > 0;) {
        std::uint32_t carry = (in[bit >> 3] >> (bit & 7)) & 1;
        for (int i = 0; i < 32; ++i) {
            const std::uint32_t v = (static_cast<std::uint32_t>(r[i]) << 1) | carry;
            r[i] = static_cast<std::uint8_t>(v);
            carry = v >> 8;
        }
        RefCondSubL(r);
    }
    std::memcpy(out, r, 32);
}
static void RefMulAdd(std::uint8_t out[32], const std::uint8_t a[32], const std::uint8_t b[32], const std::uint8_t c[32]) {
    std::uint32_t acc[64] = {0};
    for (int i = 0; i < 32; ++i)
        for (int j = 0; j < 32; ++j) acc[i + j] += static_cast<std::uint32_t>(a[i]) * b[j];
    for (int i = 0; i < 32; ++i) acc[i] += c[i];
    std::uint8_t wide[64];
    std::uint32_t carry = 0;
    for (int i = 0; i < 64; ++i) { const std::uint32_t v = acc[i] + carry; wide[i] = static_cast<std::uint8_t>(v); carry = v >> 8; }
    RefReduce(out, wide, 64);
}

static void Wide(std::uint8_t w[64], int which) {
    std::memset(w, 0, 64);
    switch (which) {
        case 0: break;
        case 1: std::memset(w, 0xff, 64); break;                           // 2^512 - 1
        case 2: std::memcpy(w, kL, 32); break;                             // L
        case 3: std::memcpy(w, kL, 32); w[0] -= 1; break;                  // L - 1
        case 4: std::memcpy(w, kL, 32); w[0] += 1; break;                  // L + 1
        case 5: std::memcpy(w, kL, 32); for (int i = 0; i < 32; ++i) { unsigned v = w[i] * 2u; w[i] = static_cast<std::uint8_t>(v); if (i < 31) w[i + 1] = static_cast<std::uint8_t>(w[i + 1] | 0); } break; // (not exact 2L; just an edge-ish value)
        case 6: w[63] = 0x80; break;                                       // 2^511
        case 7: w[31] = 0x10; break;                                       // 2^252
        case 8: std::memset(w, 0xff, 32); break;                           // 2^256 - 1
        default: {                                                         // k * L + small, for several k
            const std::uint32_t k = static_cast<std::uint32_t>(which) * 0x01010101u + 7u;
            std::uint64_t carry = 0;
            for (int i = 0; i < 32; ++i) { const std::uint64_t v = static_cast<std::uint64_t>(kL[i]) * k + carry; w[i] = static_cast<std::uint8_t>(v); carry = v >> 8; }
            for (int i = 32; i < 40 && carry; ++i) { w[i] = static_cast<std::uint8_t>(carry); carry >>= 8; }
            break;
        }
    }
}

int main() {
    RUN_SUITE("crypto_field");
    {   // field: edge values against each other, then random
        std::uint8_t a[32], b[32];
        for (int i = 0; i < 40; ++i)
            for (int j = 0; j < 40; ++j) { EdgeFe(a, i); EdgeFe(b, j); CompareOnce(a, b); }
        for (int i = 0; i < 3000; ++i) { RndFe(a); RndFe(b); CompareOnce(a, b); }
    }
    {   // long chains of operations (respecting the documented bounds; fe16 limbs are carried after a Sub,
        // which is what its callers' short Sub-then-Mul sequences amount to) must stay equal
        std::uint8_t xa[32], xb[32];
        for (int round = 0; round < 200; ++round) {
            RndFe(xa); RndFe(xb);
            if (round < 40) { EdgeFe(xa, round); }
            fe16::Fe a16, b16; fe64::Fe a64, b64, t64; fe16::Fe t16;
            fe16::FromBytes(a16, xa); fe16::FromBytes(b16, xb); fe64::FromBytes(a64, xa); fe64::FromBytes(b64, xb);
            for (int step = 0; step < 300; ++step) {
                switch (Rnd() % 6) {
                    case 0: fe16::Mul(a16, a16, b16); fe64::Mul(a64, a64, b64); break;
                    case 1: fe16::Square(a16, a16); fe64::Square(a64, a64); break;
                    case 2: fe16::Sub(a16, a16, b16); fe16::Carry(a16); fe64::Sub(a64, a64, b64); break;
                    case 3: fe16::Add(t16, a16, b16); fe16::Mul(a16, t16, t16); fe64::Add(t64, a64, b64); fe64::Mul(a64, t64, t64); break;
                    case 4: fe16::Sub(b16, b16, a16); fe16::Carry(b16); fe64::Sub(b64, b64, a64); break;
                    default: fe16::Add(t16, a16, a16); fe16::Sub(b16, t16, b16); fe16::Carry(b16); fe64::Add(t64, a64, a64); fe64::Sub(b64, t64, b64); break;
                }
                if (step % 37 == 0) CHECK(SameBytes(a16, a64) && SameBytes(b16, b64));
            }
            CHECK(SameBytes(a16, a64) && SameBytes(b16, b64));
        }
    }
    {   // scalars
        std::uint8_t w[64], got[32], want[32];
        for (int i = 0; i < 80; ++i) {
            Wide(w, i);
            sc::Reduce64(got, w); RefReduce(want, w, 64); CHECK(std::memcmp(got, want, 32) == 0);
        }
        for (int i = 0; i < 4000; ++i) {
            for (int j = 0; j < 64; ++j) w[j] = static_cast<std::uint8_t>(Rnd());
            if (i % 4 == 1) std::memset(w + 32, 0, 32);
            if (i % 4 == 2) std::memset(w + 40, 0xff, 24);
            sc::Reduce64(got, w); RefReduce(want, w, 64); CHECK(std::memcmp(got, want, 32) == 0);
        }
        std::uint8_t a[32], b[32], c[32];
        for (int i = 0; i < 3000; ++i) {
            for (int j = 0; j < 32; ++j) { a[j] = static_cast<std::uint8_t>(Rnd()); b[j] = static_cast<std::uint8_t>(Rnd()); c[j] = static_cast<std::uint8_t>(Rnd()); }
            a[31] &= 0x0f; b[31] &= 0x7f;                                     // a < 2^252 or so (a reduced scalar), b clamped-sized
            if (i % 5 == 0) { std::memcpy(a, kL, 32); a[0] -= 1; }            // a = L - 1
            if (i % 7 == 0) std::memset(b, 0xff, 32), b[31] = 0x7f;            // b = 2^255 - 1
            if (i % 11 == 0) std::memset(c, 0xff, 32);
            sc::MulAdd(got, a, b, c); RefMulAdd(want, a, b, c); CHECK(std::memcmp(got, want, 32) == 0);
        }
        // IsCanonical: L - 1 yes, L no, L + 1 no, 0 yes, 2^256 - 1 no
        std::uint8_t s[32];
        std::memcpy(s, kL, 32); s[0] -= 1; CHECK(sc::IsCanonical(s));
        std::memcpy(s, kL, 32); CHECK(!sc::IsCanonical(s));
        std::memcpy(s, kL, 32); s[0] += 1; CHECK(!sc::IsCanonical(s));
        std::memset(s, 0, 32); CHECK(sc::IsCanonical(s));
        std::memset(s, 0xff, 32); CHECK(!sc::IsCanonical(s));
    }
    TEST_MAIN_RESULT();
}

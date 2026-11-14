// Ed25519 (RFC 8032 section 5.1), pure, detached signatures. UNREVIEWED (see Crypto.hpp).
//
// Curve arithmetic is on the twisted Edwards curve -x^2 + y^2 = 1 + d x^2 y^2 in extended
// coordinates (X:Y:Z:T). The addition formula is the unified one that is complete for this curve,
// so no input is special; doubling has its own cheaper formula.
//
// Secret scalars (signing, public key from a seed) only ever go through BaseMult: the scalar is
// recoded into 64 signed 4-bit digits and, for each digit, ALL eight entries of that window's
// precomputed table are read and combined with masks (a constant-time lookup), so neither the
// memory addresses nor the operations depend on the scalar. The table holds j * 16^(2i) * B
// (j = 1..8, i = 0..31) in affine "cached" form; it is derived from the curve constants on first
// use (thread-safe function-local static), nothing is pasted in.
// Verification works on public data only (signature, message, public key), so it is VARIABLE TIME on purpose:
// one interleaved double-scalar multiplication [S]B + [h](-A) (Straus/Shamir) over width-5 and width-7 non-adjacent
// forms, with a precomputed table of odd multiples of the base point (derived on first use, like the signing table),
// shared doublings, doublings that skip the T coordinate when no addition follows, and the small-order checks done by
// comparing encodings against the seven-entry blocklist instead of decoding the points a second time.
// d, sqrt(-1) and the base point are derived from their definitions on first use.
#include <okay/Crypto/Crypto.hpp>
#include "Fe25519.hpp"
#include "Scalar25519.hpp"

namespace okay::crypto {
namespace {

using fe::Fe;

struct Point { Fe X, Y, Z, T; };
// A point with Z = 1 in the form the mixed addition wants: y + x, y - x, 2 d x y.
struct Cached { Fe yplusx, yminusx, t2d; };

struct Consts {
    Fe d, d2;
    Point base;
    Cached table[32][8];                              // table[i][j] = (j + 1) * 16^(2 i) * B
    Cached baseOdd[32];                               // baseOdd[i] = (2 i + 1) * B, for the width-7 form (verification only)
};

// A cached point that keeps Z (projective), for tables built from a variable point: no inversion to build.
struct CachedP { Fe yplusx, yminusx, z2, t2d; };

void SetIdentity(Point& p) { fe::Zero(p.X); fe::One(p.Y); fe::One(p.Z); fe::Zero(p.T); }

// Decode per RFC 8032 section 5.1.3. Does NOT insist on a canonical y (callers do);
// fails when there is no x, or x is 0 with the sign bit set. Public data only.
bool Decode(Point& p, const Fe& d, const std::uint8_t in[32]) {
    using namespace fe;
    std::uint8_t yb[32];
    for (int i = 0; i < 32; ++i) yb[i] = in[i];
    const int sign = yb[31] >> 7;
    yb[31] &= 127;
    Fe y, one, u, v, v3, v7, x, t, chk, negU;
    FromBytes(y, yb);
    One(one);
    Square(u, y);                    // y^2
    Mul(v, d, u);
    Add(v, v, one);                  // d y^2 + 1
    Sub(u, u, one);                  // y^2 - 1
    Square(v3, v); Mul(v3, v3, v);   // v^3
    Square(v7, v3); Mul(v7, v7, v);  // v^7
    Mul(t, u, v7);
    Pow22523(t, t);                  // (u v^7)^((p-5)/8)
    Mul(t, t, v3);
    Mul(x, t, u);                    // candidate x = u v^3 (u v^7)^((p-5)/8)
    Square(chk, x); Mul(chk, chk, v);   // v x^2
    Neg(negU, u);
    if (Equal(chk, u)) {
        // x is right
    } else if (Equal(chk, negU)) {
        Mul(x, x, SqrtMinusOne());
    } else {
        return false;
    }
    if (IsZero(x) && sign) return false;
    if (IsNegative(x) != sign) Neg(x, x);
    Copy(p.X, x); Copy(p.Y, y); One(p.Z); Mul(p.T, x, y);
    return true;
}

void EncodeWithInverse(std::uint8_t out[32], const Point& p, const Fe& zi) {
    using namespace fe;
    Fe x, y;
    Mul(x, p.X, zi);
    Mul(y, p.Y, zi);
    ToBytes(out, y);
    out[31] = static_cast<std::uint8_t>(out[31] | (IsNegative(x) << 7));
}

void Encode(std::uint8_t out[32], const Point& p) {
    fe::Fe zi;
    fe::Invert(zi, p.Z);
    EncodeWithInverse(out, p, zi);
}

// Encode two points with ONE inversion (Montgomery's trick: 1/(Z1 Z2), then Z2 and Z1 multiply it into 1/Z1 and 1/Z2).
void Encode2(std::uint8_t out1[32], const Point& p1, std::uint8_t out2[32], const Point& p2) {
    using namespace fe;
    Fe zz, inv, zi1, zi2;
    Mul(zz, p1.Z, p2.Z);
    Invert(inv, zz);
    Mul(zi1, inv, p2.Z);
    Mul(zi2, inv, p1.Z);
    EncodeWithInverse(out1, p1, zi1);
    EncodeWithInverse(out2, p2, zi2);
}

// Unified addition (add-2008-hwcd-3 with a = -1).
void PointAdd(Point& r, const Point& p, const Point& q, const Fe& d2) {
    using namespace fe;
    Fe a, b, c, d, e, f, g, h, t1, t2;
    Sub(t1, p.Y, p.X); Sub(t2, q.Y, q.X); Mul(a, t1, t2);
    Add(t1, p.Y, p.X); Add(t2, q.Y, q.X); Mul(b, t1, t2);
    Mul(c, p.T, q.T); Mul(c, c, d2);
    Mul(d, p.Z, q.Z); Add(d, d, d);
    Sub(e, b, a); Sub(f, d, c); Add(g, d, c); Add(h, b, a);
    Mul(r.X, e, f); Mul(r.Y, g, h); Mul(r.T, e, h); Mul(r.Z, f, g);
}

// Doubling (dbl-2008-hwcd with a = -1): 4 multiplications and 4 squarings.
void PointDouble(Point& r, const Point& p) {
    using namespace fe;
    Fe a, b, c, e, f, g, h, t;
    Square(a, p.X); Square(b, p.Y); Square(c, p.Z); Add(c, c, c);
    Add(t, p.X, p.Y); Square(t, t);
    Sub(e, t, a); Sub(e, e, b);                       // E = (X+Y)^2 - A - B
    Sub(g, b, a);                                     // G = D + B with D = -A
    Sub(f, g, c);                                     // F = G - C
    Add(t, a, b); Neg(h, t);                          // H = D - B = -(A + B)
    Mul(r.X, e, f); Mul(r.Y, g, h); Mul(r.T, e, h); Mul(r.Z, f, g);
}

// Doubling that leaves r.T unset when the caller will double again (or finish) instead of adding: 3 multiplications and
// 4 squarings. Verification only (a following addition needs T: pass needT).
void PointDoubleMaybeT(Point& r, const Point& p, bool needT) {
    using namespace fe;
    Fe a, b, c, e, f, g, h, t;
    Square(a, p.X); Square(b, p.Y); Square(c, p.Z); Add(c, c, c);
    Add(t, p.X, p.Y); Square(t, t);
    Sub(e, t, a); Sub(e, e, b);
    Sub(g, b, a);
    Sub(f, g, c);
    Add(t, a, b); Neg(h, t);
    Mul(r.X, e, f); Mul(r.Y, g, h); Mul(r.Z, f, g);
    if (needT) Mul(r.T, e, h);
}

// r = p + q with q in cached affine form (7 multiplications).
void AddCached(Point& r, const Point& p, const Cached& q) {
    using namespace fe;
    Fe a, b, c, d, e, f, g, h, t;
    Sub(t, p.Y, p.X); Mul(a, t, q.yminusx);
    Add(t, p.Y, p.X); Mul(b, t, q.yplusx);
    Mul(c, p.T, q.t2d);
    Add(d, p.Z, p.Z);
    Sub(e, b, a); Sub(f, d, c); Add(g, d, c); Add(h, b, a);
    Mul(r.X, e, f); Mul(r.Y, g, h); Mul(r.T, e, h); Mul(r.Z, f, g);
}

void ToCached(Cached& c, const Point& p, const Fe& d2) {
    using namespace fe;
    Fe zi, x, y, xy;
    Invert(zi, p.Z);
    Mul(x, p.X, zi); Mul(y, p.Y, zi);
    Add(c.yplusx, y, x);
    Sub(c.yminusx, y, x);
    Mul(xy, x, y); Mul(c.t2d, xy, d2);
}

// r = p - q (q cached affine): the negative of q swaps y+x and y-x and negates 2dxy, which swaps F and G.
void SubCached(Point& r, const Point& p, const Cached& q) {
    using namespace fe;
    Fe a, b, c, d, e, f, g, h, t;
    Sub(t, p.Y, p.X); Mul(a, t, q.yplusx);
    Add(t, p.Y, p.X); Mul(b, t, q.yminusx);
    Mul(c, p.T, q.t2d);
    Add(d, p.Z, p.Z);
    Sub(e, b, a); Add(f, d, c); Sub(g, d, c); Add(h, b, a);
    Mul(r.X, e, f); Mul(r.Y, g, h); Mul(r.T, e, h); Mul(r.Z, f, g);
}

void ToCachedP(CachedP& c, const Point& p, const Fe& d2) {
    using namespace fe;
    Add(c.yplusx, p.Y, p.X);
    Sub(c.yminusx, p.Y, p.X);
    Add(c.z2, p.Z, p.Z);
    Mul(c.t2d, p.T, d2);
}

// r = p + q or p - q (negate) with q projective-cached (8 multiplications).
void AddCachedP(Point& r, const Point& p, const CachedP& q, bool negate) {
    using namespace fe;
    Fe a, b, c, d, e, f, g, h, t;
    Sub(t, p.Y, p.X); Mul(a, t, negate ? q.yplusx : q.yminusx);
    Add(t, p.Y, p.X); Mul(b, t, negate ? q.yminusx : q.yplusx);
    Mul(c, p.T, q.t2d);
    Mul(d, p.Z, q.z2);
    Sub(e, b, a); Add(h, b, a);
    if (negate) { Add(f, d, c); Sub(g, d, c); } else { Sub(f, d, c); Add(g, d, c); }
    Mul(r.X, e, f); Mul(r.Y, g, h); Mul(r.T, e, h); Mul(r.Z, f, g);
}

// ---- constant-time fixed-base multiplication ----------------------------------------------
void CMoveCached(Cached& r, const Cached& a, std::int64_t bit) {   // r = bit ? a : r
    fe::Select(r.yplusx, r.yplusx, a.yplusx, bit);
    fe::Select(r.yminusx, r.yminusx, a.yminusx, bit);
    fe::Select(r.t2d, r.t2d, a.t2d, bit);
}

// t = digit * 16^(2 pos) * B for a digit in -8..8, reading all eight table entries whatever the digit.
void SelectBase(Cached& t, const Consts& k, int pos, std::int32_t digit) {
    const std::uint32_t sign = static_cast<std::uint32_t>(digit) >> 31;              // 1 when negative
    const std::int32_t negMask = -static_cast<std::int32_t>(sign);
    const std::uint32_t babs = static_cast<std::uint32_t>((digit ^ negMask) - negMask);
    fe::One(t.yplusx); fe::One(t.yminusx); fe::Zero(t.t2d);   // digit 0: the identity
    for (int j = 0; j < 8; ++j) {
        const std::uint32_t x = babs ^ static_cast<std::uint32_t>(j + 1);
        const std::int64_t eq = static_cast<std::int64_t>((x - 1u) >> 31);   // 1 when babs == j + 1, else 0
        CMoveCached(t, k.table[pos][j], eq);
    }
    Cached n;                                          // the negative: swap y+x and y-x, negate 2dxy
    fe::Copy(n.yplusx, t.yminusx); fe::Copy(n.yminusx, t.yplusx); fe::Neg(n.t2d, t.t2d);
    CMoveCached(t, n, static_cast<std::int64_t>(sign));
    MemZero(&n, sizeof n);
}

// r = a * B for a 32-byte little-endian scalar a < 2^255 (clamped scalars and reduced scalars both are).
void BaseMult(Point& r, const std::uint8_t a[32], const Consts& k) {
    std::int32_t e[64];
    for (int i = 0; i < 32; ++i) { e[2 * i] = a[i] & 15; e[2 * i + 1] = (a[i] >> 4) & 15; }
    std::int32_t carry = 0;                            // digits 0..15 -> -8..8 with a carry upwards
    for (int i = 0; i < 63; ++i) {
        e[i] += carry;
        carry = (e[i] + 8) >> 4;
        e[i] -= carry * 16;
    }
    e[63] += carry;                                    // a < 2^255, so e[63] <= 8
    Point h;
    Cached t;
    SetIdentity(h);
    for (int i = 1; i < 64; i += 2) { SelectBase(t, k, i / 2, e[i]); AddCached(h, h, t); }
    for (int i = 0; i < 4; ++i) PointDouble(h, h);     // times 16
    for (int i = 0; i < 64; i += 2) { SelectBase(t, k, i / 2, e[i]); AddCached(h, h, t); }
    r = h;
    MemZero(e, sizeof e); MemZero(&h, sizeof h); MemZero(&t, sizeof t);
}

// Non-adjacent form with window w (2..7): digits are zero or odd with |d| < 2^(w-1), no two nonzero digits closer than w
// positions, sum d[i] 2^i = s. Variable time: PUBLIC scalars only. s < 2^255; the array is longer than 256 because the
// last carry can land a few positions above the top bit.
constexpr int kNafLen = 264;
void Wnaf(std::int8_t naf[kNafLen], const std::uint8_t s[32], int w) {
    std::uint64_t x[6] = {0, 0, 0, 0, 0, 0};
    for (int i = 0; i < 32; ++i) x[i >> 3] |= static_cast<std::uint64_t>(s[i]) << (8 * (i & 7));
    for (int i = 0; i < kNafLen; ++i) naf[i] = 0;
    const std::uint64_t width = std::uint64_t(1) << w, mask = width - 1;
    std::uint64_t carry = 0;
    int pos = 0;
    while (pos < kNafLen) {
        const int idx = pos >> 6, bit = pos & 63;
        const std::uint64_t buf = bit < 64 - w ? x[idx] >> bit : (x[idx] >> bit) | (x[idx + 1] << (64 - bit));
        const std::uint64_t window = carry + (buf & mask);
        if ((window & 1) == 0) { ++pos; continue; }
        if (window < width / 2) { carry = 0; naf[pos] = static_cast<std::int8_t>(window); }
        else { carry = 1; naf[pos] = static_cast<std::int8_t>(static_cast<std::int64_t>(window) - static_cast<std::int64_t>(width)); }
        pos += w;
    }
}

constexpr int kWnafA = 5;   // 8 odd multiples of the variable point
constexpr int kWnafB = 7;   // 32 odd multiples of the base point

// r = [sb] B + [sa] P: both scalars public and below 2^255. Straus/Shamir: one chain of doublings shared by both.
void DoubleScalarMultPublic(Point& r, const std::uint8_t sb[32], const Point& P, const std::uint8_t sa[32], const Consts& k) {
    std::int8_t nafA[kNafLen], nafB[kNafLen];
    Wnaf(nafA, sa, kWnafA);
    Wnaf(nafB, sb, kWnafB);
    CachedP tabA[8];                                  // tabA[i] = (2 i + 1) P
    {
        Point twoP, acc = P;
        PointDouble(twoP, P);
        ToCachedP(tabA[0], acc, k.d2);
        for (int i = 1; i < 8; ++i) { PointAdd(acc, acc, twoP, k.d2); ToCachedP(tabA[i], acc, k.d2); }
    }
    int top = kNafLen - 1;
    while (top >= 0 && nafA[top] == 0 && nafB[top] == 0) --top;
    Point acc;
    SetIdentity(acc);
    for (int i = top; i >= 0; --i) {
        const bool add = nafA[i] != 0 || nafB[i] != 0;
        if (i < top) PointDoubleMaybeT(acc, acc, add);
        if (nafA[i] > 0) AddCachedP(acc, acc, tabA[nafA[i] >> 1], false);
        else if (nafA[i] < 0) AddCachedP(acc, acc, tabA[(-nafA[i]) >> 1], true);
        if (nafB[i] > 0) AddCached(acc, acc, k.baseOdd[nafB[i] >> 1]);
        else if (nafB[i] < 0) SubCached(acc, acc, k.baseOdd[(-nafB[i]) >> 1]);
    }
    r = acc;
}

Consts MakeConsts() {
    using namespace fe;
    Consts c;
    Fe n, den, inv;
    SetSmall(n, 121665);
    SetSmall(den, 121666);
    Invert(inv, den);
    Mul(c.d, n, inv);
    Neg(c.d, c.d);                                    // d = -121665/121666
    fe::Add(c.d2, c.d, c.d);
    // Base point: y = 4/5, x even (RFC 8032 encodes it as 0x58 then 0x66 x31).
    std::uint8_t enc[32];
    for (int i = 0; i < 32; ++i) enc[i] = 0x66;
    enc[0] = 0x58;
    Decode(c.base, c.d, enc);
    Point bi = c.base;                                // 16^(2 i) * B
    for (int i = 0; i < 32; ++i) {
        Point acc = bi;
        for (int j = 0; j < 8; ++j) {
            ToCached(c.table[i][j], acc, c.d2);
            PointAdd(acc, acc, bi, c.d2);
        }
        for (int j = 0; j < 8; ++j) PointDouble(bi, bi);   // * 256
    }
    {
        Point twoB, acc = c.base;
        PointDouble(twoB, c.base);
        for (int i = 0; i < 32; ++i) { ToCached(c.baseOdd[i], acc, c.d2); PointAdd(acc, acc, twoB, c.d2); }
    }
    return c;
}

const Consts& GetConsts() {
    static const Consts k = MakeConsts();
    return k;
}

bool YIsCanonical(const std::uint8_t enc[32]) {      // public data: y < p, ignoring the sign bit
    if ((enc[31] & 0x7f) != 0x7f) return true;
    for (int i = 30; i >= 1; --i) if (enc[i] != 0xff) return true;
    return enc[0] < 0xed;
}

// A point of order 1, 2, 4 or 8 (including the identity), by its ENCODING with the sign bit ignored: the y coordinates of
// those points are 0, 1, -1 and the two of order 8, and the encodings y = p and y = p + 1 (non-canonical forms of 0 and 1)
// decode to small-order points too: seven values in all, libsodium's blocklist. Negating a point does not change its order,
// so the sign bit is ignored; this also reports the two encodings RFC 8032 refuses to decode (x = 0 with the sign bit set:
// 0100..80 and ecff..ff), as libsodium's blocklist and the port in shared/AntiCheatCrypto.hpp do.
// (Earlier versions decoded the point and doubled it three times: same set, one square root more per call. The agreement is
// tested against the port, which still decodes, and libsodium, on every structured encoding: tests/crypto_difftest.cpp.)
bool HasSmallOrderEncoding(const std::uint8_t enc[32]) {
    static const std::uint8_t kBlock[7][32] = {
        {0x00, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x00},
        {0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x00},
        {0x26, 0xe8, 0x95, 0x8f, 0xc2, 0xb2, 0x27, 0xb0, 0x45, 0xc3, 0xf4, 0x89, 0xf2, 0xef, 0x98, 0xf0,
         0xd5, 0xdf, 0xac, 0x05, 0xd3, 0xc6, 0x33, 0x39, 0xb1, 0x38, 0x02, 0x88, 0x6d, 0x53, 0xfc, 0x05},
        {0xc7, 0x17, 0x6a, 0x70, 0x3d, 0x4d, 0xd8, 0x4f, 0xba, 0x3c, 0x0b, 0x76, 0x0d, 0x10, 0x67, 0x0f,
         0x2a, 0x20, 0x53, 0xfa, 0x2c, 0x39, 0xcc, 0xc6, 0x4e, 0xc7, 0xfd, 0x77, 0x92, 0xac, 0x03, 0x7a},
        {0xec, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
         0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f},
        {0xed, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
         0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f},
        {0xee, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
         0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f},
    };
    for (const auto& b : kBlock) {
        bool eq = true;
        for (int i = 0; i < 31; ++i) eq = eq && enc[i] == b[i];
        if (eq && (enc[31] & 0x7f) == b[31]) return true;
    }
    return false;
}

void Clamp(std::uint8_t a[32]) { a[0] &= 248; a[31] &= 127; a[31] |= 64; }

} // namespace

void Ed25519PublicFromSeed(std::uint8_t pk[32], const std::uint8_t seed[32]) {
    const Consts& k = GetConsts();
    std::uint8_t h[64];
    Sha512Hash(h, seed, 32);
    Clamp(h);
    Point A;
    BaseMult(A, h, k);
    Encode(pk, A);
    MemZero(h, sizeof h); MemZero(&A, sizeof A);
}

void Ed25519SeedKeypair(std::uint8_t pk[32], std::uint8_t sk[64], const std::uint8_t seed[32]) {
    std::uint8_t p[32];
    Ed25519PublicFromSeed(p, seed);
    for (int i = 0; i < 32; ++i) { sk[i] = seed[i]; sk[32 + i] = p[i]; pk[i] = p[i]; }
}

void Ed25519Keypair(std::uint8_t pk[32], std::uint8_t sk[64]) {
    std::uint8_t seed[32];
    RandomBytes(seed, 32);
    Ed25519SeedKeypair(pk, sk, seed);
    MemZero(seed, sizeof seed);
}

namespace {
// The signing core. h = SHA-512(seed) clamped is the secret scalar a (first half) and the nonce prefix (second half).
// pkOrNull: the public key to hash into k. When null it is derived from the seed (one more fixed-base multiplication).
void SignCore(std::uint8_t sig[64], const std::uint8_t* m, std::size_t n, const std::uint8_t seed[32], const std::uint8_t* pkOrNull) {
    const Consts& k = GetConsts();
    std::uint8_t h[64], pk[32];
    Sha512Hash(h, seed, 32);
    Clamp(h);                                         // h[0..32] = secret scalar a, h[32..64] = prefix
    std::uint8_t digest[64], r[32], kk[32];
    {
        Sha512 s;
        s.Update(h + 32, 32);
        if (n) s.Update(m, n);
        s.Final(digest);
        sc::Reduce64(r, digest);
    }
    Point R;
    BaseMult(R, r, k);
    if (pkOrNull) {
        for (int i = 0; i < 32; ++i) pk[i] = pkOrNull[i];
        Encode(sig, R);
    } else {
        Point A;
        BaseMult(A, h, k);
        Encode2(sig, R, pk, A);                       // both encodings share one inversion
        MemZero(&A, sizeof A);
    }
    {
        Sha512 s;
        s.Update(sig, 32); s.Update(pk, 32);
        if (n) s.Update(m, n);
        s.Final(digest);
        sc::Reduce64(kk, digest);
    }
    sc::MulAdd(sig + 32, kk, h, r);                   // S = k a + r (mod L)
    MemZero(h, sizeof h); MemZero(digest, sizeof digest); MemZero(r, sizeof r); MemZero(&R, sizeof R);
}
} // namespace

void Ed25519Sign(std::uint8_t sig[64], const std::uint8_t* m, std::size_t n, const std::uint8_t sk[64]) {
    SignCore(sig, m, n, sk, nullptr);                 // derives the public key from the seed: the tail of sk is never trusted
}

void Ed25519SignTrustedPk(std::uint8_t sig[64], const std::uint8_t* m, std::size_t n, const std::uint8_t sk[64]) {
    SignCore(sig, m, n, sk, sk + 32);                 // libsodium's behaviour: sk = seed || public key, the tail is used as is
}

bool Ed25519IsWeakKey(const std::uint8_t pk[32]) {
    return !YIsCanonical(pk) || HasSmallOrderEncoding(pk);
}

bool Ed25519Verify(const std::uint8_t sig[64], const std::uint8_t* m, std::size_t n, const std::uint8_t pk[32]) {
    // Everything here is public data: variable time is fine (and intended).
    const Consts& k = GetConsts();
    if (!sc::IsCanonical(sig + 32)) return false;
    if (HasSmallOrderEncoding(sig)) return false;
    if (!YIsCanonical(pk) || HasSmallOrderEncoding(pk)) return false;
    Point A;
    if (!Decode(A, k.d, pk)) return false;
    // -A
    fe::Neg(A.X, A.X); fe::Neg(A.T, A.T);
    std::uint8_t digest[64], h[32];
    {
        Sha512 s;
        s.Update(sig, 32); s.Update(pk, 32);
        if (n) s.Update(m, n);
        s.Final(digest);
        sc::Reduce64(h, digest);
    }
    Point sum;
    DoubleScalarMultPublic(sum, sig + 32, A, h, k);   // [S] B + [h] (-A)
    std::uint8_t enc[32];
    Encode(enc, sum);
    return ConstantTimeEqual(enc, sig, 32);
}

} // namespace okay::crypto

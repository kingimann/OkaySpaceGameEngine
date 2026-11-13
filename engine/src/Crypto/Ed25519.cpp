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
// Verification works on public data only (signature, message, public key), so it may use the
// variable-time windowed ScalarMultPublic.
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
};

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

void Encode(std::uint8_t out[32], const Point& p) {
    using namespace fe;
    Fe zi, x, y;
    Invert(zi, p.Z);
    Mul(x, p.X, zi);
    Mul(y, p.Y, zi);
    ToBytes(out, y);
    out[31] = static_cast<std::uint8_t>(out[31] | (IsNegative(x) << 7));
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

// r = s * p by 4-bit windows. NOT constant time (branches on the scalar): verification only,
// where the scalar and the point are public.
void ScalarMultPublic(Point& r, const Point& p, const std::uint8_t s[32], const Consts& k) {
    Point tab[16];
    SetIdentity(tab[0]);
    tab[1] = p;
    for (int i = 2; i < 16; ++i) PointAdd(tab[i], tab[i - 1], p, k.d2);
    Point acc;
    SetIdentity(acc);
    for (int i = 63; i >= 0; --i) {
        for (int j = 0; j < 4; ++j) PointDouble(acc, acc);
        const int nib = (s[i >> 1] >> ((i & 1) * 4)) & 15;
        if (nib) PointAdd(acc, acc, tab[nib], k.d2);
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

// A point of order 1, 2, 4 or 8 (including the identity): 8P is the identity.
bool HasSmallOrder(const Consts& k, const std::uint8_t enc[32]) {
    Point p;
    if (!Decode(p, k.d, enc)) return false;          // not a point at all: refused elsewhere
    for (int i = 0; i < 3; ++i) PointDouble(p, p);
    return fe::IsZero(p.X) && fe::Equal(p.Y, p.Z);
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

void Ed25519Sign(std::uint8_t sig[64], const std::uint8_t* m, std::size_t n, const std::uint8_t sk[64]) {
    const Consts& k = GetConsts();
    std::uint8_t h[64], pk[32];
    Sha512Hash(h, sk, 32);
    Clamp(h);                                         // h[0..32] = secret scalar a, h[32..64] = prefix
    {
        Point A;
        BaseMult(A, h, k);
        Encode(pk, A);
        MemZero(&A, sizeof A);
    }
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
    Encode(sig, R);
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

bool Ed25519Verify(const std::uint8_t sig[64], const std::uint8_t* m, std::size_t n, const std::uint8_t pk[32]) {
    const Consts& k = GetConsts();
    if (!sc::IsCanonical(sig + 32)) return false;
    if (HasSmallOrder(k, sig)) return false;
    if (!YIsCanonical(pk) || HasSmallOrder(k, pk)) return false;
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
    Point sB, hA, sum;
    BaseMult(sB, sig + 32, k);                        // (public scalar through the constant-time path: fine)
    ScalarMultPublic(hA, A, h, k);
    PointAdd(sum, sB, hA, k.d2);
    std::uint8_t enc[32];
    Encode(enc, sum);
    return ConstantTimeEqual(enc, sig, 32);
}

} // namespace okay::crypto

// Ed25519 (RFC 8032 section 5.1), pure, detached signatures. UNREVIEWED (see Crypto.hpp).
//
// Curve arithmetic is on the twisted Edwards curve -x^2 + y^2 = 1 + d x^2 y^2 in
// extended coordinates (X:Y:Z:T), with the unified addition formula that is
// complete for this curve (so doubling is just adding a point to itself and no
// input is special). Scalar multiplication is a fixed 256-step double-and-add in
// which the scalar bit only drives a masked select, so signing (secret scalars)
// runs the same operations whatever the key. Verification works on public data.
// d, sqrt(-1) and the base point are derived from their definitions on first use.
#include <okay/Crypto/Crypto.hpp>
#include "Fe25519.hpp"

namespace okay::crypto {
namespace {

using fe::Fe;

struct Point { Fe X, Y, Z, T; };

struct Consts {
    Fe d, d2;
    Point base;
};

void SetIdentity(Point& p) { fe::Zero(p.X); fe::One(p.Y); fe::One(p.Z); fe::Zero(p.T); }

// Decode per RFC 8032 section 5.1.3. Does NOT insist on a canonical y (callers do);
// fails when there is no x, or x is 0 with the sign bit set.
bool Decode(Point& p, const Consts& k, const std::uint8_t in[32]) {
    using namespace fe;
    std::uint8_t yb[32];
    for (int i = 0; i < 32; ++i) yb[i] = in[i];
    const int sign = yb[31] >> 7;
    yb[31] &= 127;
    Fe y, one, u, v, v3, v7, x, t, chk;
    FromBytes(y, yb);
    One(one);
    Square(u, y);                    // y^2
    Mul(v, k.d, u);
    Add(v, v, one);                  // d y^2 + 1
    Sub(u, u, one);                  // y^2 - 1
    Square(v3, v); Mul(v3, v3, v);   // v^3
    Square(v7, v3); Mul(v7, v7, v);  // v^7
    Mul(t, u, v7);
    Pow22523(t, t);                  // (u v^7)^((p-5)/8)
    Mul(t, t, v3);
    Mul(x, t, u);                    // candidate x = u v^3 (u v^7)^((p-5)/8)
    Square(chk, x); Mul(chk, chk, v);   // v x^2
    Fe negU; Zero(negU); Sub(negU, negU, u);
    if (Equal(chk, u)) {
        // x is right
    } else if (Equal(chk, negU)) {
        Mul(x, x, SqrtMinusOne());
    } else {
        return false;
    }
    if (IsZero(x) && sign) return false;
    if (IsNegative(x) != sign) { Fe z; Zero(z); Sub(x, z, x); }
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
void PointAdd(Point& r, const Point& p, const Point& q, const Consts& k) {
    using namespace fe;
    Fe a, b, c, d, e, f, g, h, t1, t2;
    Sub(t1, p.Y, p.X); Sub(t2, q.Y, q.X); Mul(a, t1, t2);
    Add(t1, p.Y, p.X); Add(t2, q.Y, q.X); Mul(b, t1, t2);
    Mul(c, p.T, q.T); Mul(c, c, k.d2);
    Mul(d, p.Z, q.Z); Add(d, d, d);
    Sub(e, b, a); Sub(f, d, c); Add(g, d, c); Add(h, b, a);
    Mul(r.X, e, f); Mul(r.Y, g, h); Mul(r.T, e, h); Mul(r.Z, f, g);
}

void SelectPoint(Point& r, const Point& a, const Point& b, std::int64_t bit) {
    fe::Select(r.X, a.X, b.X, bit); fe::Select(r.Y, a.Y, b.Y, bit);
    fe::Select(r.Z, a.Z, b.Z, bit); fe::Select(r.T, a.T, b.T, bit);
}

// r = scalar * p, scalar = 32 little-endian bytes, all 256 bits processed.
void ScalarMult(Point& r, const Point& p, const std::uint8_t scalar[32], const Consts& k) {
    Point acc, dbl, sum;
    SetIdentity(acc);
    for (int bit = 255; bit >= 0; --bit) {
        PointAdd(dbl, acc, acc, k);
        PointAdd(sum, dbl, p, k);
        SelectPoint(acc, dbl, sum, (scalar[bit >> 3] >> (bit & 7)) & 1);
    }
    r = acc;
    MemZero(&acc, sizeof acc); MemZero(&dbl, sizeof dbl); MemZero(&sum, sizeof sum);
}

const Consts& GetConsts() {
    static const Consts k = [] {
        using namespace fe;
        Consts c;
        Fe n, den, inv, zero;
        Zero(zero);
        Zero(n); n.v[0] = 121665 & 0xffff; n.v[1] = 121665 >> 16;
        Zero(den); den.v[0] = 121666 & 0xffff; den.v[1] = 121666 >> 16;
        Invert(inv, den);
        Mul(c.d, n, inv);
        Sub(c.d, zero, c.d);                          // d = -121665/121666
        fe::Add(c.d2, c.d, c.d);
        // Base point: y = 4/5, x even (RFC 8032 encodes it as 0x58 then 0x66 x31).
        std::uint8_t enc[32];
        for (int i = 0; i < 32; ++i) enc[i] = 0x66;
        enc[0] = 0x58;
        Point b;
        Decode(b, c, enc);
        c.base = b;
        return c;
    }();
    return k;
}

// ---- scalars modulo the group order L = 2^252 + 27742317777372353535851937790883648493 ----
const std::uint8_t kL[32] = {
    0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10};

// r - L when r >= L, else r. Constant time (borrow chain and a mask).
void CondSubL(std::uint8_t r[32]) {
    std::uint8_t u[32];
    std::uint32_t borrow = 0;
    for (int i = 0; i < 32; ++i) {
        const std::uint32_t d = static_cast<std::uint32_t>(r[i]) - kL[i] - borrow;
        u[i] = static_cast<std::uint8_t>(d);
        borrow = (d >> 31) & 1;
    }
    const std::uint8_t keepR = static_cast<std::uint8_t>(0u - borrow);   // 0xff when r < L
    for (int i = 0; i < 32; ++i) r[i] = static_cast<std::uint8_t>((r[i] & keepR) | (u[i] & ~keepR));
}

// out = (little-endian integer of n bytes) mod L, by doubling-and-adding bit by bit.
void ReduceL(std::uint8_t out[32], const std::uint8_t* in, std::size_t n) {
    std::uint8_t r[32] = {0};
    for (std::size_t bit = n * 8; bit-- > 0;) {
        std::uint32_t carry = (in[bit >> 3] >> (bit & 7)) & 1;
        for (int i = 0; i < 32; ++i) {                // r = 2r + bit (r < L so this stays < 2L < 2^254)
            const std::uint32_t v = (static_cast<std::uint32_t>(r[i]) << 1) | carry;
            r[i] = static_cast<std::uint8_t>(v);
            carry = v >> 8;
        }
        CondSubL(r);
    }
    for (int i = 0; i < 32; ++i) out[i] = r[i];
    MemZero(r, sizeof r);
}

// out = (r + a * b) mod L, with r, a, b 32-byte little-endian integers.
void MulAddL(std::uint8_t out[32], const std::uint8_t r[32], const std::uint8_t a[32], const std::uint8_t b[32]) {
    std::uint32_t acc[64] = {0};
    for (int i = 0; i < 32; ++i)
        for (int j = 0; j < 32; ++j) acc[i + j] += static_cast<std::uint32_t>(a[i]) * b[j];
    for (int i = 0; i < 32; ++i) acc[i] += r[i];
    std::uint8_t wide[64];
    std::uint32_t carry = 0;
    for (int i = 0; i < 64; ++i) {
        const std::uint32_t v = acc[i] + carry;
        wide[i] = static_cast<std::uint8_t>(v);
        carry = v >> 8;
    }
    ReduceL(out, wide, 64);
    MemZero(acc, sizeof acc); MemZero(wide, sizeof wide);
}

bool ScalarIsCanonical(const std::uint8_t s[32]) {   // public data: s < L
    std::uint32_t borrow = 0;
    for (int i = 0; i < 32; ++i) {
        const std::uint32_t d = static_cast<std::uint32_t>(s[i]) - kL[i] - borrow;
        borrow = (d >> 31) & 1;
    }
    return borrow == 1;
}

bool YIsCanonical(const std::uint8_t enc[32]) {      // public data: y < p, ignoring the sign bit
    if ((enc[31] & 0x7f) != 0x7f) return true;
    for (int i = 30; i >= 1; --i) if (enc[i] != 0xff) return true;
    return enc[0] < 0xed;
}

// A point of order 1, 2, 4 or 8 (including the identity): 8P is the identity.
bool HasSmallOrder(const Consts& k, const std::uint8_t enc[32]) {
    Point p;
    if (!Decode(p, k, enc)) return false;            // not a point at all: refused elsewhere
    for (int i = 0; i < 3; ++i) PointAdd(p, p, p, k);
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
    ScalarMult(A, k.base, h, k);
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
        ScalarMult(A, k.base, h, k);
        Encode(pk, A);
        MemZero(&A, sizeof A);
    }
    std::uint8_t digest[64], r[32], kk[32];
    {
        Sha512 s;
        s.Update(h + 32, 32);
        if (n) s.Update(m, n);
        s.Final(digest);
        ReduceL(r, digest, 64);
    }
    Point R;
    ScalarMult(R, k.base, r, k);
    Encode(sig, R);
    {
        Sha512 s;
        s.Update(sig, 32); s.Update(pk, 32);
        if (n) s.Update(m, n);
        s.Final(digest);
        ReduceL(kk, digest, 64);
    }
    MulAddL(sig + 32, r, kk, h);                      // S = r + k a (mod L)
    MemZero(h, sizeof h); MemZero(digest, sizeof digest); MemZero(r, sizeof r); MemZero(&R, sizeof R);
}

bool Ed25519Verify(const std::uint8_t sig[64], const std::uint8_t* m, std::size_t n, const std::uint8_t pk[32]) {
    const Consts& k = GetConsts();
    if (!ScalarIsCanonical(sig + 32)) return false;
    if (HasSmallOrder(k, sig)) return false;
    if (!YIsCanonical(pk) || HasSmallOrder(k, pk)) return false;
    Point A;
    if (!Decode(A, k, pk)) return false;
    // -A
    {
        Fe z; fe::Zero(z);
        fe::Sub(A.X, z, A.X); fe::Sub(A.T, z, A.T);
    }
    std::uint8_t digest[64], h[32];
    {
        Sha512 s;
        s.Update(sig, 32); s.Update(pk, 32);
        if (n) s.Update(m, n);
        s.Final(digest);
        ReduceL(h, digest, 64);
    }
    Point sB, hA, sum;
    ScalarMult(sB, k.base, sig + 32, k);
    ScalarMult(hA, A, h, k);
    PointAdd(sum, sB, hA, k);
    std::uint8_t enc[32];
    Encode(enc, sum);
    return ConstantTimeEqual(enc, sig, 32);
}

} // namespace okay::crypto

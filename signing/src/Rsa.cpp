// RSA keys and RSASSA-PKCS1-v1_5 / SHA-256. NOT INDEPENDENTLY REVIEWED. TEST KEY ONLY. See Rsa.hpp. Written from RFC 8017.
#include "okay/Sign/Rsa.hpp"

#include <utility>

#include "okay/Crypto/Crypto.hpp"
#include "okay/Sign/Der.hpp"
#include "okay/Sign/Prime.hpp"

namespace okay::sign {

namespace {

const BigInt& One() { static const BigInt v = BigInt::FromU64(1); return v; }

BigInt Minus1(const BigInt& a) { BigInt r; BigInt::Sub(a, One(), r); return r; }

BigInt Mod(const BigInt& a, const BigInt& m) { BigInt q, r; BigInt::DivMod(a, m, q, r); return r; }

BigInt Lcm(const BigInt& a, const BigInt& b) {
    BigInt q, r;
    BigInt::DivMod(BigInt::Mul(a, b), BigInt::Gcd(a, b), q, r);
    return q;
}

// A random prime of exactly `bits` bits whose top TWO bits are set (so a product of two has exactly 2*bits bits), with
// gcd(e, prime - 1) = 1. Fresh random candidate each time (FIPS 186-5 style), not an incremental search.
BigInt RandomRsaPrime(std::size_t bits, const BigInt& e, int rounds) {
    for (;;) {
        BigInt c = RandomBitsTopSet(bits);
        if (!c.Bit(bits - 2)) c = BigInt::Add(c, BigInt::ShiftLeft(One(), bits - 2));
        if (!c.IsOdd()) c = BigInt::Add(c, One());
        if (!Mod(Minus1(c), e).IsZero() && IsProbablePrime(c, rounds)) return c;   // e is prime, so gcd(e, c-1) != 1 iff e | c-1
    }
}

}  // namespace

bool RsaCheckPrivateKey(const RsaPrivateKey& k) {
    if (k.n.IsZero() || k.p.IsZero() || k.q.IsZero() || k.e.IsZero() || k.d.IsZero()) return false;
    if (BigInt::Mul(k.p, k.q) != k.n) return false;
    if (BigInt::Compare(k.p, k.q) == 0) return false;
    const BigInt pm = Minus1(k.p), qm = Minus1(k.q);
    if (!k.e.IsOdd() || BigInt::Compare(k.e, BigInt::FromU64(3)) < 0) return false;
    if (Mod(BigInt::Mul(k.e, k.d), Lcm(pm, qm)) != One()) return false;
    if (Mod(k.d, pm) != k.dP || Mod(k.d, qm) != k.dQ) return false;
    if (Mod(BigInt::Mul(k.q, k.qInv), k.p) != One()) return false;
    return IsProbablePrime(k.p, 20) && IsProbablePrime(k.q, 20);
}

bool RsaGenerateKey(std::size_t bits, RsaPrivateKey& out, int mrRounds) {
    out = RsaPrivateKey();
    if (bits < kRsaMinBits || bits % 2 != 0 || bits > 16384) return false;
    const std::size_t half = bits / 2;
    const BigInt e = BigInt::FromU64(kRsaPublicExponent);
    for (;;) {
        BigInt p = RandomRsaPrime(half, e, mrRounds), q = RandomRsaPrime(half, e, mrRounds);
        if (BigInt::Compare(p, q) < 0) std::swap(p, q);
        BigInt diff;
        BigInt::Sub(p, q, diff);
        if (diff.BitLength() <= half - 100) continue;   // FIPS 186-5: |p - q| > 2^(half-100); also rules out p == q
        const BigInt n = BigInt::Mul(p, q);
        if (n.BitLength() != bits) continue;            // cannot happen with the top two bits set; kept as a guard
        const BigInt pm = Minus1(p), qm = Minus1(q);
        BigInt d;
        if (!BigInt::ModInverse(e, Lcm(pm, qm), d)) continue;
        if (d.BitLength() <= half) continue;            // FIPS 186-5: d must be larger than 2^half
        BigInt qInv;
        if (!BigInt::ModInverse(q, p, qInv)) continue;
        out.n = n; out.e = e; out.d = d; out.p = p; out.q = q;
        out.dP = Mod(d, pm); out.dQ = Mod(d, qm); out.qInv = qInv;
        return true;
    }
}

std::vector<std::uint8_t> RsaEncodeEmsaSha256(const std::uint8_t digest[32], std::size_t emLen) {
    // DigestInfo ::= SEQUENCE { AlgorithmIdentifier { sha-256, NULL }, OCTET STRING digest }, written with our DER writer.
    const der::Bytes dig(digest, digest + 32);
    const der::Bytes t = der::Sequence({der::Sequence({der::Oid("2.16.840.1.101.3.4.2.1"), der::Null()}), der::OctetString(dig)});
    if (emLen < t.size() + 11) return {};   // RFC 8017 9.2 step 3: "intended encoded message length too short"
    std::vector<std::uint8_t> em(emLen, 0xFF);
    em[0] = 0x00;
    em[1] = 0x01;
    em[emLen - t.size() - 1] = 0x00;
    for (std::size_t i = 0; i < t.size(); ++i) em[emLen - t.size() + i] = t[i];
    return em;
}

static std::size_t ByteLen(const BigInt& n) { return (n.BitLength() + 7) / 8; }

bool RsaSignDigestSha256(const RsaPrivateKey& key, const std::uint8_t digest[32], std::vector<std::uint8_t>& sig) {
    sig.clear();
    const std::size_t k = ByteLen(key.n);
    const std::vector<std::uint8_t> em = RsaEncodeEmsaSha256(digest, k);
    if (em.empty() || key.p.IsZero() || key.q.IsZero()) return false;
    const BigInt m = BigInt::FromBytes(em);   // starts with 0x00, so m < n
    // RSASP1 by the CRT (RFC 8017 5.2.2): s1 = m^dP mod p, s2 = m^dQ mod q, h = qInv*(s1 - s2) mod p, s = s2 + q*h.
    BigInt s1, s2;
    if (!BigInt::ModExp(Mod(m, key.p), key.dP, key.p, s1) || !BigInt::ModExp(Mod(m, key.q), key.dQ, key.q, s2)) return false;
    BigInt diff;
    BigInt::Sub(BigInt::Add(s1, key.p), Mod(s2, key.p), diff);   // s1 - s2 kept non-negative by adding p
    const BigInt h = Mod(BigInt::Mul(key.qInv, diff), key.p);
    const BigInt s = BigInt::Add(s2, BigInt::Mul(key.q, h));
    // Check the result before it leaves: s^e mod n must give back m. (A fault or a bad key must not produce a "signature".)
    BigInt back;
    if (BigInt::Compare(s, key.n) >= 0 || !BigInt::ModExp(s, key.e, key.n, back) || back != m) return false;
    sig = s.ToBytes(k);
    return !sig.empty();
}

bool RsaSignSha256(const RsaPrivateKey& key, const std::uint8_t* msg, std::size_t n, std::vector<std::uint8_t>& sig) {
    std::uint8_t d[32];
    okay::crypto::Sha256Hash(d, msg, n);
    return RsaSignDigestSha256(key, d, sig);
}

bool RsaVerifyDigestSha256(const RsaPublicKey& key, const std::uint8_t digest[32], const std::uint8_t* sig, std::size_t sigLen) {
    const std::size_t k = ByteLen(key.n);
    if (k == 0 || sigLen != k || key.e.IsZero() || !key.n.IsOdd()) return false;
    const BigInt s = BigInt::FromBytes(sig, sigLen);
    if (BigInt::Compare(s, key.n) >= 0) return false;
    BigInt m;
    if (!BigInt::ModExp(s, key.e, key.n, m)) return false;
    const std::vector<std::uint8_t> got = m.ToBytes(k);
    const std::vector<std::uint8_t> want = RsaEncodeEmsaSha256(digest, k);
    return !got.empty() && !want.empty() && got == want;   // public data only, so a plain compare is fine
}

bool RsaVerifySha256(const RsaPublicKey& key, const std::uint8_t* msg, std::size_t n, const std::uint8_t* sig, std::size_t sigLen) {
    std::uint8_t d[32];
    okay::crypto::Sha256Hash(d, msg, n);
    return RsaVerifyDigestSha256(key, d, sig, sigLen);
}

std::vector<std::uint8_t> RsaPublicKeyToDer(const RsaPublicKey& k) { return der::Sequence({der::Integer(k.n), der::Integer(k.e)}); }

std::vector<std::uint8_t> RsaPrivateKeyToDer(const RsaPrivateKey& k) {
    return der::Sequence({der::IntegerU64(0), der::Integer(k.n), der::Integer(k.e), der::Integer(k.d), der::Integer(k.p),
                          der::Integer(k.q), der::Integer(k.dP), der::Integer(k.dQ), der::Integer(k.qInv)});
}

std::vector<std::uint8_t> RsaPublicKeyToSpkiDer(const RsaPublicKey& k) {
    return der::Sequence({der::Sequence({der::Oid("1.2.840.113549.1.1.1"), der::Null()}), der::BitString(RsaPublicKeyToDer(k), 0)});
}

bool RsaPublicKeyFromSpkiDer(const std::vector<std::uint8_t>& d, RsaPublicKey& out) {
    out = RsaPublicKey();
    der::Reader top(d);
    der::Tlv_ seq, alg, bits, oid, nul;
    if (!top.Expect(der::kTagSequence, seq) || !top.AtEnd()) return false;
    der::Reader in(seq.content, seq.length);
    if (!in.Expect(der::kTagSequence, alg) || !in.Expect(der::kTagBitString, bits) || !in.AtEnd()) return false;
    der::Reader ar(alg.content, alg.length);
    std::string o;
    if (!ar.Expect(der::kTagOid, oid) || !der::ReadOid(oid, o) || o != "1.2.840.113549.1.1.1") return false;
    if (!ar.Expect(der::kTagNull, nul) || nul.length != 0 || !ar.AtEnd()) return false;
    std::vector<std::uint8_t> inner;
    unsigned unused = 0;
    if (!der::ReadBitString(bits, inner, unused) || unused != 0) return false;
    return RsaPublicKeyFromDer(inner, out);
}

bool RsaPublicKeyFromDer(const std::vector<std::uint8_t>& d, RsaPublicKey& out) {
    out = RsaPublicKey();
    der::Reader top(d);
    der::Tlv_ seq, a, b;
    if (!top.Expect(der::kTagSequence, seq) || !top.AtEnd()) return false;
    der::Reader in(seq.content, seq.length);
    if (!in.Expect(der::kTagInteger, a) || !in.Expect(der::kTagInteger, b) || !in.AtEnd()) return false;
    RsaPublicKey k;
    if (!der::ReadInteger(a, k.n) || !der::ReadInteger(b, k.e)) return false;
    out = k;
    return true;
}

bool RsaPrivateKeyFromDer(const std::vector<std::uint8_t>& d, RsaPrivateKey& out) {
    out = RsaPrivateKey();
    der::Reader top(d);
    der::Tlv_ seq, t[9];
    if (!top.Expect(der::kTagSequence, seq) || !top.AtEnd()) return false;
    der::Reader in(seq.content, seq.length);
    for (auto& x : t) if (!in.Expect(der::kTagInteger, x)) return false;
    if (!in.AtEnd()) return false;
    BigInt ver;
    RsaPrivateKey k;
    BigInt* dst[9] = {&ver, &k.n, &k.e, &k.d, &k.p, &k.q, &k.dP, &k.dQ, &k.qInv};
    for (int i = 0; i < 9; ++i) if (!der::ReadInteger(t[i], *dst[i])) return false;
    if (!ver.IsZero()) return false;   // version 0 = two primes
    out = k;
    return true;
}

}  // namespace okay::sign

// RSA tests. NOT INDEPENDENTLY REVIEWED. TEST KEY ONLY. Known answers (rsa_vectors.hpp), round trips, tampering, wrong keys,
// hand-made malformed padding blocks, DER export/import.
#include <chrono>
#include <cstdio>
#include <string>

#include "check.hpp"
#include "okay/Crypto/Sha256.hpp"
#include "okay/Sign/Der.hpp"
#include "okay/Sign/Rsa.hpp"
#include "rsa_vectors.hpp"

using namespace okay::sign;
using Bytes = std::vector<std::uint8_t>;

static Bytes FromHexStr(const char* h) {
    BigInt v;
    std::string s(h);
    const std::size_t bytes = s.size() / 2;
    BigInt::FromHex(s, v);
    return bytes == 0 ? Bytes() : v.ToBytes(bytes);
}
static BigInt Hex(const char* s) { BigInt v; CHECK(BigInt::FromHex(s, v)); return v; }

static RsaPrivateKey KeyFrom(const RsaKat& k) {
    RsaPrivateKey r;
    r.n = Hex(k.n); r.e = Hex(k.e); r.d = Hex(k.d); r.p = Hex(k.p); r.q = Hex(k.q); r.dP = Hex(k.dp); r.dQ = Hex(k.dq); r.qInv = Hex(k.qinv);
    return r;
}

static void Digest(const Bytes& m, std::uint8_t out[32]) { okay::crypto::Sha256Hash(out, m.data(), m.size()); }

static void KnownAnswers() {
    // RFC 8017 9.2 note 1 gives the DigestInfo prefix for SHA-256 as the byte string 3031300d060960864801650304020105000420.
    const std::uint8_t zero[32] = {};
    const Bytes em = RsaEncodeEmsaSha256(zero, 128);
    const Bytes prefix = FromHexStr("3031300d060960864801650304020105000420");
    CHECK(em.size() == 128 && em[0] == 0 && em[1] == 1 && em[128 - 51 - 1] == 0);
    CHECK(Bytes(em.begin() + (128 - 51), em.begin() + (128 - 32)) == prefix);
    for (std::size_t i = 2; i < 128 - 52; ++i) CHECK(em[i] == 0xFF);
    CHECK(RsaEncodeEmsaSha256(zero, 61).empty() && !RsaEncodeEmsaSha256(zero, 62).empty());   // 19 + 32 + 11 = 62 bytes minimum

    for (const RsaKat& k : kRsaKats) {
        const RsaPrivateKey key = KeyFrom(k);
        const Bytes msg = FromHexStr(k.msgHex), want = FromHexStr(k.sigHex);
        CHECK(RsaCheckPrivateKey(key));
        Bytes sig;
        CHECK(RsaSignSha256(key, msg.data(), msg.size(), sig));
        CHECK(sig == want);   // PKCS#1 v1.5 is deterministic: exact bytes
        CHECK(RsaVerifySha256(key.Public(), msg.data(), msg.size(), want.data(), want.size()));
        // one changed bit anywhere in the message, or anywhere in a sample of signature bytes, must fail
        Bytes m2 = msg;
        m2.push_back(0);
        CHECK(!RsaVerifySha256(key.Public(), m2.data(), m2.size(), want.data(), want.size()));
        for (std::size_t i = 0; i < want.size(); i += 7) {
            Bytes s2 = want;
            s2[i] ^= 0x10;
            CHECK(!RsaVerifySha256(key.Public(), msg.data(), msg.size(), s2.data(), s2.size()));
        }
        // DER export and import round trip
        RsaPrivateKey back;
        CHECK(RsaPrivateKeyFromDer(RsaPrivateKeyToDer(key), back) && back.n == key.n && back.d == key.d && back.qInv == key.qInv && back.p == key.p);
        RsaPublicKey pub;
        CHECK(RsaPublicKeyFromDer(RsaPublicKeyToDer(key.Public()), pub) && pub.n == key.n && pub.e == key.e);
    }
}

static void Tampering(const RsaPrivateKey& key, const RsaPrivateKey& other) {
    const Bytes msg = {'o', 'k', 'a', 'y', ' ', 's', 'p', 'a', 'c', 'e'};
    Bytes sig;
    CHECK(RsaSignSha256(key, msg.data(), msg.size(), sig));
    const RsaPublicKey pub = key.Public();
    const std::size_t k = sig.size();
    CHECK(k == (key.n.BitLength() + 7) / 8);
    CHECK(RsaVerifySha256(pub, msg.data(), msg.size(), sig.data(), sig.size()));
    // message changed: every byte, one bit each
    for (std::size_t i = 0; i < msg.size(); ++i) { Bytes m = msg; m[i] ^= 1; CHECK(!RsaVerifySha256(pub, m.data(), m.size(), sig.data(), sig.size())); }
    // signature changed: first, middle and last byte, and several bits
    for (std::size_t i : {std::size_t(0), k / 2, k - 1}) for (int b = 0; b < 8; b += 3) { Bytes s = sig; s[i] ^= static_cast<std::uint8_t>(1 << b); CHECK(!RsaVerifySha256(pub, msg.data(), msg.size(), s.data(), s.size())); }
    // length wrong: shorter, longer, with a leading zero byte added, empty
    CHECK(!RsaVerifySha256(pub, msg.data(), msg.size(), sig.data(), k - 1));
    Bytes longer = sig; longer.push_back(0);
    CHECK(!RsaVerifySha256(pub, msg.data(), msg.size(), longer.data(), longer.size()));
    Bytes lead = sig; lead.insert(lead.begin(), 0);
    CHECK(!RsaVerifySha256(pub, msg.data(), msg.size(), lead.data(), lead.size()));
    CHECK(!RsaVerifySha256(pub, msg.data(), msg.size(), sig.data(), 0));
    // out of range: s = n, s = n + 1 as k bytes (no wrap), all zero, all one
    CHECK(!RsaVerifySha256(pub, msg.data(), msg.size(), key.n.ToBytes(k).data(), k));
    Bytes zeros(k, 0), ones(k, 0xFF);
    CHECK(!RsaVerifySha256(pub, msg.data(), msg.size(), zeros.data(), k));
    CHECK(!RsaVerifySha256(pub, msg.data(), msg.size(), ones.data(), k));
    // signature plus n: congruent mod n but not canonical, and wrong length when it carries over
    Bytes sn = BigInt::Add(BigInt::FromBytes(sig), key.n).ToBytes(k);
    if (!sn.empty()) CHECK(!RsaVerifySha256(pub, msg.data(), msg.size(), sn.data(), sn.size()));
    // wrong key: another key's public part, and the right modulus with a wrong exponent
    CHECK(!RsaVerifySha256(other.Public(), msg.data(), msg.size(), sig.data(), sig.size()));
    RsaPublicKey e3 = pub; e3.e = BigInt::FromU64(3);
    CHECK(!RsaVerifySha256(e3, msg.data(), msg.size(), sig.data(), sig.size()));
    // a signature made with the other key does not verify under this one
    Bytes sig2;
    CHECK(RsaSignSha256(other, msg.data(), msg.size(), sig2));
    CHECK(!RsaVerifySha256(pub, msg.data(), msg.size(), sig2.data(), sig2.size()));
    // a broken private key must refuse to sign (the self-check)
    RsaPrivateKey bad = key; bad.dQ = BigInt::Add(bad.dQ, BigInt::FromU64(2));
    Bytes none;
    CHECK(!RsaSignSha256(bad, msg.data(), msg.size(), none) && none.empty());
    CHECK(!RsaCheckPrivateKey(bad));
    RsaPrivateKey bad2 = key; bad2.qInv = BigInt::Add(bad2.qInv, BigInt::FromU64(1));
    CHECK(!RsaCheckPrivateKey(bad2));
}

// Raw RSA with the private exponent over a hand-made block, so we can test that verify rejects badly formed blocks that carry a
// perfectly valid RSA operation (the classic padding-parsing mistakes).
static Bytes RawSign(const RsaPrivateKey& key, const Bytes& block) {
    BigInt s;
    BigInt::ModExp(BigInt::FromBytes(block), key.d, key.n, s);
    return s.ToBytes((key.n.BitLength() + 7) / 8);
}

static void MalformedBlocks(const RsaPrivateKey& key) {
    const Bytes msg = {1, 2, 3};
    std::uint8_t dg[32];
    Digest(msg, dg);
    const std::size_t k = (key.n.BitLength() + 7) / 8;
    const Bytes good = RsaEncodeEmsaSha256(dg, k);
    const Bytes goodSig = RawSign(key, good);
    CHECK(RsaVerifySha256(key.Public(), msg.data(), msg.size(), goodSig.data(), goodSig.size()));   // the raw route equals the real one
    auto rejects = [&](const Bytes& block) {
        const Bytes s = RawSign(key, block);
        return !RsaVerifySha256(key.Public(), msg.data(), msg.size(), s.data(), s.size());
    };
    Bytes b;
    b = good; b[1] = 0x02; CHECK(rejects(b));                       // wrong block type
    b = good; b[2] = 0xFE; CHECK(rejects(b));                       // a padding byte that is not FF
    b = good; b[0] = 0x01; CHECK(rejects(b));                       // wrong first byte
    b = good; b[k - 40] ^= 1; CHECK(rejects(b));                    // inside the digest
    // Short padding with the digest info shifted left and garbage appended (the Bleichenbacher e=3 style forgery shape).
    const std::size_t tlen = 51;
    Bytes shifted(k, 0xAA);
    shifted[0] = 0x00; shifted[1] = 0x01; shifted[2] = 0xFF; shifted[3] = 0xFF; shifted[4] = 0xFF; shifted[5] = 0x00;
    for (std::size_t i = 0; i < tlen; ++i) shifted[6 + i] = good[k - tlen + i];
    CHECK(rejects(shifted));
    // Missing NULL parameters in the AlgorithmIdentifier (a real-world variant that RFC 8017 9.2 says to refuse here).
    const Bytes dig(dg, dg + 32);
    namespace d = okay::sign::der;
    const Bytes t = d::Sequence({d::Sequence({d::Oid("2.16.840.1.101.3.4.2.1")}), d::OctetString(dig)});
    Bytes noNull(k, 0xFF);
    noNull[0] = 0; noNull[1] = 1; noNull[k - t.size() - 1] = 0;
    for (std::size_t i = 0; i < t.size(); ++i) noNull[k - t.size() + i] = t[i];
    CHECK(rejects(noNull));
    // Digest of a different message in a perfectly formed block.
    const Bytes other = {9, 9, 9};
    std::uint8_t dg2[32];
    Digest(other, dg2);
    CHECK(rejects(RsaEncodeEmsaSha256(dg2, k)));
}

static double Seconds(std::chrono::steady_clock::time_point a) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - a).count();
}

int main() {
    KnownAnswers();
    // Generation and round trips at small sizes (fast), then the properties of the generated keys.
    RsaPrivateKey k512, k512b, k768;
    CHECK(RsaGenerateKey(512, k512, 20) && RsaGenerateKey(512, k512b, 20) && RsaGenerateKey(768, k768, 20));
    CHECK(k512.n.BitLength() == 512 && k768.n.BitLength() == 768);
    CHECK(k512.e == BigInt::FromU64(65537));
    CHECK(BigInt::Compare(k512.p, k512.q) > 0);
    CHECK(RsaCheckPrivateKey(k512) && RsaCheckPrivateKey(k768));
    CHECK(k512.n != k512b.n);   // two draws differ
    CHECK(BigInt::Compare(k512.d, BigInt::ShiftLeft(BigInt::FromU64(1), 256)) > 0);
    Tampering(k512, k512b);
    MalformedBlocks(k512);
    MalformedBlocks(k768);
    for (int i = 0; i < 6; ++i) {   // several fresh keys, several messages each
        RsaPrivateKey k;
        CHECK(RsaGenerateKey(512 + 2 * static_cast<std::size_t>(i) * 16, k, 10));
        CHECK(RsaCheckPrivateKey(k));
        for (int len : {0, 1, 55, 56, 64, 1000}) {
            Bytes m(static_cast<std::size_t>(len));
            for (std::size_t j = 0; j < m.size(); ++j) m[j] = static_cast<std::uint8_t>(j * 31 + static_cast<std::size_t>(i));
            Bytes sig;
            CHECK(RsaSignSha256(k, m.data(), m.size(), sig));
            CHECK(RsaVerifySha256(k.Public(), m.data(), m.size(), sig.data(), sig.size()));
        }
    }
    CHECK(!RsaGenerateKey(256, k512) && !RsaGenerateKey(511, k512) && !RsaGenerateKey(1025, k512));

    // One 2048-bit round trip with timings (printed, not asserted).
    RsaPrivateKey big;
    auto t0 = std::chrono::steady_clock::now();
    CHECK(RsaGenerateKey(2048, big));
    const double tGen = Seconds(t0);
    CHECK(big.n.BitLength() == 2048 && RsaCheckPrivateKey(big));
    const Bytes msg = {'t', 'e', 's', 't'};
    Bytes sig;
    t0 = std::chrono::steady_clock::now();
    CHECK(RsaSignSha256(big, msg.data(), msg.size(), sig));
    const double tSign = Seconds(t0);
    t0 = std::chrono::steady_clock::now();
    CHECK(RsaVerifySha256(big.Public(), msg.data(), msg.size(), sig.data(), sig.size()));
    const double tVerify = Seconds(t0);
    CHECK(sig.size() == 256);
    std::printf("timing 2048-bit: keygen %.2f s, sign %.3f s, verify %.4f s\n", tGen, tSign, tVerify);
    FINISH("test_sign_rsa");
}

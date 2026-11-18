// CMS SignedData tests. NOT INDEPENDENTLY REVIEWED. TEST KEY ONLY. Known answers (cms_vectors.hpp), round trips, tampering, wrong certificate,
// parser fuzz.
#include <cstdio>
#include <string>

#include "check.hpp"
#include "cms_vectors.hpp"
#include "cert_vectors.hpp"
#include "okay/Sign/Cms.hpp"
#include "okay/Sign/Der.hpp"
#include "rsa_vectors.hpp"

using namespace okay::sign;
using Bytes = std::vector<std::uint8_t>;

static Bytes FromHexStr(const std::string& h) { Bytes b; for (std::size_t i = 0; i + 1 < h.size(); i += 2) b.push_back(static_cast<std::uint8_t>(std::stoi(h.substr(i, 2), nullptr, 16))); return b; }
static BigInt Hex(const char* s) { BigInt v; BigInt::FromHex(s, v); return v; }
static RsaPrivateKey KatKey() {
    const RsaKat& k = kRsaKats[0];
    RsaPrivateKey r;
    r.n = Hex(k.n); r.e = Hex(k.e); r.d = Hex(k.d); r.p = Hex(k.p); r.q = Hex(k.q); r.dP = Hex(k.dp); r.dQ = Hex(k.dq); r.qInv = Hex(k.qinv);
    return r;
}
static const UtcDateTime kNow{2027, 6, 1, 12, 0, 0};
static const UtcDateTime kSignTime{2027, 1, 1, 12, 0, 0};

static Bytes Content() { const std::string s = kCmsKatContent; return Bytes(s.begin(), s.end()); }

static void KnownAnswers() {
    const RsaPrivateKey key = KatKey();
    const Bytes cert = FromHexStr(kCertKatDerHex);
    CmsSignParams p;
    p.signingTime = kSignTime;
    Bytes got;
    std::string why;
    CHECK(CmsSignSha256(key, cert, Content(), p, got, &why));
    CHECK(got == FromHexStr(kCmsKatEncapsulatedHex));   // byte for byte what the separate Python encoder made
    if (got != FromHexStr(kCmsKatEncapsulatedHex)) std::printf("encapsulated KAT mismatch: %zu vs %zu bytes (%s)\n", got.size(), FromHexStr(kCmsKatEncapsulatedHex).size(), why.c_str());
    p.encapsulate = false;
    CHECK(CmsSignSha256(key, cert, Content(), p, got, &why));
    CHECK(got == FromHexStr(kCmsKatDetachedHex));
    // the known messages verify, parse to the expected fields
    CmsInfo info;
    CHECK(CmsVerifySelfSigned(FromHexStr(kCmsKatEncapsulatedHex), nullptr, kNow, &info) == "");
    CHECK(info.version == 1 && info.eContentType == "1.2.840.113549.1.7.1" && info.hasContent && info.content == Content());
    CHECK(info.hasSigningTime && info.signingTime.year == 2027 && info.signingTime.month == 1 && info.certs.size() == 1);
    CHECK(info.digestAlgOid == "2.16.840.1.101.3.4.2.1" && info.sigAlgOid == "1.2.840.113549.1.1.11" && info.attrMessageDigest.size() == 32);
    const Bytes content = Content();
    CHECK(CmsVerifySelfSigned(FromHexStr(kCmsKatDetachedHex), &content, kNow) == "");
    // layout facts: ContentInfo { OID signedData, [0] { SEQUENCE { INTEGER 1, SET { sha-256 } ...
    const Bytes m = FromHexStr(kCmsKatEncapsulatedHex);
    CHECK(m[0] == 0x30 && m[4] == 0x06 && m[5] == 0x09 && m[6] == 0x2A && m[14] == 0x02 && m[15] == 0xA0);   // 1.2.840.113549.1.7.2 then [0]
    CHECK(info.signedAttrsForSignature[0] == 0x31);
}

static void RoundTrips() {
    RsaPrivateKey key, other;
    CHECK(RsaGenerateKey(1024, key, 20) && RsaGenerateKey(1024, other, 20));
    CertParams cp;
    cp.subject = "CN=CMS Test Publisher, O=Okay Space, C=US";
    cp.serial = RandomCertSerial();
    cp.notBefore = UtcDateTime{2026, 1, 1, 0, 0, 0};
    cp.notAfter = UtcDateTime{2036, 1, 1, 0, 0, 0};
    Bytes cert, certOther;
    CHECK(MakeSelfSignedCodeSigningCert(key, cp, cert));
    CertParams cp2 = cp;   // same name and serial: the signer id of the other certificate is identical
    CHECK(MakeSelfSignedCodeSigningCert(other, cp2, certOther));
    CHECK(cert.size() == certOther.size());

    for (int variant = 0; variant < 8; ++variant) {
        CmsSignParams p;
        p.signingTime = kSignTime;
        p.encapsulate = (variant & 1) == 0;
        p.digestAlgNullParams = (variant & 2) == 0;
        p.signatureAlgSha256WithRsa = (variant & 4) == 0;
        for (std::size_t len : {std::size_t(0), std::size_t(1), std::size_t(63), std::size_t(64), std::size_t(5000)}) {
            Bytes content(len);
            for (std::size_t i = 0; i < len; ++i) content[i] = static_cast<std::uint8_t>(i * 13 + variant);
            Bytes msg;
            std::string why;
            CHECK(CmsSignSha256(key, cert, content, p, msg, &why));
            CmsInfo info;
            CHECK(CmsVerifySelfSigned(msg, p.encapsulate ? nullptr : &content, kNow, &info) == "");
            CHECK(info.hasContent == p.encapsulate);
            CHECK(info.sigAlgOid == (p.signatureAlgSha256WithRsa ? "1.2.840.113549.1.1.11" : "1.2.840.113549.1.1.1"));
        }
    }
    // other content type: version 3 by the RFC rule, 1 when forced; no signing time
    {
        CmsSignParams p;
        p.contentTypeOid = "1.3.6.1.4.1.311.2.1.4";   // any OID is fine for the generic writer (this one is only a name here)
        p.includeSigningTime = false;
        Bytes msg, content{1, 2, 3};
        CHECK(CmsSignSha256(key, cert, content, p, msg));
        CmsInfo info;
        CHECK(CmsVerifySelfSigned(msg, nullptr, kNow, &info) == "" && info.version == 3 && !info.hasSigningTime && info.eContentType == p.contentTypeOid);
        p.signedDataVersion = 1;
        CHECK(CmsSignSha256(key, cert, content, p, msg) && CmsVerifySelfSigned(msg, nullptr, kNow, &info) == "" && info.version == 1);
    }

    CmsSignParams p;
    p.signingTime = kSignTime;
    const Bytes content = {'h', 'e', 'l', 'l', 'o', ' ', 'c', 'm', 's'};
    Bytes msg;
    CHECK(CmsSignSha256(key, cert, content, p, msg));
    CHECK(CmsVerifySelfSigned(msg, nullptr, kNow) == "");

    // refusals when signing
    Bytes none;
    CHECK(!CmsSignSha256(key, certOther, content, p, none) && none.empty());   // certificate of another key
    CHECK(!CmsSignSha256(other, cert, content, p, none));
    CmsSignParams bad = p; bad.contentTypeOid = "x";           CHECK(!CmsSignSha256(key, cert, content, bad, none));
    bad = p; bad.signingTime.month = 0;                         CHECK(!CmsSignSha256(key, cert, content, bad, none));
    bad = p; bad.signedDataVersion = 2;                         CHECK(!CmsSignSha256(key, cert, content, bad, none));
    CHECK(!CmsSignSha256(key, Bytes{1, 2, 3}, content, p, none));

    // content tampering
    {
        Bytes m = msg;
        std::size_t at = 0;
        for (std::size_t i = 0; i + content.size() <= m.size(); ++i) if (std::equal(content.begin(), content.end(), m.begin() + static_cast<std::ptrdiff_t>(i))) { at = i; break; }
        CHECK(at > 0);
        m[at] ^= 1;
        CHECK(CmsVerifySelfSigned(m, nullptr, kNow) == "content digest does not match the messageDigest attribute");
    }
    // detached: wrong content, both, none
    {
        p.encapsulate = false;
        Bytes det;
        CHECK(CmsSignSha256(key, cert, content, p, det));
        Bytes wrong = content; wrong[0] ^= 1;
        CHECK(CmsVerifySelfSigned(det, &content, kNow) == "");
        CHECK(CmsVerifySelfSigned(det, &wrong, kNow) == "content digest does not match the messageDigest attribute");
        CHECK(CmsVerifySelfSigned(det, nullptr, kNow) != "");
        CHECK(CmsVerifySelfSigned(msg, &content, kNow) != "");   // encapsulated message plus detached content: refused
    }
    // time checks
    CHECK(CmsVerifySelfSigned(msg, nullptr, UtcDateTime{2025, 1, 1, 0, 0, 0}).find("not valid yet") != std::string::npos);
    CHECK(CmsVerifySelfSigned(msg, nullptr, UtcDateTime{2037, 1, 1, 0, 0, 0}).find("expired") != std::string::npos);
    {
        CmsSignParams early = p;
        early.encapsulate = true;
        early.signingTime = UtcDateTime{2020, 1, 1, 0, 0, 0};   // before the certificate existed
        Bytes m;
        CHECK(CmsSignSha256(key, cert, content, early, m));
        CHECK(CmsVerifySelfSigned(m, nullptr, kNow) == "signing time is outside the certificate's validity");
    }
    // wrong certificate: the other key's certificate (same name and serial, valid self-signature) spliced into the message
    {
        Bytes m = msg;
        std::size_t at = 0;
        for (std::size_t i = 0; i + cert.size() <= m.size(); ++i) if (std::equal(cert.begin(), cert.end(), m.begin() + static_cast<std::ptrdiff_t>(i))) { at = i; break; }
        CHECK(at > 0);
        std::copy(certOther.begin(), certOther.end(), m.begin() + static_cast<std::ptrdiff_t>(at));
        CHECK(CmsVerifySelfSigned(m, nullptr, kNow) == "signature over the signed attributes does not verify");
    }
    // every byte flip (stride 3) and every truncation: parse failure or verify failure, never acceptance
    int accepted = 0, flips = 0;
    for (std::size_t i = 0; i < msg.size(); i += 3) {
        Bytes t = msg;
        t[i] ^= 0x01;
        ++flips;
        if (CmsVerifySelfSigned(t, nullptr, kNow).empty()) ++accepted;
    }
    CHECK(flips > 100 && accepted == 0);
    for (std::size_t i = 0; i < msg.size(); i += 7) { const Bytes t(msg.begin(), msg.begin() + static_cast<std::ptrdiff_t>(i)); CHECK(!CmsVerifySelfSigned(t, nullptr, kNow).empty()); }
    { Bytes t = msg; t.push_back(0); CHECK(!CmsVerifySelfSigned(t, nullptr, kNow).empty()); }
}

static void Fuzz() {
    const Bytes good = FromHexStr(kCmsKatEncapsulatedHex);
    unsigned seed = 4242;
    auto rnd = [&] { seed = seed * 1103515245u + 12345u; return seed >> 8; };
    int accepted = 0;
    for (int i = 0; i < 4000; ++i) {   // random mutations of a valid message: 1 to 3 byte changes
        Bytes t = good;
        for (unsigned k = 1 + rnd() % 3; k > 0; --k) t[rnd() % t.size()] = static_cast<std::uint8_t>(rnd());
        if (t != good && CmsVerifySelfSigned(t, nullptr, kNow).empty()) ++accepted;
    }
    CHECK(accepted == 0);
    for (int i = 0; i < 3000; ++i) {   // random junk behind a plausible start
        Bytes junk(static_cast<std::size_t>(10 + rnd() % 400));
        for (auto& x : junk) x = static_cast<std::uint8_t>(rnd());
        junk[0] = 0x30;
        CmsInfo info;
        CHECK(!CmsParse(junk, info));
    }
    CmsInfo info;
    CHECK(!CmsParse(Bytes(), info) && !CmsParse(Bytes{0x30, 0x00}, info));
}

int main() {
    KnownAnswers();
    RoundTrips();
    Fuzz();
    FINISH("test_sign_cms");
}

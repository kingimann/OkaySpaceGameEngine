// Certificate, DN, PEM and key-file tests. NOT INDEPENDENTLY REVIEWED. TEST KEY ONLY.
#include <cstdio>
#include <string>

#include "cert_vectors.hpp"
#include "check.hpp"
#include "okay/Sign/Cert.hpp"
#include "okay/Sign/Der.hpp"
#include "okay/Sign/Pem.hpp"
#include "rsa_vectors.hpp"

#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#endif

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

static CertParams KatParams() {
    CertParams p;
    p.subject = kCertKatPublisher;
    p.serial = FromHexStr(kCertKatSerialHex);
    p.notBefore = UtcDateTime{2026, 11, 18, 0, 0, 0};
    p.notAfter = UtcDateTime{2036, 11, 18, 0, 0, 0};
    return p;
}

static void Base64AndPem() {
    // RFC 4648 section 10 test vectors.
    const char* plain[] = {"", "f", "fo", "foo", "foob", "fooba", "foobar"};
    const char* enc[] = {"", "Zg==", "Zm8=", "Zm9v", "Zm9vYg==", "Zm9vYmE=", "Zm9vYmFy"};
    for (int i = 0; i < 7; ++i) {
        const Bytes b(plain[i], plain[i] + std::string(plain[i]).size());
        CHECK(Base64Encode(b) == enc[i]);
        Bytes back;
        CHECK(Base64Decode(enc[i], back) && back == b);
    }
    Bytes out;
    CHECK(!Base64Decode("Zm9", out));          // bad length
    CHECK(!Base64Decode("Zm9v!", out));        // bad character
    CHECK(!Base64Decode("Zg==Zg==", out));     // data after padding
    CHECK(!Base64Decode("Z===", out));         // too much padding
    CHECK(!Base64Decode("=Zm9", out));
    CHECK(Base64Decode("Zm9v\nYmFy\r\n", out) && out.size() == 6);   // whitespace skipped
    Bytes all(300);
    for (std::size_t i = 0; i < all.size(); ++i) all[i] = static_cast<std::uint8_t>(i * 7);
    const std::string pem = PemEncode("TEST DATA", all);
    CHECK(pem.rfind("-----BEGIN TEST DATA-----\n", 0) == 0 && pem.find("-----END TEST DATA-----\n") != std::string::npos);
    std::size_t lineLen = 0, longest = 0;
    for (char c : pem) { if (c == '\n') { if (lineLen > longest) longest = lineLen; lineLen = 0; } else ++lineLen; }
    CHECK(longest == 64 || longest == 25);   // base64 lines are 64 wide; the header line is 25
    Bytes back;
    CHECK(PemDecode("junk before\n" + pem + "junk after", "TEST DATA", back) && back == all);
    CHECK(!PemDecode(pem, "OTHER", back));
}

static void Names() {
    std::vector<DnAttribute> a;
    std::string why;
    CHECK(ParseDistinguishedName("CN=Okay Test Publisher, O=Okay Space, C=US", a, &why) && a.size() == 3);
    CHECK(a[0].oid == "2.5.4.3" && a[0].value == "Okay Test Publisher" && a[1].oid == "2.5.4.10" && a[2].oid == "2.5.4.6" && a[2].value == "US");
    CHECK(ParseDistinguishedName("cn=Lower,o=NoSpaces", a) && a.size() == 2 && a[1].value == "NoSpaces");
    CHECK(ParseDistinguishedName("CN=\"Acme, Inc.\", C=US", a) && a[0].value == "Acme, Inc.");
    CHECK(ParseDistinguishedName("CN=\"say \"\"hi\"\"\"", a) && a[0].value == "say \"hi\"");
    CHECK(ParseDistinguishedName("CN=Acme\\, Inc., C=US", a) && a[0].value == "Acme, Inc.");
    CHECK(ParseDistinguishedName("CN=a\\2Cb", a) && a[0].value == "a,b");
    CHECK(ParseDistinguishedName("CN=trailing   ", a) && a[0].value == "trailing");
    CHECK(ParseDistinguishedName("CN=keep\\ ", a) && a[0].value == "keep ");
    CHECK(ParseDistinguishedName("2.5.4.3=dotted", a) && a[0].oid == "2.5.4.3");
    CHECK(ParseDistinguishedName("CN=x, E=a@b.c, DC=example", a) && a.size() == 3);
    for (const char* bad : {"", "CN", "CN=", "XX=foo", "CN=a+O=b", "CN=\"open", "CN=\"a\"b", "CN=a\\", "CN=a\\q", "C=U$", "CN=a, , O=b", "CN=a,"})
        CHECK(!ParseDistinguishedName(bad, a));
    // Round trip through the formatter.
    for (const char* s : {"CN=Okay Test Publisher, O=Okay Space, C=US", "CN=\"Acme, Inc.\", C=US", "CN=\"say \"\"hi\"\"\""}) {
        std::vector<DnAttribute> x, y;
        CHECK(ParseDistinguishedName(s, x) && ParseDistinguishedName(FormatDistinguishedName(x), y) && x == y);
    }
}

static void KnownAnswerCertificate() {
    const Bytes want = FromHexStr(kCertKatDerHex);
    Bytes got;
    std::string why;
    CHECK(MakeSelfSignedCodeSigningCert(KatKey(), KatParams(), got, &why));
    CHECK(got == want);   // byte for byte what the separate Python encoder produced
    if (got != want) std::printf("kat mismatch: ours %zu bytes, expected %zu (%s)\n", got.size(), want.size(), why.c_str());
    CertInfo c;
    CHECK(ParseCertificate(want, c, &why));
    CHECK(c.serial == FromHexStr(kCertKatSerialHex));
    CHECK(c.subject.size() == 3 && c.subject == c.issuer);
    CHECK(c.notBefore.year == 2026 && c.notBefore.month == 11 && c.notAfter.year == 2036);
    CHECK(c.publicKey.n == KatKey().n && c.publicKey.e == BigInt::FromU64(65537));
    CHECK(c.hasBasicConstraints && c.basicConstraintsCritical && !c.isCa);
    CHECK(c.hasKeyUsage && c.keyUsageCritical && c.keyUsageBits == 1);
    CHECK(c.hasEku && c.ekus.size() == 1 && c.ekus[0] == "1.3.6.1.5.5.7.3.3");
    CHECK(c.hasSki && c.ski.size() == 20);
    CHECK(CheckSelfSignedCodeSigningCert(c, kCertKatPublisher, kNow) == "");
    // A few fixed layout facts of the TBS: it starts with [0] EXPLICIT version 2, then the serial.
    CHECK(c.tbsDer[0] == 0x30 && c.tbsDer.size() > 10);
    // c.tbsDer is "30 82 xx xx" then A0 03 02 01 02 02 10 4a 1b...
    CHECK(c.tbsDer[4] == 0xA0 && c.tbsDer[5] == 0x03 && c.tbsDer[6] == 0x02 && c.tbsDer[7] == 0x01 && c.tbsDer[8] == 0x02);
    CHECK(c.tbsDer[9] == 0x02 && c.tbsDer[10] == 0x10 && c.tbsDer[11] == 0x4a);
    // SPKI round trip
    RsaPublicKey pub;
    CHECK(RsaPublicKeyFromSpkiDer(RsaPublicKeyToSpkiDer(KatKey().Public()), pub) && pub.n == KatKey().n);
    const Bytes spki = RsaPublicKeyToSpkiDer(KatKey().Public());
    CHECK(spki[0] == 0x30 && spki[1] == 0x81 && spki[3] == 0x30 && spki[4] == 0x0D);   // outer SEQUENCE (long form, 1 length byte), then the 13-byte AlgorithmIdentifier
}

static void GeneratedRoundTrip() {
    RsaPrivateKey key;
    CHECK(RsaGenerateKey(1024, key, 20));
    CertParams p;
    p.subject = "CN=Round Trip Publisher, O=\"Okay, Space\", C=US";
    p.serial = RandomCertSerial();
    CHECK(p.serial.size() == 16 && p.serial[0] != 0 && p.serial[0] < 0x80);
    p.notBefore = UtcDateTime{2026, 1, 1, 0, 0, 0};
    p.notAfter = UtcDateTime{2051, 1, 1, 0, 0, 0};   // after 2049: GeneralizedTime
    Bytes der;
    std::string why;
    CHECK(MakeSelfSignedCodeSigningCert(key, p, der, &why));
    CertInfo c;
    CHECK(ParseCertificate(der, c, &why));
    CHECK(c.notAfter.year == 2051 && c.serial == p.serial);
    CHECK(CheckSelfSignedCodeSigningCert(c, p.subject, kNow) == "");
    // validity edges
    CHECK(CheckSelfSignedCodeSigningCert(c, p.subject, UtcDateTime{2025, 12, 31, 23, 59, 59}) == "certificate is not valid yet");
    CHECK(CheckSelfSignedCodeSigningCert(c, p.subject, UtcDateTime{2051, 1, 1, 0, 0, 1}) == "certificate has expired");
    CHECK(CheckSelfSignedCodeSigningCert(c, p.subject, p.notBefore) == "" && CheckSelfSignedCodeSigningCert(c, p.subject, p.notAfter) == "");
    // wrong publisher string, or different order, is refused; the reversed flag fixes a reversed certificate
    CHECK(CheckSelfSignedCodeSigningCert(c, "CN=Someone Else, O=\"Okay, Space\", C=US", kNow) == "subject does not equal the expected Publisher");
    CHECK(CheckSelfSignedCodeSigningCert(c, "C=US, O=\"Okay, Space\", CN=Round Trip Publisher", kNow) != "");
    CHECK(CheckSelfSignedCodeSigningCert(c, "CN=Round Trip Publisher, O=\"Okay, Space\"", kNow) != "");
    p.reverseNameOrder = true;
    Bytes rev;
    CHECK(MakeSelfSignedCodeSigningCert(key, p, rev));
    CertInfo cr;
    CHECK(ParseCertificate(rev, cr) && cr.subject.front().value == "US");
    CHECK(CheckSelfSignedCodeSigningCert(cr, p.subject, kNow, true) == "");
    CHECK(CheckSelfSignedCodeSigningCert(cr, p.subject, kNow, false) != "");
    // a different key signs a different-subject... self-signature by the WRONG key must fail
    RsaPrivateKey other;
    CHECK(RsaGenerateKey(1024, other, 20));
    CertInfo forged = c;
    forged.publicKey = other.Public();
    CHECK(CheckSelfSignedCodeSigningCert(forged, p.subject, kNow) != "");
    // issuer different from subject is refused
    CertInfo diff = c;
    diff.issuerDer[diff.issuerDer.size() - 1] ^= 1;
    CHECK(CheckSelfSignedCodeSigningCert(diff, p.subject, kNow) == "issuer and subject differ (not self-signed)");
    // extension field edits caught by the checker
    CertInfo e1 = c; e1.isCa = true;                   CHECK(CheckSelfSignedCodeSigningCert(e1, p.subject, kNow) != "");
    CertInfo e2 = c; e2.keyUsageBits = 32;             CHECK(CheckSelfSignedCodeSigningCert(e2, p.subject, kNow) != "");
    CertInfo e3 = c; e3.ekus = {"1.3.6.1.5.5.7.3.1"};  CHECK(CheckSelfSignedCodeSigningCert(e3, p.subject, kNow) != "");
    CertInfo e4 = c; e4.ski[0] ^= 1;                   CHECK(CheckSelfSignedCodeSigningCert(e4, p.subject, kNow) != "");
    CertInfo e5 = c; e5.hasEku = false;                CHECK(CheckSelfSignedCodeSigningCert(e5, p.subject, kNow) != "");
    CertInfo e6 = c; e6.basicConstraintsCritical = false; CHECK(CheckSelfSignedCodeSigningCert(e6, p.subject, kNow) != "");
    CertInfo e7 = c; e7.sigAlgOid = "1.2.840.113549.1.1.5"; CHECK(CheckSelfSignedCodeSigningCert(e7, p.subject, kNow) != "");

    // Tampering with the DER: every single-byte flip in the TBS part (sampled) and in the signature must be rejected, either by the
    // parser or by the checker. Never accepted.
    int accepted = 0, flips = 0;
    for (std::size_t i = 0; i < der.size(); i += 3) {
        Bytes t = der;
        t[i] ^= 0x01;
        ++flips;
        CertInfo ci;
        if (ParseCertificate(t, ci) && CheckSelfSignedCodeSigningCert(ci, p.subject, kNow) == "") ++accepted;
    }
    CHECK(flips > 100 && accepted == 0);
    // Every truncation and a trailing byte must fail the parser.
    for (std::size_t i = 0; i < der.size(); i += 5) { Bytes t(der.begin(), der.begin() + static_cast<std::ptrdiff_t>(i)); CertInfo ci; CHECK(!ParseCertificate(t, ci)); }
    { Bytes t = der; t.push_back(0); CertInfo ci; CHECK(!ParseCertificate(t, ci)); }
    // Signature replaced by another certificate's signature (same TBS length) fails.
    CertParams p2 = p; p2.reverseNameOrder = false; p2.serial = RandomCertSerial();
    Bytes der2;
    CHECK(MakeSelfSignedCodeSigningCert(key, p2, der2));
    Bytes swapped = der;
    for (std::size_t i = 0; i < 128; ++i) swapped[der.size() - 128 + i] = der2[der2.size() - 128 + i];
    CertInfo cs;
    CHECK(ParseCertificate(swapped, cs) && CheckSelfSignedCodeSigningCert(cs, p.subject, kNow) == "self-signature does not verify");
}

static void BadParameters() {
    const RsaPrivateKey key = KatKey();
    Bytes der;
    std::string why;
    CertParams p = KatParams();
    CertParams q = p; q.subject = "";           CHECK(!MakeSelfSignedCodeSigningCert(key, q, der, &why) && der.empty());
    q = p; q.serial.clear();                    CHECK(!MakeSelfSignedCodeSigningCert(key, q, der));
    q = p; q.serial = Bytes(21, 1);             CHECK(!MakeSelfSignedCodeSigningCert(key, q, der));
    q = p; q.serial = Bytes{0x80};              CHECK(!MakeSelfSignedCodeSigningCert(key, q, der));
    q = p; q.serial = Bytes{0x00, 0x01};        CHECK(!MakeSelfSignedCodeSigningCert(key, q, der));
    q = p; q.notAfter = q.notBefore;            CHECK(!MakeSelfSignedCodeSigningCert(key, q, der));
    q = p; q.notBefore.month = 13;              CHECK(!MakeSelfSignedCodeSigningCert(key, q, der));
    q = p; q.notBefore = UtcDateTime{2027, 2, 29, 0, 0, 0}; CHECK(!MakeSelfSignedCodeSigningCert(key, q, der));
    RsaPrivateKey broken = key; broken.dP = BigInt::Add(broken.dP, BigInt::FromU64(2));
    CHECK(!MakeSelfSignedCodeSigningCert(broken, p, der) && der.empty());
    CHECK(IsValidDateTime(UtcDateTime{2028, 2, 29, 0, 0, 0}) && !IsValidDateTime(UtcDateTime{2100, 2, 29, 0, 0, 0}) && IsValidDateTime(UtcDateTime{2000, 2, 29, 0, 0, 0}));
}

static void ParserStrictness() {
    const Bytes good = FromHexStr(kCertKatDerHex);
    CertInfo c;
    std::string why;
    CHECK(ParseCertificate(good, c, &why));
    CHECK(!ParseCertificate(Bytes(), c));
    CHECK(!ParseCertificate(Bytes{0x30, 0x00}, c));
    // a v1-looking certificate: change the version number 2 -> 0 (bytes 6..8 of the TBS: A0 03 02 01 02)
    Bytes v = good;
    for (std::size_t i = 0; i + 4 < v.size() && i < 40; ++i) if (v[i] == 0xA0 && v[i + 1] == 0x03 && v[i + 2] == 0x02 && v[i + 3] == 0x01 && v[i + 4] == 0x02) { v[i + 4] = 0x00; break; }
    CHECK(!ParseCertificate(v, c));
    // random garbage never crashes and is never accepted
    unsigned seed = 99;
    for (int i = 0; i < 3000; ++i) {
        Bytes junk(static_cast<std::size_t>(20 + i % 300));
        for (auto& x : junk) { seed = seed * 1103515245u + 12345u; x = static_cast<std::uint8_t>(seed >> 16); }
        junk[0] = 0x30;
        CHECK(!ParseCertificate(junk, c));
    }
}

static void KeyFiles() {
#if !defined(_WIN32)
    char dirT[] = "/tmp/okay_sign_cert_XXXXXX";
    if (!mkdtemp(dirT)) return;
    const std::string dir = dirT, path = dir + "/key.pem";
    const RsaPrivateKey key = KatKey();
    CHECK(WriteRsaPrivateKeyPemFile(path, key));
    struct stat st;
    CHECK(stat(path.c_str(), &st) == 0 && (st.st_mode & 0777) == 0600);   // owner read/write only
    CHECK(!WriteRsaPrivateKeyPemFile(path, key));                           // never overwrites
    std::string text;
    if (FILE* f = std::fopen(path.c_str(), "rb")) { int ch; while ((ch = std::fgetc(f)) != EOF) text += static_cast<char>(ch); std::fclose(f); }
    Bytes der;
    RsaPrivateKey back;
    CHECK(PemDecode(text, "RSA PRIVATE KEY", der) && RsaPrivateKeyFromDer(der, back) && back.d == key.d && back.n == key.n);
    CHECK(text.rfind("-----BEGIN RSA PRIVATE KEY-----", 0) == 0);
    const std::string pub = dir + "/cert.cer";
    CHECK(WritePublicFile(pub, FromHexStr(kCertKatDerHex)));
    CHECK(stat(pub.c_str(), &st) == 0 && static_cast<std::size_t>(st.st_size) == FromHexStr(kCertKatDerHex).size());
    CHECK(!WriteNewPrivateFile(dir + "/nonexistent_dir/x", "data"));
    unlink(path.c_str()); unlink(pub.c_str()); rmdir(dir.c_str());   // our own mkdtemp directory
#endif
}

int main() {
    Base64AndPem();
    Names();
    KnownAnswerCertificate();
    GeneratedRoundTrip();
    BadParameters();
    ParserStrictness();
    KeyFiles();
    FINISH("test_sign_cert");
}

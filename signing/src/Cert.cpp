// Self-signed X.509 code-signing test certificate. NOT INDEPENDENTLY REVIEWED. TEST KEY ONLY. See Cert.hpp. From RFC 5280.
#include "okay/Sign/Cert.hpp"

#include <cstdio>

#include "okay/Crypto/Crypto.hpp"
#include "okay/Sign/Der.hpp"

namespace okay::sign {

using Bytes = std::vector<std::uint8_t>;
namespace d = okay::sign::der;

namespace {

const char* const kOidSha256WithRsa = "1.2.840.113549.1.1.11";
const char* const kOidCodeSigning = "1.3.6.1.5.5.7.3.3";
const char* const kOidBasicConstraints = "2.5.29.19";
const char* const kOidKeyUsage = "2.5.29.15";
const char* const kOidEku = "2.5.29.37";
const char* const kOidSki = "2.5.29.14";

struct ShortName { const char* name; const char* oid; };
const ShortName kNames[] = {{"CN", "2.5.4.3"}, {"C", "2.5.4.6"}, {"L", "2.5.4.7"}, {"S", "2.5.4.8"}, {"ST", "2.5.4.8"},
                            {"O", "2.5.4.10"}, {"OU", "2.5.4.11"}, {"STREET", "2.5.4.9"}, {"SERIALNUMBER", "2.5.4.5"},
                            {"DC", "0.9.2342.19200300.100.1.25"}, {"E", "1.2.840.113549.1.9.1"}};

std::string Upper(std::string s) { for (char& c : s) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32); return s; }

bool IsDottedOid(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) if (!((c >= '0' && c <= '9') || c == '.')) return false;
    return true;
}

bool IsPrintable(const std::string& s) {
    for (char ch : s) {
        const unsigned char c = static_cast<unsigned char>(ch);
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' || c == '\'' || c == '(' ||
                        c == ')' || c == '+' || c == ',' || c == '-' || c == '.' || c == '/' || c == ':' || c == '=' || c == '?';
        if (!ok) return false;
    }
    return true;
}
bool IsAscii(const std::string& s) { for (char c : s) if (static_cast<unsigned char>(c) > 0x7F) return false; return true; }

int HexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool Fail(std::string* why, const std::string& m) { if (why) *why = m; return false; }

Bytes EncodeName(const std::vector<DnAttribute>& attrs, bool reverse) {
    std::vector<Bytes> rdns;
    for (std::size_t i = 0; i < attrs.size(); ++i) {
        const DnAttribute& a = attrs[reverse ? attrs.size() - 1 - i : i];
        Bytes str;
        if (a.oid == "2.5.4.6" || a.oid == "2.5.4.5") str = d::PrintableString(a.value);        // countryName, serialNumber: PrintableString
        else if (a.oid == "0.9.2342.19200300.100.1.25" || a.oid == "1.2.840.113549.1.9.1") str = d::Tlv(0x16, Bytes(a.value.begin(), a.value.end()));   // IA5String
        else str = d::Utf8String(a.value);
        rdns.push_back(d::SetOf({d::Sequence({d::Oid(a.oid), str})}));   // one attribute per RDN
    }
    return d::Sequence(rdns);
}

Bytes AlgId() { return d::Sequence({d::Oid(kOidSha256WithRsa), d::Null()}); }

Bytes TimeDer(const UtcDateTime& t) {   // RFC 5280 4.1.2.5: UTCTime through 2049, GeneralizedTime from 2050
    return t.year < 2050 ? d::UtcTime(t.year, t.month, t.day, t.hour, t.minute, t.second)
                         : d::GeneralizedTime(t.year, t.month, t.day, t.hour, t.minute, t.second);
}

Bytes Extension(const char* oid, bool critical, const Bytes& value) {
    std::vector<Bytes> parts{d::Oid(oid)};
    if (critical) parts.push_back(d::Boolean(true));   // DER: the default (FALSE) is never written
    parts.push_back(d::OctetString(value));
    return d::Sequence(parts);
}

Bytes KeyIdentifier(const Bytes& bitStringContent) {   // RFC 7093 section 2, method 1: leftmost 160 bits of SHA-256
    std::uint8_t h[32];
    okay::crypto::Sha256Hash(h, bitStringContent.data(), bitStringContent.size());
    return Bytes(h, h + 20);
}

bool ReadTime(const d::Tlv_& t, UtcDateTime& out) {
    const std::size_t want = t.tag == d::kTagUtcTime ? 13 : 15;
    if ((t.tag != d::kTagUtcTime && t.tag != d::kTagGeneralizedTime) || t.length != want || t.content[want - 1] != 'Z') return false;
    int v[7] = {};
    const std::size_t yl = want == 13 ? 2 : 4;
    std::size_t p = 0;
    auto num = [&](std::size_t n, int& dst) {
        dst = 0;
        for (std::size_t i = 0; i < n; ++i, ++p) { if (t.content[p] < '0' || t.content[p] > '9') return false; dst = dst * 10 + (t.content[p] - '0'); }
        return true;
    };
    if (!num(yl, v[0]) || !num(2, v[1]) || !num(2, v[2]) || !num(2, v[3]) || !num(2, v[4]) || !num(2, v[5])) return false;
    if (want == 13) v[0] += v[0] >= 50 ? 1900 : 2000;
    else if (v[0] < 2050) return false;   // DER certificates use UTCTime before 2050
    out = UtcDateTime{v[0], v[1], v[2], v[3], v[4], v[5]};
    return IsValidDateTime(out);
}

bool ReadName(const d::Tlv_& seq, std::vector<DnAttribute>& out, std::string* why) {
    out.clear();
    d::Reader rdns(seq.content, seq.length);
    while (!rdns.AtEnd()) {
        d::Tlv_ set, ava, oid, val;
        if (!rdns.Expect(d::kTagSet, set)) return Fail(why, "name: RDN is not a SET");
        d::Reader sr(set.content, set.length);
        if (!sr.Expect(d::kTagSequence, ava) || !sr.AtEnd()) return Fail(why, "name: multi-valued or malformed RDN");
        d::Reader ar(ava.content, ava.length);
        DnAttribute a;
        if (!ar.Expect(d::kTagOid, oid) || !d::ReadOid(oid, a.oid) || !ar.Next(val) || !ar.AtEnd()) return Fail(why, "name: bad attribute");
        if (val.tag != 0x0C && val.tag != 0x13 && val.tag != 0x16) return Fail(why, "name: unsupported string type");
        a.value.assign(reinterpret_cast<const char*>(val.content), val.length);
        out.push_back(a);
    }
    return !out.empty() || Fail(why, "name: empty");
}

bool ReadAlgId(const d::Tlv_& seq, std::string& oidOut) {
    d::Reader r(seq.content, seq.length);
    d::Tlv_ o, n;
    return r.Expect(d::kTagOid, o) && d::ReadOid(o, oidOut) && r.Expect(d::kTagNull, n) && n.length == 0 && r.AtEnd();
}

}  // namespace

bool IsValidDateTime(const UtcDateTime& t) {
    if (t.year < 1950 || t.year > 9999 || t.month < 1 || t.month > 12 || t.day < 1 || t.hour < 0 || t.hour > 23 || t.minute < 0 ||
        t.minute > 59 || t.second < 0 || t.second > 59)
        return false;
    static const int dm[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = (t.year % 4 == 0 && t.year % 100 != 0) || t.year % 400 == 0;
    return t.day <= dm[t.month - 1] + (t.month == 2 && leap ? 1 : 0);
}

int CompareDateTime(const UtcDateTime& a, const UtcDateTime& b) {
    const int x[6] = {a.year, a.month, a.day, a.hour, a.minute, a.second}, y[6] = {b.year, b.month, b.day, b.hour, b.minute, b.second};
    for (int i = 0; i < 6; ++i) if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
}

bool ParseDistinguishedName(const std::string& text, std::vector<DnAttribute>& out, std::string* why) {
    out.clear();
    std::size_t i = 0;
    const std::size_t n = text.size();
    auto skipSp = [&] { while (i < n && text[i] == ' ') ++i; };
    for (;;) {
        skipSp();
        std::string type;
        while (i < n && text[i] != '=') type += text[i++];
        if (i >= n) return Fail(why, "name: missing '='");
        ++i;
        while (!type.empty() && type.back() == ' ') type.pop_back();
        DnAttribute a;
        if (IsDottedOid(type)) a.oid = type;
        else {
            for (const ShortName& s : kNames) if (Upper(type) == s.name) a.oid = s.oid;
            if (a.oid.empty()) return Fail(why, "name: unknown attribute type '" + type + "'");
        }
        skipSp();
        std::string v;
        if (i < n && text[i] == '"') {   // quoted: "" is a quote
            ++i;
            for (bool closed = false; !closed;) {
                if (i >= n) return Fail(why, "name: unterminated quote");
                if (text[i] == '"') {
                    if (i + 1 < n && text[i + 1] == '"') { v += '"'; i += 2; } else { ++i; closed = true; }
                } else v += text[i++];
            }
            skipSp();
            if (i < n && text[i] != ',') return Fail(why, "name: text after closing quote");
        } else {
            std::size_t keep = 0;   // length of v that is protected from trailing-space trimming
            while (i < n && text[i] != ',') {
                if (text[i] == '+') return Fail(why, "name: multi-valued RDN (+) is not supported");
                if (text[i] == '\\') {
                    if (i + 1 >= n) return Fail(why, "name: dangling backslash");
                    const char c = text[i + 1];
                    if (HexVal(c) >= 0 && i + 2 < n && HexVal(text[i + 2]) >= 0) { v += static_cast<char>(HexVal(c) * 16 + HexVal(text[i + 2])); i += 3; }
                    else if (std::string(",+=\"\\<>#; ").find(c) != std::string::npos) { v += c; i += 2; }
                    else return Fail(why, "name: bad escape");
                    keep = v.size();
                } else v += text[i++];
            }
            while (v.size() > keep && v.back() == ' ') v.pop_back();
        }
        if (v.empty()) return Fail(why, "name: empty value");
        for (char c : v) if (static_cast<unsigned char>(c) < 0x20) return Fail(why, "name: control character in value");
        if (a.oid == "2.5.4.6" || a.oid == "2.5.4.5") { if (!IsPrintable(v)) return Fail(why, "name: this attribute must be PrintableString characters"); }
        if ((a.oid == "0.9.2342.19200300.100.1.25" || a.oid == "1.2.840.113549.1.9.1") && !IsAscii(v)) return Fail(why, "name: this attribute must be ASCII");
        a.value = v;
        out.push_back(a);
        if (i >= n) break;
        ++i;   // the ','
    }
    return true;
}

std::string FormatDistinguishedName(const std::vector<DnAttribute>& attrs) {
    std::string s;
    for (std::size_t k = 0; k < attrs.size(); ++k) {
        if (k) s += ", ";
        std::string t = attrs[k].oid;
        for (const ShortName& sn : kNames) if (attrs[k].oid == sn.oid) { t = sn.name; break; }
        s += t + "=";
        const std::string& v = attrs[k].value;
        const bool quote = v.find_first_of(",+=\"\\<>#;") != std::string::npos || v.front() == ' ' || v.back() == ' ';
        if (quote) { s += '"'; for (char c : v) { if (c == '"') s += '"'; s += c; } s += '"'; }
        else s += v;
    }
    return s;
}

Bytes RandomCertSerial() {
    Bytes s(16);
    okay::crypto::RandomBytes(s.data(), s.size());
    s[0] = static_cast<std::uint8_t>((s[0] & 0x7F) | 0x40);   // positive, and the first byte is never zero
    return s;
}

bool MakeSelfSignedCodeSigningCert(const RsaPrivateKey& key, const CertParams& p, Bytes& der, std::string* why) {
    der.clear();
    std::vector<DnAttribute> name;
    if (!ParseDistinguishedName(p.subject, name, why)) return false;
    if (p.serial.empty() || p.serial.size() > 20 || p.serial[0] == 0 || p.serial[0] >= 0x80) return Fail(why, "serial must be 1..20 bytes, positive, no leading zero");
    if (!IsValidDateTime(p.notBefore) || !IsValidDateTime(p.notAfter) || CompareDateTime(p.notBefore, p.notAfter) >= 0) return Fail(why, "bad validity times");
    const Bytes spki = RsaPublicKeyToSpkiDer(key.Public());
    Bytes keyBits = RsaPublicKeyToDer(key.Public());   // the content of the subjectPublicKey BIT STRING
    const Bytes nameDer = EncodeName(name, p.reverseNameOrder);
    const Bytes exts = d::Sequence({
        Extension(kOidBasicConstraints, true, d::Sequence({})),
        Extension(kOidKeyUsage, true, d::BitString(Bytes{0x80}, 7)),   // digitalSignature (bit 0) only; DER drops trailing zero bits
        Extension(kOidEku, false, d::Sequence({d::Oid(kOidCodeSigning)})),
        Extension(kOidSki, false, d::OctetString(KeyIdentifier(keyBits)))});
    const Bytes tbs = d::Sequence({d::ContextExplicit(0, d::IntegerU64(2)), d::Integer(BigInt::FromBytes(p.serial)), AlgId(), nameDer,
                                   d::Sequence({TimeDer(p.notBefore), TimeDer(p.notAfter)}), nameDer, spki, d::ContextExplicit(3, exts)});
    Bytes sig;
    if (!RsaSignSha256(key, tbs.data(), tbs.size(), sig)) return Fail(why, "signing failed (bad key?)");
    der = d::Sequence({tbs, AlgId(), d::BitString(sig, 0)});
    return true;
}

bool ParseCertificate(const Bytes& der, CertInfo& out, std::string* why) {
    out = CertInfo();
    CertInfo c;
    d::Reader top(der);
    d::Tlv_ cert, tbs, alg, sigbits;
    if (!top.Expect(d::kTagSequence, cert) || !top.AtEnd()) return Fail(why, "certificate: not one SEQUENCE");
    d::Reader cr(cert.content, cert.length);
    if (!cr.Expect(d::kTagSequence, tbs) || !cr.Expect(d::kTagSequence, alg) || !cr.Expect(d::kTagBitString, sigbits) || !cr.AtEnd())
        return Fail(why, "certificate: bad top-level structure");
    c.tbsDer.assign(tbs.whole, tbs.whole + tbs.wholeLength);
    if (!ReadAlgId(alg, c.sigAlgOid)) return Fail(why, "certificate: bad signatureAlgorithm");
    unsigned unused = 0;
    if (!d::ReadBitString(sigbits, c.signature, unused) || unused != 0 || c.signature.empty()) return Fail(why, "certificate: bad signature bits");

    d::Reader tr(tbs.content, tbs.length);
    d::Tlv_ ver, verInt, serial, tAlg, issuer, validity, subject, spki, extWrap;
    if (!tr.Expect(0xA0, ver)) return Fail(why, "tbs: no version (v3 required)");
    {
        d::Reader vr(ver.content, ver.length);
        BigInt v;
        if (!vr.Expect(d::kTagInteger, verInt) || !d::ReadInteger(verInt, v) || !vr.AtEnd() || v != BigInt::FromU64(2)) return Fail(why, "tbs: version is not v3");
    }
    BigInt sn;
    if (!tr.Expect(d::kTagInteger, serial) || !d::ReadInteger(serial, sn) || sn.IsZero()) return Fail(why, "tbs: bad serial");
    c.serial = sn.ToBytes();
    if (c.serial.size() > 20) return Fail(why, "tbs: serial longer than 20 bytes");
    if (!tr.Expect(d::kTagSequence, tAlg) || !ReadAlgId(tAlg, c.tbsSigAlgOid)) return Fail(why, "tbs: bad signature algorithm");
    if (!tr.Expect(d::kTagSequence, issuer)) return Fail(why, "tbs: no issuer");
    c.issuerDer.assign(issuer.whole, issuer.whole + issuer.wholeLength);
    if (!ReadName(issuer, c.issuer, why)) return false;
    if (!tr.Expect(d::kTagSequence, validity)) return Fail(why, "tbs: no validity");
    {
        d::Reader vr(validity.content, validity.length);
        d::Tlv_ a, b;
        if (!vr.Next(a) || !vr.Next(b) || !vr.AtEnd() || !ReadTime(a, c.notBefore) || !ReadTime(b, c.notAfter)) return Fail(why, "tbs: bad validity times");
    }
    if (!tr.Expect(d::kTagSequence, subject)) return Fail(why, "tbs: no subject");
    c.subjectDer.assign(subject.whole, subject.whole + subject.wholeLength);
    if (!ReadName(subject, c.subject, why)) return false;
    if (!tr.Expect(d::kTagSequence, spki)) return Fail(why, "tbs: no public key");
    {
        const Bytes spkiDer(spki.whole, spki.whole + spki.wholeLength);
        if (!RsaPublicKeyFromSpkiDer(spkiDer, c.publicKey)) return Fail(why, "tbs: public key is not RSA SubjectPublicKeyInfo");
        d::Reader sr(spki.content, spki.length);
        d::Tlv_ x, bits;
        sr.Next(x);
        sr.Next(bits);
        c.spkiBitString.assign(bits.content + 1, bits.content + bits.length);
    }
    if (!tr.Expect(0xA3, extWrap) || !tr.AtEnd()) return Fail(why, "tbs: extensions missing, or unsupported fields follow");
    {
        d::Reader wr(extWrap.content, extWrap.length);
        d::Tlv_ list;
        if (!wr.Expect(d::kTagSequence, list) || !wr.AtEnd()) return Fail(why, "extensions: bad list");
        d::Reader lr(list.content, list.length);
        std::vector<std::string> seen;
        while (!lr.AtEnd()) {
            d::Tlv_ ext, oidT, next;
            if (!lr.Expect(d::kTagSequence, ext)) return Fail(why, "extensions: bad entry");
            d::Reader er(ext.content, ext.length);
            std::string oid;
            if (!er.Expect(d::kTagOid, oidT) || !d::ReadOid(oidT, oid) || !er.Next(next)) return Fail(why, "extensions: bad entry");
            bool critical = false;
            if (next.tag == d::kTagBoolean) {
                if (!d::ReadBoolean(next, critical) || !critical || !er.Next(next)) return Fail(why, "extensions: bad critical flag");   // FALSE must be omitted
            }
            if (next.tag != d::kTagOctetString || !er.AtEnd()) return Fail(why, "extensions: bad value");
            for (const std::string& s : seen) if (s == oid) return Fail(why, "extensions: duplicate " + oid);
            seen.push_back(oid);
            d::Reader vr(next.content, next.length);
            d::Tlv_ v;
            if (oid == kOidBasicConstraints) {
                if (!vr.Expect(d::kTagSequence, v) || !vr.AtEnd()) return Fail(why, "basicConstraints: bad");
                c.hasBasicConstraints = true; c.basicConstraintsCritical = critical;
                d::Reader br(v.content, v.length);
                d::Tlv_ f;
                if (!br.AtEnd()) {
                    bool ca = false;
                    if (!br.Expect(d::kTagBoolean, f) || !d::ReadBoolean(f, ca) || !ca) return Fail(why, "basicConstraints: bad cA");
                    c.isCa = true;
                    if (!br.AtEnd() && (!br.Expect(d::kTagInteger, f) || !br.AtEnd())) return Fail(why, "basicConstraints: bad pathLen");
                }
            } else if (oid == kOidKeyUsage) {
                Bytes bits; unsigned un = 0;
                if (!vr.Expect(d::kTagBitString, v) || !vr.AtEnd() || !d::ReadBitString(v, bits, un) || bits.empty() || bits.size() > 2) return Fail(why, "keyUsage: bad");
                c.hasKeyUsage = true; c.keyUsageCritical = critical;
                for (unsigned i = 0; i < bits.size() * 8 - un; ++i) if ((bits[i / 8] >> (7 - i % 8)) & 1) c.keyUsageBits |= static_cast<std::uint16_t>(1u << i);
            } else if (oid == kOidEku) {
                if (!vr.Expect(d::kTagSequence, v) || !vr.AtEnd()) return Fail(why, "extKeyUsage: bad");
                c.hasEku = true;
                d::Reader er2(v.content, v.length);
                while (!er2.AtEnd()) { d::Tlv_ o; std::string s; if (!er2.Expect(d::kTagOid, o) || !d::ReadOid(o, s)) return Fail(why, "extKeyUsage: bad OID"); c.ekus.push_back(s); }
                if (c.ekus.empty()) return Fail(why, "extKeyUsage: empty");
            } else if (oid == kOidSki) {
                if (!vr.Expect(d::kTagOctetString, v) || !vr.AtEnd() || v.length == 0) return Fail(why, "subjectKeyIdentifier: bad");
                c.hasSki = true;
                c.ski.assign(v.content, v.content + v.length);
            } else if (critical) {
                return Fail(why, "unsupported critical extension " + oid);
            }
        }
    }
    out = c;
    return true;
}

std::string CheckSelfSignedCodeSigningCert(const CertInfo& c, const std::string& expectedPublisher, const UtcDateTime& now, bool reverse) {
    if (c.sigAlgOid != kOidSha256WithRsa || c.tbsSigAlgOid != c.sigAlgOid) return "signature algorithm is not sha256WithRSAEncryption (or inner and outer differ)";
    if (c.issuerDer != c.subjectDer) return "issuer and subject differ (not self-signed)";
    if (!RsaVerifySha256(c.publicKey, c.tbsDer.data(), c.tbsDer.size(), c.signature.data(), c.signature.size())) return "self-signature does not verify";
    std::vector<DnAttribute> want;
    std::string why;
    if (!ParseDistinguishedName(expectedPublisher, want, &why)) return "expected publisher is not a valid name: " + why;
    if (reverse) std::vector<DnAttribute>(want.rbegin(), want.rend()).swap(want);
    if (want != c.subject) return "subject does not equal the expected Publisher";
    if (!c.hasBasicConstraints || c.isCa) return "basicConstraints missing or CA is true";
    if (!c.basicConstraintsCritical) return "basicConstraints is not critical";
    if (!c.hasKeyUsage || (c.keyUsageBits & 1) == 0) return "keyUsage digitalSignature missing";
    if (!c.keyUsageCritical) return "keyUsage is not critical";
    bool code = false;
    for (const std::string& e : c.ekus) if (e == kOidCodeSigning) code = true;
    if (!c.hasEku || !code) return "extendedKeyUsage does not contain codeSigning";
    if (!c.hasSki || c.ski != KeyIdentifier(c.spkiBitString)) return "subjectKeyIdentifier missing or wrong";
    if (CompareDateTime(now, c.notBefore) < 0) return "certificate is not valid yet";
    if (CompareDateTime(now, c.notAfter) > 0) return "certificate has expired";
    return "";
}

}  // namespace okay::sign

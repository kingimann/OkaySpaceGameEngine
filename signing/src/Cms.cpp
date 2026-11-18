// CMS SignedData. NOT INDEPENDENTLY REVIEWED. TEST KEY ONLY. See Cms.hpp. Written from RFC 5652 and RFC 5754.
#include "okay/Sign/Cms.hpp"

#include <algorithm>

#include "okay/Crypto/Crypto.hpp"
#include "okay/Sign/Der.hpp"

namespace okay::sign {

using Bytes = std::vector<std::uint8_t>;
namespace d = okay::sign::der;

namespace {

const char* const kOidSignedData = "1.2.840.113549.1.7.2";
const char* const kOidData = "1.2.840.113549.1.7.1";
const char* const kOidSha256 = "2.16.840.1.101.3.4.2.1";
const char* const kOidRsa = "1.2.840.113549.1.1.1";
const char* const kOidSha256Rsa = "1.2.840.113549.1.1.11";
const char* const kOidAttrContentType = "1.2.840.113549.1.9.3";
const char* const kOidAttrDigest = "1.2.840.113549.1.9.4";
const char* const kOidAttrTime = "1.2.840.113549.1.9.5";

bool Fail(std::string* why, const std::string& m) { if (why) *why = m; return false; }

Bytes AlgId(const char* oid, bool nullParams) {
    return nullParams ? d::Sequence({d::Oid(oid), d::Null()}) : d::Sequence({d::Oid(oid)});
}

Bytes Attr(const char* oid, const Bytes& value) { return d::Sequence({d::Oid(oid), d::SetOf({value})}); }

Bytes Time(const UtcDateTime& t) {   // RFC 5652 11.3 / RFC 5280: UTCTime through 2049
    return t.year < 2050 ? d::UtcTime(t.year, t.month, t.day, t.hour, t.minute, t.second)
                         : d::GeneralizedTime(t.year, t.month, t.day, t.hour, t.minute, t.second);
}

bool ReadAlg(const d::Tlv_& seq, std::string& oid, bool* nullParams) {
    d::Reader r(seq.content, seq.length);
    d::Tlv_ o, n;
    if (!r.Expect(d::kTagOid, o) || !d::ReadOid(o, oid)) return false;
    if (r.AtEnd()) { if (nullParams) *nullParams = false; return true; }
    if (!r.Expect(d::kTagNull, n) || n.length != 0 || !r.AtEnd()) return false;
    if (nullParams) *nullParams = true;
    return true;
}

bool ReadTimeValue(const d::Tlv_& t, UtcDateTime& out) {   // same shapes as the certificate parser accepts
    const std::size_t want = t.tag == d::kTagUtcTime ? 13 : 15;
    if ((t.tag != d::kTagUtcTime && t.tag != d::kTagGeneralizedTime) || t.length != want || t.content[want - 1] != 'Z') return false;
    int v[6] = {};
    std::size_t p = 0;
    const std::size_t yl = want == 13 ? 2 : 4;
    auto num = [&](std::size_t n, int& dst) {
        dst = 0;
        for (std::size_t i = 0; i < n; ++i, ++p) { if (t.content[p] < '0' || t.content[p] > '9') return false; dst = dst * 10 + (t.content[p] - '0'); }
        return true;
    };
    if (!num(yl, v[0]) || !num(2, v[1]) || !num(2, v[2]) || !num(2, v[3]) || !num(2, v[4]) || !num(2, v[5])) return false;
    if (want == 13) v[0] += v[0] >= 50 ? 1900 : 2000; else if (v[0] < 2050) return false;
    out = UtcDateTime{v[0], v[1], v[2], v[3], v[4], v[5]};
    return IsValidDateTime(out);
}

}  // namespace

bool CmsSignSha256(const RsaPrivateKey& key, const Bytes& certDer, const Bytes& content, const CmsSignParams& p, Bytes& out, std::string* why) {
    out.clear();
    CertInfo cert;
    if (!ParseCertificate(certDer, cert, why)) return false;
    if (cert.publicKey.n != key.n || cert.publicKey.e != key.e) return Fail(why, "certificate does not belong to this key");
    if (d::Oid(p.contentTypeOid).empty()) return Fail(why, "bad content type OID");
    if (p.includeSigningTime && !IsValidDateTime(p.signingTime)) return Fail(why, "bad signing time");
    if (p.signedDataVersion != 0 && p.signedDataVersion != 1 && p.signedDataVersion != 3) return Fail(why, "bad SignedData version");

    std::uint8_t dg[32];
    okay::crypto::Sha256Hash(dg, content.data(), content.size());
    std::vector<Bytes> attrs{Attr(kOidAttrContentType, d::Oid(p.contentTypeOid)), Attr(kOidAttrDigest, d::OctetString(Bytes(dg, dg + 32)))};
    if (p.includeSigningTime) attrs.push_back(Attr(kOidAttrTime, Time(p.signingTime)));
    const Bytes attrsSet = d::SetOf(attrs);   // DER SET OF: sorted. These are the bytes that are signed (tag 0x31, RFC 5652 5.4).
    Bytes sig;
    if (!RsaSignSha256(key, attrsSet.data(), attrsSet.size(), sig)) return Fail(why, "signing failed");
    Bytes attrsImplicit = attrsSet;   // stored as [0] IMPLICIT: same content, tag 0xA0
    attrsImplicit[0] = 0xA0;

    const Bytes sid = d::Sequence({cert.issuerDer, d::Integer(BigInt::FromBytes(cert.serial))});
    const Bytes signerInfo = d::Sequence({d::IntegerU64(1), sid, AlgId(kOidSha256, p.digestAlgNullParams), attrsImplicit,
                                          AlgId(p.signatureAlgSha256WithRsa ? kOidSha256Rsa : kOidRsa, true), d::OctetString(sig)});
    std::vector<Bytes> encap{d::Oid(p.contentTypeOid)};
    if (p.encapsulate) encap.push_back(d::ContextExplicit(0, d::OctetString(content)));
    const int version = p.signedDataVersion != 0 ? p.signedDataVersion : (p.contentTypeOid == kOidData ? 1 : 3);
    const Bytes signedData = d::Sequence({d::IntegerU64(static_cast<std::uint64_t>(version)), d::SetOf({AlgId(kOidSha256, p.digestAlgNullParams)}),
                                          d::Sequence(encap), d::ContextImplicit(0, certDer, true), d::SetOf({signerInfo})});
    out = d::Sequence({d::Oid(kOidSignedData), d::ContextExplicit(0, signedData)});
    return true;
}

bool CmsParse(const Bytes& der, CmsInfo& out, std::string* why) {
    out = CmsInfo();
    CmsInfo c;
    d::Reader top(der);
    d::Tlv_ ci, oid, wrap, sd;
    std::string s;
    if (!top.Expect(d::kTagSequence, ci) || !top.AtEnd()) return Fail(why, "cms: not one SEQUENCE");
    d::Reader cr(ci.content, ci.length);
    if (!cr.Expect(d::kTagOid, oid) || !d::ReadOid(oid, s) || s != kOidSignedData) return Fail(why, "cms: not signedData");
    if (!cr.Expect(0xA0, wrap) || !cr.AtEnd()) return Fail(why, "cms: bad content wrapper");
    d::Reader wr(wrap.content, wrap.length);
    if (!wr.Expect(d::kTagSequence, sd) || !wr.AtEnd()) return Fail(why, "cms: bad SignedData");
    d::Reader r(sd.content, sd.length);
    d::Tlv_ t, algs, encap, certs, sis;
    BigInt v;
    if (!r.Expect(d::kTagInteger, t) || !d::ReadInteger(t, v)) return Fail(why, "cms: bad version");
    std::uint64_t ver = 0;
    if (!v.ToU64(ver) || (ver != 1 && ver != 3)) return Fail(why, "cms: unsupported SignedData version");
    c.version = static_cast<int>(ver);
    if (!r.Expect(d::kTagSet, algs)) return Fail(why, "cms: no digestAlgorithms");
    {
        d::Reader ar(algs.content, algs.length);
        d::Tlv_ a;
        if (!ar.Expect(d::kTagSequence, a) || !ar.AtEnd() || !ReadAlg(a, s, nullptr) || s != kOidSha256) return Fail(why, "cms: digestAlgorithms must be exactly SHA-256");
    }
    if (!r.Expect(d::kTagSequence, encap)) return Fail(why, "cms: no encapContentInfo");
    {
        d::Reader er(encap.content, encap.length);
        d::Tlv_ o, ex, oct;
        if (!er.Expect(d::kTagOid, o) || !d::ReadOid(o, c.eContentType)) return Fail(why, "cms: bad eContentType");
        if (!er.AtEnd()) {
            if (!er.Expect(0xA0, ex) || !er.AtEnd()) return Fail(why, "cms: bad eContent");
            d::Reader xr(ex.content, ex.length);
            if (!xr.Expect(d::kTagOctetString, oct) || !xr.AtEnd()) return Fail(why, "cms: eContent is not an OCTET STRING");
            c.hasContent = true;
            c.content.assign(oct.content, oct.content + oct.length);
        }
    }
    if (!r.Expect(0xA0, certs)) return Fail(why, "cms: no certificates");
    {
        d::Reader kr(certs.content, certs.length);
        while (!kr.AtEnd()) {
            d::Tlv_ k;
            if (!kr.Expect(d::kTagSequence, k)) return Fail(why, "cms: bad certificate entry");
            c.certs.emplace_back(k.whole, k.whole + k.wholeLength);
        }
        if (c.certs.empty()) return Fail(why, "cms: empty certificate set");
    }
    if (!r.Expect(d::kTagSet, sis) || !r.AtEnd()) return Fail(why, "cms: signerInfos missing, or CRLs / trailing data present");
    d::Reader sr(sis.content, sis.length);
    d::Tlv_ si;
    if (!sr.Expect(d::kTagSequence, si) || !sr.AtEnd()) return Fail(why, "cms: exactly one signer required");
    d::Reader ir(si.content, si.length);
    d::Tlv_ sv, sid, dalg, attrs, salg, sig;
    if (!ir.Expect(d::kTagInteger, sv) || !d::ReadInteger(sv, v) || v != BigInt::FromU64(1)) return Fail(why, "signer: version must be 1 (issuerAndSerialNumber)");
    if (!ir.Expect(d::kTagSequence, sid)) return Fail(why, "signer: bad sid");
    {
        d::Reader dr(sid.content, sid.length);
        d::Tlv_ iss, ser;
        BigInt sn;
        if (!dr.Expect(d::kTagSequence, iss) || !dr.Expect(d::kTagInteger, ser) || !dr.AtEnd() || !d::ReadInteger(ser, sn)) return Fail(why, "signer: bad issuerAndSerialNumber");
        c.signerIssuerDer.assign(iss.whole, iss.whole + iss.wholeLength);
        c.signerSerial = sn.ToBytes();
    }
    if (!ir.Expect(d::kTagSequence, dalg) || !ReadAlg(dalg, c.digestAlgOid, nullptr) || c.digestAlgOid != kOidSha256) return Fail(why, "signer: digest algorithm must be SHA-256");
    if (!ir.Expect(0xA0, attrs)) return Fail(why, "signer: signed attributes required");
    if (!ir.Expect(d::kTagSequence, salg) || !ReadAlg(salg, c.sigAlgOid, nullptr) || (c.sigAlgOid != kOidRsa && c.sigAlgOid != kOidSha256Rsa)) return Fail(why, "signer: unsupported signature algorithm");
    if (!ir.Expect(d::kTagOctetString, sig) || !ir.AtEnd() || sig.length == 0) return Fail(why, "signer: bad signature / unsigned attributes not supported");
    c.signature.assign(sig.content, sig.content + sig.length);
    c.signedAttrsForSignature.assign(attrs.whole, attrs.whole + attrs.wholeLength);
    c.signedAttrsForSignature[0] = 0x31;   // RFC 5652 5.4: the signature is over the DER of the attributes as an explicit SET OF
    // attributes: each one a SEQUENCE { OID, SET { one value } }; DER order (sorted by encoding); no duplicates
    d::Reader ar(attrs.content, attrs.length);
    Bytes prev;
    bool haveCt = false, haveDg = false;
    while (!ar.AtEnd()) {
        d::Tlv_ a, ao, av;
        if (!ar.Expect(d::kTagSequence, a)) return Fail(why, "attr: bad entry");
        const Bytes enc(a.whole, a.whole + a.wholeLength);
        if (!prev.empty() && !(prev < enc)) return Fail(why, "attr: not in DER SET OF order (or duplicate)");
        prev = enc;
        d::Reader tr(a.content, a.length);
        std::string ao_s;
        if (!tr.Expect(d::kTagOid, ao) || !d::ReadOid(ao, ao_s) || !tr.Expect(d::kTagSet, av) || !tr.AtEnd()) return Fail(why, "attr: bad structure");
        d::Reader vr(av.content, av.length);
        d::Tlv_ val;
        if (!vr.Next(val) || !vr.AtEnd()) return Fail(why, "attr: exactly one value required");
        if (ao_s == kOidAttrContentType) {
            if (haveCt || !d::ReadOid(val, c.attrContentType)) return Fail(why, "attr: bad contentType");
            haveCt = true;
        } else if (ao_s == kOidAttrDigest) {
            if (haveDg || val.tag != d::kTagOctetString) return Fail(why, "attr: bad messageDigest");
            haveDg = true;
            c.attrMessageDigest.assign(val.content, val.content + val.length);
        } else if (ao_s == kOidAttrTime) {
            if (c.hasSigningTime || !ReadTimeValue(val, c.signingTime)) return Fail(why, "attr: bad signingTime");
            c.hasSigningTime = true;
        }
    }
    if (!haveCt || !haveDg) return Fail(why, "attr: contentType and messageDigest are required");
    out = c;
    return true;
}

std::string CmsVerifySelfSigned(const Bytes& der, const Bytes* detached, const UtcDateTime& now, CmsInfo* infoOut) {
    CmsInfo c;
    std::string why;
    if (!CmsParse(der, c, &why)) return why;
    if (c.hasContent == (detached != nullptr)) return c.hasContent ? "content is both inside the message and given detached" : "no content: message is detached and none was given";
    const Bytes& content = c.hasContent ? c.content : *detached;
    std::uint8_t dg[32];
    okay::crypto::Sha256Hash(dg, content.data(), content.size());
    if (c.attrMessageDigest.size() != 32 || !okay::crypto::ConstantTimeEqual(dg, c.attrMessageDigest.data(), 32)) return "content digest does not match the messageDigest attribute";
    if (c.attrContentType != c.eContentType) return "contentType attribute differs from eContentType";
    // the signer's certificate: by issuer and serial
    CertInfo signer;
    bool found = false;
    for (const Bytes& cd : c.certs) {
        CertInfo ci;
        if (!ParseCertificate(cd, ci)) continue;
        if (ci.issuerDer == c.signerIssuerDer && ci.serial == c.signerSerial) { signer = ci; found = true; break; }
    }
    if (!found) return "signer certificate not found among the embedded certificates (or it does not parse)";
    const std::string certProblem = CheckSelfSignedCodeSigningCert(signer, FormatDistinguishedName(signer.subject), now);
    if (!certProblem.empty()) return "certificate: " + certProblem;
    if (!RsaVerifySha256(signer.publicKey, c.signedAttrsForSignature.data(), c.signedAttrsForSignature.size(), c.signature.data(), c.signature.size()))
        return "signature over the signed attributes does not verify";
    if (c.hasSigningTime && (CompareDateTime(c.signingTime, signer.notBefore) < 0 || CompareDateTime(c.signingTime, signer.notAfter) > 0))
        return "signing time is outside the certificate's validity";
    if (infoOut) *infoOut = c;
    return "";
}

}  // namespace okay::sign

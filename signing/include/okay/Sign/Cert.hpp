#pragma once
// Self-signed X.509 v3 certificate writer, parser and checker for our own test-key signing (docs/own_signing_plan.md, stage 3).
//
// NOT INDEPENDENTLY REVIEWED. TEST KEY ONLY. This produces and checks ONLY the one kind of certificate we need: a self-signed
// RSA code-signing test certificate for an APPX/MSIX package. It is not a general X.509 library and must not be used to make
// or judge a certificate that anyone relies on. Written from RFC 5280 (sections 4.1, 4.2.1.2, 4.2.1.3, 4.2.1.9, 4.2.1.12,
// 5.1 not needed, appendix A.1 ASN.1 modules), RFC 4514 for the string form of names (see the note on order), and RFC 7093
// section 2 for the key identifier. Signatures use stage 2 (sha256WithRSAEncryption, RFC 8017); DER uses stage 1.
//
// No hidden clock: the validity times are passed in, so output is deterministic. No <mutex>/<thread>.
#include <cstdint>
#include <string>
#include <vector>

#include "okay/Sign/Rsa.hpp"

namespace okay::sign {

struct UtcDateTime {
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
};
bool IsValidDateTime(const UtcDateTime& t);
int CompareDateTime(const UtcDateTime& a, const UtcDateTime& b);   // -1, 0, 1

struct DnAttribute {
    std::string oid;     // dotted, e.g. "2.5.4.3" (commonName)
    std::string value;   // UTF-8
    friend bool operator==(const DnAttribute& a, const DnAttribute& b) { return a.oid == b.oid && a.value == b.value; }
};

/// Parses a name string such as `CN=Okay Test, O=Okay Space, C=US` (what an APPX manifest holds in Identity/@Publisher). Types:
/// CN, C, L, S, ST, O, OU, STREET, DC, E, SERIALNUMBER, or a dotted OID. Values may be quoted ("a, b", a doubled quote is a
/// quote) or use backslash escapes (\, \" \\ \+ \= \; \< \> \# and \hh). Multi-valued RDNs (+) are refused. The attributes stay
/// in the order written. Returns false and a reason in *why on bad input.
bool ParseDistinguishedName(const std::string& text, std::vector<DnAttribute>& out, std::string* why = nullptr);
/// Back to the same string form (types by short name where known, values quoted/escaped when needed).
std::string FormatDistinguishedName(const std::vector<DnAttribute>& attrs);

struct CertParams {
    std::string subject;          // the manifest Publisher string; issuer is the same (self-signed)
    std::vector<std::uint8_t> serial;   // 1 to 20 bytes, big-endian, positive; use RandomCertSerial()
    UtcDateTime notBefore, notAfter;
    /// false (default): RDNs are written into the certificate in the order of the string (the way Windows tools print a name,
    /// first attribute first: UNVERIFIED against a real Windows check). true: reversed, as RFC 4514 defines the string form.
    bool reverseNameOrder = false;
};

/// 16 random bytes (okay::crypto::RandomBytes), positive, top byte non-zero.
std::vector<std::uint8_t> RandomCertSerial();

/// Builds and signs the certificate (DER). Extensions, in this order: basicConstraints (critical, CA false), keyUsage (critical,
/// digitalSignature), extendedKeyUsage (codeSigning 1.3.6.1.5.5.7.3.3), subjectKeyIdentifier (RFC 7093 method 1: the leftmost
/// 160 bits of the SHA-256 of the public key bit string). False and a reason on bad parameters or if signing fails.
bool MakeSelfSignedCodeSigningCert(const RsaPrivateKey& key, const CertParams& params, std::vector<std::uint8_t>& der,
                                   std::string* why = nullptr);

struct CertInfo {
    std::vector<std::uint8_t> tbsDer;       // the exact bytes that were signed (tag, length and content)
    std::vector<std::uint8_t> serial;
    std::vector<std::uint8_t> issuerDer, subjectDer;   // the whole Name encodings
    std::vector<DnAttribute> issuer, subject;
    UtcDateTime notBefore, notAfter;
    RsaPublicKey publicKey;
    std::vector<std::uint8_t> spkiBitString;   // content of the subjectPublicKey BIT STRING
    std::string sigAlgOid, tbsSigAlgOid;       // outer and inner signature algorithm (must be equal)
    std::vector<std::uint8_t> signature;
    bool hasBasicConstraints = false, basicConstraintsCritical = false, isCa = false;
    bool hasKeyUsage = false, keyUsageCritical = false;
    std::uint16_t keyUsageBits = 0;            // bit 0 (value 1) = digitalSignature, bit 5 (32) = keyCertSign (RFC 5280 order)
    bool hasEku = false;
    std::vector<std::string> ekus;
    bool hasSki = false;
    std::vector<std::uint8_t> ski;
};

/// Strict DER parse of a certificate of the shape we write (v3, RSA, sha256WithRSAEncryption, UTCTime/GeneralizedTime,
/// extensions limited to the four above; any other CRITICAL extension is refused, other non-critical ones are skipped). It checks
/// structure only, not trust. False and a reason on anything else.
bool ParseCertificate(const std::vector<std::uint8_t>& der, CertInfo& out, std::string* why = nullptr);

/// Everything our own test certificate must satisfy, returning "" when all hold, else the first reason. Checks: signature
/// algorithms equal and sha256WithRSAEncryption; issuer == subject (same bytes); the self-signature verifies with the certificate's
/// own key over the exact TBS bytes; subject equals `expectedPublisher` (attribute by attribute, in order, order reversed if
/// the certificate was written reversed: pass the same flag); extensions as above (basicConstraints present and CA false,
/// keyUsage digitalSignature, EKU contains codeSigning, SKI present and correct); notBefore <= now <= notAfter.
std::string CheckSelfSignedCodeSigningCert(const CertInfo& c, const std::string& expectedPublisher, const UtcDateTime& now,
                                           bool reverseNameOrder = false);

}  // namespace okay::sign

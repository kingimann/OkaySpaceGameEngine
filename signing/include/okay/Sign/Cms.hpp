#pragma once
// CMS / PKCS #7 SignedData writer, parser and verifier for our own test-key signing (docs/own_signing_plan.md, stage 4).
//
// NOT INDEPENDENTLY REVIEWED. TEST KEY ONLY. Generic SignedData with ONE signer, SHA-256, an RSA signature, signed attributes and
// our own stage-3 certificate. It is NOT the APPX/Authenticode signature (no SPC indirect data content yet: stage 5, whose exact layout
// is unverified) and not a general CMS library. Written from RFC 5652 (sections 3, 4, 5.1 to 5.4, 11.1 to 11.3), RFC 5754 (SHA-2 with
// CMS) and RFC 8017 (signatures, via stage 2); DER via stage 1; the certificate via stage 3. Do not use it to protect anything real.
// No hidden clock: the signing time is passed in. No <mutex>/<thread>.
#include <cstdint>
#include <string>
#include <vector>

#include "okay/Sign/Cert.hpp"

namespace okay::sign {

struct CmsSignParams {
    UtcDateTime signingTime;
    bool includeSigningTime = true;
    bool encapsulate = true;   // true: the content is inside the SignedData (eContent OCTET STRING); false: detached
    std::string contentTypeOid = "1.2.840.113549.1.7.1";   // id-data
    /// SignedData.version. 0 = what RFC 5652 5.1 says (1 for id-data, 3 for any other content type). 1 or 3 forces it
    /// (Authenticode-style SignedData uses 1 even for its own content type: stage 5 decides from the reference package).
    int signedDataVersion = 0;
    /// true (default): the digest algorithm identifiers carry NULL parameters (05 00), as older Microsoft code writes them;
    /// false: parameters absent (RFC 5754 section 2 preferred form). Which one Windows wants is UNVERIFIED.
    bool digestAlgNullParams = true;
    /// true (default): signerInfo.signatureAlgorithm = sha256WithRSAEncryption; false: rsaEncryption (both allowed by RFC 5754).
    bool signatureAlgSha256WithRsa = true;
};

/// Builds ContentInfo { signedData } as DER. `certDer` must be a certificate whose public key matches `key` (checked). False and a
/// reason on bad input.
bool CmsSignSha256(const RsaPrivateKey& key, const std::vector<std::uint8_t>& certDer, const std::vector<std::uint8_t>& content,
                   const CmsSignParams& params, std::vector<std::uint8_t>& out, std::string* why = nullptr);

struct CmsInfo {
    int version = 0;
    std::string eContentType;
    bool hasContent = false;
    std::vector<std::uint8_t> content;                 // the eContent bytes when encapsulated
    std::vector<std::vector<std::uint8_t>> certs;      // each certificate's whole DER
    std::vector<std::uint8_t> signerIssuerDer;         // signer id: issuer Name (whole DER) and serial (minimal big-endian)
    std::vector<std::uint8_t> signerSerial;
    std::string digestAlgOid, sigAlgOid;
    std::vector<std::uint8_t> signedAttrsForSignature; // the attributes re-tagged as SET (0x31), the bytes that were signed
    std::vector<std::uint8_t> signature;
    std::string attrContentType;
    std::vector<std::uint8_t> attrMessageDigest;
    bool hasSigningTime = false;
    UtcDateTime signingTime;
};

/// Strict DER parse of the shape CmsSignSha256 writes (one signer, issuerAndSerialNumber, signed attributes present and in DER order,
/// no CRLs, no unsigned attributes, SHA-256). Structure only, no trust decision.
bool CmsParse(const std::vector<std::uint8_t>& der, CmsInfo& out, std::string* why = nullptr);

/// Full check of our own output. Returns "" when valid, else the first reason. Steps: parse; content from the SignedData or from
/// `detachedContent` (exactly one of the two); SHA-256 of the content equals the messageDigest attribute; the contentType attribute
/// equals eContentType; the signer's certificate is found by issuer and serial among the embedded ones and passes
/// CheckSelfSignedCodeSigningCert at time `now` (self-signature, code-signing fields; its own subject is used as the expected name);
/// the RSA signature over the signed attributes verifies with that certificate's key; the signing time, if present, lies inside the
/// certificate's validity. `detachedContent` may be null for an encapsulated message.
std::string CmsVerifySelfSigned(const std::vector<std::uint8_t>& der, const std::vector<std::uint8_t>* detachedContent,
                                const UtcDateTime& now, CmsInfo* infoOut = nullptr);

}  // namespace okay::sign

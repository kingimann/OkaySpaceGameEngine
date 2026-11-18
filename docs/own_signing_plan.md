# Own code signing for APPX/MSIX test packages: plan only

Status: PLAN. No code exists. Approved by the owner on 2026-11-18 for planning only. Nothing here is built, run on
Windows, or reviewed. Statements about file formats are written from memory of the public documents and are marked
"verify" where we must re-read the spec before coding.

## 1. Goal and what is NOT claimed

Goal: our own APPX/MSIX packer (branch `claude/xbox-uwp-1`, another agent, not touched here) should be able to sign a
package with a self-signed certificate, so an Xbox in Developer Mode (or a Windows PC) accepts it as a test package
without Microsoft's SignTool.

This signing only has to satisfy Windows' check on a self-signed Developer Mode test certificate that the owner
installs by hand. It is NOT claimed to be:

- safe for anything security-critical, or for protecting end users in any way;
- a replacement for real code signing. It is not for store submission, and it is not for shipping to players;
- independently reviewed. No reviewer is planned (CLAUDE.md rule 18). Every piece is "not independently reviewed";
- hardened against side channels. The test key protects nothing, so a leaked key costs nothing except that someone
  could sign a package that claims to be ours, on machines that trust that one test certificate.

Rules that bind this work: rule 18 (libsodium stays the default for networking; `okay::crypto` stays off by default;
this plan does not change that and does not touch the network code), rule 19a (own code from public specs, nothing
copied or closely followed from OpenSSL, mbedTLS, BoringSSL, Microsoft code or samples; no new third-party library),
rule 4a (the owner must be able to try it; see section 5).

## 2. What AppxSignature.p7x contains (from public APPX / OPC documentation; verify each point)

An APPX/MSIX package is a ZIP (Open Packaging Conventions). Signing adds one more ZIP entry, `AppxSignature.p7x`.

File layout of the p7x:

1. 4 bytes: ASCII `PKCX` (0x58 0x43 0x4B 0x50 on disk is `P K C X`; verify byte order against a real file).
2. Then a DER-encoded PKCS#7 / CMS `ContentInfo` holding `SignedData` (RFC 5652):
   - `version` 1, `digestAlgorithms` = { SHA-256 },
   - `encapContentInfo`: content type `SPC_INDIRECT_DATA_OBJID` (1.3.6.1.4.1.311.2.1.4), content = `SpcIndirectDataContent`:
     - `data` (SpcAttributeTypeAndOptionalValue): type `SPC_SIPINFO_OBJID` (1.3.6.1.4.1.311.2.1.30), value `SpcSipInfo`
       (an integer version, the APPX SIP GUID, and reserved zero integers; verify the exact GUID and field list),
     - `messageDigest` (DigestInfo): algorithm SHA-256 and a digest value (see below).
   - `certificates` [0]: the signing certificate (the self-signed one),
   - `signerInfos`: one `SignerInfo` with issuerAndSerialNumber, digestAlgorithm SHA-256, signed attributes
     (content type, message digest of the SpcIndirectData content, and normally the Microsoft opus info
     attribute; verify which ones Windows insists on), signature algorithm `rsaEncryption` or `sha256WithRSAEncryption`
     (1.2.840.113549.1.1.1 / .11; verify which Windows wants here), and the RSA signature over the DER of the signed
     attributes (re-tagged as SET, as RFC 5652 section 5.4 requires).
   - A timestamp (countersignature) is not needed for a test package. Its absence means the signature is only valid
     while the certificate is valid. Use a long validity.

What is signed (the digest value in the DigestInfo): not a plain hash of the package. It is a small structure: a 4-byte
magic `APPX`, then up to five tagged SHA-256 hashes, each a 4-byte tag followed by 32 bytes:

| Tag | Hashes (verify exactly which bytes) |
|---|---|
| `AXPC` | the ZIP file records (the local headers and file data, in order, up to the central directory), without the signature entry |
| `AXCD` | the ZIP central directory, without the signature entry |
| `AXCT` | `[Content_Types].xml` |
| `AXBM` | `AppxBlockMap.xml` |
| `AXCI` | `AppxMetadata/CodeIntegrity.cat`, only present when the package has one (we expect not to) |

`AppxManifest.xml` is not hashed directly by the signature. It is covered because `AppxBlockMap.xml` holds the SHA-256
of every 64 KiB block of every file in the package (the manifest included), and the block map hash `AXBM` is signed.
The user's brief lists the manifest as a signed hash; the public docs, as I remember them, say it goes through the block
map. Verify this first, because it decides what the packer must emit. The digests live in the DigestInfo inside
SpcIndirectData; the CMS signed attribute `messageDigest` is the hash of that SpcIndirectData content, and the RSA
signature covers the signed attributes.

Certificate requirements (verify against the current "Create a certificate for package signing" document):

- Subject DN must equal the manifest `Identity Publisher` attribute exactly (same attributes, same order, same
  characters; a mismatch is the usual reason for a signing error).
- Extended Key Usage contains Code Signing (1.3.6.1.5.5.7.3.3). Microsoft's own instructions also use a custom
  EKU OID 1.3.6.1.4.1.311.84.3.1 in some examples; verify whether it is required.
- Key Usage: digitalSignature. Basic constraints: not a CA.
- RSA, 2048 bits or larger, SHA-256. Validity must cover the install time.
- The certificate must be trusted on the target: the owner installs the `.cer` into Trusted People (or Trusted Root)
  on the PC, and Xbox Developer Mode takes it through the Dev Home / device portal sideload flow (verify the exact Xbox
  steps; unknown to us today).

## 3. Building blocks and what each is written from

Check the repo first (done 2026-11-18 on origin/main 7d2ad98f):

| Block | Written from | Already in repo? |
|---|---|---|
| SHA-256 | FIPS 180-4 | Yes: `engine/include/okay/Crypto/Sha256.hpp` (the only SHA-256; header-only; not behind OKAY_OWN_CRYPTO). Reuse it. |
| `RandomBytes`, `MemZero`, `ConstantTimeEqual` | OS entropy | Yes in `okay::crypto`. |
| Big integers + modular arithmetic | Knuth TAOCP vol. 2 section 4.3 (general algorithms), FIPS 186-5 appendix for sizes; Montgomery multiplication from the original paper | No. New. |
| RSA PKCS#1 v1.5 signature, EMSA-PKCS1-v1_5 with SHA-256 DigestInfo prefix | RFC 8017 sections 5.2, 8.2, 9.2 | No. New. |
| RSA key generation, Miller-Rabin | FIPS 186-5 appendix B (prime generation, rounds for 1024-bit primes), RFC 8017 section 3 | No. New. Use CRT form (p, q, dP, dQ, qInv) for signing speed. |
| ASN.1 DER writer (and a small reader for tests) | ITU-T X.690 (DER subset only) | No. New. |
| X.509 v3 self-signed certificate | RFC 5280 | No. New. |
| PKCS#7 / CMS SignedData | RFC 5652; Microsoft SPC structures from the public Authenticode PE specification and the APPX/OPC docs | No. New. |
| PFX / PKCS#12 | RFC 7292 | Only if the owner needs to export a key file for SignTool or the Windows store. Otherwise we write the key in our own simple private file and the `.cer` (DER) public certificate. Default: skip PFX. |

Notes:

- Constant time. A test key signs rarely, offline, on the owner's own machine, so timing leaks are low risk. Still,
  the signing exponentiation should avoid secret-dependent table indexes and use a fixed-window or Montgomery ladder,
  and RSA blinding is cheap to add. If it is not constant time, the doc and the tool output say so. Key generation does
  not need constant time (primes are discarded candidates), but needs a good random source.
- Random: only `okay::crypto::RandomBytes`. No homemade generator (as `docs/crypto.md` already requires).
- Key size 2048 bit, public exponent 65537, p and q 1024-bit, |p-q| large, gcd(e, p-1) = 1, Miller-Rabin 5+ rounds on
  random bases after trial division (FIPS 186-5 table for 1024-bit candidates).
- Where it lives: a new static library next to `okay_crypto`, e.g. `engine/src/Sign/`, separate from `okay::crypto`
  so the network library never links it. No `<mutex>`/`<thread>` (rule 21a), because the packer may end up in a Windows tool.

## 4. Test plan

Public known-answer tests (ours, checked in):

- SHA-256: already covered; add nothing.
- Big integer: random operand identities (add/sub/mul/divmod/modexp/modinv) against a second implementation that is
  deliberately simple (schoolbook), plus fixed vectors computed with Python's built-in big integers (Python is a
  testing tool here; vectors copied as numbers, not code).
- RSA: the RFC 8017 / PKCS#1 test vectors (the RSAES/RSASSA-PKCS1-v1_5 vectors published with the RSA Labs
  "pkcs-1v2-1-vec" set) and NIST CAVP SigGen/SigVer PKCS#1 v1.5 SHA-256 vectors (fixed-key vectors, 2048-bit). Sign
  with a known key and compare the exact bytes (v1.5 is deterministic). Verify known good and known bad signatures.
- DER: round-trip and exact-encoding tests for each type used (INTEGER leading-zero rule, long-form lengths, OID base-128,
  UTCTime vs GeneralizedTime, SET OF sorting, which DER requires for signed attributes).
- X.509 and CMS: parse our own output back with our DER reader and check the fields.

Cross-check with tools on the Linux machine (TESTING ONLY): `openssl` is installed at `/usr/bin/openssl`. It may be used
from a script in `tests/` or by hand to confirm our output: `openssl x509 -inform DER -in test.cer -text -noout`,
`openssl asn1parse -inform DER`, `openssl pkcs7 -inform DER -print` for the CMS part, `openssl rsa -check` for the key,
`openssl dgst -sha256 -verify` for a raw signature, `openssl smime -verify` / `openssl cms -verify` on the CMS blob. Nothing
from OpenSSL (code, headers, test data, output formatting) is copied into the repo, nothing links to it, and the
tests that call it are optional and skipped when it is missing (not part of Windows CI). We also generate keys with
OpenSSL only to compare against, never to ship.

## 5. Staging, switch and editor display

Each stage is its own branch and its own review of the docs; all of it stays OFF by default.

1. DER writer and reader, big integer, Miller-Rabin; tests (no key material yet).
   **Status 2026-11-18: written and tested, NOT independently reviewed, test key only, not wired to anything.**
   Where: `signing/` (library `okay_sign`, headers `signing/include/okay/Sign/{BigInt,Prime,Der}.hpp`, sources `signing/src/`,
   tests `signing/tests/`). Built only with `-DOKAY_OWN_SIGNING=ON` (default OFF; `scripts/check_release_own_stack.sh` refuses
   it in releases and its self-test covers that). Not linked by the engine or the network code; it links `okay_crypto` only for
   `RandomBytes` (Miller-Rabin bases). No new hash code: stage 1 does not need SHA-256 at all, it first appears in stage 2.
   Written from Knuth TAOCP vol. 2 4.3.1 (add, subtract, multiply, long division "algorithm D"), the extended Euclidean
   algorithm, FIPS 186-5 appendix B and ITU-T X.690. What exists: BigInt (add, sub, mul, divmod, shifts, gcd, modexp, modular
   inverse, compare, big-endian bytes, hex), `IsProbablePrime` (trial division below 1000, then Miller-Rabin rounds with OS-random
   bases), `MillerRabinRound` with a chosen base, DER writer (lengths, BOOLEAN, INTEGER, NULL, OCTET/BIT STRING, OID, UTF8/Printable
   string, UTCTime/GeneralizedTime, SEQUENCE, SET, SET OF sorted, context tags) and a strict DER reader (rejects indefinite or
   non-minimal lengths, non-minimal or negative INTEGERs, BER booleans, bad OIDs and bit strings). Tests: four programs, run
   with ctest names `sign_bigint`, `sign_prime`, `sign_der`, `sign_openssl_crosscheck`; known answers from Python integers
   (numbers only), X.690 byte examples and well-known OIDs, random identities ((a+b)-b, divmod, distributive, modexp against
   repeated multiplication, inverse), edge-limb division that exercises the add-back step, Carmichael numbers and strong
   pseudoprimes, a small reader fuzz, and an optional `openssl asn1parse` / `openssl prime` cross-check (skipped when openssl
   is absent). Not done in stage 1: no constant-time code, no Montgomery multiplication (modexp is plain square-and-multiply
   with long-division reduction: correct but slow, to be measured and improved in stage 2), no prime generation function, no
   signed INTEGERs, no multi-byte tag numbers, no DER parser for whole certificates. Checked here with g++ 13 on Linux only
   (also under AddressSanitizer and UndefinedBehaviorSanitizer); never compiled with MinGW or MSVC.
2. RSA key generation, sign, verify (PKCS#1 v1.5 SHA-256); known-answer and OpenSSL cross-check tests.
   **Status 2026-11-18: written and tested, NOT independently reviewed, test key only, not wired to anything.**
   Where: `signing/include/okay/Sign/Rsa.hpp`, `signing/src/Rsa.cpp`, tests `signing/tests/test_sign_rsa.cpp` and
   `test_sign_rsa_openssl.cpp`; same switch (`OKAY_OWN_SIGNING`, default OFF, refused by releases). Written from RFC 8017
   (3, 5.1, 5.2, 8.2, 9.2, A.1) and FIPS 186-5 appendix A. Reuses `okay::crypto::Sha256Hash` (no new hash code) and
   `RandomBytes`. What exists: `RsaGenerateKey` (even size from 512 bits, e = 65537, two fresh random primes with the top two
   bits set, trial division then Miller-Rabin, |p-q| > 2^(half-100), d from lcm(p-1, q-1) and d > 2^half, CRT values),
   `RsaCheckPrivateKey`, `RsaEncodeEmsaSha256` (DigestInfo written with our DER writer), `RsaSignSha256` / `RsaSignDigestSha256`
   (CRT; the result is checked with e before it is returned), `RsaVerifySha256` / `RsaVerifyDigestSha256` (exact length, s < n,
   re-encode and compare bytes, so no padding parser to get wrong), and PKCS#1 private and public key DER export and strict import.
   Tests: exact signatures for fixed 1024 and 2048-bit test keys (6 vectors; numbers computed with Python integers and each
   one equal to what `openssl dgst -sha256 -sign` produced; these are NOT official NIST CAVP or RFC vectors, which could not
   be fetched here), the RFC 8017 DigestInfo prefix constant, round trips at several sizes and message lengths, tampered
   message / signature bit / signature length / s = n / zero / all-ones / s + n, wrong key, wrong exponent, a broken private key
   refusing to sign, and hand-made malformed blocks that carry a valid RSA operation (wrong block type, non-FF padding, short
   padding with trailing garbage, missing NULL parameters, other digest) which verify must reject. Optional cross-check (skipped
   without openssl): `openssl rsa -check` accepts our exported key, `openssl dgst -sha256 -verify` accepts our signatures and
   rejects a changed message, and our verify accepts openssl's signature made with our key (identical bytes, as expected for
   a deterministic scheme). Timings, g++ 13 `-O2`, one Linux machine, no other load: 2048-bit key generation 0.5 to 1.6 s
   (12 runs, no run over 1.6 s), signing about 8.5 ms, verifying about 0.1 ms. So the plain square-and-multiply modexp
   from stage 1 is fast enough and Montgomery / sliding window was NOT needed; it stays a possible later improvement.
   Not done / limits: not constant time and no blinding (a local timing attacker could recover the key; irrelevant for a
   throw-away test key, fatal for a real one), no private-key file writer (owner-only permissions, rule 15) yet, no SPKI
   public key encoding yet (stage 3), no sizes below 512 bits, 3072/4096-bit timings not measured, no RSA-PSS, only SHA-256.
   Verified only with g++ 13 on Linux (also AddressSanitizer + UndefinedBehaviorSanitizer on the RSA test); never built with
   MinGW or MSVC.
3. Self-signed X.509 v3 certificate (subject from the manifest Publisher, code signing EKU, key usage, validity);
   `.cer` output that `openssl x509` can read.
   **Status 2026-11-18: written and tested, NOT independently reviewed, test key only, not wired to anything.**
   Where: `signing/include/okay/Sign/{Cert,Pem}.hpp`, `signing/src/{Cert,Pem}.cpp`, SPKI functions added to `Rsa.hpp`;
   tests `signing/tests/test_sign_cert.cpp`, `test_sign_cert_openssl.cpp`. Same switch (`OKAY_OWN_SIGNING`, default OFF,
   refused by releases). Written from RFC 5280 (4.1, 4.2.1.2, 4.2.1.3, 4.2.1.9, 4.2.1.12), RFC 7093 section 2 (key identifier:
   leftmost 160 bits of SHA-256, so no SHA-1 code is needed), RFC 4648 and RFC 7468 (base64, PEM). What exists:
   `MakeSelfSignedCodeSigningCert` (v3, random 16-byte serial from `RandomCertSerial`, sha256WithRSAEncryption, issuer = subject,
   validity passed in as `UtcDateTime`, UTCTime before 2050 and GeneralizedTime from 2050, SPKI, extensions basicConstraints
   critical CA:FALSE, keyUsage critical digitalSignature, extendedKeyUsage codeSigning, subjectKeyIdentifier; no hidden clock),
   `ParseDistinguishedName` / `FormatDistinguishedName` for the manifest Publisher string (CN, C, L, S/ST, O, OU, STREET, DC, E,
   SERIALNUMBER or dotted OIDs; quotes, backslash escapes; multi-valued RDNs refused), `ParseCertificate` (strict DER, v3 only,
   unknown critical extensions refused, duplicates refused, trailing bytes refused), `CheckSelfSignedCodeSigningCert` (returns a
   reason string: algorithms, issuer == subject bytes, self-signature over the exact TBS bytes, subject equals the expected
   Publisher, extensions, key identifier, validity at a given time), PEM and base64, `WriteRsaPrivateKeyPemFile` (POSIX mode
   0600 from the first byte, refuses to overwrite; on Windows a plain new file, no ACL set, UNVERIFIED there) and
   `WritePublicFile` for the DER `.cer`. Key bytes are never printed or logged.
   Tests (all pass, also under AddressSanitizer + UndefinedBehaviorSanitizer): RFC 4648 base64 vectors, DN parse/format cases,
   a known-answer certificate whose bytes were produced by a separate small Python encoder (not our code; our writer must give
   the identical bytes, the signature being deterministic; the file was also read by `openssl x509 -text` and accepted by
   `openssl verify`), generated-key round trips, tampered DER (every third byte flipped: none accepted), truncations, swapped
   signature, wrong key, issuer/subject mismatch, wrong Publisher, wrong order, validity edges, bad parameters, random-junk parser
   fuzz, key file mode 0600 and no-overwrite. Optional openssl cross-check (skipped without openssl): `openssl x509 -inform DER
   -text` shows version 3, sha256WithRSAEncryption, 2048-bit key, CA:FALSE critical, Digital Signature critical, Code Signing,
   key identifier and the DN; `openssl verify -CAfile self.pem self.pem` says OK; the key file and the certificate have the same
   modulus. Nothing from OpenSSL is in the repo.
   UNVERIFIED and open: (a) the order of the name's attributes inside the certificate. We write them in the order of the Publisher
   string (how Windows prints a name, as I understand it) and offer `reverseNameOrder` for the RFC 4514 reading; only a real
   Windows check on the owner's PC can settle which one it accepts. (b) String types: UTF8String, except C and serialNumber
   (PrintableString) and DC and E (IA5String); Windows may compare strings, not bytes; unknown. (c) Microsoft's own
   `New-SelfSignedCertificate` example adds basicConstraints without marking it critical; we mark it critical (RFC 5280 allows
   both for an end-entity certificate); not known whether Windows cares. (d) No PFX / PKCS #12 export, no thumbprint helper.
   (e) Only RSA; only the four extensions; not a general X.509 library.
4. CMS SignedData builder (generic), then the Microsoft SPC indirect data content on top.
   **Status 2026-11-18: the generic CMS part is written and tested, NOT independently reviewed, test key only, not wired to
   anything. The Microsoft SPC part is NOT written (it belongs to stage 5, whose exact layout is unverified).**
   Where: `signing/include/okay/Sign/Cms.hpp`, `signing/src/Cms.cpp`, tests `signing/tests/test_sign_cms.cpp` and
   `test_sign_cms_openssl.cpp`. Same switch (`OKAY_OWN_SIGNING`, default OFF, refused by releases). Written from RFC 5652
   (3, 4, 5.1 to 5.4, 11.1 to 11.3) and RFC 5754 (SHA-2 in CMS); SHA-256 is `okay::crypto::Sha256Hash`. What exists:
   `CmsSignSha256` (ContentInfo { signedData }: version by the RFC 5652 5.1 rule or forced to 1 or 3; one SHA-256 digest
   algorithm with NULL parameters or without; encapsulated or detached content under any content-type OID; our stage-3
   certificate in the certificates field; one SignerInfo with issuerAndSerialNumber, signed attributes contentType,
   messageDigest and an optional signingTime passed in, kept in DER SET OF order, signed after re-tagging [0] to SET (RFC 5652
   5.4) with sha256WithRSAEncryption or rsaEncryption), `CmsParse` (strict DER, one signer, signed attributes required and in
   DER order, no CRLs, no unsigned attributes, unknown attributes ignored), `CmsVerifySelfSigned` (content from the message or
   a detached copy, never both; digest; contentType attribute; signer certificate found by issuer and serial and checked with the
   stage-3 checker including its self-signature; RSA signature over the signed attributes; signing time inside the
   certificate validity). The eContent is always an OCTET STRING here; Authenticode's content is a different ASN.1 type (stage 5).
   Tests (all pass, also under AddressSanitizer + UndefinedBehaviorSanitizer): exact bytes against two messages (encapsulated and
   detached) assembled by a separate small Python encoder (not our code; both also accepted by `openssl cms -verify -noverify`
   when made); 8 option combinations times 5 content sizes; other content type and version forcing; refusals when signing (wrong
   certificate for the key, bad OID, bad time, bad version, bad certificate bytes); tampered content, detached content wrong,
   both or none given; expired, not yet valid, signing time before the certificate; a different key's certificate with the same name
   and serial spliced in (valid self-signature, wrong key: refused); every third byte flipped (none accepted), truncations,
   trailing byte; 4,000 random multi-byte mutations of a valid message (none accepted) and 3,000 random junk inputs. Optional
   openssl cross-check (skipped without openssl): `openssl asn1parse` and `openssl cms -cmsout -print` read our output,
   `openssl cms -verify -noverify` accepts the encapsulated and detached forms and returns the content, refuses a changed
   detached content; and OUR verifier accepts a message that `openssl cms -sign` made with our key and certificate (so it
   copes with another writer's attribute set and algorithm identifiers). Nothing from OpenSSL is in the repo.
   UNVERIFIED and open: whether Windows wants NULL or absent digest-algorithm parameters, rsaEncryption or
   sha256WithRSAEncryption as the signature algorithm, which signed attributes, and which SignedData version for the APPX
   signature (the options exist so stage 5 can match a real reference package). Not done: no countersignature / timestamp,
   no unsigned attributes, no certificate chains (one self-signed certificate only), no multiple signers, no
   subjectKeyIdentifier signer id, no RSA-PSS, no enveloped or other CMS types. Checked with g++ 13 on Linux only.

   **What stage 5 needs from the owner's PC (a SignTool-signed reference package).** Stage 5 must start from facts, not from
   memory of the format. Please make one signed package on the Windows PC and send back only PUBLIC files. Never send a `.pfx`
   or any private key (the recipe below never exports one). Recipe (PowerShell, in a folder with a package folder `ref\`
   that holds a small `AppxManifest.xml` and one or two small files, or better, an unsigned `.msix` or `.appx` produced by our
   own packer; the manifest Publisher must be the subject used below):
   ```
   $c = New-SelfSignedCertificate -Type Custom -KeyUsage DigitalSignature -CertStoreLocation Cert:\CurrentUser\My -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3","2.5.29.19={text}") -Subject "CN=Okay Reference, O=Okay Space, C=US"
   makeappx pack /d ref /p ref_unsigned.msix
   Copy-Item ref_unsigned.msix ref_signed.msix
   signtool sign /fd SHA256 /sha1 $c.Thumbprint ref_signed.msix
   Export-Certificate -Cert $c -FilePath ref.cer
   Copy-Item ref_signed.msix ref_signed.zip; Expand-Archive ref_signed.zip -DestinationPath ref_signed_files
   Get-AuthenticodeSignature ref_signed.msix | Format-List * | Out-File ref_signature.txt
   certutil -dump ref.cer | Out-File ref_cer_dump.txt
   ```
   Send back: `ref_unsigned.msix`, `ref_signed.msix` (they hold only the test manifest and files), `ref.cer` (public
   certificate only), `ref_signature.txt`, `ref_cer_dump.txt`, and the Windows version and `signtool /?` header line
   (SDK version). From these I can: diff the signed against the unsigned package (what SignTool changes, where the
   `AppxSignature.p7x` entry sits in the ZIP, the central directory and ZIP flags), parse the `.p7x` (the `PKCX` header and the
   SignedData: version, digest algorithm parameters, signed attributes, the SPC content and its digest bytes), recompute the
   tagged hashes from the unsigned package and see which bytes each one covers (settling whether the manifest is hashed
   directly or only through `AppxBlockMap.xml`), and compare the certificate encoding (name order and string types) with ours.
   Optional but useful: the same steps with a SECOND, different package, to see which values change; and a package after
   `Add-AppxPackage` fails with our own signed one (the exact error text), once stage 5 exists.
5. The APPX signature: compute AXPC, AXCD, AXCT, AXBM from the packer's output, build `AppxSignature.p7x`, add it to the
   ZIP. This stage has to be done jointly with the owner of the packer branch, because the central directory hash
   depends on how the ZIP is written (see risks).
6. Verification on Windows (owner's PC, unverified from here): `Get-AuthenticodeSignature` on the package, import the
   `.cer` into Trusted People, `Add-AppxPackage`, and on an Xbox in Developer Mode. Also test the failure cases: wrong
   Publisher, untrusted certificate, a package changed after signing (must be rejected).

Off-by-default switch: CMake option `OKAY_OWN_SIGNING` (default OFF; releases refuse it ON the same way they refuse
`OKAY_OWN_CRYPTO`, see rule 18 and `OKAY_REQUIRE_SODIUM`), plus a run-time switch that must be given explicitly:
`OKAY_OWN_SIGNING=1` in the environment or `--sign-own-test` on the packer command line. How to tell it is on: the packer
prints `signing: own test signer (not independently reviewed)` and the startup stack line shows it.

Editor display (when it exists): the package panel shows one of
- `Unsigned (sign with SignTool)` (default; the packer writes an unsigned package and the owner signs it), or
- `Signed by our own test signer (not independently reviewed)` with the certificate subject, thumbprint and expiry, and a
  warning that this is for Developer Mode testing only.
It never shows a plain "Signed" or a padlock.

Rule 4a: the PR for each stage says how to try it. Stages 1 to 4 are tried with a command line test tool and
`docs/try_it.md` steps (run the test, run `openssl` on the sample output). Stage 5 and 6 need the packer, a shipped
sample package in `examples/`, and the PowerShell steps above. A stage that the owner cannot try says so at the top.

## 6. Risks, effort, unknowns

Risks:

- The APPX digest layout and the ZIP central directory hash are the fragile part. The signature must be computed over
  the exact bytes in the final ZIP, with the signature entry excluded in a precisely specified way. Getting one byte
  wrong gives a generic "signature is invalid" error with little help. Mitigation: reproduce the hashes of a real signed
  package (made once with SignTool on the owner's PC, used only as a comparison file, never committed) before trusting
  our own.
- Windows may demand fields we do not know (the exact EKU, opus info attribute, SIP GUID, signature algorithm OID).
  Each is a "verify" item above; the owner's PC is the only real oracle, so expect several round trips.
- Hand-written RSA and bignum code can be subtly wrong in ways tests do not show (carry bugs on rare inputs, bad prime
  generation). Mitigation: heavy random tests against a simple second implementation, plus OpenSSL cross-checks. That
  lowers risk; it is not a review.
- Misuse: someone treating this as production signing. Mitigation: off by default, loud labels, releases refuse it,
  no reuse in `okay::crypto` or the network layer.
- Test key handling: the private key file must be created with owner-only permissions, never committed or printed (rule 15).

Effort (rough, one engineer or agent): stage 1 about 2 days, stage 2 about 3 days, stage 3 about 2 days, stage 4 about 2 days,
stage 5 about 3 to 5 days plus Windows round trips (the main uncertainty), stage 6 a few rounds with the owner. Total
about 2.5 to 3.5 weeks of agent work if the packer's ZIP output is stable; more if Windows rejects the package for
reasons we cannot see.

Unknown today:

- Whether the manifest is signed through the block map only (our reading) or also directly. What public documentation says
  (read 2026-11-18, Microsoft Learn, "MakeAppx.exe" and "Create a certificate for package signing"): the block map is created
  by MakeAppx with a hash algorithm (SHA256 by default, SHA384 and SHA512 possible) and SignTool's file digest algorithm "must
  match the hash algorithm used to create the blockmap", which shows that the signature is tied to the block map. Those pages do
  NOT describe the layout of `AppxSignature.p7x`, the tagged hashes (`AXPC`, `AXCD`, `AXCT`, `AXBM`, `AXCI`) or whether the
  manifest is hashed separately. So the statement "the manifest is covered only through the block map hash" stays an unconfirmed
  reading (verify): it rests on the block map listing every file of the package, which I have not re-read in the OPC/APPX
  specification this session. Stage 5 must start by reading that specification, and by hashing a real SignTool-signed package.
- Exact `SpcSipInfo` contents and the SIP GUID; exact required signed attributes.
- Whether Windows accepts `rsaEncryption` or only `sha256WithRSAEncryption` as the signer algorithm OID.
- What the Windows trust check needs, per the same Microsoft page: the certificate Subject must match the manifest Publisher
  (their example: `Publisher="CN=Contoso Software, O=Contoso Corporation, C=US"`); a self-signed test certificate has
  KeyUsage DigitalSignature, EKU 1.3.6.1.5.5.7.3.3 (code signing) and an empty basicConstraints (end entity, not a CA); and to
  install a package signed with it the certificate must be imported into the Local Machine "Trusted People" store (their
  example does this with a PFX and `Import-PfxCertificate`; a DER `.cer` imported into the same store is expected to work too,
  unverified). Only users who trust the certificate can run the app. The page says nothing about the Xbox.
- Xbox Developer Mode: how a self-signed certificate is trusted there, and whether it is checked at all for
  sideloaded dev packages (it may use a different path than the PC).
- Whether the owner wants a PFX export at all.
- Whether a plain MSIX tool-free signature is rejected by newer Windows policies (Smart App Control, S mode).

If the verification step shows that Windows or the Xbox needs something we cannot satisfy honestly, we stop and say so;
the fallback stays "unsigned package, owner signs with SignTool".

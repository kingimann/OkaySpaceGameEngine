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
3. Self-signed X.509 v3 certificate (subject from the manifest Publisher, code signing EKU, key usage, validity);
   `.cer` output that `openssl x509` can read.
4. CMS SignedData builder (generic), then the Microsoft SPC indirect data content on top.
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

- Whether the manifest is signed through the block map only (our reading) or also directly.
- Exact `SpcSipInfo` contents and the SIP GUID; exact required signed attributes.
- Whether Windows accepts `rsaEncryption` or only `sha256WithRSAEncryption` as the signer algorithm OID.
- Xbox Developer Mode: how a self-signed certificate is trusted there, and whether it is checked at all for
  sideloaded dev packages (it may use a different path than the PC).
- Whether the owner wants a PFX export at all.
- Whether a plain MSIX tool-free signature is rejected by newer Windows policies (Smart App Control, S mode).

If the verification step shows that Windows or the Xbox needs something we cannot satisfy honestly, we stop and say so;
the fallback stays "unsigned package, owner signs with SignTool".

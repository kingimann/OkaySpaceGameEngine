#pragma once
// RSA key generation and RSASSA-PKCS1-v1_5 signatures with SHA-256, for our own test-key signing
// (docs/own_signing_plan.md, stage 2).
//
// NOT INDEPENDENTLY REVIEWED. TEST KEY ONLY. Never use this for anything that protects a real secret or a real user. It exists
// so our own packer can sign a Developer Mode test package with a throw-away self-signed key. The arithmetic is NOT constant
// time and signing is not blinded, so a local attacker who can time or probe it could recover the key. For a key that protects
// nothing that is accepted; it is the reason this must never become a real signing path. Off by default (OKAY_OWN_SIGNING).
//
// Written from RFC 8017 (PKCS #1 v2.2): section 3 (key shapes, CRT form), 5.2 and 5.1 (RSASP1 / RSAVP1), 8.2 (RSASSA-PKCS1-v1_5),
// 9.2 (EMSA-PKCS1-v1_5) and appendix A.1 (RSAPublicKey / RSAPrivateKey DER), and FIPS 186-5 appendix A (key pair generation:
// p and q of equal length, e = 65537, |p - q| large, d from the least common multiple). SHA-256 is the engine's one copy
// (okay/Crypto/Sha256.hpp); random numbers come only from okay::crypto::RandomBytes. No <mutex>/<thread>.
#include <cstddef>
#include <cstdint>
#include <vector>

#include "okay/Sign/BigInt.hpp"

namespace okay::sign {

struct RsaPublicKey {
    BigInt n, e;
};

struct RsaPrivateKey {
    BigInt n, e, d;        // d = e^-1 mod lcm(p-1, q-1)
    BigInt p, q;           // p > q
    BigInt dP, dQ, qInv;   // d mod (p-1), d mod (q-1), q^-1 mod p (RFC 8017 section 3.2)
    RsaPublicKey Public() const { return RsaPublicKey{n, e}; }
};

constexpr std::size_t kRsaMinBits = 512;   // smaller cannot hold a SHA-256 PKCS#1 v1.5 block (62 bytes needed); tests use 512
constexpr std::uint64_t kRsaPublicExponent = 65537;

/// New key with a modulus of exactly `bits` bits (even, at least kRsaMinBits), e = 65537. Both primes pass trial division and
/// `mrRounds` Miller-Rabin rounds. False for a bad size. Slow for big sizes (see docs/own_signing_plan.md for timings).
bool RsaGenerateKey(std::size_t bits, RsaPrivateKey& out, int mrRounds = 40);

/// Checks everything the key claims: n = p*q, p and q probable primes, e*d = 1 mod lcm(p-1,q-1), the three CRT values.
bool RsaCheckPrivateKey(const RsaPrivateKey& k);

/// EMSA-PKCS1-v1_5 with SHA-256 (RFC 8017 9.2): 00 01 FF..FF 00 DigestInfo, emLen bytes. Empty if emLen < 62.
std::vector<std::uint8_t> RsaEncodeEmsaSha256(const std::uint8_t digest[32], std::size_t emLen);

/// Signature of the SHA-256 digest (RSASSA-PKCS1-v1_5-SIGN). The signature has the length of the modulus in bytes. Signs by the
/// CRT route and then verifies its own result with e (so a computation error never leaves the program as a signature).
bool RsaSignDigestSha256(const RsaPrivateKey& key, const std::uint8_t digest[32], std::vector<std::uint8_t>& sig);
bool RsaSignSha256(const RsaPrivateKey& key, const std::uint8_t* msg, std::size_t n, std::vector<std::uint8_t>& sig);

/// RSASSA-PKCS1-v1_5-VERIFY: true only when the signature has exactly the modulus length, is below n, and the decoded block equals
/// the freshly encoded block byte for byte (RFC 8017 9.2 note: compare, do not parse).
bool RsaVerifyDigestSha256(const RsaPublicKey& key, const std::uint8_t digest[32], const std::uint8_t* sig, std::size_t sigLen);
bool RsaVerifySha256(const RsaPublicKey& key, const std::uint8_t* msg, std::size_t n, const std::uint8_t* sig, std::size_t sigLen);

/// PKCS #1 RSAPrivateKey / RSAPublicKey DER (RFC 8017 A.1.2, A.1.1). The private form holds the secret key: handle the bytes
/// like a password (owner-only file, never logged, never committed).
std::vector<std::uint8_t> RsaPrivateKeyToDer(const RsaPrivateKey& k);
std::vector<std::uint8_t> RsaPublicKeyToDer(const RsaPublicKey& k);
bool RsaPrivateKeyFromDer(const std::vector<std::uint8_t>& der, RsaPrivateKey& out);   // strict DER; does not check consistency
bool RsaPublicKeyFromDer(const std::vector<std::uint8_t>& der, RsaPublicKey& out);

}  // namespace okay::sign

#pragma once
// okay::crypto - the engine's OWN implementations of the crypto primitives the
// network layer uses (stage 1 of docs/own_code_roadmap.md).
//
//   *** UNREVIEWED. NOT THE DEFAULT. ***
//   This code was written from the RFCs by the engine team and has NOT been
//   reviewed by an outside cryptographer. It is built side by side with
//   libsodium and is only used when the build is configured with
//   -DOKAY_OWN_CRYPTO=ON. It must NOT become the default, and libsodium must
//   NOT be removed, before an outside expert has reviewed it.
//
// What is here (and only what the engine actually uses, plus its building blocks):
//   ChaCha20, HChaCha20, Poly1305            RFC 8439, draft-irtf-cfrg-xchacha
//   ChaCha20-Poly1305 AEAD (IETF)            RFC 8439
//   XChaCha20-Poly1305 AEAD (IETF)           draft-irtf-cfrg-xchacha (what SecureChannel seals with)
//   BLAKE2b                                  RFC 7693
//   SHA-256, SHA-512                         FIPS 180-4
//   X25519                                   RFC 7748
//   Ed25519 (pure, detached)                 RFC 8032
//   Kx session keys (X25519 + BLAKE2b-512)   same construction as libsodium's crypto_kx
//   RandomBytes (the OS CSPRNG), MemZero, ConstantTimeEqual
//
// Constant time by construction: no secret-dependent branches or table indexes;
// the field arithmetic uses fixed loops and masks, the X25519 and Ed25519 scalar
// multiplications are fixed-length ladders with masked selects. (This is what the
// code aims for; nobody has audited the generated machine code.)
//
// No <mutex>/<thread> here (MinGW rule). Functions never throw.
#include <cstddef>
#include <cstdint>

namespace okay::crypto {

// ---- sizes ------------------------------------------------------------------
constexpr std::size_t kChaChaKeyBytes = 32;
constexpr std::size_t kChaChaNonceBytes = 12;       // IETF ChaCha20 / ChaCha20-Poly1305
constexpr std::size_t kXChaChaNonceBytes = 24;      // XChaCha20-Poly1305
constexpr std::size_t kPoly1305KeyBytes = 32;
constexpr std::size_t kPoly1305TagBytes = 16;
constexpr std::size_t kAeadTagBytes = 16;
constexpr std::size_t kBlake2bMaxBytes = 64;
constexpr std::size_t kBlake2bMaxKeyBytes = 64;
constexpr std::size_t kSha256Bytes = 32;
constexpr std::size_t kSha512Bytes = 64;
constexpr std::size_t kX25519Bytes = 32;
constexpr std::size_t kKxPublicKeyBytes = 32;
constexpr std::size_t kKxSecretKeyBytes = 32;
constexpr std::size_t kKxSessionKeyBytes = 32;
constexpr std::size_t kEd25519PublicKeyBytes = 32;
constexpr std::size_t kEd25519SeedBytes = 32;
constexpr std::size_t kEd25519SecretKeyBytes = 64;  // seed || public key (libsodium's layout)
constexpr std::size_t kEd25519SignatureBytes = 64;

// ---- utilities ----------------------------------------------------------------
/// Fill `out` from the operating system's CSPRNG (getrandom / getentropy / BCryptGenRandom).
/// No custom generator. If the OS cannot supply bytes this aborts: continuing with
/// predictable "random" bytes would be worse than stopping.
void RandomBytes(void* out, std::size_t n);

/// Zero `p` in a way the optimiser may not remove.
void MemZero(void* p, std::size_t n);

/// True when the n bytes are equal. Time depends on n only, not on where they differ.
bool ConstantTimeEqual(const void* a, const void* b, std::size_t n);

// ---- ChaCha20 / Poly1305 (RFC 8439) -------------------------------------------
/// out = in XOR keystream (in-place allowed). 32-bit block counter, 96-bit nonce.
void ChaCha20Xor(std::uint8_t* out, const std::uint8_t* in, std::size_t n,
                 const std::uint8_t key[32], const std::uint8_t nonce[12], std::uint32_t counter);
/// HChaCha20 (the XChaCha20 subkey step): 32-byte key, 16-byte input -> 32-byte subkey.
void HChaCha20(std::uint8_t out[32], const std::uint8_t key[32], const std::uint8_t in[16]);

class Poly1305 {
public:
    explicit Poly1305(const std::uint8_t key[32]);
    ~Poly1305();
    void Update(const std::uint8_t* m, std::size_t n);
    void Final(std::uint8_t tag[16]);   // wipes the state
private:
    std::uint32_t m_r[5], m_h[5], m_pad[4];
    std::uint8_t m_buf[16];
    std::size_t m_fill;
    void Blocks(const std::uint8_t* m, std::size_t n, std::uint32_t hibit);
};
void Poly1305Mac(std::uint8_t tag[16], const std::uint8_t* m, std::size_t n, const std::uint8_t key[32]);

// ---- AEAD ---------------------------------------------------------------------
// Seal writes n + 16 bytes to `out` (ciphertext then tag); `out` may equal `in`.
// Open takes n + 16 bytes (`cn` includes the tag), returns false and zeroes `out`
// (cn - 16 bytes) when the tag does not verify. `aad` may be null when aadLen is 0.
void ChaCha20Poly1305Seal(std::uint8_t* out, const std::uint8_t* in, std::size_t n,
                          const std::uint8_t* aad, std::size_t aadLen,
                          const std::uint8_t nonce[12], const std::uint8_t key[32]);
bool ChaCha20Poly1305Open(std::uint8_t* out, const std::uint8_t* in, std::size_t cn,
                          const std::uint8_t* aad, std::size_t aadLen,
                          const std::uint8_t nonce[12], const std::uint8_t key[32]);
void XChaCha20Poly1305Seal(std::uint8_t* out, const std::uint8_t* in, std::size_t n,
                           const std::uint8_t* aad, std::size_t aadLen,
                           const std::uint8_t nonce[24], const std::uint8_t key[32]);
bool XChaCha20Poly1305Open(std::uint8_t* out, const std::uint8_t* in, std::size_t cn,
                           const std::uint8_t* aad, std::size_t aadLen,
                           const std::uint8_t nonce[24], const std::uint8_t key[32]);

// ---- hashes -------------------------------------------------------------------
/// BLAKE2b (RFC 7693), sequential mode, 1..64-byte output, optional key (0..64 bytes).
class Blake2b {
public:
    explicit Blake2b(std::size_t outLen = 64, const std::uint8_t* key = nullptr, std::size_t keyLen = 0);
    ~Blake2b();
    void Update(const std::uint8_t* m, std::size_t n);
    void Final(std::uint8_t* out);      // writes outLen bytes; wipes the state
private:
    std::uint64_t m_h[8], m_t[2];
    std::uint8_t m_buf[128];
    std::size_t m_fill, m_outLen;
    void Compress(bool last);
};
void Blake2bHash(std::uint8_t* out, std::size_t outLen, const std::uint8_t* m, std::size_t n,
                 const std::uint8_t* key = nullptr, std::size_t keyLen = 0);

class Sha256 {
public:
    Sha256();
    void Update(const std::uint8_t* m, std::size_t n);
    void Final(std::uint8_t out[32]);
private:
    std::uint32_t m_h[8];
    std::uint64_t m_len;
    std::uint8_t m_buf[64];
    std::size_t m_fill;
    void Block(const std::uint8_t* p);
};
void Sha256Hash(std::uint8_t out[32], const std::uint8_t* m, std::size_t n);

class Sha512 {
public:
    Sha512();
    void Update(const std::uint8_t* m, std::size_t n);
    void Final(std::uint8_t out[64]);
private:
    std::uint64_t m_h[8];
    std::uint64_t m_len;
    std::uint8_t m_buf[128];
    std::size_t m_fill;
    void Block(const std::uint8_t* p);
};
void Sha512Hash(std::uint8_t out[64], const std::uint8_t* m, std::size_t n);

// ---- X25519 (RFC 7748) --------------------------------------------------------
/// out = X25519(scalar, point). Returns false when the result is all zero (a
/// small-order peer point), as libsodium's crypto_scalarmult does; `out` is still written.
bool X25519(std::uint8_t out[32], const std::uint8_t scalar[32], const std::uint8_t point[32]);
/// out = X25519(scalar, 9) (the public key for a secret scalar).
void X25519Base(std::uint8_t out[32], const std::uint8_t scalar[32]);

// ---- Kx session keys ------------------------------------------------------------
// Same construction as libsodium's crypto_kx: q = X25519(my_sk, their_pk), then
// BLAKE2b-512(q || client_pk || server_pk) is split into rx || tx (client) or
// tx || rx (server). Returns false (keys zeroed) when q is all zero.
void KxKeypair(std::uint8_t pk[32], std::uint8_t sk[32]);
bool KxClientSessionKeys(std::uint8_t rx[32], std::uint8_t tx[32], const std::uint8_t clientPk[32],
                         const std::uint8_t clientSk[32], const std::uint8_t serverPk[32]);
bool KxServerSessionKeys(std::uint8_t rx[32], std::uint8_t tx[32], const std::uint8_t serverPk[32],
                         const std::uint8_t serverSk[32], const std::uint8_t clientPk[32]);

// ---- Ed25519 (RFC 8032, pure, detached signatures) -------------------------------
void Ed25519SeedKeypair(std::uint8_t pk[32], std::uint8_t sk[64], const std::uint8_t seed[32]);
void Ed25519Keypair(std::uint8_t pk[32], std::uint8_t sk[64]);   // random seed
void Ed25519PublicFromSeed(std::uint8_t pk[32], const std::uint8_t seed[32]);
/// Signs with the first 32 bytes of `sk` (the seed); the public key is recomputed, so a
/// secret key whose second half was tampered with can't leak the scalar.
void Ed25519Sign(std::uint8_t sig[64], const std::uint8_t* m, std::size_t n, const std::uint8_t sk[64]);
/// Strict, like libsodium: S must be < L, R and the public key must not be small-order,
/// and the public key must be a canonical encoding of a curve point.
bool Ed25519Verify(const std::uint8_t sig[64], const std::uint8_t* m, std::size_t n, const std::uint8_t pk[32]);
/// True for a public key no signature should ever be accepted under: a non-canonical encoding (y >= p) or a
/// point of small order (including the identity, which "verifies" R = identity, S = 0 for any message).
/// Exactly the key checks Ed25519Verify makes first; used to vet key lists before trusting them.
/// A 32-byte string that is not a curve point at all is NOT reported (it fails to verify anything anyway).
bool Ed25519IsWeakKey(const std::uint8_t pk[32]);

} // namespace okay::crypto

#pragma once
// The seam between the network layer and a crypto backend: the only place that
// knows whether libsodium or okay::crypto (our own, UNREVIEWED code, see
// okay/Crypto/Crypto.hpp) does the work. SecureChannel, SessionTicket and the relay
// call okay::net::netcrypto and nothing else.
//
//   OKAY_OWN_CRYPTO  -> okay::crypto   (CMake -DOKAY_OWN_CRYPTO=ON; never the default)
//   OKAY_HAVE_SODIUM -> libsodium      (the default when it is found)
//   neither          -> no encryption  (OKAY_NET_CRYPTO undefined)
#if defined(OKAY_OWN_CRYPTO) && !defined(OKAY_NET_CRYPTO)
#define OKAY_NET_CRYPTO
#endif
#if defined(OKAY_HAVE_SODIUM) && !defined(OKAY_NET_CRYPTO)
#define OKAY_NET_CRYPTO
#endif

#ifdef OKAY_NET_CRYPTO
#include <cstddef>
#include <cstdint>
#ifdef OKAY_OWN_CRYPTO
#include <okay/Crypto/Crypto.hpp>
#else
#include <sodium.h>
#endif

namespace okay::net::netcrypto {

constexpr std::size_t kKxPublicBytes = 32, kKxSecretBytes = 32, kKxSessionBytes = 32;
constexpr std::size_t kNonceBytes = 24, kTagBytes = 16;
constexpr std::size_t kSignPublicBytes = 32, kSignSecretBytes = 64, kSignBytes = 64, kSha256Bytes = 32;

inline const char* Name() {
#ifdef OKAY_OWN_CRYPTO
    return "okay::crypto (own, unreviewed)";
#else
    return "libsodium";
#endif
}

/// One-time init (true when the backend is usable).
inline bool Init() {
#ifdef OKAY_OWN_CRYPTO
    return true;
#else
    static const int s = sodium_init();   // 0 ok, 1 already initialised, -1 failed
    return s >= 0;
#endif
}

inline void Random(void* out, std::size_t n) {
#ifdef OKAY_OWN_CRYPTO
    crypto::RandomBytes(out, n);
#else
    randombytes_buf(out, n);
#endif
}

inline void KxKeypair(std::uint8_t* pk, std::uint8_t* sk) {
#ifdef OKAY_OWN_CRYPTO
    crypto::KxKeypair(pk, sk);
#else
    crypto_kx_keypair(pk, sk);
#endif
}
inline bool KxServer(std::uint8_t* rx, std::uint8_t* tx, const std::uint8_t* pk, const std::uint8_t* sk,
                     const std::uint8_t* peerPk) {
#ifdef OKAY_OWN_CRYPTO
    return crypto::KxServerSessionKeys(rx, tx, pk, sk, peerPk);
#else
    return crypto_kx_server_session_keys(rx, tx, pk, sk, peerPk) == 0;
#endif
}
inline bool KxClient(std::uint8_t* rx, std::uint8_t* tx, const std::uint8_t* pk, const std::uint8_t* sk,
                     const std::uint8_t* peerPk) {
#ifdef OKAY_OWN_CRYPTO
    return crypto::KxClientSessionKeys(rx, tx, pk, sk, peerPk);
#else
    return crypto_kx_client_session_keys(rx, tx, pk, sk, peerPk) == 0;
#endif
}

/// XChaCha20-Poly1305 (IETF). Seal writes n + 16 bytes to out; Open takes cn (with the tag)
/// and writes cn - 16 bytes, returning false when authentication fails.
inline void AeadSeal(std::uint8_t* out, const std::uint8_t* in, std::size_t n, const std::uint8_t* ad,
                     std::size_t adLen, const std::uint8_t* nonce, const std::uint8_t* key) {
#ifdef OKAY_OWN_CRYPTO
    crypto::XChaCha20Poly1305Seal(out, in, n, ad, adLen, nonce, key);
#else
    unsigned long long clen = 0;
    crypto_aead_xchacha20poly1305_ietf_encrypt(out, &clen, in, n, ad, adLen, nullptr, nonce, key);
#endif
}
inline bool AeadOpen(std::uint8_t* out, const std::uint8_t* in, std::size_t cn, const std::uint8_t* ad,
                     std::size_t adLen, const std::uint8_t* nonce, const std::uint8_t* key) {
#ifdef OKAY_OWN_CRYPTO
    return crypto::XChaCha20Poly1305Open(out, in, cn, ad, adLen, nonce, key);
#else
    unsigned long long mlen = 0;
    return crypto_aead_xchacha20poly1305_ietf_decrypt(out, &mlen, nullptr, in, cn, ad, adLen, nonce, key) == 0;
#endif
}

inline bool SignKeypair(std::uint8_t* pk, std::uint8_t* sk) {
#ifdef OKAY_OWN_CRYPTO
    crypto::Ed25519Keypair(pk, sk);
    return true;
#else
    return crypto_sign_keypair(pk, sk) == 0;
#endif
}
inline void Sign(std::uint8_t* sig, const std::uint8_t* m, std::size_t n, const std::uint8_t* sk) {
#ifdef OKAY_OWN_CRYPTO
    crypto::Ed25519Sign(sig, m, n, sk);
#else
    crypto_sign_detached(sig, nullptr, m, n, sk);
#endif
}
inline bool Verify(const std::uint8_t* sig, const std::uint8_t* m, std::size_t n, const std::uint8_t* pk) {
#ifdef OKAY_OWN_CRYPTO
    return crypto::Ed25519Verify(sig, m, n, pk);
#else
    return crypto_sign_verify_detached(sig, m, n, pk) == 0;
#endif
}
inline void Sha256(std::uint8_t* out, const std::uint8_t* m, std::size_t n) {
#ifdef OKAY_OWN_CRYPTO
    crypto::Sha256Hash(out, m, n);
#else
    crypto_hash_sha256(out, m, n);
#endif
}

} // namespace okay::net::netcrypto
#endif // OKAY_NET_CRYPTO

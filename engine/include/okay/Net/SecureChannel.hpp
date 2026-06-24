#pragma once
#include <cstdint>
#include <string>
#include <vector>

#ifdef OKAY_HAVE_SODIUM
#include <sodium.h>
#endif

namespace okay::net {

/// Authenticated encryption for the UDP transport, built on libsodium.
///
/// Each peer generates an X25519 key pair (PublicKey()) and exchanges public keys
/// during the join handshake. From the two public keys plus its own secret key,
/// each side derives a pair of symmetric session keys (one for each direction) with
/// crypto_kx — so traffic in each direction has its own key. Messages are then
/// sealed with XChaCha20-Poly1305 (crypto_aead): a random 24-byte nonce is prepended
/// to the ciphertext, and the Poly1305 tag authenticates it, so any tampering or
/// forgery is detected and rejected on open.
///
/// When the engine is built WITHOUT libsodium (OKAY_HAVE_SODIUM undefined), this
/// compiles to a stub: Available() is false and the seal/open calls fail, so the
/// transport simply runs unencrypted.
class SecureChannel {
public:
    static constexpr std::size_t kPublicKeyBytes = 32;

    /// True when the engine was built with libsodium, so encryption is possible.
    static bool Available() {
#ifdef OKAY_HAVE_SODIUM
        return true;
#else
        return false;
#endif
    }

    /// One-time libsodium init (safe to call repeatedly; returns false if unavailable).
    static bool Init() {
#ifdef OKAY_HAVE_SODIUM
        static int s = sodium_init();   // 0 = ok, 1 = already initialized, -1 = fail
        return s >= 0;
#else
        return false;
#endif
    }

    /// Generate this peer's ephemeral key pair. Call once before the handshake.
    void GenerateKeys() {
#ifdef OKAY_HAVE_SODIUM
        Init();
        crypto_kx_keypair(m_pk, m_sk);
        m_haveKeys = true;
#endif
    }

    /// This peer's public key to send to the other side (kPublicKeyBytes bytes).
    std::vector<std::uint8_t> PublicKey() const {
#ifdef OKAY_HAVE_SODIUM
        return std::vector<std::uint8_t>(m_pk, m_pk + crypto_kx_PUBLICKEYBYTES);
#else
        return {};
#endif
    }

    /// Derive the session keys from the peer's public key. `asServer` must differ on
    /// the two ends (the listening peer is the server). Returns false on failure
    /// (no libsodium, no local keys, or a malformed/short peer key).
    bool Establish(const std::vector<std::uint8_t>& peerPublicKey, bool asServer) {
#ifdef OKAY_HAVE_SODIUM
        if (!m_haveKeys || peerPublicKey.size() != crypto_kx_PUBLICKEYBYTES) return false;
        int rc = asServer
            ? crypto_kx_server_session_keys(m_rx, m_tx, m_pk, m_sk, peerPublicKey.data())
            : crypto_kx_client_session_keys(m_rx, m_tx, m_pk, m_sk, peerPublicKey.data());
        m_ready = (rc == 0);
        return m_ready;
#else
        (void)peerPublicKey; (void)asServer; return false;
#endif
    }

    /// True once session keys are derived and seal/open will work.
    bool Ready() const { return m_ready; }

    /// Encrypt+authenticate `plain` for the peer. Output is nonce || ciphertext.
    /// Returns false (leaving `out` empty) if the channel isn't ready.
    bool Seal(const std::uint8_t* plain, std::size_t len, std::vector<std::uint8_t>& out) const {
#ifdef OKAY_HAVE_SODIUM
        if (!m_ready) return false;
        const std::size_t N = crypto_aead_xchacha20poly1305_ietf_NPUBBYTES;
        out.resize(N + len + crypto_aead_xchacha20poly1305_ietf_ABYTES);
        randombytes_buf(out.data(), N);                  // fresh random nonce up front
        unsigned long long clen = 0;
        crypto_aead_xchacha20poly1305_ietf_encrypt(
            out.data() + N, &clen, plain, len, nullptr, 0, nullptr, out.data(), m_tx);
        out.resize(N + static_cast<std::size_t>(clen));
        return true;
#else
        (void)plain; (void)len; (void)out; return false;
#endif
    }

    /// Verify+decrypt a nonce||ciphertext blob from the peer into `out`. Returns false
    /// if the channel isn't ready, the blob is too short, or authentication fails
    /// (tampered/forged) — in which case the data must be dropped.
    bool Open(const std::uint8_t* data, std::size_t len, std::vector<std::uint8_t>& out) const {
#ifdef OKAY_HAVE_SODIUM
        if (!m_ready) return false;
        const std::size_t N = crypto_aead_xchacha20poly1305_ietf_NPUBBYTES;
        const std::size_t A = crypto_aead_xchacha20poly1305_ietf_ABYTES;
        if (len < N + A) return false;
        out.resize(len - N - A);
        unsigned long long mlen = 0;
        if (crypto_aead_xchacha20poly1305_ietf_decrypt(
                out.data(), &mlen, nullptr, data + N, len - N, nullptr, 0, data, m_rx) != 0)
            return false;                                // authentication failed -> reject
        out.resize(static_cast<std::size_t>(mlen));
        return true;
#else
        (void)data; (void)len; (void)out; return false;
#endif
    }

private:
#ifdef OKAY_HAVE_SODIUM
    unsigned char m_pk[crypto_kx_PUBLICKEYBYTES];
    unsigned char m_sk[crypto_kx_SECRETKEYBYTES];
    unsigned char m_rx[crypto_kx_SESSIONKEYBYTES];   // key for data we receive
    unsigned char m_tx[crypto_kx_SESSIONKEYBYTES];   // key for data we send
    bool m_haveKeys = false;
#endif
    bool m_ready = false;
};

} // namespace okay::net

#pragma once
#include <cstdint>
#include <cstring>
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
/// Replay protection (opt-in, SetSequenced): each sealed message then carries an
/// 8-byte sequence number inside the ciphertext, and Open refuses any number it
/// has already accepted or that is more than 64 behind the newest — so a captured
/// packet can't be played again. Both ends must turn it on (the anti-cheat
/// server does, through its Welcome); off, the format is exactly as before.
///
/// Associated data (opt-in, SetAssociatedData): bytes both ends know and that
/// travel outside the ciphertext (the envelope tag, the protocol version) are
/// authenticated with every message, so a packet sealed under a different
/// version or header doesn't open. Empty (the default) is the old format.
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

    bool HasKeys() const { return m_haveKeys; }

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

    /// Number messages and refuse replays (see the class comment).
    void SetSequenced(bool on) { m_sequenced = on; }
    bool Sequenced() const { return m_sequenced; }
    /// Messages Open refused as replays / too old.
    std::uint64_t Replays() const { return m_replays; }
    /// Authenticate these bytes with every message (see the class comment).
    void SetAssociatedData(std::vector<std::uint8_t> ad) { m_ad = std::move(ad); }
    const std::vector<std::uint8_t>& AssociatedData() const { return m_ad; }

    /// Encrypt+authenticate `plain` for the peer. Output is nonce || ciphertext.
    /// Returns false (leaving `out` empty) if the channel isn't ready.
    bool Seal(const std::uint8_t* plain, std::size_t len, std::vector<std::uint8_t>& out) const {
#ifdef OKAY_HAVE_SODIUM
        if (!m_ready) return false;
        const std::size_t N = crypto_aead_xchacha20poly1305_ietf_NPUBBYTES;
        std::vector<std::uint8_t> numbered;
        if (m_sequenced) {   // seq (little-endian u64) || plain, all inside the ciphertext
            const std::uint64_t seq = ++m_txSeq;
            numbered.resize(8 + len);
            for (int i = 0; i < 8; ++i) numbered[i] = static_cast<std::uint8_t>(seq >> (8 * i));
            if (len) std::memcpy(numbered.data() + 8, plain, len);
            plain = numbered.data(); len = numbered.size();
        }
        out.resize(N + len + crypto_aead_xchacha20poly1305_ietf_ABYTES);
        randombytes_buf(out.data(), N);                  // fresh random nonce up front
        unsigned long long clen = 0;
        crypto_aead_xchacha20poly1305_ietf_encrypt(
            out.data() + N, &clen, plain, len, m_ad.empty() ? nullptr : m_ad.data(),
            static_cast<unsigned long long>(m_ad.size()), nullptr, out.data(), m_tx);
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
                out.data(), &mlen, nullptr, data + N, len - N, m_ad.empty() ? nullptr : m_ad.data(),
                static_cast<unsigned long long>(m_ad.size()), data, m_rx) != 0)
            return false;                                // authentication failed -> reject
        out.resize(static_cast<std::size_t>(mlen));
        if (m_sequenced) {
            if (out.size() < 8) return false;
            std::uint64_t seq = 0;
            for (int i = 0; i < 8; ++i) seq |= static_cast<std::uint64_t>(out[i]) << (8 * i);
            if (!AcceptSeq(seq)) { ++m_replays; out.clear(); return false; }
            out.erase(out.begin(), out.begin() + 8);
        }
        return true;
#else
        (void)data; (void)len; (void)out; return false;
#endif
    }

private:
    // Sliding window over the last 64 sequence numbers: bit i of m_window = we
    // accepted (m_rxMax - i). Seq 0 is never sent.
    bool AcceptSeq(std::uint64_t seq) const {
        if (seq == 0) return false;
        if (seq > m_rxMax) {
            const std::uint64_t shift = seq - m_rxMax;
            m_window = shift >= 64 ? 0 : (m_window << shift);
            m_window |= 1;
            m_rxMax = seq;
            return true;
        }
        const std::uint64_t back = m_rxMax - seq;
        if (back >= 64) return false;                    // too old to tell: refuse
        const std::uint64_t bit = std::uint64_t(1) << back;
        if (m_window & bit) return false;                // seen it: a replay
        m_window |= bit;
        return true;
    }
    bool m_sequenced = false;
    std::vector<std::uint8_t> m_ad;
    mutable std::uint64_t m_txSeq = 0;
    mutable std::uint64_t m_rxMax = 0, m_window = 0, m_replays = 0;
#ifdef OKAY_HAVE_SODIUM
    unsigned char m_pk[crypto_kx_PUBLICKEYBYTES];
    unsigned char m_sk[crypto_kx_SECRETKEYBYTES];
    unsigned char m_rx[crypto_kx_SESSIONKEYBYTES];   // key for data we receive
    unsigned char m_tx[crypto_kx_SESSIONKEYBYTES];   // key for data we send
#endif
    bool m_haveKeys = false;   // (never set without libsodium)
    bool m_ready = false;
};

} // namespace okay::net

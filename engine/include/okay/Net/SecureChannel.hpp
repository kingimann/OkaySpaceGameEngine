#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <okay/Net/NetCrypto.hpp>   // OKAY_NET_CRYPTO: libsodium, or (opt-in) okay::crypto

namespace okay::net {

/// Authenticated encryption for the UDP transport, built on libsodium (default) or,
/// when the engine is configured with OKAY_OWN_CRYPTO=ON, on our own UNREVIEWED
/// okay::crypto (same wire format either way: a sealed message from one opens on the other).
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
/// When the engine is built with no crypto backend (OKAY_NET_CRYPTO undefined), this
/// compiles to a stub: Available() is false and the seal/open calls fail, so the
/// transport simply runs unencrypted.
class SecureChannel {
public:
    static constexpr std::size_t kPublicKeyBytes = 32;

    /// True when the engine was built with libsodium, so encryption is possible.
    static bool Available() {
#ifdef OKAY_NET_CRYPTO
        return true;
#else
        return false;
#endif
    }

    /// One-time libsodium init (safe to call repeatedly; returns false if unavailable).
    static bool Init() {
#ifdef OKAY_NET_CRYPTO
        static const bool s = netcrypto::Init();
        return s;
#else
        return false;
#endif
    }

    /// Generate this peer's ephemeral key pair. Call once before the handshake.
    void GenerateKeys() {
#ifdef OKAY_NET_CRYPTO
        Init();
        netcrypto::KxKeypair(m_pk, m_sk);
        m_haveKeys = true;
#endif
    }

    bool HasKeys() const { return m_haveKeys; }

    /// This peer's public key to send to the other side (kPublicKeyBytes bytes).
    std::vector<std::uint8_t> PublicKey() const {
#ifdef OKAY_NET_CRYPTO
        return std::vector<std::uint8_t>(m_pk, m_pk + netcrypto::kKxPublicBytes);
#else
        return {};
#endif
    }

    /// Derive the session keys from the peer's public key. `asServer` must differ on
    /// the two ends (the listening peer is the server). Returns false on failure
    /// (no libsodium, no local keys, or a malformed/short peer key).
    bool Establish(const std::vector<std::uint8_t>& peerPublicKey, bool asServer) {
#ifdef OKAY_NET_CRYPTO
        if (!m_haveKeys || peerPublicKey.size() != netcrypto::kKxPublicBytes) return false;
        m_ready = asServer
            ? netcrypto::KxServer(m_rx, m_tx, m_pk, m_sk, peerPublicKey.data())
            : netcrypto::KxClient(m_rx, m_tx, m_pk, m_sk, peerPublicKey.data());
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
#ifdef OKAY_NET_CRYPTO
        if (!m_ready) return false;
        const std::size_t N = netcrypto::kNonceBytes;
        std::vector<std::uint8_t> numbered;
        if (m_sequenced) {   // seq (little-endian u64) || plain, all inside the ciphertext
            const std::uint64_t seq = ++m_txSeq;
            numbered.resize(8 + len);
            for (int i = 0; i < 8; ++i) numbered[i] = static_cast<std::uint8_t>(seq >> (8 * i));
            if (len) std::memcpy(numbered.data() + 8, plain, len);
            plain = numbered.data(); len = numbered.size();
        }
        out.resize(N + len + netcrypto::kTagBytes);
        netcrypto::Random(out.data(), N);                // fresh random nonce up front
        netcrypto::AeadSeal(out.data() + N, plain, len, m_ad.empty() ? nullptr : m_ad.data(),
                            m_ad.size(), out.data(), m_tx);
        return true;
#else
        (void)plain; (void)len; (void)out; return false;
#endif
    }

    /// Verify+decrypt a nonce||ciphertext blob from the peer into `out`. Returns false
    /// if the channel isn't ready, the blob is too short, or authentication fails
    /// (tampered/forged) — in which case the data must be dropped.
    bool Open(const std::uint8_t* data, std::size_t len, std::vector<std::uint8_t>& out) const {
#ifdef OKAY_NET_CRYPTO
        if (!m_ready) return false;
        const std::size_t N = netcrypto::kNonceBytes;
        const std::size_t A = netcrypto::kTagBytes;
        if (len < N + A) return false;
        out.resize(len - N - A);
        if (!netcrypto::AeadOpen(out.data(), data + N, len - N, m_ad.empty() ? nullptr : m_ad.data(),
                                 m_ad.size(), data, m_rx))
            return false;                                // authentication failed -> reject
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
#ifdef OKAY_NET_CRYPTO
    unsigned char m_pk[netcrypto::kKxPublicBytes];
    unsigned char m_sk[netcrypto::kKxSecretBytes];
    unsigned char m_rx[netcrypto::kKxSessionBytes];   // key for data we receive
    unsigned char m_tx[netcrypto::kKxSessionBytes];   // key for data we send
#endif
    bool m_haveKeys = false;   // (never set without libsodium)
    bool m_ready = false;
};

} // namespace okay::net

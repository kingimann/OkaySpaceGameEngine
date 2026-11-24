#pragma once
#include "okay/Net/RelayProtocol.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace okay::net {

/// The one place a relay hello is built and a relay welcome is checked, shared
/// by the engine's NetworkManager and the launcher's voice calls (which have no
/// NetworkManager), so every relay client speaks the current protocol version:
/// the address cookie (version 3) and the welcome nonce (version 4). A client
/// built on these never sends a legacy (version 1/2) hello, which is what lets
/// a relay turn `--legacy-hello` off (docs/online/relay.md). Plain bytes, no Packet:
/// the layout is the little-endian one `Packet` writes.
struct RelayHelloParams {
    RelayRole role = RelayClient;
    std::string code;
    std::uint8_t version = kRelayProtocolVersion;
    std::uint64_t hostKey = 0;                          ///< 0 until the relay gave one
    std::array<std::uint8_t, kRelayCookieBytes> cookie{}; ///< zero until the relay sent one
    std::uint64_t nonce = 0;                            ///< echoed by every welcome (version 4)
    std::uint8_t flags = 0;                             ///< RelayHelloFlag bits (version 5)
};

namespace detail {
inline void PutU32LE(std::vector<std::uint8_t>& v, std::uint32_t x) {
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<std::uint8_t>(x >> (i * 8)));
}
inline std::uint32_t GetU32LE(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
           static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}
} // namespace detail

/// A hello datagram: [tag][role][u32 length + code][version][u64 key][cookie]
/// [u64 nonce][u8 flags] (flags from version 5), zero-padded to kRelayHelloMinBytes.
inline std::vector<std::uint8_t> BuildRelayHello(const RelayHelloParams& h) {
    std::vector<std::uint8_t> pk;
    pk.push_back(RelayHello);
    pk.push_back(static_cast<std::uint8_t>(h.role));
    detail::PutU32LE(pk, static_cast<std::uint32_t>(h.code.size()));
    pk.insert(pk.end(), h.code.begin(), h.code.end());
    pk.push_back(h.version);
    detail::PutU32LE(pk, static_cast<std::uint32_t>(h.hostKey));
    detail::PutU32LE(pk, static_cast<std::uint32_t>(h.hostKey >> 32));
    pk.insert(pk.end(), h.cookie.begin(), h.cookie.end());
    detail::PutU32LE(pk, static_cast<std::uint32_t>(h.nonce));
    detail::PutU32LE(pk, static_cast<std::uint32_t>(h.nonce >> 32));
    if (h.version >= 5) pk.push_back(h.flags);
    while (pk.size() < kRelayHelloMinBytes) pk.push_back(0);
    return pk;
}

/// What a welcome datagram says, once checked against the hello's nonce.
struct RelayWelcomeInfo {
    std::uint32_t slot = 0, hostSlot = 0;
    std::uint64_t hostKey = 0;
    bool haveKey = false;    ///< the relay is version 2 or newer
    bool haveNonce = false;  ///< the relay is version 4 or newer and echoed a nonce
    std::uint64_t successorKey = 0;   ///< version 5, host only: what lets a player take over (host succession)
};

/// Parse a welcome. Anyone can forge a UDP source address, so a welcome counts
/// only if it proves the sender saw our hello by echoing `nonce`: a wrong echo
/// is never accepted, a missing one only from an older relay and only when
/// `allowUnverified` is set. Returns false (and leaves `out` unset) otherwise.
inline bool ParseRelayWelcome(const std::uint8_t* d, std::size_t n, std::uint64_t nonce,
                              bool allowUnverified, RelayWelcomeInfo& out) {
    // [tag][u32 slot][u32 hostSlot] (9), [u8 version][u32 keyLo][u32 keyHi] (9), [u64 nonce] (8),
    // [u64 successor key] (8, version 5, to the host)
    if (n < 9 || d[0] != RelayWelcome) return false;
    RelayWelcomeInfo w;
    w.slot = detail::GetU32LE(d + 1);
    w.hostSlot = detail::GetU32LE(d + 5);
    if (n >= 18) {
        w.haveKey = true;
        w.hostKey = static_cast<std::uint64_t>(detail::GetU32LE(d + 10)) |
                    (static_cast<std::uint64_t>(detail::GetU32LE(d + 14)) << 32);
    }
    if (w.haveKey && n >= 26) {
        const std::uint64_t echoed = static_cast<std::uint64_t>(detail::GetU32LE(d + 18)) |
                                     (static_cast<std::uint64_t>(detail::GetU32LE(d + 22)) << 32);
        if (echoed != nonce) return false;
        w.haveNonce = true;
        if (n >= 34)
            w.successorKey = static_cast<std::uint64_t>(detail::GetU32LE(d + 26)) |
                             (static_cast<std::uint64_t>(detail::GetU32LE(d + 30)) << 32);
    } else if (!allowUnverified) {
        return false;
    }
    out = w;
    return true;
}

/// A RelayPeerAddr frame, once checked against the hello's nonce (a forged one is dropped).
struct RelayPeerAddrInfo {
    std::uint32_t slot = 0;
    std::uint32_t address = 0;
    std::uint16_t port = 0;
    std::uint64_t token = 0;
};
inline bool ParseRelayPeerAddr(const std::uint8_t* d, std::size_t n, std::uint64_t nonce, RelayPeerAddrInfo& out) {
    if (n < kRelayPeerAddrBytes || d[0] != RelayPeerAddr || nonce == 0) return false;
    const std::uint64_t echoed = static_cast<std::uint64_t>(detail::GetU32LE(d + 19)) |
                                 (static_cast<std::uint64_t>(detail::GetU32LE(d + 23)) << 32);
    if (echoed != nonce) return false;
    RelayPeerAddrInfo a;
    a.slot = detail::GetU32LE(d + 1);
    a.address = detail::GetU32LE(d + 5);
    a.port = static_cast<std::uint16_t>(d[9] | (d[10] << 8));
    a.token = static_cast<std::uint64_t>(detail::GetU32LE(d + 11)) | (static_cast<std::uint64_t>(detail::GetU32LE(d + 15)) << 32);
    if (a.slot == 0 || a.address == 0 || a.port == 0 || a.token == 0) return false;
    out = a;
    return true;
}

/// A punch datagram (peer to peer): [tag][kind][u64 token][u32 sender's slot].
inline std::vector<std::uint8_t> BuildRelayPunch(std::uint8_t kind, std::uint64_t token, std::uint32_t senderSlot) {
    std::vector<std::uint8_t> pk;
    pk.push_back(RelayPunch);
    pk.push_back(kind);
    detail::PutU32LE(pk, static_cast<std::uint32_t>(token));
    detail::PutU32LE(pk, static_cast<std::uint32_t>(token >> 32));
    detail::PutU32LE(pk, senderSlot);
    return pk;
}

} // namespace okay::net

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
/// a relay turn `--legacy-hello` off (docs/relay.md). Plain bytes, no Packet:
/// the layout is the little-endian one `Packet` writes.
struct RelayHelloParams {
    RelayRole role = RelayClient;
    std::string code;
    std::uint8_t version = kRelayProtocolVersion;
    std::uint64_t hostKey = 0;                          ///< 0 until the relay gave one
    std::array<std::uint8_t, kRelayCookieBytes> cookie{}; ///< zero until the relay sent one
    std::uint64_t nonce = 0;                            ///< echoed by every welcome (version 4)
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
/// [u64 nonce], zero-padded to kRelayHelloMinBytes.
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
    while (pk.size() < kRelayHelloMinBytes) pk.push_back(0);
    return pk;
}

/// What a welcome datagram says, once checked against the hello's nonce.
struct RelayWelcomeInfo {
    std::uint32_t slot = 0, hostSlot = 0;
    std::uint64_t hostKey = 0;
    bool haveKey = false;    ///< the relay is version 2 or newer
    bool haveNonce = false;  ///< the relay is version 4 or newer and echoed a nonce
};

/// Parse a welcome. Anyone can forge a UDP source address, so a welcome counts
/// only if it proves the sender saw our hello by echoing `nonce`: a wrong echo
/// is never accepted, a missing one only from an older relay and only when
/// `allowUnverified` is set. Returns false (and leaves `out` unset) otherwise.
inline bool ParseRelayWelcome(const std::uint8_t* d, std::size_t n, std::uint64_t nonce,
                              bool allowUnverified, RelayWelcomeInfo& out) {
    // [tag][u32 slot][u32 hostSlot] (9), [u8 version][u32 keyLo][u32 keyHi] (9), [u64 nonce] (8)
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
    } else if (!allowUnverified) {
        return false;
    }
    out = w;
    return true;
}

} // namespace okay::net

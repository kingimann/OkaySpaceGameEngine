#pragma once
#include <cstdint>

namespace okay::net {

/// Relay (TURN-style) framing that lets two peers behind NATs reach each other
/// without either one accepting an inbound connection. Both peers connect *out*
/// to a shared relay; the relay forwards datagrams between them by a small
/// per-session "slot" id. These tags are the OUTER envelope on the peer<->relay
/// wire only — the relay strips its header before the game's own message bytes
/// are seen, so they never collide with the inner Msg types.
enum RelayTag : std::uint8_t {
    RelayHello   = 0x90, // peer->relay: [u8 role][string code][u8 version][u32 keyLo][u32 keyHi]
                         //   (role: 0=host,1=client; version and hostKey are absent
                         //   from version-1 peers, and hostKey is 0 until the relay
                         //   gave one)
    RelayWelcome = 0x91, // relay->peer: [u32 yourSlot][u32 hostSlot][u8 version][u32 keyLo][u32 keyHi]
                         //   (hostKey only to the host, else 0; hostSlot 0 tells a
                         //   client its host has gone)
    RelayData    = 0x92, // peer->relay: [u32 destSlot][payload]; relay->peer: [u32 srcSlot][payload]
    RelayBye     = 0x93, // peer->relay: leaving (free my slot)
    RelayRefused = 0x94, // relay->peer: [u8 RelayRefusal][u8 relay version]
};

/// Why the relay turned a hello away (RelayRefused).
enum RelayRefusal : std::uint8_t {
    RelayRefusedVersion = 1,   ///< the peer's relay protocol version is too old
    RelayRefusedHosted  = 2,   ///< the code already has a host, and the hello lacked its key
};

/// The relay protocol version this build speaks. Version 1 had no version
/// byte, no host key and no RelayRefused; version 2 adds them.
inline constexpr std::uint8_t kRelayProtocolVersion = 2;
/// The oldest version the relay still accepts. Version-1 peers still work:
/// the relay applies its forwarding rules and caps to them too, but a
/// version-1 host can't move to a new address.
inline constexpr std::uint8_t kRelayMinProtocolVersion = 1;

/// Roles a peer announces in RelayHello. The first host to register a code owns
/// the session and is given its host key; everyone else joins as a client and
/// is wired to that host. While the session lives, another host hello for the
/// code is refused unless it carries that key.
enum RelayRole : std::uint8_t { RelayHost = 0, RelayClient = 1 };

/// Synthetic Endpoint port marker: in relay mode a peer is addressed by its slot
/// id carried in Endpoint::address, with this constant in the port so the value
/// can never be mistaken for a real resolved address.
inline constexpr std::uint16_t kRelaySlotPort = 0xFADE;

} // namespace okay::net

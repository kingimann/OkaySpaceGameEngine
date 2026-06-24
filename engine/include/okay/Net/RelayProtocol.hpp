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
    RelayHello   = 0x90, // peer->relay: [u8 role][string code]  (role: 0=host,1=client)
    RelayWelcome = 0x91, // relay->peer: [u32 yourSlot][u32 hostSlot]
    RelayData    = 0x92, // peer->relay: [u32 destSlot][payload]; relay->peer: [u32 srcSlot][payload]
    RelayBye     = 0x93, // peer->relay: leaving (free my slot)
};

/// Roles a peer announces in RelayHello. The first host to register a code owns
/// the session; everyone else joins as a client and is wired to that host.
enum RelayRole : std::uint8_t { RelayHost = 0, RelayClient = 1 };

/// Synthetic Endpoint port marker: in relay mode a peer is addressed by its slot
/// id carried in Endpoint::address, with this constant in the port so the value
/// can never be mistaken for a real resolved address.
inline constexpr std::uint16_t kRelaySlotPort = 0xFADE;

} // namespace okay::net

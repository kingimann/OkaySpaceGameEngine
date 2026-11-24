#pragma once
#include <cstddef>
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
                         //   [16-byte cookie][u32 nonceLo][u32 nonceHi] (version 4),
                         //   zero-padded to kRelayHelloMinBytes
                         //   (role: 0=host,1=client; version and hostKey are absent
                         //   from version-1 peers, the cookie from version 1 and 2;
                         //   hostKey is 0 until the relay gave one, the cookie zero
                         //   until it sent one)
    RelayWelcome = 0x91, // relay->peer: [u32 yourSlot][u32 hostSlot][u8 version][u32 keyLo][u32 keyHi]
                         //   [u32 nonceLo][u32 nonceHi] (version 4, only when the hello had a nonce)
                         //   (hostKey only to the host, else 0; hostSlot 0 tells a
                         //   client its host has gone)
    RelayData    = 0x92, // peer->relay: [u32 destSlot][payload]; relay->peer: [u32 srcSlot][payload]
    RelayBye     = 0x93, // peer->relay: leaving (free my slot)
    RelayRefused = 0x94, // relay->peer: [u8 RelayRefusal][u8 relay version]
    RelayCookie  = 0x95, // relay->peer: [u8 relay version][16-byte cookie]: send the
                         //   hello again with it (proves the address is really yours)
    RelayPeerAddr = 0x96, // relay->peer (version 5): [u32 peerSlot][u32 address][u16 port][u32 tokenLo]
                          //   [u32 tokenHi][u32 nonceLo][u32 nonceHi]: the public address the relay sees
                          //   for your session partner, so the two can try a direct path (NAT hole
                          //   punching). Sent only when BOTH sides set RelayHelloDirect, only between a
                          //   session's host and one of its clients, and it echoes the hello nonce.
    RelayPunch    = 0x98, // peer<->peer, straight to the address RelayPeerAddr gave (never via the relay):
                          //   [u8 kind: 0 probe, 1 answer][u32 tokenLo][u32 tokenHi][u32 sender's slot]
};

/// Hello flags (version 5, one byte after the nonce).
enum RelayHelloFlag : std::uint8_t {
    /// "You may tell my session partner my public address so we can try a direct path." A host sets it
    /// while it allows direct paths; a client sets it while it still wants its host's address. The relay
    /// exchanges addresses only when both have it set (NetworkManager::directConnect).
    RelayHelloDirect = 1,
};
/// Size of a RelayPunch datagram and of a RelayPeerAddr frame.
inline constexpr std::size_t kRelayPunchBytes = 1 + 1 + 8 + 4;
inline constexpr std::size_t kRelayPeerAddrBytes = 1 + 4 + 4 + 2 + 8 + 8;

/// Why the relay turned a hello away (RelayRefused).
enum RelayRefusal : std::uint8_t {
    RelayRefusedVersion = 1,   ///< the peer's relay protocol version is too old
    RelayRefusedHosted  = 2,   ///< the code already has a host, and the hello lacked its key
};

/// The relay protocol version this build speaks. Version 1 had no version
/// byte, no host key and no RelayRefused; version 2 adds them. Version 3 adds
/// the hello cookie: a new peer's first hello gets a RelayCookie, and only a
/// hello that echoes it gets a slot, so a spoofed source address gets nothing.
/// Version 4 adds the hello nonce: the peer picks a random 64-bit value, the
/// relay echoes it in every welcome for that peer, and the peer drops a welcome
/// that doesn't carry it, so a forged welcome can't pair, redirect or end a
/// session. Older relays ignore the nonce; older peers get the old welcome.
/// Version 5 adds direct paths and host succession: a hello flag byte after the
/// nonce (RelayHelloDirect), RelayPeerAddr, and a successor key in the host's
/// welcome (after the nonce). The host shares the successor key with its players
/// (inside the game's own, encrypted, traffic); a client of the session that
/// presents it in a host-role hello becomes the session's host, but only while the
/// host is gone or has been silent for kRelaySuccessionQuietSeconds, so no player
/// can take a live host's place. Both keys are made new when that happens.
inline constexpr std::uint8_t kRelayProtocolVersion = 5;
/// How long a session's host must have been silent before a successor may take over.
inline constexpr float kRelaySuccessionQuietSeconds = 1.5f;
inline constexpr std::size_t kRelayCookieBytes = 16;
/// A version-3 hello is padded to at least this, so the relay never answers
/// with more bytes than it was sent (a welcome is 26, a cookie 18); shorter
/// version-3 hellos are ignored.
inline constexpr std::size_t kRelayHelloMinBytes = 32;
/// The oldest version the relay still accepts. Version-1 peers still work:
/// the relay applies its forwarding rules and caps to them too, but a
/// version-1 host can't move to a new address.
inline constexpr std::uint8_t kRelayMinProtocolVersion = 1;

/// Roles a peer announces in RelayHello. The first host to register a code owns
/// the session and is given its host key; everyone else joins as a client and
/// is wired to that host. While the session lives, another host hello for the
/// code is refused unless it carries that key.
enum RelayRole : std::uint8_t { RelayHost = 0, RelayClient = 1, RelayHub = 2 };
/// RelayHub (version 3, the launcher's voice calls): every member announces it.
/// The first one in hosts, as with RelayHost, but the session is never
/// protected by a host key: when the hub leaves, the next hub hello takes over
/// (version 1's hand-over, now behind a cookie). A hub hello can't take a
/// protected game session, and a game host can't join a hub session that has a hub.

/// Synthetic Endpoint port marker: in relay mode a peer is addressed by its slot
/// id carried in Endpoint::address, with this constant in the port so the value
/// can never be mistaken for a real resolved address.
inline constexpr std::uint16_t kRelaySlotPort = 0xFADE;

} // namespace okay::net

#pragma once
// Network simulator and a per-object traffic counter.
//
// The simulator sits on NetworkManager's socket send/receive. Each datagram
// gets the latency, jitter and loss once: on the way out, or, when it goes to
// another socket of this program that reads through the simulator, when that
// one reads it (so two managers in one process see the setting, not twice it).
// With it off
// (the default) those paths call the socket directly. Latency, jitter, loss
// and duplication apply only while `enabled` is set, so movement, lag
// compensation and anti-cheat keep their current packets until a session
// turns the simulator on.
//
// Traffic counts logical messages by object id (a synced transform's id, an
// RPC name) and raw socket bytes. It does not change what is sent.
#include "okay/Net/UdpSocket.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace okay::net {

struct SimSettings {
    bool  enabled = false;
    float latencyMs = 80.0f;   ///< one-way delay added while enabled
    float jitterMs  = 10.0f;   ///< random extra, in both directions
    float loss      = 0.0f;    ///< 0..1 chance to drop a datagram
    float duplicate = 0.0f;    ///< 0..1 chance to send a second copy
};

SimSettings& Simulator();

// ---- NAT model -----------------------------------------------------------------------------------------
// A home router in front of one socket, for loopback tests of NAT traversal. Every datagram the socket
// sends leaves from a mapping: a real socket of this program on its own port, which is the "public
// address" everyone else (the relay, the other player) sees. Datagrams that arrive on a mapping are let in
// only as the filter allows; anything sent to the socket's own (private) port is dropped, as on a real
// router. Written from the NAT behaviour classes in RFC 4787 (mapping and filtering), no outside code.
enum class NatType : std::uint8_t {
    None = 0,         ///< no router (the default)
    FullCone,         ///< one mapping for every destination; anyone may send to it
    PortRestricted,   ///< one mapping for every destination; only from an address and port it sent to
                      ///< (the strictest "cone" kind: hole punching still works)
    Symmetric,        ///< a new mapping (a new public port) for every destination, each only answering
                      ///< that destination (hole punching between two of these fails)
};
/// Put `inside` behind a router of this kind (None takes it away). Its old mappings go.
void SetSimulatedNat(const UdpSocket& inside, NatType type);
NatType SimulatedNat(const UdpSocket& inside);
/// The router makes new mappings (a NAT rebinding): the public ports change and the old ones lead nowhere.
void RebindSimulatedNat(const UdpSocket& inside);
/// The public port `inside` reaches `to` from (0 while it has sent nothing there).
std::uint16_t SimulatedNatPort(const UdpSocket& inside, const Endpoint& to);
/// Datagrams the routers turned away (unsolicited, or sent to a private address).
std::size_t SimulatedNatDrops();
/// Take every socket out from behind its router.
void ClearSimulatedNats();
/// Send through `socket`'s router when it has one (true), else do nothing (false). Raw sends that skip the
/// latency and loss knobs (relay hellos) still go out through the router this way.
bool SendThroughNat(const UdpSocket& socket, const Endpoint& to, const void* data, std::size_t size);

/// True when enabled and at least one knob would change a datagram.
bool SimulatorActive();

/// Datagrams dropped because a socket had too many waiting (not simulated loss).
std::size_t SimulatorOverflows();

/// Drop any datagrams still waiting. Settings stay as they are.
void ResetSimulator();

/// Drop the datagrams still waiting for `socket` only (another manager's stay).
void ResetSimulator(const UdpSocket& socket);

/// Send due outbound datagrams. Call once a frame.
void PumpSimulator(UdpSocket& socket);

/// When the simulator is active, take the datagram (send later, send now, or
/// drop it) and return true. Otherwise return false and send it yourself.
bool HoldDatagram(UdpSocket& socket, const Endpoint& to, const void* data, std::size_t size);

/// A datagram for the game to read, or the same codes as UdpSocket::RecvFrom
/// (0 = nothing ready, <0 = error). While the simulator is active this drains
/// the socket into the delay queue and only returns a datagram whose delay
/// has elapsed.
int TakeDatagram(UdpSocket& socket, void* buffer, std::size_t capacity, Endpoint& from);

/// Row name for a custom message: the synced object id when the channel
/// carries one, otherwise the channel (RPC name for "__rpc/...").
std::string ObjectKey(const std::string& channel, const std::string& data);

void NoteObject(const std::string& name, std::size_t bytes, bool inbound);
void NoteWire(std::size_t bytes, bool inbound);
void ProfileAdvance(float dt);

struct TrafficRow {
    std::string name;
    float messagesPerSec = 0.0f;
    float bytesPerSec = 0.0f;
};

struct TrafficSample {
    float wireInPerSec = 0.0f;
    float wireOutPerSec = 0.0f;
    std::vector<TrafficRow> rows;   ///< highest bytes/s first, from the last full second
};

const TrafficSample& Traffic();

} // namespace okay::net

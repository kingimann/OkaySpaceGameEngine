#pragma once
// Network simulator and a per-object traffic counter.
//
// The simulator sits on NetworkManager's socket send/receive. With it off
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

/// True when enabled and at least one knob would change a datagram.
bool SimulatorActive();

/// Drop any datagrams still waiting. Settings stay as they are.
void ResetSimulator();

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

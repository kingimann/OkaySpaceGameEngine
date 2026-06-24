#pragma once
#include "okay/Net/UdpSocket.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>

namespace okay::net {

/// A small TURN-style relay so two peers behind NATs can play together without
/// either one accepting an inbound connection or configuring port-forwarding.
/// Both peers connect *out* to this relay (NATs allow that) and announce a shared
/// session code; the relay pairs them and forwards datagrams between them by a
/// per-peer "slot" id, rewriting the slot so each side sees a stable sender.
///
/// It is deliberately game-agnostic: it never parses the inner message bytes, so
/// the engine's encryption and reliability still run end-to-end through it. Drive
/// it by calling Poll() in a loop (the standalone `okayspace-relay` binary does
/// exactly that); tests pump it in-process alongside the peers.
class RelayServer {
public:
    /// Bind the relay to a UDP port (0 picks an ephemeral one). Returns false if
    /// the platform socket can't be opened/bound.
    bool Start(std::uint16_t port);
    void Stop();
    bool IsRunning() const { return m_socket.IsOpen(); }
    /// The port actually bound (useful after Start(0)).
    std::uint16_t Port() const { return m_socket.LocalPort(); }

    /// Process all pending datagrams and expire peers that have gone silent for
    /// longer than `timeout` seconds. Call it often; `dt` is seconds since the
    /// last call (used only for the idle timeout).
    void Poll(float dt = 0.0f, float timeout = 30.0f);

    /// Diagnostics for tests / a status line.
    std::size_t SessionCount() const { return m_sessions.size(); }
    std::size_t PeerCount() const { return m_peers.size(); }

private:
    struct Peer {
        std::uint32_t slot = 0;
        std::string   code;
        bool          host = false;
        float         idle = 0.0f;
    };
    struct Session {
        std::uint32_t hostSlot = 0;     // 0 until a host registers
    };

    Peer* FindBySlot(std::uint32_t slot);
    void  SendWelcome(const Endpoint& to, std::uint32_t yourSlot, std::uint32_t hostSlot);

    UdpSocket m_socket;
    std::uint32_t m_nextSlot = 1;
    std::unordered_map<Endpoint, Peer, EndpointHash> m_peers;   // by real address
    std::unordered_map<std::uint32_t, Endpoint>      m_slotEp;  // slot -> real address
    std::unordered_map<std::string, Session>         m_sessions;// by code
};

} // namespace okay::net

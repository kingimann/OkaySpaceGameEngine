#include "okay/Net/RelayServer.hpp"
#include "okay/Net/RelayProtocol.hpp"
#include "okay/Net/Packet.hpp"
#include "okay/Core/Log.hpp"
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace okay::net {

bool RelayServer::Start(std::uint16_t port) {
    Stop();
    if (!Startup()) { OKAY_ERROR("relay: net startup failed"); return false; }
    if (!m_socket.Open() || !m_socket.Bind(port)) {
        OKAY_ERROR("relay: cannot bind UDP port ", port);
        m_socket.Close();
        return false;
    }
    OKAY_INFO("relay: listening on UDP ", m_socket.LocalPort());
    return true;
}

void RelayServer::Stop() {
    m_socket.Close();
    m_peers.clear();
    m_slotEp.clear();
    m_sessions.clear();
    m_perAddress.clear();
}

RelayServer::Peer* RelayServer::FindBySlot(std::uint32_t slot) {
    auto it = m_slotEp.find(slot);
    if (it == m_slotEp.end()) return nullptr;
    auto p = m_peers.find(it->second);
    return p != m_peers.end() ? &p->second : nullptr;
}

void RelayServer::SendWelcome(const Endpoint& to, std::uint32_t yourSlot, std::uint32_t hostSlot) {
    Packet w(RelayWelcome);
    w.Write(yourSlot);
    w.Write(hostSlot);
    m_socket.SendTo(to, w.Data(), w.Size());
}

// A random, unused, non-zero slot: a peer can't guess another session's slots.
std::uint32_t RelayServer::NewSlot() {
    for (;;) {
        std::uint32_t s = static_cast<std::uint32_t>(m_rng());
        if (s != 0 && m_slotEp.find(s) == m_slotEp.end()) return s;
    }
}

// Forget a peer; a session goes when its last peer does, and loses its host
// (clients keep waiting for a new one) when the host leaves.
RelayServer::PeerMap::iterator RelayServer::RemovePeer(PeerMap::iterator it) {
    const std::uint32_t slot = it->second.slot;
    m_slotEp.erase(slot);
    auto pa = m_perAddress.find(it->first.address);
    if (pa != m_perAddress.end() && --pa->second == 0) m_perAddress.erase(pa);
    auto s = m_sessions.find(it->second.code);
    if (s != m_sessions.end()) {
        if (s->second.hostSlot == slot) s->second.hostSlot = 0;
        if (s->second.peers > 0) --s->second.peers;
        if (s->second.peers == 0) m_sessions.erase(s);
    }
    return m_peers.erase(it);
}

void RelayServer::HandleDatagram(const Endpoint& from, const std::uint8_t* data, std::size_t len) {
    if (len < 1) return;
    const std::uint8_t tag = data[0];

    if (tag == RelayHello) {
        Packet p(data, len);
        p.ReadU8();
        std::uint8_t role = p.ReadU8();
        std::string code = p.ReadString();
        if (!p.Ok() || code.empty() || code.size() > kMaxCodeLength) return;
        if (role != RelayHost && role != RelayClient) return;
        auto existing = m_peers.find(from);
        if (existing == m_peers.end()) {
            // A new peer: only within the limits.
            if (m_peers.size() >= kMaxPeers) return;
            auto pa = m_perAddress.find(from.address);
            if (pa != m_perAddress.end() && pa->second >= kMaxPeersPerAddress) return;
            auto sit = m_sessions.find(code);
            if (sit == m_sessions.end() && m_sessions.size() >= kMaxSessions) return;
            if (sit != m_sessions.end() && sit->second.peers >= kMaxPeersPerSession) return;
            Peer peer;
            peer.slot = NewSlot();
            peer.code = code;
            peer.host = (role == RelayHost);
            m_slotEp[peer.slot] = from;
            m_sessions[code].peers++;
            m_perAddress[from.address]++;
            existing = m_peers.emplace(from, peer).first;
            if (m_logBudget >= 1.0f) {
                m_logBudget -= 1.0f;
                OKAY_INFO("relay: peer slot ", peer.slot, peer.host ? " (host)" : " (client)",
                          " joined session '", code, "'",
                          m_quietJoins ? " (+" + std::to_string(m_quietJoins) + " more not logged)" : std::string());
                m_quietJoins = 0;
            } else {
                ++m_quietJoins;
            }
        }
        // Re-hellos from the same address keep the slot and session they started
        // with, so a NAT-keepalive hello is idempotent.
        Peer& peer = existing->second;
        peer.idle = 0.0f;
        Session& s = m_sessions[peer.code];
        if (peer.host && s.hostSlot == 0) {
            s.hostSlot = peer.slot;
            // A host just appeared: welcome it, and wire up any clients that
            // were already waiting for this code.
            SendWelcome(from, peer.slot, s.hostSlot);
            for (auto& [ep, other] : m_peers)
                if (!other.host && other.code == peer.code)
                    SendWelcome(ep, other.slot, s.hostSlot);
        } else if (peer.host) {
            SendWelcome(from, peer.slot, s.hostSlot);
        } else if (s.hostSlot != 0) {
            // Client and a host already exists -> pair immediately.
            SendWelcome(from, peer.slot, s.hostSlot);
        }
        // else: client with no host yet — it keeps re-helloing until one
        // registers, at which point the loop above welcomes it.
    } else if (tag == RelayData) {
        Packet p(data, len);
        p.ReadU8();
        std::uint32_t destSlot = p.ReadU32();
        if (!p.Ok()) return;
        auto sender = m_peers.find(from);
        if (sender == m_peers.end()) return;           // unknown sender — ignore
        sender->second.idle = 0.0f;
        auto destEp = m_slotEp.find(destSlot);
        if (destEp == m_slotEp.end()) return;          // dest gone — drop
        auto dest = m_peers.find(destEp->second);
        // Only between peers of the same session: a peer can't inject packets
        // into someone else's game by naming their slot.
        if (dest == m_peers.end() || dest->second.code != sender->second.code) return;
        if (destEp->second == from) return;             // not to itself
        // Rewrite the slot field from dest to the *sender's* slot, then forward
        // the untouched payload.
        const std::size_t payloadOff = 1 + 4;          // tag + destSlot
        const std::uint32_t src = sender->second.slot;
        m_out.resize(len);
        m_out[0] = RelayData;
        std::memcpy(m_out.data() + 1, &src, sizeof(src));   // Packet's layout: native u32
        if (len > payloadOff) std::memcpy(m_out.data() + payloadOff, data + payloadOff, len - payloadOff);
        m_socket.SendTo(destEp->second, m_out.data(), len);
    } else if (tag == RelayBye) {
        auto it = m_peers.find(from);
        if (it != m_peers.end()) RemovePeer(it);
    }
}

void RelayServer::Poll(float dt, float timeout) {
    if (!m_socket.IsOpen()) return;
    static thread_local std::vector<std::uint8_t> buf(kMaxDatagram);
    Endpoint from;
    int n;
    while ((n = m_socket.RecvFrom(buf.data(), buf.size(), from)) > 0)
        HandleDatagram(from, buf.data(), static_cast<std::size_t>(n));

    m_logBudget = std::fmin(20.0f, m_logBudget + 5.0f * dt);
    // Expire peers we haven't heard from in a while (NAT mapping likely gone).
    for (auto it = m_peers.begin(); it != m_peers.end(); ) {
        it->second.idle += dt;
        if (it->second.idle > timeout) it = RemovePeer(it);
        else ++it;
    }
}

} // namespace okay::net

#include "okay/Net/RelayServer.hpp"
#include "okay/Net/RelayProtocol.hpp"
#include "okay/Net/Packet.hpp"
#include "okay/Core/Log.hpp"
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
    m_nextSlot = 1;
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

void RelayServer::Poll(float dt, float timeout) {
    if (!m_socket.IsOpen()) return;
    static thread_local std::vector<std::uint8_t> buf(kMaxDatagram);
    Endpoint from;
    int n;
    while ((n = m_socket.RecvFrom(buf.data(), buf.size(), from)) > 0) {
        const std::uint8_t* data = buf.data();
        std::size_t len = static_cast<std::size_t>(n);
        if (len < 1) continue;
        std::uint8_t tag = data[0];

        if (tag == RelayHello) {
            Packet p(data, len);
            p.ReadU8();
            std::uint8_t role = p.ReadU8();
            std::string code = p.ReadString();
            if (!p.Ok()) continue;
            // Register (or refresh) this peer. Re-hellos from the same address keep
            // the same slot so a NAT-keepalive hello is idempotent.
            Peer& peer = m_peers[from];
            if (peer.slot == 0) {
                peer.slot = m_nextSlot++;
                peer.code = code;
                peer.host = (role == RelayHost);
                m_slotEp[peer.slot] = from;
                OKAY_INFO("relay: peer slot ", peer.slot, peer.host ? " (host)" : " (client)",
                          " joined session '", code, "'");
            }
            peer.idle = 0.0f;
            Session& s = m_sessions[code];
            if (peer.host && s.hostSlot == 0) {
                s.hostSlot = peer.slot;
                // A host just appeared: welcome it, and wire up any clients that
                // were already waiting for this code.
                SendWelcome(from, peer.slot, s.hostSlot);
                for (auto& [ep, other] : m_peers)
                    if (!other.host && other.code == code)
                        SendWelcome(ep, other.slot, s.hostSlot);
            } else if (peer.host) {
                SendWelcome(from, peer.slot, s.hostSlot);
            } else if (s.hostSlot != 0) {
                // Client and a host already exists -> pair immediately.
                SendWelcome(from, peer.slot, s.hostSlot);
            }
            // else: client with no host yet — it will keep re-helloing until one
            // registers, at which point the loop above welcomes it.
        } else if (tag == RelayData) {
            Packet p(data, len);
            p.ReadU8();
            std::uint32_t destSlot = p.ReadU32();
            if (!p.Ok()) continue;
            auto sender = m_peers.find(from);
            if (sender == m_peers.end()) continue;   // unknown sender — ignore
            sender->second.idle = 0.0f;
            auto destEp = m_slotEp.find(destSlot);
            if (destEp == m_slotEp.end()) continue;   // dest gone — drop
            // Rewrite the slot field from dest to the *sender's* slot, then forward
            // the untouched payload. Header layout is identical, so patch in place.
            Packet out(RelayData);
            out.Write(sender->second.slot);
            std::vector<std::uint8_t> dg(out.Data(), out.Data() + out.Size());
            const std::size_t payloadOff = 1 + 4;     // tag + destSlot
            dg.insert(dg.end(), data + payloadOff, data + len);
            m_socket.SendTo(destEp->second, dg.data(), dg.size());
        } else if (tag == RelayBye) {
            auto it = m_peers.find(from);
            if (it != m_peers.end()) {
                std::uint32_t slot = it->second.slot;
                const std::string code = it->second.code;
                bool wasHost = it->second.host;
                m_slotEp.erase(slot);
                m_peers.erase(it);
                if (wasHost) {
                    auto s = m_sessions.find(code);
                    if (s != m_sessions.end() && s->second.hostSlot == slot) m_sessions.erase(s);
                }
            }
        }
    }

    // Expire peers we haven't heard from in a while (NAT mapping likely gone).
    for (auto it = m_peers.begin(); it != m_peers.end(); ) {
        it->second.idle += dt;
        if (it->second.idle > timeout) {
            std::uint32_t slot = it->second.slot;
            const std::string code = it->second.code;
            bool wasHost = it->second.host;
            m_slotEp.erase(slot);
            if (wasHost) {
                auto s = m_sessions.find(code);
                if (s != m_sessions.end() && s->second.hostSlot == slot) m_sessions.erase(s);
            }
            it = m_peers.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace okay::net

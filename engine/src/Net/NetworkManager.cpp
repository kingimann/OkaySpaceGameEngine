#include "okay/Net/NetworkManager.hpp"
#include "okay/Net/Packet.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Core/Log.hpp"

namespace okay {

namespace {
    enum Msg : std::uint8_t { Join = 1, Welcome = 2, State = 3, Snapshot = 4, Leave = 5, Message = 6 };
    constexpr float kClientTimeout = 5.0f;
}

bool NetworkManager::StartServer(std::uint16_t port) {
    Stop();
    if (!net::Startup()) { OKAY_ERROR("net: startup failed"); return false; }
    m_netStarted = true;
    if (!m_socket.Open() || !m_socket.Bind(port)) {
        OKAY_ERROR("net: failed to bind server port ", port);
        Stop();
        return false;
    }
    m_mode = Mode::Server;
    m_localId = 0;
    OKAY_INFO("net: server listening on UDP ", port);
    return true;
}

bool NetworkManager::StartClient(const std::string& host, std::uint16_t port) {
    Stop();
    if (!net::Startup()) { OKAY_ERROR("net: startup failed"); return false; }
    m_netStarted = true;
    if (!m_socket.Open()) { OKAY_ERROR("net: failed to open client socket"); Stop(); return false; }
    if (!net::UdpSocket::Resolve(host, port, m_serverEp)) {
        OKAY_ERROR("net: cannot resolve ", host);
        Stop();
        return false;
    }
    m_mode = Mode::Client;
    m_joined = false;
    OKAY_INFO("net: client connecting to ", m_serverEp.ToString());
    return true;
}

void NetworkManager::Stop() {
    if (m_mode == Mode::Client && m_joined && m_socket.IsOpen()) {
        net::Packet p(Leave);
        p.Write(m_localId);
        m_socket.SendTo(m_serverEp, p.Data(), p.Size());
    }
    m_socket.Close();
    if (m_netStarted) { net::Shutdown(); m_netStarted = false; }
    m_clients.clear();
    m_remotes.clear();
    m_inbox.clear();
    m_mode = Mode::Offline;
    m_joined = false;
}

GameObject* NetworkManager::EnsureRemote(std::uint32_t id, char glyph) {
    auto it = m_remotes.find(id);
    if (it != m_remotes.end()) return it->second;
    GameObject* go = m_spawnRemote ? m_spawnRemote(id, glyph) : nullptr;
    if (go) m_remotes[id] = go;
    return go;
}

void NetworkManager::ApplyPeer(const PeerState& s) {
    if (s.id == m_localId) return; // never mirror ourselves
    GameObject* go = EnsureRemote(s.id, s.glyph);
    if (go && go->transform) go->transform->localPosition = {s.x, s.y, s.z};
}

void NetworkManager::Update(float dt) {
    if (m_mode == Mode::Server) ServerTick(dt);
    else if (m_mode == Mode::Client) ClientTick(dt);
}

void NetworkManager::Deliver(std::uint32_t from, const std::string& channel,
                             const std::string& data) {
    NetMessage m{from, channel, data};
    if (m_msgHandler) m_msgHandler(m);
    m_inbox.push_back(std::move(m));
}

void NetworkManager::Send(const std::string& channel, const std::string& data) {
    if (m_mode == Mode::Server) {
        // Server originates a message: stamp it with our id (0) and fan out.
        net::Packet p(Message);
        p.Write(m_localId);
        p.Write(channel);
        p.Write(data);
        for (auto& [ep, c] : m_clients)
            m_socket.SendTo(c.endpoint, p.Data(), p.Size());
    } else if (m_mode == Mode::Client && m_joined) {
        net::Packet p(Message);
        p.Write(m_localId);
        p.Write(channel);
        p.Write(data);
        m_socket.SendTo(m_serverEp, p.Data(), p.Size());
    }
}

void NetworkManager::ServerTick(float dt) {
    std::uint8_t buf[1024];
    net::Endpoint from;
    int n;
    while ((n = m_socket.RecvFrom(buf, sizeof(buf), from)) > 0) {
        net::Packet p(buf, static_cast<std::size_t>(n));
        std::uint8_t type = p.ReadU8();
        if (type == Join) {
            auto& c = m_clients[from];
            if (c.id == 0) {
                c.endpoint = from;
                c.id = m_nextId++;
                c.state = {c.id, 0, 0, 0, '@'};
            }
            c.lastSeen = 0.0f;
            net::Packet w(Welcome);
            w.Write(c.id);
            m_socket.SendTo(from, w.Data(), w.Size());
            OKAY_INFO("net: client ", c.id, " joined from ", from.ToString());
        } else if (type == State) {
            auto it = m_clients.find(from);
            if (it != m_clients.end()) {
                PeerState s;
                s.id    = it->second.id;
                p.ReadU32(); // sender-reported id (ignored; endpoint is truth)
                s.x = p.ReadF32(); s.y = p.ReadF32(); s.z = p.ReadF32();
                s.glyph = static_cast<char>(p.ReadU8());
                it->second.state = s;
                it->second.lastSeen = 0.0f;
                ApplyPeer(s); // host sees the client move
            }
        } else if (type == Leave) {
            auto it = m_clients.find(from);
            if (it != m_clients.end()) {
                std::uint32_t id = it->second.id;
                if (auto r = m_remotes.find(id); r != m_remotes.end()) {
                    if (GetScene() && r->second) GetScene()->Destroy(r->second);
                    m_remotes.erase(r);
                }
                m_clients.erase(it);
            }
        } else if (type == Message) {
            auto it = m_clients.find(from);
            if (it != m_clients.end()) {
                std::uint32_t sender = it->second.id;   // endpoint is the source of truth
                p.ReadU32();                            // sender-reported id (ignored)
                std::string channel = p.ReadString();
                std::string data    = p.ReadString();
                Deliver(sender, channel, data);         // the host sees it too
                // Relay to every other client, stamped with the real sender id.
                net::Packet relay(Message);
                relay.Write(sender);
                relay.Write(channel);
                relay.Write(data);
                for (auto& [ep, c] : m_clients)
                    if (c.id != sender)
                        m_socket.SendTo(c.endpoint, relay.Data(), relay.Size());
            }
        }
    }

    // Time out silent clients.
    for (auto it = m_clients.begin(); it != m_clients.end();) {
        it->second.lastSeen += dt;
        if (it->second.lastSeen > kClientTimeout) {
            std::uint32_t id = it->second.id;
            if (auto r = m_remotes.find(id); r != m_remotes.end()) {
                if (GetScene() && r->second) GetScene()->Destroy(r->second);
                m_remotes.erase(r);
            }
            it = m_clients.erase(it);
        } else {
            ++it;
        }
    }

    // Broadcast a snapshot of everyone (server avatar id 0 + all clients).
    m_snapshotTimer += dt;
    if (m_snapshotTimer >= m_snapshotInterval && !m_clients.empty()) {
        m_snapshotTimer = 0.0f;
        net::Packet snap(Snapshot);
        std::uint32_t count = 1 + static_cast<std::uint32_t>(m_clients.size());
        snap.Write(count);
        Vec3 lp = m_localAvatar ? m_localAvatar->localPosition : Vec3::Zero;
        snap.Write(std::uint32_t(0));
        snap.Write(lp.x); snap.Write(lp.y); snap.Write(lp.z);
        snap.Write(std::uint8_t(m_localGlyph));
        for (auto& [ep, c] : m_clients) {
            snap.Write(c.id);
            snap.Write(c.state.x); snap.Write(c.state.y); snap.Write(c.state.z);
            snap.Write(std::uint8_t(c.state.glyph));
        }
        for (auto& [ep, c] : m_clients)
            m_socket.SendTo(c.endpoint, snap.Data(), snap.Size());
    }
}

void NetworkManager::ClientTick(float dt) {
    if (!m_joined) {
        m_joinTimer -= dt;
        if (m_joinTimer <= 0.0f) {
            net::Packet p(Join);
            m_socket.SendTo(m_serverEp, p.Data(), p.Size());
            m_joinTimer = 0.5f;
        }
    }

    std::uint8_t buf[1024];
    net::Endpoint from;
    int n;
    while ((n = m_socket.RecvFrom(buf, sizeof(buf), from)) > 0) {
        net::Packet p(buf, static_cast<std::size_t>(n));
        std::uint8_t type = p.ReadU8();
        if (type == Welcome) {
            m_localId = p.ReadU32();
            m_joined = true;
            OKAY_INFO("net: joined server as peer ", m_localId);
        } else if (type == Snapshot) {
            std::uint32_t count = p.ReadU32();
            for (std::uint32_t i = 0; i < count && p.Ok(); ++i) {
                PeerState s;
                s.id = p.ReadU32();
                s.x = p.ReadF32(); s.y = p.ReadF32(); s.z = p.ReadF32();
                s.glyph = static_cast<char>(p.ReadU8());
                ApplyPeer(s);
            }
        } else if (type == Message) {
            std::uint32_t sender = p.ReadU32();
            std::string channel = p.ReadString();
            std::string data    = p.ReadString();
            if (sender != m_localId) Deliver(sender, channel, data);
        }
    }

    // Send our local state to the server.
    if (m_joined && m_localAvatar) {
        Vec3 lp = m_localAvatar->localPosition;
        net::Packet p(State);
        p.Write(m_localId);
        p.Write(lp.x); p.Write(lp.y); p.Write(lp.z);
        p.Write(std::uint8_t(m_localGlyph));
        m_socket.SendTo(m_serverEp, p.Data(), p.Size());
    }
}

} // namespace okay

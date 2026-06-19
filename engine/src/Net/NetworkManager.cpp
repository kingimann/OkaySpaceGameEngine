#include "okay/Net/NetworkManager.hpp"
#include "okay/Net/Packet.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Components/SpriteRenderer.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Core/Log.hpp"
#include <sstream>

namespace okay {

namespace {
    enum Msg : std::uint8_t {
        Join = 1, Welcome = 2, State = 3, Snapshot = 4, Leave = 5,
        Message = 6, DirectMessage = 7, SyncVar = 8, Ping = 9, Pong = 10
    };
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
    m_remoteTarget.clear();
    m_inbox.clear();
    m_syncVars.clear();
    m_rtt = 0.0f;
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
    if (!go || !go->transform) return;
    Vec3 target{s.x, s.y, s.z};
    if (interpolationRate > 0.0f) {
        // Ease toward the latest position over the next frames (set the target;
        // InterpolateRemotes moves there). Snap on the first sample so a freshly
        // spawned remote doesn't slide in from the origin.
        if (m_remoteTarget.find(s.id) == m_remoteTarget.end())
            go->transform->localPosition = target;
        m_remoteTarget[s.id] = target;
    } else {
        go->transform->localPosition = target;
    }
}

void NetworkManager::InterpolateRemotes(float dt) {
    if (interpolationRate <= 0.0f) return;
    float t = interpolationRate * dt;
    if (t > 1.0f) t = 1.0f;
    for (auto& [id, go] : m_remotes) {
        auto it = m_remoteTarget.find(id);
        if (it == m_remoteTarget.end() || !go || !go->transform) continue;
        Vec3 p = go->transform->localPosition;
        go->transform->localPosition = p + (it->second - p) * t;
    }
}

void NetworkManager::Update(float dt) {
    if (m_mode == Mode::Server) ServerTick(dt);
    else if (m_mode == Mode::Client) ClientTick(dt);
    InterpolateRemotes(dt);
}

void NetworkManager::Deliver(std::uint32_t from, const std::string& channel,
                             const std::string& data) {
    // The reserved "__spawn" channel instantiates a prefab on this peer instead
    // of surfacing as a user message (replicated object creation).
    if (channel == "__spawn") {
        Scene* s = GetScene();
        if (!s) return;
        std::stringstream ss(data);
        std::string path; float x = 0, y = 0, z = 0; char sep;
        std::getline(ss, path, '|');
        ss >> x >> sep >> y >> sep >> z;
        if (GameObject* g = SceneSerializer::InstantiateFromFile(*s, path))
            if (g->transform) g->transform->localPosition = {x, y, z};
        return;
    }
    NetMessage m{from, channel, data};
    if (m_msgHandler) m_msgHandler(m);
    m_inbox.push_back(std::move(m));
}

void NetworkManager::Spawn(const std::string& prefabPath, const Vec3& position) {
    // Instantiate locally right away so the spawner sees it without a round trip.
    if (Scene* s = GetScene()) {
        if (GameObject* g = SceneSerializer::InstantiateFromFile(*s, prefabPath))
            if (g->transform) g->transform->localPosition = position;
    }
    // Tell every other peer to make the same object (server relays for clients).
    std::ostringstream os;
    os << prefabPath << '|' << position.x << '|' << position.y << '|' << position.z;
    Send("__spawn", os.str());
}

void NetworkManager::Start() {
    if (!startName.empty()) m_localName = startName;
    if (!startRoom.empty()) m_localRoom = startRoom;
    // Default the broadcast avatar + remote factory so auto-start works with no
    // extra wiring (the component's own object is the avatar; peers show as sprites).
    if (!m_localAvatar && gameObject && gameObject->transform)
        m_localAvatar = gameObject->transform;
    if (!m_spawnRemote && gameObject && gameObject->scene()) {
        Scene* s = gameObject->scene();
        m_spawnRemote = [s](std::uint32_t id, char) {
            GameObject* g = s->CreateGameObject("Peer" + std::to_string(id));
            g->AddComponent<SpriteRenderer>()->color = Color::FromBytes(230, 120, 90);
            return g;
        };
    }
    if (autoStart == AutoStart::Host) StartServer(autoPort);
    else if (autoStart == AutoStart::Join) StartClient(autoHost, autoPort);
}

void NetworkManager::ApplySyncVar(const std::string& key, const std::string& value) {
    m_syncVars[key] = value;
}

std::string NetworkManager::GetVar(const std::string& key) const {
    auto it = m_syncVars.find(key);
    return it != m_syncVars.end() ? it->second : std::string{};
}

void NetworkManager::SetVar(const std::string& key, const std::string& value) {
    ApplySyncVar(key, value);                 // optimistic / offline local apply
    if (m_mode == Mode::Server) {
        net::Packet p(SyncVar); p.Write(key); p.Write(value);
        for (auto& [ep, c] : m_clients) m_socket.SendTo(c.endpoint, p.Data(), p.Size());
    } else if (m_mode == Mode::Client && m_joined) {
        net::Packet p(SyncVar); p.Write(key); p.Write(value);
        m_socket.SendTo(m_serverEp, p.Data(), p.Size());
    }
}

void NetworkManager::SendTo(std::uint32_t targetId, const std::string& channel,
                            const std::string& data) {
    if (m_mode == Mode::Server) {
        if (targetId == m_localId) { Deliver(m_localId, channel, data); return; }
        for (auto& [ep, c] : m_clients)
            if (c.id == targetId) {
                net::Packet p(Message);
                p.Write(m_localId); p.Write(channel); p.Write(data);
                m_socket.SendTo(c.endpoint, p.Data(), p.Size());
                return;
            }
    } else if (m_mode == Mode::Client && m_joined) {
        net::Packet p(DirectMessage);
        p.Write(m_localId); p.Write(targetId); p.Write(channel); p.Write(data);
        m_socket.SendTo(m_serverEp, p.Data(), p.Size());
    }
}

std::vector<NetworkManager::PeerInfo> NetworkManager::Peers() const {
    std::vector<PeerInfo> out;
    for (auto& [ep, c] : m_clients) out.push_back({c.id, c.name, c.state.glyph});
    return out;
}

std::string NetworkManager::PeerName(std::uint32_t id) const {
    for (auto& [ep, c] : m_clients) if (c.id == id) return c.name;
    return {};
}

void NetworkManager::Send(const std::string& channel, const std::string& data) {
    if (m_mode == Mode::Server) {
        // Server originates a message: stamp it with our id (0) and fan out to
        // the clients in the host's room.
        net::Packet p(Message);
        p.Write(m_localId);
        p.Write(channel);
        p.Write(data);
        for (auto& [ep, c] : m_clients)
            if (c.room == m_localRoom)
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
            std::string name = p.ReadString();           // display name (may be empty)
            std::string room = p.ReadString();           // lobby room (may be empty)
            auto& c = m_clients[from];
            bool isNew = (c.id == 0);
            if (isNew) {
                c.endpoint = from;
                c.id = m_nextId++;
                c.state = {c.id, 0, 0, 0, '@'};
                c.name = name.empty() ? ("Player" + std::to_string(c.id)) : name;
                c.room = room;
            }
            c.lastSeen = 0.0f;
            net::Packet w(Welcome);
            w.Write(c.id);
            m_socket.SendTo(from, w.Data(), w.Size());
            if (isNew) {
                OKAY_INFO("net: client ", c.id, " '", c.name, "' joined from ", from.ToString());
                if (m_peerJoined) m_peerJoined(c.id, c.name);
                // Full sync: send every current synced variable to the newcomer.
                for (auto& [k, v] : m_syncVars) {
                    net::Packet sv(SyncVar); sv.Write(k); sv.Write(v);
                    m_socket.SendTo(from, sv.Data(), sv.Size());
                }
            }
        } else if (type == SyncVar) {
            // A client requested a variable change; apply it and fan out to all.
            std::string key = p.ReadString();
            std::string value = p.ReadString();
            ApplySyncVar(key, value);
            net::Packet out(SyncVar); out.Write(key); out.Write(value);
            for (auto& [ep, c] : m_clients) m_socket.SendTo(c.endpoint, out.Data(), out.Size());
        } else if (type == Ping) {
            // Echo the client's timestamp straight back so it can measure RTT.
            float stamp = p.ReadF32();
            net::Packet pong(Pong); pong.Write(stamp);
            m_socket.SendTo(from, pong.Data(), pong.Size());
        } else if (type == DirectMessage) {
            auto it = m_clients.find(from);
            if (it != m_clients.end()) {
                std::uint32_t sender = it->second.id;
                p.ReadU32();                              // sender-reported id (ignored)
                std::uint32_t target = p.ReadU32();
                std::string channel = p.ReadString();
                std::string data    = p.ReadString();
                if (target == m_localId) {                // addressed to the host
                    Deliver(sender, channel, data);
                } else {                                   // route to one client
                    for (auto& [ep, cl] : m_clients)
                        if (cl.id == target) {
                            net::Packet fwd(Message);
                            fwd.Write(sender); fwd.Write(channel); fwd.Write(data);
                            m_socket.SendTo(cl.endpoint, fwd.Data(), fwd.Size());
                            break;
                        }
                }
            }
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
                if (m_peerLeft) m_peerLeft(id);
            }
        } else if (type == Message) {
            auto it = m_clients.find(from);
            if (it != m_clients.end()) {
                std::uint32_t sender = it->second.id;   // endpoint is the source of truth
                const std::string senderRoom = it->second.room;
                p.ReadU32();                            // sender-reported id (ignored)
                std::string channel = p.ReadString();
                std::string data    = p.ReadString();
                if (m_localRoom == senderRoom)          // host only sees its room
                    Deliver(sender, channel, data);
                // Relay to every other client in the sender's room.
                net::Packet relay(Message);
                relay.Write(sender);
                relay.Write(channel);
                relay.Write(data);
                for (auto& [ep, c] : m_clients)
                    if (c.id != sender && c.room == senderRoom)
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
            if (m_peerLeft) m_peerLeft(id);
        } else {
            ++it;
        }
    }

    // Send each client a snapshot of just its own room: the host (id 0) if the
    // host shares that room, plus every client in the same room. This is what
    // makes lobbies work — separate rooms never see each other.
    m_snapshotTimer += dt;
    if (m_snapshotTimer >= m_snapshotInterval && !m_clients.empty()) {
        m_snapshotTimer = 0.0f;
        Vec3 lp = m_localAvatar ? m_localAvatar->localPosition : Vec3::Zero;
        for (auto& [ep, recipient] : m_clients) {
            const std::string& room = recipient.room;
            bool hostHere = (m_localRoom == room);
            std::uint32_t count = hostHere ? 1 : 0;
            for (auto& [ep2, c] : m_clients) if (c.room == room) ++count;

            net::Packet snap(Snapshot);
            snap.Write(count);
            if (hostHere) {
                snap.Write(std::uint32_t(0));
                snap.Write(lp.x); snap.Write(lp.y); snap.Write(lp.z);
                snap.Write(std::uint8_t(m_localGlyph));
            }
            for (auto& [ep2, c] : m_clients) {
                if (c.room != room) continue;
                snap.Write(c.id);
                snap.Write(c.state.x); snap.Write(c.state.y); snap.Write(c.state.z);
                snap.Write(std::uint8_t(c.state.glyph));
            }
            m_socket.SendTo(recipient.endpoint, snap.Data(), snap.Size());
        }
    }
}

void NetworkManager::ClientTick(float dt) {
    m_clock += dt;
    if (!m_joined) {
        m_joinTimer -= dt;
        if (m_joinTimer <= 0.0f) {
            net::Packet p(Join);
            p.Write(m_localName);
            p.Write(m_localRoom);
            m_socket.SendTo(m_serverEp, p.Data(), p.Size());
            m_joinTimer = 0.5f;
        }
    } else {
        // Ping the server about once a second to measure round-trip time.
        m_pingTimer -= dt;
        if (m_pingTimer <= 0.0f) {
            net::Packet p(Ping); p.Write(m_clock);
            m_socket.SendTo(m_serverEp, p.Data(), p.Size());
            m_pingTimer = 1.0f;
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
        } else if (type == SyncVar) {
            std::string key = p.ReadString();
            std::string value = p.ReadString();
            ApplySyncVar(key, value);
        } else if (type == Pong) {
            float stamp = p.ReadF32();
            m_rtt = (m_clock - stamp) * 1000.0f;   // round-trip in ms
            if (m_rtt < 0.0f) m_rtt = 0.0f;
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

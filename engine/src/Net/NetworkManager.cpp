#include "okay/Net/NetworkManager.hpp"
#include "okay/Net/Packet.hpp"
#include "okay/Net/RelayProtocol.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Components/SpriteRenderer.hpp"
#include "okay/Components/NetworkSync.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Core/Log.hpp"
#include <sstream>

namespace okay {

namespace {
    enum Msg : std::uint8_t {
        Join = 1, Welcome = 2, State = 3, Snapshot = 4, Leave = 5,
        Message = 6, DirectMessage = 7, SyncVar = 8, Ping = 9, Pong = 10,
        Ready = 11, Reject = 12, Kicked = 13, ReliableMsg = 14, ReliableAck = 15,
        Fragment = 16   // one piece of a message too large for a single datagram
    };
    constexpr float kFragmentTtl = 5.0f;        // drop half-assembled messages after this
    // Outer tag marking an encrypted datagram (nonce||ciphertext follows). Distinct
    // from every Msg type (1..16) and from a plaintext message's leading type byte.
    constexpr std::uint8_t kEncEnvelope = 0xEE;
    constexpr float kClientTimeout = 5.0f;
    constexpr float kReliableResend = 0.25f;   // resend an unacked message this often

    std::vector<std::uint8_t> BuildReliable(std::uint32_t seq, const std::string& channel,
                                            const std::string& data) {
        net::Packet p(ReliableMsg);
        p.Write(seq); p.Write(channel); p.Write(data);
        return std::vector<std::uint8_t>(p.Data(), p.Data() + p.Size());
    }

    // A relay peer is identified by a slot id carried in the Endpoint's address,
    // with a sentinel port so it can never be confused with a real address. The
    // server keys its client table by these, so relay routing needs no other change.
    net::Endpoint SlotEndpoint(std::uint32_t slot) {
        return net::Endpoint{slot, net::kRelaySlotPort};
    }
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
    m_kicked = false; m_kickReason.clear();
    if (snapshotRate > 0.0f) m_snapshotInterval = 1.0f / snapshotRate;
    OKAY_INFO("net: server '", serverName, "' listening on UDP ", port,
              " (max ", maxPlayers, ")");
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
    m_kicked = false; m_kickReason.clear();
    m_localUserId.clear();
    if (encryption && net::SecureChannel::Available()) m_secure.GenerateKeys();
    OKAY_INFO("net: client connecting to ", m_serverEp.ToString());
    return true;
}

bool NetworkManager::StartRelay(const std::string& relayHost, std::uint16_t relayPort,
                                const std::string& code, bool asHost) {
    Stop();
    if (!net::Startup()) { OKAY_ERROR("net: startup failed"); return false; }
    m_netStarted = true;
    if (!m_socket.Open()) { OKAY_ERROR("net: failed to open relay socket"); Stop(); return false; }
    if (!net::UdpSocket::Resolve(relayHost, relayPort, m_relayEp)) {
        OKAY_ERROR("net: cannot resolve relay ", relayHost);
        Stop();
        return false;
    }
    m_relay = true;
    m_relayWelcomed = false;
    m_relayCode = code;
    m_relaySlot = 0;
    m_relayHelloTimer = 0.0f;
    m_kicked = false; m_kickReason.clear();
    if (asHost) {
        m_mode = Mode::Server;
        m_localId = 0;
        if (snapshotRate > 0.0f) m_snapshotInterval = 1.0f / snapshotRate;
        OKAY_INFO("net: hosting '", serverName, "' via relay ", m_relayEp.ToString(),
                  " (session '", code, "')");
    } else {
        m_mode = Mode::Client;
        m_joined = false;
        m_localUserId.clear();
        if (encryption && net::SecureChannel::Available()) m_secure.GenerateKeys();
        OKAY_INFO("net: joining session '", code, "' via relay ", m_relayEp.ToString());
    }
    return true;
}

bool NetworkManager::HostViaRelay(const std::string& relayHost, std::uint16_t relayPort,
                                  const std::string& code) {
    return StartRelay(relayHost, relayPort, code, /*asHost=*/true);
}

bool NetworkManager::JoinViaRelay(const std::string& relayHost, std::uint16_t relayPort,
                                  const std::string& code) {
    return StartRelay(relayHost, relayPort, code, /*asHost=*/false);
}

// Announce ourselves to the relay on connect and ~once a second after, so it can
// pair us and so the NAT mapping the relay talks back through stays open.
void NetworkManager::RelayKeepAlive(float dt) {
    if (!m_relay) return;
    m_relayHelloTimer -= dt;
    if (m_relayHelloTimer > 0.0f) return;
    m_relayHelloTimer = 1.0f;
    net::Packet h(net::RelayHello);
    h.Write(std::uint8_t(m_mode == Mode::Server ? net::RelayHost : net::RelayClient));
    h.Write(m_relayCode);
    m_socket.SendTo(m_relayEp, h.Data(), h.Size());
}

void NetworkManager::Stop() {
    if (m_mode == Mode::Client && m_joined && m_socket.IsOpen()) {
        net::Packet p(Leave);
        p.Write(m_localId);
        SendDatagram(m_serverEp, p.Data(), p.Size());
    }
    if (m_relay && m_socket.IsOpen()) {            // free our slot on the relay
        net::Packet bye(net::RelayBye);
        m_socket.SendTo(m_relayEp, bye.Data(), bye.Size());
    }
    m_relay = false;
    m_relayWelcomed = false;
    m_relaySlot = 0;
    m_relayCode.clear();
    m_relayEp = net::Endpoint{};
    m_socket.Close();
    if (m_netStarted) { net::Shutdown(); m_netStarted = false; }
    m_clients.clear();
    m_remotes.clear();
    m_remoteTarget.clear();
    m_inbox.clear();
    m_syncVars.clear();
    m_rtt = 0.0f;
    m_ready = false;
    m_matchStarted = false;
    m_serverName.clear();
    m_joinRejected = false;
    m_relOut.clear();
    m_relSeen.clear();
    m_relSeq = 0;
    // m_kicked / m_kickReason persist so WasKicked() can be read after the
    // disconnect; they're cleared when a new session starts.
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

void NetworkManager::RegisterSync(const std::string& id, Transform* t, bool owned) {
    if (id.empty()) return;
    SyncObj o; o.t = t; o.owned = owned;
    if (t) { o.targetPos = t->localPosition; o.targetRot = t->localRotation; }
    m_syncObjs[id] = o;
}

void NetworkManager::UnregisterSync(const std::string& id) { m_syncObjs.erase(id); }

void NetworkManager::SetSyncOwned(const std::string& id, bool owned) {
    auto it = m_syncObjs.find(id);
    if (it != m_syncObjs.end()) it->second.owned = owned;
}

void NetworkManager::SetSyncExtra(const std::string& id, std::function<std::string()> get,
                                  std::function<void(const std::string&)> set) {
    auto it = m_syncObjs.find(id);
    if (it == m_syncObjs.end()) return;
    it->second.getExtra = std::move(get);
    it->second.setExtra = std::move(set);
}

void NetworkManager::SyncTick(float dt) {
    if (m_syncObjs.empty()) return;
    // Move non-owned objects toward their last received state every frame: ease at
    // interpolationRate, or snap exactly when it's <= 0.
    {
        float t = interpolationRate > 0.0f ? interpolationRate * dt : 1.0f;
        if (t > 1.0f) t = 1.0f;
        for (auto& [id, o] : m_syncObjs) {
            if (o.owned || !o.t || !o.hasTarget) continue;
            o.t->localPosition = o.t->localPosition + (o.targetPos - o.t->localPosition) * t;
            o.t->localRotation = Quat::Slerp(o.t->localRotation, o.targetRot, t);
        }
    }
    // Broadcast the objects we own at the sync rate.
    if (!IsConnected()) return;
    float interval = syncRate > 0.0f ? 1.0f / syncRate : 0.0f;
    m_syncTimer += dt;
    if (m_syncTimer < interval) return;
    m_syncTimer = 0.0f;
    for (auto& [id, o] : m_syncObjs) {
        if (!o.owned || !o.t) continue;
        const Vec3& p = o.t->localPosition;
        const Quat& q = o.t->localRotation;
        std::ostringstream os;
        os << id << '|' << p.x << ' ' << p.y << ' ' << p.z << ' '
           << q.x << ' ' << q.y << ' ' << q.z << ' ' << q.w;
        if (o.getExtra) os << '|' << o.getExtra();   // optional per-object state
        Send("__xform", os.str());
    }
}

void NetworkManager::Update(float dt) {
    if (m_mode == Mode::Server) ServerTick(dt);
    else if (m_mode == Mode::Client) ClientTick(dt);
    InterpolateRemotes(dt);
    SyncTick(dt);
}

void NetworkManager::Deliver(std::uint32_t from, const std::string& channel,
                             const std::string& data) {
    // The reserved "__spawn" channel instantiates a prefab on this peer instead
    // of surfacing as a user message (replicated object creation).
    if (channel == "__match_start") {   // lobby: the match has begun for this room
        m_matchStarted = true;
        return;
    }
    if (channel == "__spawn") {
        Scene* s = GetScene();
        if (!s) return;
        std::stringstream ss(data);
        std::string path; float x = 0, y = 0, z = 0; char sep;
        std::getline(ss, path, '|');
        ss >> x >> sep >> y >> sep >> z;
        // Optional "|<netId>" marks a SpawnOwned object: attach a follower
        // NetworkSync so its transform/animation track the authority that spawned it.
        std::string netId; char bar;
        if (ss.get(bar) && bar == '|') std::getline(ss, netId);
        if (GameObject* g = SceneSerializer::InstantiateFromFile(*s, path)) {
            if (g->transform) g->transform->localPosition = {x, y, z};
            if (!netId.empty()) {
                auto* ns = g->GetComponent<NetworkSync>();
                if (!ns) ns = g->AddComponent<NetworkSync>();
                ns->netId = netId; ns->authority = NetworkSync::Authority::Manual; ns->owned = false;
            }
        }
        return;
    }
    if (channel == "__despawn") {   // remove a SpawnOwned object on this peer
        DestroyLocalSync(data);
        return;
    }
    // Networked transform: "__xform" carries "<id>|px py pz qx qy qz qw". The
    // matching registered object eases toward it (unless we own it — then we're
    // authoritative and ignore remote state).
    if (channel == "__xform") {
        std::string::size_type bar = data.find('|');
        if (bar != std::string::npos) {
            std::string id = data.substr(0, bar);
            auto it = m_syncObjs.find(id);
            if (it != m_syncObjs.end() && !it->second.owned) {
                // "<floats>[|<extra>]" — the optional extra payload follows a second '|'.
                std::string rest = data.substr(bar + 1);
                std::string extra;
                std::string::size_type bar2 = rest.find('|');
                if (bar2 != std::string::npos) { extra = rest.substr(bar2 + 1); rest.resize(bar2); }
                std::stringstream ss(rest);
                Vec3 p; Quat q;
                ss >> p.x >> p.y >> p.z >> q.x >> q.y >> q.z >> q.w;
                it->second.targetPos = p;
                it->second.targetRot = q;
                // Snap on the first sample so a freshly seen object doesn't slide
                // in from wherever it happened to spawn.
                if (!it->second.hasTarget && it->second.t) {
                    it->second.t->localPosition = p;
                    it->second.t->localRotation = q;
                }
                it->second.hasTarget = true;
                if (it->second.setExtra && bar2 != std::string::npos) it->second.setExtra(extra);
            }
        }
        return;
    }
    // First-class RPC: "__rpc/<name>" dispatches by name to a registered handler
    // (with the sender id + payload) instead of surfacing as a raw message.
    if (channel.size() > 6 && channel.compare(0, 6, "__rpc/") == 0) {
        auto it = m_rpcHandlers.find(channel.substr(6));
        if (it != m_rpcHandlers.end() && it->second) it->second(from, data);
        return;
    }
    // Chat: "__chat" carries "<name>\n<text>"; logged + handed to the chat handler.
    if (channel == "__chat") {
        ChatEntry e{from, "", data};
        std::string::size_type nl = data.find('\n');
        if (nl != std::string::npos) { e.name = data.substr(0, nl); e.text = data.substr(nl + 1); }
        m_chatLog.push_back(e);
        if (chatHistory > 0 && (int)m_chatLog.size() > chatHistory)
            m_chatLog.erase(m_chatLog.begin(), m_chatLog.begin() + ((int)m_chatLog.size() - chatHistory));
        if (m_chatHandler) m_chatHandler(e);
        return;
    }
    NetMessage m{from, channel, data};
    if (m_msgHandler) m_msgHandler(m);
    m_inbox.push_back(std::move(m));
}

void NetworkManager::Chat(const std::string& text) {
    // Show our own line locally first (a broadcast Send never echoes to the sender),
    // then fan it out to everyone else in the room.
    ChatEntry e{m_localId, m_localName, text};
    m_chatLog.push_back(e);
    if (chatHistory > 0 && (int)m_chatLog.size() > chatHistory)
        m_chatLog.erase(m_chatLog.begin(), m_chatLog.begin() + ((int)m_chatLog.size() - chatHistory));
    if (m_chatHandler) m_chatHandler(e);
    Send("__chat", m_localName + "\n" + text);
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

GameObject* NetworkManager::SpawnOwned(const std::string& prefabPath, const Vec3& position) {
    // A globally-unique id: our peer id namespaces it, so two peers spawning at the
    // same moment never collide.
    std::string id = "sp/" + std::to_string(m_localId) + "/" + std::to_string(++m_spawnCounter);
    GameObject* g = nullptr;
    if (Scene* s = GetScene()) {
        g = SceneSerializer::InstantiateFromFile(*s, prefabPath);
        if (g) {
            if (g->transform) g->transform->localPosition = position;
            // We own it: drive its replication. NetworkSync registers on its first
            // Update (the instance is updated every frame once spawned).
            auto* ns = g->GetComponent<NetworkSync>();
            if (!ns) ns = g->AddComponent<NetworkSync>();
            ns->netId = id; ns->authority = NetworkSync::Authority::Manual; ns->owned = true;
        }
    }
    std::ostringstream os;
    os << prefabPath << '|' << position.x << '|' << position.y << '|' << position.z << '|' << id;
    Send("__spawn", os.str());
    return g;
}

void NetworkManager::DestroyLocalSync(const std::string& id) {
    auto it = m_syncObjs.find(id);
    if (it != m_syncObjs.end()) {
        GameObject* go = it->second.t ? it->second.t->gameObject : nullptr;
        if (go) if (Scene* s = GetScene()) s->Destroy(go);
        m_syncObjs.erase(it);
    }
}

void NetworkManager::Despawn(const std::string& netId) {
    DestroyLocalSync(netId);
    Send("__despawn", netId);
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

void NetworkManager::SetReady(bool ready) {
    m_ready = ready;
    if (m_mode == Mode::Client && m_joined) {
        net::Packet p(Ready); p.Write(std::uint8_t(ready ? 1 : 0));
        SendDatagram(m_serverEp, p.Data(), p.Size());
    }
}

int NetworkManager::ReadyCount() const {
    int n = 0;
    for (auto& [ep, c] : m_clients) if (c.room == m_localRoom && c.ready) ++n;
    return n;
}

bool NetworkManager::AllReady() const {
    int inRoom = 0, ready = 0;
    for (auto& [ep, c] : m_clients)
        if (c.room == m_localRoom) { ++inRoom; if (c.ready) ++ready; }
    return inRoom > 0 && ready == inRoom;
}

void NetworkManager::StartMatch() {
    m_matchStarted = true;
    Send("__match_start", "1");   // room-scoped broadcast
}

std::string NetworkManager::GetVar(const std::string& key) const {
    auto it = m_syncVars.find(key);
    return it != m_syncVars.end() ? it->second : std::string{};
}

void NetworkManager::SetVar(const std::string& key, const std::string& value) {
    ApplySyncVar(key, value);                 // optimistic / offline local apply
    if (m_mode == Mode::Server) {
        net::Packet p(SyncVar); p.Write(key); p.Write(value);
        for (auto& [ep, c] : m_clients) SendDatagram(c.endpoint, p.Data(), p.Size());
    } else if (m_mode == Mode::Client && m_joined) {
        net::Packet p(SyncVar); p.Write(key); p.Write(value);
        SendDatagram(m_serverEp, p.Data(), p.Size());
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
                SendDatagram(c.endpoint, p.Data(), p.Size());
                return;
            }
    } else if (m_mode == Mode::Client && m_joined) {
        net::Packet p(DirectMessage);
        p.Write(m_localId); p.Write(targetId); p.Write(channel); p.Write(data);
        SendDatagram(m_serverEp, p.Data(), p.Size());
    }
}

void NetworkManager::SendReliableTo(std::uint32_t targetId, const std::string& channel,
                                    const std::string& data) {
    if (m_mode == Mode::Server) {
        for (auto& [ep, c] : m_clients)
            if (c.id == targetId) {
                std::uint32_t seq = c.relSeq++;
                auto bytes = BuildReliable(seq, channel, data);
                c.relOut[seq] = {bytes, 0.0f};
                SendDatagram(c.endpoint, bytes.data(), bytes.size());
                return;
            }
    } else if (m_mode == Mode::Client && m_joined) {
        // Clients can only address the server reliably (id 0).
        std::uint32_t seq = m_relSeq++;
        auto bytes = BuildReliable(seq, channel, data);
        m_relOut[seq] = {bytes, 0.0f};
        SendDatagram(m_serverEp, bytes.data(), bytes.size());
    }
}

void NetworkManager::SendReliable(const std::string& channel, const std::string& data) {
    if (m_mode == Mode::Server) {
        for (auto& [ep, c] : m_clients) SendReliableTo(c.id, channel, data);
    } else {
        SendReliableTo(0, channel, data);
    }
}

void NetworkManager::ResendReliable(
        std::unordered_map<std::uint32_t, std::pair<std::vector<std::uint8_t>, float>>& out,
        const net::Endpoint& to, float dt) {
    for (auto& [seq, item] : out) {
        item.second += dt;
        if (item.second >= kReliableResend) {
            item.second = 0.0f;
            SendDatagram(to, item.first.data(), item.first.size());
        }
    }
}

// Send a datagram, splitting it into MTU-sized fragments if it's too big to fit in
// one. Small messages (the vast majority) go out directly. Each fragment carries
// {Fragment, msgId, index, count} so the receiver can reassemble in order.
net::SecureChannel* NetworkManager::SessionFor(const net::Endpoint& ep) {
    if (m_mode == Mode::Client) return (ep == m_serverEp) ? &m_secure : nullptr;
    if (m_mode == Mode::Server) { auto it = m_clients.find(ep); return it != m_clients.end() ? &it->second.secure : nullptr; }
    return nullptr;
}

bool NetworkManager::Encrypted() const {
    if (m_mode == Mode::Client) return m_secure.Ready();
    return m_mode == Mode::Server && encryption && net::SecureChannel::Available();
}

void NetworkManager::SendDatagram(const net::Endpoint& to, const std::uint8_t* data, std::size_t size,
                                  bool allowEncrypt) {
    // Encrypt once the session for this peer is established. The sealed blob is
    // tagged with kEncEnvelope, then handed to the fragmentation step below — so
    // encryption sits above fragmentation (the receiver reassembles, then decrypts).
    std::vector<std::uint8_t> enc;
    if (allowEncrypt && encryption) {
        if (net::SecureChannel* s = SessionFor(to)) {
            if (s->Ready() && s->Seal(data, size, enc)) {
                enc.insert(enc.begin(), kEncEnvelope);
                data = enc.data(); size = enc.size();
            }
        }
    }
    if (size <= net::kFragmentThreshold) { Transmit(to, data, size); return; }
    const std::size_t header = 1 + 4 + 4 + 4;            // type + msgId + index + count
    const std::size_t chunkMax = net::kFragmentThreshold - header;
    std::uint32_t count = static_cast<std::uint32_t>((size + chunkMax - 1) / chunkMax);
    std::uint32_t id = m_fragNextId++;
    for (std::uint32_t i = 0; i < count; ++i) {
        std::size_t off = static_cast<std::size_t>(i) * chunkMax;
        std::size_t len = std::min(chunkMax, size - off);
        net::Packet h(Fragment); h.Write(id); h.Write(i); h.Write(count);
        std::vector<std::uint8_t> dg(h.Data(), h.Data() + h.Size());
        dg.insert(dg.end(), data + off, data + off + len);
        Transmit(to, dg.data(), dg.size());
    }
}

// One physical datagram out. Direct mode sends straight to the peer; relay mode
// wraps it in a RelayData frame addressed by the peer's slot (carried in `to`'s
// address) and sends it to the relay, which forwards it on.
void NetworkManager::Transmit(const net::Endpoint& to, const std::uint8_t* data, std::size_t size) {
    if (!m_relay) { m_socket.SendTo(to, data, size); return; }
    net::Packet h(net::RelayData);
    h.Write(to.address);                       // destination slot
    std::vector<std::uint8_t> dg(h.Data(), h.Data() + h.Size());
    dg.insert(dg.end(), data, data + size);
    m_socket.SendTo(m_relayEp, dg.data(), dg.size());
}

// One physical datagram in. Direct mode is a pass-through. Relay mode peels the
// RelayData header (substituting a synthetic slot-endpoint as the sender) and
// quietly handles RelayWelcome control frames, looping until it has an actual
// application payload to return (or nothing is pending).
int NetworkManager::ReceiveFrom(void* buffer, std::size_t capacity, net::Endpoint& from) {
    for (;;) {
        int n = m_socket.RecvFrom(buffer, capacity, from);
        if (n <= 0 || !m_relay) return n;
        if (!(from == m_relayEp)) continue;          // only the relay should reach us
        auto* b = static_cast<std::uint8_t*>(buffer);
        std::uint8_t tag = b[0];
        if (tag == net::RelayWelcome) {
            net::Packet p(b, static_cast<std::size_t>(n));
            p.ReadU8();
            std::uint32_t yourSlot = p.ReadU32();
            std::uint32_t hostSlot = p.ReadU32();
            if (p.Ok()) {
                m_relaySlot = yourSlot;
                m_relayWelcomed = true;
                if (m_mode == Mode::Client) m_serverEp = SlotEndpoint(hostSlot);
            }
            continue;
        }
        if (tag == net::RelayData) {
            const std::size_t payloadOff = 1 + 4;    // tag + srcSlot
            if (static_cast<std::size_t>(n) < payloadOff) continue;
            net::Packet hp(b, payloadOff);
            hp.ReadU8();
            std::uint32_t srcSlot = hp.ReadU32();
            std::size_t payload = static_cast<std::size_t>(n) - payloadOff;
            std::memmove(buffer, b + payloadOff, payload);
            from = SlotEndpoint(srcSlot);
            return static_cast<int>(payload);
        }
        // Unknown relay tag — ignore and keep reading.
    }
}

// Collect one fragment from `from`. Returns true (and fills `out` with the whole
// reassembled message) only once every fragment of that message has arrived.
bool NetworkManager::TakeFragment(const net::Endpoint& from, const std::uint8_t* data,
                                  std::size_t size, std::vector<std::uint8_t>& out) {
    const std::size_t header = 1 + 4 + 4 + 4;
    if (size < header) return false;
    net::Packet p(data, size);
    p.ReadU8();                                   // Fragment tag
    std::uint32_t id = p.ReadU32(), index = p.ReadU32(), count = p.ReadU32();
    if (!p.Ok() || count == 0 || count > 65535 || index >= count) return false;
    auto& asm_ = m_fragIn[from][id];
    if (asm_.chunks.empty()) { asm_.count = count; asm_.chunks.resize(count); }
    if (asm_.count != count) return false;        // inconsistent — drop this stray fragment
    if (asm_.chunks[index].empty()) {             // first time we see this index
        asm_.chunks[index].assign(data + header, data + size);
        asm_.got++;
    }
    asm_.age = 0.0f;
    if (asm_.got < asm_.count) return false;
    out.clear();
    for (auto& c : asm_.chunks) out.insert(out.end(), c.begin(), c.end());
    m_fragIn[from].erase(id);
    if (m_fragIn[from].empty()) m_fragIn.erase(from);
    return true;
}

// Discard half-assembled messages whose fragments stopped arriving, so a lost
// fragment can't leak memory forever.
void NetworkManager::PruneFragments(float dt) {
    for (auto epIt = m_fragIn.begin(); epIt != m_fragIn.end(); ) {
        for (auto it = epIt->second.begin(); it != epIt->second.end(); ) {
            it->second.age += dt;
            if (it->second.age > kFragmentTtl) it = epIt->second.erase(it);
            else ++it;
        }
        if (epIt->second.empty()) epIt = m_fragIn.erase(epIt);
        else ++epIt;
    }
}

void NetworkManager::Kick(std::uint32_t id, const std::string& reason) {
    if (m_mode != Mode::Server) return;
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        if (it->second.id != id) continue;
        net::Packet p(Kicked); p.Write(reason);
        SendDatagram(it->second.endpoint, p.Data(), p.Size());
        if (auto r = m_remotes.find(id); r != m_remotes.end()) {
            if (GetScene() && r->second) GetScene()->Destroy(r->second);
            m_remotes.erase(r);
        }
        m_clients.erase(it);
        if (m_peerLeft) m_peerLeft(id);
        OKAY_INFO("net: kicked peer ", id, reason.empty() ? "" : (" (" + reason + ")"));
        return;
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

std::string NetworkManager::PeerUserId(std::uint32_t id) const {
    for (auto& [ep, c] : m_clients) if (c.id == id) return c.userId;
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
                SendDatagram(c.endpoint, p.Data(), p.Size());
    } else if (m_mode == Mode::Client && m_joined) {
        net::Packet p(Message);
        p.Write(m_localId);
        p.Write(channel);
        p.Write(data);
        SendDatagram(m_serverEp, p.Data(), p.Size());
    }
}

void NetworkManager::ServerTick(float dt) {
    // A full-size receive buffer: a UDP datagram can be up to ~64 KB, so a small
    // fixed buffer silently truncated any larger message (e.g. a big reliable
    // payload). Reused across ticks (thread-local) to avoid per-frame allocation.
    RelayKeepAlive(dt);
    PruneFragments(dt);
    static thread_local std::vector<std::uint8_t> buf(net::kMaxDatagram);
    net::Endpoint from;
    int n;
    while ((n = ReceiveFrom(buf.data(), buf.size(), from)) > 0) {
        const std::uint8_t* data = buf.data();
        std::size_t len = static_cast<std::size_t>(n);
        std::vector<std::uint8_t> assembled;   // holds the message if this was a fragment
        if (len >= 1 && data[0] == Fragment) {
            if (!TakeFragment(from, data, len, assembled)) continue;  // wait for the rest
            data = assembled.data(); len = assembled.size();
        }
        std::vector<std::uint8_t> decrypted;   // holds the message if it was encrypted
        if (len >= 1 && data[0] == kEncEnvelope) {
            net::SecureChannel* s = SessionFor(from);
            if (!s || !s->Open(data + 1, len - 1, decrypted)) continue;  // can't open -> drop
            data = decrypted.data(); len = decrypted.size();
        }
        net::Packet p(data, len);
        std::uint8_t type = p.ReadU8();
        if (type == Join) {
            std::string name = p.ReadString();           // display name (may be empty)
            std::string room = p.ReadString();           // lobby room (may be empty)
            std::string pass = p.ReadString();           // password (may be empty)
            std::string token = p.ReadString();           // identity token (may be empty)
            std::string clientPk = p.ReadString();        // encryption public key (may be empty)
            bool existed = m_clients.count(from) != 0;
            std::string userId;
            if (!existed) {
                // Gate new joins on capacity, password and (if a verifier is set)
                // identity; refuse politely.
                const char* why = nullptr;
                if ((int)m_clients.size() >= maxPlayers) why = "server is full";
                else if (!password.empty() && pass != password) why = "wrong password";
                else if (m_verifyToken && !m_verifyToken(token, userId)) why = "authentication failed";
                if (why) {
                    net::Packet r(Reject); r.Write(std::string(why));
                    SendDatagram(from, r.Data(), r.Size(), /*allowEncrypt=*/false);
                    continue;
                }
            }
            auto& c = m_clients[from];
            bool isNew = (c.id == 0);
            if (isNew) {
                c.endpoint = from;
                c.id = m_nextId++;
                c.state = {c.id, 0, 0, 0, '@'};
                c.name = name.empty() ? ("Player" + std::to_string(c.id)) : name;
                c.room = room;
                c.userId = userId;
                // Establish this client's encrypted session from its public key.
                if (encryption && net::SecureChannel::Available() && !clientPk.empty()) {
                    c.secure.GenerateKeys();
                    c.secure.Establish(std::vector<std::uint8_t>(clientPk.begin(), clientPk.end()),
                                       /*asServer=*/true);
                }
            }
            c.lastSeen = 0.0f;
            net::Packet w(Welcome);
            w.Write(c.id);
            w.Write(serverName);
            w.Write(c.userId);   // tell the client its verified identity ("" if anon)
            {   // our public key so the client can establish its side ("" if none)
                auto pk = c.secure.PublicKey();
                w.Write(std::string(pk.begin(), pk.end()));
            }
            SendDatagram(from, w.Data(), w.Size(), /*allowEncrypt=*/false);  // pre-handshake
            if (isNew) {
                OKAY_INFO("net: client ", c.id, " '", c.name, "' joined from ", from.ToString());
                if (m_peerJoined) m_peerJoined(c.id, c.name);
                // Full sync: send every current synced variable to the newcomer.
                for (auto& [k, v] : m_syncVars) {
                    net::Packet sv(SyncVar); sv.Write(k); sv.Write(v);
                    SendDatagram(from, sv.Data(), sv.Size());
                }
            }
        } else if (type == SyncVar) {
            // A client requested a variable change; apply it and fan out to all.
            std::string key = p.ReadString();
            std::string value = p.ReadString();
            ApplySyncVar(key, value);
            net::Packet out(SyncVar); out.Write(key); out.Write(value);
            for (auto& [ep, c] : m_clients) SendDatagram(c.endpoint, out.Data(), out.Size());
        } else if (type == Ping) {
            // Echo the client's timestamp straight back so it can measure RTT.
            float stamp = p.ReadF32();
            net::Packet pong(Pong); pong.Write(stamp);
            SendDatagram(from, pong.Data(), pong.Size());
        } else if (type == Ready) {
            std::uint8_t r = p.ReadU8();
            auto it = m_clients.find(from);
            if (it != m_clients.end()) it->second.ready = (r != 0);
        } else if (type == ReliableMsg) {
            auto it = m_clients.find(from);
            if (it != m_clients.end()) {
                std::uint32_t seq = p.ReadU32();
                std::string channel = p.ReadString();
                std::string data    = p.ReadString();
                net::Packet ack(ReliableAck); ack.Write(seq);
                SendDatagram(from, ack.Data(), ack.Size());
                if (it->second.relSeen.insert(seq).second)   // first time -> deliver
                    Deliver(it->second.id, channel, data);
            }
        } else if (type == ReliableAck) {
            auto it = m_clients.find(from);
            if (it != m_clients.end()) it->second.relOut.erase(p.ReadU32());
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
                            SendDatagram(cl.endpoint, fwd.Data(), fwd.Size());
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
                it->second.hasState = true;   // we now know where this client is (for interest culling)
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
                        SendDatagram(c.endpoint, relay.Data(), relay.Size());
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
            // A lambda below needs the recipient; capturing a structured binding directly
            // is ill-formed before C++20 (Clang/Emscripten rejects it), so alias it to a
            // plain reference the closure can capture portably.
            auto& rc = recipient;
            const std::string& room = recipient.room;
            bool hostHere = (m_localRoom == room);
            // Interest culling needs the recipient's own position; until it has
            // reported one, hold its snapshots (a tick or two) so we never spawn a
            // far peer on it from a default origin.
            if (interestRadius > 0.0f && !recipient.hasState) continue;
            Vec3 rpos{recipient.state.x, recipient.state.y, recipient.state.z};

            // Build only the peers this recipient needs THIS tick: skip its own
            // avatar (interest), skip peers outside interestRadius (interest), and
            // skip peers whose state is unchanged since we last sent it (delta).
            std::vector<PeerState> include;
            auto consider = [&](const PeerState& s, bool known) {
                if (s.id == rc.id) return;                          // don't echo their own avatar
                if (interestRadius > 0.0f) {                        // area-of-interest cull
                    if (!known) return;                             // position unknown -> don't sync yet
                    float dx = s.x - rpos.x, dy = s.y - rpos.y, dz = s.z - rpos.z;
                    if (dx * dx + dy * dy + dz * dz > interestRadius * interestRadius) return;
                }
                auto it = rc.sentStates.find(s.id);                 // delta: only if changed
                bool changed = it == rc.sentStates.end() ||
                               std::fabs(it->second.x - s.x) > 1e-4f ||
                               std::fabs(it->second.y - s.y) > 1e-4f ||
                               std::fabs(it->second.z - s.z) > 1e-4f ||
                               it->second.glyph != s.glyph;
                if (!changed) return;
                rc.sentStates[s.id] = s;
                include.push_back(s);
            };
            if (hostHere) consider(PeerState{0, lp.x, lp.y, lp.z, m_localGlyph}, true);
            for (auto& [ep2, c] : m_clients) if (c.room == room) consider(c.state, c.hasState);
            if (include.empty()) continue;                          // nothing changed -> send nothing

            net::Packet snap(Snapshot);
            snap.Write(std::uint32_t(include.size()));
            for (const PeerState& s : include) {
                snap.Write(s.id);
                snap.Write(s.x); snap.Write(s.y); snap.Write(s.z);
                snap.Write(std::uint8_t(s.glyph));
            }
            SendDatagram(recipient.endpoint, snap.Data(), snap.Size());
        }
    }

    // Resend any unacked reliable messages to each client.
    for (auto& [ep, c] : m_clients) ResendReliable(c.relOut, c.endpoint, dt);
}

void NetworkManager::ClientTick(float dt) {
    m_clock += dt;
    RelayKeepAlive(dt);
    // Through a relay we don't know the host's (synthetic) endpoint until the relay
    // pairs us, so hold the join handshake until then (the recv loop below still
    // runs, so the relay's welcome is processed).
    bool awaitingRelay = m_relay && !m_relayWelcomed;
    if (!m_joined && !awaitingRelay) {
        m_joinTimer -= dt;
        if (m_joinTimer <= 0.0f) {
            net::Packet p(Join);
            p.Write(m_localName);
            p.Write(m_localRoom);
            p.Write(password);          // matched against the server's password
            p.Write(m_authToken);       // identity token the server may verify
            {   // our encryption public key ("" if encryption is off / unavailable)
                auto pk = (encryption ? m_secure.PublicKey() : std::vector<std::uint8_t>{});
                p.Write(std::string(pk.begin(), pk.end()));
            }
            SendDatagram(m_serverEp, p.Data(), p.Size(), /*allowEncrypt=*/false);  // pre-handshake
            m_joinTimer = 0.5f;
        }
    } else {
        // Ping the server about once a second to measure round-trip time.
        m_pingTimer -= dt;
        if (m_pingTimer <= 0.0f) {
            net::Packet p(Ping); p.Write(m_clock);
            SendDatagram(m_serverEp, p.Data(), p.Size());
            m_pingTimer = 1.0f;
        }
        ResendReliable(m_relOut, m_serverEp, dt);   // keep retrying unacked messages
    }

    PruneFragments(dt);
    static thread_local std::vector<std::uint8_t> buf(net::kMaxDatagram);
    net::Endpoint from;
    int n;
    while ((n = ReceiveFrom(buf.data(), buf.size(), from)) > 0) {
        const std::uint8_t* data = buf.data();
        std::size_t len = static_cast<std::size_t>(n);
        std::vector<std::uint8_t> assembled;
        if (len >= 1 && data[0] == Fragment) {
            if (!TakeFragment(from, data, len, assembled)) continue;
            data = assembled.data(); len = assembled.size();
        }
        std::vector<std::uint8_t> decrypted;
        if (len >= 1 && data[0] == kEncEnvelope) {
            net::SecureChannel* s = SessionFor(from);
            if (!s || !s->Open(data + 1, len - 1, decrypted)) continue;
            data = decrypted.data(); len = decrypted.size();
        }
        net::Packet p(data, len);
        std::uint8_t type = p.ReadU8();
        if (type == Welcome) {
            m_localId = p.ReadU32();
            m_serverName = p.ReadString();
            m_localUserId = p.ReadString();   // our verified identity ("" if anon)
            std::string serverPk = p.ReadString();   // server's encryption public key
            if (encryption && net::SecureChannel::Available() && !serverPk.empty() && !m_secure.Ready())
                m_secure.Establish(std::vector<std::uint8_t>(serverPk.begin(), serverPk.end()),
                                   /*asServer=*/false);
            m_joined = true;
            m_joinRejected = false;
            OKAY_INFO("net: joined '", m_serverName, "' as peer ", m_localId);
        } else if (type == Reject) {
            std::string why = p.ReadString();
            m_joinRejected = true;
            m_joinTimer = 1e9f;      // stop retrying
            OKAY_WARN("net: join refused: ", why);
        } else if (type == Kicked) {
            m_kickReason = p.ReadString();
            m_kicked = true;
            OKAY_WARN("net: kicked by server: ", m_kickReason);
            Stop();
            return;
        } else if (type == ReliableMsg) {
            std::uint32_t seq = p.ReadU32();
            std::string channel = p.ReadString();
            std::string data    = p.ReadString();
            net::Packet ack(ReliableAck); ack.Write(seq);
            SendDatagram(m_serverEp, ack.Data(), ack.Size());
            if (m_relSeen.insert(seq).second) Deliver(0, channel, data);
        } else if (type == ReliableAck) {
            m_relOut.erase(p.ReadU32());
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
        SendDatagram(m_serverEp, p.Data(), p.Size());
    }
}

} // namespace okay

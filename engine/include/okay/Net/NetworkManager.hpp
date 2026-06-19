#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Net/UdpSocket.hpp"
#include "okay/Math/Vec3.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace okay {

class Transform;
class GameObject;

/// Authoritative-relay multiplayer over UDP. One peer runs as the server; the
/// rest connect as clients. Each peer owns a "local avatar" Transform whose
/// position it broadcasts; the manager mirrors every other peer into the scene
/// via a spawn factory and keeps their Transforms in sync each frame.
///
/// This is intentionally small but real: it opens sockets, performs a join
/// handshake, serializes state, and reconciles a snapshot — the backbone every
/// networked game needs.
class NetworkManager : public Behaviour {
public:
    enum class Mode { Offline, Server, Client };

    /// Begin listening for clients on the given UDP port.
    bool StartServer(std::uint16_t port);
    /// Connect to a server at host:port (host may be a name or dotted IP).
    bool StartClient(const std::string& host, std::uint16_t port);
    void Stop();

    Mode mode() const { return m_mode; }
    bool IsServer() const { return m_mode == Mode::Server; }
    bool IsClient() const { return m_mode == Mode::Client; }
    /// This peer's network id (0 for the server, >0 for clients once joined).
    std::uint32_t LocalId() const { return m_localId; }
    /// The UDP port the server is bound to (0 if not a server).
    std::uint16_t ServerPort() const { return m_socket.LocalPort(); }
    /// True once a client has completed the join handshake.
    bool Joined() const { return m_joined; }
    /// Number of remote peers currently mirrored into the scene.
    std::size_t PeerCount() const { return m_remotes.size(); }

    /// The Transform whose state this peer broadcasts (e.g. its player).
    void SetLocalAvatar(Transform* t, char glyph = '@') {
        m_localAvatar = t; m_localGlyph = glyph;
    }

    /// This peer's display name, sent to the server on join and shown in the
    /// roster (defaults to "Player").
    void SetLocalName(const std::string& name) { m_localName = name; }
    const std::string& LocalName() const { return m_localName; }

    // ---- No-code setup: start automatically when the scene plays ------
    enum class AutoStart { None, Host, Join };
    AutoStart   autoStart = AutoStart::None;
    std::uint16_t autoPort = 45000;
    std::string autoHost = "127.0.0.1";
    std::string startName;          // local name to use on auto-start (optional)

    /// On scene Start: act on `autoStart` (host or join) so multiplayer needs no
    /// script — add the component, pick Host/Join in the Inspector, press Play.
    void Start() override;

    // ---- Synced variables (server-authoritative shared state) ---------
    /// Set a shared variable. On the server it applies and broadcasts to every
    /// client; on a client it asks the server, which applies and re-broadcasts.
    /// Great for scores, game phase, who's turn it is — one source of truth.
    void SetVar(const std::string& key, const std::string& value);
    /// Read the local copy of a synced variable ("" if unset).
    std::string GetVar(const std::string& key) const;
    /// Called when a new remote peer appears; return a GameObject to represent
    /// it (its Transform will be driven by incoming snapshots).
    void SetRemoteFactory(std::function<GameObject*(std::uint32_t id, char glyph)> f) {
        m_spawnRemote = std::move(f);
    }

    // ---- Custom messages (RPC / events / chat) -------------------------
    /// A custom message received from a peer: who sent it, a `channel` tag the
    /// game chooses (e.g. "chat", "spawn", "hit"), and an opaque string payload.
    struct NetMessage {
        std::uint32_t from;
        std::string   channel;
        std::string   data;
    };

    /// Broadcast a custom message to every other peer. On a client it travels to
    /// the server, which relays it to all the others (and delivers it to the
    /// host); on the server it goes straight out to every client. This is the
    /// general-purpose channel games build chat, events and spawn requests on.
    void Send(const std::string& channel, const std::string& data);

    /// Send a custom message to a single peer by id (0 = the server/host). The
    /// server routes it; a client relays through the server. Use it for private
    /// messages, targeted RPCs, or replying to one player.
    void SendTo(std::uint32_t targetId, const std::string& channel, const std::string& data);

    /// Spawn a prefab on **every** peer (bullets, pickups, effects). Instantiates
    /// it locally now and tells the others to instantiate the same prefab at the
    /// same position — replicated object creation with one call.
    void Spawn(const std::string& prefabPath, const Vec3& position = Vec3::Zero);

    /// A connected peer in the session roster.
    struct PeerInfo { std::uint32_t id; std::string name; char glyph; };
    /// The current roster the server knows about (server only): every connected
    /// client. Empty on a client.
    std::vector<PeerInfo> Peers() const;
    /// The display name a peer joined with (server only), or "" if unknown.
    std::string PeerName(std::uint32_t id) const;

    /// Register a callback fired the moment a custom message arrives.
    void SetMessageHandler(std::function<void(const NetMessage&)> f) {
        m_msgHandler = std::move(f);
    }
    /// Fired on the server when a client joins (with its id + chosen name).
    void SetPeerJoinedHandler(std::function<void(std::uint32_t, const std::string&)> f) {
        m_peerJoined = std::move(f);
    }
    /// Fired on the server when a client leaves or times out.
    void SetPeerLeftHandler(std::function<void(std::uint32_t)> f) {
        m_peerLeft = std::move(f);
    }
    /// Drain the queue of messages received since the last call (for polling
    /// code and scripts that don't use a callback).
    std::vector<NetMessage> TakeMessages() {
        std::vector<NetMessage> out;
        out.swap(m_inbox);
        return out;
    }
    /// Pop the oldest unread message into `out`; false if the inbox is empty.
    /// Lets scripts drain one message at a time in a while-loop.
    bool PopMessage(NetMessage& out) {
        if (m_inbox.empty()) return false;
        out = m_inbox.front();
        m_inbox.erase(m_inbox.begin());
        return true;
    }
    bool HasMessages() const { return !m_inbox.empty(); }
    bool IsConnected() const { return m_mode != Mode::Offline; }

    void Update(float dt) override;
    void OnDestroy() override { Stop(); }

private:
    struct PeerState { std::uint32_t id; float x, y, z; char glyph; };
    struct Client {
        net::Endpoint endpoint;
        std::uint32_t id;
        PeerState state;
        float lastSeen;
        std::string name;
    };

    void ServerTick(float dt);
    void ClientTick(float dt);
    GameObject* EnsureRemote(std::uint32_t id, char glyph);
    void ApplyPeer(const PeerState& s);
    /// Deliver a received custom message to the handler and the inbox queue.
    void Deliver(std::uint32_t from, const std::string& channel, const std::string& data);

    Mode m_mode = Mode::Offline;
    net::UdpSocket m_socket;
    bool m_netStarted = false;

    // Local peer
    Transform* m_localAvatar = nullptr;
    char       m_localGlyph  = '@';
    std::uint32_t m_localId  = 0;
    std::string m_localName  = "Player";

    // Client-only
    net::Endpoint m_serverEp{};
    bool     m_joined = false;
    float    m_joinTimer = 0.0f;

    // Server-only
    std::unordered_map<net::Endpoint, Client, net::EndpointHash> m_clients;
    std::uint32_t m_nextId = 1;
    float m_snapshotTimer = 0.0f;
    float m_snapshotInterval = 0.05f; // 20 Hz

    // Remotes mirrored into the scene
    std::function<GameObject*(std::uint32_t, char)> m_spawnRemote;
    std::unordered_map<std::uint32_t, GameObject*>  m_remotes;

    // Custom message channel
    std::function<void(const NetMessage&)> m_msgHandler;
    std::vector<NetMessage> m_inbox;
    std::function<void(std::uint32_t, const std::string&)> m_peerJoined;
    std::function<void(std::uint32_t)> m_peerLeft;

    // Server-authoritative synced variables
    std::unordered_map<std::string, std::string> m_syncVars;
    void ApplySyncVar(const std::string& key, const std::string& value);
};

} // namespace okay

#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Net/UdpSocket.hpp"
#include "okay/Net/SecureChannel.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Quat.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
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

    // ---- Relay (NAT traversal) ----------------------------------------
    /// Host a session *through a relay* instead of binding a public port. Both
    /// the host and its clients connect outward to the same relay and announce a
    /// shared `code`; the relay forwards traffic between them. This is what lets
    /// players behind home routers / NATs play together with no port-forwarding.
    /// The relay never sees plaintext — encryption (if enabled) still runs end to
    /// end. Run a relay with the `okayspace-relay` binary.
    bool HostViaRelay(const std::string& relayHost, std::uint16_t relayPort,
                      const std::string& code);
    /// Join a relay-hosted session by the same code the host used.
    bool JoinViaRelay(const std::string& relayHost, std::uint16_t relayPort,
                      const std::string& code);
    /// True while this peer is routing through a relay (host or client).
    bool Relayed() const { return m_relay; }
    /// True once the relay has paired this peer (assigned a slot / host wired up).
    bool RelayReady() const { return m_relay && m_relayWelcomed; }


    Mode mode() const { return m_mode; }
    bool IsServer() const { return m_mode == Mode::Server; }
    bool IsClient() const { return m_mode == Mode::Client; }
    /// This peer's network id (0 for the server, >0 for clients once joined).
    std::uint32_t LocalId() const { return m_localId; }

    /// How fast remote avatars are eased toward their latest networked position
    /// (per second; 0 = snap instantly). Smooths the ~20 Hz snapshots into fluid
    /// motion at the local frame rate.
    float interpolationRate = 12.0f;

    /// Spatial interest management: a client is only sent peers within this many
    /// world units of its own avatar (0 = no culling — send everyone in the room).
    /// Trims bandwidth in large worlds; an out-of-range peer simply stops updating
    /// on that client until it comes back into range.
    float interestRadius = 0.0f;

    /// Round-trip time to the server in milliseconds (clients only; 0 until the
    /// first ping completes). A simple latency readout for HUDs / netcode.
    float RttMs() const { return m_rtt; }
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

    // ---- Identity (authenticated join) --------------------------------
    /// The auth token this peer presents when joining (e.g. a Supabase access
    /// token). Set it before joining; the server can verify it to bind the peer to
    /// a real account. "" = anonymous.
    void SetAuthToken(const std::string& token) { m_authToken = token; }
    const std::string& AuthToken() const { return m_authToken; }

    /// Server: a hook that verifies each joining client's token. Return true and set
    /// `outUserId` to accept (binding the peer to that identity); return false to
    /// reject the join with "authentication failed". If unset, joins are NOT
    /// authenticated (an open server — the default, unchanged behavior). The engine
    /// never talks to an auth backend itself: the host supplies this (e.g. verifying
    /// a Supabase token via auth/v1/user), so the net core stays dependency-free.
    using TokenVerifier = std::function<bool(const std::string& token, std::string& outUserId)>;
    void SetTokenVerifier(TokenVerifier v) { m_verifyToken = std::move(v); }

    /// Encrypt traffic. When true (and the engine is built with libsodium), peers
    /// exchange public keys during the join handshake and all post-handshake
    /// datagrams are sealed with XChaCha20-Poly1305 (confidentiality + integrity).
    /// Set it on BOTH ends before host/join. If either end lacks encryption support,
    /// the session falls back to plaintext, so it's safe to leave on. Default off.
    bool encryption = false;
    /// True once an encrypted session is established (client: with the server;
    /// server: reported per-peer via PeerEncrypted). Handy for a HUD lock icon.
    bool Encrypted() const;
    /// The verified account id a peer joined with (server only); "" if anonymous.
    std::string PeerUserId(std::uint32_t id) const;
    /// This peer's own verified id once joined (client), or "" if anonymous.
    const std::string& LocalUserId() const { return m_localUserId; }

    /// The "room" (lobby) this peer is in. Peers only see avatars and receive
    /// broadcast messages from others in the *same* room, so one server can host
    /// many independent matches. Set it before joining. Default room is "".
    void SetRoom(const std::string& room) { m_localRoom = room; }
    const std::string& Room() const { return m_localRoom; }

    // ---- No-code setup: start automatically when the scene plays ------
    enum class AutoStart { None, Host, Join };
    AutoStart   autoStart = AutoStart::None;
    std::uint16_t autoPort = 45000;
    std::string autoHost = "127.0.0.1";
    std::string startName;          // local name to use on auto-start (optional)
    std::string startRoom;          // lobby room to join on auto-start (optional)

    // ---- Host settings (used when this peer is the server) ------------
    int         maxPlayers = 8;     // reject joins beyond this many clients
    float       snapshotRate = 20.0f; // state broadcasts per second (tick rate)
    std::string serverName;         // friendly server label, sent to clients
    std::string password;           // clients must match to join ("" = open)

    /// The server's friendly name (clients learn it from the join handshake).
    const std::string& ServerName() const { return m_serverName; }
    /// True on a client if the last join was refused (server full / bad password).
    bool JoinRejected() const { return m_joinRejected; }

    /// On scene Start: act on `autoStart` (host or join) so multiplayer needs no
    /// script — add the component, pick Host/Join in the Inspector, press Play.
    void Start() override;

    // ---- Lobby: ready-up and start-match ------------------------------
    /// Mark this peer ready / not ready (clients tell the server). The host can
    /// then poll the lobby and start the match when everyone's set.
    void SetReady(bool ready);
    bool IsReady() const { return m_ready; }
    /// Server only: how many clients in the host's room are ready.
    int  ReadyCount() const;
    /// Server only: true when there's at least one client in the room and every
    /// client in it is ready — the cue to start.
    bool AllReady() const;
    /// Server only: tell everyone in the room the match has begun (sets
    /// MatchStarted() on every peer in the room).
    void StartMatch();
    /// True once the match has been started for this peer's room.
    bool MatchStarted() const { return m_matchStarted; }

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

    // ---- Networked transforms (drop-in object replication) -------------
    /// Replicate a Transform across the whole session under a unique `id`. The peer
    /// that owns it (`owned` = true) broadcasts its position + rotation `syncRate`
    /// times a second; every other peer eases its local copy toward the latest
    /// received state (using `interpolationRate`). This is what the NetworkSync
    /// component is built on, so ANY object can replicate with no networking code —
    /// just give matching ids on each peer and flag the authority as the owner.
    void RegisterSync(const std::string& id, Transform* t, bool owned);
    /// Stop replicating the object registered under `id`.
    void UnregisterSync(const std::string& id);
    /// Change who is authoritative for `id` at runtime (ownership transfer).
    void SetSyncOwned(const std::string& id, bool owned);
    /// True if a transform is currently registered under `id`.
    bool HasSync(const std::string& id) const { return m_syncObjs.count(id) != 0; }
    float syncRate = 15.0f;   ///< networked-transform broadcasts per second (0 = every frame)

    // ---- Reliable messaging (guaranteed delivery + de-dup over UDP) ----
    /// Like Send(), but resent until the other end acknowledges it and de-duped
    /// on arrival — for events you can't afford to drop (deaths, score, "go!").
    /// On a client it targets the server; on the server it reliably reaches every
    /// client.
    void SendReliable(const std::string& channel, const std::string& data);
    /// Reliable message to a single peer id (server routes; 0 = the host).
    void SendReliableTo(std::uint32_t targetId, const std::string& channel, const std::string& data);

    // ---- Remote procedure calls (named events) -------------------------
    /// A first-class RPC: every peer that registered a handler for `name` runs it
    /// with the sender id + payload. Built on the message system but dispatched by
    /// name to a callback (no manual channel parsing). Broadcast to everyone else.
    void Rpc(const std::string& name, const std::string& data = "") { Send("__rpc/" + name, data); }
    /// RPC to a single peer id (0 = the server/host).
    void RpcTo(std::uint32_t targetId, const std::string& name, const std::string& data = "") {
        SendTo(targetId, "__rpc/" + name, data);
    }
    /// RPC that is guaranteed to arrive (resent until acked) — for one-off events
    /// you can't drop (a death, "round start", a door opening).
    void RpcReliable(const std::string& name, const std::string& data = "") { SendReliable("__rpc/" + name, data); }
    /// Register the handler that runs when an RPC named `name` arrives. The callback
    /// gets (senderId, payload). Replaces any previous handler for that name.
    void OnRpc(const std::string& name, std::function<void(std::uint32_t, const std::string&)> f) {
        m_rpcHandlers[name] = std::move(f);
    }
    /// Remove a previously registered RPC handler.
    void ClearRpc(const std::string& name) { m_rpcHandlers.erase(name); }

    // ---- Chat ----------------------------------------------------------
    /// One chat line: who said it (peer id + the name they joined with) and the text.
    struct ChatEntry { std::uint32_t from; std::string name; std::string text; };
    /// Send a chat line to everyone (reliable, so it isn't dropped). It's stamped
    /// with your name and added to your own log too.
    void Chat(const std::string& text);
    /// The rolling chat history (most recent last, capped at `chatHistory`).
    const std::vector<ChatEntry>& ChatLog() const { return m_chatLog; }
    /// Clear the local chat history.
    void ClearChat() { m_chatLog.clear(); }
    /// Fired the moment a chat line arrives (for live UIs / sounds).
    void OnChat(std::function<void(const ChatEntry&)> f) { m_chatHandler = std::move(f); }
    int chatHistory = 100;   ///< how many chat lines to keep

    // ---- Moderation ----------------------------------------------------
    /// Server: disconnect a client by id (with an optional reason it's told).
    void Kick(std::uint32_t id, const std::string& reason = "");
    /// Client: true if the server kicked this peer, plus the reason it gave.
    bool WasKicked() const { return m_kicked; }
    const std::string& KickReason() const { return m_kickReason; }

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
        std::string room;
        std::string userId;   // verified identity (from the token verifier), or ""
        net::SecureChannel secure;   // per-client encrypted session (if negotiated)
        std::unordered_map<std::uint32_t, PeerState> sentStates;  // last state sent (delta snapshots)
        bool hasState = false;   // true once this client has reported its position
        bool ready = false;
        // Per-client reliable channel (server side).
        std::uint32_t relSeq = 0;   // next outgoing sequence to this client
        std::unordered_map<std::uint32_t, std::pair<std::vector<std::uint8_t>, float>> relOut; // unacked
        std::unordered_set<std::uint32_t> relSeen;  // delivered seqs (de-dup)
    };

    void ResendReliable(std::unordered_map<std::uint32_t, std::pair<std::vector<std::uint8_t>, float>>& out,
                        const net::Endpoint& to, float dt);

    // Datagram fragmentation: SendDatagram splits an oversized message into
    // MTU-sized pieces; TakeFragment reassembles them on the far side; PruneFragments
    // discards half-assembled messages whose fragments stopped arriving.
    void SendDatagram(const net::Endpoint& to, const std::uint8_t* data, std::size_t size,
                      bool allowEncrypt = true);
    // Physical I/O, relay-aware. Transmit either sends straight to `to` (direct
    // mode) or wraps the bytes in a relay frame addressed by `to`'s slot and sends
    // them to the relay. ReceiveFrom transparently consumes relay control frames
    // and hands back application payloads with a synthetic slot-endpoint as `from`,
    // so the server/client logic above is identical in both modes.
    void Transmit(const net::Endpoint& to, const std::uint8_t* data, std::size_t size);
    int  ReceiveFrom(void* buffer, std::size_t capacity, net::Endpoint& from);
    void RelayKeepAlive(float dt);   // re-announce to the relay so the NAT stays open
    bool StartRelay(const std::string& relayHost, std::uint16_t relayPort,
                    const std::string& code, bool asHost);
    /// The encrypted session for an endpoint (client: the server; server: that
    /// client), or nullptr if none / not established.
    net::SecureChannel* SessionFor(const net::Endpoint& ep);
    struct FragAsm {
        std::uint32_t count = 0;
        std::uint32_t got   = 0;
        std::vector<std::vector<std::uint8_t>> chunks;
        float age = 0.0f;
    };
    bool TakeFragment(const net::Endpoint& from, const std::uint8_t* data,
                      std::size_t size, std::vector<std::uint8_t>& out);
    void PruneFragments(float dt);
    std::unordered_map<net::Endpoint, std::unordered_map<std::uint32_t, FragAsm>, net::EndpointHash> m_fragIn;
    std::uint32_t m_fragNextId = 0;

    void ServerTick(float dt);
    void ClientTick(float dt);
    GameObject* EnsureRemote(std::uint32_t id, char glyph);
    void ApplyPeer(const PeerState& s);
    /// Deliver a received custom message to the handler and the inbox queue.
    void Deliver(std::uint32_t from, const std::string& channel, const std::string& data);

    Mode m_mode = Mode::Offline;
    net::UdpSocket m_socket;
    bool m_netStarted = false;

    // Relay routing (NAT traversal). When m_relay is set, every physical send goes
    // to m_relayEp framed with a slot id, and every peer is keyed by a synthetic
    // Endpoint{slot} so the rest of the code is unchanged.
    bool m_relay = false;
    bool m_relayWelcomed = false;     // relay has assigned our slot / wired the host
    net::Endpoint m_relayEp{};        // the relay's real address
    std::string m_relayCode;          // shared session code
    std::uint32_t m_relaySlot = 0;    // our slot (set on welcome)
    float m_relayHelloTimer = 0.0f;   // re-hello cadence (NAT keepalive)

    // Local peer
    Transform* m_localAvatar = nullptr;
    char       m_localGlyph  = '@';
    std::uint32_t m_localId  = 0;
    std::string m_localName  = "Player";
    net::SecureChannel m_secure; // client: encrypted session with the server
    std::string m_authToken;     // token presented on join ("" = anonymous)
    std::string m_localUserId;   // client: our own verified id once joined
    TokenVerifier m_verifyToken; // server: verifies joining clients' tokens
    std::string m_localRoom;     // "" = default room
    bool m_ready = false;        // this peer's ready flag
    bool m_matchStarted = false; // set when StartMatch reaches this peer's room
    std::string m_serverName;    // client: the server's friendly name
    bool m_joinRejected = false; // client: server refused us (full / password)
    bool m_kicked = false;       // client: kicked by the server
    std::string m_kickReason;
    // Client-side reliable channel (to the server).
    std::uint32_t m_relSeq = 0;
    std::unordered_map<std::uint32_t, std::pair<std::vector<std::uint8_t>, float>> m_relOut;
    std::unordered_set<std::uint32_t> m_relSeen;

    // Client-only
    net::Endpoint m_serverEp{};
    bool     m_joined = false;
    float    m_joinTimer = 0.0f;
    float    m_clock = 0.0f;      // local seconds, for ping timestamps
    float    m_pingTimer = 0.0f;
    float    m_rtt = 0.0f;        // last measured round-trip, ms

    // Smooth remote avatars toward their latest received position.
    std::unordered_map<std::uint32_t, Vec3> m_remoteTarget;
    void InterpolateRemotes(float dt);

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

    // Networked transforms (drop-in object replication via NetworkSync).
    struct SyncObj {
        Transform* t = nullptr;
        bool  owned = false;
        Vec3  targetPos{};
        Quat  targetRot{};
        bool  hasTarget = false;
    };
    std::unordered_map<std::string, SyncObj> m_syncObjs;
    float m_syncTimer = 0.0f;
    void SyncTick(float dt);

    // First-class RPC handlers (by name) + chat log/handler.
    std::unordered_map<std::string, std::function<void(std::uint32_t, const std::string&)>> m_rpcHandlers;
    std::vector<ChatEntry> m_chatLog;
    std::function<void(const ChatEntry&)> m_chatHandler;

    // Server-authoritative synced variables
    std::unordered_map<std::string, std::string> m_syncVars;
    void ApplySyncVar(const std::string& key, const std::string& value);
};

} // namespace okay

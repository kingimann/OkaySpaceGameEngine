#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Net/UdpSocket.hpp"
#include "okay/Net/SecureChannel.hpp"
#include "okay/Net/AntiCheat.hpp"
#include "okay/Net/ClockCheck.hpp"
#include "okay/Net/PlayerInput.hpp"
#include "okay/Physics/Physics3D.hpp"   // RaycastHit3D (RewindRaycast)
#include <map>
#include "okay/Math/Vec3.hpp"
#include "okay/Math/Quat.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <algorithm>
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

    NetworkManager();
    ~NetworkManager() override;
    /// True while `nm` points at a NetworkManager that hasn't been destroyed —
    /// for code that kept a pointer and must not touch a dead one.
    static bool IsLive(const NetworkManager* nm);
    /// Server ticks run so far (per-tick caches key off this).
    std::uint64_t TickCount() const { return m_tickCount; }

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
    /// The relay session code ("" when not relayed).
    const std::string& RelayCode() const { return m_relayCode; }
    /// Why the relay ended this peer's session, or "" if it didn't. A client
    /// disconnects when its host leaves or changes; a host stops when the relay
    /// refuses it (the code is hosted by someone else, or the versions don't
    /// match). It stays readable after the disconnect, until the next session.
    const std::string& RelayError() const { return m_relayError; }
    /// "a.b.c.d:port" of the relay in use ("" when not relayed).
    std::string RelayAddress() const;
    /// Client (direct): "a.b.c.d:port" of the server we connect to, else "".
    std::string ServerAddress() const;
    /// Players in the session: the server counts its clients (+1 for itself unless
    /// dedicated); a joined client counts the peers it sees + itself.
    int PlayerCount() const;

    /// Connect using a join link (okay://join?..., see okay/Net/JoinLink.hpp):
    /// direct or through a relay, into its room, with its password.
    bool JoinByLink(const std::string& link);

    // ---- Launcher hand-off (Steam's "+connect") --------------------------
    /// A session the game was launched (or asked by the launcher) to join. The
    /// next NetworkManager to Start() — or one already running, via
    /// TakeLaunchJoin() — joins it instead of its own auto-start.
    static void SetLaunchJoin(const std::string& link);
    static std::string TakeLaunchJoin();
    /// True if this run was started to join someone (scripts: net_launch_join()).
    static bool LaunchedToJoin();
    /// The account token / name every NetworkManager presents unless it was given
    /// its own (the player sets these from the signed-in OkaySpace account).
    static void SetDefaultIdentity(const std::string& token, const std::string& name);

    // ---- Server authority (basic anti-cheat) ----------------------------
    /// Server: the fastest a client's avatar may move (world units per second;
    /// 0 = no check). A faster move is rejected and the client is snapped back to
    /// its last good position. Use ServerTeleport() for legit jumps (respawns).
    float maxMoveSpeed = 0.0f;
    /// Server: custom messages a client may send per second (0 = unlimited);
    /// extra ones are dropped (reliable ones are still acknowledged).
    int maxMessagesPerSecond = 0;
    /// Server: kick a client after this many rejected moves within a minute (0 = never).
    int kickAfterViolations = 0;
    /// Server: move a peer's avatar (0 = the host's own) — accepted by the move
    /// check and pushed to that client.
    void ServerTeleport(std::uint32_t id, const Vec3& pos);
    /// Server: rejected moves from a peer in the last minute.
    int PeerViolations(std::uint32_t id) const;
    /// Client: how many times the server corrected our position.
    std::uint32_t Corrections() const { return m_corrections; }

    // ---- Anti-cheat (opt-in; see okay/Net/AntiCheat.hpp) ------------------
    /// Server: what clients may send. Off by default (set
    /// anticheat.settings.enabled, e.g. from a parsed .okayac); with it off
    /// nothing is checked and nothing is sent differently.
    AntiCheat anticheat;
    /// Server: called for every message anti-cheat refuses.
    void SetCheatHandler(std::function<void(const CheatReport&)> f) { m_cheatHandler = std::move(f); }
    const std::function<void(const CheatReport&)>& CheatHandler() const { return m_cheatHandler; }

    // ---- Scores + roster (every peer sees who's in the room) -------------
    /// Server only: set a peer's score (0 = the host). Clients can't change scores.
    void SetPeerScore(std::uint32_t id, int score);
    /// Any peer: a player's score (clients read it from the roster).
    int PeerScore(std::uint32_t id) const;
    struct RosterEntry { std::uint32_t id = 0; std::string name, userId; int score = 0; float seconds = 0.0f; };
    /// Who's in the session: on the server every client (+ itself unless
    /// dedicated); on a client everyone in its room, refreshed each second.
    std::vector<RosterEntry> Roster() const;

    // ---- Matches (server) --------------------------------------------------
    /// Server: start a match — every score goes back to 0 and the match clock starts.
    void BeginMatch();
    /// Server: stop the match clock; returns how long the match lasted (seconds).
    /// Report the results with okay::MatchReport::FromRoster(Roster(), ...).
    float EndMatch();
    bool  InMatch() const { return m_matchOn; }
    /// Seconds since BeginMatch() (since the server started if no match was begun).
    float MatchSeconds() const { return m_matchOn || m_matchEnded ? (m_matchOn ? m_serverTime : m_matchEndAt) - m_matchStartAt : m_serverTime; }

    // ---- What a server browser sees (answered to queries without joining) ----
    std::string gameId;    ///< which game this is (the player fills in its title)
    std::string mapName;   ///< current map / scene name


    Mode mode() const { return m_mode; }
    bool IsServer() const { return m_mode == Mode::Server; }
    bool IsClient() const { return m_mode == Mode::Client; }
    /// This peer's network id (0 for the server, >0 for clients once joined).
    std::uint32_t LocalId() const { return m_localId; }

    /// How fast remote avatars are eased toward their latest networked position
    /// (per second; 0 = snap instantly). Smooths the ~20 Hz snapshots into fluid
    /// motion at the local frame rate. With a fixed net tick in force, synced
    /// objects (NetworkSync) use an interpolation buffer instead: they're shown
    /// 3 ticks in the past (100 ms at 30 Hz; longer while states arrive later or
    /// further apart than that allows), between the two states either side of
    /// that moment, so they move at the speed they really moved whatever the
    /// packet timing. interpolationRate 0 still snaps.
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
    /// Send the auth token to any host. Off (the default): before joining, the
    /// client asks the host whether it checks sign-ins (the server browser's
    /// "auth" flag), and sends the token only if it does, so a game hosted by
    /// someone else never sees your account's token for nothing. A host that
    /// doesn't answer the question (an older one) gets no token.
    bool authTokenToAnyHost = false;
    /// Client: whether the host said it checks sign-ins (false until it answers).
    bool HostWantsAuth() const { return m_hostWantsAuth; }

    /// Server: a hook that verifies each joining client's token. Return true and set
    /// `outUserId` to accept (binding the peer to that identity); return false to
    /// reject the join with "authentication failed". If unset, joins are NOT
    /// authenticated (an open server — the default, unchanged behavior). The engine
    /// never talks to an auth backend itself: the host supplies this (e.g. verifying
    /// a Supabase token via auth/v1/user), so the net core stays dependency-free.
    using TokenVerifier = std::function<bool(const std::string& token, std::string& outUserId)>;
    void SetTokenVerifier(TokenVerifier v) { m_verifyToken = std::move(v); }

    /// Server: like SetTokenVerifier, but for checks that take time (a web request)
    /// so they never stall the network tick. Return 1 to accept (set `outUserId`),
    /// 0 to reject, or -1 while the answer is still pending: the join is ignored
    /// for now and the client's next join retry (every half second) asks again.
    /// Takes precedence over SetTokenVerifier when both are set.
    using JoinCheck = std::function<int(const std::string& token, std::string& outUserId)>;
    void SetJoinCheck(JoinCheck c) { m_joinCheck = std::move(c); }

    /// Dedicated (headless) server: the server is not a player itself. It has no
    /// avatar in anyone's snapshots and it hears messages from EVERY room, so one
    /// server process can host many matches and run their game logic.
    bool dedicated = false;

    /// Encrypt traffic. When true (and the engine is built with libsodium), peers
    /// exchange public keys during the join handshake and all post-handshake
    /// datagrams are sealed with XChaCha20-Poly1305 (confidentiality + integrity).
    /// Set it on BOTH ends before host/join. If either end lacks encryption support,
    /// the session falls back to plaintext, so it's safe to leave on. Default off.
    bool encryption = false;
    /// True once an encrypted session is established (client: with the server;
    /// server: reported per-peer via PeerEncrypted). Handy for a HUD lock icon.
    bool Encrypted() const;
    /// Client: the session numbers its packets and refuses replays (an
    /// anti-cheat server that requires encryption turns this on).
    bool ReplayProtected() const { return m_mode == Mode::Client && m_secure.Ready() && m_secure.Sequenced(); }
    /// Client: the join itself (name, password, token) went sealed. With
    /// encryption on, the client first trades keys with the server (a
    /// versioned key exchange) and sends its Join encrypted; an older server
    /// that doesn't answer gets the plaintext Join after a few tries.
    bool SealedJoin() const { return m_mode == Mode::Client && m_keyed; }

    /// Client: pin the host's identity, the base64url ed25519 public key a
    /// server prints from HostIdentityPublicKey(). With a pin the client joins
    /// only a server that signs the key exchange with the matching key, and
    /// never falls back to a plaintext join. Needs encryption on. "" = no pin.
    std::string pinnedHostKey;
    /// Server: the ed25519 identity seed (base64url, 32 bytes) it signs its key
    /// exchange with, so pinned clients can tell it from an impostor. Keep it
    /// secret (the dedicated server reads it from --host-key FILE). False for a
    /// malformed seed; "" clears it.
    bool SetHostIdentity(const std::string& seedB64);
    /// The public key that goes with SetHostIdentity (base64url), or "".
    std::string HostIdentityPublicKey() const;
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
    /// A fixed network tick, in Hz (30 is a good rate). 0 (the default): as
    /// before, snapshots go at snapshotRate and clients send their state every
    /// frame. Above 0, on the host: a tick counter steps at exactly this rate
    /// whatever the frame rate, every snapshot carries the tick and goes out
    /// once a tick, and the Welcome tells clients the rate, so they send their
    /// state once a tick instead of once a frame. OnNetTick runs on each tick.
    float       netTickRate = 0.0f;
    std::string serverName;         // friendly server label, sent to clients
    std::string password;           // clients must match to join ("" = open)

    /// The current network tick: the host's own count; on a client, its
    /// estimate of the host's (the latest snapshot's tick, stepped at the rate
    /// between snapshots). 0 when there's no fixed tick.
    std::uint32_t NetTick() const { return m_netTick; }
    /// The fixed tick rate in use: the host's netTickRate, or on a client what
    /// the host announced (0 = none: the frame-rate behaviour).
    float NetTickRate() const { return m_mode == Mode::Client ? m_hostTickRate : (m_mode == Mode::Server ? HostTickRate() : 0.0f); }

    /// Server-authoritative movement (host setting; off by default). Clients
    /// then send their inputs, not positions: each tick, a client's InputCmd
    /// (move, look, buttons, numbered, the last 3 repeated against loss) drives
    /// its player on the server through the controller's Simulate (IPlayerSim),
    /// and the server alone decides where it is. Positions and __xform from the
    /// client for that player are ignored. Runs on the fixed tick (netTickRate,
    /// 30 Hz if that's 0). The client predicts its own player: every frame it
    /// runs the same Simulate with the input it last sent (so an input shows
    /// within a tick) and this frame's look, and reconciles with the position
    /// the host acks for each input: tiny differences are ignored, small ones
    /// eased out, large ones snapped.
    bool authoritativeMovement = false;
    /// Client prediction: how fast a correction from the host is eased out (per
    /// second, frame-rate independent), and the size above which it snaps
    /// instead (units; the controller is told, so its camera doesn't glide).
    float reconcileRate = 12.0f;
    float reconcileSnap = 2.0f;
    /// Client: the object this player drives (its SpawnOwned player, with a
    /// controller that implements IPlayerSim). Its inputs go to the host when
    /// the host runs authoritative movement.
    void SetPawn(GameObject* pawn);
    /// Client: where inputs come from (default: the pawn's controller reads the
    /// keyboard / mouse / pad). For tests, bots and replays.
    void SetInputSource(std::function<PlayerInput()> f) { m_inputSource = std::move(f); }
    /// Whether authoritative movement is in force: the host's setting, or what
    /// the host announced (client).
    bool AuthoritativeMovement() const { return m_mode == Mode::Client ? m_hostAuthoritative : (m_mode == Mode::Server && authoritativeMovement); }
    /// Client: the newest input the host has applied (from its snapshots).
    std::uint32_t LastAckedInput() const { return m_lastAckedSeq; }
    /// Server: inputs refused for being too far from the tick or over the budget.
    std::uint32_t InputsDropped() const { return m_inputsDropped; }
    /// Client: whether it is predicting its pawn now (authoritative movement
    /// from a host that acks positions, and a pawn with a controller).
    bool Predicting() const { return m_predicting; }
    /// For NetworkSync: this object is the client's own predicted pawn (its
    /// controller moves and animates it here).
    bool Predicted(const std::string& netId) const { return m_predicting && netId == m_pawnId; }
    /// Client: host acks that disagreed with the prediction by more than the
    /// threshold (eased out), and those that were snapped.
    std::uint32_t PredictionCorrections() const { return m_predCorrections; }
    std::uint32_t PredictionSnaps() const { return m_predSnaps; }
    /// For NetworkSync: 1 = this peer owns the object's transform because of
    /// authoritative movement, 0 = it doesn't, -1 = not a player's object.
    int MovementAuthority(const std::string& netId) const;
    /// For NetworkSync: the network moves this object from a player's inputs here.
    bool InputDriven(const std::string& netId) const;
    /// Called on every fixed tick (host and client), with the tick and its
    /// length in seconds: where fixed-step game logic goes.
    void OnNetTick(std::function<void(std::uint32_t tick, float dt)> f) { m_onNetTick = std::move(f); }
    /// Client: state packets sent since it joined (for tests and stats).
    std::uint32_t StatesSent() const { return m_statesSent; }

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

    /// Spawn a prefab on every peer AND make THIS peer the network authority for
    /// it: the new instance is given a NetworkSync (a unique id, owned here and
    /// followed everywhere else) so its transform — and Character animation, if any
    /// — replicate automatically as you move it. Use for bullets, thrown/dropped
    /// items, abilities: the mover lives on the spawner, everyone else just sees it.
    /// Returns the local instance (or nullptr if it couldn't be created).
    GameObject* SpawnOwned(const std::string& prefabPath, const Vec3& position = Vec3::Zero);

    /// Destroy a previously SpawnOwned object on every peer by its sync id (the
    /// NetworkSync netId). Removes it locally and tells the others to do the same —
    /// the matching despawn for replicated bullets / pickups.
    void Despawn(const std::string& netId);

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
    /// Attach optional EXTRA state to a synced object: on the owner, `get` is called
    /// each broadcast to pack a small string (e.g. an animation index); on everyone
    /// else, `set` is called with the received string. Rides alongside the transform
    /// in the same packet. This is how NetworkSync replicates Character animation
    /// without the net core knowing anything about characters.
    void SetSyncExtra(const std::string& id, std::function<std::string()> get,
                      std::function<void(const std::string&)> set);
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

    /// Claim that this peer's shot hit the networked object `targetNetId`
    /// (its NetworkSync id), fired from `origin` along `direction`. A client
    /// sends it to the server ("__hit"), which checks it against its own view
    /// (the shooter's position, range, line of sight, where the target was,
    /// the weapon's Net Validator "hit/<weapon>": fire rate and ammo) and the
    /// game's Validate Hit rules, then applies the damage through the
    /// target's HealthStat and replicates it. On the host it's applied at once
    /// (the host is trusted), after the game's rules. Never apply damage
    /// locally on a client: the server's word comes back.
    /// With a fixed net tick (lag compensation): the claim says which moment
    /// the shooter saw (ViewTick), and the server checks it against where
    /// everything was then (its pose history), not where it is now, with the
    /// rewind clamped to 200 ms plus the shooter's round trip.
    void ClaimHit(const std::string& targetNetId, const std::string& weapon, const Vec3& origin, const Vec3& direction);
    /// Server, lag compensation: a 3D raycast against where every synced
    /// object was at `viewTick` (a client's ViewTick; its pose history keeps
    /// about a second), then everything is put back. Without a fixed tick, or
    /// on a client, an ordinary raycast. `ignore` as for Physics3D::Raycast.
    RaycastHit3D RewindRaycast(const Vec3& origin, const Vec3& direction, float maxDistance, double viewTick,
                               GameObject* ignore = nullptr);
    /// Client: the moment (in host ticks, with the fraction) it shows other
    /// players at: now minus the interpolation delay. What a hit claim carries.
    double ViewTick() const;
    /// Server: how far back (seconds) the last checked hit claim was rewound.
    float LastRewind() const { return m_lastRewind; }
    /// Game rules for hit claims (server): false refuses one; it may change
    /// claim.damage. The same hook as anticheat.validateHit.
    void ValidateHit(std::function<bool(HitClaim& claim)> rule) { anticheat.validateHit = std::move(rule); }
    /// A player the anti-cheat kicked (it reached kickAfterViolations), with its
    /// verified account ("" if anonymous) and its last refusals, oldest first.
    struct AntiCheatKick {
        std::uint32_t peer = 0;
        std::string name, userId;
        std::vector<CheatReport> recent;
    };
    /// Server: called for every anti-cheat kick, before the player is dropped.
    /// The dedicated server's --report-kicks files these for admin review.
    void SetAntiCheatKickHandler(std::function<void(const AntiCheatKick&)> f) { m_acKickHandler = std::move(f); }
    /// Hits the server applied / refused since it started (for tests and stats).
    int HitsApplied() const { return m_hitsApplied; }
    int HitsRefused() const { return m_hitsRefused; }
    /// Client: true if the server kicked this peer, plus the reason it gave.
    bool WasKicked() const { return m_kicked; }
    const std::string& KickReason() const { return m_kickReason; }

    /// A connected peer in the session roster.
    struct PeerInfo { std::uint32_t id; std::string name; char glyph; };
    /// The current roster the server knows about (server only): every connected
    /// client. Empty on a client.
    std::vector<PeerInfo> Peers() const;
    /// The display name a peer joined with ("" if unknown). Clients look it up in the roster.
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
    // Which reliable sequence numbers were already delivered: the newest one
    // and a 4096-wide window behind it, instead of every number ever seen. A
    // number older than the window counts as delivered (it's acked, not run
    // twice); the sender resends unacked messages long before that.
    struct SeqWindow {
        static constexpr std::uint32_t kWidth = 4096;
        std::uint32_t newest = 0;
        bool any = false;
        std::vector<std::uint64_t> bits = std::vector<std::uint64_t>(kWidth / 64, 0);
        bool Accept(std::uint32_t seq);   // true the first time a seq is seen
        void Clear() { newest = 0; any = false; std::fill(bits.begin(), bits.end(), 0); }
    };
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
        float stateAge = 0.0f;   // seconds since its last accepted position (move check)
        int   violations = 0;    // rejected moves in the current minute
        float violationTimer = 0.0f;
        int   msgCount = 0;      // custom messages this second (rate limit)
        int   hitClaims = 0;     // hit claims this second (the limit without the anti-cheat)
        float msgTimer = 0.0f;
        float joinedAt = 0.0f;   // server clock when it joined
        int   score = 0;
        // Per-client reliable channel (server side).
        std::uint32_t relSeq = 0;   // next outgoing sequence to this client
        std::unordered_map<std::uint32_t, std::pair<std::vector<std::uint8_t>, float>> relOut; // unacked
        SeqWindow relSeen;          // delivered seqs (de-dup)
        // Authoritative movement: the player object its inputs drive, the
        // inputs waiting (by seq), the newest applied, and the last one (repeated
        // when an input is late).
        std::string pawn;
        std::map<std::uint32_t, std::pair<std::uint32_t, PlayerInput>> inputs;   // seq -> (tick, input)
        std::uint32_t lastInputSeq = 0;
        PlayerInput lastInput;
        int repeats = 0;            // ticks the last input stood in for a late one
        bool sealedJoin = false;    // joined through the sealed handshake
        // Anti-cheat clock heartbeat (maxClockDrift): the check, when to ask
        // next, how long since a good answer, and since its last drift report.
        std::shared_ptr<net::ClockCheck> clock;
        float clockTimer = 0.0f, clockQuiet = 0.0f, clockReported = 1e9f;
        // Round trip, from the heartbeat's pings (-1 until measured): how far
        // a target may have moved while this client's shot was on its way.
        float rtt = -1.0f;
        std::uint64_t pingNonce = 0;
        double pingSentMs = 0.0;
    };
    // Sealed-join key exchanges waiting for their Join (server side), by address.
    struct PendingJoin {
        net::SecureChannel secure;
        std::vector<std::uint8_t> clientPk;
        float age = 0.0f;
    };
    static constexpr std::size_t kMaxPendingJoins = 256;

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
        std::size_t   bytes = 0;    // payload held so far
        std::vector<std::vector<std::uint8_t>> chunks;
        float age = 0.0f;
    };
public:
    /// Reassembly limits. A joined peer (or, for a client, its server) may have a
    /// few large messages in flight; anyone else only enough for one Join with an
    /// identity token, and only a bounded number of strangers at once — so forged
    /// fragment headers can't balloon memory.
    static constexpr std::uint32_t kMaxFragments = 4096;              // ~4.8 MB per message
    static constexpr std::size_t   kMaxPendingBytes = 8u << 20;       // per peer
    static constexpr std::size_t   kMaxPendingMessages = 8;           // per peer
    static constexpr std::uint32_t kStrangerMaxFragments = 8;
    static constexpr std::size_t   kMaxStrangers = 64;
    /// Half-assembled messages held for `from` (diagnostics / tests).
    std::size_t PendingFragmentMessages(const net::Endpoint& from) const {
        auto it = m_fragIn.find(from);
        return it == m_fragIn.end() ? 0 : it->second.size();
    }
    std::size_t PendingFragmentBytes() const {
        std::size_t n = 0;
        for (const auto& e : m_fragIn) for (const auto& m : e.second) n += m.second.bytes;
        return n;
    }
    /// Feed one received datagram through reassembly as if it came from `from`
    /// (tests use it to probe the limits without sockets).
    bool FeedFragmentForTest(const net::Endpoint& from, const std::uint8_t* data, std::size_t size,
                             std::vector<std::uint8_t>& out) { return TakeFragment(from, data, size, out); }
private:
    bool Handshaken(const net::Endpoint& from) const;
    bool TakeFragment(const net::Endpoint& from, const std::uint8_t* data,
                      std::size_t size, std::vector<std::uint8_t>& out);
    void PruneFragments(float dt);
    std::unordered_map<net::Endpoint, std::unordered_map<std::uint32_t, FragAsm>, net::EndpointHash> m_fragIn;
    std::uint32_t m_fragNextId = 0;

    void InitServerState();
    void ServerTick(float dt);
    void ClientTick(float dt);
    // A client's hit claim ("__hit"): checked, then applied or refused.
    void HandleHitClaim(Client& c, const std::string& data);
    std::string CheckHitView(const HitClaim& claim, std::uint32_t shooter, Transform* targetT, float slack);
    void ApplyHit(HitClaim& claim);
    int m_hitsApplied = 0, m_hitsRefused = 0;
    std::unordered_set<std::string> m_unknownWeapons;   // weapon names refused (logged once each)
    std::function<void(const AntiCheatKick&)> m_acKickHandler;
    std::unordered_map<std::uint32_t, std::vector<CheatReport>> m_recentCheats;   // last few per peer
    static constexpr std::size_t kRecentCheats = 5;
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
    std::uint64_t m_relayHostKey = 0; // host: the relay's key for our code (0 until welcomed)
    bool m_relayLost = false;         // the relay ended our session this tick (Stop() follows)
    std::string m_relayError;         // why the relay session ended ("" if it didn't)
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
    JoinCheck m_joinCheck;       // server: async (tri-state) join check
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
    SeqWindow m_relSeen;
    // Fixed net tick (netTickRate): the tick count, the time not yet stepped,
    // the rate the host announced (client), and the tick hook.
    std::uint32_t m_netTick = 0;
    float m_tickAccum = 0.0f;
    float m_hostTickRate = 0.0f;
    std::uint32_t m_statesSent = 0;
    std::function<void(std::uint32_t, float)> m_onNetTick;
    int StepNetTick(float rate, float dt);   // ticks that elapsed this frame (fires OnNetTick)
    float HostTickRate() const { return netTickRate > 0.0f ? netTickRate : (authoritativeMovement ? 30.0f : 0.0f); }
    // Authoritative movement.
    void ServerMovementTick(float step);   // each fixed tick: run each player one input
    void ClientInputTick();                // each fixed tick: send this tick's input
    static constexpr std::size_t kMaxQueuedInputs = 8;   // the tick budget: older ones are dropped
    static constexpr int kInputRepeats = 3;              // late-input cover before the player stops
    std::string m_pawnId;                  // client: the object this player drives
    std::function<PlayerInput()> m_inputSource;
    PlayerInput m_heldInput;               // client: this frame's input
    std::uint32_t m_pressedAccum = 0;      // client: presses since the last tick
    std::uint32_t m_inputSeq = 0;
    std::vector<std::pair<std::uint32_t, std::pair<std::uint32_t, PlayerInput>>> m_sentInputs;   // seq -> (tick, input), last 4
    bool m_hostAuthoritative = false;      // client: the host announced it
    std::uint32_t m_lastAckedSeq = 0;
    // Prediction (client).
    static constexpr float kReconcileEpsilon = 0.02f;   // ignore smaller differences (units)
    static constexpr std::size_t kMaxPredicted = 128;
    bool m_hostAcksPawn = false;           // the host sends PawnAck (Welcome bit 3)
    bool m_predicting = false;
    std::vector<std::pair<std::uint32_t, Vec3>> m_predicted;   // seq -> where the prediction put the pawn
    Vec3 m_predPending{0.0f, 0.0f, 0.0f};  // correction still to ease in
    PlayerInput m_predCmd;                 // the input last sent: what the prediction runs until the next tick
    std::uint32_t m_predPressOnce = 0;     // its presses, for the first frame only
    std::uint32_t m_predSeq = 0;           // the input the prediction is running
    std::uint32_t m_earlyAckSeq = 0;       // its ack, if it came before we finished it
    Vec3 m_earlyAck{0.0f, 0.0f, 0.0f};
    std::uint32_t m_lastPawnAck = 0;
    std::uint32_t m_predCorrections = 0, m_predSnaps = 0;
    void Reconcile(std::uint32_t seq, const Vec3& server);
    std::unordered_map<std::string, std::uint32_t> m_pawnOwner;   // server: player object -> peer
    std::uint32_t m_inputsDropped = 0;
    double m_acClockMs = 0.0;     // server: the clock the heartbeat checks clients against
    double m_gameClockMs = 0.0;   // client: the game clock it answers with
    std::unordered_map<net::Endpoint, PendingJoin, net::EndpointHash> m_pendingJoins;
    std::vector<std::uint8_t> m_hostSeed;   // server identity (ed25519 seed), empty = none
    // Client sealed join: key hellos sent unanswered, keyed by the server's
    // JoinKey, or given up on (an older server: the plaintext Join).
    int  m_joinHellos = 0;
    bool m_keyed = false;
    bool m_legacyJoin = false;
    bool m_pinWarned = false;
    // Client: asking the host whether it wants the auth token (see authTokenToAnyHost).
    int  m_authQueries = 0;
    bool m_authDecided = false;
    bool m_hostWantsAuth = false;
    std::uint32_t m_authNonce = 0;

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
    float m_serverTime = 0.0f;        // seconds since the server started
    int   m_hostScore = 0;
    bool  m_matchOn = false, m_matchEnded = false;
    float m_matchStartAt = 0.0f, m_matchEndAt = 0.0f;
    float m_rosterTimer = 0.0f;
    std::uint32_t m_querySecret = 0;  // keys the player-list query challenge
    std::uint32_t QueryChallenge(const net::Endpoint& from) const;
    // Client-only: the roster the server sends, and position corrections received.
    std::vector<RosterEntry> m_roster;
    std::uint32_t m_corrections = 0;
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
        struct Sample { double tick; Vec3 pos; Quat rot; };
        std::vector<Sample> samples;   // the interpolation buffer (fixed tick), oldest first
        std::vector<Sample> history;   // server: its pose each tick, ~1 s (lag compensation)
        std::function<std::string()> getExtra;            // owner packs extra state
        std::function<void(const std::string&)> setExtra; // remote applies it
    };
    std::unordered_map<std::string, SyncObj> m_syncObjs;
    float m_syncTimer = 0.0f;
    std::uint32_t m_spawnCounter = 0;     // unique-id source for SpawnOwned
    void SyncTick(float dt);
    // The interpolation buffer (fixed tick).
    static constexpr double kInterpTicks = 3.0;          // how far in the past synced objects are shown, at least
    static constexpr double kInterpRecover = 0.02;       // ticks per tick the need eases back once the network allows
    static constexpr double kInterpSlew = 0.2;           // ticks per tick the delay moves toward the need
    static constexpr std::size_t kMaxSamples = 64;
    double m_interpDelay = kInterpTicks;                 // ticks shown in the past (follows m_interpNeed)
    double m_interpNeed = kInterpTicks;                  // what the samples' lateness + spacing call for
    double m_smoothTick = 0.0;                           // TickTime without the resync jumps
    bool   m_smoothTickInit = false;
    void AdvanceSmoothTick(float dt);
    float TickRateInForce() const { return m_mode == Mode::Server ? HostTickRate() : (m_mode == Mode::Client ? m_hostTickRate : 0.0f); }
    double TickTime() const;                             // the tick now, with its fraction
    static void SampleAt(const SyncObj& o, double tick, Vec3& pos, Quat& rot);
    static void PoseAt(const std::vector<SyncObj::Sample>& b, double tick, Vec3& pos, Quat& rot);
    // Lag compensation (server).
    static constexpr float kRewindBase = 0.2f;           // seconds of rewind allowed on top of the shooter's RTT
    void RecordHistory();                                // each fixed tick: every synced object's pose
    struct Rewound { Transform* t; Vec3 pos; Quat rot; };
    std::vector<Rewound> RewindTo(double tick);          // move everything to `tick`; returns what to put back
    static void Restore(std::vector<Rewound>& saved);
    double ClampRewind(double viewTick, float rttSeconds) const;
    float m_lastRewind = 0.0f;
public:
    /// Synced objects shown from the interpolation buffer (a fixed tick in force).
    bool InterpolationBuffer() const { return TickRateInForce() > 0.0f && interpolationRate > 0.0f; }
    /// How far in the past (seconds) the buffer shows them: 3 ticks (100 ms at
    /// 30 Hz), more while states arrive later or further apart than that allows.
    float InterpolationDelay() const { const float r = TickRateInForce(); return r > 0.0f ? (float)(m_interpDelay / r) : 0.0f; }
private:
    void DestroyLocalSync(const std::string& id);   // despawn a synced object here

    // First-class RPC handlers (by name) + chat log/handler.
    std::unordered_map<std::string, std::function<void(std::uint32_t, const std::string&)>> m_rpcHandlers;
    std::vector<ChatEntry> m_chatLog;
    std::function<void(const ChatEntry&)> m_chatHandler;

    // Anti-cheat: refusals are reported here and kick repeat offenders.
    std::uint64_t m_tickCount = 0;
    std::function<void(const CheatReport&)> m_cheatHandler;
    /// Server: may client `peer` send this? (False = drop it, already reported.)
    bool AntiCheatAllows(std::uint32_t peer, const std::string& channel, const std::string& data);
    /// Server, always (anti-cheat on or off): may a client send on `channel`?
    /// Reserved "__" channels other than the ones clients really use are
    /// dropped (a client can't start the match), and chat is re-signed with the
    /// sender's roster name (no posting as someone else).
    bool ClientMessageOk(const Client& from, const std::string& channel, std::string& data) const;
    bool AntiCheatAllowsSyncVar(std::uint32_t peer, const std::string& key);
    void AntiCheatAfterRefusal(std::uint32_t peer);

    // Server-authoritative synced variables
    std::unordered_map<std::string, std::string> m_syncVars;
    void ApplySyncVar(const std::string& key, const std::string& value);
};

} // namespace okay

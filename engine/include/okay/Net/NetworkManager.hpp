#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Net/UdpSocket.hpp"
#include "okay/Net/NetSim.hpp"        // NatType (simulateNat)
#include "okay/Net/RelayProtocol.hpp"  // kRelaySlotPort
#include "okay/Net/SecureChannel.hpp"
#include "okay/AntiCheat/AntiCheat.hpp"
#include "okay/AntiCheat/AntiCheatLog.hpp"
#include "okay/AntiCheat/ClockCheck.hpp"
#include "okay/Net/PlayerInput.hpp"
#include "okay/Net/NetDelta.hpp"
#include "okay/Net/NetChannels.hpp"
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
namespace net { class Packet; class BuildSync; class QuestSync; class Rollback; }

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
    /// Host on `port` if it is free on this computer; otherwise join the game
    /// already hosted on it at `host` (this computer by default). Two copies of
    /// a game on one PC become host and client with no setup (AutoStart::Auto).
    bool HostOrJoin(std::uint16_t port = kDefaultPort, const std::string& host = "127.0.0.1");
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

    // ---- Direct paths through NATs (hole punching) -----------------------
    /// Relay sessions: try a direct path between this player and the host (true, the default). The relay
    /// tells the host this player's public address, and this player the host's (never anyone else's, and
    /// only when both sides have this on); both then send to each other at once, which opens most home
    /// routers. Traffic uses the direct path once it answers both ways and goes back to the relay if it
    /// stops answering; a router that gives a new port to every destination (a symmetric NAT) stays on
    /// the relay. Off keeps every address private: everything goes through the relay.
    bool directConnect = true;
    /// Seconds to try a direct path before staying on the relay.
    float punchTimeoutSeconds = 5.0f;
    /// Client: the game reaches its host on a direct path (punched through the routers), not via the relay.
    /// A direct (non-relayed) session counts as direct too.
    bool DirectToHost() const;
    /// Host: clients reached on a direct path (punched) and through the relay.
    int DirectPeers() const;
    int RelayedPeers() const;
    /// "direct", "direct (punched)", "relayed", "relayed (trying direct)" or "" offline: this peer's link to
    /// its host (on the host, "relayed" while any client is still relayed).
    std::string PathText() const;
    /// Direct paths opened, and tries that gave up and stayed on the relay (this session).
    int PunchesOpened() const { return m_punchOpened; }
    int PunchesFailed() const { return m_punchFailed; }
    /// Test a router in front of this peer (NetSim.hpp's NAT model), applied each time its socket opens.
    net::NatType simulateNat = net::NatType::None;

    // ---- Online play with a room code (one call) -------------------------
    /// The relay "host[:port]" online play uses (saved with the Network Manager). Empty: the OKAY_RELAY
    /// environment variable, then the relay this build was made with (OKAY_DEFAULT_RELAY, else the
    /// launcher's OKAY_DEFAULT_VOICE_RELAY). Never a GitHub address.
    std::string relayServer;
    /// The relay "host:port" HostOnline / JoinCode would use ("" when none is set).
    std::string OnlineRelay() const;
    /// Host a game over the internet through the relay, under a short room code (made up when `code` is
    /// empty; a made-up code that turns out to be taken is replaced by another; a code of your own needs 6 to
    /// 12 letters and digits, since anyone who guesses it can knock). False with LastError() if it could not
    /// start; RoomCode() is the code to give friends.
    bool HostOnline(const std::string& code = "");
    /// Join a game by its room code ("K7Q29X"; case, spaces and dashes do not matter), through the relay.
    /// It returns at once; IsConnected() turns true when the host answers, or LastError() says why not
    /// ("No game is hosted with code ...", "Could not reach the relay server ...").
    bool JoinCode(const std::string& code);
    /// The room code of this online game ("" when not online).
    std::string RoomCode() const { return m_relay ? m_relayCode : std::string(); }
    /// A join link for this online game (okay://join?..., okay/Net/JoinLink.hpp), "" when not online.
    std::string OnlineJoinLink() const;
    /// A new random room code: 6 letters and digits, no look-alikes (0/O, 1/I).
    static std::string MakeRoomCode();
    /// `code` in the form the relay knows it (upper case, no spaces or dashes); "" if it is not a room
    /// code (4 to 12 letters and digits).
    static std::string NormalizeRoomCode(const std::string& code);
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

    // ---- Join gate (session tickets) -------------------------------------
    /// What a client must do before it joins: called at the start of StartClient / JoinViaRelay (so also for a launcher join, a
    /// reconnect and a script's net_join), before any socket is opened or key made. The gate may fetch a session ticket
    /// (TicketBinding(), then SetAuthToken(ticket)); returning false refuses the join: nothing is sent and the call returns false
    /// with the reason in LastJoinRefusal(). No gate (the default) = every join goes ahead as before. See engine/include/okay/AntiCheat/TicketJoin.hpp.
    using JoinGate = std::function<bool(NetworkManager& nm, const std::string& host, std::uint16_t port,
                                        bool relayed, const std::string& relayCode, std::string& why)>;
    static void SetJoinGate(JoinGate gate);
    /// Why the gate refused the most recent join ("" = it did not).
    static std::string LastJoinRefusal();

    // ---- Server authority (basic anti-cheat) ----------------------------
    /// Server: the fastest a client's avatar may move (world units per second;
    /// 0 = no check). A faster move is rejected and the client is snapped back to
    /// its last good position. Use ServerTeleport() for legit jumps (respawns).
    float maxMoveSpeed = 0.0f;
    /// Server: custom messages a client may send per second (0 = unlimited);
    /// extra ones are dropped (reliable ones aren't acknowledged, so they come
    /// again later).
    int maxMessagesPerSecond = 0;
    /// Server: the longest custom message a client may send to be applied or
    /// relayed (channel + data, bytes; 0 = any). A client also gets four times
    /// this in bytes per second. Chat lines are cut to 512 bytes.
    int maxRelayedBytes = 16 * 1024;
    /// Server: kick a client after this many rejected moves within a minute (0 = never).
    int kickAfterViolations = 0;
    /// Server, authoritative movement: the shortest time between two presses of
    /// Jump, Dash or Fire in a player's inputs (seconds; 0 = off). A quicker
    /// press is dropped, so a client can't press every tick.
    float minPressInterval = 0.0f;
    /// Server: move a peer's avatar (0 = the host's own) — accepted by the move
    /// check and pushed to that client.
    void ServerTeleport(std::uint32_t id, const Vec3& pos);
    /// Server: rejected moves from a peer in the last minute.
    int PeerViolations(std::uint32_t id) const;
    /// Client: how many times the server corrected our position.
    std::uint32_t Corrections() const { return m_corrections; }

    // ---- Anti-cheat (opt-in; see okay/AntiCheat/AntiCheat.hpp) ------------------
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

    /// Spatial interest management: a client is only sent peers, and owned
    /// objects in the binary snapshot, within this many world units of its own
    /// avatar (0 = no culling — send everyone in the room). Objects are gathered
    /// from a grid whose cell is this radius (the client's cell and its
    /// neighbours) and then kept only inside the sphere. An out-of-range object
    /// stops updating on that client until it comes back.
    float interestRadius = 0.0f;

    /// Round-trip time to the server in milliseconds (clients only; 0 until the
    /// first ping completes). A simple latency readout for HUDs / netcode.
    float RttMs() const { return m_rtt; }
    /// The UDP port the server is bound to (0 if not a server).
    std::uint16_t ServerPort() const { return m_socket.LocalPort(); }
    /// This peer's socket (tests and tools: the NAT model keys on it).
    const net::UdpSocket& Socket() const { return m_socket; }
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
    /// Client: what to send the ticket service as "cnf" so the session ticket
    /// it signs works only for this player's next connection: base64url SHA-256
    /// of the key StartClient / JoinViaRelay will use (made now). Call it before
    /// starting the client, then SetAuthToken(ticket). "" without encryption.
    std::string TicketBinding();
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
    /// -2 = too busy to check: the join is refused ("server busy, try again").
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
    /// Set it on BOTH ends before host/join. A client without encryption support
    /// falls back to plaintext; a host without it refuses to start (no quietly
    /// unencrypted server). Default off.
    bool encryption = false;
    /// Relay sessions: seconds to wait for the relay's welcome before giving up
    /// with RelayError() set ("the relay never answered"). 0 waits forever.
    float relayTimeoutSeconds = 15.0f;
    /// Client: a joined session ends (Stop(), ConnectionLost() set) when the host
    /// sends nothing for this many seconds. The client pings once a second and
    /// the host answers, so a live host is never this quiet. 0 never gives up.
    float serverTimeoutSeconds = 10.0f;
    /// Client: rejoin by itself after ConnectionLost() (the host went silent).
    /// Off by default. Each try waits reconnectDelaySeconds, doubling per failed
    /// try up to reconnectMaxDelaySeconds, for at most reconnectMaxAttempts tries
    /// (a try that gets "wrong password", "authentication failed" or a kick ends
    /// it at once). Needs a session started with StartClient / JoinViaRelay /
    /// JoinByLink: the target is remembered. The name, room, password and
    /// account token are kept; the host hands back the peer id, score, ready flag
    /// and last position if it still holds them (resumeGraceSeconds), and objects
    /// this player made with SpawnOwned are announced again. See Resumed().
    bool autoReconnect = false;
    int   reconnectMaxAttempts = 5;
    float reconnectDelaySeconds = 1.0f;
    float reconnectMaxDelaySeconds = 15.0f;
    /// Host: how long (seconds) the state of a client that dropped is kept so
    /// the same client can resume it (matched by a secret token the Welcome
    /// gave it). At most kMaxParked are kept. 0 = keep nothing.
    float resumeGraceSeconds = 30.0f;
    /// Host: hand a resume token only to a client whose join was sealed, i.e. whose Welcome
    /// (the one message that carries the token) travels encrypted. The token is a bearer secret:
    /// on an unencrypted session anyone who can read the datagrams (same network, a router on the
    /// path) sees it and can take the player's place, and no keyed proof can fix that because
    /// there is no key. With this on, such a client gets no token and a rejoin is a new peer.
    /// Off by default (LAN and tests without encryption keep resuming). See docs/online/multiplayer.md.
    bool resumeRequiresEncryption = false;
    /// What a reconnecting client reports to its handler.
    enum class ReconnectEvent { Lost, Attempt, Restored, GaveUp };
    /// Client: called on each step; `attempt` is the try number (1-based; 0 for Lost).
    void SetReconnectHandler(std::function<void(ReconnectEvent, int attempt)> f) { m_reconnectHandler = std::move(f); }
    /// Client: waiting to retry, or mid-try.
    bool Reconnecting() const { return m_reconnecting; }
    int ReconnectAttempt() const { return m_reconnectAttempt; }
    /// Client: every try failed (ConnectionLost() stays true).
    bool ReconnectGaveUp() const { return m_reconnectGaveUp; }
    /// Client: how many times a lost session has been rejoined.
    int Reconnects() const { return m_reconnectCount; }
    /// Client: the last rejoin got the old peer id back with score and position.
    bool Resumed() const { return m_resumed; }
    /// Client: the secret the host's Welcome gave this session (0 before joining). Whoever
    /// has it can take this player's place after a drop, so never log or share it. A game
    /// that wants to resume after its own restart saves it and calls SetResumeToken(token)
    /// right after StartClient (before the join completes).
    std::uint64_t ResumeToken() const { return m_resumeToken; }
    void SetResumeToken(std::uint64_t token) { if (m_mode == Mode::Client && !m_joined) m_resumeToken = token; }
    /// Host: how many dropped clients' states are being kept (at most kMaxParked).
    std::size_t ParkedCount() const { return m_parked.size(); }
    /// Host: a client that rejoined with its old state.
    struct ResumedPeer { std::uint32_t id = 0; Vec3 position{}; bool hasPosition = false; int score = 0; bool ready = false; };
    void SetPeerResumedHandler(std::function<void(const ResumedPeer&)> f) { m_peerResumed = std::move(f); }
    static constexpr std::size_t kMaxParked = 64;
    /// Reliable messages (SendReliable* and SendOrdered*) that were still unacked when a
    /// session dropped are kept, up to kMaxResendMessages / kMaxResendBytes (payload bytes)
    /// per drop, oldest first, and sent again once the rejoin is RESUMED: a client keeps
    /// its own, the host keeps a dropped client's with its parked state. A rejoin that is
    /// not a resume (grace over, host restarted) is a new peer and drops them. They go out as
    /// new messages, so one the peer had already got whose ack was lost arrives twice:
    /// handlers of reliable messages should be idempotent. Counts since this manager was made.
    std::size_t ResentAfterResume() const { return m_resentTotal; }   ///< messages re-sent
    std::size_t ResendDropped() const { return m_resendDropped; }     ///< a client's own, left out by the bounds or lost with a non-resumed rejoin; the host's, left out by the bounds
    static constexpr std::size_t kMaxResendMessages = 256;
    static constexpr std::size_t kMaxResendBytes = 256u * 1024u;

    /// Client: seconds to wait for the host to accept the join before giving up
    /// (JoinRejected() with the reason "the host did not answer"). A relay
    /// session counts from when the relay pairs it. 0 waits forever.
    float joinTimeoutSeconds = 15.0f;
    /// Relay sessions: a welcome must echo the nonce this peer sent, so one forged
    /// by someone who can't see our traffic is ignored. Relays older than protocol
    /// version 4 can't echo it; set this on the peer that has to use one anyway, accepting
    /// that a forged welcome then can't be told from theirs. Default off.
    bool allowUnverifiedRelay = false;
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
    /// Auto: host if the port is free on this computer, otherwise join the
    /// game already hosted here (127.0.0.1), so pressing Play in two copies of
    /// the game on one PC makes a two-player test with no setup.
    enum class AutoStart { None, Host, Join, Auto };
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
    /// and the server alone decides where it is. The server claims that player
    /// itself — the first object the client spawns under `sp/<its id>/` that
    /// carries a movement controller — so a client that never calls SetPawn
    /// still can't move it by sending positions. Until a pawn is claimed, State
    /// positions and __xform for those controller objects are dropped. An
    /// InputCmd may claim a different object under the client's prefix only
    /// while it has no live pawn. Other owned objects (props, projectiles) are
    /// unchanged. Runs on the fixed tick (netTickRate, 30 Hz if that's 0). The
    /// client predicts its own player: every frame it runs the same Simulate
    /// with the input it last sent (so an input shows within a tick) and this
    /// frame's look, and reconciles with the position the host acks for each
    /// input: tiny differences are ignored, small ones eased out, large ones
    /// snapped.
    bool authoritativeMovement = false;
    /// Client prediction: how fast a correction from the host is eased out (per
    /// second, frame-rate independent), and the size above which it snaps
    /// instead (units; the controller is told, so its camera doesn't glide).
    float reconcileRate = 12.0f;
    float reconcileSnap = 2.0f;
    /// Prefabs clients may spawn on this host (Spawn / SpawnOwned / net_spawn)
    /// when the anti-cheat is off; the scene's NetworkPlayerSpawner prefab
    /// counts too. With the anti-cheat on, its spawn allowlist decides instead.
    /// Paths are relative to the game folder, like every network prefab path.
    std::vector<std::string> spawnablePrefabs;
    /// Whether `path` may be named in a network spawn: relative, inside the game
    /// folder (no "..", drive, UNC or device names), a .okayprefab, a regular
    /// file and at most kMaxNetworkPrefabBytes. Every peer checks a spawn it
    /// receives with it before opening anything.
    static bool IsSafeNetworkPrefabPath(const std::string& path, std::string* why = nullptr);
    static constexpr std::uintmax_t kMaxNetworkPrefabBytes = 4u * 1024u * 1024u;
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
    /// True on a client if the last join was refused (server full / bad password /
    /// the pinned host key didn't match).
    bool JoinRejected() const { return m_joinRejected; }
    /// Why JoinRejected() is set ("" when it isn't).
    const std::string& JoinRejectReason() const { return m_joinRejectReason; }

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
    /// A change is sent again (the newest value only, a few times a second) until the
    /// other side acknowledges it, so a lost datagram does not lose the change.
    void SetVar(const std::string& key, const std::string& value);
    /// Variable changes sent and not acknowledged yet (to every client on the host; to the host on a client).
    std::size_t VarsUnacked() const;
    /// Read the local copy of a synced variable ("" if unset).
    std::string GetVar(const std::string& key) const;
    /// Every shared variable (a read-only view, for tests and tools: how many, and how big).
    const std::unordered_map<std::string, std::string>& SyncVars() const { return m_syncVars; }
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
    /// Owned transforms go out as a quantized binary delta from the last
    /// snapshot the receiver acknowledged. The text "__xform" path stays for
    /// an explicit Send() and for when this is off.
    bool binarySnapshots = true;

    // ---- Reliable messaging (guaranteed delivery + de-dup over UDP) ----
    /// Like Send(), but resent until the other end acknowledges it and de-duped
    /// on arrival — for events you can't afford to drop (deaths, score, "go!").
    /// On a client it targets the server; on the server it reliably reaches every
    /// client.
    void SendReliable(const std::string& channel, const std::string& data);
    /// Reliable message to a single peer id (server routes; 0 = the host).
    void SendReliableTo(std::uint32_t targetId, const std::string& channel, const std::string& data);

    /// Reliable and ordered on one named channel. Each channel has its own
    /// sequence, so a gap on one name does not hold up another. Resent until
    /// the other side's cumulative ack covers it. Separate from SendReliable,
    /// and not used for movement or hit claims.
    void SendOrdered(const std::string& channel, const std::string& data);
    void SendOrderedTo(std::uint32_t targetId, const std::string& channel, const std::string& data);
    /// Unreliable, but only the newest packet on this channel is applied. An
    /// older or repeated one is dropped. A broadcast is relayed like Send().
    void SendSequenced(const std::string& channel, const std::string& data);
    void SendSequencedTo(std::uint32_t targetId, const std::string& channel, const std::string& data);

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
    /// Runs for every arriving RPC, after the named handler (and also when
    /// none is registered). NetworkObject uses it to route "<netId>/<name>"
    /// onto the matching object. Cleared by Stop().
    void SetRpcObserver(std::function<void(const std::string& name, std::uint32_t from, const std::string& data)> f) {
        m_rpcObserver = std::move(f);
    }
    bool HasRpcObserver() const { return static_cast<bool>(m_rpcObserver); }

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
    /// object was at `viewTick` (a client's ViewTick), then everything is put
    /// back. The rewind uses the same limit as a hit claim: 200 ms, plus
    /// `shooter`'s round trip when that client id is given (capped by the
    /// anti-cheat's maxRewindRtt). With no shooter, 200 ms. Pose history is
    /// still kept for about a second. Without a fixed tick, or on a client,
    /// an ordinary raycast. `ignore` as for Physics3D::Raycast.
    RaycastHit3D RewindRaycast(const Vec3& origin, const Vec3& direction, float maxDistance, double viewTick,
                               GameObject* ignore = nullptr, std::uint32_t shooter = 0);
    /// Client: the moment (in host ticks, with the fraction) it shows other
    /// players at: now minus the interpolation delay. What a hit claim carries.
    double ViewTick() const;
    /// Server: how far back (seconds) the last checked hit claim was rewound.
    float LastRewind() const { return m_lastRewind; }
    /// Game rules for hit claims (server): false refuses one; it may change
    /// claim.damage. The same hook as anticheat.validateHit.
    void ValidateHit(std::function<bool(HitClaim& claim)> rule) { anticheat.validateHit = std::move(rule); }
    /// Server: `go` (a networked object with a Health Stat) just respawned. Every other peer
    /// is told ("__respawn": health and position, reliable) so its copy comes back too, the
    /// movement check lets the owner's next position jump, and lag-compensation history
    /// before the jump is dropped. Health Stat calls this itself on a respawn or Revive().
    void NoteRespawn(GameObject* go);
    /// Server: `go` (a networked object with a Health Stat) just died. Every other peer is told
    /// ("__death": where the blow came from and the object's id, reliable) so its copy dies too and
    /// waits for the server's respawn. Health Stat calls this itself on every death; one that a
    /// replicated hit caused is skipped (the hit's "__dmg" carries it), so each death is sent once.
    void NoteDeath(GameObject* go, bool hasFrom, const Vec3& from);
    /// Server: deaths sent by NoteDeath since it started (not the ones a hit carried).
    int DeathsSent() const { return m_deathsSent; }

    /// Build mode over the network: client placements, removals, moves, links and undo are requests the
    /// server checks; what players built reaches late joiners. Rules and counters live here. On by default
    /// and idle while the scene has no BuildMode. See okay/Net/NetBuildSync.hpp and docs/gameplay/build_mode.md.
    net::BuildSync& Build() { return *m_buildSync; }
    /// Quest log and story variables over the network (server authoritative). See okay/Net/NetQuestSync.hpp.
    net::QuestSync& Quests() { return *m_questSync; }

    // ---- Request channels and join hooks (for replicated game systems) -------------
    /// Server: a client's message on `channel` (it must start with "__req/") goes to `handler`
    /// (sender id, payload) and nowhere else: it is not relayed to other clients, not put in the
    /// inbox, and a channel nobody registered is dropped. It still goes through the same checks as
    /// any client message first (cookie, per-client rate and size limits, the anti-cheat's rate
    /// limits and kick rules). The handler decides what the request means and must treat the
    /// payload as hostile. Used by build mode and the quest log. Not cleared by Stop().
    using RequestHandler = std::function<void(std::uint32_t from, const std::string& data)>;
    void SetRequestHandler(const std::string& channel, RequestHandler handler);
    void ClearRequestHandler(const std::string& channel);
    /// Server: called when a client joins (or rejoins) with its id, once the Welcome is sent, so a
    /// replicated system can send it the state it missed. Returns an id for RemoveJoinHook.
    int  AddJoinHook(std::function<void(std::uint32_t peer)> hook);
    void RemoveJoinHook(int id);
    /// Server: where the client's avatar last reported being (false if it has not reported one yet).
    bool PeerPosition(std::uint32_t id, Vec3& out) const;
    /// Server: deaths and damaged health a client that joined late was told about since this manager started.
    int LateJoinLifeSent() const { return m_lateLifeSent; }
    /// A player the anti-cheat kicked (it reached kickAfterViolations), with its
    /// verified account ("" if anonymous) and its last refusals, oldest first.
    struct AntiCheatKick {
        std::uint32_t peer = 0;
        std::string name, userId;
        std::vector<CheatReport> recent;
        /// Detection engine (detect.enabled): the peer's risk score, whether the
        /// kick was a BAN REQUEST (risk past banAt), and its evidence bundle as
        /// JSON (no account / address; call anticheat.detector.Bundle from the
        /// handler to add them). Empty / 0 when the engine is off.
        float risk = 0.0f;
        bool banRequested = false;
        std::string evidence;
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
    /// Client: true if the session ended because the host went silent
    /// (serverTimeoutSeconds). Stays readable after the disconnect.
    bool ConnectionLost() const { return m_connectionLost; }
    const std::string& ConnectionLostReason() const { return m_connectionLostReason; }

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
    // ---- The easy layer: players, join / leave and named events on every peer ----
    // The same on the host and on every client, so game code never asks which
    // side it is on. Visual Script (On Player Joined, Send Network Event, ...),
    // OkayScript (net.on, on_player_joined) and okay::Net (Net.hpp) all sit on these.

    /// The default game port (Host, Join, Auto Start and the LAN scan's first port).
    static constexpr std::uint16_t kDefaultPort = 45000;
    /// One player in the session. `id` 0 is the host (when it plays).
    struct Player { std::uint32_t id = 0; std::string name; bool isHost = false, isLocal = false; };
    /// Everyone in this peer's session and room, you included, sorted by id.
    /// Empty while offline. A client's list follows the host's roster (about once a second).
    std::vector<Player> Players() const;
    /// The host? (A listen server or a dedicated server.) Same as IsServer().
    bool IsHost() const { return IsServer(); }
    /// Run `f(id, name)` each time another player joins, on every peer. A client
    /// that joins gets one call for each player already there (the host first).
    /// Returns an id for RemoveListener.
    int OnPlayerJoined(std::function<void(std::uint32_t id, const std::string& name)> f);
    /// Run `f(id)` each time another player leaves (quit, timed out or kicked), on every peer.
    int OnPlayerLeft(std::function<void(std::uint32_t id)> f);
    /// Run `f(from, data)` when the named network event arrives ("" = every event,
    /// with the name in ReceivedEvent::name via OnAnyEvent). `from` is the sender's player id.
    int OnEvent(const std::string& name, std::function<void(std::uint32_t from, const std::string& data)> f);
    struct ReceivedEvent { std::uint32_t from = 0; std::string name, data; };
    int OnAnyEvent(std::function<void(const ReceivedEvent&)> f);
    void RemoveListener(int id);
    /// Send a named event to every player in the room, reliably and in order
    /// (resent until it arrives; events reach each player in the order they were
    /// sent; a client's goes through the host, which stamps the real sender).
    /// Big data is split into pieces and put back together on the way.
    /// With `includeSelf` (the default) it also runs here, so "an event happens
    /// everywhere". Names: 1-64 letters, digits, '_', '-', '.', ' '; data up to
    /// kMaxEventData bytes. False (and LastError says why) if it was not sent.
    bool SendEvent(const std::string& name, const std::string& data = "", bool includeSelf = true);
    /// The same, to one player only (it does not run here unless `target` is you).
    bool SendEventTo(std::uint32_t target, const std::string& name, const std::string& data = "");
    static constexpr std::size_t kMaxEventName = 64, kMaxEventData = 16 * 1024;
    static bool EventNameOk(const std::string& name);
    /// The last thing that went wrong, in plain words ("Could not reach host
    /// 1.2.3.4:45000 (no answer after 15 s)."), or "" if nothing did. Cleared by
    /// the next Host / Join. Every failure is also written to the log once.
    const std::string& LastError() const { return m_lastError; }
    /// Network time in seconds, the same on every peer to within about half a
    /// round trip: the host's clock since it started hosting. 0 while offline.
    double NetworkTime() const;
    // ---- Ownership handoff (server-authoritative) ----
    /// Host: hand the networked object `netId` (its Network Sync's id) to `player`
    /// (0 = the host). Every peer's copy follows: the owner's copy drives it, the
    /// others follow it, and the host takes its transform from that player only
    /// (any other client's is refused). It holds for players who join later; when
    /// the owner leaves, the object goes back to the host. False if not the host.
    bool GiveOwnership(const std::string& netId, std::uint32_t player);
    /// Client: ask the host for `netId`. The host grants it when its Network Sync
    /// says "Players can claim it" (clientClaimable) and no other client holds it,
    /// or when `allowOwnershipRequest` (host) says yes. The answer comes back as an
    /// ownership change (OnOwnershipChanged). The host itself just takes it.
    void RequestOwnership(const std::string& netId);
    /// Host's rule for RequestOwnership (unset: the clientClaimable rule above).
    std::function<bool(std::uint32_t player, const std::string& netId)> allowOwnershipRequest;
    /// Who holds `netId` after a handoff; -1 if it was never handed (its Network Sync's authority decides).
    std::int64_t OwnerOf(const std::string& netId) const;
    /// Run `f(netId, owner)` on every peer when an object changes hands.
    int OnOwnershipChanged(std::function<void(const std::string& netId, std::uint32_t owner)> f);
    /// Does this peer drive `netId` now (its own player, an object it was given, ...)?
    bool IsMine(const std::string& netId) const;

    // ---- Stats ----
    struct NetStats {
        bool  direct = false;        ///< client: a direct path to the host (a direct session, or punched through NATs)
        bool  relayed = false;       ///< traffic goes through a relay (client: to the host; host: to any client)
        int   directPeers = 0;       ///< host: clients on a direct path; client: 1 when direct
        int   relayedPeers = 0;      ///< host: clients still through the relay; client: 1 when relayed
        float rttMs = 0.0f;          ///< round trip to the host; on the host, the average over its clients
        float lossPercent = 0.0f;    ///< pings without an answer, last ~16 s; on the host, the average over its clients (it pings each once a second)
        float bytesInPerSec = 0.0f;  ///< this peer's socket, last full second
        float bytesOutPerSec = 0.0f;
        int   players = 0;           ///< Players().size()
        double networkTime = 0.0;    ///< NetworkTime()
    };
    NetStats Stats() const;
    /// Host: one client's round trip (ms) and loss (%), from the host's own pings. False for an unknown player.
    bool PeerLink(std::uint32_t player, float& rttMs, float& lossPercent) const;

    // ---- In-game network stats overlay ----
    /// Show ping, loss, bandwidth and the player count on screen while playing (the player draws it top right;
    /// the editor adds it to the Game view's stats). Also net.show_stats(true) and the Show Network Stats node.
    bool showStatsOverlay = false;
    /// The overlay's text lines ("Ping 42 ms", "Loss 3.1 %", ...): empty while offline. With rollback running,
    /// its frame, rollbacks and stalls too; on the host, a line per client.
    std::vector<std::string> StatsLines() const;
    /// The Network Manager whose overlay is on (the most recently started one first), or nullptr.
    static NetworkManager* OverlayManager();

    // ---- Big messages ----
    /// A reliable message bigger than one datagram goes in pieces; the far side tells which pieces it has,
    /// and a resend sends only the missing ones. Pieces sent again since this manager started, and how many
    /// resends of a whole message that saved.
    std::size_t PiecesResent() const { return m_piecesResent; }
    std::size_t PieceResends() const { return m_pieceResends; }

    // ---- Host migration ----
    /// When the host leaves (quits, or goes silent for serverTimeoutSeconds), the players left agree on a new
    /// host, the one with the lowest player id, without talking: they all hold the same list. It hosts on the
    /// port it already used, so the others can reach it at the address the old host saw; they join it, keep
    /// their player ids (it knows them by that address) and the game goes on. The new host becomes player 0
    /// (OnHostMigrated says which id it had), takes over the shared variables, the ownership table and every
    /// synced object it already mirrors; the old host's own spawned objects ("sp/0/...") go, as a leaving
    /// client's do.
    ///   Off:    never (the session ends when the host leaves, as before).
    ///   Lan:    the default. Only when every player is on this computer or a private network (10.x, 172.16-31.x,
    ///           192.168.x, 127.x, 169.254.x): the host shares each player's address with the others, which is
    ///           only fine where they can see each other anyway.
    ///   Always: also over the internet (players then learn each other's addresses; joining a player behind a
    ///           home router usually fails, and the next player is tried).
    /// Through a relay (HostViaRelay, HostOnline) both Lan and Always migrate the same way, and no address is
    /// shared: the list names players by their relay slot, the host shares the relay's successor key with
    /// them, and the lowest id takes the host's place on the relay (the room keeps its code; the relay lets
    /// it only once the old host is gone or silent). The others stay where they are on the relay and join
    /// it there; direct paths are then punched to the new host again. Needs a relay of protocol version 5.
    /// Never for a dedicated server, with anti-cheat rules, a token verifier or join check on
    /// the host (a player can't be trusted to enforce those), with a pinned host key on the client, or with
    /// only one client (nobody to play with). A client with autoReconnect on waits for its host instead when
    /// the host goes silent (a clean leave still migrates).
    enum class HostMigration { Off, Lan, Always };
    HostMigration hostMigration = HostMigration::Lan;
    /// Client: how long to wait for the chosen new host before trying the next one (seconds).
    float migrationJoinTimeoutSeconds = 3.0f;
    /// New host: how long players are waited for (kept in the player list) before they count as gone.
    float migrationGraceSeconds = 6.0f;
    /// Run `f(previousId)` on every peer once the game has a new host: `previousId` is the id the new host had
    /// before it became player 0 (anything a game keyed by that id now belongs to 0).
    int OnHostMigrated(std::function<void(std::uint32_t previousId)> f);
    /// Between the host leaving and the new host taking over.
    bool Migrating() const { return m_migrating; }
    /// Times this peer moved to a new host (or became one).
    int HostMigrations() const { return m_migrations; }
    /// Client: the host offered migration for this session (it sent the player list it would use).
    bool MigrationOffered() const { return !m_migPeers.empty(); }
    /// Client: the list the host offered, as (old id, address this player would use) - for tests and tools.
    std::vector<std::pair<std::uint32_t, net::Endpoint>> MigrationList() const {
        std::vector<std::pair<std::uint32_t, net::Endpoint>> out;
        for (const auto& p : m_migPeers) out.emplace_back(p.id, p.ep);
        return out;
    }
    /// The address to reach a listed player at. The host lists a player on its own computer as this
    /// computer (127.x); a player on another computer reaches that one at the host's address instead.
    /// A relay slot (relay sessions) is kept as it is.
    static net::Endpoint MigrationAddress(const net::Endpoint& listed, const net::Endpoint& hostAsSeen) {
        if (listed.port == net::kRelaySlotPort || hostAsSeen.port == net::kRelaySlotPort) return listed;
        const bool listedLoop = (listed.address >> 24) == 127, hostLoop = (hostAsSeen.address >> 24) == 127;
        if (listedLoop && !hostLoop && hostAsSeen.address != 0) return net::Endpoint{hostAsSeen.address, listed.port};
        return listed;
    }

    // ---- Rollback (deterministic games; off until started) ----
    /// See okay/Net/Rollback.hpp: fixed tick, input delay, input exchange, saved states, resimulation.
    net::Rollback& Rollback() { return *m_rollback; }
    const net::Rollback& Rollback() const { return *m_rollback; }

    // ---- Bad-network testing ----
    /// Added one-way delay (ms), random extra delay (ms) and the share of datagrams
    /// dropped (%), to test a game on a bad connection on one PC. All 0 (the
    /// default) is off. They drive the program-wide simulator (net::Simulator(),
    /// so every Network Manager in this program shares them) each frame while any
    /// is above 0, so they can be changed while playing.
    float simulateLatencyMs = 0.0f, simulateJitterMs = 0.0f, simulateLossPercent = 0.0f;

    /// Find this scene's Network Manager, or add one (on an object "__Network").
    static NetworkManager* Ensure(Scene& scene);
    /// The most recently started Network Manager that is still alive (okay::Net uses it).
    static NetworkManager* Current();

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
        bool Seen(std::uint32_t seq) const;   // Accept would say no (nothing changes)
        void Clear() { newest = 0; any = false; std::fill(bits.begin(), bits.end(), 0); }
    };
    // A shared variable change waiting for its acknowledgement (VarAck): the version
    // sent, how many times it was sent again, and (a client's own write) the value.
    struct VarPending { std::uint32_t ver = 0; int tries = 0; std::string value; float age = 0.0f; };
    struct Client {
        net::Endpoint endpoint;
        std::uint32_t id;
        PeerState state;
        float lastSeen;
        std::string name;
        std::string room;
        std::string userId;   // verified identity (from the token verifier), or ""
        std::string ident;    // who it is across reconnects: "u:<account>", else "a:<address>"
        net::SecureChannel secure;   // per-client encrypted session (if negotiated)
        std::unordered_map<std::uint32_t, PeerState> sentStates;  // last state sent (delta snapshots)
        bool hasState = false;   // true once this client has reported its position
        bool ready = false;
        float stateAge = 0.0f;   // seconds since its last accepted position (move check)
        int   violations = 0;    // rejected moves in the current minute
        float violationTimer = 0.0f;
        int   msgCount = 0;      // custom messages this second (rate limit)
        int   pktCount = 0;      // every packet of any type this second (anti-cheat packet ceiling)
        bool  pktReported = false;   // ...reported once a second at most
        std::size_t msgBytes = 0;   // and their bytes (4 x maxRelayedBytes a second)
        int   varsCreated = 0;   // SyncVar keys this client made (at most kMaxClientVars)
        std::unordered_map<std::string, VarPending> varOut;   // changes sent to it, not acked (key -> our version)
        std::unordered_map<std::string, std::uint32_t> varIn;  // the newest version of its own writes applied, per key
        int   hitClaims = 0;     // hit claims this second (the limit without the anti-cheat)
        float msgTimer = 0.0f;
        float joinedAt = 0.0f;   // server clock when it joined
        int   score = 0;
        // Per-client reliable channel (server side).
        std::uint32_t relSeq = 0;   // next outgoing sequence to this client
        std::unordered_map<std::uint32_t, std::pair<std::vector<std::uint8_t>, float>> relOut; // unacked
        SeqWindow relSeen;          // delivered seqs (de-dup)
        // Binary snapshots: what we have acked to this client, and what it has
        // acked to us. Independent of relSeq and of the avatar snapshot.
        net::SnapOut snapOut;
        net::SnapIn snapIn;
        // Per-channel reliable-ordered link with this client, plus the filter
        // for unreliable-sequenced packets it sends. Independent of relSeq.
        net::ChannelEnd pipes;
        // Authoritative movement: the player object its inputs drive, the
        // inputs waiting (by seq), the newest applied, and the last one (repeated
        // when an input is late).
        std::string pawn;
        std::map<std::uint32_t, std::pair<std::uint32_t, PlayerInput>> inputs;   // seq -> (tick, input)
        std::uint32_t lastInputSeq = 0;   // highest seq actually applied (acks and snapshots)
        float lastPress[3] = {-1e9f, -1e9f, -1e9f};   // server time of its last Jump, Dash, Fire press (minPressInterval)
        PlayerInput lastInput;
        int repeats = 0;            // ticks the last input stood in for a late one
        // Empty ticks earn one credit, capped. Spent one at a time to apply a
        // second queued input after a hitch. Zeroed once the queue is shallow,
        // so a client that keeps it full never buys a second step.
        int inputCredit = 0;
        std::uint32_t droppedPressed = 0;   // presses from inputs dropped as newest
        std::uint32_t droppedSeq[8] = {};   // those seqs, so a resend isn't applied twice
        std::uint8_t droppedN = 0;
        Vec3 repeatPos{};           // pose before the current repeat streak
        Vec3 repeatVel{};
        bool haveRepeatPose = false;
        bool sealedJoin = false;    // joined through the sealed handshake
        // Without encryption a client is only its address: the cookie its
        // Welcome gave it proves a Leave / State / Message / SyncVar is its
        // own. Required once it has sent one (an older client never does).
        std::uint64_t cookie = 0;
        bool cookieSeen = false;
        std::uint64_t resumeToken = 0;   // secret that lets this client take its state back after a drop
        bool resumed = false;            // it took its old state back (every Welcome it gets says so, retries included)
        float teleportUntil = -1.0f;     // server time until which its positions skip the move check (a respawn)
        // Anti-cheat clock heartbeat (maxClockDrift): the check, when to ask
        // next, how long since a good answer, and since its last drift report.
        std::shared_ptr<net::ClockCheck> clock;
        float clockTimer = 0.0f, clockQuiet = 0.0f, clockReported = 1e9f;
        // Round trip, from the heartbeat's pings (-1 until measured): how far
        // a target may have moved while this client's shot was on its way.
        // The quickest of the last few, so answering pings late doesn't
        // stretch it unless every one is late.
        float rtt = -1.0f;
        float rttSamples[8] = {};
        std::uint8_t rttCount = 0, rttNext = 0;
        std::uint64_t pingNonce = 0;
        double pingSentMs = 0.0;
        // The host's own pings to it, once a second (Stats / PeerLink): stamp, age, answered.
        struct Mark { float stamp = 0.0f; float age = 0.0f; bool answered = false; };
        std::vector<Mark> linkMarks;   // the last 16
        float linkTimer = 0.0f;
        float linkRtt = 0.0f;          // ms, the last answered one
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
    bool AllowMessage(Client& c, std::size_t bytes);
    void OnOrdered(net::ChannelEnd& link, const net::Endpoint& ackTo, std::uint32_t fromId,
                   Client* sender, net::Packet& p, std::size_t len);
    void OnOrderedAck(net::ChannelEnd& link, net::Packet& p);
    void OnSequencedFromClient(Client& from, net::Packet& p, std::size_t len);
    void OnSequencedFromServer(net::Packet& p);

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
    /// Feed one piece receipt (FragAck) as if it came from `from` (tests: forged receipts).
    void FeedFragAckForTest(const net::Endpoint& from, const std::uint8_t* data, std::size_t size) { OnFragAck(from, data, size); }
    /// Tests (relay security): the relay successor key this peer holds (the host's own, or a player's copy from
    /// the host's list), and a host-role hello sent from this peer's own relay registration (its socket, cookie
    /// and nonce) carrying `key`, as a player holding the key could send it.
    std::uint64_t RelaySuccessorKeyForTest() const { return m_mode == Mode::Server ? m_relaySuccessorKey : m_migRelayKey; }
    void SendRelayHostHelloForTest(std::uint64_t key);
    /// Tests: the direct paths this peer keeps (one per relay slot) and where each one sends.
    struct DirectPathInfo { std::uint32_t slot = 0; net::Endpoint ep; std::uint64_t token = 0; bool open = false, failed = false; };
    std::vector<DirectPathInfo> DirectPathsForTest() const;
    /// Peers with a "put together lately" list (kept only for peers still in the session).
    std::size_t FragDonePeers() const { return m_fragDone.size(); }
    /// Sent pieced messages kept for per-piece resend, and their bytes.
    std::size_t FragOutMessages() const { std::size_t n = 0; for (const auto& e : m_fragOut) n += e.second.size(); return n; }
    std::size_t FragOutBytes() const {
        std::size_t n = 0;
        for (const auto& e : m_fragOut) for (const auto& f : e.second) { n += f.plain.size(); for (const auto& pc : f.pieces) n += pc.size(); }
        return n;
    }
private:
    bool Handshaken(const net::Endpoint& from) const;
    bool TakeFragment(const net::Endpoint& from, const std::uint8_t* data,
                      std::size_t size, std::vector<std::uint8_t>& out);
    void PruneFragments(float dt);
    std::unordered_map<net::Endpoint, std::unordered_map<std::uint32_t, FragAsm>, net::EndpointHash> m_fragIn;
    std::uint32_t m_fragNextId = 0;
    // Per-piece resend: reliable messages sent in pieces, kept until every piece is acknowledged, so the
    // resend of the same message sends only what is missing (NetworkManagerSync.cpp).
    struct FragOut {
        std::vector<std::uint8_t> plain;                  // the message as the caller sent it (before sealing)
        std::vector<std::vector<std::uint8_t>> pieces;    // the datagrams, as first sent
        std::vector<bool> acked;
        std::uint32_t id = 0;
        bool allowEncrypt = true;
        int resends = 0;
        float age = 0.0f;
    };
    std::unordered_map<net::Endpoint, std::vector<FragOut>, net::EndpointHash> m_fragOut;
    std::unordered_map<net::Endpoint, std::vector<std::pair<std::uint32_t, std::uint32_t>>, net::EndpointHash> m_fragDone;   // (id, count) put together lately
    std::size_t m_piecesResent = 0, m_pieceResends = 0;
    static constexpr std::size_t kMaxFragOutPerPeer = 8;
    static constexpr std::size_t kMaxFragOutBytes = 256u * 1024u;   // bigger messages are resent whole
    static constexpr int kMaxPieceResends = 12;                     // then a fresh copy (new id, all pieces)
    static constexpr std::size_t kFragDoneKept = 16;
    void SendFragAck(const net::Endpoint& to, std::uint32_t id, std::uint32_t count, const FragAsm* asm_);
    void OnFragAck(const net::Endpoint& from, const std::uint8_t* data, std::size_t size);

    void InitServerState();
    void ServerTick(float dt);
    void ClientTick(float dt);
    // A client's hit claim ("__hit"): checked, then applied or refused.
    void HandleHitClaim(Client& c, const std::string& data);
    std::string CheckHitView(const HitClaim& claim, std::uint32_t shooter, Transform* targetT, float slack);
    void ApplyHit(HitClaim& claim);
    bool m_applyingHit = false;   // server: inside ApplyHit (its __dmg carries the death)
    int m_lateLifeSent = 0;
    std::unordered_map<std::string, RequestHandler> m_requestHandlers;
    std::vector<std::pair<int, std::function<void(std::uint32_t)>>> m_joinHooks;
    int m_nextJoinHook = 1;
    // After the tables they unregister from (destroyed first).
    std::unique_ptr<net::BuildSync> m_buildSync;
    std::unique_ptr<net::QuestSync> m_questSync;
    std::unique_ptr<net::Rollback> m_rollback;
    /// A client's message on a "__req/" channel: true if it was one (handled or dropped).
    bool HandleClientRequest(Client& from, const std::string& channel, const std::string& data);
    /// Server: object life events (death, respawn, health) go out on one ordered channel so a
    /// respawn can never be applied before the death it follows, whatever the network does.
    void SendLife(char kind, const std::string& body, std::int64_t only = -1);
    struct DeadInfo { bool hasFrom = false; Vec3 from{}; };
    std::unordered_map<std::string, DeadInfo> m_dead;   // synced objects the server knows are dead (cleared by a respawn)
    static constexpr std::size_t kMaxDeadTracked = 4096;
    void SendLifeSnapshot(std::uint32_t peer);
    int m_deathsSent = 0;
    float m_hitDist = 0.0f, m_hitOff = 0.0f, m_hitAllowed = 0.0f;   // the last CheckHitView's numbers (for the detector)
    void FeedShot(std::uint32_t shooter, const HitClaim& claim, bool hit, const std::string& why);
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
    std::uint64_t m_relayNonce = 0;   // random per attempt; a welcome must echo it (version 4)
    float m_relayWait = 0.0f;         // seconds spent without a welcome
    int   m_relayBadWelcomes = 0;     // welcomes dropped for a missing or wrong nonce
    std::uint32_t m_relaySlot = 0;    // client: the slot the relay gave us (it changes when our address does)
    static constexpr int kMaxRelayFramesPerCall = 64;   // relay datagrams consumed per ReceiveFrom
    std::uint64_t m_relayHostKey = 0; // host: the relay's key for our code (0 until welcomed)
    std::uint8_t m_relayCookie[16] = {}; // the relay's cookie for our address, echoed in hellos (v3)
    bool m_relayLost = false;         // the relay ended our session this tick (Stop() follows)
    bool m_relayHeard = false;        // the relay answered this attempt (a cookie or a welcome): it is reachable
    bool m_keepRelay = false;         // Stop() keeps the socket and the relay registration (host migration)
    std::uint64_t m_relaySuccessorKey = 0;   // host: the relay's successor key (version 5), shared with players
    std::uint32_t m_relayNewHost = 0; // client (migrating): the slot the relay now names as host
    bool m_relayPromoting = false;    // a player taking the host's place on the relay, not confirmed yet
    float m_relayPromoteWait = 0.0f;
    bool m_onlineMadeCode = false;    // HostOnline made the code up: a taken one is replaced
    int m_onlineCodeTries = 0;
    struct DirectPath {               // a punched path to one relay slot (see directConnect)
        std::uint32_t slot = 0;
        net::Endpoint ep;             // where it answers: the relay's word (its cookie proved it), or another port
                                      // at that same address once a probe of ours got an answer from there
        net::Endpoint alt;            // before it opens: another port at that address its probes came from...
        bool altProbed = false;       // ...which we probed (only an answer from there moves `ep`)
        float altReplyTimer = 0.0f;   // at most one probe to `alt` per kProbeEverySeconds
        std::uint64_t token = 0;      // from the relay; both sides prove the path with it
        float age = 0.0f, probeTimer = 0.0f, quiet = 0.0f;
        bool heard = false;           // its probe reached us
        bool open = false;            // it answered ours: traffic goes this way
        bool failed = false;          // gave up (or went quiet): the relay carries it
    };
    std::vector<DirectPath> m_direct;
    int m_punchOpened = 0, m_punchFailed = 0;
    DirectPath* DirectFor(std::uint32_t slot);
    const DirectPath* DirectFor(std::uint32_t slot) const;
    void DirectTick(float dt);
    void OnRelayPeerAddr(const std::uint8_t* d, std::size_t n);
    bool OnDirectDatagram(std::uint8_t* b, int n, net::Endpoint& from);   // true: a game datagram, `from` is now the slot
    void SendRaw(const net::Endpoint& to, const void* data, std::size_t size);   // through the NAT model, no latency knobs
    bool WantDirectNow() const;
    void SendDirectAllow(bool always);   // host: tell the relay which players passed the join (RelayDirectAllow)
    std::vector<std::uint32_t> m_directAllowSent;   // host: the slots it last named
    bool RelaySwitch(bool asHost);    // host migration on the relay: become host / client without leaving it
    void NoteRelayError();            // a relay session ended: LastError in plain words
    std::string RelayPlainError() const;
    bool RetryOnlineCode();           // HostOnline: a made-up code was taken, try another
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
    bool m_joinRejected = false; // client: server refused us (full / password / pin)
    std::string m_joinRejectReason;
    bool m_kicked = false;       // client: kicked by the server
    std::string m_kickReason;
    bool m_connectionLost = false;   // client: the host went silent
    std::string m_connectionLostReason;
    float m_serverSilence = 0.0f;    // client: seconds since the host last sent anything
    float m_joinWait = 0.0f;         // client: seconds spent waiting for the join to be accepted
    // Reconnect (client): where we joined, and the retry schedule.
    struct ConnTarget { int kind = 0; std::string host; std::uint16_t port = 0; std::string code; };   // 1 direct, 2 relay
    ConnTarget m_lastConn;
    bool m_reconnecting = false, m_reconnectGaveUp = false, m_resumed = false, m_keepResume = false;
    bool m_reconnectTrying = false;      // a try is in flight (joining)
    int m_reconnectAttempt = 0, m_reconnectCount = 0;
    float m_reconnectTimer = 0.0f;
    bool m_wasReady = false;
    std::uint64_t m_resumeToken = 0;     // client: what the host's Welcome gave us
    std::map<std::string, std::string> m_ownSpawns;   // SpawnOwned objects of this player: netId -> prefab
    std::function<void(ReconnectEvent, int)> m_reconnectHandler;
    void ReconnectTick(float dt);
    void BeginReconnect();
    void FailReconnectTry();
    void ReannounceOwnSpawns();
    // Reliable messages in flight when a session dropped, re-sent after a resume.
    struct Unacked { bool ordered = false; std::string channel, data; };
    std::size_t CollectUnacked(const std::unordered_map<std::uint32_t, std::pair<std::vector<std::uint8_t>, float>>& relOut,
                               const net::OrderedEnd& ordered, std::vector<Unacked>& out) const;
    void ResendUnacked(std::uint32_t target, std::vector<Unacked>& q);   // sends and empties `q`
    std::vector<Unacked> m_resendQ;                   // client: kept across the drop, sent on Restored
    std::size_t m_resentTotal = 0, m_resendDropped = 0;
    // Host: dropped clients' state, kept for resumeGraceSeconds.
    struct Parked {
        std::vector<Unacked> resend;   // what it had not acked
        std::uint64_t token = 0; std::uint32_t id = 0; std::string userId;
        Vec3 pos{}; bool hasState = false; char glyph = '@'; int score = 0; bool ready = false; float age = 0.0f;
    };
    std::vector<Parked> m_parked;
    std::function<void(const ResumedPeer&)> m_peerResumed;
    // Client-side reliable channel (to the server).
    std::uint32_t m_relSeq = 0;
    std::unordered_map<std::uint32_t, std::pair<std::vector<std::uint8_t>, float>> m_relOut;
    SeqWindow m_relSeen;
    // Client: binary snapshots to and from the server. Server uses the per-client
    // links above; m_relaySnap holds poses a client owns so the host can fold
    // them into everyone else's snapshot.
    net::SnapOut m_snapUp;
    net::SnapIn m_snapDown;
    struct RelayPose { net::QPose pose; std::string extra; std::string room; };
    std::unordered_map<std::string, RelayPose> m_relaySnap;
    // Client: ordered and sequenced link to the server. Server-originated
    // sequenced numbers live in m_seqOrigin so every client sees one sequence
    // per channel. Stop() clears both. Not the movement or hit-claim stream.
    net::ChannelEnd m_toServer;
    net::SequencedTx m_seqOrigin;
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
    void ServerMovementTick(float step);   // each fixed tick: one input, or two while catching up
    void ClientInputTick();                // each fixed tick: send this tick's input
    /// Server, authoritative movement: the first controller-carrying object this
    /// client spawned (`sp/<id>/...`) is its pawn, without waiting for an InputCmd.
    void NoteSpawnedPawn(const std::string& netId, GameObject* go);
    /// Server: may this client relay an __xform for `id`? A claimed pawn, or a
    /// controller object it spawned before claiming one, may not.
    bool ClientMaySendXform(const Client& from, const std::string& id) const;
    GameObject* ObjectByNetId(const std::string& id) const;
    /// The pawn object went away (despawn or scene destroy): drop the claim so
    /// the next one can be claimed, and forget its lag-comp history.
    void ReleasePawn(const std::string& id);
    void ResetPawnPrediction();   // client: prediction history for the pawn
    void ClearLocalPawn();        // client: pawn id, prediction, and the input stream
    static constexpr std::size_t kMaxQueuedInputs = 8;   // past this, the newest input is dropped (its press is kept)
    static constexpr int kTargetQueuedInputs = 2;        // deeper than this, a client that was late may apply one extra
    static constexpr int kInputRepeats = 3;              // late-input cover before the player stops
    std::string m_pawnId;                  // client: the object this player drives
    std::function<PlayerInput()> m_inputSource;
    PlayerInput m_heldInput;               // client: this frame's input
    std::uint32_t m_pressedAccum = 0;      // client: presses since the last tick
    std::uint32_t m_inputSeq = 0;
    std::vector<std::pair<std::uint32_t, std::pair<std::uint32_t, PlayerInput>>> m_sentInputs;   // seq -> (tick, input), last 4
    bool m_hostAuthoritative = false;      // client: the host announced it
    std::uint32_t m_lastAckedSeq = 0;
    std::uint64_t m_sessionCookie = 0;
    bool m_protectionsLogged = false;    // server: the "protections off" line, once per start   // client: the host's cookie for an unencrypted session (0 = none)
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
    net::SecureChannel m_nextSecure;   // client: keys made early for TicketBinding, used by the next session
    void UseNextSessionKeys();
    bool m_audienceWarned = false;     // server: "ticketKey without ticketAudience" logged
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
    std::unordered_map<std::uint32_t, int> m_remoteMissing;   // client: consecutive rosters a remote was absent from
    void InterpolateRemotes(float dt);

    // Server-only
    float m_serverTime = 0.0f;        // seconds since the server started
    // Server: join attempts per address (a refused Join, a new key exchange;
    // a wrong password counts two): past kJoinAttempts in kJoinWindow seconds
    // the address is ignored for a while, 10 s doubling to 320 s.
    struct JoinLimit { float windowStart = 0.0f; int count = 0; float blockedUntil = -1.0f, backoff = 0.0f; bool told = false; };
    std::unordered_map<std::uint32_t, JoinLimit> m_joinLimits;
    static constexpr int kJoinAttempts = 5;
    static constexpr float kJoinWindow = 10.0f;
    bool JoinBlocked(const net::Endpoint& from) const;
    void CountJoinAttempt(const net::Endpoint& from, int cost);
    // Server: accounts and addresses the anti-cheat kicked, refused until `until`
    // (m_serverTime); each repeat doubles the wait, up to an hour.
    // The ban list file and the audit log (settings banFile / auditLog), opened on first use.
    BanList m_banList;
    AuditLog m_audit;
    std::string m_banPath, m_auditPath;
    bool m_acFilesReady = false;
    std::unordered_map<std::string, int> m_acKickCounts;   // anti-cheat kicks per identity this run (capped)
    void AcFilesSync();
    void AcAudit(const std::string& event, const std::string& ident, const std::string& detail);
    std::string IdentOfPeer(std::uint32_t peer) const;
    /// True (and `why` filled) when `ident` is on the ban list now.
    bool BannedByList(const std::string& ident, std::string& why);
    struct KickBan { float until = 0.0f; int strikes = 0; };
    std::unordered_map<std::string, KickBan> m_kickBans;
    float KickBanLeft(const std::string& ident) const;
    int   m_hostScore = 0;
    bool  m_matchOn = false, m_matchEnded = false;
    float m_matchStartAt = 0.0f, m_matchEndAt = 0.0f;
    float m_rosterTimer = 0.0f;
    int m_rosterBurst = 0;          // server: rosters still to send at once (someone just joined), one a frame
    std::uint32_t m_querySecret = 0;  // keys the player-list query challenge
    std::uint32_t QueryChallenge(const net::Endpoint& from) const;
    // Client-only: the roster the server sends, and position corrections received.
    std::vector<RosterEntry> m_roster;
    std::uint32_t m_corrections = 0;
    std::unordered_map<net::Endpoint, Client, net::EndpointHash> m_clients;
    void ParkClient(const Client& c);
    void ReleaseClient(std::unordered_map<net::Endpoint, Client, net::EndpointHash>::iterator it);
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
    // The easy layer (NetworkManagerEvents.cpp).
    struct Listener {
        int id = 0;
        std::string name;   // OnEvent's name ("" for the others)
        std::function<void(std::uint32_t, const std::string&)> joined;
        std::function<void(std::uint32_t)> left;
        std::function<void(std::uint32_t, const std::string&)> event;
        std::function<void(const ReceivedEvent&)> any;
        std::function<void(const std::string&, std::uint32_t)> owner;
        std::function<void(std::uint32_t)> migrated;
    };
    std::vector<Listener> m_listeners;
    int m_nextListener = 1;
    std::unordered_map<std::uint32_t, std::string> m_knownPlayers;   // who the join / leave events have announced
    bool m_playersAnnounced = false;
    std::string m_lastError;
    bool m_errorNoted = false;   // the current join / connection failure was already turned into m_lastError
    double m_clientNetTime = -1.0;   // client: the host's clock, from snapshots (-1 = not heard yet)
    void SetError(const std::string& plain);
    float m_timeTimer = 0.0f;    // host: time since the clock was last sent
    void PlayersTick(float dt);  // each Update: join / leave events from the roster, the network clock
    void OnHostTime(const std::string& data);
    void ErrorsTick();           // each Update: a timed-out join or a lost connection becomes LastError
    void OnEventWire(std::uint32_t from, const std::string& wire);
    void DispatchEvent(std::uint32_t from, const std::string& name, const std::string& data);
    void RaisePlayerEvent(bool joined, std::uint32_t id, const std::string& name);
    // Host migration (NetworkManagerMigration.cpp).
    struct MigPeer { std::uint32_t id = 0; std::string name; net::Endpoint ep; };
    std::vector<MigPeer> m_migPeers;       // client: every client of the host, as the host sees them ("__mig")
    std::string m_migSent;                 // host: the list it last sent (sent again when it changes)
    float m_migResend = 0.0f;
    bool m_migrating = false;
    int m_migrations = 0;
    bool m_migrateNow = false;             // client: a player on the list is joining us, so the host is gone
    float m_migWait = 0.0f;                // client: seconds waiting for the chosen new host
    struct MigCarry {                      // what a peer keeps across the move
        std::uint32_t myOldId = 0;
        std::uint16_t localPort = 0;
        std::vector<MigPeer> candidates;   // sorted by id; the front is tried next
        std::unordered_map<std::string, std::string> vars;
        std::unordered_map<std::string, std::uint32_t> owners;
        std::unordered_map<std::uint32_t, GameObject*> remotes;
        std::unordered_map<std::uint32_t, Vec3> remoteTargets;
        std::unordered_map<std::uint32_t, std::string> known;
        std::string serverName;
        std::uint64_t relayKey = 0;        // relay sessions: the successor key the host shared
        std::uint32_t oldHostSlot = 0;     // relay sessions: the old host's slot (the relay may name it a while yet)
    } m_mig;
    std::uint64_t m_migRelayKey = 0;       // client: the relay successor key from the host's list ("R|" line)
    std::vector<MigPeer> m_migExpect;      // new host: players expected back, with their old ids
    float m_migGrace = 0.0f;               // new host: seconds left to wait for them
    std::uint16_t m_bindPort = 0;          // client: bind this local port (a migrating player keeps its address)
    bool MigrationAllowedHere() const;     // host: may this session migrate?
    void MigrationHostTick(float dt);
    void OnMigWire(std::uint32_t from, const std::string& data);
    bool BeginMigration(const std::string& why);   // client: true when it moves on instead of ending
    bool KnockHeeded() const;              // client: our own link to the host is quiet enough to believe a knock
    void MigrationTick(float dt);
    void TryNextHost();
    void TryNextHostRelay();
    void HostAfterMigration();
    void EndMigrationAlone();
    void RestoreCarry();
    void FinishMigration(std::uint32_t newHostOldId);
    bool TakeExpectedId(const net::Endpoint& from, std::uint32_t& id);   // new host: a returning player
    void SendHostBye();                    // host: tell every client it is leaving (Stop)
    void RaiseHostMigrated(std::uint32_t previousId);   // listeners, graphs, scripts (NetworkManagerEvents.cpp)
    std::unordered_map<std::string, std::uint32_t> m_netOwners;   // netId -> owner, after a handoff
    void ApplyOwner(const std::string& netId, std::uint32_t owner);   // here, on this peer
    void OnOwnerWire(std::uint32_t from, const std::string& data);    // "__own" (host -> clients)
    void OnOwnerRequest(std::uint32_t from, const std::string& netId); // "__ownreq" (client -> host)
    // Stats: socket bytes this second, and the client's pings (stamp, answered).
    std::size_t m_bytesInAcc = 0, m_bytesOutAcc = 0;
    float m_bytesInRate = 0.0f, m_bytesOutRate = 0.0f, m_statTimer = 0.0f;
    struct PingMark { float stamp = 0.0f; float age = 0.0f; bool answered = false; };
    std::vector<PingMark> m_pingMarks;   // the last 16
    void NotePingSent(float stamp);
    void NotePong(float stamp);
    bool m_simApplied = false;   // this manager turned the simulator on
    void SimulatorTick();

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
    void ApplyRemoteXform(const std::string& id, const Vec3& p, const Quat& q, double tick,
                          const std::string& extra, bool hasExtra);
    void BroadcastBinarySnapshots(double tick);
    void CullInterest(net::DeltaBase& mine, const Client& c) const;
    void SendSnap(net::SnapOut& out, const net::DeltaBase& current, double tick,
                  const net::Endpoint& to, std::uint64_t cookie);
    void NoteSnapAck(net::SnapOut& out, std::uint32_t id);
    /// `sender` is set when a client uploaded the snapshot. Null when the server sent it.
    void OnSnapDelta(net::SnapIn& in, const std::uint8_t* data, std::size_t size,
                     const net::Endpoint& ackTo, Client* sender);
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
    bool CookieOk(Client& c, net::Packet& p) const;
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
    std::function<void(const std::string&, std::uint32_t, const std::string&)> m_rpcObserver;
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
    /// May this peer's __spawn be opened here (and, on the server, relayed)?
    /// A safe path always; from a client, also one the host lets clients spawn.
    /// Refusals are logged once per peer.
    bool SpawnPathOk(std::uint32_t from, const std::string& path, bool fromClient) const;
    bool ClientMaySpawn(const std::string& path) const;
    mutable std::unordered_set<std::uint32_t> m_spawnRefusalLogged;
    bool AntiCheatAllowsSyncVar(std::uint32_t peer, const std::string& key);
    void AntiCheatAfterRefusal(std::uint32_t peer);

    // Server-authoritative synced variables. What a client may write: keys up
    // to 128 bytes, values up to 4 KB, at most kMaxClientVars new keys each.
    static constexpr std::size_t kMaxVarKey = 128, kMaxVarValue = 4096;
    static constexpr int kMaxClientVars = 256;
    /// Server: a peer left: its SpawnOwned objects ("sp/<id>/...") go, everywhere.
    void DespawnPeerObjects(std::uint32_t id);
    std::unordered_map<std::string, std::string> m_syncVars;
    void ApplySyncVar(const std::string& key, const std::string& value);
    // Reliable shared variables (NetworkManagerSync.cpp): every change carries a version; the
    // receiver acknowledges it (VarAck) and ignores an older version arriving late; the sender
    // sends the newest value again until acknowledged (kVarResendTries times at most).
    static constexpr int kVarResendTries = 24;          // x kReliableResend: about 6 s
    static constexpr std::size_t kMaxVarPending = 4096; // per peer
    std::unordered_map<std::string, std::uint32_t> m_varVer;   // host: the version of each variable's value
    std::uint32_t m_varCounter = 0;                           // host: versions given out; client: our own writes
    std::unordered_map<std::string, std::uint32_t> m_varSeen;  // client: the newest host version applied, per key
    std::unordered_map<std::string, VarPending> m_varOut;      // client: our writes the host has not acked
    std::uint32_t BumpVar(const std::string& key) { return m_varVer[key] = ++m_varCounter; }
    void SendVarTo(Client& c, const std::string& key);    // host: the current value, tracked until acked
    void SendVarAck(const net::Endpoint& to, const std::string& key, std::uint32_t ver);
    std::vector<std::pair<std::string, std::uint32_t>> m_varAcks;   // client: receipts to send this frame
    void FlushVarAcks();
    void SendOwnVar(const std::string& key, const VarPending& v);   // client: one of our writes
    void VarResendTick(float dt);
    void HandleClientVar(Client& c, const std::string& key, const std::string& value, bool hasVer, std::uint32_t ver);
};

} // namespace okay

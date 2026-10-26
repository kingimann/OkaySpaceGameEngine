#include "okay/Net/NetworkManager.hpp"
#include "okay/Net/SessionTicket.hpp"
#include "AntiCheatCrypto.hpp"   // shared/: ed25519 for the host identity (#269)
#include "OfficialGame.hpp"      // shared/: SafeRelPath / ResolveInside for network prefab paths
#include "okay/Components/NetworkPlayerSpawner.hpp"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <unordered_set>
#include <mutex>
#include "okay/Net/Packet.hpp"
#include "okay/Net/RelayProtocol.hpp"
#include "okay/Net/ServerQuery.hpp"
#include "okay/Net/JoinLink.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Components/SpriteRenderer.hpp"
#include "okay/Components/NetworkSync.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Physics/Physics2D.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Core/Log.hpp"
#include <algorithm>
#include <cmath>
#include <random>
#include <iomanip>
#include <sstream>

namespace okay {

namespace {
    enum Msg : std::uint8_t {
        Join = 1, Welcome = 2, State = 3, Snapshot = 4, Leave = 5,
        Message = 6, DirectMessage = 7, SyncVar = 8, Ping = 9, Pong = 10,
        Ready = 11, Reject = 12, Kicked = 13, ReliableMsg = 14, ReliableAck = 15,
        Fragment = 16,  // one piece of a message too large for a single datagram
        Query = 17, QueryInfo = 18,  // server browser: "who are you?" without joining
        Correct = 19,                // server -> client: you're here (move rejected / teleported)
        PlayerQuery = 20, PlayerList = 21, PlayerChallenge = 22,   // server browser: who's playing
        RosterMsg = 23,              // server -> clients: everyone in the room, scores, time
        JoinHello = 24,              // client -> server: [u8 version][string clientPk], padded
        JoinKey = 25,                // server -> client: [u8 version][string serverPk][string signature]
        ClockPing = 26,              // server -> client: [u64 nonce][u32 seq][string key] (anti-cheat clock)
        ClockPong = 27,              // client -> server: [u64 nonce][u32 seq][u64 clientMs][string tag]
        InputCmd = 28,               // client -> server: [string pawn][u8 n] n x [u32 tick][u32 seq][f32 mx my yaw pitch][u32 held][u32 pressed]
        PawnAck = 29                 // server -> a client: [string pawn][u32 seq][f32 x y z] where its input seq left the pawn
    };
    constexpr float kClockQuietLimit = 10.0f;   // no good clock answer for this long: reported
    constexpr float kClockReportEvery = 2.0f;   // one drift report per heartbeat window
    void WriteU64(net::Packet& p, std::uint64_t v) { p.Write(static_cast<std::uint32_t>(v)); p.Write(static_cast<std::uint32_t>(v >> 32)); }
    std::uint64_t ReadU64(net::Packet& p) { const std::uint64_t lo = p.ReadU32(); return lo | (static_cast<std::uint64_t>(p.ReadU32()) << 32); }
    // The sealed join: the client trades keys first (JoinHello / JoinKey) and
    // sends its Join encrypted. Version 2 is the first such version (a plain
    // Join is version 1).
    constexpr std::uint8_t kJoinVersion = 2;
    constexpr std::size_t kJoinHelloSize = 128;    // at least JoinKey's size: no amplification
    constexpr int kSealedJoinTries = 4;            // unanswered hellos before the plaintext Join
    constexpr float kPendingJoinTtl = 10.0f;
    // What a pinned client checks the server's signature over.
    std::string JoinKeyMessage(const std::string& serverPk, const std::string& clientPk) {
        return "okayjoin2|" + serverPk + clientPk;
    }
    constexpr float kFragmentTtl = 5.0f;        // drop half-assembled messages after this
    // Outer tag marking an encrypted datagram (nonce||ciphertext follows). Distinct
    // from every Msg type (1..16) and from a plaintext message's leading type byte.
    constexpr std::uint8_t kEncEnvelope = 0xEE;
    constexpr float kClientTimeout = 5.0f;
    // The start of the refusal an encryption-only host sends a plaintext join;
    // a client that can encrypt joins again, sealed.
    constexpr const char* kNeedsEncryption = "encryption required (update the game)";
    constexpr int kHitClaimsPerSecond = 20;    // a client's hit claims without the anti-cheat on
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

namespace {
std::mutex g_liveMutex;
std::unordered_set<const NetworkManager*>& LiveManagers() { static std::unordered_set<const NetworkManager*> s; return s; }
} // namespace

NetworkManager::NetworkManager() { std::lock_guard<std::mutex> l(g_liveMutex); LiveManagers().insert(this); }
NetworkManager::~NetworkManager() { std::lock_guard<std::mutex> l(g_liveMutex); LiveManagers().erase(this); }
bool NetworkManager::IsLive(const NetworkManager* nm) {
    std::lock_guard<std::mutex> l(g_liveMutex);
    return nm && LiveManagers().count(nm) != 0;
}

bool NetworkManager::SeqWindow::Accept(std::uint32_t seq) {
    if (!any) { any = true; newest = seq; std::fill(bits.begin(), bits.end(), 0); bits[0] = 1; return true; }
    if (seq > newest) {
        // Slide: what was `back` behind the newest is now `back + shift` behind.
        const std::uint32_t shift = seq - newest;
        if (shift >= kWidth) std::fill(bits.begin(), bits.end(), 0);
        else {
            const std::size_t ws = shift / 64, bs = shift % 64, n = bits.size();
            for (std::size_t w = n; w-- > 0;) {
                std::uint64_t v = w >= ws ? bits[w - ws] << bs : 0;
                if (bs && w >= ws + 1) v |= bits[w - ws - 1] >> (64 - bs);
                bits[w] = v;
            }
        }
        bits[0] |= 1;
        newest = seq;
        return true;
    }
    const std::uint32_t back = newest - seq;
    if (back >= kWidth) return false;               // older than the window: treat as delivered
    std::uint64_t& word = bits[back / 64];
    const std::uint64_t bit = std::uint64_t(1) << (back % 64);
    if (word & bit) return false;
    word |= bit;
    return true;
}

bool NetworkManager::SeqWindow::Seen(std::uint32_t seq) const {
    if (!any || seq > newest) return false;
    const std::uint32_t back = newest - seq;
    return back >= kWidth || ((bits[back / 64] >> (back % 64)) & 1u);
}

bool NetworkManager::SetHostIdentity(const std::string& seedB64) {
    if (seedB64.empty()) { m_hostSeed.clear(); return true; }
    std::vector<std::uint8_t> seed;
    if (!net::SessionTicket::Base64UrlDecode(seedB64, seed) || seed.size() != 32) return false;
    m_hostSeed = std::move(seed);
    return true;
}

std::string NetworkManager::HostIdentityPublicKey() const {
    if (m_hostSeed.size() != 32) return {};
    okayac::ed25519::Seed seed;
    std::copy(m_hostSeed.begin(), m_hostSeed.end(), seed.begin());
    const auto pk = okayac::ed25519::PublicKeyFromSeed(seed);
    okayac::Wipe(seed.data(), seed.size());
    return net::SessionTicket::Base64UrlEncode(std::vector<std::uint8_t>(pk.begin(), pk.end()));
}

// Encryption asked for but not built in: no host rather than a quietly
// unencrypted one.
static bool EncryptionBuiltIn(bool wanted) {
    if (!wanted || net::SecureChannel::Available()) return true;
    OKAY_ERROR("net: encryption is on, but this build has no libsodium: not hosting (turn encryption off to host unencrypted)");
    return false;
}

bool NetworkManager::StartServer(std::uint16_t port) {
    Stop();
    if (!EncryptionBuiltIn(encryption)) return false;
    if (!net::Startup()) { OKAY_ERROR("net: startup failed"); return false; }
    m_netStarted = true;
    if (!m_socket.Open() || !m_socket.Bind(port)) {
        OKAY_ERROR("net: failed to bind server port ", port);
        Stop();
        return false;
    }
    InitServerState();
    OKAY_INFO("net: server '", serverName, "' listening on UDP ", port,
              " (max ", maxPlayers, ")");
    OKAY_INFO("net: clients may spawn only the ", spawnablePrefabs.size(),
              " Spawnable prefab(s) and the Player Spawner's prefab (the spawn allowlist when the anti-cheat is on)");
    if (anticheat.settings.hitsNeedMoveCheck && maxMoveSpeed <= 0.0f && !authoritativeMovement)
        OKAY_WARN("net: client hit claims (net_hit) are refused: they need a max move speed (net_max_speed, --max-speed) "
                  "or authoritative movement to check where the shooter is (or hitsNeedMoveCheck false in the anti-cheat rules)");
    return true;
}

// Everything a host needs, however it's reached: a bound port (StartServer) or
// a relay session (StartRelay as host). Both then run the same ServerTick, so
// the anti-cheat hooks must be there in both.
void NetworkManager::InitServerState() {
    m_mode = Mode::Server;
    m_localId = 0;
    m_kicked = false; m_kickReason.clear();
    m_serverTime = 0.0f; m_hostScore = 0; m_rosterTimer = 0.0f;
    m_protectionsLogged = false;
    { std::random_device rd; m_querySecret = rd() | 1u; }
    anticheat.Reset();
    // Objects this server drives itself are the server's: no client may move them.
    anticheat.serverOwns = [this](const std::string& id) {
        auto it = m_syncObjs.find(id);
        return it != m_syncObjs.end() && it->second.owned;
    };
    anticheat.exists = [this](const std::string& id) { return m_syncObjs.count(id) != 0; };
    anticheat.positionOf = [this](std::uint32_t peer, Vec3& at) {
        for (const auto& [ep, c] : m_clients)
            if (c.id == peer && c.hasState) { at = Vec3(c.state.x, c.state.y, c.state.z); return true; }
        return false;
    };
    anticheat.claimable = [this](const std::string& id) {
        auto it = m_syncObjs.find(id);
        if (it == m_syncObjs.end() || !it->second.t || !it->second.t->gameObject) return false;
        const auto* ns = it->second.t->gameObject->GetComponent<NetworkSync>();
        return ns && ns->clientClaimable;
    };
    anticheat.onCheat = [this](const CheatReport& r) {
        auto& recent = m_recentCheats[r.peer];   // for a kick's report
        if (recent.size() >= kRecentCheats) recent.erase(recent.begin());
        recent.push_back(r);
        if (m_cheatHandler) m_cheatHandler(r);
    };
    if (snapshotRate > 0.0f) m_snapshotInterval = 1.0f / snapshotRate;
}

int NetworkManager::PlayerCount() const {
    if (m_mode == Mode::Server) return (int)m_clients.size() + (dedicated ? 0 : 1);
    if (m_mode == Mode::Client) return m_joined ? (int)m_remotes.size() + 1 : 0;
    return 0;
}
std::string NetworkManager::ServerAddress() const {
    return (m_mode == Mode::Client && !m_relay) ? m_serverEp.ToString() : std::string();
}
std::string NetworkManager::RelayAddress() const {
    return m_relay ? m_relayEp.ToString() : std::string();
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
    if (asHost && !EncryptionBuiltIn(encryption)) return false;
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
    m_relayHostKey = 0;
    std::memset(m_relayCookie, 0, sizeof m_relayCookie);
    m_relayHelloTimer = 0.0f;
    m_relayError.clear();
    m_kicked = false; m_kickReason.clear();
    if (asHost) {
        InitServerState();
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
    h.Write(net::kRelayProtocolVersion);
    h.Write(static_cast<std::uint32_t>(m_relayHostKey));          // 0 until the relay gives one
    h.Write(static_cast<std::uint32_t>(m_relayHostKey >> 32));
    for (std::uint8_t b : m_relayCookie) h.Write(b);             // zero until the relay sends one
    while (h.Size() < net::kRelayHelloMinBytes) h.Write(std::uint8_t(0));
    m_socket.SendTo(m_relayEp, h.Data(), h.Size());
}

void NetworkManager::Stop() {
    if (m_mode == Mode::Client && m_joined && m_socket.IsOpen()) {
        net::Packet p(Leave);
        p.Write(m_localId);
        if (m_sessionCookie) WriteU64(p, m_sessionCookie);
        SendDatagram(m_serverEp, p.Data(), p.Size());
    }
    if (m_relay && m_socket.IsOpen()) {            // free our slot on the relay
        net::Packet bye(net::RelayBye);
        m_socket.SendTo(m_relayEp, bye.Data(), bye.Size());
    }
    m_relay = false;
    m_relayWelcomed = false;
    m_relaySlot = 0;
    m_relayHostKey = 0;
    std::memset(m_relayCookie, 0, sizeof m_relayCookie);
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
    m_relSeen.Clear();
    m_netTick = 0;
    m_tickAccum = 0.0f;
    m_hostTickRate = 0.0f;
    m_hostAuthoritative = false;
    m_lastAckedSeq = 0;
    m_sessionCookie = 0;
    m_hostAcksPawn = false;
    m_predicting = false;
    m_predicted.clear();
    m_predPending = Vec3(0.0f, 0.0f, 0.0f);
    m_predCmd = PlayerInput{};
    m_predPressOnce = 0;
    m_predSeq = 0;
    m_earlyAckSeq = 0;
    m_lastPawnAck = 0;
    m_predCorrections = m_predSnaps = 0;
    m_interpDelay = m_interpNeed = kInterpTicks;
    m_smoothTick = 0.0; m_smoothTickInit = false;
    m_inputSeq = 0;
    m_pressedAccum = 0;
    m_heldInput = PlayerInput{};
    m_sentInputs.clear();
    m_pawnOwner.clear();
    m_inputsDropped = 0;
    m_statesSent = 0;
    m_acClockMs = 0.0;
    m_gameClockMs = 0.0;
    m_pendingJoins.clear();
    m_joinLimits.clear();
    m_kickBans.clear();
    m_secure = net::SecureChannel{};   // a new session makes new keys (a reconnect mustn't keep the old ones)
    m_joinHellos = 0;
    m_authQueries = 0;
    m_authDecided = false;
    m_hostWantsAuth = false;
    m_keyed = false;
    m_legacyJoin = false;
    m_pinWarned = false;
    m_relSeq = 0;
    m_roster.clear();
    m_corrections = 0;
    // A session's anti-cheat counters and hooks end with it (InitServerState
    // installs fresh ones); the rules themselves (settings) stay.
    anticheat.Reset();
    anticheat.serverOwns = nullptr;
    anticheat.exists = nullptr;
    anticheat.positionOf = nullptr;
    anticheat.claimable = nullptr;
    anticheat.onCheat = nullptr;
    m_recentCheats.clear();
    m_relayLost = false;
    // m_kicked / m_kickReason / m_relayError persist so WasKicked() can be read after the
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
        const bool buffered = InterpolationBuffer();
        AdvanceSmoothTick(dt);
        // The delay follows what the network needs (m_interpNeed) a little at a
        // time, so the shown clock runs slightly slower or faster for a moment
        // instead of jumping; the need eases back to kInterpTicks slowly.
        if (buffered) {
            const double ticks = (double)dt * (double)TickRateInForce();
            m_interpNeed = std::max(kInterpTicks, m_interpNeed - kInterpRecover * ticks);
            const double step = kInterpSlew * ticks;
            if (m_interpDelay < m_interpNeed) m_interpDelay = std::min(m_interpNeed, m_interpDelay + step);
            else m_interpDelay = std::max(m_interpNeed, m_interpDelay - step);
        }
        const double showAt = buffered ? m_smoothTick - m_interpDelay : 0.0;
        for (auto& [id, o] : m_syncObjs) {
            if (o.owned || !o.t || !o.hasTarget) continue;
            if (m_predicting && id == m_pawnId) continue;   // our predicted player: reconciled by PawnAck
            if (buffered && !o.samples.empty()) {
                // Drop what's older than the pair around showAt, then show that moment.
                std::size_t keep = 0;
                while (keep + 1 < o.samples.size() && o.samples[keep + 1].tick <= showAt) ++keep;
                if (keep) o.samples.erase(o.samples.begin(), o.samples.begin() + (std::ptrdiff_t)keep);
                SampleAt(o, showAt, o.t->localPosition, o.t->localRotation);
                continue;
            }
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
        // With a fixed tick: when this state is from (the sender's tick, with
        // its fraction), for receivers' interpolation buffers. Older receivers
        // read the seven numbers and stop.
        if (TickRateInForce() > 0.0f) os << ' ' << std::setprecision(10) << m_smoothTick << std::setprecision(6);
        if (o.getExtra) os << '|' << o.getExtra();   // optional per-object state
        Send("__xform", os.str());
    }
}

double NetworkManager::TickTime() const {
    const float rate = TickRateInForce();
    return rate > 0.0f ? (double)m_netTick + (double)m_tickAccum * (double)rate : 0.0;
}

// A tick clock that runs at the frame rate and is pulled gently toward the
// tick (a client's estimate jumps when a snapshot resyncs it): stamps and
// shows states without that jitter. Far off (a start, a stall): it jumps.
void NetworkManager::AdvanceSmoothTick(float dt) {
    const float rate = TickRateInForce();
    if (rate <= 0.0f) { m_smoothTickInit = false; return; }
    const double target = TickTime();
    if (!m_smoothTickInit || std::fabs(target - m_smoothTick) > 4.0) {
        m_smoothTick = target; m_smoothTickInit = true; return;
    }
    m_smoothTick += (double)dt * (double)rate;
    m_smoothTick += (target - m_smoothTick) * 0.05;
}

// The state at `tick`, between the two buffered samples either side of it;
// before the first, the first; after the last, the last (held, not guessed).
void NetworkManager::SampleAt(const SyncObj& o, double tick, Vec3& pos, Quat& rot) { PoseAt(o.samples, tick, pos, rot); }

void NetworkManager::PoseAt(const std::vector<SyncObj::Sample>& b, double tick, Vec3& pos, Quat& rot) {
    if (tick <= b.front().tick) { pos = b.front().pos; rot = b.front().rot; return; }
    for (std::size_t i = 0; i + 1 < b.size(); ++i)
        if (tick < b[i + 1].tick) {
            const float k = (float)((tick - b[i].tick) / (b[i + 1].tick - b[i].tick));
            pos = b[i].pos + (b[i + 1].pos - b[i].pos) * k;
            rot = Quat::Slerp(b[i].rot, b[i + 1].rot, k);
            return;
        }
    pos = b.back().pos; rot = b.back().rot;
}

// ---- Lag compensation (server) ------------------------------------------------

void NetworkManager::RecordHistory() {
    const std::size_t keep = (std::size_t)std::ceil(HostTickRate()) + 2;   // ~1 s
    for (auto& [id, o] : m_syncObjs) {
        if (!o.t) continue;
        o.history.push_back(SyncObj::Sample{(double)m_netTick, o.t->localPosition, o.t->localRotation});
        if (o.history.size() > keep) o.history.erase(o.history.begin(), o.history.end() - (std::ptrdiff_t)keep);
    }
}

std::vector<NetworkManager::Rewound> NetworkManager::RewindTo(double tick) {
    std::vector<Rewound> saved;
    for (auto& [id, o] : m_syncObjs) {
        if (!o.t || o.history.empty()) continue;
        saved.push_back({o.t, o.t->localPosition, o.t->localRotation});
        PoseAt(o.history, tick, o.t->localPosition, o.t->localRotation);
    }
    return saved;
}

void NetworkManager::Restore(std::vector<Rewound>& saved) {
    for (auto& r : saved) { r.t->localPosition = r.pos; r.t->localRotation = r.rot; }
    saved.clear();
}

// How far back a shot may be checked: what the shooter says it saw, but no
// further than 200 ms plus its round trip (nor the history's second), and
// never in the future.
double NetworkManager::ClampRewind(double viewTick, float rttSeconds) const {
    const double now = TickTime();
    const double rate = (double)HostTickRate();
    const double most = std::min(1.0, (double)kRewindBase + (double)std::max(0.0f, rttSeconds)) * rate;
    if (!std::isfinite(viewTick)) return now;
    return std::max(now - most, std::min(now, viewTick));
}

// The cookie at the end of a plaintext client's Leave / State / Message /
// SyncVar. A client that has sent it once must always send it; one that never
// has (an older client) is still believed.
bool NetworkManager::CookieOk(Client& c, net::Packet& p) const {
    if (c.cookie == 0) return true;
    const std::uint64_t got = ReadU64(p);
    if (!p.Ok()) return !c.cookieSeen;
    if (got != c.cookie) return false;
    c.cookieSeen = true;
    return true;
}

RaycastHit3D NetworkManager::RewindRaycast(const Vec3& origin, const Vec3& direction, float maxDistance,
                                           double viewTick, GameObject* ignore) {
    Scene* sc = GetScene();
    if (!sc) return RaycastHit3D{};
    if (m_mode != Mode::Server || HostTickRate() <= 0.0f)
        return sc->physics3D().Raycast(*sc, origin, direction, maxDistance, ignore);
    const double at = std::max(TickTime() - (double)HostTickRate(), std::min(TickTime(), std::isfinite(viewTick) ? viewTick : TickTime()));
    std::vector<Rewound> saved = RewindTo(at);
    RaycastHit3D hit = sc->physics3D().Raycast(*sc, origin, direction, maxDistance, ignore);
    Restore(saved);
    return hit;
}

double NetworkManager::ViewTick() const {
    if (TickRateInForce() <= 0.0f) return 0.0;
    if (m_mode != Mode::Client) return TickTime();
    return (m_smoothTickInit ? m_smoothTick : TickTime()) - (InterpolationBuffer() ? m_interpDelay : 0.0);
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
        // "<path>|x|y|z[|<netId>]"; an optional netId marks a SpawnOwned object:
        // attach a follower NetworkSync so its transform/animation track the
        // authority that spawned it. (The same parser the anti-cheat checks with.)
        const SpawnMessage m = ParseSpawnMessage(data);
        // The server checked a client's spawn before relaying it; a client
        // checks what the host sends, since nothing on the wire is trusted.
        if (m_mode == Mode::Client && !SpawnPathOk(from, m.path, false)) return;
        const std::string& netId = m.netId;
        if (GameObject* g = SceneSerializer::InstantiateFromFile(*s, m.path)) {
            if (g->transform) g->transform->localPosition = m.pos;
            if (!netId.empty()) {
                auto* ns = g->GetComponent<NetworkSync>();
                if (!ns) ns = g->AddComponent<NetworkSync>();
                ns->netId = netId; ns->authority = NetworkSync::Authority::Manual; ns->owned = false;
            }
        }
        return;
    }
    if (channel == "__dmg") {   // "<netId>|<health>|<damage>|<shooter>": the server applied a hit
        if (m_mode != Mode::Client || from != 0) return;
        const std::size_t a = data.find('|');
        if (a == std::string::npos) return;
        auto obj = m_syncObjs.find(data.substr(0, a));
        if (obj == m_syncObjs.end() || !obj->second.t || !obj->second.t->gameObject) return;
        if (auto* hs = obj->second.t->gameObject->GetComponent<HealthStat>())
            hs->SetHealthFromServer(std::strtof(data.c_str() + a + 1, nullptr));
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
                double tick = -1.0;
                if (!(ss >> tick) || !std::isfinite(tick)) tick = -1.0;   // an older sender: stamp it on arrival
                it->second.targetPos = p;
                it->second.targetRot = q;
                if (TickRateInForce() > 0.0f) {
                    auto& buf = it->second.samples;
                    const double now = m_smoothTick;
                    // A sender's clock that's far off ours (or none) is stamped
                    // with when it arrived instead.
                    if (tick < 0.0 || std::fabs(tick - now) > 2.0 * TickRateInForce()) tick = now;
                    // Kept in tick order (datagrams can arrive out of order).
                    auto at = buf.end();
                    while (at != buf.begin() && (at - 1)->tick > tick) --at;
                    const bool newest = at == buf.end();
                    const double gap = (newest && !buf.empty()) ? tick - buf.back().tick : 0.0;
                    if (at == buf.begin() || (at - 1)->tick < tick) buf.insert(at, SyncObj::Sample{tick, p, q});
                    if (buf.size() > kMaxSamples) buf.erase(buf.begin());
                    // Show far enough back that the next state is (nearly) always
                    // here in time: how late this one came, plus the gap to the one
                    // after it. Rises at once, eases back down (SyncTick).
                    if (newest) {
                        const double need = std::min((now - tick) + gap + 1.0, (double)TickRateInForce());
                        if (need > m_interpNeed) m_interpNeed = need;
                    }
                }
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

void NetworkManager::DespawnPeerObjects(std::uint32_t id) {
    const std::string prefix = "sp/" + std::to_string(id) + "/";
    std::vector<std::string> ids;
    for (const auto& [netId, o] : m_syncObjs) if (netId.compare(0, prefix.size(), prefix) == 0) ids.push_back(netId);
    for (const auto& netId : ids) {
        DestroyLocalSync(netId);
        Send("__despawn", netId);   // and on every client still here
    }
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

namespace {
    std::string g_launchJoin;          // pending launcher join (consumed by the next Start)
    bool        g_launchedToJoin = false;
    std::string g_defaultToken, g_defaultName;
}
void NetworkManager::SetLaunchJoin(const std::string& link) {
    g_launchJoin = link;
    if (!link.empty()) g_launchedToJoin = true;
}
std::string NetworkManager::TakeLaunchJoin() { std::string l; l.swap(g_launchJoin); return l; }
bool NetworkManager::LaunchedToJoin() { return g_launchedToJoin; }
void NetworkManager::SetDefaultIdentity(const std::string& token, const std::string& name) {
    g_defaultToken = token; g_defaultName = name;
}

bool NetworkManager::JoinByLink(const std::string& text) {
    JoinLink l;
    if (!JoinLink::Parse(text, l)) { OKAY_WARN("net: not a join link: ", text); return false; }
    if (!l.room.empty()) m_localRoom = l.room;
    if (!l.password.empty()) password = l.password;
    if (l.Relayed()) {
        std::string h; std::uint16_t port = 0;
        if (!SplitHostPort(l.relay, h, port, 45100)) return false;
        return JoinViaRelay(h, port, l.code);
    }
    std::string h; std::uint16_t port = 0;
    if (!SplitHostPort(l.addr, h, port)) return false;
    return StartClient(h, port);
}

void NetworkManager::Start() {
    if (m_authToken.empty() && !g_defaultToken.empty()) m_authToken = g_defaultToken;
    if (m_localName == "Player" && !g_defaultName.empty()) m_localName = g_defaultName;
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
    const std::string launch = TakeLaunchJoin();   // launched from a friend / invite / server browser
    if (!launch.empty() && JoinByLink(launch)) return;
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
        if (m_sessionCookie) WriteU64(p, m_sessionCookie);
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
    if (m_mode == Mode::Server) {
        auto it = m_clients.find(ep);
        if (it != m_clients.end()) return &it->second.secure;
        auto pj = m_pendingJoins.find(ep);   // a sealed join in progress
        return pj != m_pendingJoins.end() ? &pj->second.secure : nullptr;
    }
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
            if (!p.Ok()) continue;
            // Version 2 relays add [u8 version][u64 host key]; the key comes
            // only to the host, which sends it back in every hello so only it
            // can host this code (from this address or a new one).
            p.ReadU8();
            const std::uint32_t keyLo = p.ReadU32(), keyHi = p.ReadU32();
            if (p.Ok() && m_mode == Mode::Server && (keyLo | keyHi))
                m_relayHostKey = (static_cast<std::uint64_t>(keyHi) << 32) | keyLo;
            if (m_mode == Mode::Client) {
                // The host we were paired with is the only server we talk to.
                // Any change (it left: slot 0, or another host took the code)
                // ends this session instead of re-pointing us at someone else.
                if (m_relayWelcomed && !(m_serverEp == SlotEndpoint(hostSlot))) {
                    m_relayError = hostSlot ? "the session's host changed" : "the host left the session";
                    OKAY_WARN("net: relay session '", m_relayCode, "' ended: ", m_relayError);
                    m_relayLost = true;
                    return 0;
                }
                if (hostSlot == 0) continue;   // no host yet
                m_serverEp = SlotEndpoint(hostSlot);
            }
            m_relaySlot = yourSlot;
            m_relayWelcomed = true;
            continue;
        }
        if (tag == net::RelayCookie) {
            // A version-3 relay wants proof we receive at our address: hello
            // again now with its cookie.
            net::Packet p(b, static_cast<std::size_t>(n));
            p.ReadU8();
            p.ReadU8();
            std::uint8_t c[sizeof m_relayCookie];
            for (auto& x : c) x = p.ReadU8();
            if (!p.Ok()) continue;
            std::memcpy(m_relayCookie, c, sizeof c);
            m_relayHelloTimer = 0.0f;
            continue;
        }
        if (tag == net::RelayRefused) {
            net::Packet p(b, static_cast<std::size_t>(n));
            p.ReadU8();
            const std::uint8_t why = p.ReadU8();
            const std::uint8_t relayVersion = p.ReadU8();
            if (!p.Ok()) continue;
            m_relayError = why == net::RelayRefusedHosted
                ? "session code '" + m_relayCode + "' is already hosted"
                : "the relay needs protocol version " + std::to_string(relayVersion) +
                  " (this game speaks " + std::to_string(net::kRelayProtocolVersion) + ")";
            OKAY_WARN("net: relay refused us: ", m_relayError);
            m_relayLost = true;
            return 0;
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
// A peer we've completed a handshake with: a joined client (server side) or our
// server (client side).
bool NetworkManager::Handshaken(const net::Endpoint& from) const {
    if (m_mode == Mode::Server) return m_clients.find(from) != m_clients.end();
    if (m_mode == Mode::Client) return from == m_serverEp;
    return false;
}

bool NetworkManager::TakeFragment(const net::Endpoint& from, const std::uint8_t* data,
                                  std::size_t size, std::vector<std::uint8_t>& out) {
    const std::size_t header = 1 + 4 + 4 + 4;
    // Senders keep every fragment under the threshold; anything bigger is forged.
    if (size < header || size > net::kFragmentThreshold) return false;
    net::Packet p(data, size);
    p.ReadU8();                                   // Fragment tag
    std::uint32_t id = p.ReadU32(), index = p.ReadU32(), count = p.ReadU32();
    const bool known = Handshaken(from);
    const std::uint32_t maxCount = known ? kMaxFragments : kStrangerMaxFragments;
    if (!p.Ok() || count == 0 || count > maxCount || index >= count) return false;
    auto epIt = m_fragIn.find(from);
    if (epIt == m_fragIn.end()) {
        if (!known) {                             // only so many strangers mid-message at once
            std::size_t strangers = 0;
            for (const auto& e : m_fragIn) if (!Handshaken(e.first)) ++strangers;
            if (strangers >= kMaxStrangers) return false;
        }
        epIt = m_fragIn.emplace(from, std::unordered_map<std::uint32_t, FragAsm>{}).first;
    }
    auto& msgs = epIt->second;
    auto mIt = msgs.find(id);
    if (mIt == msgs.end()) {
        if (msgs.size() >= (known ? kMaxPendingMessages : 1)) return false;
        mIt = msgs.emplace(id, FragAsm{}).first;
        mIt->second.count = count;
        mIt->second.chunks.resize(count);
    }
    FragAsm& asm_ = mIt->second;
    if (asm_.count != count) return false;        // inconsistent — drop this stray fragment
    if (asm_.chunks[index].empty()) {             // first time we see this index
        std::size_t held = 0;
        for (const auto& m : msgs) held += m.second.bytes;
        if (held + (size - header) > kMaxPendingBytes) return false;
        asm_.chunks[index].assign(data + header, data + size);
        asm_.bytes += size - header;
        asm_.got++;
    }
    asm_.age = 0.0f;
    if (asm_.got < asm_.count) return false;
    out.clear();
    for (auto& c : asm_.chunks) out.insert(out.end(), c.begin(), c.end());
    msgs.erase(mIt);
    if (msgs.empty()) m_fragIn.erase(epIt);
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

namespace {
// "x y z", all of it finite.
bool ParseVec3(const std::string& s, Vec3& out) {
    const char* p = s.c_str();
    float v[3];
    for (float& f : v) {
        char* end = nullptr;
        f = std::strtof(p, &end);
        if (end == p || !std::isfinite(f)) return false;
        p = end;
    }
    while (*p == ' ') ++p;
    if (*p) return false;
    out = Vec3(v[0], v[1], v[2]);
    return true;
}
// Is `o` the object `root`, or one of its children?
bool IsPartOf(const GameObject* o, const GameObject* root) {
    if (!o || !root) return false;
    for (const Transform* t = o->transform; t; t = t->Parent())
        if (t->gameObject == root) return true;
    return false;
}
} // namespace

void NetworkManager::ClaimHit(const std::string& targetNetId, const std::string& weapon,
                              const Vec3& origin, const Vec3& direction) {
    const float len = direction.Magnitude();
    if (targetNetId.empty() || len < 1e-6f || targetNetId.find('|') != std::string::npos ||
        weapon.find('|') != std::string::npos) return;
    const Vec3 dir = direction * (1.0f / len);
    if (m_mode == Mode::Server) {   // the host's own shot: trusted, only the game's rules apply
        HitClaim claim;
        claim.target = targetNetId; claim.weapon = weapon; claim.clientTick = (std::uint32_t)m_tickCount;
        claim.origin = origin; claim.direction = dir; claim.damage = anticheat.settings.hitDamage;
        if (anticheat.validateHit && !anticheat.validateHit(claim)) { ++m_hitsRefused; return; }
        ApplyHit(claim);
        return;
    }
    if (m_mode != Mode::Client || !m_joined) return;
    char buf[224];
    // "<frames>[:<view tick>]": an older server reads the frame count and stops.
    char tick[64];
    if (TickRateInForce() > 0.0f) std::snprintf(tick, sizeof tick, "%u:%.3f", (unsigned)m_tickCount, ViewTick());
    else std::snprintf(tick, sizeof tick, "%u", (unsigned)m_tickCount);
    std::snprintf(buf, sizeof buf, "|%s|%.9g %.9g %.9g|%.9g %.9g %.9g", tick,
                  origin.x, origin.y, origin.z, dir.x, dir.y, dir.z);
    SendReliable("__hit", targetNetId + "|" + weapon + buf);
}

// "<targetNetId>|<weapon>|<clientTick>|ox oy oz|dx dy dz" from client `c`. The
// server believes its own view: where it last saw the shooter, where the
// target is, what's in between.
void NetworkManager::HandleHitClaim(Client& c, const std::string& data) {
    const std::uint32_t shooter = c.id;
    auto refuse = [&](const std::string& why) {
        ++m_hitsRefused;
        anticheat.Report(shooter, CheatKind::HitRejected, why);
        AntiCheatAfterRefusal(shooter);   // (may kick: c is gone after this)
    };
    // Without the anti-cheat's RPC limit, a fixed one: a client can't make the
    // server raycast and rewind for a flood of claims.
    if (!anticheat.settings.enabled && ++c.hitClaims > kHitClaimsPerSecond) { refuse("too many hit claims"); return; }
    std::vector<std::string> part;
    for (std::size_t a = 0;;) {
        const std::size_t b = data.find('|', a);
        part.push_back(data.substr(a, b == std::string::npos ? std::string::npos : b - a));
        if (b == std::string::npos) break;
        a = b + 1;
    }
    HitClaim claim;
    claim.shooter = shooter;
    Vec3 dir;
    char* end = nullptr;
    if (part.size() != 5 || part[0].empty() || part[0].size() > 128 || part[1].size() > 64 ||
        !ParseVec3(part[3], claim.origin) || !ParseVec3(part[4], dir) || dir.Magnitude() < 1e-6f) {
        refuse("malformed hit claim");
        return;
    }
    claim.target = part[0];
    claim.weapon = part[1];
    claim.clientTick = static_cast<std::uint32_t>(std::strtoul(part[2].c_str(), &end, 10));
    double viewTick = -1.0;   // lag compensation: the moment the shooter saw
    if (end && *end == ':') { viewTick = std::strtod(end + 1, nullptr); if (!std::isfinite(viewTick) || viewTick < 0.0) viewTick = -1.0; }
    claim.direction = dir * (1.0f / dir.Magnitude());
    claim.damage = anticheat.settings.hitDamage;
    const AntiCheatSettings& s = anticheat.settings;
    if (s.enabled) {
        // Only a weapon the game knows (a Net Validator for "hit/<weapon>", or
        // a "weapon" in the rules) may claim, so an invented name can't dodge
        // the fire rate and ammo; and every claim counts against the RPC rate.
        // An unknown one does nothing but isn't a violation: a game that
        // hasn't listed its weapons yet mustn't get honest players kicked.
        if (!anticheat.KnownWeapon(claim.weapon)) {
            ++m_hitsRefused;
            if (m_unknownWeapons.size() < 32 && m_unknownWeapons.insert(claim.weapon).second) {
                std::string w = claim.weapon;
                for (char& ch : w) if ((unsigned char)ch < 0x20 || ch == 0x7f) ch = '?';
                OKAY_WARN("anti-cheat: refused hits with weapon \"", w, "\": add a Net Validator for hit/", w,
                          " or \"weapon ", w, "\" to the rules");
            }
            return;
        }
        if (!anticheat.CountRpc(shooter, "hit/" + claim.weapon)) { ++m_hitsRefused; AntiCheatAfterRefusal(shooter); return; }
    }

    // The shooter is alive (when its avatar, or the pawn the server moves for it, has health).
    if (auto r = m_remotes.find(shooter); r != m_remotes.end() && r->second)
        if (auto* hs = r->second->GetComponent<HealthStat>(); hs && hs->IsDead()) { refuse("shooter is dead"); return; }
    if (!c.pawn.empty())
        if (auto p = m_syncObjs.find(c.pawn); p != m_syncObjs.end() && p->second.t && p->second.t->gameObject)
            if (auto* hs = p->second.t->gameObject->GetComponent<HealthStat>(); hs && hs->IsDead()) { refuse("shooter is dead"); return; }
    // The weapon's fire rate and ammo: its Net Validator ("hit/<weapon>"), as for an RPC.
    if (anticheat.validateRpc && !anticheat.validateRpc(shooter, "hit/" + claim.weapon, data)) {
        refuse("fire rate or ammo: " + claim.weapon);
        return;
    }
    // Fired from where the server last saw the shooter, not where it says it is.
    if (!c.hasState) { refuse("no position for the shooter yet"); return; }
    // ... which only means something when that position is checked: the
    // server moves the shooter (authoritative movement), or maxMoveSpeed
    // limits what it reports. Without either, the claim is refused (the
    // server's setup, not the player's doing: no violation).
    if (s.hitsNeedMoveCheck && c.pawn.empty() && maxMoveSpeed <= 0.0f) { ++m_hitsRefused; return; }
    // The target, where the server has it.
    auto obj = m_syncObjs.find(claim.target);
    if (obj == m_syncObjs.end() || !obj->second.t || !obj->second.t->gameObject) { refuse("no such target: " + claim.target); return; }
    const float rtt = c.rtt >= 0.0f ? std::min(c.rtt, 1.0f) : 0.25f;
    // Lag compensation: with a fixed tick and the moment the shooter saw,
    // everything goes back to where it was then (clamped to 200 ms + its round
    // trip) for the checks below, and is put back after. The target then
    // only gets a tick's movement of slack, not its speed x the ping.
    std::vector<Rewound> rewound;
    m_lastRewind = 0.0f;
    if (viewTick >= 0.0 && HostTickRate() > 0.0f) {
        const double at = ClampRewind(viewTick, rtt);
        m_lastRewind = (float)((TickTime() - at) / (double)HostTickRate());
        rewound = RewindTo(at);
    }
    // The shooter's position: its pawn, where the server has it (rewound with
    // everything else under lag compensation), else its checked report.
    Vec3 from(c.state.x, c.state.y, c.state.z);
    if (!c.pawn.empty())
        if (auto p = m_syncObjs.find(c.pawn); p != m_syncObjs.end() && p->second.t)
            from = p->second.t->TransformPoint(Vec3(0.0f, 0.0f, 0.0f));
    if ((claim.origin - from).Magnitude() > s.hitOriginTolerance) {
        Restore(rewound);
        refuse("shot from where the shooter isn't");
        return;
    }
    const std::string why = CheckHitView(claim, c.id, obj->second.t, rewound.empty() ? rtt : 1.0f / HostTickRate());
    Restore(rewound);
    if (!why.empty()) { refuse(why); return; }
    // The game's own rules (Validate Hit / on_validate_hit / NetworkManager::ValidateHit).
    if (anticheat.validateHit && !anticheat.validateHit(claim)) { refuse("refused by the game: " + claim.weapon); return; }
    ApplyHit(claim);
}

// The view checks of a hit claim: the target in front, in range, on the ray
// (within the tolerance plus `slack` seconds of its top speed), and nothing
// solid in between. "" = it hit; else why not.
std::string NetworkManager::CheckHitView(const HitClaim& claim, std::uint32_t shooter, Transform* targetT, float slack) {
    const AntiCheatSettings& s = anticheat.settings;
    GameObject* target = targetT->gameObject;
    const Vec3 tp = targetT->TransformPoint(Vec3(0.0f, 0.0f, 0.0f));
    const Vec3 to = tp - claim.origin;
    const float along = Vec3::Dot(to, claim.direction);
    if (along < 0.0f) return "target behind the shot";
    if (s.maxHitRange > 0.0f && along > s.maxHitRange) return "out of range";
    // Off the ray by no more than the tolerance plus how far the target can
    // have moved in `slack` (the shooter's ping, or a tick when rewound).
    const float speed = s.maxSyncSpeed > 0.0f ? s.maxSyncSpeed : 8.0f;
    if ((to - claim.direction * along).Magnitude() > s.hitTolerance + speed * slack) return "missed: the target wasn't there";
    // Nothing solid in between (a server raycast, 3D and 2D).
    if (s.hitLineOfSight && GetScene()) {
        Scene& sc = *GetScene();
        GameObject* self = nullptr;
        if (auto r = m_remotes.find(shooter); r != m_remotes.end()) self = r->second;
        bool blocked = false;
        for (const auto& h : sc.physics3D().RaycastAll(sc, claim.origin, claim.direction, along, nullptr,
                                                        Physics3D::kAllLayers, /*queryTriggers=*/false)) {
            if (IsPartOf(h.gameObject, target)) break;   // reached it
            if (IsPartOf(h.gameObject, self)) continue;
            if (h.distance < along - 0.05f) { blocked = true; break; }
        }
        const Vec2 d2(claim.direction.x, claim.direction.y);
        if (!blocked && d2.Magnitude() > 1e-6f) {
            const float along2 = Vec2(to.x, to.y).Magnitude();
            for (const auto& h : sc.physics().RaycastAll(sc, Vec2(claim.origin.x, claim.origin.y), d2 * (1.0f / d2.Magnitude()),
                                                         along2, nullptr, Physics2D::kAllLayers, /*queryTriggers=*/false)) {
                if (IsPartOf(h.gameObject, target)) break;
                if (IsPartOf(h.gameObject, self)) continue;
                if (h.distance < along2 - 0.05f) { blocked = true; break; }
            }
        }
        if (blocked) return "through a wall";
    }
    return "";
}

// Damage on the server only, through the target's HealthStat, then the result
// to every client ("__dmg").
void NetworkManager::ApplyHit(HitClaim& claim) {
    auto obj = m_syncObjs.find(claim.target);
    if (obj == m_syncObjs.end() || !obj->second.t || !obj->second.t->gameObject) return;
    auto* hs = obj->second.t->gameObject->GetComponent<HealthStat>();
    if (!hs || claim.damage <= 0.0f || !std::isfinite(claim.damage)) return;
    hs->Damage(claim.damage);
    ++m_hitsApplied;
    char buf[96];
    std::snprintf(buf, sizeof buf, "|%.9g|%.9g|%u", hs->health, claim.damage, (unsigned)claim.shooter);
    SendReliable("__dmg", claim.target + buf);
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
        anticheat.ParkPeer(id, it->second.ident);
        m_clients.erase(it);
        anticheat.ForgetPeer(id);
        DespawnPeerObjects(id);
        m_recentCheats.erase(id);
        for (auto po = m_pawnOwner.begin(); po != m_pawnOwner.end();) po = po->second == id ? m_pawnOwner.erase(po) : std::next(po);
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
    if (m_mode == Mode::Server && id == 0) return m_localName;
    for (const auto& e : m_roster) if (e.id == id) return e.name;
    return {};
}

std::uint32_t NetworkManager::QueryChallenge(const net::Endpoint& from) const {
    std::uint32_t h = (from.address * 2654435761u) ^ (std::uint32_t(from.port) * 40503u) ^ m_querySecret;
    h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
    return h ? h : 1u;
}

void NetworkManager::ServerTeleport(std::uint32_t id, const Vec3& pos) {
    if (m_mode != Mode::Server) return;
    if (id == 0) { if (m_localAvatar) m_localAvatar->localPosition = pos; return; }
    for (auto& [ep, c] : m_clients) {
        if (c.id != id) continue;
        c.state.x = pos.x; c.state.y = pos.y; c.state.z = pos.z;
        c.hasState = true; c.stateAge = 0.0f;
        net::Packet cp(Correct); cp.Write(pos.x); cp.Write(pos.y); cp.Write(pos.z);
        SendDatagram(c.endpoint, cp.Data(), cp.Size());
        ApplyPeer(c.state);
        return;
    }
}

int NetworkManager::PeerViolations(std::uint32_t id) const {
    for (auto& [ep, c] : m_clients) if (c.id == id) return c.violations;
    return 0;
}

void NetworkManager::SetPeerScore(std::uint32_t id, int score) {
    if (m_mode != Mode::Server) return;
    if (id == 0) { m_hostScore = score; return; }
    for (auto& [ep, c] : m_clients) if (c.id == id) { c.score = score; return; }
}

void NetworkManager::BeginMatch() {
    if (m_mode != Mode::Server) return;
    m_hostScore = 0;
    for (auto& [ep, c] : m_clients) c.score = 0;
    m_matchOn = true; m_matchEnded = false;
    m_matchStartAt = m_serverTime;
}

float NetworkManager::EndMatch() {
    if (m_mode != Mode::Server) return 0.0f;
    const float len = MatchSeconds();
    if (m_matchOn) { m_matchOn = false; m_matchEnded = true; m_matchEndAt = m_serverTime; }
    return len;
}

int NetworkManager::PeerScore(std::uint32_t id) const {
    if (m_mode == Mode::Server) {
        if (id == 0) return m_hostScore;
        for (auto& [ep, c] : m_clients) if (c.id == id) return c.score;
        return 0;
    }
    for (const auto& e : m_roster) if (e.id == id) return e.score;
    return 0;
}

std::vector<NetworkManager::RosterEntry> NetworkManager::Roster() const {
    if (m_mode == Mode::Client) return m_roster;
    std::vector<RosterEntry> out;
    if (m_mode != Mode::Server) return out;
    if (!dedicated) out.push_back({0, m_localName, std::string(), m_hostScore, m_serverTime});
    for (auto& [ep, c] : m_clients) out.push_back({c.id, c.name, c.userId, c.score, m_serverTime - c.joinedAt});
    std::sort(out.begin(), out.end(), [](const RosterEntry& a, const RosterEntry& b) { return a.id < b.id; });
    return out;
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
        if (m_sessionCookie) WriteU64(p, m_sessionCookie);
        SendDatagram(m_serverEp, p.Data(), p.Size());
    }
}

bool NetworkManager::ClientMessageOk(const Client& from, const std::string& channel, std::string& data) const {
    // Nothing big enough to choke every peer it's relayed to.
    if (maxRelayedBytes > 0 && channel.size() + data.size() > (std::size_t)maxRelayedBytes) return false;
    if (channel.size() < 2 || channel[0] != '_' || channel[1] != '_') return true;   // a game channel
    if (channel == "__chat") {
        // "<name>\n<text>": the name is the server's, not the sender's choice;
        // the text is cut to 512 bytes (on a UTF-8 character boundary).
        const std::size_t nl = data.find('\n');
        std::string text = nl == std::string::npos ? data : data.substr(nl + 1);
        if (text.size() > 512) {
            std::size_t cut = 512;
            while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
            text.resize(cut);
        }
        data = from.name + "\n" + text;
        return true;
    }
    if (channel == "__xform" && m_pawnOwner.count(data.substr(0, data.find('|'))))
        return false;   // a player the server moves: its position is the server's, never the client's
    if (channel == "__spawn") {
        const SpawnMessage m = ParseSpawnMessage(data);
        if (!std::isfinite(m.pos.x) || !std::isfinite(m.pos.y) || !std::isfinite(m.pos.z)) return false;
        return SpawnPathOk(from.id, m.path, true);
    }
    if (channel == "__despawn" || channel == "__xform") return true;
    if (channel.size() > 6 && channel.compare(0, 6, "__rpc/") == 0) return true;
    return false;   // __match_start and any other reserved channel: host only
}

namespace {
// "a\\b" and "./a/b" name the same prefab as "a/b".
std::string NetPrefabKey(std::string p) {
    std::replace(p.begin(), p.end(), '\\', '/');
    while (p.rfind("./", 0) == 0) p.erase(0, 2);
    return p;
}
} // namespace

bool NetworkManager::IsSafeNetworkPrefabPath(const std::string& path, std::string* why) {
    namespace fs = std::filesystem;
    auto no = [&](const char* w) { if (why) *why = w; return false; };
    // A leading separator is an absolute, UNC, \\?\ or \\.\ path; any other
    // backslash is just a Windows separator.
    if (path.empty() || path[0] == '/' || path[0] == '\\') return no("not a relative path");
    const std::string rel = NetPrefabKey(path);
    if (!okayofficial::SafeRelPath(rel, why)) return false;   // "..", drives, device names...
    const std::string ext = ".okayprefab";
    if (rel.size() <= ext.size() || okayofficial::Lower(rel.substr(rel.size() - ext.size())) != ext)
        return no("not a .okayprefab");
    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);   // where prefab paths are opened from
    const fs::path full = ec ? fs::path() : okayofficial::ResolveInside(cwd, rel);
    if (full.empty()) return no("outside the game folder");
    if (!fs::is_regular_file(full, ec)) return no("not a file");
    const std::uintmax_t size = fs::file_size(full, ec);
    if (ec || size > kMaxNetworkPrefabBytes) return no("too large");
    return true;
}

bool NetworkManager::ClientMaySpawn(const std::string& path) const {
    const std::string key = NetPrefabKey(path);
    for (const auto& p : spawnablePrefabs) if (NetPrefabKey(p) == key) return true;
    if (const Scene* s = GetScene())
        for (const auto& go : s->Objects())
            if (auto* sp = go->GetComponent<NetworkPlayerSpawner>())
                if (!sp->prefabFile.empty() && NetPrefabKey(sp->prefabFile) == key) return true;
    return false;
}

bool NetworkManager::SpawnPathOk(std::uint32_t from, const std::string& path, bool fromClient) const {
    std::string why;
    if (IsSafeNetworkPrefabPath(path, &why)) {
        // With the anti-cheat on, its allowlist judges a client's prefab next.
        if (!fromClient || anticheat.settings.enabled || ClientMaySpawn(path)) return true;
        why = "clients may not spawn it here (add it to the NetworkManager's Spawnable prefabs)";
    }
    if (m_spawnRefusalLogged.size() > 256) m_spawnRefusalLogged.clear();
    if (m_spawnRefusalLogged.insert(from).second)
        OKAY_WARN("net: refused a spawn of \"", okayofficial::CleanText(path, 120, false), "\" from peer ", from, ": ", why);
    return false;
}

bool NetworkManager::AntiCheatAllows(std::uint32_t peer, const std::string& channel, const std::string& data) {
    if (!anticheat.settings.enabled) return true;
    if (anticheat.ValidateMessage(peer, channel, data)) return true;
    AntiCheatAfterRefusal(peer);
    return false;
}

bool NetworkManager::AntiCheatAllowsSyncVar(std::uint32_t peer, const std::string& key) {
    if (!anticheat.settings.enabled) return true;
    if (anticheat.ValidateSyncVar(peer, key)) return true;
    AntiCheatAfterRefusal(peer);
    return false;
}

bool NetworkManager::JoinBlocked(const net::Endpoint& from) const {
    auto it = m_joinLimits.find(from.address);
    return it != m_joinLimits.end() && m_serverTime < it->second.blockedUntil;
}

void NetworkManager::CountJoinAttempt(const net::Endpoint& from, int cost) {
    if (m_joinLimits.size() > 4096)   // addresses long quiet go (the map can't grow without end)
        for (auto it = m_joinLimits.begin(); it != m_joinLimits.end();)
            it = m_serverTime - it->second.windowStart > 600.0f && m_serverTime > it->second.blockedUntil ? m_joinLimits.erase(it) : std::next(it);
    JoinLimit& l = m_joinLimits[from.address];
    if (m_serverTime - l.windowStart >= kJoinWindow) {
        l.windowStart = m_serverTime; l.count = 0;
        if (m_serverTime > l.blockedUntil + 60.0f) l.backoff = 0.0f;   // a quiet minute after a block forgives it
    }
    l.count += cost;
    if (l.count > kJoinAttempts) {
        l.backoff = l.backoff > 0.0f ? std::min(l.backoff * 2.0f, 320.0f) : 10.0f;
        l.blockedUntil = m_serverTime + l.backoff;
        l.count = 0; l.windowStart = m_serverTime; l.told = false;
        OKAY_WARN("net: too many join attempts from ", from.ToString(), "; ignoring it for ", (int)l.backoff, " s");
    }
}

float NetworkManager::KickBanLeft(const std::string& ident) const {
    auto it = m_kickBans.find(ident);
    return it == m_kickBans.end() ? 0.0f : std::max(0.0f, it->second.until - m_serverTime);
}

void NetworkManager::AntiCheatAfterRefusal(std::uint32_t peer) {
    const int limit = anticheat.settings.kickAfterViolations;
    if (limit > 0 && anticheat.Violations(peer) >= limit) {
        if (m_acKickHandler) {
            AntiCheatKick k;
            k.peer = peer;
            for (auto& [ep, c] : m_clients) if (c.id == peer) { k.name = c.name; k.userId = c.userId; }
            if (auto r = m_recentCheats.find(peer); r != m_recentCheats.end()) k.recent = r->second;
            m_acKickHandler(k);
        }
        // Kicked: not back for a minute (account and address), doubling on
        // each repeat up to an hour; so a kick isn't just a reconnect.
        for (auto it = m_kickBans.begin(); it != m_kickBans.end();)
            it = m_serverTime > it->second.until + 3600.0f ? m_kickBans.erase(it) : std::next(it);
        for (auto& [ep, c] : m_clients) {
            if (c.id != peer) continue;
            std::vector<std::string> keys{c.ident};
            const std::string addr = "a:" + std::to_string(ep.address);
            if (addr != c.ident) keys.push_back(addr);
            for (const auto& key : keys) {
                KickBan& b = m_kickBans[key];
                b.strikes = std::min(b.strikes + 1, 7);
                b.until = m_serverTime + std::min(3600.0f, 60.0f * float(1 << (b.strikes - 1)));
            }
            break;
        }
        Kick(peer, "anti-cheat");   // (parks and forgets its anti-cheat state too)
    }
}

// Step the fixed tick by this frame's time; returns how many ticks elapsed
// (at most 8: a long stall doesn't replay a burst of ticks).
int NetworkManager::StepNetTick(float rate, float dt) {
    if (rate <= 0.0f) return 0;
    const float step = 1.0f / rate;
    m_tickAccum += dt;
    int ticks = 0;
    while (m_tickAccum >= step) {
        m_tickAccum -= step;
        ++m_netTick;
        if (m_mode == Mode::Server && authoritativeMovement) ServerMovementTick(step);
        else if (m_mode == Mode::Client && m_hostAuthoritative) ClientInputTick();
        if (m_mode == Mode::Server) RecordHistory();
        if (m_onNetTick) m_onNetTick(m_netTick, step);
        if (++ticks >= 8) { m_tickAccum = 0.0f; break; }
    }
    return ticks;
}

void NetworkManager::SetPawn(GameObject* pawn) {
    m_pawnId.clear();
    if (!pawn) return;
    if (auto* ns = pawn->GetComponent<NetworkSync>()) m_pawnId = ns->netId.empty() ? pawn->name : ns->netId;
}

int NetworkManager::MovementAuthority(const std::string& netId) const {
    if (m_mode == Mode::Server && authoritativeMovement && m_pawnOwner.count(netId)) return 1;
    if (m_mode == Mode::Client && m_hostAuthoritative && !m_pawnId.empty() && netId == m_pawnId) return 0;
    return -1;
}

bool NetworkManager::InputDriven(const std::string& netId) const {
    return m_mode == Mode::Server && authoritativeMovement && m_pawnOwner.count(netId) != 0;
}

// Each fixed tick on the server: every player moves by one input (the next
// one it sent, or its last one again if that's late), through its controller.
void NetworkManager::ServerMovementTick(float step) {
    for (auto& [ep, c] : m_clients) {
        if (c.pawn.empty()) continue;
        auto obj = m_syncObjs.find(c.pawn);
        if (obj == m_syncObjs.end() || !obj->second.t || !obj->second.t->gameObject) continue;
        // A late input is covered by the last one for a few ticks (a lost
        // packet); after that the player stands still rather than keep going.
        PlayerInput in = c.repeats < kInputRepeats ? c.lastInput : PlayerInput{};
        in.pressed = 0;   // a held button stays held; a press isn't repeated
        ++c.repeats;
        const bool fresh = !c.inputs.empty();
        if (fresh) {
            auto next = c.inputs.begin();
            in = next->second.second;
            c.lastInputSeq = next->first;
            c.inputs.erase(next);
            c.repeats = 0;
        }
        if (minPressInterval > 0.0f) {   // presses closer together than that are dropped
            const std::uint32_t limited[3] = {PlayerInput::Jump, PlayerInput::Dash, PlayerInput::Fire};
            for (int b = 0; b < 3; ++b) {
                if (!(in.pressed & limited[b])) continue;
                if (m_serverTime - c.lastPress[b] < minPressInterval) in.pressed &= ~limited[b];
                else c.lastPress[b] = m_serverTime;
            }
        }
        c.lastInput = in;
        if (IPlayerSim* sim = FindPlayerSim(obj->second.t->gameObject)) sim->Simulate(in, step);
        const Vec3 at = obj->second.t->TransformPoint(Vec3(0.0f, 0.0f, 0.0f));
        c.state.x = at.x; c.state.y = at.y; c.state.z = at.z;   // the server's word (snapshots, hit checks)
        c.hasState = true;
        // Tell the player where this input left it, for its prediction (only
        // for a new input: a repeat has no number of its own).
        if (fresh) {
            const Vec3 lp = obj->second.t->localPosition;
            net::Packet a(PawnAck);
            a.Write(c.pawn); a.Write(c.lastInputSeq);
            a.Write(lp.x); a.Write(lp.y); a.Write(lp.z);
            SendDatagram(c.endpoint, a.Data(), a.Size());
        }
    }
}

// Client: the host says input `seq` left the pawn at `server`. Compare with
// where the prediction had it after that input; the difference moves the
// prediction (and every later one) over: tiny ones are ignored, small ones
// eased in over a few frames, large ones (a teleport, a wall the client went
// through) snapped. Same as rewinding to the host's state and replaying the
// newer inputs, without running the controller's side effects twice.
void NetworkManager::Reconcile(std::uint32_t seq, const Vec3& server) {
    if (seq <= m_lastPawnAck) return;   // old or repeated
    if (seq == m_predSeq && m_predicting) {
        // On a short round trip the ack for the input we're still running
        // arrives before we've finished it: hold it until we have.
        m_earlyAckSeq = seq; m_earlyAck = server;
        return;
    }
    m_lastPawnAck = seq;
    auto it = m_predicted.begin();
    while (it != m_predicted.end() && it->first < seq) ++it;
    if (it == m_predicted.end() || it->first != seq) { m_predicted.erase(m_predicted.begin(), it); return; }
    const Vec3 d = server - it->second;
    m_predicted.erase(m_predicted.begin(), it + 1);
    const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    if (!std::isfinite(len) || len < kReconcileEpsilon) return;
    ++m_predCorrections;
    for (auto& [s2, p] : m_predicted) p = p + d;
    auto sync = m_syncObjs.find(m_pawnId);
    if (len > reconcileSnap && sync != m_syncObjs.end() && sync->second.t) {
        sync->second.t->localPosition = sync->second.t->localPosition + m_predPending + d;
        m_predPending = Vec3(0.0f, 0.0f, 0.0f);
        ++m_predSnaps;
        if (IPlayerSim* sim = FindPlayerSim(sync->second.t->gameObject)) sim->OnTeleported();
    } else {
        m_predPending = m_predPending + d;
    }
}

// Each fixed tick on a client: this tick's input, numbered, with the last 3
// again (a lost packet's input arrives with the next).
void NetworkManager::ClientInputTick() {
    if (m_pawnId.empty() || !m_joined) return;
    PlayerInput in = m_heldInput;
    in.pressed = m_pressedAccum;
    m_pressedAccum = 0;
    m_sentInputs.push_back({++m_inputSeq, {m_netTick, in}});
    if (m_predicting) {
        // The frames since the last tick ran the previous input: where it left
        // the pawn, by the prediction (with the correction still being eased
        // in), is compared with the host's ack for it. From now on the
        // prediction runs this one.
        if (m_predSeq != 0)
            if (auto sync = m_syncObjs.find(m_pawnId); sync != m_syncObjs.end() && sync->second.t) {
                m_predicted.push_back({m_predSeq, sync->second.t->localPosition + m_predPending});
                if (m_predicted.size() > kMaxPredicted) m_predicted.erase(m_predicted.begin());
                if (m_earlyAckSeq == m_predSeq) { m_earlyAckSeq = 0; m_predSeq = 0; Reconcile(m_predicted.back().first, m_earlyAck); }
            }
        m_predCmd = in;
        m_predPressOnce = in.pressed;
        m_predSeq = m_inputSeq;
    }
    if (m_sentInputs.size() > 4) m_sentInputs.erase(m_sentInputs.begin());
    net::Packet p(InputCmd);
    p.Write(m_pawnId);
    p.Write(std::uint8_t(m_sentInputs.size()));
    for (const auto& [seq, ti] : m_sentInputs) {
        const PlayerInput& x = ti.second;
        p.Write(ti.first); p.Write(seq);
        p.Write(x.move.x); p.Write(x.move.y); p.Write(x.yaw); p.Write(x.pitch);
        p.Write(x.held); p.Write(x.pressed);
    }
    SendDatagram(m_serverEp, p.Data(), p.Size());
}

void NetworkManager::ServerTick(float dt) {
    if (!m_protectionsLogged) {   // on the first tick: the scene's AntiCheatConfig has applied by then
        m_protectionsLogged = true;
        std::string off;
        auto add = [&off](const char* what) { off += off.empty() ? what : std::string(", ") + what; };
        if (!anticheat.settings.enabled) add("anti-cheat");
        if (!encryption && !(anticheat.settings.enabled && anticheat.settings.requireEncryption)) add("encryption (players are known by address and a session cookie)");
        if (maxMoveSpeed <= 0.0f && !authoritativeMovement) add("move check");
        if (maxMessagesPerSecond <= 0) add("message rate limit");
        if (!off.empty()) OKAY_WARN("net: protections off: ", off);
    }
    m_serverTime += dt;
    m_acClockMs += static_cast<double>(dt) * 1000.0;
    ++m_tickCount;
    if (anticheat.settings.enabled) anticheat.Tick(dt);
    // Rate limit: true if this client may send another custom message this second.
    auto rateOk = [this](Client& c, std::size_t bytes) {
        if (maxMessagesPerSecond > 0 && ++c.msgCount > maxMessagesPerSecond) return false;
        return maxRelayedBytes <= 0 || (c.msgBytes += bytes) <= 4 * (std::size_t)maxRelayedBytes;
    };
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
        const bool sealed = len >= 1 && data[0] == kEncEnvelope;
        if (sealed) {
            net::SecureChannel* s = SessionFor(from);
            if (!s || !s->Open(data + 1, len - 1, decrypted)) continue;  // can't open (or a replay) -> drop
            data = decrypted.data(); len = decrypted.size();
        }
        net::Packet p(data, len);
        std::uint8_t type = p.ReadU8();
        // Anti-cheat P2: once encryption is required, a joined client's game
        // traffic must be sealed; plaintext from its address is someone else's.
        if (!sealed && anticheat.settings.enabled && anticheat.settings.requireEncryption &&
            type != Join && type != Query && type != PlayerQuery && m_clients.count(from))
            continue;
        // And in every mode: once a peer's session is keyed, it seals everything
        // (a plaintext Join or key hello is only a retry from before it knew).
        if (!sealed && type != Join && type != JoinHello && type != Query && type != PlayerQuery) {
            const net::SecureChannel* s = SessionFor(from);
            if (s && s->Ready()) continue;
        }
        if (type == JoinHello) {
            // A sealed join starts: trade keys, then the client sends its Join
            // encrypted. The hello is padded to at least the answer's size.
            if (len < kJoinHelloSize || m_clients.count(from)) continue;
            if (JoinBlocked(from)) continue;   // too many attempts from this address: its key exchange waits
            const std::uint8_t version = p.ReadU8();
            const std::string clientPk = p.ReadString();
            if (!p.Ok() || version < kJoinVersion) continue;
            if (anticheat.settings.enabled && anticheat.settings.requireEncryption) encryption = true;   // the rules ask for it
            net::Packet k(JoinKey);
            k.Write(kJoinVersion);
            if (!encryption || !net::SecureChannel::Available() ||
                clientPk.size() != net::SecureChannel::kPublicKeyBytes) {
                // No encryption here: the client may join in the clear.
                k.Write(std::string()); k.Write(std::string());
                SendDatagram(from, k.Data(), k.Size(), /*allowEncrypt=*/false);
                continue;
            }
            auto pj = m_pendingJoins.find(from);
            if (pj == m_pendingJoins.end()) {
                if (m_pendingJoins.size() >= kMaxPendingJoins) {
                    // Full: the oldest exchange goes (it has had the longest).
                    auto oldest = std::max_element(m_pendingJoins.begin(), m_pendingJoins.end(),
                        [](const auto& a, const auto& b) { return a.second.age < b.second.age; });
                    m_pendingJoins.erase(oldest);
                }
                pj = m_pendingJoins.emplace(from, PendingJoin{}).first;
            }
            PendingJoin& pending = pj->second;
            const std::vector<std::uint8_t> pkBytes(clientPk.begin(), clientPk.end());
            if (pending.clientPk != pkBytes || !pending.secure.Ready()) {
                CountJoinAttempt(from, 1);   // new keys (and a signature): an attempt
                pending.secure = net::SecureChannel{};
                pending.secure.GenerateKeys();
                if (!pending.secure.Establish(pkBytes, /*asServer=*/true)) { m_pendingJoins.erase(pj); continue; }
                pending.secure.SetSequenced(true);
                pending.secure.SetAssociatedData({kEncEnvelope, kJoinVersion});
                pending.clientPk = pkBytes;
            }
            pending.age = 0.0f;
            const auto spk = pending.secure.PublicKey();
            const std::string serverPk(spk.begin(), spk.end());
            k.Write(serverPk);
            std::string sig;
            if (m_hostSeed.size() == 32) {
                okayac::ed25519::Seed seed;
                std::copy(m_hostSeed.begin(), m_hostSeed.end(), seed.begin());
                const auto sg = okayac::ed25519::Sign(JoinKeyMessage(serverPk, clientPk), seed);
                okayac::Wipe(seed.data(), seed.size());
                sig.assign(sg.begin(), sg.end());
            }
            k.Write(sig);
            SendDatagram(from, k.Data(), k.Size(), /*allowEncrypt=*/false);
            continue;
        }
        if (type == Query) {
            // A server browser asking for our details (no join, no state). The
            // request is padded to at least the size of a typical answer so the
            // server can't be used to amplify traffic at a spoofed address.
            if (len < net::kQueryMinSize || p.ReadString() != net::kQueryMagic) continue;
            const std::uint32_t nonce = p.ReadU32();
            net::Packet q(QueryInfo);
            q.Write(std::string(net::kQueryMagic)); q.Write(nonce);
            q.Write(std::uint8_t(net::kQueryProtocol));
            q.Write(serverName); q.Write(gameId); q.Write(mapName);
            q.Write(std::uint32_t(PlayerCount()));
            q.Write(std::uint32_t(maxPlayers + (dedicated ? 0 : 1)));
            std::uint8_t flags = 0;
            if (!password.empty()) flags |= net::kQueryPassword;
            if (dedicated) flags |= net::kQueryDedicated;
            if (m_joinCheck || m_verifyToken || (anticheat.settings.enabled && !anticheat.settings.ticketKey.empty()))
                flags |= net::kQueryAuth;   // it checks sign-ins (or session tickets): clients send their token
            if (encryption && net::SecureChannel::Available()) flags |= net::kQueryEncrypted;
            q.Write(flags);
            SendDatagram(from, q.Data(), q.Size(), /*allowEncrypt=*/false);
            continue;
        }
        if (type == PlayerQuery) {
            // Who's playing (server browser). Answered only to a sender that echoes
            // the challenge we gave its address, so the (larger) list can't be
            // aimed at a spoofed victim.
            if (len < net::kQueryMinSize || p.ReadString() != net::kQueryMagic) continue;
            const std::uint32_t got = p.ReadU32(), want = QueryChallenge(from);
            if (got != want) {
                net::Packet c(PlayerChallenge); c.Write(std::string(net::kQueryMagic)); c.Write(want);
                SendDatagram(from, c.Data(), c.Size(), /*allowEncrypt=*/false);
                continue;
            }
            const auto roster = Roster();
            const std::uint32_t n = (std::uint32_t)std::min<std::size_t>(roster.size(), 64);
            net::Packet l(PlayerList); l.Write(std::string(net::kQueryMagic)); l.Write(n);
            for (std::uint32_t i = 0; i < n; ++i) {
                l.Write(roster[i].name.substr(0, 32)); l.Write(roster[i].userId);
                l.Write(std::int32_t(roster[i].score)); l.Write(roster[i].seconds);
            }
            SendDatagram(from, l.Data(), l.Size(), /*allowEncrypt=*/false);
            continue;
        }
        if (type == Join) {
            std::string name = p.ReadString();           // display name (may be empty)
            std::string room = p.ReadString();           // lobby room (may be empty)
            std::string pass = p.ReadString();           // password (may be empty)
            std::string token = p.ReadString();           // identity token (may be empty)
            std::string clientPk = p.ReadString();        // encryption public key (may be empty)
            bool existed = m_clients.count(from) != 0;
            // A sealed join arrives encrypted under its key exchange's session.
            auto pendingIt = (!existed && sealed) ? m_pendingJoins.find(from) : m_pendingJoins.end();
            const bool viaPending = pendingIt != m_pendingJoins.end();
            std::string userId;
            const bool acOn = anticheat.settings.enabled;
            const bool needCrypto = acOn && anticheat.settings.requireEncryption;
            if (needCrypto && net::SecureChannel::Available()) encryption = true;   // the rules ask for it
            if (!existed && JoinBlocked(from)) {
                // Too many attempts from this address: say so once, then ignore.
                JoinLimit& l = m_joinLimits[from.address];
                if (!l.told) {
                    l.told = true;
                    net::Packet r(Reject);
                    r.Write("too many join attempts; try again in " + std::to_string((int)std::ceil(l.blockedUntil - m_serverTime)) + " s");
                    SendDatagram(from, r.Data(), r.Size(), /*allowEncrypt=*/viaPending);
                }
                continue;
            }
            if (!existed) {
                // Gate new joins on capacity, then encryption (nothing secret is
                // checked in the clear), then the password, then identity;
                // refuse politely.
                const char* why = nullptr;
                std::string ticketWhy, banWhy;
                const std::string addrIdent = "a:" + std::to_string(from.address);
                auto kickedRecently = [&](const std::string& ident) {
                    const float left = KickBanLeft(ident);
                    if (left <= 0.0f) return false;
                    banWhy = "kicked by the anti-cheat; try again in " + std::to_string((int)std::ceil(left)) + " s";
                    return true;
                };
                if (!anticheat.configError.empty()) why = "server misconfigured (its anti-cheat rules didn't load)";
                else if ((int)m_clients.size() >= maxPlayers) why = "server is full";
                else if (needCrypto && !viaPending)
                    // The password and ticket must not travel in the clear.
                    why = net::SecureChannel::Available() ? kNeedsEncryption
                                                          : "encryption required, but this server was built without libsodium";
                else if (!password.empty() && pass != password) why = "wrong password";
                else if (acOn && !anticheat.settings.ticketKey.empty()) {
                    // A signed session ticket names the account; it takes the
                    // place of the token verifiers.
                    std::vector<std::uint8_t> key;
                    net::SessionTicket::Base64UrlDecode(anticheat.settings.ticketKey, key);
                    net::SessionTicketClaims claims;
                    std::string err;
                    if (net::SessionTicket::Verify(token, key, anticheat.settings.ticketAudience,
                                                   (std::int64_t)std::time(nullptr), claims, &err))
                        userId = claims.userId;
                    else { ticketWhy = "session ticket refused: " + err; why = ticketWhy.c_str(); }
                }
                else if (m_joinCheck) {
                    const int r = m_joinCheck(token, userId);
                    if (r == -2) why = "server busy, try again";
                    else if (r < 0) continue;                  // still checking: the client retries
                    else if (r == 0) why = "authentication failed";
                }
                else if (m_verifyToken && !m_verifyToken(token, userId)) why = "authentication failed";
                // Kicked by the anti-cheat a moment ago: the account, or for an
                // anonymous join the address (other signed-in players behind
                // the same address aren't locked out).
                if (!why && kickedRecently(userId.empty() ? addrIdent : "u:" + userId)) why = banWhy.c_str();
                if (why) {
                    // A refusal is an attempt; a wrong password counts double
                    // (guessing it is what the limit is for).
                    CountJoinAttempt(from, std::string(why) == "wrong password" ? 2 : 1);
                    net::Packet r(Reject); r.Write(std::string(why));
                    SendDatagram(from, r.Data(), r.Size(), /*allowEncrypt=*/viaPending);
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
                c.joinedAt = m_serverTime;
                c.userId = userId;
                // A join that got in costs this address nothing (its key
                // exchange is given back): players behind one address still
                // all get in, a flood that never finishes doesn't.
                if (auto jl = m_joinLimits.find(from.address); jl != m_joinLimits.end() && jl->second.count > 0) --jl->second.count;
                c.ident = userId.empty() ? "a:" + std::to_string(from.address) : "u:" + userId;
                anticheat.RestorePeer(c.id, c.ident);   // what it had if it left a moment ago
                if (viaPending) {
                    // The key exchange's session becomes the client's (numbered,
                    // with the version authenticated).
                    c.secure = pendingIt->second.secure;
                    c.sealedJoin = true;
                    m_pendingJoins.erase(pendingIt);
                }
                // Establish this client's encrypted session from its public key.
                else if (encryption && net::SecureChannel::Available() && !clientPk.empty()) {
                    c.secure.GenerateKeys();
                    c.secure.Establish(std::vector<std::uint8_t>(clientPk.begin(), clientPk.end()),
                                       /*asServer=*/true);
                    // Replay protection rides on required encryption (the Welcome
                    // tells the client to number its packets too).
                    if (needCrypto) c.secure.SetSequenced(true);
                }
            }
            if (isNew && !c.secure.Ready()) {
                std::random_device rd;
                c.cookie = ((static_cast<std::uint64_t>(rd()) << 32) ^ rd()) | 1u;
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
            // A trailing flags byte only when there's something to say (bit 0 =
            // number sealed packets); a server without anti-cheat P2 sends the
            // same Welcome as before, and older clients stop reading earlier.
            // bit 1 = a fixed net tick, its rate (f32) follows.
            std::uint8_t welcomeFlags = c.secure.Sequenced() ? 1 : 0;
            if (HostTickRate() > 0.0f) welcomeFlags |= 2;
            if (authoritativeMovement) welcomeFlags |= 4 | 8;   // bit 2: send inputs, not positions; bit 3: PawnAcks (predict)
            if (c.cookie) welcomeFlags |= 16;                    // bit 4: a session cookie (u64) follows
            if (welcomeFlags) w.Write(welcomeFlags);
            if (welcomeFlags & 2) w.Write(HostTickRate());
            if (welcomeFlags & 16) WriteU64(w, c.cookie);
            // A sealed joiner already has the keys; a plaintext joiner gets them here.
            SendDatagram(from, w.Data(), w.Size(), /*allowEncrypt=*/c.sealedJoin);
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
            std::string senderRoom;
            {   // only joined clients may change shared state (and, with anti-cheat on, within its rules)
                auto cl = m_clients.find(from);
                if (cl == m_clients.end() || !CookieOk(cl->second, p) || !rateOk(cl->second, len)) continue;
                senderRoom = cl->second.room;
                // Sized like a variable, and not a key factory: a client makes
                // at most kMaxClientVars keys of its own.
                const bool isNew = !m_syncVars.count(key);
                if (key.empty() || key.size() > kMaxVarKey || value.size() > kMaxVarValue ||
                    (isNew && cl->second.varsCreated >= kMaxClientVars)) continue;
                if (anticheat.settings.enabled && !AntiCheatAllowsSyncVar(cl->second.id, key)) {
                    // Put the sender's copy back to the server's value (it set it locally first).
                    if (m_clients.count(from)) {
                        auto cur = m_syncVars.find(key);
                        net::Packet back(SyncVar); back.Write(key); back.Write(cur != m_syncVars.end() ? cur->second : std::string());
                        SendDatagram(from, back.Data(), back.Size());
                    }
                    continue;
                }
            }
            if (auto cl = m_clients.find(from); cl != m_clients.end() && !m_syncVars.count(key)) ++cl->second.varsCreated;
            ApplySyncVar(key, value);
            // A client's write goes to its own room (the host applies every one).
            net::Packet out(SyncVar); out.Write(key); out.Write(value);
            for (auto& [ep, c] : m_clients)
                if (c.room == senderRoom) SendDatagram(c.endpoint, out.Data(), out.Size());
        } else if (type == ClockPong) {
            // A client's clock, for the heartbeat (anti-cheat maxClockDrift).
            auto it = m_clients.find(from);
            if (it == m_clients.end()) continue;
            const std::uint64_t nonce = ReadU64(p);
            const std::uint32_t seq = p.ReadU32();
            const std::uint64_t clientMs = ReadU64(p);
            const std::string tag = p.ReadString();
            if (!p.Ok()) continue;
            Client& c = it->second;
            if (nonce == c.pingNonce && c.pingNonce != 0) {   // its round trip, for hit claims
                const float rtt = static_cast<float>((m_acClockMs - c.pingSentMs) / 1000.0);
                c.rtt = c.rtt < 0.0f ? rtt : c.rtt * 0.75f + rtt * 0.25f;
                c.pingNonce = 0;
            }
            if (!c.clock) continue;
            const auto v = c.clock->Accept(nonce, seq, clientMs, tag, static_cast<std::uint64_t>(m_acClockMs));
            if (v == net::ClockCheck::Verdict::BadResponse) continue;
            c.clockQuiet = 0.0f;
            if (v == net::ClockCheck::Verdict::Drift && c.clockReported >= kClockReportEvery) {
                c.clockReported = 0.0f;
                const double d = c.clock->LastDrift() * 100.0;
                char detail[48];
                std::snprintf(detail, sizeof detail, "%+.0f%%", d);
                const std::uint32_t id = c.id;
                anticheat.Report(id, CheatKind::ClockDrift, detail);
                AntiCheatAfterRefusal(id);   // (may kick: c is gone after this)
            }
        } else if (type == InputCmd) {
            // A player's inputs (authoritative movement): queued by number, one
            // applied per tick (ServerMovementTick).
            auto it = m_clients.find(from);
            if (it == m_clients.end() || !authoritativeMovement) continue;
            Client& c = it->second;
            const std::string pawn = p.ReadString();
            const std::uint8_t n = p.ReadU8();
            if (!p.Ok() || n == 0 || n > 8) continue;
            if (c.pawn.empty()) {
                // Its player: one of its own SpawnOwned objects ("sp/<its id>/...").
                const std::string prefix = "sp/" + std::to_string(c.id) + "/";
                if (pawn.compare(0, prefix.size(), prefix) != 0 || !m_syncObjs.count(pawn) || m_pawnOwner.count(pawn)) continue;
                c.pawn = pawn;
                m_pawnOwner[pawn] = c.id;
                c.lastSeen = 0.0f;
            } else if (pawn != c.pawn) {
                continue;
            }
            const float rate = HostTickRate();
            for (std::uint8_t i = 0; i < n; ++i) {
                const std::uint32_t tick = p.ReadU32(), seq = p.ReadU32();
                PlayerInput in;
                in.move.x = p.ReadF32(); in.move.y = p.ReadF32();
                in.yaw = p.ReadF32(); in.pitch = p.ReadF32();
                in.held = p.ReadU32(); in.pressed = p.ReadU32();
                if (!p.Ok()) break;
                if (seq <= c.lastInputSeq || c.inputs.count(seq)) continue;   // applied, or a repeat
                // Commands for ticks too far ahead of or behind the server's are refused.
                if (std::fabs((double)tick - (double)m_netTick) > rate) { ++m_inputsDropped; continue; }
                in.Clamp();
                c.inputs[seq] = {tick, in};
            }
            // The tick budget: one input a tick; a client sending faster (a speed
            // hack) only fills the queue, and the oldest beyond it are dropped.
            while (c.inputs.size() > kMaxQueuedInputs) {
                c.lastInputSeq = std::max(c.lastInputSeq, c.inputs.begin()->first);
                c.inputs.erase(c.inputs.begin());
                ++m_inputsDropped;
            }
        } else if (type == Ping) {
            // Echo the client's timestamp straight back so it can measure RTT
            // (a joined client's, or one mid-join: not a stranger's).
            if (!m_clients.count(from) && !m_pendingJoins.count(from)) continue;
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
                // Over the rate: not acked either, so the client sends it again later.
                if (!it->second.relSeen.Seen(seq) && !rateOk(it->second, len)) continue;
                net::Packet ack(ReliableAck); ack.Write(seq);
                SendDatagram(from, ack.Data(), ack.Size());
                if (!it->second.relSeen.Accept(seq)) continue;   // a repeat (the rate was charged before the ack)
                if (channel == "__hit") { HandleHitClaim(it->second, data); continue; }   // the server's to judge
                if (ClientMessageOk(it->second, channel, data) && AntiCheatAllows(it->second.id, channel, data))
                    Deliver(it->second.id, channel, data);
            }
        } else if (type == ReliableAck) {
            auto it = m_clients.find(from);
            if (it != m_clients.end()) it->second.relOut.erase(p.ReadU32());
        } else if (type == DirectMessage) {
            auto it = m_clients.find(from);
            if (it != m_clients.end() && rateOk(it->second, len)) {
                std::uint32_t sender = it->second.id;
                p.ReadU32();                              // sender-reported id (ignored)
                std::uint32_t target = p.ReadU32();
                std::string channel = p.ReadString();
                std::string data    = p.ReadString();
                if (!ClientMessageOk(it->second, channel, data)) continue;
                if (!AntiCheatAllows(sender, channel, data)) continue;   // refused (and maybe kicked)
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
                Client& c = it->second;
                if (p.Remaining() == 4 || p.Remaining() == 12) p.ReadU32();   // its net tick
                if (!CookieOk(c, p)) continue;
                if (!c.pawn.empty()) {   // authoritative movement: where it is is the server's call
                    c.state.glyph = s.glyph;
                    c.lastSeen = 0.0f;
                    continue;
                }
                c.lastSeen = 0.0f;
                if (!std::isfinite(s.x) || !std::isfinite(s.y) || !std::isfinite(s.z)) continue;   // garbage
                if (maxMoveSpeed > 0.0f && c.hasState) {   // too fast? keep the last good spot
                    const float dx = s.x - c.state.x, dy = s.y - c.state.y, dz = s.z - c.state.z;
                    // At most a quarter second of travel, however long it was quiet:
                    // going silent mustn't bank a long jump (a bigger gap is
                    // corrected back to the last good spot).
                    const float allowed = maxMoveSpeed * std::min(std::max(c.stateAge, m_snapshotInterval), 0.25f) * 1.25f + 0.25f;
                    if (dx * dx + dy * dy + dz * dz > allowed * allowed) {
                        ++c.violations;
                        net::Packet cp(Correct); cp.Write(c.state.x); cp.Write(c.state.y); cp.Write(c.state.z);
                        SendDatagram(c.endpoint, cp.Data(), cp.Size());
                        if (kickAfterViolations > 0 && c.violations >= kickAfterViolations)
                            Kick(c.id, "movement check failed");   // (erases c; nothing below touches it)
                        continue;
                    }
                }
                c.state = s;
                c.hasState = true;   // we now know where this client is (for interest culling)
                c.stateAge = 0.0f;
                ApplyPeer(s); // host sees the client move
            }
        } else if (type == Leave) {
            auto it = m_clients.find(from);
            p.ReadU32();   // sender-reported id (ignored)
            if (it != m_clients.end() && CookieOk(it->second, p)) {
                std::uint32_t id = it->second.id;
                if (auto r = m_remotes.find(id); r != m_remotes.end()) {
                    if (GetScene() && r->second) GetScene()->Destroy(r->second);
                    m_remotes.erase(r);
                }
                anticheat.ParkPeer(id, it->second.ident);
                m_clients.erase(it);
                anticheat.ForgetPeer(id);
                DespawnPeerObjects(id);
                m_recentCheats.erase(id);
                for (auto po = m_pawnOwner.begin(); po != m_pawnOwner.end();) po = po->second == id ? m_pawnOwner.erase(po) : std::next(po);
                if (m_peerLeft) m_peerLeft(id);
            }
        } else if (type == Message) {
            auto it = m_clients.find(from);
            if (it != m_clients.end() && rateOk(it->second, len)) {
                std::uint32_t sender = it->second.id;   // endpoint is the source of truth
                const std::string senderRoom = it->second.room;
                p.ReadU32();                            // sender-reported id (ignored)
                std::string channel = p.ReadString();
                std::string data    = p.ReadString();
                if (!CookieOk(it->second, p)) continue;
                if (channel == "__hit") { HandleHitClaim(it->second, data); continue; }   // never relayed
                if (!ClientMessageOk(it->second, channel, data)) continue;
                if (!AntiCheatAllows(sender, channel, data)) continue;   // not applied, not relayed
                if (dedicated || m_localRoom == senderRoom)   // host only sees its room (a dedicated server sees all)
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
    if (m_relayLost) { Stop(); return; }   // the relay refused this host (RelayError())
    for (auto it = m_pendingJoins.begin(); it != m_pendingJoins.end();)   // key exchanges never finished
        it = (it->second.age += dt) > kPendingJoinTtl ? m_pendingJoins.erase(it) : std::next(it);

    // Time out silent clients (and age the move-check / rate-limit windows).
    for (auto it = m_clients.begin(); it != m_clients.end();) {
        it->second.lastSeen += dt;
        it->second.stateAge += dt;
        if ((it->second.msgTimer += dt) >= 1.0f) { it->second.msgTimer = 0.0f; it->second.msgCount = 0; it->second.msgBytes = 0; it->second.hitClaims = 0; }
        if ((it->second.violationTimer += dt) >= 60.0f) { it->second.violationTimer = 0.0f; it->second.violations = 0; }
        if (it->second.lastSeen > kClientTimeout) {
            std::uint32_t id = it->second.id;
            if (auto r = m_remotes.find(id); r != m_remotes.end()) {
                if (GetScene() && r->second) GetScene()->Destroy(r->second);
                m_remotes.erase(r);
            }
            anticheat.ParkPeer(id, it->second.ident);
            it = m_clients.erase(it);
            anticheat.ForgetPeer(id);
            DespawnPeerObjects(id);
            m_recentCheats.erase(id);
            for (auto po = m_pawnOwner.begin(); po != m_pawnOwner.end();) po = po->second == id ? m_pawnOwner.erase(po) : std::next(po);
            if (m_peerLeft) m_peerLeft(id);
        } else {
            ++it;
        }
    }

    // Anti-cheat clock heartbeat: ask each client for its game clock about once
    // a second (the answer is checked above). One that won't answer is reported
    // too, or skipping the answer would be the way around it.
    // With the anti-cheat on, or a fixed tick (lag compensation clamps its
    // rewind to the shooter's round trip), the same pings also measure each
    // client's round trip, whether or not the clock is checked.
    if (anticheat.settings.enabled || HostTickRate() > 0.0f) {
        const bool checkClock = anticheat.settings.enabled && anticheat.settings.maxClockDrift > 0.0f;
        std::vector<std::uint32_t> quiet;
        for (auto& [ep, c] : m_clients) {
            if (checkClock && !c.clock) c.clock = std::make_shared<net::ClockCheck>(anticheat.settings.maxClockDrift);
            c.clockReported += dt;
            if ((c.clockTimer -= dt) <= 0.0f) {
                c.clockTimer = 1.0f;
                net::Packet cp(ClockPing);
                if (c.clock) {
                    const auto ch = c.clock->Issue(static_cast<std::uint64_t>(m_acClockMs));
                    WriteU64(cp, ch.nonce); cp.Write(ch.seq); cp.Write(c.clock->Key());
                    c.pingNonce = ch.nonce;
                } else {
                    std::random_device rd;
                    c.pingNonce = ((static_cast<std::uint64_t>(rd()) << 32) ^ rd()) | 1u;
                    WriteU64(cp, c.pingNonce); cp.Write(std::uint32_t(0)); cp.Write(std::string("rtt"));
                }
                c.pingSentMs = m_acClockMs;
                SendDatagram(c.endpoint, cp.Data(), cp.Size());
            }
            if (!checkClock) continue;
            if ((c.clockQuiet += dt) > kClockQuietLimit) {
                c.clockQuiet = 0.0f;
                quiet.push_back(c.id);
            }
        }
        // Reported after the loop: the cheat handler (game code) may kick.
        for (std::uint32_t id : quiet) {
            anticheat.Report(id, CheatKind::ClockDrift, "no clock answer");
            AntiCheatAfterRefusal(id);
        }
    }

    // Once a second, tell each client who's in its room (names, scores, time).
    m_rosterTimer += dt;
    if (m_rosterTimer >= 1.0f && !m_clients.empty()) {
        m_rosterTimer = 0.0f;
        std::unordered_map<std::string, std::vector<RosterEntry>> byRoom;
        if (!dedicated) byRoom[m_localRoom].push_back({0, m_localName, std::string(), m_hostScore, m_serverTime});
        for (auto& [ep, c] : m_clients) byRoom[c.room].push_back({c.id, c.name, c.userId, c.score, m_serverTime - c.joinedAt});
        for (auto& [ep, c] : m_clients) {
            const auto& list = byRoom[c.room];
            net::Packet r(RosterMsg); r.Write(std::uint32_t(list.size()));
            for (const auto& e : list) { r.Write(e.id); r.Write(e.name); r.Write(e.userId); r.Write(std::int32_t(e.score)); r.Write(e.seconds); }
            SendDatagram(c.endpoint, r.Data(), r.Size());
        }
    }

    // Send each client a snapshot of just its own room: the host (id 0) if the
    // host shares that room, plus every client in the same room. This is what
    // makes lobbies work — separate rooms never see each other.
    // With a fixed net tick, a snapshot goes out once a tick and carries it
    // (even when nothing moved, so clients keep the tick); otherwise at snapshotRate.
    const bool fixedTick = HostTickRate() > 0.0f;
    const bool tickNow = fixedTick && StepNetTick(HostTickRate(), dt) > 0;
    m_snapshotTimer += dt;
    if ((fixedTick ? tickNow : m_snapshotTimer >= m_snapshotInterval) && !m_clients.empty()) {
        m_snapshotTimer = 0.0f;
        Vec3 lp = m_localAvatar ? m_localAvatar->localPosition : Vec3::Zero;
        for (auto& [ep, recipient] : m_clients) {
            // A lambda below needs the recipient; capturing a structured binding directly
            // is ill-formed before C++20 (Clang/Emscripten rejects it), so alias it to a
            // plain reference the closure can capture portably.
            auto& rc = recipient;
            const std::string& room = recipient.room;
            bool hostHere = !dedicated && (m_localRoom == room);   // a dedicated server has no avatar
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
            if (include.empty() && !fixedTick) continue;            // nothing changed -> send nothing

            net::Packet snap(Snapshot);
            snap.Write(std::uint32_t(include.size()));
            for (const PeerState& s : include) {
                snap.Write(s.id);
                snap.Write(s.x); snap.Write(s.y); snap.Write(s.z);
                snap.Write(std::uint8_t(s.glyph));
            }
            if (fixedTick) snap.Write(m_netTick);   // trailing: older clients stop reading before it
            if (fixedTick && authoritativeMovement) snap.Write(recipient.lastInputSeq);   // its newest input applied
            SendDatagram(recipient.endpoint, snap.Data(), snap.Size());
        }
    }

    // Resend any unacked reliable messages to each client.
    for (auto& [ep, c] : m_clients) ResendReliable(c.relOut, c.endpoint, dt);
}

void NetworkManager::ClientTick(float dt) {
    m_clock += dt;
    ++m_tickCount;   // the frame count a hit claim carries
    m_gameClockMs += static_cast<double>(dt) * 1000.0;
    RelayKeepAlive(dt);
    // Through a relay we don't know the host's (synthetic) endpoint until the relay
    // pairs us, so hold the join handshake until then (the recv loop below still
    // runs, so the relay's welcome is processed).
    bool awaitingRelay = m_relay && !m_relayWelcomed;
    if (!m_joined && !awaitingRelay) {
        m_joinTimer -= dt;
        const bool canSeal = encryption && net::SecureChannel::Available();
        const bool pinned = !pinnedHostKey.empty();
        // With a token to give, first ask the host whether it wants one (a
        // server-browser query: plaintext, padded, answered by every version).
        if (!m_authDecided && (m_authToken.empty() || authTokenToAnyHost)) m_authDecided = true;
        if (m_joinTimer <= 0.0f && !m_authDecided) {
            if (m_authQueries >= 4) {
                m_authDecided = true;   // no answer: an older host; it gets no token
                OKAY_WARN("net: ", m_serverEp.ToString(), " didn't say whether it checks sign-ins; joining without the account token");
            } else {
                if (m_authNonce == 0) { std::random_device rd; m_authNonce = rd() | 1u; }
                net::Packet q(Query);
                q.Write(std::string(net::kQueryMagic));
                q.Write(m_authNonce);
                std::vector<std::uint8_t> d(q.Data(), q.Data() + q.Size());
                if (d.size() < net::kQueryMinSize) d.resize(net::kQueryMinSize, 0);
                SendDatagram(m_serverEp, d.data(), d.size(), /*allowEncrypt=*/false);
                ++m_authQueries;
                m_joinTimer = 0.25f;
            }
        }
        if (!m_authDecided) {
            // still asking
        } else if (m_joinTimer <= 0.0f && pinned && !canSeal) {
            // A pin can only be checked over the sealed join.
            if (!m_pinWarned) OKAY_ERROR("net: a pinned host key needs encryption on (and libsodium); not joining");
            m_pinWarned = true;
            m_joinTimer = 1.0f;
        } else if (m_joinTimer <= 0.0f && canSeal && !m_keyed && !m_legacyJoin &&
                   (pinned || m_joinHellos < kSealedJoinTries)) {
            // Sealed join, step 1: trade keys (padded, so the answer is no bigger).
            net::Packet h(JoinHello);
            h.Write(kJoinVersion);
            const auto pk = m_secure.PublicKey();
            h.Write(std::string(pk.begin(), pk.end()));
            std::vector<std::uint8_t> d(h.Data(), h.Data() + h.Size());
            if (d.size() < kJoinHelloSize) d.resize(kJoinHelloSize, 0);
            SendDatagram(m_serverEp, d.data(), d.size(), /*allowEncrypt=*/false);
            ++m_joinHellos;
            m_joinTimer = 0.5f;
        } else if (m_joinTimer <= 0.0f) {
            if (canSeal && !m_keyed && !m_legacyJoin) {
                m_legacyJoin = true;   // an older server: it never answered the key hello
                OKAY_WARN("net: ", m_serverEp.ToString(), " doesn't do the sealed join; joining in the clear");
            }
            net::Packet p(Join);
            p.Write(m_localName);
            p.Write(m_localRoom);
            p.Write(password);          // matched against the server's password
            // The identity token the server may verify: only to a host that asked for it.
            p.Write((authTokenToAnyHost || m_hostWantsAuth) ? m_authToken : std::string());
            {   // our encryption public key ("" if encryption is off / unavailable, or
                // already traded in the sealed join)
                auto pk = (encryption && !m_keyed ? m_secure.PublicKey() : std::vector<std::uint8_t>{});
                p.Write(std::string(pk.begin(), pk.end()));
            }
            if (m_keyed) p.Write(kJoinVersion);
            // Step 2 of the sealed join goes encrypted; the plaintext Join is pre-handshake.
            SendDatagram(m_serverEp, p.Data(), p.Size(), /*allowEncrypt=*/m_keyed);
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
        if (!(from == m_serverEp)) continue;   // only our server talks to us
        const std::uint8_t* data = buf.data();
        std::size_t len = static_cast<std::size_t>(n);
        std::vector<std::uint8_t> assembled;
        if (len >= 1 && data[0] == Fragment) {
            if (!TakeFragment(from, data, len, assembled)) continue;
            data = assembled.data(); len = assembled.size();
        }
        std::vector<std::uint8_t> decrypted;
        const bool sealed = len >= 1 && data[0] == kEncEnvelope;
        if (sealed) {
            net::SecureChannel* s = SessionFor(from);
            if (!s || !s->Open(data + 1, len - 1, decrypted)) continue;
            data = decrypted.data(); len = decrypted.size();
        }
        net::Packet p(data, len);
        std::uint8_t type = p.ReadU8();
        // Once our session is keyed the server seals everything: plaintext now
        // is someone else's (spoofing the server's address).
        if (!sealed && m_secure.Ready()) continue;
        // In a sealed join (not the clear join an older or unencrypted host
        // gets), the server's Welcome, Reject and Kicked come sealed under the
        // JoinKey session; before that only its JoinKey and QueryInfo are in the
        // clear. A plaintext one is a forgery that would skip the pin or end the
        // join.
        if (!sealed && encryption && net::SecureChannel::Available() && !m_legacyJoin &&
            type != JoinKey && type != QueryInfo) continue;
        if (type == QueryInfo) {
            // The host's answer to "do you check sign-ins?" (before the join).
            if (m_authDecided || p.ReadString() != net::kQueryMagic || p.ReadU32() != m_authNonce) continue;
            p.ReadU8(); p.ReadString(); p.ReadString(); p.ReadString(); p.ReadU32(); p.ReadU32();
            const std::uint8_t flags = p.ReadU8();
            if (!p.Ok()) continue;
            m_hostWantsAuth = (flags & net::kQueryAuth) != 0;
            m_authDecided = true;
            m_joinTimer = 0.0f;
            continue;
        }
        if (type == JoinKey) {
            // Sealed join, step 1's answer: the server's key (signed, if it has
            // an identity). Step 2, the sealed Join, goes on the next tick.
            if (m_joined || m_keyed || m_legacyJoin || !encryption) continue;
            const std::uint8_t version = p.ReadU8();
            const std::string serverPk = p.ReadString(), sig = p.ReadString();
            if (!p.Ok() || version < kJoinVersion) continue;
            if (serverPk.empty()) {
                // The server has encryption off.
                if (!pinnedHostKey.empty()) {
                    if (!m_pinWarned) OKAY_ERROR("net: the server has encryption off, so its pinned key can't be checked; not joining");
                    m_pinWarned = true;
                    continue;
                }
                m_legacyJoin = true;
                m_joinTimer = 0.0f;
                continue;
            }
            const auto mine = m_secure.PublicKey();
            if (!pinnedHostKey.empty()) {
                std::vector<std::uint8_t> pin;
                okayac::ed25519::PublicKey pk{};
                okayac::ed25519::Signature sg{};
                const bool shaped = net::SessionTicket::Base64UrlDecode(pinnedHostKey, pin) && pin.size() == pk.size() &&
                                    sig.size() == sg.size();
                if (shaped) { std::copy(pin.begin(), pin.end(), pk.begin()); std::copy(sig.begin(), sig.end(), sg.begin()); }
                if (!shaped || !okayac::ed25519::Verify(JoinKeyMessage(serverPk, std::string(mine.begin(), mine.end())), sg, pk)) {
                    // Not the pinned host (or a forgery): ignore it, the real one may still answer.
                    if (!m_pinWarned) OKAY_WARN("net: ", m_serverEp.ToString(), " didn't prove the pinned host key; ignoring it");
                    m_pinWarned = true;
                    continue;
                }
            }
            if (!m_secure.Establish(std::vector<std::uint8_t>(serverPk.begin(), serverPk.end()), /*asServer=*/false))
                continue;
            m_secure.SetSequenced(true);
            m_secure.SetAssociatedData({kEncEnvelope, kJoinVersion});
            m_keyed = true;
            m_joinTimer = 0.0f;
            continue;
        }
        if (type == Welcome) {
            m_localId = p.ReadU32();
            m_serverName = p.ReadString();
            m_localUserId = p.ReadString();   // our verified identity ("" if anon)
            std::string serverPk = p.ReadString();   // server's encryption public key
            // Only the clear join keys its session from the Welcome (an older
            // host; never with a pin, which needs the sealed join).
            if (m_legacyJoin && pinnedHostKey.empty() && !serverPk.empty() && !m_secure.Ready())
                m_secure.Establish(std::vector<std::uint8_t>(serverPk.begin(), serverPk.end()),
                                   /*asServer=*/false);
            const std::uint8_t welcomeFlags = p.ReadU8();   // optional: absent from older servers
            if (p.Ok() && (welcomeFlags & 1) && m_secure.Ready()) m_secure.SetSequenced(true);
            if (p.Ok() && (welcomeFlags & 2)) {   // the host runs a fixed net tick
                const float rate = p.ReadF32();
                if (p.Ok() && std::isfinite(rate) && rate >= 1.0f && rate <= 240.0f) m_hostTickRate = rate;
            }
            if (p.Ok() && (welcomeFlags & 16)) {   // an unencrypted session's cookie, for what we send
                const std::uint64_t cookie = ReadU64(p);
                if (p.Ok()) m_sessionCookie = cookie;
            }
            if (p.Ok() && (welcomeFlags & 4) && m_hostTickRate > 0.0f) m_hostAuthoritative = true;
            m_hostAcksPawn = m_hostAuthoritative && (welcomeFlags & 8);   // an older 5b host doesn't: follow it instead
            m_joined = true;
            m_joinRejected = false;
            OKAY_INFO("net: joined '", m_serverName, "' as peer ", m_localId);
        } else if (type == Reject) {
            std::string why = p.ReadString();
            if (!m_joined && !encryption && net::SecureChannel::Available() && why.rfind(kNeedsEncryption, 0) == 0) {
                // The host only takes encrypted players: join again, sealed.
                encryption = true;
                m_secure.GenerateKeys();
                m_joinTimer = 0.0f;
                OKAY_INFO("net: ", m_serverEp.ToString(), " requires encryption; joining encrypted");
                continue;
            }
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
            if (m_relSeen.Accept(seq)) Deliver(0, channel, data);
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
            if (m_hostTickRate > 0.0f) {   // the host's tick (trailing)
                const std::uint32_t tick = p.ReadU32();
                // Snapshots can arrive out of order: only move forward.
                // Forward only (snapshots arrive out of order), unless we're more
                // than a second ahead of the host: then start again from its tick.
                if (p.Ok() && (tick > m_netTick || (double)m_netTick - (double)tick > m_hostTickRate)) {
                    m_netTick = tick; m_tickAccum = 0.0f;
                }
                if (m_hostAuthoritative) {   // the newest input the host applied for us
                    const std::uint32_t acked = p.ReadU32();
                    if (p.Ok() && acked > m_lastAckedSeq) m_lastAckedSeq = acked;
                }
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
        } else if (type == Correct) {
            // The server rejected a move (or teleported us): go where it says.
            const float x = p.ReadF32(), y = p.ReadF32(), z = p.ReadF32();
            if (p.Ok() && m_localAvatar) m_localAvatar->localPosition = Vec3(x, y, z);
            ++m_corrections;
        } else if (type == PawnAck) {
            const std::string pawn = p.ReadString();
            const std::uint32_t seq = p.ReadU32();
            const float x = p.ReadF32(), y = p.ReadF32(), z = p.ReadF32();
            if (p.Ok() && m_hostAcksPawn && pawn == m_pawnId) Reconcile(seq, Vec3(x, y, z));
        } else if (type == RosterMsg) {
            const std::uint32_t n = p.ReadU32();
            std::vector<RosterEntry> list;
            for (std::uint32_t i = 0; i < n && i < 1024 && p.Ok(); ++i) {
                RosterEntry e; e.id = p.ReadU32(); e.name = p.ReadString(); e.userId = p.ReadString();
                e.score = p.ReadI32(); e.seconds = p.ReadF32();
                list.push_back(e);
            }
            if (p.Ok()) m_roster = std::move(list);
        } else if (type == ClockPing) {
            // The server's clock heartbeat: answer with our game clock.
            const std::uint64_t nonce = ReadU64(p);
            const std::uint32_t seq = p.ReadU32();
            const std::string key = p.ReadString();
            if (!p.Ok() || key.empty() || key.size() > 128) continue;
            const auto ms = static_cast<std::uint64_t>(m_gameClockMs);
            net::Packet r(ClockPong);
            WriteU64(r, nonce); r.Write(seq); WriteU64(r, ms);
            r.Write(net::ClockCheck::Answer(key, nonce, seq, ms));
            SendDatagram(m_serverEp, r.Data(), r.Size());
        } else if (type == Pong) {
            float stamp = p.ReadF32();
            m_rtt = (m_clock - stamp) * 1000.0f;   // round-trip in ms
            if (m_rtt < 0.0f) m_rtt = 0.0f;
        }
    }
    if (m_relayLost) { Stop(); return; }   // host gone or changed, or refused (RelayError())

    // Authoritative movement: sample this frame's input for the next tick's
    // command (a press between ticks still counts).
    m_predicting = false;
    if (m_hostAuthoritative && m_joined) {
        PlayerInput f;
        bool have = false;
        auto sync = m_syncObjs.find(m_pawnId);
        Transform* pawnT = sync != m_syncObjs.end() ? sync->second.t : nullptr;
        IPlayerSim* sim = pawnT ? FindPlayerSim(pawnT->gameObject) : nullptr;
        if (m_inputSource) { f = m_inputSource(); have = true; }
        else if (sim) { f = sim->ReadLocalInput(); have = true; }
        if (have) { m_heldInput = f; m_pressedAccum |= f.pressed; }
        // Prediction: move our own player here, through the same Simulate the
        // host will run, with the input we last sent (so the two move the
        // same way: a key let go between ticks would otherwise move us a frame
        // further than the host and pull us back), but this frame's look; then
        // ease in what's left of any correction.
        if (have && sim && m_hostAcksPawn) {
            m_predicting = true;
            PlayerInput local = m_predCmd;
            local.yaw = f.yaw; local.pitch = f.pitch;
            local.pressed = m_predPressOnce;   // a press counts once, as on the host
            m_predPressOnce = 0;
            local.Clamp();   // as the host will
            sim->Simulate(local, dt);
            const float k = reconcileRate > 0.0f ? 1.0f - std::exp(-reconcileRate * dt) : 1.0f;
            const Vec3 step = m_predPending * k;
            pawnT->localPosition = pawnT->localPosition + step;
            m_predPending = m_predPending - step;
            sim->OnCorrected(step);
        }
    }
    // Send our local state to the server: once a frame, or once a tick when
    // the host runs a fixed net tick (with our estimate of that tick).
    const bool sendNow = m_hostTickRate > 0.0f ? (m_joined && StepNetTick(m_hostTickRate, dt) > 0) : true;
    if (m_joined && m_localAvatar && sendNow) {
        Vec3 lp = m_localAvatar->localPosition;
        net::Packet p(State);
        p.Write(m_localId);
        p.Write(lp.x); p.Write(lp.y); p.Write(lp.z);
        p.Write(std::uint8_t(m_localGlyph));
        if (m_hostTickRate > 0.0f) p.Write(m_netTick);   // trailing: an older host stops reading before it
        if (m_sessionCookie) WriteU64(p, m_sessionCookie);
        SendDatagram(m_serverEp, p.Data(), p.Size());
        ++m_statesSent;
    }
}

} // namespace okay

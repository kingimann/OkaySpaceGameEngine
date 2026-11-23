#include "okay/Net/NetworkManager.hpp"
#include "okay/Net/NetSim.hpp"
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
#include "okay/Net/RelayHello.hpp"
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
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Render/Color.hpp"
#include "okay/Core/Log.hpp"
#include "okay/Net/NetBuildSync.hpp"
#include "okay/Net/NetQuestSync.hpp"
#include <algorithm>
#include <cmath>
#include <random>
#include <iomanip>
#include <sstream>
#include "NetworkManagerInternal.hpp"

namespace okay {

namespace {
std::mutex g_liveMutex;
std::unordered_set<const NetworkManager*>& LiveManagers() { static std::unordered_set<const NetworkManager*> s; return s; }
} // namespace

NetworkManager::NetworkManager() : m_buildSync(std::make_unique<net::BuildSync>(*this)), m_questSync(std::make_unique<net::QuestSync>(*this)) {
    std::lock_guard<std::mutex> l(g_liveMutex); LiveManagers().insert(this);
}
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
        AcAudit("cheat", IdentOfPeer(r.peer), std::string("kind=") + CheatKindName(r.kind) + " peer=" + std::to_string(r.peer) + " " + r.detail);
        if (m_cheatHandler) m_cheatHandler(r);
    };
    anticheat.onDetectAudit = [this](const DetectResponse& r) {
        AcAudit("detect", IdentOfPeer(r.peer), std::string("action=") + DetectActionName(r.action) + " peer=" + std::to_string(r.peer) + " risk=" + std::to_string((int)r.risk));
    };
    anticheat.rttOf = [this](std::uint32_t peer) {
        for (const auto& [ep, c] : m_clients) if (c.id == peer) return c.rtt;
        return -1.0f;
    };
    // The detection engine's view of the world (static solids and the ground), from the scene's physics.
    anticheat.detector.hooks.groundAt = [this](const Vec3& pos, float& groundY) {
        Scene* sc = GetScene();
        if (!sc) return false;
        const Vec3 from(pos.x, pos.y + 0.5f, pos.z);
        for (const auto& h : sc->physics3D().RaycastAll(*sc, from, Vec3(0, -1, 0), 2000.0f, nullptr, Physics3D::kAllLayers, false)) {
            if (!h.gameObject || h.gameObject->GetComponent<NetworkSync>()) continue;   // players and their objects are not the ground
            groundY = h.point.y;
            return true;
        }
        return false;
    };
    anticheat.detector.hooks.staticBlocked = [this](const Vec3& a, const Vec3& b) {
        Scene* sc = GetScene();
        if (!sc) return false;
        const float len = (b - a).Magnitude();
        if (len < 1e-4f) return false;
        // Both ways: a ray that starts inside a solid doesn't see the face it leaves through, so the step that
        // comes out of a wall is found by the ray from the far end.
        auto solidBetween = [&](const Vec3& from, const Vec3& to) {
            const Vec3 dir = (to - from) * (1.0f / len);
            for (const auto& h : sc->physics3D().RaycastAll(*sc, from, dir, len, nullptr, Physics3D::kAllLayers, false)) {
                if (!h.gameObject || h.distance < 0.01f || h.distance > len - 0.01f) continue;
                if (h.gameObject->GetComponent<NetworkSync>()) continue;   // players and their objects are not walls
                if (const auto* rb = h.gameObject->GetComponent<Rigidbody3D>(); rb && rb->bodyType != Rigidbody3D::BodyType::Static) continue;
                return true;
            }
            return false;
        };
        return solidBetween(a, b) || solidBetween(b, a);
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

std::string NetworkManager::TicketBinding() {
    if (!encryption || !net::SecureChannel::Available()) return {};
    if (!m_nextSecure.HasKeys()) m_nextSecure.GenerateKeys();
    return net::SessionTicket::KeyBinding(m_nextSecure.PublicKey());
}

void NetworkManager::UseNextSessionKeys() {
    if (!encryption || !net::SecureChannel::Available()) return;
    if (m_nextSecure.HasKeys()) m_secure = m_nextSecure;   // the keys a ticket was bound to (TicketBinding)
    else m_secure.GenerateKeys();
    m_nextSecure = net::SecureChannel{};                   // used once: the next session makes new ones
}

namespace {
    NetworkManager::JoinGate g_joinGate;
    std::string g_joinRefusal;
}
void NetworkManager::SetJoinGate(JoinGate gate) { g_joinGate = std::move(gate); g_joinRefusal.clear(); }
std::string NetworkManager::LastJoinRefusal() { return g_joinRefusal; }

bool NetworkManager::StartClient(const std::string& host, std::uint16_t port) {
    g_joinRefusal.clear();
    if (g_joinGate) {
        std::string why;
        if (!g_joinGate(*this, host, port, false, std::string(), why)) {
            g_joinRefusal = why.empty() ? std::string("join refused") : why;
            OKAY_WARN("net: not joining ", host, ":", port, ": ", g_joinRefusal);
            return false;
        }
    }
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
    if (!m_keepResume) {   // a new session of the game's own, not one of our retries
        m_connectionLost = false; m_connectionLostReason.clear();
        m_lastConn = ConnTarget{1, host, port, std::string()};
        m_reconnectGaveUp = false; m_reconnectCount = 0; m_resumed = false;
        m_ownSpawns.clear(); m_resendQ.clear();
    }
    m_serverSilence = 0.0f; m_joinWait = 0.0f;
    m_localUserId.clear();
    UseNextSessionKeys();
    OKAY_INFO("net: client connecting to ", m_serverEp.ToString());
    return true;
}

bool NetworkManager::StartRelay(const std::string& relayHost, std::uint16_t relayPort,
                                const std::string& code, bool asHost) {
    if (!asHost) {
        g_joinRefusal.clear();
        if (g_joinGate) {
            std::string why;
            if (!g_joinGate(*this, relayHost, relayPort, true, code, why)) {
                g_joinRefusal = why.empty() ? std::string("join refused") : why;
                OKAY_WARN("net: not joining through the relay: ", g_joinRefusal);
                return false;
            }
        }
    }
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
    m_relayHostKey = 0;
    std::memset(m_relayCookie, 0, sizeof m_relayCookie);
    m_relayHelloTimer = 0.0f;
    m_relayWait = 0.0f;
    m_relayBadWelcomes = 0;
    m_relaySlot = 0;
    {   // The welcome must echo this back (see RelayProtocol.hpp, version 4).
        std::random_device rd;
        do { m_relayNonce = (static_cast<std::uint64_t>(rd()) << 32) | rd(); } while (m_relayNonce == 0);
    }
    m_relayError.clear();
    m_kicked = false; m_kickReason.clear();
    if (asHost) {
        InitServerState();
        OKAY_INFO("net: hosting '", serverName, "' via relay ", m_relayEp.ToString(),
                  " (session '", code, "')");
    } else {
        m_mode = Mode::Client;
        m_joined = false;
        if (!m_keepResume) {
            m_connectionLost = false; m_connectionLostReason.clear();
            m_lastConn = ConnTarget{2, relayHost, relayPort, code};
            m_reconnectGaveUp = false; m_reconnectCount = 0; m_resumed = false;
            m_ownSpawns.clear(); m_resendQ.clear();
        }
        m_serverSilence = 0.0f; m_joinWait = 0.0f;
        m_localUserId.clear();
        UseNextSessionKeys();
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
    if (!m_relay || m_relayLost) return;
    // A relay that never answers (dead, wrong port, or no host for a client's code)
    // ends the attempt with an error instead of leaving the player waiting forever.
    if (!m_relayWelcomed && relayTimeoutSeconds > 0.0f && (m_relayWait += dt) >= relayTimeoutSeconds) {
        m_relayError = m_relayBadWelcomes > 0
            ? "the relay's welcome could not be verified (an older relay? see allowUnverifiedRelay)"
            : "the relay never answered (it is unreachable, or no host is registered for session code '" +
              m_relayCode + "')";
        OKAY_WARN("net: ", m_relayError);
        m_relayLost = true;
        return;
    }
    m_relayHelloTimer -= dt;
    if (m_relayHelloTimer > 0.0f) return;
    m_relayHelloTimer = 1.0f;
    net::RelayHelloParams hp;
    hp.role = m_mode == Mode::Server ? net::RelayHost : net::RelayClient;
    hp.code = m_relayCode;
    hp.hostKey = m_relayHostKey;                 // 0 until the relay gives one
    std::memcpy(hp.cookie.data(), m_relayCookie, hp.cookie.size());   // zero until the relay sends one
    hp.nonce = m_relayNonce;                     // version 4: the welcome echoes it
    const std::vector<std::uint8_t> hello = net::BuildRelayHello(hp);
    m_socket.SendTo(m_relayEp, hello.data(), hello.size());
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
    m_relayHostKey = 0;
    m_relayNonce = 0;
    std::memset(m_relayCookie, 0, sizeof m_relayCookie);
    m_relayCode.clear();
    m_relayEp = net::Endpoint{};
    net::ResetSimulator(m_socket);
    m_socket.Close();
    if (m_netStarted) { net::Shutdown(); m_netStarted = false; }
    m_clients.clear();
    m_remotes.clear();
    m_remoteTarget.clear();
    m_remoteMissing.clear();
    m_inbox.clear();
    m_rpcObserver = nullptr;
    m_syncVars.clear();
    m_dead.clear();
    if (!m_keepResume) { m_buildSync->Reset(); m_questSync->Reset(); }
    m_rtt = 0.0f;
    m_ready = false;
    m_matchStarted = false;
    m_serverName.clear();
    m_joinRejected = false;
    m_joinRejectReason.clear();
    m_relOut.clear();
    m_relSeen.Clear();
    m_snapUp.Clear();
    m_snapDown.Clear();
    m_relaySnap.clear();
    m_toServer.Clear();
    m_seqOrigin.Clear();
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
    m_parked.clear();
    if (!m_keepResume) { m_reconnecting = false; m_reconnectTrying = false; m_resumeToken = 0; m_resendQ.clear(); }
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

// What the detection engine learns from a hit claim: its outcome and the
// numbers the server's own checks measured.
void NetworkManager::FeedShot(std::uint32_t shooter, const HitClaim& claim, bool hit, const std::string& why) {
    if (!anticheat.DetectOn()) return;
    ShotEvent e;
    e.t = anticheat.Clock();
    e.weapon = claim.weapon;
    e.hit = hit;
    e.damage = claim.damage;
    e.distance = m_hitDist;
    e.offRay = m_hitOff; e.allowedOffRay = m_hitAllowed;
    e.blockedByWall = why == "through a wall";
    e.badOrigin = why.rfind("shot from where", 0) == 0;
    e.outOfRange = why == "out of range";
    if (!hit && why.rfind("missed", 0) != 0) { e.offRay = 0.0f; e.allowedOffRay = 0.0f; }   // only a miss is about where the target was
    e.detail = why;
    anticheat.detector.OnShot(shooter, e);
}

// "<targetNetId>|<weapon>|<clientTick>|ox oy oz|dx dy dz" from client `c`. The
// server believes its own view: where it last saw the shooter, where the
// target is, what's in between.
void NetworkManager::HandleHitClaim(Client& c, const std::string& data) {
    const std::uint32_t shooter = c.id;
    HitClaim fed;   // what the detection engine is told (set once the claim is parsed)
    fed.shooter = shooter;
    m_hitDist = m_hitOff = m_hitAllowed = 0.0f;
    auto refuse = [&](const std::string& why) {
        ++m_hitsRefused;
        if (anticheat.DetectOn()) {
            if (why.rfind("malformed", 0) == 0) anticheat.detector.OnMalformed(shooter, why, anticheat.Clock());
            else if (why == "through a wall" || why.rfind("missed", 0) == 0 || why.rfind("shot from where", 0) == 0 ||
                     why == "out of range" || why.rfind("fire rate", 0) == 0)
                FeedShot(shooter, fed, false, why);
        }
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
    if (end && *end == ':') { viewTick = std::strtod(end + 1, nullptr); viewTick = std::isfinite(viewTick) ? std::max(0.0, viewTick) : -1.0; }
    claim.direction = dir * (1.0f / dir.Magnitude());
    claim.damage = anticheat.settings.hitDamage;
    fed = claim;
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
    // With a fixed tick, a claim must say when the shooter looked: without
    // it, the check would fall back to the target's speed x the ping as
    // slack. Older clients don't say, so not a violation.
    if (viewTick < 0.0 && HostTickRate() > 0.0f) { ++m_hitsRefused; return; }
    const float rtt = std::min(c.rtt >= 0.0f ? c.rtt : 0.25f, std::max(0.0f, s.maxRewindRtt));
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
    if (anticheat.DetectOn()) {
        fed = claim;
        FeedShot(shooter, claim, true, std::string());
        if (anticheat.detector.KickRequested(shooter)) { AntiCheatAfterRefusal(shooter); return; }   // (c may be gone)
        if (anticheat.detector.ShadowRestricted(shooter)) { ++m_hitsRefused; return; }              // no damage, and no sign of it
    }
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
    m_hitDist = std::max(0.0f, along);
    m_hitOff = (to - claim.direction * along).Magnitude();
    m_hitAllowed = s.hitTolerance + (s.maxSyncSpeed > 0.0f ? s.maxSyncSpeed : 8.0f) * slack;
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
    m_applyingHit = true;   // a death it causes goes out in the __dmg below, not as a __death
    hs->Damage(claim.damage);
    m_applyingHit = false;
    ++m_hitsApplied;
    char buf[96];
    std::snprintf(buf, sizeof buf, "|%.9g|%.9g|%u", hs->health, claim.damage, (unsigned)claim.shooter);
    SendLife('H', claim.target + buf);
}

void NetworkManager::NoteRespawn(GameObject* go) {
    if (m_mode != Mode::Server || !go || !go->transform || go->scene() != GetScene()) return;
    NetworkSync* ns = go->GetComponent<NetworkSync>();
    if (!ns) return;
    const std::string id = ns->netId.empty() ? go->name : ns->netId;
    auto it = m_syncObjs.find(id);
    if (it == m_syncObjs.end() || it->second.t != go->transform) return;
    const Vec3 pos = go->transform->Position();
    auto* hs = go->GetComponent<HealthStat>();
    anticheat.AllowTeleport(id);
    it->second.history.clear();   // lag compensation must not rewind across the jump
    it->second.samples.clear();
    // A player's own object: the position it reports next is far from the last one, once.
    std::uint32_t owner = anticheat.OwnerOf(id);
    if (auto po = m_pawnOwner.find(id); po != m_pawnOwner.end()) owner = po->second;
    if (id.compare(0, 3, "sp/") == 0) {   // a SpawnOwned id names the peer that made it
        const std::size_t slash = id.find('/', 3);
        if (slash != std::string::npos && slash > 3 && slash <= 13 &&
            id.find_first_not_of("0123456789", 3) == slash)
            owner = static_cast<std::uint32_t>(std::strtoull(id.c_str() + 3, nullptr, 10));
    }
    if (owner != 0)
        for (auto& [ep, c] : m_clients)
            if (c.id == owner) c.teleportUntil = m_serverTime + 1.0f;
    if (owner != 0 && anticheat.DetectOn()) anticheat.detector.NoteRespawn(owner, anticheat.Clock());   // grace: its next positions and shots
    char buf[160];
    std::snprintf(buf, sizeof buf, "%.9g|%.9g|%.9g|%.9g|", hs ? hs->health : 0.0f, pos.x, pos.y, pos.z);
    m_dead.erase(id);
    SendLife('R', std::string(buf) + id);
}

void NetworkManager::NoteDeath(GameObject* go, bool hasFrom, const Vec3& from) {
    if (m_mode != Mode::Server || !go || !go->transform || go->scene() != GetScene()) return;
    NetworkSync* ns = go->GetComponent<NetworkSync>();
    if (!ns) return;
    const std::string id = ns->netId.empty() ? go->name : ns->netId;
    auto it = m_syncObjs.find(id);
    if (it == m_syncObjs.end() || it->second.t != go->transform) return;
    // Remembered either way (a death a hit carried too) for a client that joins while it is dead.
    if (m_dead.size() < kMaxDeadTracked || m_dead.count(id)) m_dead[id] = DeadInfo{hasFrom, hasFrom ? from : Vec3{}};
    if (m_applyingHit) return;   // the hit's "__dmg" carries it
    char buf[160];
    std::snprintf(buf, sizeof buf, "%d|%.9g|%.9g|%.9g|", hasFrom ? 1 : 0, hasFrom ? from.x : 0.0f,
                  hasFrom ? from.y : 0.0f, hasFrom ? from.z : 0.0f);
    SendLife('D', std::string(buf) + id);
    ++m_deathsSent;
}

void NetworkManager::SendLife(char kind, const std::string& body, std::int64_t only) {
    if (m_mode != Mode::Server) return;
    const std::string data = std::string(1, kind) + body;
    if (only >= 0) SendOrderedTo((std::uint32_t)only, "__life", data);
    else SendOrdered("__life", data);
}

// A client that joins now missed every death and every damage hit so far. Tell it what is true now:
// who is dead (where the blow came from, for the fall) and who is hurt. Ordered with what follows, so a
// respawn that comes next cannot overtake it.
void NetworkManager::SendLifeSnapshot(std::uint32_t peer) {
    for (auto it = m_dead.begin(); it != m_dead.end();) {
        auto obj = m_syncObjs.find(it->first);
        // Still here and alive (revived without a respawn message): not dead any more.
        if (obj != m_syncObjs.end() && obj->second.t && obj->second.t->gameObject) {
            if (auto* hs = obj->second.t->gameObject->GetComponent<HealthStat>()) if (!hs->dead) { it = m_dead.erase(it); continue; }
        }
        char buf[160];
        std::snprintf(buf, sizeof buf, "%d|%.9g|%.9g|%.9g|", it->second.hasFrom ? 1 : 0, it->second.from.x, it->second.from.y, it->second.from.z);
        SendLife('D', std::string(buf) + it->first, peer);
        ++m_lateLifeSent;
        ++it;
    }
    for (auto& [id, o] : m_syncObjs) {
        if (!o.t || !o.t->gameObject || m_dead.count(id)) continue;
        auto* hs = o.t->gameObject->GetComponent<HealthStat>();
        if (!hs || hs->dead || !std::isfinite(hs->health) || hs->health >= hs->maxHealth) continue;
        char buf[96];
        std::snprintf(buf, sizeof buf, "|%.9g|0|0", hs->health);
        SendLife('H', id + buf, peer);
        ++m_lateLifeSent;
    }
}

void NetworkManager::SetRequestHandler(const std::string& channel, RequestHandler handler) {
    if (channel.compare(0, 6, "__req/") != 0 || channel.size() <= 6) return;
    if (handler) m_requestHandlers[channel] = std::move(handler);
    else m_requestHandlers.erase(channel);
}
void NetworkManager::ClearRequestHandler(const std::string& channel) { m_requestHandlers.erase(channel); }

int NetworkManager::AddJoinHook(std::function<void(std::uint32_t)> hook) {
    if (!hook) return 0;
    m_joinHooks.emplace_back(m_nextJoinHook, std::move(hook));
    return m_nextJoinHook++;
}
void NetworkManager::RemoveJoinHook(int id) {
    m_joinHooks.erase(std::remove_if(m_joinHooks.begin(), m_joinHooks.end(),
                                     [&](const auto& h) { return h.first == id; }), m_joinHooks.end());
}

bool NetworkManager::PeerPosition(std::uint32_t id, Vec3& out) const {
    for (const auto& [ep, c] : m_clients)
        if (c.id == id && c.hasState) { out = Vec3(c.state.x, c.state.y, c.state.z); return true; }
    return false;
}

bool NetworkManager::HandleClientRequest(Client& from, const std::string& channel, const std::string& data) {
    if (channel.compare(0, 6, "__req/") != 0) return false;
    auto it = m_requestHandlers.find(channel);
    if (it == m_requestHandlers.end()) return true;
    if (maxRelayedBytes > 0 && channel.size() + data.size() > (std::size_t)maxRelayedBytes) return true;
    if (!dedicated && m_localRoom != from.room) return true;
    if (!AntiCheatAllows(from.id, channel, data)) return true;
    const std::uint32_t id = from.id;
    RequestHandler h = it->second;   // a copy: the handler may unregister itself
    h(id, data);
    return true;
}

void NetNoteDeath(GameObject* go, bool hasFrom, const Vec3& from) {
    std::vector<NetworkManager*> live;
    {
        std::lock_guard<std::mutex> l(g_liveMutex);
        for (const NetworkManager* nm : LiveManagers()) live.push_back(const_cast<NetworkManager*>(nm));
    }
    for (NetworkManager* nm : live) nm->NoteDeath(go, hasFrom, from);
}

void NetNoteRespawn(GameObject* go) {
    std::vector<NetworkManager*> live;
    {
        std::lock_guard<std::mutex> l(g_liveMutex);
        for (const NetworkManager* nm : LiveManagers()) live.push_back(const_cast<NetworkManager*>(nm));
    }
    for (NetworkManager* nm : live) nm->NoteRespawn(go);
}

// A client leaves the table (Leave, timeout, kick, or taken over by its own
// reconnect): its objects go, and its state is kept for a while so the same
// client can resume it.
void NetworkManager::ReleaseClient(std::unordered_map<net::Endpoint, Client, net::EndpointHash>::iterator it) {
    const std::uint32_t id = it->second.id;
    if (auto r = m_remotes.find(id); r != m_remotes.end()) {
        if (GetScene() && r->second) GetScene()->Destroy(r->second);
        m_remotes.erase(r);
    }
    anticheat.ParkPeer(id, it->second.ident);
    ParkClient(it->second);
    m_clients.erase(it);
    anticheat.ForgetPeer(id);
    DespawnPeerObjects(id);
    m_recentCheats.erase(id);
    for (auto po = m_pawnOwner.begin(); po != m_pawnOwner.end();) po = po->second == id ? m_pawnOwner.erase(po) : std::next(po);
    if (m_peerLeft) m_peerLeft(id);
}

std::size_t NetworkManager::CollectUnacked(
    const std::unordered_map<std::uint32_t, std::pair<std::vector<std::uint8_t>, float>>& relOut,
    const net::OrderedEnd& ordered, std::vector<Unacked>& out) const {
    std::size_t left = 0, bytes = 0, taken = 0;
    std::map<std::uint32_t, const std::vector<std::uint8_t>*> bySeq;   // oldest first
    for (const auto& [seq, e] : relOut) bySeq[seq] = &e.first;
    for (const auto& [seq, raw] : bySeq) {
        net::Packet p(raw->data(), raw->size());   // [ReliableMsg][u32 seq][channel][data]
        p.ReadU8(); p.ReadU32();
        std::string channel = p.ReadString();
        std::string data = p.ReadString();
        if (!p.Ok()) continue;
        if (taken >= kMaxResendMessages || bytes + data.size() > kMaxResendBytes) { ++left; continue; }
        bytes += data.size(); ++taken;
        out.push_back(Unacked{false, std::move(channel), std::move(data)});
    }
    std::vector<std::pair<std::string, std::string>> ord;
    left += ordered.Unacked(ord, kMaxResendMessages - taken, kMaxResendBytes - bytes);
    for (auto& [channel, data] : ord) out.push_back(Unacked{true, std::move(channel), std::move(data)});
    return left;
}

void NetworkManager::ResendUnacked(std::uint32_t target, std::vector<Unacked>& q) {
    std::vector<Unacked> work;
    work.swap(q);
    for (const Unacked& u : work) {
        if (u.ordered) SendOrderedTo(target, u.channel, u.data);
        else SendReliableTo(target, u.channel, u.data);
        ++m_resentTotal;
    }
}

void NetworkManager::ParkClient(const Client& c) {
    if (resumeGraceSeconds <= 0.0f || c.resumeToken == 0) return;
    for (auto it = m_parked.begin(); it != m_parked.end();)
        it = (it->token == c.resumeToken || it->id == c.id) ? m_parked.erase(it) : std::next(it);
    if (m_parked.size() >= kMaxParked) m_parked.erase(m_parked.begin());   // the oldest
    Parked p;
    p.token = c.resumeToken; p.id = c.id; p.userId = c.userId;
    p.pos = Vec3{c.state.x, c.state.y, c.state.z}; p.hasState = c.hasState; p.glyph = c.state.glyph;
    p.score = c.score; p.ready = c.ready;
    m_resendDropped += CollectUnacked(c.relOut, c.pipes.ordered, p.resend);
    m_parked.push_back(std::move(p));
}

// The host went silent (or the relay session ended) and autoReconnect is on:
// keep what the game owns, drop what only the session knew, and start retrying.
void NetworkManager::BeginReconnect() {
    m_wasReady = m_ready;
    // Other players' proxies describe a session that is gone; the new one makes
    // them again from its snapshots.
    for (auto& [id, go] : m_remotes)
        if (GetScene() && go) GetScene()->Destroy(go);
    m_remotes.clear();
    // Reliable messages the host never acked: kept, and sent again if the rejoin resumes.
    m_resendQ.clear();
    m_resendDropped += CollectUnacked(m_relOut, m_toServer.ordered, m_resendQ);
    m_keepResume = true;
    Stop();
    m_keepResume = false;
    m_reconnecting = true; m_reconnectTrying = false; m_reconnectGaveUp = false;
    m_reconnectAttempt = 0;
    m_reconnectTimer = std::max(0.0f, reconnectDelaySeconds);
    if (m_reconnectHandler) m_reconnectHandler(ReconnectEvent::Lost, 0);
}

void NetworkManager::ReconnectTick(float dt) {
    if (!m_reconnecting || m_reconnectTrying) return;
    if ((m_reconnectTimer -= dt) > 0.0f) return;
    ++m_reconnectAttempt;
    m_reconnectTrying = true;
    OKAY_INFO("net: reconnect try ", m_reconnectAttempt, " of ", reconnectMaxAttempts);
    m_keepResume = true;
    const bool ok = m_lastConn.kind == 2
        ? StartRelay(m_lastConn.host, m_lastConn.port, m_lastConn.code, /*asHost=*/false)
        : StartClient(m_lastConn.host, m_lastConn.port);
    m_keepResume = false;
    if (m_reconnectHandler) m_reconnectHandler(ReconnectEvent::Attempt, m_reconnectAttempt);
    if (!ok) FailReconnectTry();
}

// A try ended without a session (refused, no answer, relay gone): back off and
// try again, or give up.
void NetworkManager::FailReconnectTry() {
    const std::string why = m_joinRejectReason;
    bool final = m_reconnectAttempt >= reconnectMaxAttempts;
    for (const char* hard : {"wrong password", "authentication failed", "kicked by", "session ticket refused", "encryption required"})
        if (why.find(hard) != std::string::npos) final = true;
    if (m_kicked) final = true;
    m_keepResume = true;
    Stop();
    m_keepResume = false;
    m_reconnectTrying = false;
    if (final) {
        m_reconnecting = false; m_reconnectGaveUp = true; m_resumeToken = 0;
        m_resendDropped += m_resendQ.size(); m_resendQ.clear();
        OKAY_WARN("net: giving up on reconnecting after ", m_reconnectAttempt, " tries");
        if (m_reconnectHandler) m_reconnectHandler(ReconnectEvent::GaveUp, m_reconnectAttempt);
        return;
    }
    float d = std::max(0.0f, reconnectDelaySeconds);
    for (int i = 0; i < m_reconnectAttempt && d < reconnectMaxDelaySeconds; ++i) d *= 2.0f;
    m_reconnectTimer = std::min(d, std::max(reconnectMaxDelaySeconds, reconnectDelaySeconds));
}

// After a rejoin that got its old peer id back, tell the host about the
// objects this player had made (it removed them when the connection dropped).
void NetworkManager::ReannounceOwnSpawns() {
    if (!m_resumed) { return; }   // a new peer id: the old netIds are not ours; the game respawns
    for (auto it = m_ownSpawns.begin(); it != m_ownSpawns.end();) {
        GameObject* g = ObjectByNetId(it->first);
        if (!g || !g->transform) { it = m_ownSpawns.erase(it); continue; }
        const Vec3 pos = g->transform->localPosition;
        std::ostringstream os;
        os << it->second << '|' << pos.x << '|' << pos.y << '|' << pos.z << '|' << it->first;
        Send("__spawn", os.str());
        ++it;
    }
}

void NetworkManager::Kick(std::uint32_t id, const std::string& reason) {
    if (m_mode != Mode::Server) return;
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        if (it->second.id != id) continue;
        const std::string ident = it->second.ident;
        net::Packet p(Kicked); p.Write(reason);
        SendDatagram(it->second.endpoint, p.Data(), p.Size());
        it->second.resumeToken = 0;   // a kicked player does not get its state back
        ReleaseClient(it);
        OKAY_INFO("net: kicked peer ", id, reason.empty() ? "" : (" (" + reason + ")"));
        AcAudit("kicked", ident, "peer=" + std::to_string(id) + " reason=" + reason);
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
        if (anticheat.DetectOn()) anticheat.detector.NoteTeleport(id, anticheat.Clock());
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


} // namespace okay

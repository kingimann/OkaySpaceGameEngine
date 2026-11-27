// NetworkManager, part: server authority: lag compensation (rewind), network prefab path checks, anti-cheat checks, movement authority and client prediction.
// Moved unchanged from NetworkManager.cpp (docs/engine/modularization_plan.md, batch N1).
// The class is declared in okay/Net/NetworkManager.hpp; file-local helpers shared by the parts are in
// NetworkManagerInternal.hpp.
#include "okay/Net/NetworkManager.hpp"
#include "okay/Net/NetSim.hpp"
#include "okay/Net/SessionTicket.hpp"
#include "okay/AntiCheat/AntiCheatCrypto.hpp"   // ed25519 for the host identity (#269)
#include "OfficialGame.hpp"      // shared/: SafeRelPath / ResolveInside for network prefab paths
#include "okay/Components/NetworkPlayerSpawner.hpp"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <unordered_set>
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

// ---- Lag compensation (server) ------------------------------------------------

void NetworkManager::RecordHistory() {
    const std::size_t keep = (std::size_t)std::ceil(HostTickRate()) + 2;   // ~1 s
    for (auto& [id, o] : m_syncObjs) {
        // A despawned pawn is released (and its history cleared) before the sync
        // entry is erased. An entry whose object is already gone is not a target.
        if (!o.t || !o.t->gameObject) { o.history.clear(); continue; }
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
                                           double viewTick, GameObject* ignore, std::uint32_t shooter) {
    Scene* sc = GetScene();
    if (!sc) return RaycastHit3D{};
    if (m_mode != Mode::Server || HostTickRate() <= 0.0f)
        return sc->physics3D().Raycast(*sc, origin, direction, maxDistance, ignore);
    // Same window as a hit claim. No shooter: 200 ms, not the history's second.
    float rtt = 0.0f;
    if (shooter != 0)
        for (const auto& [ep, c] : m_clients)
            if (c.id == shooter) {
                rtt = std::min(c.rtt >= 0.0f ? c.rtt : 0.25f, std::max(0.0f, anticheat.settings.maxRewindRtt));
                break;
            }
    const double at = ClampRewind(viewTick, rtt);
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

void NetworkManager::AcFilesSync() {
    const AntiCheatSettings& s = anticheat.settings;
    if (!m_acFilesReady || s.banFile != m_banPath) {
        m_banPath = s.banFile;
        std::string err;
        if (!m_banList.Load(m_banPath, &err)) OKAY_ERROR("anti-cheat: ", err, " (bans from this file are not enforced)");
        else if (!m_banPath.empty()) OKAY_INFO("anti-cheat: ban list ", m_banPath, ": ", m_banList.Size(), " ban(s)",
                                               m_banList.Skipped() ? (", " + std::to_string(m_banList.Skipped()) + " unreadable line(s) skipped") : std::string());
    }
    if (!m_acFilesReady || s.auditLog != m_auditPath) {
        m_auditPath = s.auditLog;
        m_audit.Open(m_auditPath);
    }
    m_acFilesReady = true;
}

void NetworkManager::AcAudit(const std::string& event, const std::string& ident, const std::string& detail) {
    if (m_mode != Mode::Server || anticheat.settings.auditLog.empty()) return;
    AcFilesSync();
    m_audit.Write((std::int64_t)std::time(nullptr), event, ident, detail);
}

std::string NetworkManager::IdentOfPeer(std::uint32_t peer) const {
    for (const auto& [ep, c] : m_clients) if (c.id == peer) return c.ident;
    return std::string();
}

bool NetworkManager::BannedByList(const std::string& ident, std::string& why) {
    if (m_mode != Mode::Server) return false;
    AcFilesSync();
    BanEntry e;
    if (!m_banList.IsBanned(ident, (std::int64_t)std::time(nullptr), &e)) return false;
    why = "banned from this server" + std::string(e.until ? " (until " + UtcTimeText(e.until) + ")" : "");
    return true;
}

void NetworkManager::AntiCheatAfterRefusal(std::uint32_t peer) {
    const int limit = anticheat.settings.kickAfterViolations;
    const bool detectKick = anticheat.DetectOn() && anticheat.detector.KickRequested(peer);
    if ((limit > 0 && anticheat.Violations(peer) >= limit) || detectKick) {
        if (m_acKickHandler) {
            AntiCheatKick k;
            k.peer = peer;
            if (anticheat.DetectOn()) {
                k.risk = anticheat.detector.Risk(peer);
                k.banRequested = anticheat.detector.BanRequested(peer);
                k.evidence = anticheat.detector.Bundle(peer, BundleOptions{}).ToJson(12 * 1024);
            }
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
        // Escalation: kicks add up, and a ban request from the detection engine (or enough kicks)
        // puts the identity on the ban list, so a rejoin after the cooldown is refused too.
        {
            const std::string ident = IdentOfPeer(peer);
            if (!ident.empty()) {
                if (m_acKickCounts.size() > 4096) m_acKickCounts.clear();   // can't grow without end
                const int kicks = ++m_acKickCounts[ident];
                const AntiCheatSettings& s = anticheat.settings;
                const bool requested = s.banOnRequest && anticheat.DetectOn() && anticheat.detector.BanRequested(peer);
                const bool tooMany = s.banAfterKicks > 0 && kicks >= s.banAfterKicks;
                AcAudit("strike", ident, "reason=anti-cheat kicks=" + std::to_string(kicks) +
                        (anticheat.DetectOn() ? " risk=" + std::to_string((int)anticheat.detector.Risk(peer)) : std::string()));
                if ((requested || tooMany) && ident.size() > 2) {
                    AcFilesSync();
                    const std::string reason = requested ? "ban requested by the detection engine" : "kicked " + std::to_string(kicks) + (kicks == 1 ? " time" : " times") + " by the anti-cheat";
                    if (m_banList.Add(ident, (std::int64_t)std::time(nullptr), s.banHours, reason)) {
                        std::string err;
                        if (!m_banList.Save(&err)) OKAY_ERROR("anti-cheat: ", err);
                        AcAudit("ban", ident, "hours=" + std::to_string((int)s.banHours) + " reason=" + reason);
                        OKAY_INFO("anti-cheat: banned peer ", peer, " (", reason, ")");
                    }
                }
            }
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

void NetworkManager::ResetPawnPrediction() {
    m_predicted.clear();
    m_predPending = Vec3(0.0f, 0.0f, 0.0f);
    m_predSeq = 0;
    m_earlyAckSeq = 0;
    m_earlyAck = Vec3(0.0f, 0.0f, 0.0f);
    m_lastPawnAck = 0;
    m_predPressOnce = 0;
    m_predicting = false;
}

void NetworkManager::ClearLocalPawn() {
    ResetPawnPrediction();
    m_pawnId.clear();
    m_lastAckedSeq = 0;
    m_sentInputs.clear();
    m_inputSeq = 0;          // the server forgot lastInputSeq; the next claim starts at 1
    m_pressedAccum = 0;
    m_heldInput = PlayerInput{};
    m_predCmd = PlayerInput{};
}

// Local despawn, a relayed __despawn, or the object's own OnDestroy. The claim
// has to die with the object or the next pawn is refused (pawn != c.pawn) and
// never acked.
void NetworkManager::ReleasePawn(const std::string& id) {
    if (id.empty()) return;
    if (id == m_pawnId) ClearLocalPawn();
    if (auto it = m_syncObjs.find(id); it != m_syncObjs.end()) it->second.history.clear();
    auto own = m_pawnOwner.find(id);
    if (own == m_pawnOwner.end()) return;
    const std::uint32_t peer = own->second;
    m_pawnOwner.erase(own);
    if (m_mode != Mode::Server) return;
    for (auto& [ep, c] : m_clients) {
        if (c.id != peer || c.pawn != id) continue;
        c.pawn.clear();
        c.inputs.clear();
        c.lastInputSeq = 0;
        c.lastInput = PlayerInput{};
        c.repeats = 0;
        c.inputCredit = 0;
        c.droppedPressed = 0;
        c.droppedN = 0;
        c.haveRepeatPose = false;
        break;
    }
}

void NetworkManager::SetPawn(GameObject* pawn) {
    std::string id;
    if (pawn)
        if (auto* ns = pawn->GetComponent<NetworkSync>())
            id = ns->netId.empty() ? pawn->name : ns->netId;
    if (id == m_pawnId) return;
    // A different object: don't reconcile the previous pawn's acks against it.
    // The sequence counter stays, unless a despawn already cleared it — the
    // server only forgets lastInputSeq when the old pawn is destroyed.
    ResetPawnPrediction();
    m_sentInputs.clear();
    m_lastAckedSeq = 0;
    m_pawnId = id;
}

int NetworkManager::MovementAuthority(const std::string& netId) const {
    if (m_mode == Mode::Server && authoritativeMovement && m_pawnOwner.count(netId)) return 1;
    if (m_mode == Mode::Client && m_hostAuthoritative && !m_pawnId.empty() && netId == m_pawnId) return 0;
    return -1;
}

bool NetworkManager::InputDriven(const std::string& netId) const {
    return m_mode == Mode::Server && authoritativeMovement && m_pawnOwner.count(netId) != 0;
}

namespace {
// A movement controller on this object or anything under it (a player prefab
// sometimes parents the body to an empty root).
IPlayerSim* FindSimInTree(GameObject* go) {
    if (!go) return nullptr;
    if (IPlayerSim* s = FindPlayerSim(go)) return s;
    if (!go->transform) return nullptr;
    for (Transform* c : go->transform->Children())
        if (c) if (IPlayerSim* s = FindSimInTree(c->gameObject)) return s;
    return nullptr;
}
} // namespace

GameObject* NetworkManager::ObjectByNetId(const std::string& id) const {
    if (auto it = m_syncObjs.find(id); it != m_syncObjs.end() && it->second.t)
        return it->second.t->gameObject;
    Scene* sc = GetScene();
    if (!sc) return nullptr;
    for (const auto& go : sc->Objects()) {
        if (!go) continue;
        if (auto* ns = go->GetComponent<NetworkSync>()) {
            const std::string& nid = ns->netId.empty() ? go->name : ns->netId;
            if (nid == id) return go.get();
        }
    }
    return nullptr;
}

// The pawn is whichever object the server says, not whichever one the client
// names in an InputCmd. The first controller-carrying spawn under sp/<id>/ wins;
// a later InputCmd can still claim a different one, but only while this is empty.
void NetworkManager::NoteSpawnedPawn(const std::string& netId, GameObject* go) {
    if (m_mode != Mode::Server || !authoritativeMovement || netId.empty() || !go) return;
    if (netId.compare(0, 3, "sp/") != 0) return;
    const std::size_t slash = netId.find('/', 3);
    if (slash == std::string::npos || slash == 3) return;
    std::uint32_t cid = 0;
    for (std::size_t i = 3; i < slash; ++i) {
        if (netId[i] < '0' || netId[i] > '9') return;
        cid = cid * 10u + static_cast<std::uint32_t>(netId[i] - '0');
    }
    if (!FindSimInTree(go)) return;
    if (m_pawnOwner.count(netId)) return;
    for (auto& [ep, c] : m_clients) {
        if (c.id != cid) continue;
        if (!c.pawn.empty()) return;   // a live pawn stays until it's released
        c.pawn = netId;
        m_pawnOwner[netId] = c.id;
        c.lastSeen = 0.0f;
        return;
    }
}

bool NetworkManager::ClientMaySendXform(const Client& from, const std::string& id) const {
    // An object the host handed to a player: only that player moves it.
    if (auto own = m_netOwners.find(id); own != m_netOwners.end()) return own->second == from.id;
    if (!authoritativeMovement) return true;
    if (id.empty() || m_pawnOwner.count(id)) return false;
    const std::string prefix = "sp/" + std::to_string(from.id) + "/";
    if (id.compare(0, prefix.size(), prefix) != 0) return true;   // not one of its spawns
    // Until this client has a pawn, a controller object it spawned is not trusted
    // (it may be the player, and it may not be claimed yet). Props and projectiles
    // have no movement controller, so they keep today's transforms. Once a pawn is
    // live, only that pawn is refused — m_pawnOwner above — and a second owned
    // object is left alone.
    if (!from.pawn.empty()) return true;
    if (GameObject* go = ObjectByNetId(id))
        return FindSimInTree(go) == nullptr;
    // Not in the world yet: don't relay a transform we can't classify.
    return false;
}

// Each fixed tick on the server. One queued input is applied (or the last one
// again, for a few ticks, when none has arrived). A hitch delivers several at
// once: while the queue is deeper than kTargetQueuedInputs and this client was
// actually late (inputCredit, earned only while the queue was empty), a second
// input is applied the same tick, so the depth falls back within a few ticks.
// A client that keeps the queue full never earns credit, and never more than
// two inputs are applied, so sending faster than the tick still only fills the
// queue.
//
// Repeats are not an extra step. The pose from before the streak is put back
// when a real input arrives, and that input is the movement for this tick.
void NetworkManager::ServerMovementTick(float step) {
    for (auto& [ep, c] : m_clients) {
        if (c.pawn.empty()) continue;
        auto obj = m_syncObjs.find(c.pawn);
        if (obj == m_syncObjs.end() || !obj->second.t || !obj->second.t->gameObject) continue;
        Transform* tr = obj->second.t;
        GameObject* go = tr->gameObject;
        auto saveRepeat = [&]() {
            c.repeatPos = tr->localPosition;
            c.repeatVel = Vec3(0.0f, 0.0f, 0.0f);
            if (auto* rb = go->GetComponent<Rigidbody3D>()) c.repeatVel = rb->velocity;
            c.haveRepeatPose = true;
        };
        auto restoreRepeat = [&]() {
            if (!c.haveRepeatPose) return;
            tr->localPosition = c.repeatPos;
            if (auto* rb = go->GetComponent<Rigidbody3D>()) rb->velocity = c.repeatVel;
            c.haveRepeatPose = false;
            c.repeats = 0;
        };
        auto publish = [&]() {
            const Vec3 at = tr->TransformPoint(Vec3(0.0f, 0.0f, 0.0f));
            c.state.x = at.x; c.state.y = at.y; c.state.z = at.z;
            c.hasState = true;
        };
        if (c.inputs.empty()) {
            // A late input is covered by the last one for a few ticks (a lost
            // packet); after that the player stands still rather than keep going.
            if (!c.haveRepeatPose) saveRepeat();
            PlayerInput in = c.repeats < kInputRepeats ? c.lastInput : PlayerInput{};
            in.pressed = 0;   // a held button stays held; a press isn't repeated
            ++c.repeats;
            c.inputCredit = std::min(c.inputCredit + 1, static_cast<int>(kMaxQueuedInputs));
            c.lastInput = in;
            if (IPlayerSim* sim = FindSimInTree(go)) sim->Simulate(in, step);
            publish();
            continue;
        }
        // Shallow again: the earlier gap is paid. A full queue from here on is
        // a client sending faster than the tick, not one catching up.
        if (static_cast<int>(c.inputs.size()) <= kTargetQueuedInputs) c.inputCredit = 0;
        if (c.repeats > 0) restoreRepeat();
        int budget = 1;
        if (static_cast<int>(c.inputs.size()) > kTargetQueuedInputs && c.inputCredit > 0) {
            budget = 2;
            --c.inputCredit;
        }
        for (int n = 0; n < budget && !c.inputs.empty(); ++n) {
            auto next = c.inputs.begin();
            PlayerInput in = next->second.second;
            if (c.droppedPressed) { in.pressed |= c.droppedPressed; c.droppedPressed = 0; }
            c.lastInputSeq = std::max(c.lastInputSeq, next->first);
            c.inputs.erase(next);
            c.repeats = 0;
            c.haveRepeatPose = false;
            if (minPressInterval > 0.0f) {   // presses closer together than that are dropped
                const std::uint32_t limited[3] = {PlayerInput::Jump, PlayerInput::Dash, PlayerInput::Fire};
                for (int b = 0; b < 3; ++b) {
                    if (!(in.pressed & limited[b])) continue;
                    if (m_serverTime - c.lastPress[b] < minPressInterval) in.pressed &= ~limited[b];
                    else c.lastPress[b] = m_serverTime;
                }
            }
            c.lastInput = in;
            if (IPlayerSim* sim = FindSimInTree(go)) sim->Simulate(in, step);
            // One ack per seq, at the pose that input left. Prediction compares each.
            const Vec3 lp = tr->localPosition;
            net::Packet a(PawnAck);
            a.Write(c.pawn); a.Write(c.lastInputSeq);
            a.Write(lp.x); a.Write(lp.y); a.Write(lp.z);
            SendDatagram(c.endpoint, a.Data(), a.Size());
        }
        publish();
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

} // namespace okay

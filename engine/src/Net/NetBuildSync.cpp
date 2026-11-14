#include "okay/Net/NetBuildSync.hpp"
#include "okay/Net/NetworkManager.hpp"
#include "okay/Net/NetChannels.hpp"
#include "okay/Components/Buildable.hpp"
#include "okay/Components/LogicLinks.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Core/Log.hpp"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace okay::net {

namespace {

constexpr float kWorldLimit = 1.0e7f;
constexpr std::size_t kMaxChunk = 24 * 1024;     // payload per snapshot message
constexpr std::size_t kMaxLinesPerMessage = 4096;
constexpr std::size_t kMaxPrefabLen = 260;
constexpr int kMaxClientObjects = 8192;           // a client never holds more placed objects than this

// "a\\b" and "./a/b" name the same prefab as "a/b".
std::string PrefabKey(std::string p) {
    std::replace(p.begin(), p.end(), '\\', '/');
    while (p.rfind("./", 0) == 0) p.erase(0, 2);
    return p;
}

bool ParseFloat(const std::string& s, float& out) {
    if (s.empty() || s.size() > 24) return false;
    char* end = nullptr;
    errno = 0;
    const float v = std::strtof(s.c_str(), &end);
    if (end != s.c_str() + s.size() || !std::isfinite(v) || std::fabs(v) > kWorldLimit) return false;
    out = v;
    return true;
}

bool ParseId(const std::string& s, int& out) {
    if (s.empty() || s.size() > 9) return false;
    for (char c : s) if (c < '0' || c > '9') return false;
    const long v = std::strtol(s.c_str(), nullptr, 10);
    if (v <= 0 || v > 100000000) return false;
    out = (int)v;
    return true;
}

std::vector<std::string> Split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::size_t at = 0;
    for (;;) {
        const std::size_t n = s.find(sep, at);
        if (n == std::string::npos) { out.push_back(s.substr(at)); break; }
        out.push_back(s.substr(at, n - at));
        at = n + 1;
    }
    return out;
}

// "px|py|pz|qx|qy|qz|qw" starting at parts[at].
bool ParsePose(const std::vector<std::string>& parts, std::size_t at, Vec3& p, Quat& q) {
    if (parts.size() < at + 7) return false;
    float v[7];
    for (int i = 0; i < 7; ++i) if (!ParseFloat(parts[at + i], v[i])) return false;
    const float len = std::sqrt(v[3] * v[3] + v[4] * v[4] + v[5] * v[5] + v[6] * v[6]);
    if (len < 0.9f || len > 1.1f) return false;   // not a rotation
    p = Vec3(v[0], v[1], v[2]);
    q = Quat(v[3] / len, v[4] / len, v[5] / len, v[6] / len);
    return true;
}

std::string PoseText(const Vec3& p, const Quat& q) {
    char b[200];
    std::snprintf(b, sizeof b, "%.9g|%.9g|%.9g|%.9g|%.9g|%.9g|%.9g", p.x, p.y, p.z, q.x, q.y, q.z, q.w);
    return b;
}

bool PrefabTextOk(const std::string& p) {
    if (p.empty() || p.size() > kMaxPrefabLen) return false;
    for (unsigned char c : p) if (c < 0x20 || c == '|' || c == 0x7f) return false;
    return NetworkManager::IsSafeNetworkPrefabPath(p);
}

} // namespace

BuildSync::BuildSync(NetworkManager& nm) : m_nm(nm) {}
BuildSync::~BuildSync() { ServerOff(); }

long long BuildSync::OwnerOf(int id) const {
    auto it = m_owner.find(id);
    return it == m_owner.end() ? -1 : (long long)it->second;
}

std::size_t BuildSync::HistorySize(std::uint32_t peer) const {
    auto it = m_peers.find(peer);
    return it == m_peers.end() ? 0 : it->second.history.size();
}

void BuildSync::Reset() {
    Unhook();
    ServerOff();
    m_peers.clear();
    m_owner.clear();
    m_removedLevel.clear();
    m_stale.clear();
    m_inSnapshot = false;
    m_actor = -1;
    m_undoing = false;
    if (m_execId) {   // the hidden object that applies changes goes with the session
        GameObject* g = GameObject::FromInstanceId(m_execId);
        Scene* sc = m_nm.GetScene();
        if (g && !g->IsDestroyed() && sc && g->scene() == sc) sc->Destroy(g);
    }
    m_exec = nullptr;
    m_execId = 0;
    m_scene = nullptr;
}

// ---------------------------------------------------------------------------
// Hooking the scene's BuildModes
// ---------------------------------------------------------------------------

void BuildSync::EnsureExecutor(Scene& scene) {
    if (m_exec) {
        GameObject* g = GameObject::FromInstanceId(m_execId);
        if (g && !g->IsDestroyed() && g->scene() == &scene) return;
        m_exec = nullptr;
    }
    GameObject* go = scene.CreateGameObject("BuildNetExec");
    go->tag = BuildMode::kRuntimeTag;   // never saved with the scene
    m_exec = go->AddComponent<BuildMode>();
    m_exec->showHotbar = false;
    m_exec->showGhost = false;
    m_exec->bindings.toggle = 0;   // nobody drives it: it only applies what the server decided
    m_exec->bindings.padToggle = -1;
    m_exec->netOwner = this;
    m_execId = go->InstanceId();
}

void BuildSync::Unhook() {
    Scene* scene = m_nm.GetScene();
    if (scene) {
        for (BuildMode* b : scene->FindObjectsOfType<BuildMode>()) {
            if (!b || b->netOwner != this || b == m_exec) continue;
            b->onChanged = nullptr;
            b->remoteRequest = nullptr;
            b->netOwner = nullptr;
        }
    }
    m_hooked.clear();
}

void BuildSync::Rescan(Scene& scene, bool server, bool client) {
    for (BuildMode* b : scene.FindObjectsOfType<BuildMode>()) {
        if (!b || b == m_exec || (b->gameObject && b->gameObject->tag == BuildMode::kRuntimeTag)) continue;
        if (b->netOwner && b->netOwner != this) continue;   // another sync owns it
        if (b->netOwner == this) continue;
        b->netOwner = this;
        if (server) {
            b->onChanged = [this](const BuildMode::Op& op) { OnChange(op); };
        } else if (client) {
            b->remoteRequest = [this](const BuildMode::Op& op) -> bool {
                std::string line;
                switch (op.type) {
                    case BuildMode::Op::Type::Place:
                        if (!PrefabTextOk(op.prefab)) return false;
                        line = "P|" + op.prefab + "|" + PoseText(op.position, op.rotation); break;
                    case BuildMode::Op::Type::Remove: line = "R|" + std::to_string(op.id); break;
                    case BuildMode::Op::Type::Pose:   line = "M|" + std::to_string(op.id) + "|" + PoseText(op.position, op.rotation); break;
                    case BuildMode::Op::Type::Link:   line = "L|" + std::to_string(op.id) + "|" + std::to_string(op.other); break;
                    case BuildMode::Op::Type::Unlink: line = "U|" + std::to_string(op.id) + "|" + std::to_string(op.other); break;
                    case BuildMode::Op::Type::Undo:   line = "Z"; break;
                    default: return false;
                }
                return Send(line);
            };
        }
        m_hooked.push_back(b->gameObject ? b->gameObject->InstanceId() : 0);
    }
}

void BuildSync::Tick(Scene* scene, float dt) {
    m_time += dt;
    const bool server = m_nm.IsServer();
    const bool client = m_nm.IsClient() || m_nm.Reconnecting();
    if (!enabled || !scene || (!server && !client)) {
        if (!m_hooked.empty() || m_handlersOn || m_exec) Reset();
        return;
    }
    if (m_scene != scene) { Reset(); m_scene = scene; m_present = 0.0f; m_hasBuild = false; }
    // A server whose scene has no build mode has nothing to place with, and sends nothing. A client always
    // can show what the server built, even if its own player cannot build (the executor is made when told).
    if (server && !m_exec && (m_present -= dt) <= 0.0f) {
        m_present = 0.25f;
        m_hasBuild = !scene->FindObjectsOfType<BuildMode>().empty();
    }
    if (server && !m_exec && !m_hasBuild) return;
    if (server || m_exec) EnsureExecutor(*scene);
    if (server) {
        const bool wasOn = m_handlersOn;
        ServerOn();
        if (!wasOn) {   // it started mid-session: the clients already here are told what is built
            for (const auto& p : m_nm.Peers()) SendSnapshot((std::int64_t)p.id);
        }
        m_exec->maxObjects = maxObjects;
        if (!m_exec->onChanged) m_exec->onChanged = [this](const BuildMode::Op& op) { OnChange(op); };   // what it does for a request goes out
    } else if (m_exec) {
        m_exec->onChanged = nullptr;   // a client applies what the server sent; it echoes nothing
    }
    if ((m_scan -= dt) <= 0.0f) {
        m_scan = client ? 0.1f : 0.5f;   // a client's build mode must not run unhooked for long
        Rescan(*scene, server, client);
    }
    if (server && (m_prune -= dt) <= 0.0f) {
        m_prune = 2.0f;
        std::set<std::uint32_t> live;
        for (const auto& p : m_nm.Peers()) live.insert(p.id);
        for (auto it = m_peers.begin(); it != m_peers.end();)
            it = live.count(it->first) ? std::next(it) : m_peers.erase(it);
    }
}

void BuildSync::ServerOn() {
    if (m_handlersOn) return;
    m_handlersOn = true;
    m_nm.SetRequestHandler("__req/build", [this](std::uint32_t from, const std::string& data) { OnRequest(from, data); });
    m_joinHook = m_nm.AddJoinHook([this](std::uint32_t peer) {
        if (enabled && m_exec) SendSnapshot(peer);
    });
}

void BuildSync::ServerOff() {
    if (!m_handlersOn) return;
    m_handlersOn = false;
    m_nm.ClearRequestHandler("__req/build");
    if (m_joinHook) m_nm.RemoveJoinHook(m_joinHook);
    m_joinHook = 0;
}

// ---------------------------------------------------------------------------
// Server: requests
// ---------------------------------------------------------------------------

bool BuildSync::TakeToken(std::uint32_t peer) {
    if (m_peers.size() > 1024 && !m_peers.count(peer)) return false;
    PeerState& ps = m_peers[peer];
    const float rate = (float)std::max(1, maxRequestsPerSecond);
    if (ps.last == 0.0) ps.tokens = rate * 2.0f;
    else ps.tokens = std::min(rate * 2.0f, ps.tokens + (float)(m_time - ps.last) * rate);
    ps.last = m_time > 0.0 ? m_time : 1e-9;
    if (ps.tokens < 1.0f) return false;
    ps.tokens -= 1.0f;
    return true;
}

void BuildSync::Refuse(std::uint32_t peer, const std::string& why, bool reply) {
    ++m_refused;
    m_lastRefusal = why;
    if (reply) m_nm.SendOrderedTo(peer, "__bld", "X|" + why);
}

bool BuildSync::InBounds(const Vec3& p) const {
    return p.x >= boundsMin.x && p.x <= boundsMax.x && p.y >= boundsMin.y && p.y <= boundsMax.y &&
           p.z >= boundsMin.z && p.z <= boundsMax.z;
}

bool BuildSync::InReach(std::uint32_t peer, const Vec3& at) const {
    if (maxReach <= 0.0f) return true;
    Vec3 pos;
    if (!m_nm.PeerPosition(peer, pos)) return true;   // not known yet: the move check's business
    return Vec3::Distance(pos, at) <= maxReach;
}

int BuildSync::PlacedBy(std::uint32_t peer) const {
    int n = 0;
    for (const auto& [id, owner] : m_owner) if (owner == peer && m_exec && m_exec->FindBuilt(id)) ++n;
    return n;
}

bool BuildSync::MayEdit(std::uint32_t peer, GameObject* go, std::string& why) const {
    if (!go || !m_exec->CanEdit(go)) { why = "it can't be edited"; return false; }
    Buildable* b = Buildable::Of(go);
    if (!b || b->buildId <= 0) { why = "it can't be edited"; return false; }
    if (!b->placed) {
        if (!levelEditable) { why = "it belongs to the level"; return false; }
        return true;
    }
    if (ownerOnly) {
        auto it = m_owner.find(b->buildId);
        const std::uint32_t owner = it == m_owner.end() ? 0u : it->second;   // not known: the host's
        if (owner != peer) { why = "it isn't yours"; return false; }
    }
    return true;
}

// Every BuildMode's palette is the allowlist; the executor's own palette is rebuilt from them.
void BuildSync::SyncPalette(Scene& scene) {
    std::vector<std::pair<std::string, std::string>> items;
    std::set<std::string> seen;
    auto add = [&](const std::string& name, const std::string& prefab) {
        if (prefab.empty() || !seen.insert(PrefabKey(prefab)).second) return;
        items.emplace_back(name, prefab);
    };
    for (BuildMode* b : scene.FindObjectsOfType<BuildMode>())
        if (b && b != m_exec) for (const auto& it : b->palette) add(it.name, it.prefab);
    for (const auto& p : extraPrefabs) add(p, p);
    m_exec->ClearPalette();
    for (const auto& it : items) m_exec->AddToPalette(it.first, it.second);
}

void BuildSync::OnRequest(std::uint32_t peer, const std::string& data) {
    if (!enabled || !m_exec || !m_scene) return;
    if (!TakeToken(peer)) { Refuse(peer, "too many requests", false); return; }
    if (data.empty() || data.size() > 1024 || data.find('\n') != std::string::npos) { ++m_malformed; return; }
    const std::vector<std::string> parts = Split(data, '|');
    const std::string& op = parts[0];
    Scene& scene = *m_scene;
    std::string why;

    if (op == "P") {   // place: P|prefab|pose
        Vec3 pos; Quat rot; int slot = -1;
        if (parts.size() != 9 || !PrefabTextOk(parts[1]) || !ParsePose(parts, 2, pos, rot)) { ++m_malformed; return; }
        SyncPalette(scene);
        const std::string key = PrefabKey(parts[1]);
        for (std::size_t i = 0; i < m_exec->palette.size(); ++i)
            if (PrefabKey(m_exec->palette[i].prefab) == key) { slot = (int)i; break; }
        if (slot < 0) { Refuse(peer, "that can't be placed here"); return; }
        if (!InBounds(pos)) { Refuse(peer, "outside the build area"); return; }
        if (!InReach(peer, pos)) { Refuse(peer, "too far away"); return; }
        if (maxObjects > 0 && m_exec->PlacedCount() >= maxObjects) { Refuse(peer, "limit reached (" + std::to_string(maxObjects) + ")"); return; }
        if (maxPerPlayer > 0 && PlacedBy(peer) >= maxPerPlayer) { Refuse(peer, "you have placed the most you may (" + std::to_string(maxPerPlayer) + ")"); return; }
        if (checkBlocked && !m_exec->FitsAt(m_exec->palette[slot].prefab, pos, rot, &why)) { Refuse(peer, why == "blocked" ? "blocked" : "can't place that"); return; }
        m_actor = peer;
        GameObject* g = m_exec->PlaceItem(slot, pos, rot);
        m_actor = -1;
        if (!g) { Refuse(peer, "can't place that"); return; }
        ++m_accepted;
        return;
    }
    if (op == "R" || op == "M") {   // remove: R|id    move: M|id|pose
        int id = 0; Vec3 pos; Quat rot;
        if (parts.size() < 2 || !ParseId(parts[1], id)) { ++m_malformed; return; }
        if (op == "R" ? parts.size() != 2 : (parts.size() != 9 || !ParsePose(parts, 2, pos, rot))) { ++m_malformed; return; }
        GameObject* go = m_exec->FindBuilt(id);
        if (!go) { m_nm.SendOrderedTo(peer, "__bld", "R|" + std::to_string(id)); Refuse(peer, "it is gone", false); return; }
        if (!MayEdit(peer, go, why)) { Refuse(peer, why); SendOne(peer, go); return; }
        Buildable* b = Buildable::Of(go);
        GameObject* root = b->gameObject;
        if (!InReach(peer, root->transform->Position())) { Refuse(peer, "too far away"); SendOne(peer, root); return; }
        if (op == "M") {
            if (!InBounds(pos)) { Refuse(peer, "outside the build area"); SendOne(peer, root); return; }
            if (!InReach(peer, pos)) { Refuse(peer, "too far away"); SendOne(peer, root); return; }
            if (checkBlocked && !b->prefab.empty() && !m_exec->FitsAt(b->prefab, pos, rot, &why, root)) { Refuse(peer, "blocked"); SendOne(peer, root); return; }
            m_actor = peer;
            const bool ok = m_exec->SetPose(root, pos, rot);
            m_actor = -1;
            if (!ok) { Refuse(peer, "can't move that"); SendOne(peer, root); return; }
        } else {
            m_actor = peer;
            const bool ok = m_exec->Remove(root);
            m_actor = -1;
            if (!ok) { Refuse(peer, "can't remove that"); return; }
        }
        ++m_accepted;
        return;
    }
    if (op == "L" || op == "U") {   // link / unlink: L|src|dst
        int src = 0, dst = 0;
        if (parts.size() != 3 || !ParseId(parts[1], src) || !ParseId(parts[2], dst)) { ++m_malformed; return; }
        GameObject* a = m_exec->FindBuilt(src);
        GameObject* b = m_exec->FindBuilt(dst);
        if (!a || !b) { Refuse(peer, "it is gone"); return; }
        if (!MayEdit(peer, a, why) || !MayEdit(peer, b, why)) { Refuse(peer, why); return; }
        if (!InReach(peer, a->transform->Position()) && !InReach(peer, b->transform->Position())) { Refuse(peer, "too far away"); return; }
        m_actor = peer;
        const bool ok = op == "L" ? m_exec->Link(a, b) : m_exec->Unlink(a, b);
        m_actor = -1;
        if (!ok) { Refuse(peer, op == "L" ? "can't link those" : "they aren't linked"); return; }
        ++m_accepted;
        return;
    }
    if (op == "Z") {   // undo
        if (parts.size() != 1) { ++m_malformed; return; }
        if (!Undo(peer, why)) { Refuse(peer, why); return; }
        ++m_accepted;
        return;
    }
    ++m_malformed;   // an op this server does not know
}

void BuildSync::Record(Hist h) {
    if (m_actor < 0 || m_undoing) return;
    PeerState& ps = m_peers[(std::uint32_t)m_actor];
    ps.history.push_back(std::move(h));
    while (ps.history.size() > kMaxHistory) ps.history.pop_front();
}

// Undo one of this player's own steps: the inverse goes through the same checks as a request would.
bool BuildSync::Undo(std::uint32_t peer, std::string& why) {
    auto it = m_peers.find(peer);
    if (it == m_peers.end() || it->second.history.empty()) { why = "nothing to undo"; return false; }
    std::deque<Hist>& hist = it->second.history;
    for (int tries = 0; tries < 8 && !hist.empty(); ++tries) {
        Hist h = std::move(hist.back());
        hist.pop_back();
        std::string w;
        bool done = false;
        m_actor = peer; m_undoing = true;
        switch (h.type) {
            case Hist::Type::Place: {   // the piece goes
                GameObject* go = m_exec->FindBuilt(h.id);
                if (go && MayEdit(peer, go, w)) done = m_exec->Remove(go);
                break;
            }
            case Hist::Type::Remove: {  // the piece comes back, with its id
                if (m_exec->FindBuilt(h.id)) break;
                if (maxObjects > 0 && m_exec->PlacedCount() >= maxObjects) break;
                if (maxPerPlayer > 0 && PlacedBy(peer) >= maxPerPlayer) break;
                done = m_exec->RestoreObject(h.snapshot) != nullptr;
                break;
            }
            case Hist::Type::Pose: {
                GameObject* go = m_exec->FindBuilt(h.id);
                if (go && MayEdit(peer, go, w) && InBounds(h.prevPos)) done = m_exec->SetPose(go, h.prevPos, h.prevRot);
                break;
            }
            case Hist::Type::Link:
            case Hist::Type::Unlink: {
                GameObject* a = m_exec->FindBuilt(h.id);
                GameObject* b = m_exec->FindBuilt(h.other);
                if (a && b && MayEdit(peer, a, w) && MayEdit(peer, b, w))
                    done = h.type == Hist::Type::Link ? m_exec->Unlink(a, b) : m_exec->Link(a, b);
                break;
            }
        }
        m_actor = -1; m_undoing = false;
        if (done) return true;
    }
    why = "nothing to undo";
    return false;
}

// ---------------------------------------------------------------------------
// Server: changes go out
// ---------------------------------------------------------------------------

std::string BuildSync::PlaceLine(GameObject* go) const {
    Buildable* b = go ? go->GetComponent<Buildable>() : nullptr;
    if (!b || b->buildId <= 0 || !go->transform) return {};
    std::string line = "P|" + std::to_string(b->buildId) + "|" + std::to_string(OwnerOf(b->buildId) < 0 ? 0LL : OwnerOf(b->buildId)) +
                       "|" + b->prefab + "|" + PoseText(go->transform->Position(), go->transform->Rotation()) + "|";
    bool first = true;
    for (int l : BuildMode::LinksOf(go)) { if (!first) line += ","; line += std::to_string(l); first = false; }
    return line;
}

void BuildSync::SendOne(std::uint32_t peer, GameObject* go) {
    const std::string line = PlaceLine(go);
    if (!line.empty()) m_nm.SendOrderedTo(peer, "__bld", line);
}

void BuildSync::Broadcast(const std::string& line) {
    if (!line.empty()) m_nm.SendOrdered("__bld", line);
}

void BuildSync::OnChange(const BuildMode::Op& op) {
    if (!m_nm.IsServer()) return;
    const std::uint32_t actor = m_actor < 0 ? 0u : (std::uint32_t)m_actor;
    switch (op.type) {
        case BuildMode::Op::Type::Place: {
            GameObject* go = m_exec ? m_exec->FindBuilt(op.id) : nullptr;
            if (!go) return;
            m_owner[op.id] = actor;
            Hist h; h.type = Hist::Type::Place; h.id = op.id;
            Record(h);
            Broadcast(PlaceLine(go));
            break;
        }
        case BuildMode::Op::Type::Remove: {
            Hist h; h.type = Hist::Type::Remove; h.id = op.id; h.snapshot = op.snapshot;
            Record(h);
            auto own = m_owner.find(op.id);
            if (own == m_owner.end()) m_removedLevel.insert(op.id);   // a level object (nobody placed it)
            else m_owner.erase(own);
            Broadcast("R|" + std::to_string(op.id));
            break;
        }
        case BuildMode::Op::Type::Pose: {
            Hist h; h.type = Hist::Type::Pose; h.id = op.id; h.prevPos = op.prevPosition; h.prevRot = op.prevRotation;
            Record(h);
            Broadcast("M|" + std::to_string(op.id) + "|" + PoseText(op.position, op.rotation));
            break;
        }
        case BuildMode::Op::Type::Link:
        case BuildMode::Op::Type::Unlink: {
            const bool on = op.type == BuildMode::Op::Type::Link;
            Hist h; h.type = on ? Hist::Type::Link : Hist::Type::Unlink; h.id = op.id; h.other = op.other;
            Record(h);
            Broadcast(std::string(on ? "L|" : "U|") + std::to_string(op.id) + "|" + std::to_string(op.other));
            break;
        }
        case BuildMode::Op::Type::Reset:
            m_owner.clear();
            for (auto& [peer, ps] : m_peers) ps.history.clear();
            SendSnapshot(-1);
            break;
        default: break;
    }
}

// Everything players built, for a client that has just joined (or after a creation is loaded). Chunked
// to fit a message; the client reconciles: what the snapshot does not name is removed.
void BuildSync::SendSnapshot(std::int64_t peer) {
    if (!m_exec || !m_scene) return;
    std::vector<std::string> lines;
    lines.push_back("B");
    for (GameObject* root : BuildCreation::PlacedRoots(*m_scene)) {
        Buildable* b = root->GetComponent<Buildable>();
        if (!b || b->Removed() || b->buildId <= 0) continue;
        const std::string l = PlaceLine(root);
        if (!l.empty()) lines.push_back(l);
    }
    if (levelEditable) {
        for (const auto& up : m_scene->Objects()) {
            if (!up || up->IsDestroyed()) continue;
            Buildable* b = up->GetComponent<Buildable>();
            if (!b || b->placed || b->Removed() || !b->editable || b->buildId <= 0 || !up->transform) continue;
            lines.push_back("M|" + std::to_string(b->buildId) + "|" + PoseText(up->transform->Position(), up->transform->Rotation()));
        }
        for (int id : m_removedLevel) lines.push_back("R|" + std::to_string(id));
    }
    lines.push_back("E");
    std::vector<std::string> chunks(1);
    for (const std::string& l : lines) {
        if (!chunks.back().empty() && chunks.back().size() + l.size() + 1 > kMaxChunk) chunks.emplace_back();
        chunks.back() += l + "\n";
    }
    for (const std::string& c : chunks) {
        if (peer >= 0) m_nm.SendOrderedTo((std::uint32_t)peer, "__bld", c);
        else m_nm.SendOrdered("__bld", c);
    }
}

// ---------------------------------------------------------------------------
// Client
// ---------------------------------------------------------------------------

bool BuildSync::Send(const std::string& line) {
    if (!m_nm.IsClient() || !m_nm.Joined()) return false;
    m_nm.SendOrdered("__req/build", line);
    return true;
}

void BuildSync::OnServerMessage(const std::string& data) {
    if (!enabled || !m_nm.IsClient()) return;
    if (!m_scene) m_scene = m_nm.GetScene();
    if (m_scene && !m_exec) EnsureExecutor(*m_scene);
    if (!m_scene || !m_exec) { ++m_malformed; return; }
    if (data.empty() || data.size() > kChannelPayloadMax) { ++m_malformed; return; }
    std::size_t at = 0, lines = 0;
    while (at < data.size() && lines < kMaxLinesPerMessage) {
        std::size_t nl = data.find('\n', at);
        if (nl == std::string::npos) nl = data.size();
        if (nl > at) ApplyLine(data.substr(at, nl - at));
        at = nl + 1;
        ++lines;
    }
}

void BuildSync::ApplyLine(const std::string& line) {
    const std::vector<std::string> parts = Split(line, '|');
    const std::string& op = parts[0];
    if (op == "B") {
        m_stale.clear();
        for (GameObject* root : BuildCreation::PlacedRoots(*m_scene))
            if (Buildable* b = root->GetComponent<Buildable>()) if (!b->Removed() && b->buildId > 0) m_stale.insert(b->buildId);
        m_inSnapshot = true;
        return;
    }
    if (op == "E") {
        if (!m_inSnapshot) { ++m_malformed; return; }
        for (int id : m_stale) m_exec->ApplyRemove(id);
        m_stale.clear();
        m_inSnapshot = false;
        ++m_snapshots;
        return;
    }
    if (op == "X") {
        if (parts.size() < 2) return;
        std::string why = line.substr(2);
        if (why.size() > 120) why.resize(120);
        for (char& c : why) if ((unsigned char)c < 0x20) c = ' ';
        m_lastRefusal = why;
        ++m_refused;
        if (onRefused) onRefused(why);
        return;
    }
    if (op == "P") {   // P|id|owner|prefab|pose|links
        int id = 0; Vec3 pos; Quat rot;
        if (parts.size() != 12 || !ParseId(parts[1], id) || !PrefabTextOk(parts[3]) || !ParsePose(parts, 4, pos, rot)) { ++m_malformed; return; }
        std::vector<int> links;
        if (!parts[11].empty()) {
            for (const std::string& t : Split(parts[11], ',')) {
                int l = 0;
                if (!ParseId(t, l)) { ++m_malformed; return; }
                if (links.size() < 64) links.push_back(l);
            }
        }
        if (!m_exec->FindBuilt(id) && m_exec->PlacedCount() >= kMaxClientObjects) { ++m_malformed; return; }
        if (!m_exec->ApplyPlace(parts[3], id, pos, rot, links)) { ++m_malformed; return; }
        m_stale.erase(id);
        return;
    }
    if (op == "R") {
        int id = 0;
        if (parts.size() != 2 || !ParseId(parts[1], id)) { ++m_malformed; return; }
        m_exec->ApplyRemove(id);
        m_stale.erase(id);
        return;
    }
    if (op == "M") {
        int id = 0; Vec3 pos; Quat rot;
        if (parts.size() != 9 || !ParseId(parts[1], id) || !ParsePose(parts, 2, pos, rot)) { ++m_malformed; return; }
        m_exec->ApplyPose(id, pos, rot);
        return;
    }
    if (op == "L" || op == "U") {
        int a = 0, b = 0;
        if (parts.size() != 3 || !ParseId(parts[1], a) || !ParseId(parts[2], b)) { ++m_malformed; return; }
        m_exec->ApplyLink(a, b, op == "L");
        return;
    }
    ++m_malformed;
}

} // namespace okay::net

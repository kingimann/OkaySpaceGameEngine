#include "okay/Net/NetQuestSync.hpp"
#include "okay/Net/NetworkManager.hpp"
#include "okay/Net/NetChannels.hpp"
#include "okay/Scene/Scene.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace okay::net {

namespace {

constexpr std::size_t kMaxChunk = 24 * 1024;
constexpr std::size_t kMaxLinesPerMessage = 4096;
constexpr std::size_t kMaxName = 64;
constexpr std::size_t kMaxText = 256;
constexpr std::size_t kMaxObjectives = 64;
constexpr int kMaxProgress = 1000000;

bool NameOk(const std::string& s) {
    if (s.empty() || s.size() > kMaxName) return false;
    for (unsigned char c : s) if (c < 0x20 || c == 0x7f || c == '|') return false;
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

std::string Esc(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '%') o += "%25"; else if (c == '|') o += "%7C"; else if (c == '\n') o += "%0A"; else if (c == '\r') o += "%0D";
        else o += c;
    }
    return o;
}

bool Unesc(const std::string& s, std::string& out) {
    out.clear();
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '%') { out += s[i]; continue; }
        if (i + 2 >= s.size()) return false;
        const std::string h = s.substr(i + 1, 2);
        if (h == "25") out += '%'; else if (h == "7C") out += '|'; else if (h == "0A") out += '\n'; else if (h == "0D") out += '\r';
        else return false;
        i += 2;
    }
    return true;
}

// A value on the wire: "n<number>", "b0" / "b1", "s<escaped text>". Other types do not travel.
bool EncodeValue(const vs::VsValue& v, std::string& out) {
    if (v.IsNumber()) {
        const float f = v.AsFloat();
        if (!std::isfinite(f)) return false;
        char b[40]; std::snprintf(b, sizeof b, "n%.9g", f);
        out = b; return true;
    }
    if (v.IsBool()) { out = v.AsBool() ? "b1" : "b0"; return true; }
    if (v.IsString()) {
        const std::string t = v.AsString();
        if (t.size() > kMaxText) return false;
        out = "s" + Esc(t); return true;
    }
    return false;
}

bool DecodeValue(const std::string& s, vs::VsValue& out) {
    if (s.empty()) return false;
    if (s[0] == 'n') {
        if (s.size() < 2 || s.size() > 30) return false;
        char* end = nullptr;
        const float f = std::strtof(s.c_str() + 1, &end);
        if (end != s.c_str() + s.size() || !std::isfinite(f)) return false;
        out = vs::VsValue(f); return true;
    }
    if (s[0] == 'b') {
        if (s == "b0") { out = vs::VsValue(false); return true; }
        if (s == "b1") { out = vs::VsValue(true); return true; }
        return false;
    }
    if (s[0] == 's') {
        std::string t;
        if (s.size() > kMaxText * 3 + 1 || !Unesc(s.substr(1), t) || t.size() > kMaxText) return false;
        out = vs::VsValue(t); return true;
    }
    return false;
}

std::string QuestLine(const QuestRecord& r) {
    std::string l = "Q|" + r.id + "|" + std::to_string((int)r.status) + "|" + std::to_string(r.completions) + "|";
    for (std::size_t i = 0; i < r.progress.size() && i < kMaxObjectives; ++i) { if (i) l += ","; l += std::to_string(r.progress[i]); }
    l += "|";
    for (std::size_t i = 0; i < r.done.size() && i < kMaxObjectives; ++i) { if (i) l += ","; l += std::to_string(r.done[i]); }
    return l;
}

bool ParseInt(const std::string& s, int lo, int hi, int& out) {
    if (s.empty() || s.size() > 10) return false;
    std::size_t i = (s[0] == '-') ? 1 : 0;
    if (i == s.size()) return false;
    for (std::size_t k = i; k < s.size(); ++k) if (s[k] < '0' || s[k] > '9') return false;
    const long long v = std::strtoll(s.c_str(), nullptr, 10);
    if (v < lo || v > hi) return false;
    out = (int)v;
    return true;
}

bool ParseIntList(const std::string& s, int lo, int hi, std::vector<int>& out) {
    out.clear();
    if (s.empty()) return true;
    const auto parts = Split(s, ',');
    if (parts.size() > kMaxObjectives) return false;
    for (const auto& p : parts) { int v = 0; if (!ParseInt(p, lo, hi, v)) return false; out.push_back(v); }
    return true;
}

bool HasPrefix(const std::vector<std::string>& list, const std::string& name) {
    for (const auto& p : list) if (!p.empty() && name.compare(0, p.size(), p) == 0) return true;
    return false;
}

} // namespace

QuestSync::QuestSync(NetworkManager& nm) : m_nm(nm) {}
QuestSync::~QuestSync() { ServerOff(); }

bool QuestSync::QuestIsPerPlayer(const std::string& id) const {
    return std::find(perPlayerQuests.begin(), perPlayerQuests.end(), id) != perPlayerQuests.end();
}
bool QuestSync::VarIsPerPlayer(const std::string& name) const { return HasPrefix(perPlayerVars, name); }
bool QuestSync::MayWriteShared(const std::string& name) const { return HasPrefix(clientWritableVars, name); }

void QuestSync::Reset() {
    UnhookClient();
    ServerOff();
    m_shared.clear();
    m_peers.clear();
    m_staleQ.clear(); m_staleV.clear(); m_staleF.clear();
    m_inSnapshot = false;
    m_synced = false;
    m_scene = nullptr;
}

void QuestSync::Tick(Scene* scene, float dt) {
    m_time += dt;
    const bool server = m_nm.IsServer();
    const bool client = m_nm.IsClient() || m_nm.Reconnecting();
    if (!enabled || !scene || (!server && !client)) {
        if (m_handlersOn || m_hookedLog || m_hookedStory || !m_shared.empty()) Reset();
        return;
    }
    if (m_scene != scene) { Reset(); m_scene = scene; m_present = 0.0f; m_have = false; }
    if (server) {
        // A scene with no quest log and no story state has nothing to sync, and sends nothing.
        if ((m_present -= dt) <= 0.0f) {
            m_present = 0.25f;
            m_have = QuestLog::Get(scene, false) || StoryState::Get(scene, false);
        }
        if (!m_have) { if (m_handlersOn) { ServerOff(); m_shared.clear(); } return; }
        const bool wasOn = m_handlersOn;
        ServerOn();
        if (!wasOn) {   // it started mid-session: the clients already here are told the state
            m_shared = SharedNow();   // the snapshot below is the news; the next diff starts from it
            for (const auto& p : m_nm.Peers()) SendSnapshot(p.id);
        }
        for (const auto& p : m_nm.Peers()) if (auto it = m_peers.find(p.id); it != m_peers.end()) it->second.seen = m_time;
        if ((m_acc += dt) >= std::max(0.02f, syncInterval)) { m_acc = 0.0f; Flush(); }
        if ((m_scan -= dt) <= 0.0f) {   // a player that left and is not coming back
            m_scan = 5.0f;
            for (auto it = m_peers.begin(); it != m_peers.end();)
                it = (!PeerConnected(it->first) && m_time - it->second.seen > keepSeconds) ? m_peers.erase(it) : std::next(it);
            while (m_peers.size() > 256) {   // the stalest go first
                auto old = m_peers.begin();
                for (auto it = m_peers.begin(); it != m_peers.end(); ++it) if (it->second.seen < old->second.seen) old = it;
                m_peers.erase(old);
            }
        }
    } else if ((m_scan -= dt) <= 0.0f) {
        m_scan = 0.5f;
        HookClient(*scene);
    }
}

bool QuestSync::PeerConnected(std::uint32_t peer) const {
    if (peer == 0) return true;
    for (const auto& p : m_nm.Peers()) if (p.id == peer) return true;
    return false;
}

QuestSync::PeerState* QuestSync::Peer(std::uint32_t peer, bool create) {
    auto it = m_peers.find(peer);
    if (it != m_peers.end()) return &it->second;
    if (!create || peer == 0 || m_peers.size() >= 256) return nullptr;
    PeerState& ps = m_peers[peer];
    ps.seen = m_time;
    return &ps;
}

void QuestSync::ServerOn() {
    if (m_handlersOn) return;
    m_handlersOn = true;
    m_nm.SetRequestHandler("__req/quest", [this](std::uint32_t from, const std::string& data) { OnRequest(from, data); });
    m_joinHook = m_nm.AddJoinHook([this](std::uint32_t peer) {
        if (!enabled || !m_scene) return;
        Peer(peer, true);
        SendSnapshot(peer);
    });
}

void QuestSync::ServerOff() {
    if (!m_handlersOn) return;
    m_handlersOn = false;
    m_nm.ClearRequestHandler("__req/quest");
    if (m_joinHook) m_nm.RemoveJoinHook(m_joinHook);
    m_joinHook = 0;
}

// ---------------------------------------------------------------------------
// Server: what is true now, and telling the clients
// ---------------------------------------------------------------------------

QuestSync::Lines QuestSync::SharedNow() const {
    Lines out;
    if (!m_scene) return out;
    if (QuestLog* ql = QuestLog::Get(m_scene, false))
        for (const QuestRecord& r : ql->records)
            if (NameOk(r.id) && !QuestIsPerPlayer(r.id)) out["q:" + r.id] = QuestLine(r);
    if (StoryState* st = StoryState::Get(m_scene, false)) {
        for (const auto& [name, v] : st->vars) {
            std::string enc;
            if (!NameOk(name) || VarIsPerPlayer(name) || !EncodeValue(v, enc)) continue;
            if (out.size() >= (std::size_t)maxSharedVars * 2 + 4096) break;
            out["v:" + name] = "V|" + name + "|" + enc;
        }
        for (const auto& name : st->flags)
            if (NameOk(name) && !VarIsPerPlayer(name)) out["f:" + name] = "F|" + name;
    }
    return out;
}

QuestSync::Lines QuestSync::PlayerNow(const PeerState& ps) const {
    Lines out;
    for (const QuestRecord& r : ps.records) if (NameOk(r.id)) out["q:" + r.id] = QuestLine(r);
    for (const auto& [name, v] : ps.vars) {
        std::string enc;
        if (NameOk(name) && EncodeValue(v, enc)) out["v:" + name] = "V|" + name + "|" + enc;
    }
    for (const auto& name : ps.flags) if (NameOk(name)) out["f:" + name] = "F|" + name;
    return out;
}

void QuestSync::SendDiff(const Lines& now, Lines& was, std::int64_t only) {
    std::vector<std::string> lines;
    for (const auto& [key, line] : now) {
        auto it = was.find(key);
        if (it == was.end() || it->second != line) lines.push_back(line);
    }
    for (const auto& [key, line] : was) {
        if (now.count(key)) continue;
        const std::string name = key.substr(2);
        lines.push_back(std::string(1, key[0]) + "|" + name);   // "q|id", "v|name", "f|name": gone
    }
    was = now;
    if (lines.empty()) return;
    std::string chunk;
    auto flush = [&] {
        if (chunk.empty()) return;
        if (only >= 0) m_nm.SendOrderedTo((std::uint32_t)only, "__qst", chunk);
        else m_nm.SendOrdered("__qst", chunk);
        chunk.clear();
    };
    for (const std::string& l : lines) {
        if (!chunk.empty() && chunk.size() + l.size() + 1 > kMaxChunk) flush();
        chunk += l + "\n";
    }
    flush();
}

void QuestSync::Flush() {
    if (!m_scene) return;
    SendDiff(SharedNow(), m_shared, -1);
    for (auto& [peer, ps] : m_peers) {
        if (!PeerConnected(peer)) continue;
        SendDiff(PlayerNow(ps), ps.sent, (std::int64_t)peer);
    }
}

void QuestSync::SendSnapshot(std::uint32_t peer) {
    PeerState* ps = Peer(peer, true);
    if (!ps) return;
    // The diffs are taken against what everybody has been told (m_shared), so the joiner's snapshot must be
    // that same state: bring everyone up to now first, then send the joiner what everyone has now.
    Flush();
    const Lines shared = m_shared;
    const Lines own = PlayerNow(*ps);
    std::vector<std::string> lines{"B"};
    for (const auto& kv : shared) lines.push_back(kv.second);
    for (const auto& kv : own) lines.push_back(kv.second);
    lines.push_back("E");
    ps->sent = own;
    std::string chunk;
    for (const std::string& l : lines) {
        if (!chunk.empty() && chunk.size() + l.size() + 1 > kMaxChunk) { m_nm.SendOrderedTo(peer, "__qst", chunk); chunk.clear(); }
        chunk += l + "\n";
    }
    if (!chunk.empty()) m_nm.SendOrderedTo(peer, "__qst", chunk);
}

// ---------------------------------------------------------------------------
// Server: per-player quests and variables
// ---------------------------------------------------------------------------

bool QuestSync::RunQuestOp(std::uint32_t peer, const std::string& id, const std::function<bool(QuestLog&)>& op) {
    if (!m_scene || !m_nm.IsServer() || !QuestIsPerPlayer(id)) return false;
    QuestLog* ql = QuestLog::Get(m_scene, false);
    if (!ql) return false;
    const QuestStatus before = peer == 0 ? ql->Status(id) : [&] {
        const PeerState* p = Peer(peer, false);
        if (p) for (const auto& r : p->records) if (r.id == id) return r.status;
        return QuestStatus::Inactive;
    }();
    bool ok = false;
    QuestStatus after = before;
    if (peer == 0) {
        ok = op(*ql);
        after = ql->Status(id);
    } else {
        PeerState* ps = Peer(peer, false);
        if (!ps || !PeerConnected(peer)) return false;
        // The shared quests ride along, so a prerequisite that is shared is seen; only this player's own come back.
        std::vector<QuestRecord> work = ps->records;
        for (const QuestRecord& r : ql->records) if (!QuestIsPerPlayer(r.id)) work.push_back(r);
        ql->RunOn(work, [&] { ok = op(*ql); });
        ps->records.clear();
        for (const QuestRecord& r : work) if (QuestIsPerPlayer(r.id)) ps->records.push_back(r);
        for (const auto& r : ps->records) if (r.id == id) after = r.status;
    }
    if (after != before && onPlayerQuest) onPlayerQuest(peer, id, after);
    return ok;
}

bool QuestSync::StartQuest(std::uint32_t peer, const std::string& id) { return RunQuestOp(peer, id, [&](QuestLog& q) { return q.StartQuest(id); }); }
bool QuestSync::CompleteQuest(std::uint32_t peer, const std::string& id) { return RunQuestOp(peer, id, [&](QuestLog& q) { return q.CompleteQuest(id); }); }
bool QuestSync::FailQuest(std::uint32_t peer, const std::string& id) { return RunQuestOp(peer, id, [&](QuestLog& q) { return q.FailQuest(id); }); }
bool QuestSync::ResetQuest(std::uint32_t peer, const std::string& id) {
    return RunQuestOp(peer, id, [&](QuestLog& q) { q.ResetQuest(id); return true; });
}
bool QuestSync::CompleteObjective(std::uint32_t peer, const std::string& id, int index) {
    return RunQuestOp(peer, id, [&](QuestLog& q) { return q.CompleteObjective(id, index); });
}
bool QuestSync::AddProgress(std::uint32_t peer, const std::string& id, int index, int amount) {
    return RunQuestOp(peer, id, [&](QuestLog& q) { return q.AddProgress(id, index, amount); });
}

QuestStatus QuestSync::Status(std::uint32_t peer, const std::string& id) const {
    if (!m_scene) return QuestStatus::Inactive;
    if (peer == 0 || !QuestIsPerPlayer(id)) {
        QuestLog* ql = QuestLog::Get(m_scene, false);
        return ql ? ql->Status(id) : QuestStatus::Inactive;
    }
    auto it = m_peers.find(peer);
    if (it != m_peers.end()) for (const auto& r : it->second.records) if (r.id == id) return r.status;
    return QuestStatus::Inactive;
}

int QuestSync::Progress(std::uint32_t peer, const std::string& id, int index) const {
    if (!m_scene) return 0;
    if (peer == 0 || !QuestIsPerPlayer(id)) {
        QuestLog* ql = QuestLog::Get(m_scene, false);
        return ql ? ql->Progress(id, index) : 0;
    }
    auto it = m_peers.find(peer);
    if (it != m_peers.end())
        for (const auto& r : it->second.records)
            if (r.id == id) return index >= 0 && index < (int)r.progress.size() ? r.progress[index] : 0;
    return 0;
}

bool QuestSync::SetPlayerVar(std::uint32_t peer, const std::string& name, const vs::VsValue& v) {
    if (!m_scene || !m_nm.IsServer() || !NameOk(name) || !VarIsPerPlayer(name)) return false;
    std::string enc;
    if (!EncodeValue(v, enc)) return false;
    if (peer == 0) { if (StoryState* st = StoryState::Get(m_scene, true)) { st->SetRaw(name, v); return true; } return false; }
    PeerState* ps = Peer(peer, false);
    if (!ps || !PeerConnected(peer)) return false;
    if (!ps->vars.count(name) && (int)(ps->vars.size() + ps->flags.size()) >= maxPlayerVars) return false;
    ps->vars[name] = v;
    return true;
}

vs::VsValue QuestSync::PlayerVar(std::uint32_t peer, const std::string& name) const {
    if (!m_scene) return {};
    if (peer == 0) { StoryState* st = StoryState::Get(m_scene, false); return st ? st->Get(name) : vs::VsValue{}; }
    auto it = m_peers.find(peer);
    if (it == m_peers.end()) return {};
    auto v = it->second.vars.find(name);
    return v == it->second.vars.end() ? vs::VsValue{} : v->second;
}

bool QuestSync::SetPlayerFlag(std::uint32_t peer, const std::string& name, bool on) {
    if (!m_scene || !m_nm.IsServer() || !NameOk(name) || !VarIsPerPlayer(name)) return false;
    if (peer == 0) { if (StoryState* st = StoryState::Get(m_scene, true)) { st->SetFlagRaw(name, on); return true; } return false; }
    PeerState* ps = Peer(peer, false);
    if (!ps || !PeerConnected(peer)) return false;
    if (on && !ps->flags.count(name) && (int)(ps->vars.size() + ps->flags.size()) >= maxPlayerVars) return false;
    if (on) ps->flags.insert(name); else ps->flags.erase(name);
    return true;
}

bool QuestSync::PlayerFlag(std::uint32_t peer, const std::string& name) const {
    if (!m_scene) return false;
    if (peer == 0) { StoryState* st = StoryState::Get(m_scene, false); return st && st->HasFlag(name); }
    auto it = m_peers.find(peer);
    return it != m_peers.end() && it->second.flags.count(name) != 0;
}

// ---------------------------------------------------------------------------
// Server: a client's request to change a variable or flag
// ---------------------------------------------------------------------------

bool QuestSync::TakeToken(PeerState& ps) {
    const float rate = (float)std::max(1, maxRequestsPerSecond);
    if (ps.last == 0.0) ps.tokens = rate * 2.0f;
    else ps.tokens = std::min(rate * 2.0f, ps.tokens + (float)(m_time - ps.last) * rate);
    ps.last = m_time > 0.0 ? m_time : 1e-9;
    if (ps.tokens < 1.0f) return false;
    ps.tokens -= 1.0f;
    return true;
}

void QuestSync::OnRequest(std::uint32_t peer, const std::string& data) {
    if (!enabled || !m_scene) return;
    PeerState* ps = Peer(peer, true);
    if (!ps) { Refuse("too many players"); return; }
    ps->seen = m_time;
    if (!TakeToken(*ps)) { Refuse("too many requests"); return; }
    if (data.empty() || data.size() > 1024 || data.find('\n') != std::string::npos) { ++m_malformed; return; }
    const std::vector<std::string> parts = Split(data, '|');
    const std::string& op = parts[0];
    const bool isVar = op == "V" || op == "v";
    const bool isFlag = op == "F" || op == "f";
    if (!isVar && !isFlag) { ++m_malformed; return; }
    if (parts.size() != (op == "V" ? 3u : 2u) || !NameOk(parts[1])) { ++m_malformed; return; }
    const std::string& name = parts[1];
    vs::VsValue val;
    if (op == "V" && !DecodeValue(parts[2], val)) { ++m_malformed; return; }
    auto refuse = [&](const std::string& why) {
        Refuse(why);
        m_nm.SendOrderedTo(peer, "__qst", "X|" + why);
    };
    if (VarIsPerPlayer(name)) {   // its own copy: always allowed, within its cap
        if (op == "V") {
            if (!ps->vars.count(name) && (int)(ps->vars.size() + ps->flags.size()) >= maxPlayerVars) { refuse("too many variables"); return; }
            ps->vars[name] = val;
        } else if (op == "v") ps->vars.erase(name);
        else if (op == "F") {
            if (!ps->flags.count(name) && (int)(ps->vars.size() + ps->flags.size()) >= maxPlayerVars) { refuse("too many variables"); return; }
            ps->flags.insert(name);
        } else ps->flags.erase(name);
        ++m_applied;
        return;
    }
    if (!MayWriteShared(name)) { refuse("the server owns that variable"); return; }
    StoryState* st = StoryState::Get(m_scene, true);
    if (!st) return;
    if ((op == "V" && !st->Has(name) && (int)st->vars.size() >= maxSharedVars) ||
        (op == "F" && !st->HasFlag(name) && (int)st->flags.size() >= maxSharedVars)) { refuse("too many variables"); return; }
    if (op == "V") st->SetRaw(name, val); else if (op == "v") st->EraseRaw(name);
    else st->SetFlagRaw(name, op == "F");
    ++m_applied;
}

// ---------------------------------------------------------------------------
// Client
// ---------------------------------------------------------------------------

void QuestSync::HookClient(Scene& scene) {
    if (!m_synced) return;   // a server that never sent a snapshot does not run this: keep the log local
    if (QuestLog* ql = QuestLog::Get(&scene, false)) {
        ql->remoteAuthority = true;
        m_hookedLog = ql->gameObject ? ql->gameObject->InstanceId() : 0;
    }
    if (StoryState* st = StoryState::Get(&scene, false)) {
        const std::uint64_t id = st->gameObject ? st->gameObject->InstanceId() : 0;
        if (!st->interceptWrite || id != m_hookedStory) {
            st->interceptWrite = [this](const StoryState::Change& c) { return ClientWrite(c); };
            m_hookedStory = id;
        }
    }
}

void QuestSync::UnhookClient() {
    if (Scene* sc = m_nm.GetScene()) {
        if (QuestLog* ql = QuestLog::Get(sc, false)) ql->remoteAuthority = false;
        if (StoryState* st = StoryState::Get(sc, false)) st->interceptWrite = nullptr;
    }
    m_hookedLog = m_hookedStory = 0;
}

// A write the client's story state was asked to make: the server decides, so it is sent, not applied.
bool QuestSync::ClientWrite(const StoryState::Change& c) {
    if (!enabled || !m_nm.IsClient()) return false;
    std::string line;
    switch (c.kind) {
        case StoryState::Change::Kind::Set: {
            std::string enc;
            if (!EncodeValue(c.value, enc)) return false;   // a type that does not travel stays local
            if (!NameOk(c.name)) return true;
            line = "V|" + c.name + "|" + enc;
            break;
        }
        case StoryState::Change::Kind::Erase:   if (!NameOk(c.name)) return true; line = "v|" + c.name; break;
        case StoryState::Change::Kind::FlagOn:  if (!NameOk(c.name)) return true; line = "F|" + c.name; break;
        case StoryState::Change::Kind::FlagOff: if (!NameOk(c.name)) return true; line = "f|" + c.name; break;
    }
    if (m_nm.Joined()) m_nm.SendOrdered("__req/quest", line);
    return true;
}

void QuestSync::OnServerMessage(const std::string& data) {
    if (!enabled || !m_nm.IsClient()) return;
    if (!m_scene) m_scene = m_nm.GetScene();
    if (!m_scene || data.empty() || data.size() > kChannelPayloadMax) { ++m_malformed; return; }
    std::size_t at = 0, lines = 0;
    while (at < data.size() && lines < kMaxLinesPerMessage) {
        std::size_t nl = data.find('\n', at);
        if (nl == std::string::npos) nl = data.size();
        if (nl > at) ApplyLine(data.substr(at, nl - at));
        at = nl + 1;
        ++lines;
    }
}

void QuestSync::ApplyLine(const std::string& line) {
    const std::vector<std::string> parts = Split(line, '|');
    const std::string& op = parts[0];
    auto log = [&]() { return QuestLog::Get(m_scene, true); };
    auto story = [&]() { StoryState* s = StoryState::Get(m_scene, true); if (s) HookClient(*m_scene); return s; };
    if (op == "B") {
        m_staleQ.clear(); m_staleV.clear(); m_staleF.clear();
        if (QuestLog* q = QuestLog::Get(m_scene, false)) for (const auto& r : q->records) m_staleQ.insert(r.id);
        if (StoryState* s = StoryState::Get(m_scene, false)) {
            for (const auto& kv : s->vars) m_staleV.insert(kv.first);
            for (const auto& f : s->flags) m_staleF.insert(f);
        }
        m_inSnapshot = true;
        return;
    }
    if (op == "E") {
        if (!m_inSnapshot) { ++m_malformed; return; }
        if (QuestLog* q = QuestLog::Get(m_scene, false)) for (const auto& id : m_staleQ) q->RemoveRemoteRecord(id);
        if (StoryState* s = StoryState::Get(m_scene, false)) {
            for (const auto& n : m_staleV) s->EraseRaw(n);
            for (const auto& n : m_staleF) s->SetFlagRaw(n, false);
        }
        m_staleQ.clear(); m_staleV.clear(); m_staleF.clear();
        m_inSnapshot = false;
        ++m_snapshots;
        m_synced = true;
        HookClient(*m_scene);
        return;
    }
    if (op == "X") { if (parts.size() >= 2) { m_lastRefusal = line.substr(2, 120); ++m_refused; } return; }
    if (op == "Q") {   // Q|id|status|completions|progress|done
        int status = 0, completions = 0;
        if (parts.size() != 6 || !NameOk(parts[1]) || !ParseInt(parts[2], 0, 3, status) || !ParseInt(parts[3], 0, kMaxProgress, completions)) { ++m_malformed; return; }
        QuestRecord r;
        r.id = parts[1]; r.status = (QuestStatus)status; r.completions = completions;
        if (!ParseIntList(parts[4], 0, kMaxProgress, r.progress) || !ParseIntList(parts[5], 0, 1, r.done)) { ++m_malformed; return; }
        QuestLog* q = log();
        if (!q) return;
        q->ApplyRemoteRecord(r, !m_inSnapshot);
        m_staleQ.erase(r.id);
        return;
    }
    if (op == "q") {
        if (parts.size() != 2 || !NameOk(parts[1])) { ++m_malformed; return; }
        if (QuestLog* q = QuestLog::Get(m_scene, false)) q->RemoveRemoteRecord(parts[1]);
        m_staleQ.erase(parts[1]);
        return;
    }
    if (op == "V") {
        vs::VsValue v;
        if (parts.size() != 3 || !NameOk(parts[1]) || !DecodeValue(parts[2], v)) { ++m_malformed; return; }
        if (StoryState* s = story()) s->SetRaw(parts[1], v);
        m_staleV.erase(parts[1]);
        return;
    }
    if (op == "v") {
        if (parts.size() != 2 || !NameOk(parts[1])) { ++m_malformed; return; }
        if (StoryState* s = StoryState::Get(m_scene, false)) s->EraseRaw(parts[1]);
        m_staleV.erase(parts[1]);
        return;
    }
    if (op == "F" || op == "f") {
        if (parts.size() != 2 || !NameOk(parts[1])) { ++m_malformed; return; }
        if (op == "F") { if (StoryState* s = story()) s->SetFlagRaw(parts[1], true); }
        else if (StoryState* s = StoryState::Get(m_scene, false)) s->SetFlagRaw(parts[1], false);
        m_staleF.erase(parts[1]);
        return;
    }
    ++m_malformed;
}

} // namespace okay::net

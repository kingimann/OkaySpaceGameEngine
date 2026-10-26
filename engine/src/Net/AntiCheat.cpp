#include "okay/Net/AntiCheat.hpp"
#include "okay/Core/Log.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <iterator>
#include <fstream>
#include <sstream>

namespace okay {

const char* CheatKindName(CheatKind k) {
    switch (k) {
        case CheatKind::ForgedTransform: return "forged transform";
        case CheatKind::ForgedDespawn:   return "forged despawn";
        case CheatKind::ForgedSpawnId:   return "forged spawn id";
        case CheatKind::SpawnNotAllowed: return "spawn not allowed";
        case CheatKind::ForgedSyncVar:   return "forged sync var";
        case CheatKind::SyncVarFlood:    return "sync var flood";
        case CheatKind::SpeedHack:       return "speed hack";
        case CheatKind::RpcRejected:     return "rpc rejected";
    }
    return "cheat";
}

namespace {
std::string Trim(const std::string& s) {
    const std::size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}
bool ParseBool(const std::string& v, bool& out) {
    if (v == "true" || v == "1" || v == "on" || v == "yes") { out = true; return true; }
    if (v == "false" || v == "0" || v == "off" || v == "no") { out = false; return true; }
    return false;
}
bool ParseFloat(const std::string& v, float& out) {   // the whole value: "5abc" is not 5
    if (v.empty()) return false;
    char* end = nullptr;
    const float f = std::strtof(v.c_str(), &end);
    if (end != v.c_str() + v.size() || !std::isfinite(f) || f < 0.0f) return false;
    out = f; return true;
}
bool ParseInt(const std::string& v, int& out) {
    int i = 0; auto r = std::from_chars(v.data(), v.data() + v.size(), i);
    if (r.ec != std::errc() || r.ptr != v.data() + v.size() || i < 0) return false;
    out = i; return true;
}
// "a\\b" and "a/b" name the same prefab.
std::string NormPath(std::string p) {
    std::replace(p.begin(), p.end(), '\\', '/');
    while (p.rfind("./", 0) == 0) p.erase(0, 2);
    return p;
}
// A SpawnOwned id "sp/<peer>/<n>": the peer that minted it (-1 if not that form).
long long SpawnIdPeer(const std::string& id) {
    if (id.rfind("sp/", 0) != 0) return -1;
    const std::size_t slash = id.find('/', 3);
    if (slash == std::string::npos) return -1;
    unsigned long long p = 0;
    auto r = std::from_chars(id.data() + 3, id.data() + slash, p);
    if (r.ec != std::errc() || r.ptr != id.data() + slash || p > 0xFFFFFFFFull) return -1;
    return (long long)p;
}
} // namespace

SpawnMessage ParseSpawnMessage(const std::string& data) {
    // Kept byte-for-byte with what Deliver always did: path up to the first '|',
    // three numbers with any one-char separators, then an optional "|<netId>"
    // read to the end of the line.
    SpawnMessage m;
    std::stringstream ss(data);
    char sep;
    std::getline(ss, m.path, '|');
    ss >> m.pos.x >> sep >> m.pos.y >> sep >> m.pos.z;
    char bar;
    if (ss.get(bar) && bar == '|') std::getline(ss, m.netId);
    return m;
}

bool AntiCheatSettings::Parse(const std::string& text, AntiCheatSettings& out, std::string* error) {
    AntiCheatSettings s;
    std::istringstream in(text);
    std::string line;
    int lineNo = 0;
    auto fail = [&](const std::string& m) { if (error) *error = "line " + std::to_string(lineNo) + ": " + m; return false; };
    while (std::getline(in, line)) {
        ++lineNo;
        if (const std::size_t h = line.find('#'); h != std::string::npos) line.erase(h);
        line = Trim(line);
        if (line.empty()) continue;
        const std::size_t sp = line.find_first_of(" \t");
        const std::string key = line.substr(0, sp), val = sp == std::string::npos ? std::string() : Trim(line.substr(sp));
        if (key == "enabled") { if (!ParseBool(val, s.enabled)) return fail("enabled needs true or false"); }
        else if (key == "spawn") { if (val.empty()) return fail("spawn needs a prefab path"); s.spawnAllowlist.push_back(NormPath(val)); }
        else if (key == "maxSyncSpeed") { if (!ParseFloat(val, s.maxSyncSpeed)) return fail("maxSyncSpeed needs a number >= 0"); }
        else if (key == "maxSyncVarsPerSecond") { if (!ParseInt(val, s.maxSyncVarsPerSecond)) return fail("maxSyncVarsPerSecond needs a whole number >= 0"); }
        else if (key == "kickAfterViolations") { if (!ParseInt(val, s.kickAfterViolations)) return fail("kickAfterViolations needs a whole number >= 0"); }
        else if (key == "ownerOnlySyncVars") { if (!ParseBool(val, s.ownerOnlySyncVars)) return fail("ownerOnlySyncVars needs true or false"); }
        else return fail("unknown setting '" + key + "'");
    }
    out = std::move(s);
    return true;
}

bool AntiCheatSettings::LoadFile(const std::string& path, std::string* error) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { if (error) *error = "can't open " + path; return false; }
    std::stringstream ss; ss << f.rdbuf();
    return Parse(ss.str(), *this, error);
}

std::string AntiCheatSettings::ToText() const {
    std::ostringstream o;
    o << "# OkaySpace anti-cheat settings (.okayac)\n";
    o << "enabled " << (enabled ? "true" : "false") << "\n";
    for (const auto& p : spawnAllowlist) o << "spawn " << p << "\n";
    o << "maxSyncSpeed " << maxSyncSpeed << "\n";
    o << "maxSyncVarsPerSecond " << maxSyncVarsPerSecond << "\n";
    o << "kickAfterViolations " << kickAfterViolations << "\n";
    o << "ownerOnlySyncVars " << (ownerOnlySyncVars ? "true" : "false") << "\n";
    return o.str();
}

std::uint32_t AntiCheat::OwnerOf(const std::string& netId) const {
    auto it = m_owner.find(netId);
    return it != m_owner.end() ? it->second : 0;
}

int AntiCheat::Violations(std::uint32_t peer) const {
    auto it = m_peers.find(peer);
    return it != m_peers.end() ? it->second.violations : 0;
}

void AntiCheat::Tick(float dt) {
    m_clock += dt;
    for (auto& [id, p] : m_peers) {
        if ((p.syncVarTimer += dt) >= 1.0f) { p.syncVarTimer = 0.0f; p.syncVars = 0; }
        if ((p.violationTimer += dt) >= 60.0f) { p.violationTimer = 0.0f; p.violations = 0; }
    }
}

void AntiCheat::ForgetPeer(std::uint32_t peer) {
    m_peers.erase(peer);
    for (auto it = m_owner.begin(); it != m_owner.end();)
        it = it->second == peer ? m_owner.erase(it) : std::next(it);
    for (auto it = m_track.begin(); it != m_track.end();)
        it = it->second.peer == peer ? m_track.erase(it) : std::next(it);
}

void AntiCheat::Reset() { m_peers.clear(); m_owner.clear(); m_track.clear(); m_total = 0; m_clock = 0.0; }

bool AntiCheat::Refuse(std::uint32_t peer, CheatKind k, const std::string& detail) {
    const int n = ++m_peers[peer].violations;
    ++m_total;
    // A flood of refusals mustn't flood the log: a few per peer per minute.
    if (n <= kLoggedPerMinute)
        OKAY_WARN("anti-cheat: refused ", CheatKindName(k), " from peer ", peer, detail.empty() ? "" : (" (" + detail + ")"));
    else if (n == kLoggedPerMinute + 1)
        OKAY_WARN("anti-cheat: more refusals from peer ", peer, " this minute (counted, not logged)");
    if (onCheat) onCheat(CheatReport{peer, k, detail});
    return false;
}

bool AntiCheat::MayDrive(std::uint32_t peer, const std::string& netId, bool claim) {
    if (serverOwns && serverOwns(netId)) return false;           // the server drives it
    const long long minted = SpawnIdPeer(netId);
    if (minted >= 0) return (std::uint32_t)minted == peer;      // SpawnOwned: the id names its owner
    auto it = m_owner.find(netId);
    if (it != m_owner.end()) return it->second == peer;
    // The first peer to drive an object that exists here (and nobody owns) owns
    // it; an id the server doesn't have can't be claimed ahead of time.
    if (claim && (!exists || exists(netId))) { m_owner[netId] = peer; return true; }
    return false;
}

bool AntiCheat::SpawnAllowed(const std::string& path) const {
    const std::string p = NormPath(path);
    for (const auto& a : settings.spawnAllowlist) if (a == p) return true;
    return false;
}

bool AntiCheat::ValidateMessage(std::uint32_t peer, const std::string& channel, const std::string& data) {
    if (!settings.enabled || peer == 0) return true;   // off, or the host itself
    if (channel == "__spawn") {
        // Checked as parsed by the receiver (ParseSpawnMessage), never by a
        // second reading of its own; one line only.
        if (data.find_first_of("\r\n") != std::string::npos) return Refuse(peer, CheatKind::ForgedSpawnId, "multi-line spawn");
        const SpawnMessage m = ParseSpawnMessage(data);
        if (!SpawnAllowed(m.path)) return Refuse(peer, CheatKind::SpawnNotAllowed, m.path);
        if (!m.netId.empty()) {
            const long long minted = SpawnIdPeer(m.netId);
            if (minted < 0 || (std::uint32_t)minted != peer) return Refuse(peer, CheatKind::ForgedSpawnId, m.netId);
            m_owner[m.netId] = peer;
        }
        return true;
    }
    if (channel == "__despawn") {
        if (!MayDrive(peer, data, /*claim=*/false)) return Refuse(peer, CheatKind::ForgedDespawn, data);
        m_owner.erase(data); m_track.erase(data);
        return true;
    }
    if (channel == "__xform") {
        const std::size_t bar = data.find('|');
        if (bar == std::string::npos) return true;   // malformed: the normal path ignores it
        const std::string id = data.substr(0, bar);
        // Nobody can apply a transform for an object the server doesn't have, so
        // it's dropped without counting and leaves no state behind.
        if (exists && !exists(id)) return false;
        if (!MayDrive(peer, id, /*claim=*/true)) return Refuse(peer, CheatKind::ForgedTransform, id);
        if (settings.maxSyncSpeed > 0.0f) {
            std::istringstream ss(data.substr(bar + 1));
            Vec3 p; ss >> p.x >> p.y >> p.z;
            if (!ss || !std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
                return Refuse(peer, CheatKind::SpeedHack, id + " (bad position)");
            const float rate = settings.maxSyncSpeed * 1.25f;          // 25% slack, like the avatar check
            const float cap = rate * 0.5f + 0.25f;
            auto it = m_track.find(id);
            if (it == m_track.end()) {
                m_track[id] = Track{p, m_clock, cap, peer};
                return true;
            }
            Track& t = it->second;
            t.budget = std::min(cap, t.budget + rate * (float)std::max(0.0, m_clock - t.t));
            t.t = m_clock;
            const Vec3 d = p - t.pos;
            const float dist = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            t.pos = p;
            if (dist > t.budget) {
                // The jump is refused (not applied, not relayed, counted), but the
                // track re-anchors on it: one lag burst or respawn mustn't lock the
                // object out for good. Jumping again is another violation.
                t.budget = 0.0f;
                return Refuse(peer, CheatKind::SpeedHack, id);
            }
            t.budget -= dist;
        }
        return true;
    }
    if (channel.size() > 6 && channel.compare(0, 6, "__rpc/") == 0 && validateRpc) {
        if (!validateRpc(peer, channel.substr(6), data)) return Refuse(peer, CheatKind::RpcRejected, channel.substr(6));
    }
    return true;
}

bool AntiCheat::ValidateSyncVar(std::uint32_t peer, const std::string& key) {
    if (!settings.enabled || peer == 0) return true;
    PeerInfo& p = m_peers[peer];
    if (settings.maxSyncVarsPerSecond > 0 && ++p.syncVars > settings.maxSyncVarsPerSecond)
        return Refuse(peer, CheatKind::SyncVarFlood, key);
    if (settings.ownerOnlySyncVars) {
        // Every prefix ending at a '.' or '/' may name an object ("car.hp",
        // "car.stats.hp", "sp/3/1.hp"): if any of them is a networked object
        // this peer doesn't own, the write is refused. Plain game-wide keys
        // ("round", "score") stay writable.
        for (std::size_t cut = key.find_first_of("./"); cut != std::string::npos; cut = key.find_first_of("./", cut + 1)) {
            if (cut == 0) continue;
            const std::string netId = key.substr(0, cut);
            bool mine = true;   // an object nobody owns yet stays writable
            if (serverOwns && serverOwns(netId)) mine = false;
            else if (const long long minted = SpawnIdPeer(netId); minted >= 0) mine = (std::uint32_t)minted == peer;
            else if (auto o = m_owner.find(netId); o != m_owner.end()) mine = o->second == peer;
            if (!mine) return Refuse(peer, CheatKind::ForgedSyncVar, key);
        }
    }
    return true;
}

} // namespace okay

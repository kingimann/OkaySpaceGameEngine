#include "okay/Net/AntiCheat.hpp"
#include "okay/Core/Log.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
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
bool ParseFloat(const std::string& v, float& out) {
    std::istringstream is(v); float f = 0.0f;
    if (!(is >> f) || !std::isfinite(f) || f < 0.0f) return false;
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
    for (auto& [id, p] : m_peers) {
        if ((p.syncVarTimer += dt) >= 1.0f) { p.syncVarTimer = 0.0f; p.syncVars = 0; }
        if ((p.violationTimer += dt) >= 60.0f) { p.violationTimer = 0.0f; p.violations = 0; }
    }
}

void AntiCheat::ForgetPeer(std::uint32_t peer) {
    m_peers.erase(peer);
    for (auto it = m_owner.begin(); it != m_owner.end();) {
        if (it->second == peer) { m_track.erase(it->first); it = m_owner.erase(it); }
        else ++it;
    }
}

void AntiCheat::Reset() { m_peers.clear(); m_owner.clear(); m_track.clear(); m_total = 0; }

bool AntiCheat::Refuse(std::uint32_t peer, CheatKind k, const std::string& detail) {
    ++m_peers[peer].violations;
    ++m_total;
    OKAY_WARN("anti-cheat: refused ", CheatKindName(k), " from peer ", peer, detail.empty() ? "" : (" (" + detail + ")"));
    if (onCheat) onCheat(CheatReport{peer, k, detail});
    return false;
}

bool AntiCheat::MayDrive(std::uint32_t peer, const std::string& netId, bool claim) {
    if (serverOwns && serverOwns(netId)) return false;           // the server drives it
    const long long minted = SpawnIdPeer(netId);
    if (minted >= 0) return (std::uint32_t)minted == peer;      // SpawnOwned: the id names its owner
    auto it = m_owner.find(netId);
    if (it != m_owner.end()) return it->second == peer;
    if (claim) m_owner[netId] = peer;                            // first peer to drive it owns it
    return claim;
}

bool AntiCheat::SpawnAllowed(const std::string& path) const {
    const std::string p = NormPath(path);
    for (const auto& a : settings.spawnAllowlist) if (a == p) return true;
    return false;
}

bool AntiCheat::ValidateMessage(std::uint32_t peer, const std::string& channel, const std::string& data, float now) {
    if (!settings.enabled || peer == 0) return true;   // off, or the host itself
    if (channel == "__spawn") {
        // "<path>|x|y|z[|<netId>]"
        const std::string path = data.substr(0, data.find('|'));
        if (!SpawnAllowed(path)) return Refuse(peer, CheatKind::SpawnNotAllowed, path);
        const std::size_t last = data.rfind('|');
        std::size_t bars = (std::size_t)std::count(data.begin(), data.end(), '|');
        if (bars >= 4 && last != std::string::npos) {
            const std::string netId = data.substr(last + 1);
            const long long minted = SpawnIdPeer(netId);
            if (minted < 0 || (std::uint32_t)minted != peer) return Refuse(peer, CheatKind::ForgedSpawnId, netId);
            m_owner[netId] = peer;
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
        if (!MayDrive(peer, id, /*claim=*/true)) return Refuse(peer, CheatKind::ForgedTransform, id);
        if (settings.maxSyncSpeed > 0.0f) {
            std::istringstream ss(data.substr(bar + 1));
            Vec3 p; ss >> p.x >> p.y >> p.z;
            if (!ss || !std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
                return Refuse(peer, CheatKind::SpeedHack, id + " (bad position)");
            Track& t = m_track[id];
            const float rate = settings.maxSyncSpeed * 1.25f;          // 25% slack, like the avatar check
            const float cap = rate * 0.5f + 0.25f;
            if (!t.have) { t.pos = p; t.t = now; t.budget = cap; t.have = true; return true; }
            t.budget = std::min(cap, t.budget + rate * std::max(0.0f, now - t.t));
            t.t = now;
            const Vec3 d = p - t.pos;
            const float dist = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            if (dist > t.budget) return Refuse(peer, CheatKind::SpeedHack, id);   // stays at the last good spot
            t.budget -= dist;
            t.pos = p;
        }
        return true;
    }
    if (channel.size() > 6 && channel.compare(0, 6, "__rpc/") == 0 && validateRpc) {
        if (!validateRpc(peer, channel.substr(6), data)) return Refuse(peer, CheatKind::RpcRejected, channel.substr(6));
    }
    return true;
}

bool AntiCheat::ValidateSyncVar(std::uint32_t peer, const std::string& key, float) {
    if (!settings.enabled || peer == 0) return true;
    PeerInfo& p = m_peers[peer];
    if (settings.maxSyncVarsPerSecond > 0 && ++p.syncVars > settings.maxSyncVarsPerSecond)
        return Refuse(peer, CheatKind::SyncVarFlood, key);
    if (settings.ownerOnlySyncVars) {
        const std::size_t cut = key.find_last_of("./");
        if (cut != std::string::npos && cut > 0) {
            const std::string netId = key.substr(0, cut);
            // Only keys that name a networked object are owned; plain game-wide
            // keys ("round", "score") stay writable.
            const bool known = m_owner.count(netId) || SpawnIdPeer(netId) >= 0 || (serverOwns && serverOwns(netId));
            if (known && !MayDrive(peer, netId, /*claim=*/false)) return Refuse(peer, CheatKind::ForgedSyncVar, key);
        }
    }
    return true;
}

} // namespace okay

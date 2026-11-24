// Host migration for NetworkManager: when the host leaves, the players left move
// to a new host and the game goes on (see NetworkManager::HostMigration). Written
// for this engine from its own join, roster and ownership code (no outside source).
//
// How it works, with no extra round of talking:
//   1. The host sends each client in its room the same list ("__mig", reliable and
//      ordered, again whenever it changes): every client's id, name and address as
//      the host sees it. Only when the session may migrate (MigrationAllowedHere)
//      and has at least two clients (an empty list clears it again).
//   2. The host leaving: Stop() sends "__bye" to every client (three times); a host
//      that crashed is noticed after serverTimeoutSeconds of silence.
//   3. Every client takes the lowest id on the list. That player hosts on the local
//      port it was already using, so it is at the address the old host saw. The
//      others join it from their old local port, so it knows each one by the
//      address on the list and gives it its old id back (TakeExpectedId).
//   4. A chosen host that does not answer in migrationJoinTimeoutSeconds is struck
//      off and the next lowest is tried. A player that hears a join from someone on
//      the list while it still waits for the old host knows the host is gone too,
//      and one still trying a lower id that hears a join from a higher one hosts at
//      once (the higher one gave up on the lower ids already).
//   5. Everyone keeps the shared variables, the ownership table, the synced objects
//      and the player stand-ins. The new host is player 0; the old host's stand-in
//      and its own spawned objects ("sp/0/...") go, as a leaving client's do, and
//      whatever the old host or a player who is gone held is the new host's.
#include "okay/Net/NetworkManager.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Core/Log.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include "NetworkManagerInternal.hpp"

namespace okay {

namespace {
// A private network (or this computer): where players can see each other's addresses anyway.
bool PrivateAddress(std::uint32_t a) {
    return (a >> 24) == 10 || (a >> 24) == 127 || (a >> 20) == 0xAC1 || (a >> 16) == 0xC0A8 || (a >> 16) == 0xA9FE;
}
std::string AddressText(std::uint32_t a) {
    char b[24];
    std::snprintf(b, sizeof b, "%u.%u.%u.%u", (a >> 24) & 0xFF, (a >> 16) & 0xFF, (a >> 8) & 0xFF, a & 0xFF);
    return b;
}
constexpr std::size_t kMaxMigPeers = 64;
} // namespace

int NetworkManager::OnHostMigrated(std::function<void(std::uint32_t)> f) {
    Listener l; l.id = m_nextListener++; l.migrated = std::move(f);
    m_listeners.push_back(std::move(l));
    return m_listeners.back().id;
}

bool NetworkManager::MigrationAllowedHere() const {
    if (hostMigration == HostMigration::Off || m_mode != Mode::Server || m_relay || dedicated) return false;
    // The host enforces these; a player who takes over could not be trusted to.
    if (anticheat.settings.enabled || m_verifyToken || m_joinCheck) return false;
    if (hostMigration == HostMigration::Lan)
        for (const auto& [ep, c] : m_clients)
            if (c.room == m_localRoom && !PrivateAddress(ep.address)) return false;
    return true;
}

// ---- the host: the list every client would use ----------------------------------------------------------

void NetworkManager::MigrationHostTick(float dt) {
    if (m_migGrace > 0.0f && (m_migGrace -= dt) <= 0.0f && !m_migExpect.empty()) {
        // Players who never came back after the move: they count as gone now (PlayersTick says so).
        OKAY_INFO("net: ", m_migExpect.size(), " player(s) did not come back to the new host");
        const std::vector<MigPeer> gone = m_migExpect;
        m_migExpect.clear();
        for (const MigPeer& g : gone) {   // as if they had left: their stand-ins and spawned objects go
            if (auto r = m_remotes.find(g.id); r != m_remotes.end()) {
                if (Scene* sc = GetScene()) if (r->second) sc->Destroy(r->second);
                m_remotes.erase(r);
            }
            m_remoteTarget.erase(g.id);
            DespawnPeerObjects(g.id);
        }
    }
    std::string list;
    if (MigrationAllowedHere()) {
        std::vector<const Client*> room;
        for (const auto& [ep, c] : m_clients) if (c.room == m_localRoom) room.push_back(&c);
        std::sort(room.begin(), room.end(), [](const Client* a, const Client* b) { return a->id < b->id; });
        if (room.size() < 2) room.clear();   // one client has nobody to go on with: no list (and no traffic)
        for (const Client* c : room) {
            if (list.size() > 8000) break;
            std::string name = c->name.substr(0, 64);
            for (char& ch : name) if (ch == '\n' || ch == '|') ch = ' ';
            list += std::to_string(c->id) + "|" + std::to_string(c->endpoint.address) + "|" +
                    std::to_string(c->endpoint.port) + "|" + name + "\n";
        }
    }
    if (list == m_migSent) return;
    m_migSent = list;
    for (auto& [ep, c] : m_clients)
        if (c.room == m_localRoom) SendOrderedTo(c.id, "__mig", list);
}

void NetworkManager::SendHostBye() {
    if (m_mode != Mode::Server || m_clients.empty()) return;
    net::Packet p(Message);
    p.Write(m_localId);
    p.Write(std::string("__bye"));
    p.Write(std::string());
    for (int i = 0; i < 3; ++i)   // unreliable: three copies; a client that misses all of them notices the silence
        for (auto& [ep, c] : m_clients) SendDatagram(c.endpoint, p.Data(), p.Size());
}

// ---- a client: the list, and the move ----------------------------------------------------------------------

void NetworkManager::OnMigWire(std::uint32_t from, const std::string& data) {
    if (m_mode != Mode::Client || from != 0) return;   // the host's word only
    std::vector<MigPeer> list;
    std::size_t at = 0;
    while (at < data.size() && list.size() < kMaxMigPeers) {
        std::size_t nl = data.find('\n', at);
        if (nl == std::string::npos) nl = data.size();
        const std::string line = data.substr(at, nl - at);
        at = nl + 1;
        const std::size_t b1 = line.find('|'), b2 = b1 == std::string::npos ? b1 : line.find('|', b1 + 1),
                          b3 = b2 == std::string::npos ? b2 : line.find('|', b2 + 1);
        if (b3 == std::string::npos) return;   // not the host's shape: ignore the whole list
        char* end = nullptr;
        MigPeer mp;
        const unsigned long id = std::strtoul(line.c_str(), &end, 10);
        const unsigned long addr = std::strtoul(line.c_str() + b1 + 1, nullptr, 10);
        const unsigned long port = std::strtoul(line.c_str() + b2 + 1, nullptr, 10);
        if (end != line.c_str() + b1 || id == 0 || id > 0xFFFFFFFFul || addr > 0xFFFFFFFFul || port == 0 || port > 65535) return;
        mp.id = (std::uint32_t)id;
        mp.ep = net::Endpoint{(std::uint32_t)addr, (std::uint16_t)port};
        mp.name = line.substr(b3 + 1);
        list.push_back(std::move(mp));
    }
    std::sort(list.begin(), list.end(), [](const MigPeer& a, const MigPeer& b) { return a.id < b.id; });
    m_migPeers = std::move(list);
}

bool NetworkManager::BeginMigration(const std::string& why) {
    if (m_mode != Mode::Client || !m_joined || m_migrating) return false;
    if (hostMigration == HostMigration::Off || m_relay || !pinnedHostKey.empty()) return false;
    if (m_migPeers.size() < 2) return false;   // alone: nobody to go on with
    if (std::none_of(m_migPeers.begin(), m_migPeers.end(), [&](const MigPeer& p) { return p.id == m_localId; })) return false;
    const std::uint16_t port = m_socket.LocalPort();
    if (port == 0) return false;
    m_mig = MigCarry{};
    m_mig.myOldId = m_localId;
    m_mig.localPort = port;
    m_mig.candidates = m_migPeers;
    m_mig.vars = m_syncVars;
    m_mig.owners = m_netOwners;
    m_mig.remotes = m_remotes;
    m_mig.remoteTargets = m_remoteTarget;
    m_mig.known = m_knownPlayers;
    m_mig.serverName = m_serverName;
    OKAY_INFO("net: ", why, "; moving to a new host (", m_migPeers.size(), " players on the list)");
    m_migrating = true;
    TryNextHost();
    return true;
}

void NetworkManager::RestoreCarry() {
    m_syncVars = m_mig.vars;
    m_netOwners = m_mig.owners;
    m_remotes = m_mig.remotes;
    m_remoteTarget = m_mig.remoteTargets;
    m_knownPlayers = m_mig.known;
    m_playersAnnounced = true;
}

void NetworkManager::TryNextHost() {
    while (!m_mig.candidates.empty()) {
        const MigPeer next = m_mig.candidates.front();
        if (next.id == m_mig.myOldId) {
            // We are the lowest id left: host, on the port the others know us by.
            m_keepResume = true;   // build mode and quest state, and our own spawned objects, stay
            const bool ok = StartServer(m_mig.localPort);
            m_keepResume = false;
            if (!ok) {
                OKAY_WARN("net: could not host on port ", m_mig.localPort, " for the others; trying the next player");
                m_mig.candidates.erase(m_mig.candidates.begin());
                continue;
            }
            RestoreCarry();
            m_resumeToken = 0;
            m_connectionLost = false; m_connectionLostReason.clear();
            m_migExpect.clear();
            std::uint32_t top = m_mig.myOldId;
            for (const MigPeer& p : m_mig.candidates) {
                top = std::max(top, p.id);
                if (p.id != m_mig.myOldId) m_migExpect.push_back(p);
            }
            for (const auto& [id, name] : m_mig.known) top = std::max(top, id);
            m_nextId = top + 1;   // no newcomer gets an id one of us had (its objects are "sp/<id>/...")
            m_migGrace = migrationGraceSeconds;
            if (serverName.empty()) serverName = m_mig.serverName;
            FinishMigration(m_mig.myOldId);
            return;
        }
        // Join the lowest id left, from our old local port (it knows us by that address).
        const std::string host = AddressText(next.ep.address);
        m_bindPort = m_mig.localPort;
        m_keepResume = true;
        const bool ok = StartClient(host, next.ep.port);
        m_keepResume = false;
        m_bindPort = 0;
        if (!ok) { m_mig.candidates.erase(m_mig.candidates.begin()); continue; }
        m_lastConn = ConnTarget{1, host, next.ep.port, std::string()};
        m_resumeToken = 0;
        m_connectionLost = false; m_connectionLostReason.clear();
        RestoreCarry();
        m_migWait = 0.0f;
        OKAY_INFO("net: joining player ", next.id, " at ", next.ep.ToString(), " as the new host");
        return;
    }
    // Nobody left to go on with.
    m_migrating = false;
    m_keepResume = false;
    Stop();
    m_connectionLost = true;
    m_connectionLostReason = "the host left and no other player could take over";
    SetError("Lost the connection to the host: " + m_connectionLostReason + ".");
    m_errorNoted = true;
}

void NetworkManager::MigrationTick(float dt) {
    if (m_mode == Mode::Server) { MigrationHostTick(dt); return; }
    if (!m_migrating) return;
    if (m_mode != Mode::Client) { m_migrating = false; return; }
    if (m_mig.candidates.empty()) { TryNextHost(); return; }
    if (m_joined) { FinishMigration(m_mig.candidates.front().id); return; }
    if (m_migrateNow) {   // a player after us on the list is joining us: everyone before us is gone for it
        m_migrateNow = false;
        auto& cs = m_mig.candidates;
        cs.erase(std::remove_if(cs.begin(), cs.end(), [&](const MigPeer& p) { return p.id < m_mig.myOldId; }), cs.end());
        OKAY_INFO("net: another player is joining this one as the new host; hosting now");
        TryNextHost();
        return;
    }
    if ((m_migWait += dt) >= migrationJoinTimeoutSeconds || m_joinRejected) {
        OKAY_INFO("net: player ", m_mig.candidates.front().id, m_joinRejected ? " refused us" : " did not answer",
                  " as the new host; trying the next one");
        m_mig.candidates.erase(m_mig.candidates.begin());
        TryNextHost();
    }
}

bool NetworkManager::TakeExpectedId(const net::Endpoint& from, std::uint32_t& id) {
    for (auto it = m_migExpect.begin(); it != m_migExpect.end(); ++it) {
        if (!(it->ep == from)) continue;
        for (const auto& [ep, c] : m_clients) if (c.id == it->id) return false;   // taken already
        id = it->id;
        m_migExpect.erase(it);
        return true;
    }
    return false;
}

void NetworkManager::FinishMigration(std::uint32_t newHostOldId) {
    const bool iHost = m_mode == Mode::Server;
    Scene* sc = GetScene();
    // The old host left: its stand-in and its own spawned objects go, as a leaving client's do.
    if (auto r = m_remotes.find(0); r != m_remotes.end()) {
        if (sc && r->second) sc->Destroy(r->second);
        m_remotes.erase(r);
    }
    m_remoteTarget.erase(0);
    m_remoteMissing.erase(0);
    if (!iHost) {   // the new host is player 0 now: its stand-in here follows it
        if (auto r = m_remotes.find(newHostOldId); r != m_remotes.end()) {
            GameObject* go = r->second;
            m_remotes.erase(r);
            m_remotes[0] = go;
        }
        if (auto t = m_remoteTarget.find(newHostOldId); t != m_remoteTarget.end()) {
            const Vec3 at = t->second;
            m_remoteTarget.erase(t);
            m_remoteTarget[0] = at;
        }
    } else if (auto r = m_remotes.find(newHostOldId); r != m_remotes.end()) {   // never ours, but tidy
        if (sc && r->second) sc->Destroy(r->second);
        m_remotes.erase(r);
    }
    {
        const std::string prefix = "sp/0/";
        std::vector<std::string> gone;
        for (const auto& [netId, o] : m_syncObjs) if (netId.compare(0, prefix.size(), prefix) == 0) gone.push_back(netId);
        for (const std::string& netId : gone) { DestroyLocalSync(netId); m_netOwners.erase(netId); }
    }
    // Who holds what: the new host's and the old host's go to 0, so does whatever a player who is gone held.
    {
        std::vector<std::uint32_t> staying;
        for (const MigPeer& p : m_mig.candidates) staying.push_back(p.id);
        for (const MigPeer& p : m_migExpect) staying.push_back(p.id);
        std::vector<std::pair<std::string, std::uint32_t>> moved;
        for (const auto& [netId, owner] : m_netOwners) {
            std::uint32_t now = owner;
            if (owner == newHostOldId || owner == 0 || std::find(staying.begin(), staying.end(), owner) == staying.end()) now = 0;
            moved.emplace_back(netId, now);
        }
        for (const auto& [netId, owner] : moved) ApplyOwner(netId, owner);
    }
    // The player list: the old host left; the new host is 0.
    std::string oldHostName;
    if (auto k = m_knownPlayers.find(0); k != m_knownPlayers.end()) { oldHostName = k->second; m_knownPlayers.erase(k); }
    if (auto k = m_knownPlayers.find(newHostOldId); k != m_knownPlayers.end()) {
        const std::string name = k->second;
        m_knownPlayers.erase(k);
        if (!iHost) m_knownPlayers[0] = name;
    }
    m_migrating = false;
    ++m_migrations;
    OKAY_INFO("net: host migration done: ", iHost ? "this player is the new host" : "playing on with the new host",
              " (it was player ", newHostOldId, ")");
    RaisePlayerEvent(false, 0, oldHostName);
    RaiseHostMigrated(newHostOldId);
}

} // namespace okay

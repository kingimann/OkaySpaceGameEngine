// The easy layer of NetworkManager: the player list, join / leave events and
// named network events that work the same on the host and on every client, the
// network clock, and errors in plain words. Visual Script, OkayScript and
// okay::Net (Net.hpp) are thin calls onto these. Written for this engine from
// its own roster and reliable-message code (no outside source).
//
// Wire use (no new packet types):
//   "__ev"         reliable and ordered (one channel for every event, so they
//                  arrive in the order they were sent). Client -> host:
//                  "<name>\n<data>"; host -> client: "<sender id>|<name>\n<data>"
//                  (the host stamps who sent it; a client's own claim is never
//                  read). The host relays a client's event to the other clients
//                  in the sender's room.
//   "__time"       unreliable, host -> clients, about once a second: the host's
//                  clock in seconds. Clients add half the round trip.
#include "okay/Net/NetworkManager.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/NetworkSync.hpp"
#include "okay/Net/NetSim.hpp"
#include "okay/Components/VisualScriptComponent.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Core/Log.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_set>

namespace okay {

namespace {
NetworkManager* g_current = nullptr;   // the most recently started manager (read through IsLive)

// "Player Hit" -> "Player_Hit": an entry name is one token (the same as VisualScriptComponent::SendEvent).
std::string EntryToken(std::string s) {
    for (char& c : s) if (c == ' ' || c == '\t' || c == ':' || c == '/') c = '_';
    return s;
}
// Every visual script and text script in the scene that may run now, collected
// first: a handler may add or remove objects.
void SceneListeners(Scene* sc, std::vector<VisualScriptComponent*>& graphs, std::vector<ScriptComponent*>& scripts) {
    if (!sc) return;
    for (auto* v : sc->FindObjectsOfType<VisualScriptComponent>())
        if (v->enabled && v->gameObject && v->gameObject->active && ScriptsRun(v->gameObject)) graphs.push_back(v);
    for (auto* s : sc->FindObjectsOfType<ScriptComponent>())
        if (s->enabled && s->VM() && s->gameObject && s->gameObject->active && ScriptsRun(s->gameObject)) scripts.push_back(s);
}
} // namespace

NetworkManager* NetworkManager::Current() {
    return IsLive(g_current) ? g_current : nullptr;
}

NetworkManager* NetworkManager::Ensure(Scene& scene) {
    if (auto* n = scene.FindObjectOfType<NetworkManager>()) return n;
    return scene.CreateGameObject("__Network")->AddComponent<NetworkManager>();
}

bool NetworkManager::HostOrJoin(std::uint16_t port, const std::string& host) {
    bool free = false;
    if (net::Startup()) {
        net::UdpSocket probe;
        free = probe.Open() && probe.Bind(port);
        probe.Close();
        net::Shutdown();
    }
    if (free) return StartServer(port);
    OKAY_INFO("net: port ", port, " is taken on this computer, so joining the game hosted there (", host, ")");
    return StartClient(host, port);
}

bool NetworkManager::EventNameOk(const std::string& name) {
    if (name.empty() || name.size() > kMaxEventName) return false;
    for (unsigned char c : name)
        if (!(std::isalnum(c) || c == '_' || c == '-' || c == '.' || c == ' ')) return false;
    return true;
}

void NetworkManager::SetError(const std::string& plain) {
    m_lastError = plain;
    OKAY_WARN("net: ", plain);
}

std::vector<NetworkManager::Player> NetworkManager::Players() const {
    std::vector<Player> out;
    if (m_mode == Mode::Offline) return out;
    if (m_mode == Mode::Client && !m_joined) return out;
    for (const RosterEntry& e : Roster()) {
        Player p;
        p.id = e.id; p.name = e.name;
        p.isHost = e.id == 0;
        p.isLocal = (m_mode == Mode::Server) ? e.id == 0 : e.id == m_localId;
        out.push_back(std::move(p));
    }
    // A client hears the roster about once a second: until the first one, it still knows itself.
    if (m_mode == Mode::Client && std::none_of(out.begin(), out.end(), [&](const Player& p) { return p.isLocal; })) {
        Player me; me.id = m_localId; me.name = m_localName; me.isLocal = true;
        out.push_back(std::move(me));
        std::sort(out.begin(), out.end(), [](const Player& a, const Player& b) { return a.id < b.id; });
    }
    return out;
}

int NetworkManager::OnPlayerJoined(std::function<void(std::uint32_t, const std::string&)> f) {
    Listener l; l.id = m_nextListener++; l.joined = std::move(f);
    m_listeners.push_back(std::move(l));
    return m_listeners.back().id;
}
int NetworkManager::OnPlayerLeft(std::function<void(std::uint32_t)> f) {
    Listener l; l.id = m_nextListener++; l.left = std::move(f);
    m_listeners.push_back(std::move(l));
    return m_listeners.back().id;
}
int NetworkManager::OnEvent(const std::string& name, std::function<void(std::uint32_t, const std::string&)> f) {
    Listener l; l.id = m_nextListener++; l.name = name; l.event = std::move(f);
    m_listeners.push_back(std::move(l));
    return m_listeners.back().id;
}
int NetworkManager::OnAnyEvent(std::function<void(const ReceivedEvent&)> f) {
    Listener l; l.id = m_nextListener++; l.any = std::move(f);
    m_listeners.push_back(std::move(l));
    return m_listeners.back().id;
}
void NetworkManager::RemoveListener(int id) {
    m_listeners.erase(std::remove_if(m_listeners.begin(), m_listeners.end(), [id](const Listener& l) { return l.id == id; }),
                      m_listeners.end());
}

// ---- named events --------------------------------------------------------------------------------------------
// One reliable ordered channel, "__ev", carries every event, so events arrive
// once and in the order each player sent them (whatever their names).
//   client -> host: "<name>\n<data>", or "@<target>|<name>\n<data>" for one player
//   host -> client: "<sender>|<name>\n<data>"

bool NetworkManager::SendEvent(const std::string& name, const std::string& data, bool includeSelf) {
    if (!EventNameOk(name)) {
        SetError("Network event \"" + name.substr(0, 80) + "\" was not sent: a name is 1 to 64 letters, digits, spaces, '_', '-' or '.'.");
        return false;
    }
    if (data.size() > kMaxEventData) {
        SetError("Network event \"" + name + "\" was not sent: its data is " + std::to_string(data.size()) +
                 " bytes, and the most is " + std::to_string(kMaxEventData) + ".");
        return false;
    }
    if (m_mode == Mode::Offline || (m_mode == Mode::Client && !m_joined)) {
        SetError("Network event \"" + name + "\" was not sent: this game is not in a network session yet (Host or Join first).");
        return false;
    }
    if (m_mode == Mode::Server) {
        const std::string stamped = std::to_string(m_localId) + "|" + name + "\n" + data;
        for (auto& [ep, c] : m_clients)
            if (dedicated || c.room == m_localRoom) SendOrderedTo(c.id, "__ev", stamped);
    } else {
        SendOrderedTo(0, "__ev", name + "\n" + data);
    }
    if (includeSelf) DispatchEvent(m_localId, name, data);
    return true;
}

bool NetworkManager::SendEventTo(std::uint32_t target, const std::string& name, const std::string& data) {
    if (!EventNameOk(name) || data.size() > kMaxEventData) {
        SetError("Network event \"" + name.substr(0, 80) + "\" was not sent: bad name or more than " +
                 std::to_string(kMaxEventData) + " bytes of data.");
        return false;
    }
    if (m_mode != Mode::Offline && target == m_localId) { DispatchEvent(m_localId, name, data); return true; }
    if (m_mode == Mode::Server) {
        for (auto& [ep, c] : m_clients)
            if (c.id == target) { SendOrderedTo(c.id, "__ev", std::to_string(m_localId) + "|" + name + "\n" + data); return true; }
        SetError("Network event \"" + name + "\" was not sent: there is no player " + std::to_string(target) + ".");
        return false;
    }
    if (m_mode == Mode::Client && m_joined) {
        SendOrderedTo(0, "__ev", "@" + std::to_string(target) + "|" + name + "\n" + data);   // the host passes it on
        return true;
    }
    SetError("Network event \"" + name + "\" was not sent: this game is not in a network session yet (Host or Join first).");
    return false;
}

namespace {
// "<digits>|rest" -> the number and the rest. False if it is not that shape.
bool TakeId(const std::string& s, std::size_t from, std::uint32_t& id, std::string& rest) {
    const auto bar = s.find('|', from);
    if (bar == std::string::npos || bar == from || bar - from > 10) return false;
    std::uint64_t v = 0;
    for (std::size_t i = from; i < bar; ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        v = v * 10 + (std::uint64_t)(s[i] - '0');
    }
    if (v > 0xFFFFFFFFull) return false;
    id = (std::uint32_t)v;
    rest = s.substr(bar + 1);
    return true;
}
// "<name>\n<data>" -> both. False if the name is not a fair one.
bool SplitEvent(const std::string& s, std::string& name, std::string& data) {
    const auto nl = s.find('\n');
    if (nl == std::string::npos) return false;
    name = s.substr(0, nl);
    data = s.substr(nl + 1);
    return NetworkManager::EventNameOk(name) && data.size() <= NetworkManager::kMaxEventData;
}
} // namespace

// Called from Deliver for "__ev" (on the host, after ClientMessageOk and the anti-cheat).
void NetworkManager::OnEventWire(std::uint32_t from, const std::string& wire) {
    std::string name, data;
    if (m_mode == Mode::Server) {
        if (from == m_localId) return;   // only clients send to the host
        const Client* sender = nullptr;
        for (auto& [ep, c] : m_clients) if (c.id == from) { sender = &c; break; }
        if (!sender) return;
        std::string body = wire;
        std::int64_t only = -1;   // one player (SendEventTo), or everyone in the sender's room
        if (!body.empty() && body[0] == '@') {
            std::uint32_t t = 0; std::string rest;
            if (!TakeId(body, 1, t, rest)) return;
            only = t;
            body = std::move(rest);
        }
        if (!SplitEvent(body, name, data)) return;
        // The host stamps who sent it: a client can't speak for another player.
        const std::string stamped = std::to_string(from) + "|" + body;
        const std::string room = sender->room;
        for (auto& [ep, c] : m_clients)
            if (c.id != from && c.room == room && (only < 0 || (std::int64_t)c.id == only))
                SendOrderedTo(c.id, "__ev", stamped);
        if ((only < 0 || only == (std::int64_t)m_localId) && (dedicated || room == m_localRoom))
            DispatchEvent(from, name, data);
        return;
    }
    // A client: only the host's word counts, and the host says who sent it.
    if (m_mode != Mode::Client || from != 0) return;
    std::uint32_t sender = 0; std::string rest;
    if (!TakeId(wire, 0, sender, rest) || !SplitEvent(rest, name, data)) return;
    DispatchEvent(sender, name, data);
}

void NetworkManager::DispatchEvent(std::uint32_t from, const std::string& name, const std::string& data) {
    const std::vector<Listener> snap = m_listeners;   // a handler may add or remove listeners
    ReceivedEvent ev{from, name, data};
    for (const Listener& l : snap) {
        if (l.event && l.name == name) l.event(from, data);
        if (l.any) l.any(ev);
    }
    std::vector<VisualScriptComponent*> graphs;
    std::vector<ScriptComponent*> scripts;
    SceneListeners(GetScene(), graphs, scripts);
    const std::string entry = "OnNetworkEvent:" + EntryToken(name);
    for (auto* g : graphs) {
        if (!g->Graph() || g->Graph()->Entries(entry.c_str()).empty()) continue;
        g->SetVariable("event_sender", vs::VsValue((float)from));
        g->RaiseEvent(entry.c_str(), nullptr, name, vs::VsValue(data));
    }
    const std::vector<vs::VsValue> all{vs::VsValue(name), vs::VsValue(data), vs::VsValue((float)from)};
    const std::vector<vs::VsValue> one{vs::VsValue(data), vs::VsValue((float)from), vs::VsValue(name)};
    for (auto* s : scripts) {
        if (!s->VM()) continue;
        s->VM()->CallEventArgs("on_network_event", all);
        s->VM()->CallEventArgs("net_event:" + name, one);   // net.on("<name>", fn)
    }
}

// ---- players joining and leaving --------------------------------------------------------------------------------

void NetworkManager::RaisePlayerEvent(bool joined, std::uint32_t id, const std::string& name) {
    const std::vector<Listener> snap = m_listeners;
    for (const Listener& l : snap) {
        if (joined && l.joined) l.joined(id, name);
        if (!joined && l.left) l.left(id);
    }
    std::vector<VisualScriptComponent*> graphs;
    std::vector<ScriptComponent*> scripts;
    SceneListeners(GetScene(), graphs, scripts);
    const char* entry = joined ? "OnPlayerJoined" : "OnPlayerLeft";
    for (auto* g : graphs) g->RaiseEvent(entry, nullptr, name, vs::VsValue((float)id));
    const std::vector<vs::VsValue> args{vs::VsValue((float)id), vs::VsValue(name)};
    for (auto* s : scripts)
        if (s->VM()) s->VM()->CallEventArgs(joined ? "on_player_joined" : "on_player_left", args);
}

void NetworkManager::PlayersTick(float dt) {
    SimulatorTick();
    if ((m_statTimer += dt) >= 1.0f) {
        m_bytesInRate = (float)m_bytesInAcc / m_statTimer;
        m_bytesOutRate = (float)m_bytesOutAcc / m_statTimer;
        m_bytesInAcc = m_bytesOutAcc = 0;
        m_statTimer = 0.0f;
    }
    for (auto& m : m_pingMarks) m.age += dt;
    const bool live = m_mode == Mode::Server || (m_mode == Mode::Client && m_joined);
    if (!live) {
        // A session that ended: the next one announces everybody again. (No leave
        // events for our own Leave: the game asked for it.)
        m_knownPlayers.clear();
        m_playersAnnounced = false;
        m_clientNetTime = -1.0;
        m_timeTimer = 0.0f;
        m_netOwners.clear();
        m_pingMarks.clear();
        return;
    }
    g_current = this;
    // The network clock: the host tells its clients its time about once a second.
    if (m_mode == Mode::Server) {
        if ((m_timeTimer += dt) >= 1.0f || !m_playersAnnounced) {
            m_timeTimer = 0.0f;
            const std::string t = std::to_string(m_serverTime);
            for (auto& [ep, c] : m_clients) SendTo(c.id, "__time", t);
        }
    } else if (m_clientNetTime >= 0.0) {
        m_clientNetTime += dt;
    }
    // A client waits for the host's first roster: it then knows everyone at once.
    if (m_mode == Mode::Client && m_roster.empty()) return;
    std::unordered_map<std::uint32_t, std::string> now;
    for (const Player& p : Players())
        if (!p.isLocal) now[p.id] = p.name;
    // Joins first (in id order, the host first), then leaves.
    std::vector<std::uint32_t> joined, left;
    for (const auto& [id, name] : now) if (!m_knownPlayers.count(id)) joined.push_back(id);
    for (const auto& [id, name] : m_knownPlayers) if (!now.count(id)) left.push_back(id);
    std::sort(joined.begin(), joined.end());
    std::sort(left.begin(), left.end());
    for (std::uint32_t id : joined) {
        m_knownPlayers[id] = now[id];
        if (m_mode == Mode::Server)   // who holds what, for a player who just came in
            for (const auto& [netId, owner] : m_netOwners) SendOrderedTo(id, "__own", std::to_string(owner) + "|" + netId);
        RaisePlayerEvent(true, id, now[id]);
    }
    for (std::uint32_t id : left) {
        // A client hears the roster unreliably: only a player missing from two
        // rosters in a row has left (as the stand-ins do, NetworkManagerClient).
        // Its stand-in goes after the second miss; until then it has not left.
        if (m_mode == Mode::Client && m_remotes.count(id)) continue;
        const std::string name = m_knownPlayers[id];
        m_knownPlayers.erase(id);
        if (m_mode == Mode::Server) {   // what the player held goes back to the host
            std::vector<std::string> back;
            for (const auto& [netId, owner] : m_netOwners) if (owner == id) back.push_back(netId);
            for (const std::string& netId : back) GiveOwnership(netId, m_localId);
        }
        RaisePlayerEvent(false, id, name);
    }
    m_playersAnnounced = true;
}

// ---- ownership handoff -----------------------------------------------------------------------------------------
// The host decides. "__own" (ordered, host -> clients): "<owner>|<netId>". "__ownreq" (ordered,
// client -> host): the netId. A late joiner gets the whole table when it is announced (PlayersTick).

namespace {
std::string SyncIdOf(const NetworkSync* ns) {
    return ns && ns->gameObject ? (ns->netId.empty() ? ns->gameObject->name : ns->netId) : std::string();
}
} // namespace

std::int64_t NetworkManager::OwnerOf(const std::string& netId) const {
    auto it = m_netOwners.find(netId);
    return it != m_netOwners.end() ? (std::int64_t)it->second : -1;
}

bool NetworkManager::IsMine(const std::string& netId) const {
    if (m_mode == Mode::Offline) return true;
    if (auto it = m_netOwners.find(netId); it != m_netOwners.end()) return it->second == m_localId;
    auto s = m_syncObjs.find(netId);
    return s != m_syncObjs.end() && s->second.owned;
}

int NetworkManager::OnOwnershipChanged(std::function<void(const std::string&, std::uint32_t)> f) {
    Listener l; l.id = m_nextListener++; l.owner = std::move(f);
    m_listeners.push_back(std::move(l));
    return m_listeners.back().id;
}

void NetworkManager::ApplyOwner(const std::string& netId, std::uint32_t owner) {
    const bool changed = OwnerOf(netId) != (std::int64_t)owner;
    m_netOwners[netId] = owner;
    if (m_mode == Mode::Server) anticheat.SetOwner(netId, owner);
    const bool mine = owner == m_localId;
    std::vector<GameObject*> objs;
    if (Scene* sc = GetScene())
        for (auto* ns : sc->FindObjectsOfType<NetworkSync>())
            if (SyncIdOf(ns) == netId) {
                ns->authority = NetworkSync::Authority::Manual;
                ns->SetOwned(mine);
                objs.push_back(ns->gameObject);
            }
    SetSyncOwned(netId, mine);
    if (!changed) return;
    const std::vector<Listener> snap = m_listeners;
    for (const Listener& l : snap) if (l.owner) l.owner(netId, owner);
    for (GameObject* go : objs) {
        if (!go) continue;
        for (auto* g : go->GetComponents<VisualScriptComponent>()) g->RaiseEvent("OnOwnershipChanged", nullptr, netId, vs::VsValue((float)owner));
        for (auto* s : go->GetComponents<ScriptComponent>())
            if (s->VM()) s->VM()->CallEventArgs("on_ownership_changed", {vs::VsValue((float)owner), vs::VsValue(netId)});
    }
}

bool NetworkManager::GiveOwnership(const std::string& netId, std::uint32_t player) {
    if (m_mode != Mode::Server) {
        SetError("Only the host can give an object to a player (\"" + netId + "\"); a client asks with RequestOwnership.");
        return false;
    }
    if (netId.empty() || netId.size() > kMaxVarKey || netId.find('|') != std::string::npos) {
        SetError("Could not give \"" + netId.substr(0, 80) + "\": not a network id.");
        return false;
    }
    bool known = player == 0;
    for (auto& [ep, c] : m_clients) if (c.id == player) known = true;
    if (!known) { SetError("Could not give \"" + netId + "\" to player " + std::to_string(player) + ": no such player."); return false; }
    ApplyOwner(netId, player);
    const std::string wire = std::to_string(player) + "|" + netId;
    for (auto& [ep, c] : m_clients) SendOrderedTo(c.id, "__own", wire);
    return true;
}

void NetworkManager::RequestOwnership(const std::string& netId) {
    if (m_mode == Mode::Server) { GiveOwnership(netId, m_localId); return; }
    if (m_mode == Mode::Client && m_joined && !netId.empty() && netId.size() <= kMaxVarKey)
        SendOrderedTo(0, "__ownreq", netId);
}

void NetworkManager::OnOwnerRequest(std::uint32_t from, const std::string& netId) {
    if (m_mode != Mode::Server || from == m_localId || netId.empty() || netId.size() > kMaxVarKey) return;
    bool allowed;
    if (allowOwnershipRequest) allowed = allowOwnershipRequest(from, netId);
    else {
        // The object's own rule: "Players can claim it", and no other client holds it now.
        const std::int64_t cur = OwnerOf(netId);
        allowed = anticheat.claimable && anticheat.claimable(netId) && (cur <= 0 || cur == (std::int64_t)from);
    }
    if (allowed) GiveOwnership(netId, from);
    else OKAY_INFO("net: player ", from, " asked for '", netId, "', refused (not claimable, or someone holds it)");
}

void NetworkManager::OnOwnerWire(std::uint32_t from, const std::string& data) {
    if (m_mode != Mode::Client || from != 0) return;   // the host's word only
    std::uint32_t owner = 0; std::string netId;
    if (!TakeId(data, 0, owner, netId) || netId.empty() || netId.size() > kMaxVarKey) return;
    ApplyOwner(netId, owner);
}

// ---- stats -----------------------------------------------------------------------------------------------------

void NetworkManager::NotePingSent(float stamp) {
    if (m_pingMarks.size() >= 16) m_pingMarks.erase(m_pingMarks.begin());
    m_pingMarks.push_back(PingMark{stamp, 0.0f, false});
}
void NetworkManager::NotePong(float stamp) {
    for (auto& m : m_pingMarks) if (m.stamp == stamp) { m.answered = true; break; }
}

NetworkManager::NetStats NetworkManager::Stats() const {
    NetStats st;
    st.rttMs = m_mode == Mode::Client ? m_rtt : 0.0f;
    int decided = 0, lost = 0;
    for (const auto& m : m_pingMarks)
        if (m.answered || m.age > 2.0f) { ++decided; if (!m.answered) ++lost; }   // still waiting: not counted yet
    st.lossPercent = decided > 0 ? 100.0f * (float)lost / (float)decided : 0.0f;
    st.bytesInPerSec = m_bytesInRate;
    st.bytesOutPerSec = m_bytesOutRate;
    st.players = (int)Players().size();
    st.networkTime = NetworkTime();
    return st;
}

// ---- bad-network testing -----------------------------------------------------------------------------------------

void NetworkManager::SimulatorTick() {
    const bool want = simulateLatencyMs > 0.0f || simulateJitterMs > 0.0f || simulateLossPercent > 0.0f;
    net::SimSettings& sim = net::Simulator();
    if (want) {
        sim.enabled = true;
        sim.latencyMs = std::max(0.0f, simulateLatencyMs);
        sim.jitterMs = std::max(0.0f, simulateJitterMs);
        sim.loss = std::min(1.0f, std::max(0.0f, simulateLossPercent / 100.0f));
        m_simApplied = true;
    } else if (m_simApplied) {
        sim.enabled = false;   // this manager turned it on: it turns it off
        m_simApplied = false;
    }
}

// ---- errors in plain words -----------------------------------------------------------------------------------

void NetworkManager::ErrorsTick() {
    if (m_mode == Mode::Client && !m_joined && m_joinRejected && !m_errorNoted) {
        m_errorNoted = true;
        const std::string where = m_serverEp.ToString();
        if (m_joinRejectReason == "the host did not answer")
        {
            char wait[32];   // "15 s", "0.5 s"
            const float t = joinTimeoutSeconds;
            if (std::fabs(t - std::round(t)) < 0.05f) std::snprintf(wait, sizeof wait, "%d s", (int)std::lround(t));
            else std::snprintf(wait, sizeof wait, "%.1f s", t);
            SetError("Could not reach host " + where + " (no answer after " + wait + "). Is the game hosted there, on this "
                     "port, and does the host's firewall let UDP through?");
        }
        else
            SetError("Could not join host " + where + ": " + m_joinRejectReason + ".");
    }
    if (m_connectionLost && !m_errorNoted) {
        m_errorNoted = true;
        SetError("Lost the connection to the host: " + (m_connectionLostReason.empty() ? std::string("no reason given") : m_connectionLostReason) + ".");
    }
    if (m_kicked && !m_errorNoted) {
        m_errorNoted = true;
        SetError("You were removed from the game" + (m_kickReason.empty() ? std::string(".") : ": " + m_kickReason + "."));
    }
}

// ---- the network clock ----------------------------------------------------------------------------------------

// A client hears the host's clock: add half the round trip; small differences are
// eased out (so the time never jumps), a large one (the first, a long stall) is taken at once.
void NetworkManager::OnHostTime(const std::string& data) {
    char* end = nullptr;
    const double t = std::strtod(data.c_str(), &end);
    if (end == data.c_str() || !std::isfinite(t) || t < 0.0) return;
    const double est = t + (double)m_rtt * 0.0005;   // m_rtt is in milliseconds
    if (m_clientNetTime < 0.0 || std::fabs(est - m_clientNetTime) > 0.5) m_clientNetTime = est;
    else m_clientNetTime += (est - m_clientNetTime) * 0.2;
}

double NetworkManager::NetworkTime() const {
    if (m_mode == Mode::Server) return m_serverTime;
    if (m_mode == Mode::Client && m_joined && m_clientNetTime >= 0.0) return m_clientNetTime;
    return 0.0;
}

} // namespace okay

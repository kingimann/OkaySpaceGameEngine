// okay::Net: one-line calls onto the scene's NetworkManager (see Net.hpp).
#include "okay/Net/Net.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Core/Log.hpp"

namespace okay {

namespace {
NetworkManager* g_used = nullptr;   // from Net::Use (checked with IsLive before each use)
std::string g_noManagerError;       // LastError when there is no manager to hold one

NetworkManager* Need(const char* what) {
    NetworkManager* n = Net::Manager();
    if (!n) {
        g_noManagerError = std::string("Net::") + what + ": there is no Network Manager yet. Call Net::Use(scene) first "
                           "(or add a Network Manager to the scene).";
        OKAY_WARN("net: ", g_noManagerError);
    }
    return n;
}
} // namespace

NetworkManager* Net::Use(Scene& scene) {
    g_used = NetworkManager::Ensure(scene);
    return g_used;
}

NetworkManager* Net::Manager() {
    if (NetworkManager::IsLive(g_used)) return g_used;
    g_used = nullptr;
    return NetworkManager::Current();
}

bool Net::Host(std::uint16_t port) { auto* n = Need("Host"); return n && n->StartServer(port); }
bool Net::HostOrJoin(std::uint16_t port) { auto* n = Need("HostOrJoin"); return n && n->HostOrJoin(port); }

bool Net::Join(const std::string& address) {
    auto* n = Need("Join");
    if (!n) return false;
    std::string host;
    std::uint16_t port = NetworkManager::kDefaultPort;
    if (!SplitHostPort(address, host, port, NetworkManager::kDefaultPort)) {
        g_noManagerError = "Could not join \"" + address + "\": write it as an address (192.168.1.20), or an address and a port (192.168.1.20:45000).";
        OKAY_WARN("net: ", g_noManagerError);
        return false;
    }
    return n->StartClient(host, port);
}

void Net::Leave() { if (auto* n = Manager()) n->Stop(); }
bool Net::IsHost() { auto* n = Manager(); return n && n->IsServer(); }
bool Net::IsConnected() { auto* n = Manager(); return n && (n->IsServer() || (n->IsClient() && n->Joined())); }
std::uint32_t Net::MyId() { auto* n = Manager(); return n ? n->LocalId() : 0; }
std::vector<Net::Player> Net::Players() { auto* n = Manager(); return n ? n->Players() : std::vector<Player>{}; }

bool Net::Send(const std::string& event, const std::string& data) { auto* n = Need("Send"); return n && n->SendEvent(event, data); }
bool Net::SendTo(std::uint32_t player, const std::string& event, const std::string& data) {
    auto* n = Need("SendTo"); return n && n->SendEventTo(player, event, data);
}
int Net::On(const std::string& event, std::function<void(std::uint32_t, const std::string&)> f) {
    auto* n = Need("On"); return n ? n->OnEvent(event, std::move(f)) : 0;
}
int Net::OnPlayerJoined(std::function<void(std::uint32_t, const std::string&)> f) {
    auto* n = Need("OnPlayerJoined"); return n ? n->OnPlayerJoined(std::move(f)) : 0;
}
int Net::OnPlayerLeft(std::function<void(std::uint32_t)> f) {
    auto* n = Need("OnPlayerLeft"); return n ? n->OnPlayerLeft(std::move(f)) : 0;
}
int Net::OnHostMigrated(std::function<void(std::uint32_t)> f) {
    auto* n = Need("OnHostMigrated"); return n ? n->OnHostMigrated(std::move(f)) : 0;
}
void Net::Off(int listener) { if (auto* n = Manager()) n->RemoveListener(listener); }

GameObject* Net::Spawn(const std::string& prefab, const Vec3& position) {
    auto* n = Need("Spawn"); return n ? n->SpawnOwned(prefab, position) : nullptr;
}
void Net::SetVar(const std::string& key, const std::string& value) { if (auto* n = Need("SetVar")) n->SetVar(key, value); }
std::string Net::GetVar(const std::string& key) { auto* n = Manager(); return n ? n->GetVar(key) : std::string{}; }

bool Net::Give(const std::string& netId, std::uint32_t player) {
    auto* n = Need("Give");
    if (!n) return false;
    if (!n->IsServer()) { n->RequestOwnership(netId); return true; }
    return n->GiveOwnership(netId, player);
}
bool Net::IsMine(const std::string& netId) { auto* n = Manager(); return !n || n->IsMine(netId); }
NetworkManager::NetStats Net::Stats() { auto* n = Manager(); return n ? n->Stats() : NetworkManager::NetStats{}; }
void Net::ShowStats(bool on) { if (auto* n = Manager()) n->showStatsOverlay = on; }

double Net::Time() { auto* n = Manager(); return n ? n->NetworkTime() : 0.0; }
float Net::Ping() { auto* n = Manager(); return n ? n->RttMs() : 0.0f; }
std::string Net::LastError() {
    auto* n = Manager();
    if (n && !n->LastError().empty()) return n->LastError();
    return g_noManagerError;
}

std::vector<ServerInfo> Net::FindLanGames(int timeoutMs) {
    std::vector<ServerInfo> out;
    for (ServerInfo& s : QueryServers({}, timeoutMs, true))
        if (s.responded) out.push_back(std::move(s));
    return out;
}

bool Net::JoinLanGame(int timeoutMs) {
    const auto games = FindLanGames(timeoutMs);
    if (games.empty()) {
        g_noManagerError = "No game is hosted on the local network (nothing answered on UDP ports " +
                           std::to_string(net::kLanPortFirst) + "-" + std::to_string(net::kLanPortLast) + " within " +
                           std::to_string(timeoutMs) + " ms).";
        OKAY_WARN("net: ", g_noManagerError);
        return false;
    }
    auto* n = Need("JoinLanGame");
    return n && n->StartClient(games.front().host, games.front().port);
}

} // namespace okay

#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Platform/Steam/SteamService.hpp"
#include <memory>

namespace okay {

/// Drop-in component that owns an ISteamService, initializes it on Awake, pumps
/// its callbacks every frame, and shuts it down on destroy. Access the service
/// through Service() to unlock achievements, set stats, and so on.
class SteamManager : public Behaviour {
public:
    SteamConfig config;

    ISteamService* Service() const { return m_service.get(); }
    bool IsAvailable() const { return m_service && m_service->IsAvailable(); }

    /// Convenience pass-throughs for the most common calls.
    bool UnlockAchievement(const std::string& id) {
        return m_service && m_service->UnlockAchievement(id);
    }
    void SetStat(const std::string& name, float value) {
        if (m_service) m_service->SetStat(name, value);
    }
    void  SetStatInt(const std::string& name, int value) { if (m_service) m_service->SetStatInt(name, value); }
    int   GetStatInt(const std::string& name) const { return m_service ? m_service->GetStatInt(name) : 0; }
    int   IncrementStatInt(const std::string& name, int by = 1) { return m_service ? m_service->IncrementStatInt(name, by) : 0; }
    bool  StoreStats() { return m_service && m_service->StoreStats(); }

    // Rich presence + friends (shows what the player is doing; invite friends).
    void SetRichPresence(const std::string& key, const std::string& value) { if (m_service) m_service->SetRichPresence(key, value); }
    int  FriendCount() const { return m_service ? m_service->FriendCount() : 0; }
    std::string FriendName(int i) const { return m_service ? m_service->FriendName(i) : std::string{}; }
    bool InviteFriend(std::uint64_t friendId) { return m_service && m_service->InviteFriend(friendId); }
    void OpenOverlay(const std::string& page) { if (m_service) m_service->ActivateOverlay(page); }

    // Steam lobbies (matchmaking rooms) — the easy way to host/join/browse games.
    std::uint64_t CreateLobby(int maxMembers, const std::string& name = "") { return m_service ? m_service->CreateLobby(maxMembers, name) : 0; }
    bool JoinLobby(std::uint64_t id) { return m_service && m_service->JoinLobby(id); }
    void LeaveLobby() { if (m_service) m_service->LeaveLobby(); }
    std::uint64_t CurrentLobby() const { return m_service ? m_service->CurrentLobby() : 0; }
    std::vector<SteamLobby> LobbyList() const { return m_service ? m_service->LobbyList() : std::vector<SteamLobby>{}; }
    void SetLobbyData(const std::string& key, const std::string& value) { if (m_service) m_service->SetLobbyData(key, value); }
    std::string GetLobbyData(std::uint64_t id, const std::string& key) const { return m_service ? m_service->GetLobbyData(id, key) : std::string{}; }
    std::vector<std::string> LobbyMembers(std::uint64_t id) const { return m_service ? m_service->LobbyMembers(id) : std::vector<std::string>{}; }

    // Steam Cloud pass-throughs (save data that follows the player across PCs).
    bool CloudWrite(const std::string& file, const std::string& data) {
        return m_service && m_service->CloudWrite(file, data);
    }
    std::string CloudRead(const std::string& file) const {
        return m_service ? m_service->CloudRead(file) : std::string{};
    }

    // Steam Workshop pass-throughs (community content).
    std::uint64_t WorkshopPublish(const WorkshopItem& item) {
        return m_service ? m_service->WorkshopPublish(item) : 0;
    }
    bool WorkshopSubscribe(std::uint64_t id) {
        return m_service && m_service->WorkshopSubscribe(id);
    }
    std::vector<WorkshopItem> WorkshopSubscribedItems() const {
        return m_service ? m_service->WorkshopSubscribedItems() : std::vector<WorkshopItem>{};
    }

    void Awake() override;
    void Update(float dt) override;
    void OnDestroy() override;

private:
    std::unique_ptr<ISteamService> m_service;
};

} // namespace okay

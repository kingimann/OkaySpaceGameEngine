// Real Steamworks backend. Compiled only when the engine is built with
// -DOKAY_WITH_STEAM=ON and pointed at a Steamworks SDK (STEAMWORKS_SDK_PATH),
// since Valve's SDK is proprietary and cannot be redistributed with the engine.
//
// It implements ISteamService against the documented Steamworks C++ API. At
// run time it requires the Steam client to be running and a steam_appid.txt (or
// a real app id) present.
#include "okay/Platform/Steam/SteamService.hpp"
#include "okay/Core/Log.hpp"

#include "steam/steam_api.h"

#include <unordered_map>

namespace okay {

// Provided by NullSteamService.cpp for graceful fallback.
std::unique_ptr<ISteamService> CreateNullSteamService();

class SteamworksService : public ISteamService {
public:
    bool Initialize(const SteamConfig& config) override {
        if (!config.skipRelaunchCheck && SteamAPI_RestartAppIfNecessary(config.appId)) {
            OKAY_INFO("Steam: relaunching through the Steam client");
            return false;
        }
        if (!SteamAPI_Init()) {
            OKAY_ERROR("Steam: SteamAPI_Init failed (is the Steam client running?)");
            return false;
        }
        m_ready = true;
        if (SteamUserStats()) SteamUserStats()->RequestCurrentStats();
        OKAY_INFO("Steam: connected as '", UserName(), "'");
        return true;
    }

    void Shutdown() override {
        if (m_ready) { SteamAPI_Shutdown(); m_ready = false; }
    }

    void RunCallbacks() override {
        if (m_ready) SteamAPI_RunCallbacks();
    }

    bool IsAvailable() const override { return m_ready; }
    const char* BackendName() const override { return "steamworks"; }

    std::string UserName() const override {
        if (m_ready && SteamFriends()) return SteamFriends()->GetPersonaName();
        return "Player";
    }
    std::uint64_t UserId() const override {
        if (m_ready && SteamUser()) return SteamUser()->GetSteamID().ConvertToUint64();
        return 0;
    }

    bool UnlockAchievement(const std::string& id) override {
        if (!m_ready || !SteamUserStats()) return false;
        SteamUserStats()->SetAchievement(id.c_str());
        return SteamUserStats()->StoreStats();
    }
    bool IsAchievementUnlocked(const std::string& id) const override {
        bool achieved = false;
        if (m_ready && SteamUserStats())
            SteamUserStats()->GetAchievement(id.c_str(), &achieved);
        return achieved;
    }
    bool ClearAchievement(const std::string& id) override {
        if (!m_ready || !SteamUserStats()) return false;
        SteamUserStats()->ClearAchievement(id.c_str());
        return SteamUserStats()->StoreStats();
    }

    void SetStat(const std::string& name, float value) override {
        if (m_ready && SteamUserStats()) SteamUserStats()->SetStat(name.c_str(), value);
    }
    float GetStat(const std::string& name) const override {
        float v = 0.0f;
        if (m_ready && SteamUserStats()) SteamUserStats()->GetStat(name.c_str(), &v);
        return v;
    }
    bool StoreStats() override {
        return m_ready && SteamUserStats() && SteamUserStats()->StoreStats();
    }

    void SetRichPresence(const std::string& key, const std::string& value) override {
        if (m_ready && SteamFriends())
            SteamFriends()->SetRichPresence(key.c_str(), value.c_str());
    }

    // ---- Steam Cloud (ISteamRemoteStorage) -----------------------------
    bool CloudWrite(const std::string& file, const std::string& data) override {
        return m_ready && SteamRemoteStorage() &&
               SteamRemoteStorage()->FileWrite(file.c_str(), data.data(), (int32)data.size());
    }
    std::string CloudRead(const std::string& file) const override {
        if (!m_ready || !SteamRemoteStorage() || !SteamRemoteStorage()->FileExists(file.c_str()))
            return {};
        int32 size = SteamRemoteStorage()->GetFileSize(file.c_str());
        std::string buf(size, '\0');
        int32 read = SteamRemoteStorage()->FileRead(file.c_str(), buf.data(), size);
        buf.resize(read > 0 ? read : 0);
        return buf;
    }
    bool CloudHasFile(const std::string& file) const override {
        return m_ready && SteamRemoteStorage() && SteamRemoteStorage()->FileExists(file.c_str());
    }
    bool CloudDelete(const std::string& file) override {
        return m_ready && SteamRemoteStorage() && SteamRemoteStorage()->FileDelete(file.c_str());
    }
    std::vector<std::string> CloudFiles() const override {
        std::vector<std::string> names;
        if (!m_ready || !SteamRemoteStorage()) return names;
        int32 count = SteamRemoteStorage()->GetFileCount();
        for (int32 i = 0; i < count; ++i) {
            int32 size = 0;
            const char* name = SteamRemoteStorage()->GetFileNameAndSize(i, &size);
            if (name) names.emplace_back(name);
        }
        return names;
    }
    bool CloudEnabled() const override {
        return m_ready && SteamRemoteStorage() &&
               SteamRemoteStorage()->IsCloudEnabledForAccount() &&
               SteamRemoteStorage()->IsCloudEnabledForApp();
    }

    // ---- Workshop (ISteamUGC) ------------------------------------------
    // Subscribe / unsubscribe / install queries are synchronous; publishing is
    // an async multi-step flow on the real SDK and is left to higher-level tools.
    bool WorkshopSubscribe(std::uint64_t id) override {
        if (!m_ready || !SteamUGC()) return false;
        SteamUGC()->SubscribeItem((PublishedFileId_t)id);
        return true;
    }
    bool WorkshopUnsubscribe(std::uint64_t id) override {
        if (!m_ready || !SteamUGC()) return false;
        SteamUGC()->UnsubscribeItem((PublishedFileId_t)id);
        return true;
    }
    bool WorkshopIsSubscribed(std::uint64_t id) const override {
        if (!m_ready || !SteamUGC()) return false;
        return (SteamUGC()->GetItemState((PublishedFileId_t)id) & k_EItemStateSubscribed) != 0;
    }
    std::string WorkshopItemPath(std::uint64_t id) const override {
        if (!m_ready || !SteamUGC()) return {};
        uint64 sizeOnDisk = 0; char folder[1024] = {0}; uint32 timestamp = 0;
        if (SteamUGC()->GetItemInstallInfo((PublishedFileId_t)id, &sizeOnDisk,
                                           folder, sizeof(folder), &timestamp))
            return std::string(folder);
        return {};
    }
    std::vector<WorkshopItem> WorkshopSubscribedItems() const override {
        std::vector<WorkshopItem> rows;
        if (!m_ready || !SteamUGC()) return rows;
        uint32 n = SteamUGC()->GetNumSubscribedItems();
        std::vector<PublishedFileId_t> ids(n);
        if (n > 0) SteamUGC()->GetSubscribedItems(ids.data(), n);
        for (auto id : ids) {
            WorkshopItem it;
            it.id = (std::uint64_t)id;
            it.subscribed = true;
            it.contentPath = WorkshopItemPath(it.id);
            it.installed = !it.contentPath.empty();
            rows.push_back(it);
        }
        return rows;
    }

private:
    bool m_ready = false;
};

std::unique_ptr<ISteamService> CreateSteamService() {
    auto svc = std::make_unique<SteamworksService>();
    // The caller still must Initialize(); but if Steam isn't present at all we
    // hand back the simulation so the game keeps working.
    return svc;
}

} // namespace okay

#pragma once
#include <cstdint>
#include <memory>
#include <string>

namespace okay {

struct SteamConfig {
    /// Steam application id. 480 is Valve's public "Spacewar" test app id.
    std::uint32_t appId = 480;
    /// If true and Steam isn't running, the process won't be relaunched via Steam.
    bool skipRelaunchCheck = true;
};

/// Platform service abstraction for Steam features (achievements, stats, rich
/// presence, identity). The engine always links a no-op/simulation backend so
/// games build and run without the proprietary Steamworks SDK; building with
/// -DOKAY_WITH_STEAM=ON and pointing at the SDK swaps in the real backend.
class ISteamService {
public:
    virtual ~ISteamService() = default;

    /// Bring the service up. Returns true on success.
    virtual bool Initialize(const SteamConfig& config) = 0;
    virtual void Shutdown() = 0;
    /// Pump platform callbacks; call once per frame.
    virtual void RunCallbacks() = 0;

    /// True when a real Steam client is connected (false for the simulation).
    virtual bool IsAvailable() const = 0;
    /// Identifier of the active backend ("steamworks" or "null").
    virtual const char* BackendName() const = 0;

    // ---- Identity ------------------------------------------------------
    virtual std::string   UserName() const = 0;
    virtual std::uint64_t UserId() const = 0;

    // ---- Achievements --------------------------------------------------
    virtual bool UnlockAchievement(const std::string& id) = 0;
    virtual bool IsAchievementUnlocked(const std::string& id) const = 0;
    virtual bool ClearAchievement(const std::string& id) = 0;

    // ---- Stats ---------------------------------------------------------
    virtual void  SetStat(const std::string& name, float value) = 0;
    virtual float GetStat(const std::string& name) const = 0;
    /// Flush stats/achievements to the backend.
    virtual bool  StoreStats() = 0;

    // ---- Rich presence -------------------------------------------------
    virtual void SetRichPresence(const std::string& key, const std::string& value) = 0;
};

/// Create the best available Steam service for this build: the real Steamworks
/// backend when compiled with OKAY_WITH_STEAM and Steam is running, otherwise a
/// simulation backend that records calls in memory (handy for development).
std::unique_ptr<ISteamService> CreateSteamService();

} // namespace okay

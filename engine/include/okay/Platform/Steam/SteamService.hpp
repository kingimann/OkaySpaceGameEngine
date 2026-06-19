#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace okay {

struct SteamConfig {
    /// Steam application id. 480 is Valve's public "Spacewar" test app id.
    std::uint32_t appId = 480;
    /// If true and Steam isn't running, the process won't be relaunched via Steam.
    bool skipRelaunchCheck = true;
};

/// One row of a Steam leaderboard: the player's display name, their score, and
/// their global rank (1 = top).
struct LeaderboardEntry {
    std::string  name;
    std::int32_t score = 0;
    int          rank  = 0;
};

/// Platform service abstraction for Steam features. The engine always links a
/// no-op/simulation backend so games build and run without the proprietary
/// Steamworks SDK; building with -DOKAY_WITH_STEAM=ON and pointing at the SDK
/// swaps in the real backend.
///
/// The lower section (stats increment, achievement progress, leaderboards,
/// Steam Cloud, friends, overlay) is provided with safe default implementations
/// so alternate backends keep compiling; the simulation backend overrides them
/// to behave in memory.
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
    /// Show progress toward an achievement (e.g. 30/100) without unlocking it.
    virtual void IndicateAchievementProgress(const std::string& id,
                                             std::uint32_t current,
                                             std::uint32_t max) { (void)id; (void)current; (void)max; }

    // ---- Stats ---------------------------------------------------------
    virtual void  SetStat(const std::string& name, float value) = 0;
    virtual float GetStat(const std::string& name) const = 0;
    /// Add to a stat and return the new value (e.g. lifetime kills).
    virtual float IncrementStat(const std::string& name, float by) {
        SetStat(name, GetStat(name) + by); return GetStat(name);
    }
    /// Flush stats/achievements to the backend.
    virtual bool  StoreStats() = 0;

    // ---- Leaderboards --------------------------------------------------
    /// Submit a score to a named leaderboard (keeps the player's best).
    virtual bool UploadLeaderboardScore(const std::string& board, std::int32_t score) {
        (void)board; (void)score; return false;
    }
    /// Top `count` entries of a leaderboard, ranked high-to-low.
    virtual std::vector<LeaderboardEntry> DownloadLeaderboardTop(const std::string& board,
                                                                 int count) const {
        (void)board; (void)count; return {};
    }

    // ---- Steam Cloud (remote storage) ----------------------------------
    virtual bool        CloudWrite(const std::string& file, const std::string& data) { (void)file; (void)data; return false; }
    virtual std::string CloudRead(const std::string& file) const { (void)file; return {}; }
    virtual bool        CloudHasFile(const std::string& file) const { (void)file; return false; }
    virtual bool        CloudDelete(const std::string& file) { (void)file; return false; }

    // ---- Friends / overlay ---------------------------------------------
    /// Number of the player's Steam friends (0 in simulation unless seeded).
    virtual int  FriendCount() const { return 0; }
    /// Open the Steam overlay to a page ("friends", "achievements", ...).
    virtual void ActivateOverlay(const std::string& page) { (void)page; }

    // ---- Rich presence -------------------------------------------------
    virtual void SetRichPresence(const std::string& key, const std::string& value) = 0;
};

/// Create the best available Steam service for this build: the real Steamworks
/// backend when compiled with OKAY_WITH_STEAM and Steam is running, otherwise a
/// simulation backend that records calls in memory (handy for development).
std::unique_ptr<ISteamService> CreateSteamService();

} // namespace okay

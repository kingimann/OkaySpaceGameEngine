#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace okay {

struct PlayFabConfig {
    /// Your PlayFab Title ID (from the Game Manager dashboard).
    std::string titleId;
};

/// Backend service for PlayFab LiveOps: player login, player data, statistics
/// and leaderboards. As with Steam, the engine always links a simulation
/// backend so games build and run without the PlayFab SDK or a network; build
/// with -DOKAY_WITH_PLAYFAB=ON (needs libcurl) for the real REST backend.
class IPlayFabService {
public:
    struct LeaderboardEntry {
        std::string playFabId;
        std::string displayName;
        int value = 0;
        int rank  = 0;
    };

    virtual ~IPlayFabService() = default;

    virtual bool Initialize(const PlayFabConfig& config) = 0;
    virtual void Shutdown() = 0;

    /// Identifier of the active backend ("playfab-rest" or "null").
    virtual const char* BackendName() const = 0;
    /// True when talking to real PlayFab servers (false for the simulation).
    virtual bool IsRealBackend() const = 0;

    // ---- Authentication ------------------------------------------------
    /// Log in with a device/custom id, optionally creating the account.
    virtual bool LoginWithCustomId(const std::string& customId, bool createAccount = true) = 0;
    virtual bool IsLoggedIn() const = 0;
    virtual std::string PlayFabId() const = 0;
    virtual std::string SessionTicket() const = 0;

    // ---- Player data (key/value store) --------------------------------
    virtual bool SetUserData(const std::string& key, const std::string& value) = 0;
    virtual std::string GetUserData(const std::string& key) const = 0;

    // ---- Statistics & leaderboards ------------------------------------
    virtual bool UpdateStatistic(const std::string& name, int value) = 0;
    virtual int  GetStatistic(const std::string& name) const = 0;
    virtual std::vector<LeaderboardEntry> GetLeaderboard(const std::string& name,
                                                         int maxCount = 10) = 0;
};

/// Create the best available PlayFab service: the real REST backend when built
/// with OKAY_WITH_PLAYFAB, otherwise an in-memory simulation backend.
std::unique_ptr<IPlayFabService> CreatePlayFabService();

} // namespace okay

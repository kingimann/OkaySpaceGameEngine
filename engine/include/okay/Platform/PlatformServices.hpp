#pragma once
// Platform services: one set of calls for achievements, stats, leaderboards, cloud saves and presence that goes to
// whichever store this build of the game is for: Steam, the Epic Games Store, or none (an in-memory record, for
// builds sold elsewhere and for testing). A game script calls Achievements.Unlock("first_win") and never needs to
// know which store it runs on.
//
// Which store: game.okayconfig's "store=" line (written by Build Settings > Stores), overridden by the
// environment variable OKAY_STORE (steam, epic or none). Our own design; no SDK code.
#include <cstdint>
#include <memory>
#include <string>

namespace okay {

class ISteamService;
class IEpicService;

enum class StoreKind { None = 0, Steam = 1, Epic = 2 };

/// "none", "steam" or "epic".
const char* StoreKindName(StoreKind k);
/// Reads "steam" / "epic" / "none" in any case; anything else is None.
StoreKind ParseStoreKind(const std::string& text);

/// The store lines of game.okayconfig (written by the editor, read by the player). Never holds a secret.
struct StoreSettings {
    StoreKind     store = StoreKind::None;
    std::uint32_t steamAppId = 0;
    bool          steamRelaunch = false;    // ask Steam to relaunch the game through the client (shipping builds)
    std::string   epicProductId, epicSandboxId, epicDeploymentId, epicClientId;
};
/// One key=value line of game.okayconfig. True if the key is a store key.
bool ApplyStoreKey(StoreSettings& s, const std::string& key, const std::string& value);
/// The lines for game.okayconfig; "" when the store is None (old games read the same config as before).
std::string WriteStoreConfig(const StoreSettings& s);

/// The calls every store offers. Stats are whole numbers (Epic has only those).
class IStoreServices {
public:
    virtual ~IStoreServices() = default;
    virtual StoreKind   Kind() const = 0;
    /// True when the real store client is connected (false for simulations and None).
    virtual bool        IsAvailable() const = 0;
    virtual std::string UserName() const = 0;

    virtual bool UnlockAchievement(const std::string& id) = 0;
    virtual bool IsAchievementUnlocked(const std::string& id) const = 0;
    /// Progress toward an achievement; it unlocks when current reaches max.
    virtual void AchievementProgress(const std::string& id, std::uint32_t current, std::uint32_t max) = 0;

    virtual void         SetStat(const std::string& name, std::int32_t value) = 0;
    virtual std::int32_t AddStat(const std::string& name, std::int32_t by) = 0;
    virtual std::int32_t GetStat(const std::string& name) const = 0;
    /// Send changed stats and achievements to the store now.
    virtual bool         StoreStats() = 0;

    virtual bool SubmitScore(const std::string& board, std::int32_t score) = 0;

    virtual bool        CloudWrite(const std::string& file, const std::string& data) = 0;
    virtual std::string CloudRead(const std::string& file) const = 0;
    virtual bool        CloudHas(const std::string& file) const = 0;
    virtual bool        CloudDelete(const std::string& file) = 0;

    virtual void SetPresence(const std::string& key, const std::string& value) = 0;
    virtual void ShowOverlay(const std::string& page) = 0;

    virtual void Tick() = 0;
    virtual void Shutdown() = 0;
};

/// Adapters over the store services (exposed so tests can wrap fakes).
std::unique_ptr<IStoreServices> MakeSteamStoreServices(ISteamService& steam);
std::unique_ptr<IStoreServices> MakeEpicStoreServices(IEpicService& epic);
std::unique_ptr<IStoreServices> MakeNoStoreServices();

/// The process-wide facade the player, scripts and visual scripts use.
class PlatformServices {
public:
    /// The services for the active store (created on first use from Configure() and OKAY_STORE).
    static IStoreServices& Get();
    static StoreKind Active();
    /// Which store to use; call before the first Get(). Also hands the ids to Steam / Epic.
    static void Configure(const StoreSettings& settings);
    /// Use these services instead (tests, or a custom store). Replaces any current one.
    static void Set(std::unique_ptr<IStoreServices> services);
    static void Tick();
    /// Shut the services down and forget them (the next Get() makes them again).
    static void Shutdown();
    /// The store to use: OKAY_STORE in the environment wins over the configured one.
    static StoreKind Choose(StoreKind configured, const char* envValue);
};

}  // namespace okay

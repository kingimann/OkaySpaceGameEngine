#pragma once
// Epic Online Services (EOS) seam for games sold on the Epic Games Store.
//
// The engine always builds and links a simulation backend (NullEpicService) so a game can be written and tested
// against this interface with no SDK. An Epic build of a game (Build Settings > Stores) loads the EOS SDK library
// the game developer copied from their own SDK (EOSSDK-Win64-Shipping.dll beside the game) at run time and talks
// to Epic through its documented plain C functions (DynamicEpicService.cpp, the Store Kit). We never ship, commit or
// download Epic's SDK ourselves (docs/release/epic_release.md).
//
// Written from Epic's public EOS documentation (platform, auth, connect, achievements, stats, player data
// storage, presence); no Epic sample code was used.
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace okay {

/// What the Epic Games Launcher puts on the game's command line when it starts the game. The exchange code is a
/// one-time login token: it is never logged (see RedactEpicArgs).
struct EpicLaunchArgs {
    bool        fromLauncher = false;   // -EpicPortal, or an exchange code was given
    std::string exchangeCode;           // -AUTH_PASSWORD=<code>
    std::string authType;               // -AUTH_TYPE=exchangecode
    std::string appId;                  // -epicapp=<artifact or app name>
    std::string environment;            // -epicenv=Prod
    std::string userId;                 // -epicuserid=<Epic account id>
    std::string userName;               // -epicusername=<display name>
    std::string locale;                 // -epiclocale=en-US
    std::string sandboxId;              // -epicsandboxid=<id>
};

/// Read the launcher's arguments. Names are matched without regard to case; both "-NAME=value" and
/// "-NAME value" are accepted. Unknown arguments are ignored.
EpicLaunchArgs ParseEpicLaunchArgs(const std::vector<std::string>& args);
EpicLaunchArgs ParseEpicLaunchArgs(int argc, char** argv);
/// The same arguments with every login secret (-AUTH_PASSWORD, -AUTH_LOGIN when the type is a password)
/// replaced by "***", for logs and crash reports.
std::vector<std::string> RedactEpicArgs(const std::vector<std::string>& args);

/// Settings the game passes to the platform. The ids come from the Epic Developer Portal (Product Settings).
struct EpicConfig {
    std::string productName = "OkaySpace Game";
    std::string productVersion = "1.0";
    std::string productId;
    std::string sandboxId;
    std::string deploymentId;
    std::string clientId;
    /// The game client's secret. It is never stored in project files: the editor reads it from the environment
    /// variable OKAY_EOS_CLIENT_SECRET at build time and writes it (packed) into the built game only.
    std::string clientSecret;
    /// Player Data Storage's file encryption key: 64 hex digits. Empty = made from the product and deployment ids
    /// (the same for every player of the game, so saves stay readable after an update).
    std::string encryptionKey;
    /// Optional EOS Developer Authentication Tool login for testing without the launcher: "host:port|credential".
    std::string devAuth;
    EpicLaunchArgs launch;
};

/// Where an Epic account login stands.
enum class EpicLoginState { LoggedOut, Pending, LoggedIn, Failed };

class IEpicService {
public:
    virtual ~IEpicService() = default;

    /// Bring the platform up (EOS_Initialize + EOS_Platform_Create). True on success.
    virtual bool Initialize(const EpicConfig& config) = 0;
    virtual void Shutdown() = 0;
    /// Let the SDK run its callbacks; call once per frame.
    virtual void Tick() = 0;
    /// True when the real EOS platform is up (false for the simulation).
    virtual bool IsAvailable() const = 0;
    /// "eos" or "null".
    virtual const char* BackendName() const = 0;

    // ---- Account ---------------------------------------------------------------------------------------------
    /// Start logging in with what the launcher passed (exchange code) or the developer tool. Asynchronous on the
    /// real backend: watch LoginState(). Returns false if there is nothing to log in with.
    virtual bool Login() = 0;
    virtual EpicLoginState LoginState() const = 0;
    virtual std::string DisplayName() const = 0;
    /// The Epic account id (as text), "" before login.
    virtual std::string AccountId() const = 0;

    // ---- Achievements ----------------------------------------------------------------------------------------
    virtual bool UnlockAchievement(const std::string& id) = 0;
    virtual bool IsAchievementUnlocked(const std::string& id) const = 0;

    // ---- Stats -----------------------------------------------------------------------------------------------
    /// Send an amount to a stat. How it adds up (sum, largest, latest, smallest) is set per stat in the portal.
    virtual bool IngestStat(const std::string& name, std::int32_t amount) = 0;
    /// The value as this session knows it (the simulation sums; the real backend keeps what was sent).
    virtual std::int32_t GetStat(const std::string& name) const = 0;

    // ---- Player data storage (cloud saves) -------------------------------------------------------------------
    virtual bool        WriteFile(const std::string& name, const std::string& data) = 0;
    /// The file's contents if known ("" otherwise). On the real backend a file not yet downloaded starts a
    /// download and returns ""; HasFile turns true once it arrives.
    virtual std::string ReadFile(const std::string& name) const = 0;
    virtual bool        HasFile(const std::string& name) const = 0;
    virtual bool        DeleteFile(const std::string& name) = 0;

    // ---- Presence --------------------------------------------------------------------------------------------
    /// The rich text friends see ("In the Arena").
    virtual void        SetPresence(const std::string& text) = 0;
    virtual std::string Presence() const = 0;

    // ---- Overlay ---------------------------------------------------------------------------------------------
    /// Open the Epic overlay's friends page (EOS_UI_ShowFriends). No-op in the simulation.
    virtual void ShowFriends() {}
};

/// The best Epic backend this build has without a run-time library: the simulation.
std::unique_ptr<IEpicService> CreateEpicService();
/// "EOSSDK-Win64-Shipping.dll" on Windows (libEOSSDK-Linux-Shipping.so, libEOSSDK-Mac-Shipping.dylib elsewhere).
const char* DefaultEosLibraryName();
/// The Store Kit's real Epic backend: loads the developer's EOS SDK library (`libraryPath`, or the default name
/// beside the game) and calls its documented C functions. nullptr when the library is missing or lacks the
/// functions it needs. Initialize() then creates the EOS platform; Login() uses the launcher's exchange code.
std::unique_ptr<IEpicService> CreateDynamicEpicService(const std::string& libraryPath = std::string());
/// The encryption key used when EpicConfig::encryptionKey is empty: 64 hex digits made from the ids.
std::string DeriveEosEncryptionKey(const std::string& productId, const std::string& deploymentId);
/// Always the simulation (used as the fallback when the real platform cannot start).
std::unique_ptr<IEpicService> CreateNullEpicService();

}  // namespace okay

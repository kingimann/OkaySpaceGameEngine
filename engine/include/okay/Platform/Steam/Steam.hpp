#pragma once
#include <functional>
#include "okay/Platform/Steam/SteamService.hpp"

namespace okay {

/// A process-wide Steam accessor so the editor, the shipped player, and game
/// scripts all share one backend without each wiring up its own. The first call
/// creates and initializes the best available service (the real Steamworks
/// backend when built with -DOKAY_WITH_STEAM and Steam is running, otherwise the
/// in-memory simulation). This is what the `steam_*` OkayScript builtins use.
class Steam {
public:
    /// The shared service (created + initialized on first use).
    static ISteamService& Get();
    /// True once Get() has created the service.
    static bool Exists();
    /// Pump callbacks — call once per frame (the player/editor do).
    static void RunCallbacks();
    /// Release the service (mainly for tests).
    static void Shutdown();

    /// Override the app id used when the service is first created. Call before
    /// the first Get(); ignored afterwards.
    static void SetAppId(std::uint32_t appId);
    /// The app id the service is (or will be) created with.
    static std::uint32_t AppId();
    /// When true (a shipped store build), the real backend asks Steam to relaunch the game through the Steam
    /// client if it was started directly (SteamAPI_RestartAppIfNecessary). Call before the first Get().
    static void SetRelaunchCheck(bool on);
    /// True when Steam asked to relaunch the game through the Steam client (the game should quit at once).
    static bool RelaunchRequested();

    /// Let the service load the developer-supplied Steam library at run time (the Store Kit; a Steam store build turns
    /// this on). Off by default. `libraryPath` "" = the default name. Call before the first Get().
    static void EnableRuntimeLibrary(bool on, const std::string& libraryPath = std::string());
    /// Replace how the service is made (tests). nullptr restores the default (CreateSteamService).
    static void SetFactory(std::function<std::unique_ptr<ISteamService>()> factory);
};

} // namespace okay

#pragma once
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
};

} // namespace okay

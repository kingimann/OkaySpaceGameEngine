#pragma once
#include <functional>
#include "okay/Platform/Epic/EpicService.hpp"

namespace okay {

/// One process-wide Epic service shared by the player, scripts and the platform-services facade (like Steam).
/// The first Get() creates and initializes the best backend; if the real platform cannot start, the simulation
/// takes its place so the game keeps running.
class Epic {
public:
    static IEpicService& Get();
    static bool Exists();
    static void Tick();
    static void Shutdown();
    /// The settings used when the service is first created. Call before the first Get(); ignored afterwards.
    static void Configure(const EpicConfig& config);
    static const EpicConfig& Config();
    /// Replace how the service is made (tests). nullptr restores the default.
    static void SetFactory(std::function<std::unique_ptr<IEpicService>()> factory);
};

}  // namespace okay

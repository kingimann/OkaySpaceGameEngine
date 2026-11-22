#include "okay/Platform/Steam/Steam.hpp"
#include "okay/Core/Log.hpp"
#include <cstring>
#include <string>
#include <memory>

namespace okay {

// Provided by NullSteamService.cpp: the in-memory simulation.
std::unique_ptr<ISteamService> CreateNullSteamService();

namespace {
    std::unique_ptr<ISteamService>& Service() {
        static std::unique_ptr<ISteamService> s;
        return s;
    }
    std::function<std::unique_ptr<ISteamService>()>& Factory() {
        static std::function<std::unique_ptr<ISteamService>()> f;
        return f;
    }
    // Default Steam App ID. 480 = Valve's Spacewar test app (dev default); a
    // release build bakes the real id in with -DOKAY_STEAM_APP_ID=<id>. SetAppId()
    // can still override at runtime before the service is created (a game built
    // for Steam does, from its game.okayconfig).
#ifndef OKAY_STEAM_APP_ID
#define OKAY_STEAM_APP_ID 480
#endif
    std::uint32_t& AppIdRef() { static std::uint32_t id = OKAY_STEAM_APP_ID; return id; }
    bool& RelaunchRef() { static bool on = false; return on; }
    bool& RelaunchRequestedRef() { static bool on = false; return on; }
    bool& RuntimeLibRef() { static bool on = false; return on; }
    std::string& RuntimeLibPathRef() { static std::string p; return p; }
}

void Steam::SetAppId(std::uint32_t appId) {
    if (!Service()) AppIdRef() = appId;
}
std::uint32_t Steam::AppId() { return AppIdRef(); }
void Steam::SetRelaunchCheck(bool on) { if (!Service()) RelaunchRef() = on; }
bool Steam::RelaunchRequested() { return RelaunchRequestedRef(); }
void Steam::EnableRuntimeLibrary(bool on, const std::string& libraryPath) {
    if (Service()) return;
    RuntimeLibRef() = on; RuntimeLibPathRef() = libraryPath;
}
void Steam::SetFactory(std::function<std::unique_ptr<ISteamService>()> factory) { Factory() = std::move(factory); }

bool Steam::Exists() { return Service() != nullptr; }

ISteamService& Steam::Get() {
    auto& s = Service();
    if (!s) {
        RelaunchRequestedRef() = false;
        if (Factory()) s = Factory()();
#if !defined(OKAY_WITH_STEAM)
        // The Store Kit: the developer's own steam_api64.dll, loaded at run time (only when a Steam build asked for it).
        else if (RuntimeLibRef()) {
            s = CreateDynamicSteamService(RuntimeLibPathRef());
            if (!s) OKAY_INFO("Steam: ", RuntimeLibPathRef().empty() ? std::string(DefaultSteamLibraryName()) : RuntimeLibPathRef(),
                              " was not found beside the game; using the simulation");
        }
#endif
        if (!s) s = CreateSteamService();
        if (!s) s = CreateNullSteamService();
        SteamConfig cfg; cfg.appId = AppIdRef(); cfg.skipRelaunchCheck = !RelaunchRef();
        if (!s->Initialize(cfg) && std::strcmp(s->BackendName(), "null") != 0) {
            if (s->WantsRelaunch()) RelaunchRequestedRef() = true;
            // The real backend could not start (the Steam client is not running, or the game was not started
            // through it). Keep the game working with the simulation instead of a dead service.
            OKAY_WARN("Steam: the Steam client is not available; using the simulation (nothing reaches Steam)");
            s->Shutdown();
            s = CreateNullSteamService();
            s->Initialize(cfg);
        }
    }
    return *s;
}

void Steam::RunCallbacks() {
    if (Service()) Service()->RunCallbacks();
}

void Steam::Shutdown() {
    if (Service()) { Service()->Shutdown(); Service().reset(); }
}

} // namespace okay

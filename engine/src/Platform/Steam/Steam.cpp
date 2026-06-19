#include "okay/Platform/Steam/Steam.hpp"
#include <memory>

namespace okay {

namespace {
    std::unique_ptr<ISteamService>& Service() {
        static std::unique_ptr<ISteamService> s;
        return s;
    }
    std::uint32_t& AppId() { static std::uint32_t id = 480; return id; }
}

void Steam::SetAppId(std::uint32_t appId) {
    if (!Service()) AppId() = appId;
}

bool Steam::Exists() { return Service() != nullptr; }

ISteamService& Steam::Get() {
    auto& s = Service();
    if (!s) {
        s = CreateSteamService();
        SteamConfig cfg; cfg.appId = AppId();
        s->Initialize(cfg);
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

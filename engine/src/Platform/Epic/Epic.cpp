#include "okay/Platform/Epic/Epic.hpp"
#include "okay/Core/Log.hpp"

#include <cstring>

namespace okay {

namespace {
std::unique_ptr<IEpicService>& Service() { static std::unique_ptr<IEpicService> s; return s; }
EpicConfig& ConfigRef() { static EpicConfig c; return c; }
std::function<std::unique_ptr<IEpicService>()>& Factory() { static std::function<std::unique_ptr<IEpicService>()> f; return f; }
bool& RuntimeLibRef() { static bool on = false; return on; }
std::string& RuntimeLibPathRef() { static std::string p; return p; }
}  // namespace

void Epic::Configure(const EpicConfig& config) { if (!Service()) ConfigRef() = config; }
const EpicConfig& Epic::Config() { return ConfigRef(); }
void Epic::EnableRuntimeLibrary(bool on, const std::string& libraryPath) {
    if (Service()) return;
    RuntimeLibRef() = on; RuntimeLibPathRef() = libraryPath;
}
void Epic::SetFactory(std::function<std::unique_ptr<IEpicService>()> factory) { Factory() = std::move(factory); }
bool Epic::Exists() { return Service() != nullptr; }

IEpicService& Epic::Get() {
    auto& s = Service();
    if (!s) {
        if (Factory()) s = Factory()();
        else if (RuntimeLibRef()) {
            // The Store Kit: the developer's own EOS SDK library beside the game (only when an Epic build asked for it).
            s = CreateDynamicEpicService(RuntimeLibPathRef());
            if (!s) OKAY_INFO("Epic: ", RuntimeLibPathRef().empty() ? std::string(DefaultEosLibraryName()) : RuntimeLibPathRef(),
                              " was not found beside the game; using the simulation");
        }
        if (!s) s = CreateEpicService();
        if (!s) s = CreateNullEpicService();
        if (!s->Initialize(ConfigRef()) && std::strcmp(s->BackendName(), "null") != 0) {
            OKAY_WARN("Epic: the EOS platform could not start; using the simulation (nothing reaches Epic)");
            s->Shutdown();
            s = CreateNullEpicService();
            s->Initialize(ConfigRef());
        }
    }
    return *s;
}

void Epic::Tick() { if (Service()) Service()->Tick(); }

void Epic::Shutdown() {
    if (Service()) { Service()->Shutdown(); Service().reset(); }
}

}  // namespace okay

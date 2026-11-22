#include "okay/Platform/Epic/Epic.hpp"
#include "okay/Core/Log.hpp"

#include <cstring>

namespace okay {

namespace {
std::unique_ptr<IEpicService>& Service() { static std::unique_ptr<IEpicService> s; return s; }
EpicConfig& ConfigRef() { static EpicConfig c; return c; }
std::function<std::unique_ptr<IEpicService>()>& Factory() { static std::function<std::unique_ptr<IEpicService>()> f; return f; }
}  // namespace

void Epic::Configure(const EpicConfig& config) { if (!Service()) ConfigRef() = config; }
const EpicConfig& Epic::Config() { return ConfigRef(); }
// Not written yet (the failing tests come first).
void Epic::EnableRuntimeLibrary(bool, const std::string&) {}
const char* DefaultEosLibraryName() { return "EOSSDK-Win64-Shipping.dll"; }
std::unique_ptr<IEpicService> CreateDynamicEpicService(const std::string&) { return nullptr; }
std::string DeriveEosEncryptionKey(const std::string&, const std::string&) { return {}; }

void Epic::SetFactory(std::function<std::unique_ptr<IEpicService>()> factory) { Factory() = std::move(factory); }
bool Epic::Exists() { return Service() != nullptr; }

IEpicService& Epic::Get() {
    auto& s = Service();
    if (!s) {
        s = Factory() ? Factory()() : CreateEpicService();
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

// module:sky_weather — DayNightCycle's inspector fields are registered by the module.
// With the module on, "time" is there. After the property table is rebuilt with the
// module off, those fields are not registered again by the central gameplay table.
#include "test_framework.hpp"
#include "okay/Modules/ModuleRegistry.hpp"
#include "okay/Scene/PropertyRegistry.hpp"
#include <iostream>

using namespace okay;

static bool HasField(const ComponentTypeInfo& info, const char* name) {
    for (const PropertyDesc& d : info.properties) if (d.name == name) return true;
    return false;
}

int main() {
    RUN_SUITE("module:sky_weather");
    ModuleRegistry::Get();
    RegisterBuiltinProperties();
    auto on = PropertyRegistry::FindByName("DayNightCycle");
    CHECK(on && HasField(*on, "time") && HasField(*on, "dayLengthSeconds"));
    const int fields = (int)on->properties.size();
    std::cout << "sky_weather on: DayNightCycle fields " << fields << " (includes time)\n";

    // Rebuilding the table must register the fields again while the module stays on.
    // A one-shot hook would leave DayNightCycle missing here.
    PropertyRegistry::Clear();
    RegisterBuiltinProperties();
    auto rebuilt = PropertyRegistry::FindByName("DayNightCycle");
    CHECK(rebuilt && (int)rebuilt->properties.size() == fields && HasField(*rebuilt, "time"));
    std::cout << "sky_weather on, table rebuilt: DayNightCycle fields " << rebuilt->properties.size() << "\n";

    PropertyRegistry::Clear();
    ModuleManifest saved = ModuleRegistry::Get().Find("sky_weather")->manifest;
    ModuleRegistry::Get().Add(saved, false);
    RegisterBuiltinProperties();
    auto off = PropertyRegistry::FindByName("DayNightCycle");
    CHECK(!off);
    std::cout << "sky_weather off: DayNightCycle has no property table\n";

    PropertyRegistry::Clear();
    ModuleRegistry::Get().Add(saved, true);
    RegisterBuiltinProperties();
    auto back = PropertyRegistry::FindByName("DayNightCycle");
    CHECK(back && HasField(*back, "time"));
    std::cout << "sky_weather on again: DayNightCycle fields " << back->properties.size() << "\n";
    TEST_MAIN_RESULT();
}

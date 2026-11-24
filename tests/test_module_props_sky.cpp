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
    std::cout << "sky_weather on: DayNightCycle fields " << on->properties.size() << " (includes time)\n";

    PropertyRegistry::Clear();
    ModuleManifest saved = ModuleRegistry::Get().Find("sky_weather")->manifest;
    ModuleRegistry::Get().Add(saved, false);
    RegisterBuiltinProperties();
    auto off = PropertyRegistry::FindByName("DayNightCycle");
    CHECK(!off);
    std::cout << "sky_weather off: DayNightCycle has no property table\n";

    ModuleRegistry::Get().Add(saved, true);
    TEST_MAIN_RESULT();
}

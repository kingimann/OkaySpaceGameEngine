// module:weapons — the Add Component list comes from the property registry and drops
// Crosshair when the weapons module is off.
#include "test_framework.hpp"
#include "okay/Modules/ModuleRegistry.hpp"
#include "okay/Modules/ModuleMenus.hpp"
#include <iostream>

using namespace okay;

static bool Has(const std::vector<AddComponentRow>& rows, const char* type) {
    for (const AddComponentRow& r : rows) if (r.type == type) return true;
    return false;
}

int main() {
    RUN_SUITE("module:weapons");
    auto on = AddComponentRows();
    CHECK(Has(on, "Crosshair"));
    int shown = 0;
    for (const AddComponentRow& r : on) if (r.module == "weapons") ++shown;
    std::cout << "weapons on: Add Component";
    for (const AddComponentRow& r : on) if (r.module == "weapons") std::cout << " [" << r.type << "]";
    std::cout << "\n";

    ModuleManifest saved = ModuleRegistry::Get().Find("weapons")->manifest;
    ModuleRegistry::Get().Add(saved, false);
    auto off = AddComponentRows();
    CHECK(!Has(off, "Crosshair"));
    std::cout << "weapons off: Add Component";
    for (const AddComponentRow& r : off) if (r.module == "weapons") std::cout << " [" << r.type << "]";
    std::cout << " (no weapons rows)\n";
    ModuleRegistry::Get().Add(saved, true);
    TEST_MAIN_RESULT();
}

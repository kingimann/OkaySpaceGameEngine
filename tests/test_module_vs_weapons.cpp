// module:weapons — Swing/Fire/Reload/Ammo/Set come from the visual-script node registry
// and are hidden when the weapons module is off. With the module on they still build.
#include "test_framework.hpp"
#include "okay/Modules/ModuleRegistry.hpp"
#include "okay/VisualScript/NodeGraph.hpp"
#include "okay/VisualScript/Nodes.hpp"
#include "okay/VisualScript/VsNodeRegistry.hpp"
#include <iostream>
#include <string>

using namespace okay;

static bool Listed(const std::vector<std::string>& v, const char* s) {
    for (const std::string& x : v) if (x == s) return true;
    return false;
}

int main() {
    RUN_SUITE("module:weapons");
    ModuleRegistry::Get();
    std::string err;
    auto on = vs::MakeNode("ReloadWeapon", {}, &err);
    CHECK(on != nullptr);
    auto ammo = vs::MakeNode("WeaponAmmo", {}, &err);
    CHECK(ammo != nullptr);
    auto set = vs::MakeNode("SetWeapon", {"bow"}, &err);
    CHECK(set != nullptr);
    const vs::VsNodeEntry* entry = vs::FindRegisteredVsNode("FireWeapon");
    CHECK(entry && entry->module == "weapons");
    auto shown = vs::RegisteredVsNodeTypes(true);
    CHECK(Listed(shown, "SwingWeapon") && Listed(shown, "SetWeapon"));
    std::cout << "weapons on:";
    for (const std::string& n : shown) std::cout << " " << n;
    std::cout << "\n";

    ModuleManifest saved = ModuleRegistry::Get().Find("weapons")->manifest;
    ModuleRegistry::Get().Add(saved, false);
    err.clear();
    auto off = vs::MakeNode("ReloadWeapon", {}, &err);
    CHECK(off == nullptr);
    CHECK(err.find("weapons") != std::string::npos);
    auto hidden = vs::RegisteredVsNodeTypes(true);
    CHECK(!Listed(hidden, "ReloadWeapon") && !Listed(hidden, "WeaponAmmo"));
    CHECK(vs::VsNodeSuppressed("ReloadWeapon"));
    std::cout << "weapons off:";
    for (const std::string& n : hidden) std::cout << " " << n;
    std::cout << " (ReloadWeapon hidden)\n";

    ModuleRegistry::Get().Add(saved, true);
    err.clear();
    CHECK(vs::MakeNode("FireWeapon", {}, &err) != nullptr);
    TEST_MAIN_RESULT();
}

// module:weapons — weapon_fire / weapon_reload / weapon_ammo / weapon_swing live on the
// shared script table. A new script VM sees them only while the weapons module is on.
#include "test_framework.hpp"
#include "okay/Modules/ModuleRegistry.hpp"
#include "okay/Scripting/OkayScriptVM.hpp"
#include "okay/Scripting/ScriptApiRegistry.hpp"
#include <iostream>
#include <string>

using namespace okay;

static bool Has(const std::vector<std::string>& v, const char* s) {
    for (const std::string& x : v) if (x == s) return true;
    return false;
}

int main() {
    RUN_SUITE("module:weapons");
    ModuleRegistry::Get();
    const ScriptApiEntry* fire = FindScriptApi("weapon_fire");
    CHECK(fire && fire->module == "weapons");
    OkayScriptVM on;
    CHECK(Has(on.BuiltinNames(), "weapon_fire"));
    CHECK(Has(on.BuiltinNames(), "weapon_swing"));
    CHECK(Has(on.BuiltinNames(), "weapon_reload"));
    CHECK(Has(on.BuiltinNames(), "weapon_ammo"));
    std::string err;
    vs::VsValue none = on.CallBuiltin("weapon_fire", {}, &err);
    CHECK(err.empty() && none.AsFloat() == 0.0f);
    std::cout << "weapons on: weapon_fire weapon_swing weapon_reload weapon_ammo\n";

    ModuleManifest saved = ModuleRegistry::Get().Find("weapons")->manifest;
    ModuleRegistry::Get().Add(saved, false);
    OkayScriptVM off;
    CHECK(!Has(off.BuiltinNames(), "weapon_fire"));
    err.clear();
    off.CallBuiltin("weapon_fire", {}, &err);
    CHECK(!err.empty());
    std::cout << "weapons off: weapon_fire is not a script command\n";
    ModuleRegistry::Get().Add(saved, true);
    TEST_MAIN_RESULT();
}

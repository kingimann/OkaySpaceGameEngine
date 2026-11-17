#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Testing/SceneValidator.hpp"
#include "okay/Components/DayNightCycle.hpp"
#ifdef OKAY_TEST_ALL_MODULES
#include "okay/Components/Ragdoll.hpp"
#endif
#include "okay/Scene/PropertyRegistry.hpp"
#include "okay/Scene/SceneRules.hpp"
#include "okay/Scripting/ScriptApiRegistry.hpp"

using namespace okay;

static bool Has(const char* name) {
    for (auto& e : ScriptApis()) if (e.first == name) return true;
    return false;
}

int main() {
    RUN_SUITE("module_script_api");
    auto& reg = ModuleRegistry::Get();
    const bool sky = reg.IsEnabled("sky_weather"), rag = reg.IsEnabled("ragdoll");
    CHECK(Has("time_of_day") == sky && Has("ragdoll") == rag && Has("ragdoll_push") == rag && Has("is_ragdoll") == rag);
    CHECK((PropertyRegistry::FindByName("DayNightCycle") != nullptr) == sky);
    bool rule = false;
    for (auto& r : rules::SingletonRules()) if (r.name == "DayNightCycle") rule = true;
    CHECK(rule == sky);

    if (sky) {
        Scene s("T");
        GameObject* g = s.CreateGameObject("Sky");
        auto* dn = g->AddComponent<DayNightCycle>();
        ScriptHost host; host.gameObject = g; host.transform = g->transform;
        OkayScriptVM vm;
        vm.Bind(&host);
        s.Start();
        CHECK(vm.CallBuiltin("time_of_day", {vs::VsValue{6.0f}}).AsFloat() == 6.0f);
        CHECK(dn->Hour() == 6.0f);
        // two cycles in a scene: the validator warns (the rule moved to the module)
        s.CreateGameObject("Sky2")->AddComponent<DayNightCycle>();
        auto report = testing::SceneValidator::Validate(s);
        bool warned = false;
        for (auto& i : report.issues) if (i.code == "component.duplicate_singleton") warned = true;
        CHECK(warned);
        CHECK(PropertyRegistry::FindByName("DayNightCycle")->name == "DayNightCycle");
    }
#ifdef OKAY_TEST_ALL_MODULES
    if (rag) {
        Scene s("T");
        GameObject* g = s.CreateGameObject("Body");
        g->AddComponent<Ragdoll>();
        ScriptHost host; host.gameObject = g; host.transform = g->transform;
        OkayScriptVM vm;
        vm.Bind(&host);
        CHECK(vm.CallBuiltin("is_ragdoll", {}).AsFloat() == 0.0f);
    } else
#endif
    {
        OkayScriptVM vm;
        std::string err;
        vm.CallBuiltin("is_ragdoll", {}, &err);
        CHECK(!err.empty());   // the function does not exist in a build without the module
    }
    TEST_MAIN_RESULT();
}

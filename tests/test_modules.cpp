// Engine modules, stage 0: the manifest parser, a small hand-made registry (order, requirements, problems) and the
// real list the build generated from engine/modules/*/module.okaymodule (modules_enabled.cpp).
#include "test_framework.hpp"
#include "okay/Modules/ModuleRegistry.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

using namespace okay;

static bool Has(const std::vector<std::string>& v, const char* s) { return std::find(v.begin(), v.end(), s) != v.end(); }
static int IndexOf(const std::vector<std::string>& v, const char* s) {
    return int(std::find(v.begin(), v.end(), s) - v.begin());
}

static int g_hookRuns = 0;
static void CountingHook(ModuleContext& c) { ++g_hookRuns; CHECK(c.registry.Find("core") != nullptr); }
static int g_throwRuns = 0, g_laterRuns = 0;
static void LaterHook(ModuleContext&) { ++g_laterRuns; }
static void ThrowingHook(ModuleContext&) { ++g_throwRuns; throw std::runtime_error("hook failed on purpose"); }

int main() {
    RUN_SUITE("modules");

    // ---- manifest parsing
    {
        ModuleManifest m; std::string err;
        CHECK(ModuleManifest::Parse("# c\nname=weapons\ntitle=Weapons\nversion=3\ndefault=off\nrequires=a, b\n"
                                    "provides=IWeapon\ncomponents=X,Y\nserializers=x\nvs_nodes=Fire\nvs_group=weapons\n"
                                    "editor=Win\nsources=a/* b/c.hpp\nfuture_key=1\n", m, &err));
        CHECK(m.name == "weapons" && m.title == "Weapons" && m.version == 3 && !m.defaultOn && !m.core);
        CHECK(m.requires_.size() == 2 && m.requires_[1] == "b");
        CHECK(m.components.size() == 2 && m.vsNodes.size() == 1 && m.vsGroup == "weapons");
        CHECK(m.sources.size() == 2 && m.sources[1] == "b/c.hpp");
        CHECK(!ModuleManifest::Parse("title=x\n", m, &err) && !err.empty());   // no name
        CHECK(!ModuleManifest::Parse("name=Bad Name\n", m, &err));            // not an id
        CHECK(ModuleManifest::Parse("name=c\ncore=1\ndefault=off\n", m) && m.core && m.defaultOn);   // core is always on
        CHECK(ModuleManifest::Parse("name=t\n", m) && m.title == "t");        // the title falls back to the name
        CHECK(ModuleManifest::SplitList(" a, ,b ,") == std::vector<std::string>({"a", "b"}));
    }

    // ---- a hook that throws: the registry finishes, that module and what needs it are left off, the rest register
    {
        ModuleRegistry r = ModuleRegistry::Empty();
        r.AddText("name=core\ncore=1\n", true);
        r.AddText("name=broken\nrequires=core\n", true);
        r.AddText("name=needsbroken\nrequires=broken\n", true);
        r.AddText("name=fine\nrequires=core\n", true);
        r.SetRegisterHook("broken", &ThrowingHook);
        r.SetRegisterHook("needsbroken", &LaterHook);
        r.SetRegisterHook("fine", &LaterHook);
        bool escaped = false;
        try { r.RunRegisterHooks(); r.RunRegisterHooks(); } catch (...) { escaped = true; }
        CHECK(!escaped);                                   // first use (ModuleRegistry::Get) must be able to finish
        CHECK(g_throwRuns == 1);                           // not tried again
        CHECK(!r.IsEnabled("broken") && !r.IsEnabled("needsbroken"));
        CHECK(r.IsEnabled("fine") && r.IsEnabled("core"));
        CHECK(g_laterRuns == 1);                            // only `fine` registered
        std::cout << "throwing hook: escaped " << escaped << ", broken on " << r.IsEnabled("broken") << ", needsbroken on "
                  << r.IsEnabled("needsbroken") << ", other hooks run " << g_laterRuns << "\n";
    }

    // ---- a small registry: order, requirements, hooks, problems
    {
        ModuleRegistry r = ModuleRegistry::Empty();
        r.AddText("name=core\ncore=1\n", false);
        r.AddText("name=health\nrequires=core\ncomponents=HealthStat\nserializers=stat_health\n", true);
        r.AddText("name=weapons\nrequires=health,physics\ncomponents=Weapon\n", true);
        r.AddText("name=physics\nrequires=core\n", true);
        r.AddText("name=inv\nrequires=weapons\n", true);
        CHECK(r.Validate().empty());
        CHECK(r.IsEnabled("core") && !r.IsEnabled("nope"));                   // core is on even when added as off
        auto order = r.DependencyOrder();
        CHECK(order.size() == 5);
        CHECK(IndexOf(order, "core") < IndexOf(order, "health"));
        CHECK(IndexOf(order, "physics") < IndexOf(order, "weapons"));
        CHECK(IndexOf(order, "weapons") < IndexOf(order, "inv"));
        auto need = r.TransitiveRequires("inv");
        CHECK(Has(need, "weapons") && Has(need, "health") && Has(need, "physics") && Has(need, "core") && !Has(need, "inv"));
        CHECK(r.Summary() == "5 modules, 5 on");

        r.SetRegisterHook("weapons", &CountingHook);
        r.RunRegisterHooks();
        r.RunRegisterHooks();                                                  // each hook runs once
        CHECK(g_hookRuns == 1);

        r.AddText("name=a\nrequires=b\n", true);
        r.AddText("name=b\nrequires=a\n", true);
        r.AddText("name=dup\ncomponents=HealthStat\n", true);
        r.AddText("name=lost\nrequires=missing\n", true);
        r.AddText("name=c2\ncore=1\nrequires=physics\n", true);
        auto problems = r.Validate();
        bool cyc = false, dup = false, lost = false, coreNeeds = false;
        for (auto& p : problems) {
            if (p.find("cycle") != std::string::npos) cyc = true;
            if (p.find("claimed by") != std::string::npos) dup = true;
            if (p.find("unknown module 'missing'") != std::string::npos) lost = true;
            if (p.find("core module 'c2' requires non-core") != std::string::npos) coreNeeds = true;
        }
        CHECK(cyc && dup && lost && coreNeeds);
        CHECK(r.DependencyOrder().size() == r.All().size());                  // a cycle must not hang or drop modules
    }

    // ---- the real list the build made from engine/modules (modules_enabled.cpp)
    {
        ModuleRegistry& r = ModuleRegistry::Get();
        CHECK(&r == &ModuleRegistry::Get());
        CHECK(r.All().size() >= 30);
        auto problems = r.Validate();
        for (auto& p : problems) std::cerr << "  problem: " << p << "\n";
        CHECK(problems.empty());
        CHECK(r.Find("core") && r.Find("core")->manifest.core);
        CHECK(r.Find("weapons") && Has(r.Find("weapons")->manifest.requires_, "health_damage"));
        for (auto& e : r.All()) CHECK(e.enabled);                             // stage 0: every module is on
        CHECK(r.DependencyOrder().size() == r.All().size());
    }
    TEST_MAIN_RESULT();
}

#include "test_framework.hpp"
#include "okay/Modules/ModuleRegistry.hpp"
#include <algorithm>

using namespace okay;

static bool Has(const std::vector<std::string>& v, const char* s) { return std::find(v.begin(), v.end(), s) != v.end(); }
static int IndexOf(const std::vector<std::string>& v, const char* s) {
    return int(std::find(v.begin(), v.end(), s) - v.begin());
}

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
        CHECK(!ModuleManifest::Parse("title=x\n", m, &err) && !err.empty());
        CHECK(!ModuleManifest::Parse("name=Bad Name\n", m, &err));
        CHECK(ModuleManifest::Parse("name=c\ncore=1\ndefault=off\n", m) && m.core && m.defaultOn);
    }

    // ---- a small registry: order, requirements, resolve, cycles
    {
        ModuleRegistry r = ModuleRegistry::Empty();
        r.AddText("name=core\ncore=1\n", true);
        r.AddText("name=health\nrequires=core\nprovides=IHealth\ncomponents=HealthStat\nserializers=stat_health\n", true);
        r.AddText("name=weapons\nrequires=health,physics\ncomponents=Weapon\n", false);
        r.AddText("name=physics\nrequires=core\n", false);
        r.AddText("name=inv\nrequires=weapons\n", false);
        CHECK(r.Validate().empty());
        auto order = r.DependencyOrder();
        CHECK(IndexOf(order, "core") < IndexOf(order, "health"));
        CHECK(IndexOf(order, "physics") < IndexOf(order, "weapons"));
        CHECK(IndexOf(order, "weapons") < IndexOf(order, "inv"));
        auto need = r.TransitiveRequires("inv");
        CHECK(Has(need, "weapons") && Has(need, "health") && Has(need, "physics") && Has(need, "core"));
        CHECK(Has(r.RequiredBy("health"), "weapons") && Has(r.RequiredBy("health"), "inv"));
        CHECK(Has(r.MissingRequires("weapons"), "physics") && !Has(r.MissingRequires("weapons"), "health"));
        CHECK(r.ModuleProviding("IHealth") && r.ModuleProviding("IHealth")->manifest.name == "health");
        CHECK(r.ComponentEnabled("HealthStat") && !r.ComponentEnabled("Weapon") && r.ComponentEnabled("NoSuchThing"));
        CHECK(r.RecordEnabled("stat_health") && r.ModuleOfRecord("stat_health")->manifest.name == "health");

        std::vector<std::string> added, unknown;
        auto set = r.Resolve({"inv", "nope"}, &added, &unknown);
        CHECK(Has(set, "inv") && Has(set, "weapons") && Has(set, "physics") && Has(set, "core"));
        CHECK(Has(added, "weapons") && Has(added, "physics") && !Has(added, "inv") && !Has(added, "core"));
        CHECK(unknown.size() == 1 && unknown[0] == "nope");
        r.SetEnabledSet({"weapons"});
        CHECK(r.IsEnabled("weapons") && r.IsEnabled("health") && r.IsEnabled("physics") && !r.IsEnabled("inv"));
        r.SetEnabledSet({});
        CHECK(r.IsEnabled("core") && !r.IsEnabled("health"));   // core can never be turned off

        r.AddText("name=a\nrequires=b\n", true);
        r.AddText("name=b\nrequires=a\n", true);
        r.AddText("name=dup\ncomponents=HealthStat\n", true);
        r.AddText("name=lost\nrequires=missing\n", true);
        auto problems = r.Validate();
        bool cyc = false, dup = false, lost = false;
        for (auto& p : problems) {
            if (p.find("cycle") != std::string::npos) cyc = true;
            if (p.find("claimed by") != std::string::npos) dup = true;
            if (p.find("unknown module 'missing'") != std::string::npos) lost = true;
        }
        CHECK(cyc && dup && lost);
        CHECK(r.DependencyOrder().size() == r.All().size());   // a cycle must not hang or drop modules
    }

    // ---- the real list built from engine/modules (modules_enabled.cpp)
    {
        ModuleRegistry& r = ModuleRegistry::Get();
        CHECK(r.All().size() >= 30);
        auto problems = r.Validate();
        for (auto& p : problems) std::cerr << "  problem: " << p << "\n";
        CHECK(problems.empty());
        CHECK(r.IsEnabled("core"));
        const auto* hd = r.ModuleOfComponent("HealthStat");
        CHECK(hd && hd->manifest.name == "health_damage");
        CHECK(r.ModuleProviding("Damage"));   // the one damage path, okay/Components/Damage.hpp
        CHECK(r.ModuleOfRecord("stat_health") != nullptr);
        CHECK(r.Find("weapons") && Has(r.Find("weapons")->manifest.requires_, "health_damage"));
        std::string sel = detail::ConfiguredModuleSelection();
        if (sel == "all") {
            // default build: nothing is off, so nothing changes for anyone
            for (auto& e : r.All()) CHECK(e.enabled);
        } else {
            for (auto& e : r.All()) CHECK(!e.enabled || r.MissingRequires(e.manifest.name).empty());
        }
        // every module's requirements, resolved, are a superset that includes core
        for (auto& e : r.All()) CHECK(Has(r.Resolve({e.manifest.name}), "core"));
    }
    TEST_MAIN_RESULT();
}

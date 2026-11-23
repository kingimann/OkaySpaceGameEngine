#include "test_framework.hpp"
#include "ModulesModel.hpp"
#include "ProjectState.hpp"
#include <Okay.hpp>
#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>

using namespace okaymod;
using namespace oked;
using okay::ModuleRegistry;

static bool Has(const std::vector<std::string>& v, const char* s) { return std::find(v.begin(), v.end(), s) != v.end(); }

int main() {
    RUN_SUITE("modules_model");
    ModuleRegistry& reg = ModuleRegistry::Get();

    // ---- every preset names real modules and resolves cleanly
    for (auto& p : Presets()) {
        std::vector<std::string> unknown;
        auto set = reg.Resolve(p.modules, nullptr, &unknown);
        if (!unknown.empty()) std::cerr << "  preset " << p.id << " names unknown module " << unknown[0] << "\n";
        CHECK(unknown.empty());
        CHECK(Has(set, "core"));
    }

    // ---- an old project (no modules line) has every module on and writes no line
    {
        Selection sel(reg, "");
        CHECK(sel.AllOn() && sel.ToText().empty() && sel.MatchingPreset() == "full");
        ProjectSettings ps;
        CHECK(WriteProjectSettings(ps, "").find("modules=") == std::string::npos);
    }

    // ---- ticking a module ticks what it needs; unticking one that is needed is refused with the reason
    {
        Selection sel(reg, "physics3d");                  // core + physics3d (+ what it needs) only
        CHECK(sel.IsOn("physics3d") && !sel.IsOn("weapons") && !sel.AllOn());
        auto c = sel.Set("weapons", true);
        CHECK(c.ok && Has(c.alsoOn, "health_damage") && sel.IsOn("health_damage") && sel.IsOn("weapons"));
        auto off = sel.Set("health_damage", false);
        CHECK(!off.ok && Has(off.blockedBy, "weapons") && sel.IsOn("health_damage"));
        CHECK(sel.Set("weapons", false).ok);
        CHECK(sel.Set("health_damage", false).ok && !sel.IsOn("health_damage"));
        CHECK(!sel.Set("no_such_module", true).ok);
        CHECK(sel.IsOn("core"));                           // core cannot be switched off
        auto rows = sel.Rows();
        CHECK(!rows.empty());
        CHECK(rows[0].name == "core" && rows[0].on && rows[0].locked && !rows[0].lockReason.empty());   // core: first, ticked, greyed
        CHECK(!sel.Set("core", false).ok && sel.IsOn("core"));
        sel.Set("weapons", true);
        bool sawLocked = false;
        for (auto& r : sel.Rows()) if (r.name == "health_damage") sawLocked = r.locked && r.lockReason.find("weapons") != std::string::npos;
        CHECK(sawLocked);
        CHECK(sel.NeedsText([&] { for (auto& r : sel.Rows()) if (r.name == "weapons") return r; return Row(); }()).find("Health and damage") != std::string::npos);
    }

    // ---- presets: apply, recognise, save and load
    {
        Selection sel(reg, "");
        CHECK(sel.ApplyPreset("2d"));
        CHECK(sel.IsOn("physics2d") && sel.IsOn("tilemap2d") && !sel.IsOn("physics3d") && !sel.IsOn("weapons"));
        CHECK(sel.MatchingPreset() == "2d");
        std::string text = sel.ToText();
        CHECK(!text.empty() && text.find("core") == std::string::npos);
        Selection back(reg, text);
        CHECK(back.OnList() == sel.OnList());
        sel.Set("audio", false);
        CHECK(sel.MatchingPreset().empty() || sel.MatchingPreset() != "2d");
        CHECK(!sel.ApplyPreset("nope"));
        Selection surv(reg, ""); surv.ApplyPreset("survival");
        CHECK(surv.IsOn("survival") && surv.IsOn("inventory") && surv.IsOn("health_damage") && !surv.IsOn("networking"));
        // modules a newer editor knew stay in the saved text
        Selection fut(reg, "physics3d,quantum_physics");
        CHECK(Has(fut.Unknown(), "quantum_physics") && fut.ToText().find("quantum_physics") != std::string::npos);
    }

    // ---- project.okayproj round trip
    {
        ProjectSettings ps;
        ps.modules = "physics3d,health_damage,weapons";
        std::string text = WriteProjectSettings(ps, "");
        CHECK(text.find("modules=physics3d,health_damage,weapons") != std::string::npos);
        ProjectSettings back;
        ParseProjectSettings(back, text);
        CHECK(back.modules == ps.modules);
        ProjectSettings old;
        ParseProjectSettings(old, "company=Acme\nversion=1.2.3\n");
        CHECK(old.modules.empty());
    }

    // ---- the preset each New Project template starts with is a real preset; unknown templates get every module
    {
        for (const auto& t : okayproj::Templates()) {
            const std::string id = PresetForTemplate(t.title);
            Selection sel(reg, "");
            CHECK(sel.ApplyPreset(id));
        }
        CHECK(PresetForTemplate("2D Scene") == "2d" && PresetForTemplate("Something New") == "full");
        Selection two(reg, ""); two.ApplyPreset(PresetForTemplate("2D Scene"));
        CHECK(two.IsOn("physics2d") && !two.ToText().empty());
    }

    // ---- the Add Component menu: every row of a module's component is recognised as that module's (editor/main.cpp)
    {
        std::ifstream f(std::string(OKAY_SOURCE_DIR) + "/editor/main.cpp", std::ios::binary);
        std::stringstream ss; ss << f.rdbuf();
        const std::string src = ss.str();
        CHECK(src.size() > 100000);
        const std::regex row(R"(item\(\s*!go->GetComponent<(\w+)>\(\)[^"]*"([^"]+)")");
        int rows = 0, moduleRows = 0, wrong = 0;
        for (std::sregex_iterator it(src.begin(), src.end(), row), end; it != end; ++it) {
            ++rows;
            const std::string type = (*it)[1], label = (*it)[2];
            const auto* owner = reg.ModuleOfComponent(type);
            const auto* guess = reg.ModuleOfComponent(TypeForMenuLabel(label));
            if (owner) ++moduleRows;
            // a module's row must be hidden with its module; a row must never be hidden with another module
            if ((owner && (!guess || guess != owner)) || (!owner && guess)) {
                ++wrong;
                std::cerr << "  Add Component row \"" << label << "\" adds " << type << " but maps to "
                          << (guess ? guess->manifest.name : std::string("no module")) << "\n";
            }
        }
        CHECK(rows > 150 && moduleRows > 100);
        CHECK(wrong == 0);
        Selection sel(reg, "physics3d");
        const auto off = OffComponentTypes(sel, reg);
        CHECK(off.count("Rigidbody2D") && !off.count("Joint3D"));
        CHECK(off.count(TypeForMenuLabel("Box Collider 2D")) && off.count(TypeForMenuLabel("Day / Night Cycle")));
    }

    // ---- what the open scene uses
    {
        okay::Scene s("T");
        s.CreateGameObject("A")->AddComponent<okay::HealthStat>();
        s.CreateGameObject("B")->AddComponent<okay::HealthStat>();
        s.CreateGameObject("C")->AddComponent<okay::Seat>();
        auto usage = UsageInScene(reg, s);
        CHECK(usage["health_damage"] == 2 && usage["vehicles"] == 1);
        Selection sel(reg, "physics3d");
        auto off = UsedButOff(sel, usage);
        bool vehicles = false;
        for (auto& kv : off) if (kv.first == "vehicles") vehicles = true;
        CHECK(vehicles);
    }
    TEST_MAIN_RESULT();
}

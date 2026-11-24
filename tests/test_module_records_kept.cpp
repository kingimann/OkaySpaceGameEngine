// Review fix for PR #1226: a scene record whose module is off (or whose reader is missing) must not be
// lost by load then save. The line is kept as it was read, like a newer build's unknown record, and the
// component comes back with its values when the module is on again. Covers every record the scene chain
// hands to a module: crosshair (weapons), vehicle, vehicletune, vehiclesusp, vehicle2d (vehicles), survival.
#include "test_framework.hpp"
#include "okay/Modules/ModuleRegistry.hpp"
#include "okay/Scene/ComponentSerializer.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Components/Crosshair.hpp"
#include "okay/Components/SurvivalStats.hpp"
#include "okay/Components/VehicleController.hpp"
#include "okay/Components/VehicleController2D.hpp"
#include <iostream>
#include <string>
#include <vector>

using namespace okay;

namespace {

// The whole "  <record> ..." line of `text` (without its line break), or "" when there is none.
std::string SavedRecordText(const std::string& text, const std::string& record) {
    const auto at = text.find("\n  " + record + " ");
    if (at == std::string::npos) return "";
    const auto end = text.find('\n', at + 1);
    return text.substr(at + 1, end == std::string::npos ? std::string::npos : end - at - 1);
}

bool HasWholeLine(const std::string& text, const std::string& line) {
    return !line.empty() && text.find("\n" + line + "\n") != std::string::npos;
}

struct Gated {
    const char* module;
    std::vector<std::string> records;
    bool (*present)(Scene&);
};

bool HasCrosshair(Scene& s) {
    GameObject* go = s.Find("Hud");
    Crosshair* c = go ? go->GetComponent<Crosshair>() : nullptr;
    return c && c->gap == 8.0f && c->circle && !c->enabled;
}
bool HasVehicles(Scene& s) {
    GameObject* car = s.Find("Car");
    GameObject* kart = s.Find("Kart");
    VehicleController* v = car ? car->GetComponent<VehicleController>() : nullptr;
    VehicleController2D* v2 = kart ? kart->GetComponent<VehicleController2D>() : nullptr;
    return v && v->acceleration == 18.0f && v->suspension && v->rideHeight == 0.35f && v2 && v2->sideView && v2->maxSpeed == 22.0f;
}
bool HasSurvival(Scene& s) {
    GameObject* go = s.Find("Player");
    SurvivalStats* sv = go ? go->GetComponent<SurvivalStats>() : nullptr;
    return sv && sv->maxHunger == 80.0f && sv->godMode;
}
// Load `text`, save it again.
std::string Resave(const std::string& text, bool* loaded) {
    Scene s("resave");
    *loaded = SceneSerializer::Deserialize(s, text);
    return SceneSerializer::Serialize(s);
}

} // namespace

int main() {
    RUN_SUITE("module records are kept while their module is off");
    ModuleRegistry& mods = ModuleRegistry::Get();
    Scene scene("all");
    Crosshair* cr = scene.CreateGameObject("Hud")->AddComponent<Crosshair>();
    cr->gap = 8.0f; cr->circle = true; cr->spreadRecover = 12.0f;
    cr->enabled = false;   // the `compoff` line that belongs to the crosshair must survive too
    VehicleController* v = scene.CreateGameObject("Car")->AddComponent<VehicleController>();
    v->acceleration = 18.0f; v->wheelBase = 3.1f; v->suspension = true; v->rideHeight = 0.35f;
    VehicleController2D* v2 = scene.CreateGameObject("Kart")->AddComponent<VehicleController2D>();
    v2->maxSpeed = 22.0f; v2->sideView = true;
    SurvivalStats* sv = scene.CreateGameObject("Player")->AddComponent<SurvivalStats>();
    sv->maxHunger = 80.0f; sv->godMode = true;
    std::string first = SceneSerializer::Serialize(scene);
    // A record of a build newer than this one, on the same object as the crosshair (the reviewer's probe).
    const std::string future = "  futurething 1 2 3";
    const auto hudEnd = first.find("\nend\n", first.find("\n  crosshair "));
    CHECK(hudEnd != std::string::npos);
    if (hudEnd == std::string::npos) { TEST_MAIN_RESULT(); }
    first.insert(hudEnd + 1, future + "\n");
    {
        bool ok = false;
        CHECK(Resave(first, &ok) == first);   // with every module on the text is stable, future record included
        CHECK(ok);
    }

    const std::vector<Gated> gated = {
        {"weapons", {"crosshair", "compoff"}, &HasCrosshair},
        {"vehicles", {"vehicle", "vehicletune", "vehiclesusp", "vehicle2d"}, &HasVehicles},
        {"survival", {"survival"}, &HasSurvival},
    };
    for (const Gated& g : gated) {
        const ModuleManifest manifest = mods.Find(g.module)->manifest;
        mods.Add(manifest, false);
        Scene off("off");
        CHECK(SceneSerializer::Deserialize(off, first));
        CHECK(!g.present(off));
        const std::string saved = SceneSerializer::Serialize(off);
        for (const std::string& rec : g.records) {
            const std::string line = SavedRecordText(first, rec);
            CHECK(!line.empty());
            const bool kept = HasWholeLine(saved, line);
            CHECK(kept);
            std::cout << g.module << " off, load, save: " << rec << (kept ? " kept: " : " LOST: ") << line << "\n";
        }
        CHECK(HasWholeLine(saved, future));
        bool again = false;
        CHECK(Resave(saved, &again) == saved);   // a second load and save while off adds and loses nothing
        CHECK(again);
        mods.Add(manifest, true);
        Scene on("on");
        CHECK(SceneSerializer::Deserialize(on, saved));
        const bool back = g.present(on);
        CHECK(back);
        CHECK(SceneSerializer::Serialize(on) == first);   // every value, and each record at its usual place again
        std::cout << g.module << " on again: component " << (back ? "restored" : "MISSING") << "\n";
    }

    // The module is on but nothing registered a reader for its record (a hook that failed): the record's
    // numbers must not be read as the next field names. The record is kept like the ones above.
    {
        auto reader = ComponentSerializerRegistry::Find("crosshair");
        CHECK(reader != nullptr);
        ComponentSerializerRegistry::Unregister("crosshair");
        CHECK(ComponentSerializerRegistry::Find("crosshair") == nullptr);
        Scene s("no reader");
        CHECK(SceneSerializer::Deserialize(s, first));
        GameObject* hud = s.Find("Hud");
        CHECK(hud && hud->GetComponent<Crosshair>() == nullptr);
        CHECK(hud && hud->unknownRecords.size() == 3);   // crosshair, futurething, compoff: not one per number
        CHECK(HasVehicles(s) && HasSurvival(s));         // the rest of the file is read as usual
        const std::string saved = SceneSerializer::Serialize(s);
        const bool kept = HasWholeLine(saved, SavedRecordText(first, "crosshair"));
        CHECK(kept);
        std::cout << "weapons on, no crosshair reader: record " << (kept ? "kept" : "LOST") << ", "
                  << (hud ? hud->unknownRecords.size() : 0) << " kept lines on Hud\n";
        if (reader) ComponentSerializerRegistry::Register(*reader);
        Scene on("reader back");
        CHECK(SceneSerializer::Deserialize(on, saved));
        CHECK(HasCrosshair(on));
        CHECK(SceneSerializer::Serialize(on) == first);
    }
    TEST_MAIN_RESULT();
}

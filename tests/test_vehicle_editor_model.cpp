// Vehicle Inspector decisions (editor/VehicleEditorModel.hpp): combo lists, the Simple rows, the Advanced groups, key labels, and
// a save/load preset round trip like the Presets tab does. No window, no GPU.
#include "test_framework.hpp"
#include "VehicleEditorModel.hpp"
#include "okay/Components/VehicleController.hpp"
#include <algorithm>
#include <cstring>
#include <set>

using namespace okay;
namespace vm = oked::vehiclemodel;

int main() {
    RUN_SUITE("vehicle_editor_model");

    const std::string a = vm::ComboItems("Rear-wheel|Front-wheel|All-wheel");
    CHECK(a == std::string("Rear-wheel\0Front-wheel\0All-wheel\0", 33));
    CHECK(vm::ComboItems("") == std::string(1, '\0'));
    CHECK(vm::ComboItems(nullptr) == std::string(1, '\0'));
    for (const auto& d : VehicleTuning::Table())
        if (d.kind == VehicleTuning::KEnum) {
            const std::string s = vm::ComboItems(d.options);
            CHECK(s.size() >= 2 && s.back() == '\0');
            int n = 0; for (const char* p = s.c_str(); *p; p += std::strlen(p) + 1) ++n;
            const std::string o = d.options;
            CHECK(n == (int)std::count(o.begin(), o.end(), '|') + 1);
            CHECK(n >= 2);
        }

    for (const char* n : vm::SimpleRows()) {
        const VehicleTuning::Desc* d = VehicleTuning::Find(n);
        CHECK(d != nullptr);
        if (d) CHECK(std::string(d->group) == "Simple");
    }

    const auto groups = vm::AdvancedGroups();
    CHECK(groups.size() >= 5);
    std::set<std::string> names; size_t rows = 0, simple = 0;
    for (const auto& g : groups) { CHECK(names.insert(g.name).second); CHECK(!g.rows.empty()); rows += g.rows.size(); CHECK(g.name != "Simple"); }
    for (const auto& d : VehicleTuning::Table()) if (std::string(d.group) == "Simple") ++simple;
    CHECK(rows + simple == VehicleTuning::Table().size());
    for (const auto& d : VehicleTuning::Table()) { CHECK(d.tip && *d.tip); CHECK(d.hi >= d.lo); }

    CHECK(vm::KeyLabel('W') == "W");
    CHECK(vm::KeyLabel('7') == "7");
    CHECK(vm::KeyLabel(' ') == "#32");
    CHECK(vm::KeyLabel((char)200) == "#200");

    // Presets tab round trip: save text, change, load text.
    VehicleController v;
    v.ApplyPreset("Sedan");
    v.maxSpeed = 31.5f;
    const std::string txt = v.SavePresetText("Mine");
    VehicleController w;
    CHECK(w.LoadPresetText(txt));
    CHECK(w.maxSpeed == 31.5f);
    CHECK(!w.LoadPresetText("not a vehicle preset"));
    TEST_MAIN_RESULT();
}

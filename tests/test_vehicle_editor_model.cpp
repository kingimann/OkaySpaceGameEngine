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
    // Audit: every setting has a plain name and unit; the one Inspector shows about 15 settings, no Simple / Advanced split.
    CHECK(vm::Infos().size() == VehicleTuning::Table().size());
    size_t shown = 0, revealed = 0, hidden = 0;
    for (const auto& d : VehicleTuning::Table()) {
        const vm::Info* i = vm::InfoFor(d.name);
        CHECK(i != nullptr); if (!i) continue;
        CHECK(i->label && *i->label); CHECK(i->unit && *i->unit);
        if (i->level != vm::Level::Hidden) { CHECK(d.tip && *d.tip); CHECK(i->section && *i->section); bool known = false; for (const char* s : vm::Sections()) known = known || std::string(s) == i->section; CHECK(known); }
        if (i->level == vm::Level::Shown) ++shown; else if (i->level == vm::Level::Revealed) ++revealed; else ++hidden;
        if (std::string(d.name).rfind("key", 0) == 0 || std::string(d.name).rfind("pad", 0) == 0) CHECK(i->level == vm::Level::Hidden);
    }
    CHECK(shown + 1 <= 15);                    // +1: the "Camera follows when driven" checkbox that is not in the table
    CHECK(revealed <= 2); CHECK(vm::ShownRows().size() == shown);
    CHECK(shown + revealed + hidden == VehicleTuning::Table().size());
    CHECK(hidden >= 60);
    for (const char* s : vm::Sections()) CHECK(!vm::RowsOf(s, vm::Level::Shown).empty());
    CHECK(vm::RowsOf("Boost", vm::Level::Revealed).size() == revealed);
    // Hidden settings are still loadable by name and by file (old scenes keep every field).
    for (const auto& d : VehicleTuning::Table()) { VehicleTuning t0; double v0 = 0; CHECK(t0.GetSetting(d.name, v0)); CHECK(t0.SetSetting(d.name, v0)); }
    // Per-setting reset to the preset's value or the default.
    { VehicleTuning t; t.ApplyPreset("Sedan"); const float sedan = t.maxSpeed; t.maxSpeed = 99;
      CHECK(vm::IsChanged(t, "maxSpeed", "Sedan")); CHECK(!vm::IsChanged(t, "grip", "Sedan"));
      CHECK(vm::ResetSetting(t, "maxSpeed", "Sedan")); CHECK(t.maxSpeed == sedan); CHECK(!vm::IsChanged(t, "maxSpeed", "Sedan"));
      t.maxSpeed = 5; CHECK(vm::ResetSetting(t, "maxSpeed", "")); CHECK(t.maxSpeed == VehicleTuning().maxSpeed);
      CHECK(!vm::ResetSetting(t, "nonsense", "Sedan")); CHECK(!vm::IsChanged(t, "nonsense", "")); }
    // Controls tab text: every action has a plain label, bindings read as words, pad choices are all legal pad bindings, clashes are listed.
    {
        using namespace okay::drivectl;
        for (int a = 0; a < ActionCount; ++a) CHECK(std::string(vm::ActionLabel(a)).size() > 3);
        CHECK(vm::BindingLabel(Binding::K('w')) == "W"); CHECK(vm::BindingLabel(Binding::K(' ')) == "Space");
        CHECK(vm::BindingLabel(Binding::K((char)16)) == "Shift"); CHECK(vm::BindingLabel(Binding::M(1)) == "Mouse 2");
        CHECK(vm::BindingLabel(Binding::B(0)) == "Pad A"); CHECK(vm::BindingLabel(Binding::T(1)) == "Right trigger");
        CHECK(vm::BindingLabel(Binding::A(0, true)) == "Left stick left"); CHECK(vm::BindingLabel(Binding::A(3, false)) == "Right stick up");
        CHECK(vm::BindingLabel(Binding()) == "(none)"); CHECK(vm::BindingLabel(Binding::K(Input::KeyUp)) == "Up arrow");
        std::set<std::string> seen;
        for (const Binding& b : vm::PadChoices()) { CHECK(b.Valid()); CHECK(b.IsNone() || b.IsPad()); CHECK(seen.insert(vm::BindingLabel(b)).second); }
        CHECK(vm::PadChoices().size() == 1 + 2 + 14 + 8);
        ControlScheme st; Preset("Street", st); CHECK(vm::ConflictLines(st).empty());
        CaptureRebind(st, Horn, 0, Binding::K('l'));
        const auto lines = vm::ConflictLines(st);
        CHECK(lines.size() == 1); CHECK(lines.size() == 1 && lines[0] == "Horn and Headlights on / off both use L");
    }
    TEST_MAIN_RESULT();
}

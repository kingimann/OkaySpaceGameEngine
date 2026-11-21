#pragma once
// The pure (no Dear ImGui, no window) decisions behind the Vehicle Controller Inspector in VehicleEditorUi.hpp, so they can be
// tested headless (tests/test_vehicle_editor_model.cpp). Written from scratch for this engine on top of the VehicleTuning table.
#include "okay/Components/VehicleTuning.hpp"
#include "okay/Components/VehicleControlScheme.hpp"
#include "okay/Input/Input.hpp"
#include <cstdio>
#include <string>
#include <vector>

namespace oked {
namespace vehiclemodel {

/// "A|B|C" -> "A\0B\0C\0\0", the packed list Dear ImGui's Combo wants.
inline std::string ComboItems(const char* options) {
    std::string items;
    for (const char* c = options ? options : ""; *c; ++c) items += (*c == '|') ? '\0' : *c;
    items += '\0';
    return items;
}

/// Short text for a key setting: a letter or digit shows as itself, anything else as its number.
inline std::string KeyLabel(char k) {
    const unsigned char u = (unsigned char)k;
    if ((u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9')) return std::string(1, (char)u);
    char b[16]; std::snprintf(b, sizeof(b), "#%d", (int)u); return b;
}

// ---- Plain-language view of every setting (audit: docs/status/UNFINISHED.md vehicle row) ------------------------------------
/// One short Inspector, no Simple / Advanced split. Shown = always drawn (about 15 rows with the camera-follows checkbox), in the
/// named section. Revealed = drawn only while the section's checkbox (Boost) is on. Hidden = automatic: the setting keeps a good
/// value from the preset, is still saved and loaded and can be set by name (old scenes, scripts), but is never drawn.
enum class Level { Shown, Revealed, Hidden };
struct Info { const char* name; const char* label; const char* unit; Level level; const char* section; };

inline const std::vector<Info>& Infos() {
    static const std::vector<Info> v = {
        {"openWorld", "Open-world model", "on/off", Level::Hidden, ""},
        {"handling", "Handling: realistic to arcade", "0 to 1", Level::Shown, "Handling"},
        {"maxSpeed", "Top speed", "m/s", Level::Shown, "Driving"},
        {"acceleration", "Engine push", "m/s2", Level::Shown, "Driving"},
        {"grip", "Tyre stickiness", "units", Level::Shown, "Handling"},
        {"driftAmount", "Drift friendliness", "0 to 1", Level::Shown, "Handling"},
        {"reverseSpeed", "Top reverse speed", "m/s", Level::Hidden, ""},
        {"idleRpm", "Idle engine speed", "rpm", Level::Hidden, ""},
        {"maxRpm", "Engine speed limit", "rpm", Level::Hidden, ""},
        {"torquePeak", "Where the engine pulls hardest", "0 to 1", Level::Hidden, ""},
        {"gearCount", "Number of gears", "gears", Level::Hidden, ""},
        {"gearbox", "Gearbox", "choice", Level::Shown, "Driving"},
        {"shiftTime", "Gear change time", "s", Level::Hidden, ""},
        {"engineBraking", "Engine braking", "m/s2", Level::Hidden, ""},
        {"drag", "Rolling slowdown", "m/s2", Level::Hidden, ""},
        {"drivetrain", "Driven wheels", "choice", Level::Shown, "Driving"},
        {"torqueSplit", "Front share of the push (all-wheel)", "0 to 1", Level::Hidden, ""},
        {"diffLock", "Differential lock", "0 to 1", Level::Hidden, ""},
        {"maxSteerAngle", "Steering lock", "degrees", Level::Shown, "Handling"},
        {"steerSpeedSensitivity", "Less steering at speed", "0 to 1", Level::Hidden, ""},
        {"steerResponse", "Steering speed", "1/s", Level::Hidden, ""},
        {"autoCentre", "Wheel self-centring", "1/s", Level::Hidden, ""},
        {"counterSteerAssist", "Slide catch assist", "0 to 1", Level::Hidden, ""},
        {"steerAssist", "Grip-limited steering", "on/off", Level::Hidden, ""},
        {"turnSpeed", "Turn rate (older model)", "degrees/s", Level::Hidden, ""},
        {"brakeForce", "Brake power", "m/s2", Level::Shown, "Driving"},
        {"brakeBias", "Front brake share", "0 to 1", Level::Hidden, ""},
        {"abs", "Anti-lock brakes", "on/off", Level::Hidden, ""},
        {"handbrakeStrength", "Handbrake strength", "0 to 3", Level::Hidden, ""},
        {"reverseMode", "How to reverse", "choice", Level::Hidden, ""},
        {"frontGrip", "Front tyre grip", "times", Level::Hidden, ""},
        {"rearGrip", "Rear tyre grip", "times", Level::Hidden, ""},
        {"slipLimit", "Slide angle before grip is lost", "degrees", Level::Hidden, ""},
        {"handbrakeGrip", "Rear grip with handbrake", "units", Level::Hidden, ""},
        {"iceGrip", "Grip on ice", "times", Level::Hidden, ""},
        {"mudGrip", "Grip on mud", "times", Level::Hidden, ""},
        {"grassGrip", "Grip on grass", "times", Level::Hidden, ""},
        {"downforce", "Extra grip at speed", "0 to 1", Level::Hidden, ""},
        {"aeroDrag", "Air resistance", "units", Level::Hidden, ""},
        {"leanAmount", "Body lean", "0 to 2", Level::Hidden, ""},
        {"antiRoll", "Roll resistance", "0 to 1", Level::Hidden, ""},
        {"tractionControl", "Traction control", "on/off", Level::Hidden, ""},
        {"stabilityControl", "Stability control", "0 to 1", Level::Hidden, ""},
        {"autoFlip", "Self-right after a flip", "on/off", Level::Hidden, ""},
        {"airControl", "Tilt control in the air", "0 to 1", Level::Hidden, ""},
        {"boostEnabled", "Boost button", "on/off", Level::Shown, "Boost"},
        {"boostPower", "Boost push", "m/s2", Level::Revealed, "Boost"},
        {"boostDuration", "Boost tank size", "s", Level::Revealed, "Boost"},
        {"boostRecharge", "Boost refill rate", "tanks/s", Level::Hidden, ""},
        {"steerSensitivity", "Steering sensitivity", "times", Level::Shown, "Handling"},
        {"inputSmoothing", "Key easing", "1/s", Level::Hidden, ""},
        {"deadZone", "Stick dead zone", "0 to 0.9", Level::Hidden, ""},
        {"invertSteer", "Invert steering", "on/off", Level::Hidden, ""},
        {"keyThrottle", "Gas key", "key", Level::Hidden, ""},
        {"keyBrake", "Brake key", "key", Level::Hidden, ""},
        {"keySteerLeft", "Steer left key", "key", Level::Hidden, ""},
        {"keySteerRight", "Steer right key", "key", Level::Hidden, ""},
        {"keyBoost", "Boost key", "key", Level::Hidden, ""},
        {"keyRecover", "Recover key", "key", Level::Hidden, ""},
        {"keyHorn", "Horn key", "key", Level::Hidden, ""},
        {"keyLights", "Lights key", "key", Level::Hidden, ""},
        {"keyCamera", "Camera key", "key", Level::Hidden, ""},
        {"keyLookBack", "Look-behind key", "key", Level::Hidden, ""},
        {"keyGearUp", "Gear up key", "key", Level::Hidden, ""},
        {"keyGearDown", "Gear down key", "key", Level::Hidden, ""},
        {"keyReverse", "Reverse key", "key", Level::Hidden, ""},
        {"padHandbrake", "Handbrake button", "button", Level::Hidden, ""},
        {"padBoost", "Boost button", "button", Level::Hidden, ""},
        {"padRecover", "Recover button", "button", Level::Hidden, ""},
        {"padHorn", "Horn button", "button", Level::Hidden, ""},
        {"padLights", "Lights button", "button", Level::Hidden, ""},
        {"padCamera", "Camera button", "button", Level::Hidden, ""},
        {"padGearUp", "Gear up button", "button", Level::Hidden, ""},
        {"padGearDown", "Gear down button", "button", Level::Hidden, ""},
        {"camView", "Starting camera view", "choice", Level::Shown, "Camera"},
        {"camFov", "Field of view", "degrees", Level::Shown, "Camera"},
        {"camSpeedFov", "Extra field of view at speed", "degrees", Level::Hidden, ""},
        {"camSpeedDistance", "Extra distance at speed", "m", Level::Hidden, ""},
        {"camDriftSwing", "Camera swing in drifts", "0 to 2", Level::Hidden, ""},
        {"camLookAhead", "Look into the turn", "0 to 2", Level::Hidden, ""},
    };
    return v;
}
inline const Info* InfoFor(const std::string& name) {
    for (const Info& i : Infos()) if (name == i.name) return &i;
    return nullptr;
}
/// The section names in the order the Inspector draws them.
inline const std::vector<const char*>& Sections() { static const std::vector<const char*> s = {"Driving", "Handling", "Camera", "Boost"}; return s; }
/// Rows of one section with the given level, in table order.
inline std::vector<const okay::VehicleTuning::Desc*> RowsOf(const char* section, Level level) {
    std::vector<const okay::VehicleTuning::Desc*> out;
    for (const okay::VehicleTuning::Desc& d : okay::VehicleTuning::Table()) {
        const Info* i = InfoFor(d.name);
        if (i && i->level == level && std::string(i->section) == section) out.push_back(&d);
    }
    return out;
}
/// Every always-drawn row (all sections).
inline std::vector<const okay::VehicleTuning::Desc*> ShownRows() {
    std::vector<const okay::VehicleTuning::Desc*> out;
    for (const char* s : Sections()) for (const auto* d : RowsOf(s, Level::Shown)) out.push_back(d);
    return out;
}
/// Per-setting reset: put one named setting back to the preset's value (or the plain default when the preset is empty or
/// unknown). Returns false for an unknown setting.
inline bool ResetSetting(okay::VehicleTuning& t, const std::string& name, const std::string& preset) {
    okay::VehicleTuning ref;
    if (!preset.empty()) ref.ApplyPreset(preset);
    double v = 0;
    if (!ref.GetSetting(name, v)) return false;
    return t.SetSetting(name, v);
}
/// True when the setting differs from the preset's value (so the Inspector can show a reset button only then).
inline bool IsChanged(const okay::VehicleTuning& t, const std::string& name, const std::string& preset) {
    okay::VehicleTuning ref;
    if (!preset.empty()) ref.ApplyPreset(preset);
    double a = 0, b = 0;
    if (!t.GetSetting(name, a) || !ref.GetSetting(name, b)) return false;
    return std::fabs(a - b) > 1e-4 * (1.0 + std::fabs(b));
}

// ---- Controls tab (pure text and lists) ----------------------------------------------------------------------------------------

/// Plain name of a driving action for the Controls tab.
inline const char* ActionLabel(int a) {
    static const char* n[okay::drivectl::ActionCount] = {"Accelerate", "Brake / reverse", "Steer left", "Steer right", "Handbrake", "Horn",
        "Headlights on / off", "Look behind", "Change camera", "Get in / out", "Boost", "Put car back on its wheels", "Gear up", "Gear down",
        "Look left", "Look right"};
    return (a >= 0 && a < okay::drivectl::ActionCount) ? n[a] : "";
}
/// Text on a binding button: "W", "Space", "Shift", "Mouse 2", "Pad A", "Left stick left", "Right trigger", "(none)".
inline std::string BindingLabel(const okay::drivectl::Binding& b) {
    using B = okay::drivectl::Binding;
    static const char* padNames[16] = {"A", "B", "X", "Y", "Back", "Start", "Left bumper", "Right bumper", "D-pad up", "D-pad down",
        "D-pad left", "D-pad right", "Left stick click", "Right stick click", "Left trigger", "Right trigger"};
    switch (b.type) {
        case B::None: return "(none)";
        case B::Key:
            if (b.code == ' ') return "Space";
            if (b.code == 16) return "Shift";
            if (b.code == 17) return "Ctrl";
            if (b.code >= 'a' && b.code <= 'z') return std::string(1, (char)(b.code - 'a' + 'A'));
            if ((b.code >= '0' && b.code <= '9') || (b.code >= 'A' && b.code <= 'Z')) return std::string(1, (char)b.code);
            if (b.code == okay::Input::KeyUp) return "Up arrow";
            if (b.code == okay::Input::KeyDown) return "Down arrow";
            if (b.code == okay::Input::KeyLeft) return "Left arrow";
            if (b.code == okay::Input::KeyRight) return "Right arrow";
            { char s[16]; std::snprintf(s, sizeof(s), "Key %d", b.code); return s; }
        case B::Mouse: { char s[16]; std::snprintf(s, sizeof(s), "Mouse %d", b.code + 1); return s; }
        case B::PadButton: return std::string("Pad ") + padNames[b.code & 15];
        case B::PadAxis: { static const char* ax[4] = {"Left stick ", "Left stick ", "Right stick ", "Right stick "};
                           const bool horiz = b.code % 2 == 0;
                           return std::string(ax[b.code & 3]) + (horiz ? (b.negative ? "left" : "right") : (b.negative ? "down" : "up")); }
        case B::PadTrigger: return b.code == 0 ? "Left trigger" : "Right trigger";
    }
    return "";
}
/// The gamepad choices offered in the Controls tab (the first is "none").
inline const std::vector<okay::drivectl::Binding>& PadChoices() {
    static const std::vector<okay::drivectl::Binding> v = [] {
        using B = okay::drivectl::Binding;
        std::vector<B> c; c.push_back(B());
        c.push_back(B::T(0)); c.push_back(B::T(1));
        for (int i = 0; i < 14; ++i) c.push_back(B::B(i));
        c.push_back(B::A(0, true)); c.push_back(B::A(0, false)); c.push_back(B::A(1, true)); c.push_back(B::A(1, false));
        c.push_back(B::A(2, true)); c.push_back(B::A(2, false)); c.push_back(B::A(3, true)); c.push_back(B::A(3, false));
        return c;
    }();
    return v;
}
/// One plain-words line per clash in the scheme (both slots), or an empty list when every input is used once.
inline std::vector<std::string> ConflictLines(const okay::drivectl::ControlScheme& s) {
    using namespace okay::drivectl;
    std::vector<std::string> out;
    for (int slot = 0; slot < 2; ++slot)
        for (int a = 0; a < ActionCount; ++a)
            for (int o : Conflicts(s, a, slot))
                if (o > a) out.push_back(std::string(ActionLabel(a)) + " and " + ActionLabel(o) + " both use " +
                                         BindingLabel(slot == 0 ? s.act[a].primary : s.act[a].pad));
    return out;
}

} // namespace vehiclemodel
} // namespace oked

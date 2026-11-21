#pragma once
// The pure (no Dear ImGui, no window) decisions behind the Vehicle Controller Inspector in VehicleEditorUi.hpp, so they can be
// tested headless (tests/test_vehicle_editor_model.cpp). Written from scratch for this engine on top of the VehicleTuning table.
#include "okay/Components/VehicleTuning.hpp"
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

/// The rows of the Simple tab (after the preset and Handling controls).
inline const std::vector<const char*>& SimpleRows() {
    static const std::vector<const char*> r = {"maxSpeed", "acceleration", "grip", "driftAmount"};
    return r;
}

/// One group of the Advanced tab: its name and the settings in it, in table order. "Simple" settings are not repeated here.
struct Group { std::string name; std::vector<const okay::VehicleTuning::Desc*> rows; };
inline std::vector<Group> AdvancedGroups() {
    std::vector<Group> out;
    for (const okay::VehicleTuning::Desc& d : okay::VehicleTuning::Table()) {
        if (std::string(d.group) == "Simple") continue;
        if (out.empty() || out.back().name != d.group) out.push_back({d.group, {}});
        out.back().rows.push_back(&d);
    }
    return out;
}

/// Short text for a key setting: a letter or digit shows as itself, anything else as its number.
inline std::string KeyLabel(char k) {
    const unsigned char u = (unsigned char)k;
    if ((u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9')) return std::string(1, (char)u);
    char b[16]; std::snprintf(b, sizeof(b), "#%d", (int)u); return b;
}

// ---- Plain-language view of every setting (audit: docs/status/UNFINISHED.md vehicle row) ------------------------------------
/// Who sees a setting: Simple = the default Inspector (about 8 rows); Advanced = behind the Advanced tab; Hidden = still saved,
/// loaded and settable by name (old scenes keep working) but never drawn (key and button settings are changed in the Controls
/// tab instead; the older-model turn rate and the model switch are not for people to edit).
enum class Level { Simple, Advanced, Hidden };
struct Info { const char* name; const char* label; const char* unit; Level level; };

inline const std::vector<Info>& Infos() {
    static const std::vector<Info> v = {
        {"openWorld", "Open-world model", "on/off", Level::Hidden}, {"handling", "Handling: realistic to arcade", "0 to 1", Level::Simple},
        {"maxSpeed", "Top speed", "m/s", Level::Simple}, {"acceleration", "Engine push", "m/s2", Level::Simple},
        {"grip", "Tyre stickiness", "units", Level::Simple}, {"driftAmount", "Drift friendliness", "0 to 1", Level::Simple},
        {"reverseSpeed", "Top reverse speed", "m/s", Level::Advanced}, {"idleRpm", "Idle engine speed", "rpm", Level::Advanced},
        {"maxRpm", "Engine speed limit", "rpm", Level::Advanced}, {"torquePeak", "Where the engine pulls hardest", "0 to 1", Level::Advanced},
        {"gearCount", "Number of gears", "gears", Level::Advanced}, {"gearbox", "Gearbox", "choice", Level::Simple},
        {"shiftTime", "Gear change time", "s", Level::Advanced}, {"engineBraking", "Engine braking", "m/s2", Level::Advanced},
        {"drag", "Rolling slowdown", "m/s2", Level::Advanced}, {"drivetrain", "Driven wheels", "choice", Level::Advanced},
        {"torqueSplit", "Front share of the push (all-wheel)", "0 to 1", Level::Advanced}, {"diffLock", "Differential lock", "0 to 1", Level::Advanced},
        {"maxSteerAngle", "Steering lock", "degrees", Level::Advanced}, {"steerSpeedSensitivity", "Less steering at speed", "0 to 1", Level::Advanced},
        {"steerResponse", "Steering speed", "1/s", Level::Advanced}, {"autoCentre", "Wheel self-centring", "1/s", Level::Advanced},
        {"counterSteerAssist", "Slide catch assist", "0 to 1", Level::Advanced}, {"steerAssist", "Grip-limited steering", "on/off", Level::Advanced},
        {"turnSpeed", "Turn rate (older model)", "degrees/s", Level::Hidden}, {"brakeForce", "Brake power", "m/s2", Level::Advanced},
        {"brakeBias", "Front brake share", "0 to 1", Level::Advanced}, {"abs", "Anti-lock brakes", "on/off", Level::Advanced},
        {"handbrakeStrength", "Handbrake strength", "0 to 3", Level::Advanced}, {"reverseMode", "How to reverse", "choice", Level::Advanced},
        {"frontGrip", "Front tyre grip", "times", Level::Advanced}, {"rearGrip", "Rear tyre grip", "times", Level::Advanced},
        {"slipLimit", "Slide angle before grip is lost", "degrees", Level::Advanced}, {"handbrakeGrip", "Rear grip with handbrake", "units", Level::Advanced},
        {"iceGrip", "Grip on ice", "times", Level::Advanced}, {"mudGrip", "Grip on mud", "times", Level::Advanced},
        {"grassGrip", "Grip on grass", "times", Level::Advanced}, {"downforce", "Extra grip at speed", "0 to 1", Level::Advanced},
        {"aeroDrag", "Air resistance", "units", Level::Advanced}, {"leanAmount", "Body lean", "0 to 2", Level::Advanced},
        {"antiRoll", "Roll resistance", "0 to 1", Level::Advanced}, {"tractionControl", "Traction control", "on/off", Level::Advanced},
        {"stabilityControl", "Stability control", "0 to 1", Level::Advanced}, {"autoFlip", "Self-right after a flip", "on/off", Level::Advanced},
        {"airControl", "Tilt control in the air", "0 to 1", Level::Advanced}, {"boostEnabled", "Boost allowed", "on/off", Level::Advanced},
        {"boostPower", "Boost push", "m/s2", Level::Advanced}, {"boostDuration", "Boost tank size", "s", Level::Advanced},
        {"boostRecharge", "Boost refill rate", "tanks/s", Level::Advanced}, {"steerSensitivity", "Steering sensitivity", "times", Level::Simple},
        {"inputSmoothing", "Key easing", "1/s", Level::Advanced}, {"deadZone", "Stick dead zone", "0 to 0.9", Level::Advanced},
        {"invertSteer", "Invert steering", "on/off", Level::Advanced},
        {"keyThrottle", "Gas key", "key", Level::Hidden}, {"keyBrake", "Brake key", "key", Level::Hidden},
        {"keySteerLeft", "Steer left key", "key", Level::Hidden}, {"keySteerRight", "Steer right key", "key", Level::Hidden},
        {"keyBoost", "Boost key", "key", Level::Hidden}, {"keyRecover", "Recover key", "key", Level::Hidden},
        {"keyHorn", "Horn key", "key", Level::Hidden}, {"keyLights", "Lights key", "key", Level::Hidden},
        {"keyCamera", "Camera key", "key", Level::Hidden}, {"keyLookBack", "Look-behind key", "key", Level::Hidden},
        {"keyGearUp", "Gear up key", "key", Level::Hidden}, {"keyGearDown", "Gear down key", "key", Level::Hidden},
        {"keyReverse", "Reverse key", "key", Level::Hidden},
        {"padHandbrake", "Handbrake button", "button", Level::Hidden}, {"padBoost", "Boost button", "button", Level::Hidden},
        {"padRecover", "Recover button", "button", Level::Hidden}, {"padHorn", "Horn button", "button", Level::Hidden},
        {"padLights", "Lights button", "button", Level::Hidden}, {"padCamera", "Camera button", "button", Level::Hidden},
        {"padGearUp", "Gear up button", "button", Level::Hidden}, {"padGearDown", "Gear down button", "button", Level::Hidden},
        {"camView", "Starting camera view", "choice", Level::Simple}, {"camFov", "Field of view", "degrees", Level::Advanced},
        {"camSpeedFov", "Extra field of view at speed", "degrees", Level::Advanced}, {"camSpeedDistance", "Extra distance at speed", "m", Level::Advanced},
        {"camDriftSwing", "Camera swing in drifts", "0 to 2", Level::Advanced}, {"camLookAhead", "Look into the turn", "0 to 2", Level::Advanced},
    };
    return v;
}
inline const Info* InfoFor(const std::string& name) {
    for (const Info& i : Infos()) if (name == i.name) return &i;
    return nullptr;
}
/// Rows of the default (Simple) Inspector, in order: the settings whose level is Simple.
inline std::vector<const okay::VehicleTuning::Desc*> DefaultRows() {
    std::vector<const okay::VehicleTuning::Desc*> out;
    for (const okay::VehicleTuning::Desc& d : okay::VehicleTuning::Table()) {
        const Info* i = InfoFor(d.name);
        if (i && i->level == Level::Simple) out.push_back(&d);
    }
    return out;
}
/// Rows of the Advanced tab: level Advanced only.
inline std::vector<const okay::VehicleTuning::Desc*> AdvancedRows() {
    std::vector<const okay::VehicleTuning::Desc*> out;
    for (const okay::VehicleTuning::Desc& d : okay::VehicleTuning::Table()) {
        const Info* i = InfoFor(d.name);
        if (i && i->level == Level::Advanced) out.push_back(&d);
    }
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

} // namespace vehiclemodel
} // namespace oked

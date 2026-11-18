#pragma once
// Raw controller state -> the engine's logical pad (two sticks, two triggers, 15 buttons).
//
//  * RawPadState is what a device reports before any naming: numbered axes, buttons, hats.
//  * ControllerMapping says which raw input drives each logical one. It can be a
//    generic guess (BuildGenericMapping) or the player's own (ControllerSetup).
//  * It saves to and loads from one line of text, so it can live in Prefs per device.
// Pure logic: no SDL, no device. Written from the general idea of axis / button / hat
// controllers; the default layout is our own choice and "Controller Setup" fixes any pad.
#include "okay/Input/GamepadInfo.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace okay {

enum HatDir : std::uint8_t { HatUp = 1, HatRight = 2, HatDown = 4, HatLeft = 8 };

struct RawPadState {
    std::vector<std::int16_t> axes;      ///< -32768..32767
    std::vector<std::uint8_t> buttons;   ///< 0 / 1
    std::vector<std::uint8_t> hats;      ///< HatDir bits
    bool Empty() const { return axes.empty() && buttons.empty() && hats.empty(); }
};

/// Where a logical input comes from.
struct PadSource {
    enum Kind : std::uint8_t { None, Axis, Button, Hat, AxisDir } kind = None;
    std::uint8_t index = 0;    ///< axis / button / hat number
    std::uint8_t dir = 0;      ///< Hat: the HatDir bits that count; AxisDir: 1 = past +half, 2 = past -half
    bool invert = false;       ///< Axis: flip the sign
    bool restLow = false;      ///< Axis used as a trigger: it rests at -32768 and pulls up to 32767
    bool operator==(const PadSource& o) const {
        return kind == o.kind && index == o.index && dir == o.dir && invert == o.invert && restLow == o.restLow;
    }
};

enum class MapSource { None, Builtin, Generic, User };
const char* MapSourceName(MapSource s);   // "built-in", "generic", "user", "none"

/// Logical axes follow platform::GamepadAxis (LeftX, LeftY, RightX, RightY, TriggerLeft, TriggerRight; sticks +y down),
/// logical buttons follow platform::GamepadButton (A B X Y Back Guide Start LS RS LB RB DpadUp DpadDown DpadLeft DpadRight).
constexpr int kLogicalAxes = 6;
constexpr int kLogicalButtons = 15;
enum LogicalAxis { LAxLeftX, LAxLeftY, LAxRightX, LAxRightY, LAxTriggerLeft, LAxTriggerRight };
enum LogicalButton { LBtA, LBtB, LBtX, LBtY, LBtBack, LBtGuide, LBtStart, LBtLeftStick, LBtRightStick,
                     LBtLeftShoulder, LBtRightShoulder, LBtDpadUp, LBtDpadDown, LBtDpadLeft, LBtDpadRight };

struct ControllerMapping {
    MapSource source = MapSource::None;
    PadSource axis[kLogicalAxes];
    PadSource button[kLogicalButtons];
    /// One line of text ("okpad1 a0=a0 ..."); Parse reads it back. Round trips exactly.
    std::string Serialize() const;
    static bool Parse(const std::string& text, ControllerMapping& out);
    /// How many logical inputs are connected to something.
    int MappedCount() const;
    bool operator==(const ControllerMapping& o) const;
};

struct LogicalPadState {
    std::int16_t axis[kLogicalAxes] = {};
    bool button[kLogicalButtons] = {};
};

/// Raw -> logical through the mapping. Missing raw inputs (index past the end) read as idle.
LogicalPadState ApplyMapping(const ControllerMapping& m, const RawPadState& raw);

/// A usable default for a device nothing maps: sticks and triggers found from the
/// idle sample (an axis that rests at its minimum is a trigger), the d-pad from a hat
/// (or four buttons), face buttons in the usual A B X Y LB RB Back Start L3 R3 order.
/// `idle` must be sampled with nothing touched.
ControllerMapping BuildGenericMapping(const RawPadState& idle, const GamepadInfo* info = nullptr);

/// "vvvv:pppp:aNbNhN" - stable per model and layout, the key the saved mapping lives under.
std::string DeviceKey(std::uint16_t vendor, std::uint16_t product, int axes, int buttons, int hats);

// ---- Controller Setup: ask the player to press each control ----------------------------------

class ControllerSetup {
public:
    enum class Step { LeftX, LeftY, RightX, RightY, LeftTrigger, RightTrigger,
                      A, B, X, Y, LeftShoulder, RightShoulder, Back, Start, LeftStick, RightStick,
                      DpadUp, DpadDown, DpadLeft, DpadRight, Count };
    /// Start (or restart) with a sample taken with nothing touched.
    void Begin(const RawPadState& idle, ControllerFamily family = ControllerFamily::Unknown);
    /// Call every frame with the live raw state. Captures the pressed control for the current step.
    void Feed(const RawPadState& now);
    bool Active() const { return m_active; }
    bool Finished() const { return m_finished; }
    int StepIndex() const { return m_step; }
    int StepCount() const { return (int)Step::Count; }
    /// "Push the LEFT stick to the RIGHT", "Press Cross (A)" ...
    std::string Prompt() const;
    /// What the last capture was ("axis 3", "button 7", "hat 0 up"), "" before the first.
    const std::string& LastCapture() const { return m_last; }
    void Skip();          ///< leave this control unmapped and go on
    void Back();          ///< go to the previous control (its capture is cleared)
    void Cancel();        ///< stop; Result() stays what it was
    /// The mapping so far (source User). Complete after Finished().
    const ControllerMapping& Result() const { return m_map; }
private:
    void Advance();
    bool Neutral(const RawPadState& now) const;
    bool Used(const PadSource& s) const;
    RawPadState m_idle;
    ControllerMapping m_map;
    ControllerFamily m_family = ControllerFamily::Unknown;
    int m_step = 0;
    bool m_active = false, m_finished = false, m_wantNeutral = false;
    std::string m_last;
};

} // namespace okay

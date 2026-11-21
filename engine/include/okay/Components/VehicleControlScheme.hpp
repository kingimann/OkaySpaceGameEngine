#pragma once
// Vehicle control schemes: which input does what while driving, and how a raw input is shaped into a drive value.
// Pure data and maths (no window, no Input calls), so every decision is tested headless (tests/test_vehicle_controls.cpp).
//
// Written from scratch from the general idea of a third-person street-driving scheme: gas and brake on two separate
// analog axes, steering on the left stick or A and D, a handbrake button, horn, headlight toggle, look-behind, a camera
// button that cycles views, a camera that swings back behind the car when you stop looking around, and one key that gets
// in (a tap = driver, a short hold = passenger) and out. No code, constants or text from any game or engine.
// Header only, free of <mutex>/<thread> (rule 21a).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace okay {
namespace drivectl {

/// One physical input. Key: code is the engine key char (letters are lower case, 16 = Shift, 32 = Space). Mouse: 0 left,
/// 1 right, 2 middle. PadButton: Input::PadButton id 0..15. PadAxis: 0 = left stick X, 1 = left stick Y, 2 = right stick X,
/// 3 = right stick Y. PadTrigger: 0 = left, 1 = right. `negative` picks the minus half of an axis (left / down).
struct Binding {
    enum Type { None = 0, Key, Mouse, PadButton, PadAxis, PadTrigger };
    Type type = None; int code = 0; bool negative = false;
    bool operator==(const Binding& o) const { return type == o.type && code == o.code && negative == o.negative; }
    bool IsNone() const { return type == None; }
    bool IsPad() const { return type >= PadButton; }
    static Binding K(char c) { return {Key, (unsigned char)c, false}; }
    static Binding M(int b) { return {Mouse, b, false}; }
    static Binding B(int b) { return {PadButton, b, false}; }
    static Binding A(int axis, bool neg) { return {PadAxis, axis, neg}; }
    static Binding T(int side) { return {PadTrigger, side, false}; }
    /// True when the binding is a legal one: known type and a code in range.
    bool Valid() const {
        switch (type) {
            case None: return code == 0 && !negative;
            case Key: return code >= 1 && code <= 255 && !negative;
            case Mouse: return code >= 0 && code <= 4 && !negative;
            case PadButton: return code >= 0 && code <= 15 && !negative;
            case PadAxis: return code >= 0 && code <= 3;
            case PadTrigger: return (code == 0 || code == 1) && !negative;
        }
        return false;
    }
};

enum Action { Throttle, Brake, SteerLeft, SteerRight, Handbrake, Horn, Headlights, LookBack, CycleCamera, EnterExit,
              Boost, Recover, GearUp, GearDown, LookLeft, LookRight, ActionCount };

inline const char* ActionName(int a) {
    static const char* n[ActionCount] = {"throttle", "brake", "steerLeft", "steerRight", "handbrake", "horn", "headlights",
        "lookBack", "cycleCamera", "enterExit", "boost", "recover", "gearUp", "gearDown", "lookLeft", "lookRight"};
    return (a >= 0 && a < ActionCount) ? n[a] : "";
}
inline int ActionFromName(const std::string& s) { for (int a = 0; a < ActionCount; ++a) if (s == ActionName(a)) return a; return -1; }
/// Actions that are a continuous 0..1 amount (the rest are buttons).
inline bool IsAnalog(int a) { return a == Throttle || a == Brake || a == SteerLeft || a == SteerRight || a == LookLeft || a == LookRight; }

/// How one action reads its input: two slots (keyboard or mouse in `primary`, gamepad in `pad`) plus its shaping.
struct ActionConfig {
    Binding primary, pad;
    float sensitivity = 1.0f;   ///< 0.1 .. 3, multiplies the shaped value (result is clamped to 0..1)
    float deadZone = 0.15f;     ///< 0 .. 0.9, the part of a stick or trigger that reads zero
    float curve = 1.0f;         ///< 0.5 .. 3 response exponent: 1 = straight, above 1 = gentle near the middle
    bool invert = false;        ///< swap the result's sign (steering and look only)
};

constexpr int kCameraViews = 4;   ///< 0 chase, 1 far chase, 2 hood, 3 cockpit
inline const char* CameraViewName(int v) { static const char* n[kCameraViews] = {"Chase", "Far chase", "Hood", "Cockpit"}; return (v >= 0 && v < kCameraViews) ? n[v] : ""; }

struct ControlScheme {
    std::string name = "Classic";
    ActionConfig act[ActionCount];
    float steerAssist = 0.5f;        ///< 0 .. 1: how much the steering is trimmed at speed (0 = none, 1 = strongest)
    bool  autoGear = true;           ///< true: the car changes gear itself; false: GearUp / GearDown change it
    float camDistance[kCameraViews] = {7.0f, 11.0f, 0.0f, 0.0f};   ///< metres behind the car (0 for the first-person views)
    float camHeight[kCameraViews]   = {3.0f, 4.5f, 1.25f, 1.2f};
    float camFov[kCameraViews]      = {60.0f, 62.0f, 70.0f, 75.0f};
    float mouseLookSensitivity = 0.15f;   ///< degrees of camera yaw per mouse count while driving (0 = off)
    float recentreDelay = 1.5f;      ///< seconds after the last look input before the camera swings back behind the car
    float recentreRate = 2.5f;       ///< how fast it swings back (1 / seconds)
    float enterHoldTime = 0.45f;     ///< seconds the enter key must be held to get in as a passenger
    bool  mouseLook = false;         ///< mouse moves the camera while driving
};

inline void Set(ControlScheme& s, int a, Binding kb, Binding pad) { s.act[a].primary = kb; s.act[a].pad = pad; }

/// The scheme the vehicle had before this file: W S A D, space handbrake, H horn, L lights, C camera, B look back, E.
inline ControlScheme ClassicScheme() {
    ControlScheme s; s.name = "Classic";
    using B = Binding;
    Set(s, Throttle, B::K('w'), B::T(1));        Set(s, Brake, B::K('s'), B::T(0));
    Set(s, SteerLeft, B::K('a'), B::A(0, true)); Set(s, SteerRight, B::K('d'), B::A(0, false));
    Set(s, Handbrake, B::K(' '), B::B(0));       Set(s, Horn, B::K('h'), B::B(12));
    Set(s, Headlights, B::K('l'), B::B(8));      Set(s, LookBack, B::K('b'), B());
    Set(s, CycleCamera, B::K('c'), B::B(13));    Set(s, EnterExit, B::K('e'), B::B(3));
    Set(s, Boost, B::K((char)16), B::B(1));      Set(s, Recover, B::K('r'), B::B(4));
    Set(s, GearUp, B::K('x'), B::B(7));          Set(s, GearDown, B::K('z'), B::B(6));
    s.mouseLook = false; s.steerAssist = 0.5f;
    return s;
}

/// A street-driving scheme: gas and brake are separate analog axes (right and left trigger, or W and S), steering on the
/// left stick or A and D, handbrake on Space / pad A, horn on H / left-stick click, headlights on L / D-pad up, look behind
/// on C / right-stick click, camera on V / pad Back, look left and right on Q and E / right stick, enter and exit on F / pad Y.
/// The mouse looks around while driving and the camera recentres behind the car.
inline ControlScheme StreetScheme() {
    ControlScheme s; s.name = "Street";
    using B = Binding;
    Set(s, Throttle, B::K('w'), B::T(1));        Set(s, Brake, B::K('s'), B::T(0));
    Set(s, SteerLeft, B::K('a'), B::A(0, true)); Set(s, SteerRight, B::K('d'), B::A(0, false));
    Set(s, Handbrake, B::K(' '), B::B(0));       Set(s, Horn, B::K('h'), B::B(12));
    Set(s, Headlights, B::K('l'), B::B(8));      Set(s, LookBack, B::K('c'), B::B(13));
    Set(s, CycleCamera, B::K('v'), B::B(4));     Set(s, EnterExit, B::K('f'), B::B(3));
    Set(s, Boost, B::K((char)16), B::B(1));      Set(s, Recover, B::K('r'), B::B(5));
    Set(s, GearUp, B::K('x'), B::B(7));          Set(s, GearDown, B::K('z'), B::B(6));
    Set(s, LookLeft, B::K('q'), B::A(2, true));  Set(s, LookRight, B::K('e'), B::A(2, false));
    s.act[Throttle].curve = 1.4f; s.act[Brake].curve = 1.4f; s.act[SteerLeft].curve = 1.3f; s.act[SteerRight].curve = 1.3f;
    s.act[Throttle].deadZone = s.act[Brake].deadZone = 0.05f;
    s.steerAssist = 0.7f; s.mouseLook = true; s.autoGear = true;
    return s;
}

/// Arcade: Street controls with a lot of steering help and gentle response, easy for a first drive.
inline ControlScheme ArcadeScheme() { ControlScheme s = StreetScheme(); s.name = "Arcade"; s.steerAssist = 1.0f; s.act[SteerLeft].curve = s.act[SteerRight].curve = 1.0f; s.recentreDelay = 0.8f; return s; }
/// Realistic: Street controls, manual-friendly: little steering help, a firmer response curve, slower camera recentre.
inline ControlScheme RealisticScheme() { ControlScheme s = StreetScheme(); s.name = "Realistic"; s.steerAssist = 0.2f; s.act[SteerLeft].curve = s.act[SteerRight].curve = 1.6f; s.recentreDelay = 2.5f; s.autoGear = false; return s; }

/// The default for a new vehicle (zero tweaking): the street scheme.
inline const char* DefaultPresetName() { return "Street"; }
inline std::vector<std::string> PresetNames() { return {"Street", "Classic", "Arcade", "Realistic"}; }
inline bool Preset(const std::string& name, ControlScheme& out) {
    if (name == "Classic") { out = ClassicScheme(); return true; }
    if (name == "Street") { out = StreetScheme(); return true; }
    if (name == "Arcade") { out = ArcadeScheme(); return true; }
    if (name == "Realistic") { out = RealisticScheme(); return true; }
    return false;
}

// ---- shaping -------------------------------------------------------------------------------------------------------
/// Raw 0..1 (key or button 0/1, trigger or stick magnitude) -> shaped 0..1: dead zone removed and rescaled, response
/// curve, sensitivity, clamp. NaN reads as 0.
inline float Shape(float raw, const ActionConfig& c) {
    if (!(raw == raw)) return 0.0f;
    float v = std::min(1.0f, std::max(0.0f, raw));
    const float dz = std::min(0.9f, std::max(0.0f, c.deadZone));
    v = v <= dz ? 0.0f : (v - dz) / (1.0f - dz);
    v = std::pow(v, std::min(3.0f, std::max(0.5f, c.curve)));
    v *= std::min(3.0f, std::max(0.1f, c.sensitivity));
    return std::min(1.0f, v);
}
/// Steering from the two halves: right minus left, both shaped, then inverted if asked.
inline float SteerValue(float rawLeft, float rawRight, const ActionConfig& left, const ActionConfig& right) {
    float s = Shape(rawRight, right) - Shape(rawLeft, left);
    if (left.invert || right.invert) s = -s;
    return std::min(1.0f, std::max(-1.0f, s));
}
/// Speed-sensitive steering: the share of full lock allowed at `speed` (m/s) for a car whose top speed is `maxSpeed`.
/// 1 at a standstill, falling smoothly (smoothstep) to (1 - 0.7 * assist) at top speed.
inline float SteerLimit(float speed, float maxSpeed, float assist) {
    const float x = std::min(1.0f, std::fabs(speed) / std::max(0.1f, maxSpeed));
    const float sm = x * x * (3.0f - 2.0f * x);
    return 1.0f - 0.7f * std::min(1.0f, std::max(0.0f, assist)) * sm;
}

// ---- camera ----------------------------------------------------------------------------------------------------------
inline int NextCamera(int view) { return ((view % kCameraViews) + kCameraViews + 1) % kCameraViews; }
/// The camera's yaw offset from straight behind the car (degrees, -180..180). Look input moves it; after `delay` seconds
/// with none it eases back to 0 at `rate`. `lookBack` holds it at 180.
struct CameraRecentre {
    float offset = 0.0f, idle = 0.0f;
    void Look(float degrees) { offset += degrees; while (offset > 180.0f) offset -= 360.0f; while (offset < -180.0f) offset += 360.0f; idle = 0.0f; }
    float Update(float dt, bool lookBack, float delay, float rate) {
        if (lookBack) { offset = 180.0f; idle = 0.0f; return offset; }
        idle += dt;
        if (idle >= delay) {
            const float k = 1.0f - std::exp(-std::max(0.0f, rate) * dt);
            offset *= (1.0f - k);
            if (std::fabs(offset) < 0.01f) offset = 0.0f;
        }
        return offset;
    }
};

// ---- enter / exit key --------------------------------------------------------------------------------------------------
/// One key to get in and out: released before `holdTime` = driver (a tap); still held at `holdTime` = passenger. Each result
/// is given once.
struct EnterHold {
    enum Result { Nothing, Driver, Passenger };
    float held = 0.0f; bool down = false, fired = false;
    Result Update(bool keyDown, float dt, float holdTime) {
        Result r = Nothing;
        if (keyDown) {
            if (!down) { held = 0.0f; fired = false; }
            held += dt;
            if (!fired && held >= holdTime) { fired = true; r = Passenger; }
        } else if (down && !fired) r = Driver;
        down = keyDown;
        return r;
    }
};

// ---- rebinding and conflicts -------------------------------------------------------------------------------------------
/// Sets one slot (0 = keyboard/mouse, 1 = gamepad). Refused (false, nothing changed) for a bad action, slot or binding, or a
/// binding of the wrong device for the slot.
inline bool Rebind(ControlScheme& s, int action, int slot, const Binding& b) {
    if (action < 0 || action >= ActionCount || (slot != 0 && slot != 1) || !b.Valid()) return false;
    if (!b.IsNone() && b.IsPad() != (slot == 1)) return false;
    ((slot == 0) ? s.act[action].primary : s.act[action].pad) = b;
    return true;
}
/// Other actions that use the same binding in the same slot.
inline std::vector<int> Conflicts(const ControlScheme& s, int action, int slot) {
    std::vector<int> out;
    if (action < 0 || action >= ActionCount) return out;
    const Binding& me = slot == 0 ? s.act[action].primary : s.act[action].pad;
    if (me.IsNone()) return out;
    for (int a = 0; a < ActionCount; ++a)
        if (a != action && (slot == 0 ? s.act[a].primary : s.act[a].pad) == me) out.push_back(a);
    return out;
}

/// "Press the key you want": the window passes the input it just saw. Applies it and returns a plain-words message: empty
/// when all is well, a warning that names the other action(s) when the input is already used (they are not changed, the
/// caller may unbind them), or a refusal text when the input is not allowed in that slot (then nothing changes).
inline std::string CaptureRebind(ControlScheme& s, int action, int slot, const Binding& b) {
    if (!Rebind(s, action, slot, b)) return "That input cannot be used for this slot. Nothing was changed.";
    const std::vector<int> c = Conflicts(s, action, slot);
    if (c.empty()) return "";
    std::string m = "Warning: this input is also used for";
    for (size_t i = 0; i < c.size(); ++i) m += std::string(i ? ", " : " ") + ActionName(c[i]);
    return m + ".";
}

// ---- save / load ------------------------------------------------------------------------------------------------------------
/// Plain text: "okaydrivecontrols 1", "name X", global lines, camera lines, then one line per action:
/// "action throttle <type> <code> <neg> <type> <code> <neg> sensitivity deadZone curve invert".
inline std::string ToText(const ControlScheme& s) {
    std::ostringstream o;
    o << "okaydrivecontrols 1\nname " << s.name << "\n";
    o << "steerAssist " << s.steerAssist << "\nautoGear " << (s.autoGear ? 1 : 0) << "\nmouseLook " << (s.mouseLook ? 1 : 0)
      << "\nmouseLookSensitivity " << s.mouseLookSensitivity << "\nrecentreDelay " << s.recentreDelay << "\nrecentreRate "
      << s.recentreRate << "\nenterHoldTime " << s.enterHoldTime << "\n";
    for (int i = 0; i < kCameraViews; ++i) o << "camera " << i << " " << s.camDistance[i] << " " << s.camHeight[i] << " " << s.camFov[i] << "\n";
    for (int a = 0; a < ActionCount; ++a) {
        const ActionConfig& c = s.act[a];
        o << "action " << ActionName(a) << " " << (int)c.primary.type << " " << c.primary.code << " " << (c.primary.negative ? 1 : 0)
          << " " << (int)c.pad.type << " " << c.pad.code << " " << (c.pad.negative ? 1 : 0) << " " << c.sensitivity << " "
          << c.deadZone << " " << c.curve << " " << (c.invert ? 1 : 0) << "\n";
    }
    return o.str();
}
/// Reads text made by ToText. All or nothing: any bad line (wrong header, unknown action, NaN, out-of-range number, illegal
/// binding) makes it return false and leaves `out` unchanged. Unknown top-level words are skipped so a newer file still loads.
inline bool Fail(std::string* why, const std::string& key) {
    if (why) *why = key.empty() ? "This is not a driving controls file." : "The controls file has a bad or missing value near '" + key + "'. Nothing was changed.";
    return false;
}
inline bool FromText(const std::string& text, ControlScheme& out, std::string* why = nullptr) {
    std::istringstream in(text);
    std::string head, key; int ver = 0;
    if (!(in >> head >> ver) || head != "okaydrivecontrols" || ver < 1) return Fail(why, key);
    ControlScheme s = ClassicScheme();
    auto okf = [](float v, float lo, float hi) { return v == v && v >= lo && v <= hi; };
    while (in >> key) {
        if (key == "name") { std::string r; std::getline(in, r); if (!r.empty() && r[0] == ' ') r.erase(0, 1); if (r.size() > 64) return Fail(why, key); s.name = r; }
        else if (key == "steerAssist") { if (!(in >> s.steerAssist) || !okf(s.steerAssist, 0, 1)) return Fail(why, key); }
        else if (key == "autoGear" || key == "mouseLook") { int v; if (!(in >> v) || (v != 0 && v != 1)) return Fail(why, key); (key == "autoGear" ? s.autoGear : s.mouseLook) = v == 1; }
        else if (key == "mouseLookSensitivity") { if (!(in >> s.mouseLookSensitivity) || !okf(s.mouseLookSensitivity, 0, 2)) return Fail(why, key); }
        else if (key == "recentreDelay") { if (!(in >> s.recentreDelay) || !okf(s.recentreDelay, 0, 30)) return Fail(why, key); }
        else if (key == "recentreRate") { if (!(in >> s.recentreRate) || !okf(s.recentreRate, 0, 30)) return Fail(why, key); }
        else if (key == "enterHoldTime") { if (!(in >> s.enterHoldTime) || !okf(s.enterHoldTime, 0.05f, 3)) return Fail(why, key); }
        else if (key == "camera") {
            int i; float d, h, f;
            if (!(in >> i >> d >> h >> f) || i < 0 || i >= kCameraViews || !okf(d, 0, 100) || !okf(h, -10, 50) || !okf(f, 20, 120)) return Fail(why, key);
            s.camDistance[i] = d; s.camHeight[i] = h; s.camFov[i] = f;
        } else if (key == "action") {
            std::string n; int t1, c1, n1, t2, c2, n2, inv; ActionConfig c;
            if (!(in >> n >> t1 >> c1 >> n1 >> t2 >> c2 >> n2 >> c.sensitivity >> c.deadZone >> c.curve >> inv)) return Fail(why, key);
            const int a = ActionFromName(n);
            if (a < 0 || (inv != 0 && inv != 1) || !okf(c.sensitivity, 0.1f, 3) || !okf(c.deadZone, 0, 0.9f) || !okf(c.curve, 0.5f, 3)) return Fail(why, key);
            if (t1 < 0 || t1 > Binding::PadTrigger || t2 < 0 || t2 > Binding::PadTrigger) return Fail(why, key);
            c.invert = inv == 1;
            c.primary = {(Binding::Type)t1, c1, n1 != 0}; c.pad = {(Binding::Type)t2, c2, n2 != 0};
            if (!c.primary.Valid() || !c.pad.Valid()) return Fail(why, key);
            if (c.primary.IsPad() || (!c.pad.IsNone() && !c.pad.IsPad())) return Fail(why, key);
            s.act[a] = c;
        } else { std::string skip; std::getline(in, skip); }
    }
    out = s;
    return true;
}

} // namespace drivectl
} // namespace okay

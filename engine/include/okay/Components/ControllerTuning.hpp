#pragma once
// ---------------------------------------------------------------------------
// ControllerTuning: the plain-language "Simple" view of a character controller, as pure data and functions
// (no ImGui), so it is tested headless. Written for this engine from the controllers' own fields.
//
//   * Eight Simple settings (seven on First Person) with a plain name, a unit, a tooltip, a range, a default and
//     "reset to default". Some are friendlier forms of raw fields: "Jump height (m)" sets jumpForce,
//     "Time to full speed (s)" sets walkAcceleration and acceleration, "Time to stop (s)" sets deceleration.
//   * Everything else stays as an Advanced setting. A few raw numbers are Hidden because a Simple setting replaces
//     them (their saved values still load, nothing is removed from the scene file).
//   * Validation: a bad value gets a plain sentence and a suggested fix.
//   * Save / load as plain text (.okaycontroller), shareable between projects; unknown lines are ignored.
// The controllers read their fields every frame, so a change made in Play mode takes effect on the next frame.
// ---------------------------------------------------------------------------
#include "okay/Components/FirstPersonController.hpp"
#include "okay/Components/ThirdPersonController.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace okay {

struct ControllerTuning {
    enum Level { Simple, Advanced, Hidden };

    struct Setting {
        const char* id;       ///< stable key used in saved files
        const char* label;    ///< plain name shown in the Inspector
        const char* unit;     ///< "m/s", "seconds", "m", "0 to 1", ...
        const char* tooltip;  ///< one line: what it changes in play
        const char* meaning;  ///< two settings with the same meaning are a duplicate
        float lo, hi;
    };

    static const std::vector<Setting>& Settings() {
        static const std::vector<Setting> s = {
            {"walkSpeed",    "Walk speed",          "m/s",        "How fast the player moves when walking.", "speed.walk", 0.2f, 12.0f},
            {"runSpeed",     "Sprint speed",        "m/s",        "How fast the player moves while the sprint key is held.", "speed.run", 0.2f, 20.0f},
            {"crouchSpeed",  "Crouch speed",        "m/s",        "How fast the player moves while crouched.", "speed.crouch", 0.1f, 8.0f},
            {"jumpHeight",   "Jump height",         "m",          "How high a jump lifts the player.", "jump.height", 0.0f, 6.0f},
            {"speedUpTime",  "Time to full speed",  "seconds",    "How long it takes to reach full speed from a standstill. Smaller feels snappier.", "time.accel", 0.02f, 3.0f},
            {"stopTime",     "Time to stop",        "seconds",    "How long it takes to stop after you let go of the keys. Smaller stops sooner.", "time.stop", 0.02f, 3.0f},
            {"airControl",   "Air control",         "0 to 1",     "How much you can steer in the air. 0 = none, 1 = as on the ground.", "air.control", 0.0f, 1.0f},
            {"turnSpeed",    "Turn speed",          "per second", "How quickly the character turns to face where it walks (third person only).", "turn.rate", 1.0f, 100.0f},
        };
        return s;
    }
    static const Setting* Find(const std::string& id) {
        for (const Setting& s : Settings()) if (id == s.id) return &s;
        return nullptr;
    }
    /// Third person has the turn setting, first person does not (its body faces the camera).
    template <class C> static constexpr bool HasTurn() { return std::is_same<C, ThirdPersonController>::value; }
    template <class C> static std::vector<const Setting*> SimpleSettings() {
        std::vector<const Setting*> v;
        for (const Setting& s : Settings()) if (HasTurn<C>() || std::string(s.id) != "turnSpeed") v.push_back(&s);
        return v;
    }

    /// Simple, Advanced or Hidden, by registered property name. Hidden = replaced by a Simple setting, or set by
    /// the preset picker. (jumpHeight, speedUpTime and stopTime are Simple but are not raw properties.)
    static Level LevelOf(const std::string& property) {
        static const char* kSimple[] = {"walkSpeed", "runSpeed", "crouchSpeed", "airControl", "turnSpeed"};
        for (const char* n : kSimple) if (property == n) return Simple;
        static const char* kHidden[] = {"jumpForce", "walkAcceleration", "acceleration", "deceleration", "steerRate",
                                        "controllerPreset", "bobGroundGrace"};
        for (const char* n : kHidden) if (property == n) return Hidden;
        return Advanced;
    }

    static constexpr float kGravity = 9.81f;

    // ---- values ----
    template <class C> static float Get(const C& c, const std::string& id, float g = kGravity) {
        if (id == "walkSpeed") return c.walkSpeed;
        if (id == "runSpeed") return c.runSpeed;
        if (id == "crouchSpeed") return c.crouchSpeed;
        if (id == "jumpHeight") return (c.jumpForce * c.jumpForce) / (2.0f * g);
        if (id == "speedUpTime") return c.acceleration > 1e-3f ? c.runSpeed / c.acceleration : 3.0f;
        if (id == "stopTime") return c.deceleration > 1e-3f ? c.runSpeed / c.deceleration : 3.0f;
        if (id == "airControl") return c.airControl;
        if constexpr (HasTurn<C>()) { if (id == "turnSpeed") return c.turnSpeed; }
        return 0.0f;
    }
    /// Set one Simple setting (clamped to its range). Returns false for an unknown id.
    template <class C> static bool Set(C& c, const std::string& id, float v, float g = kGravity) {
        const Setting* s = Find(id);
        if (!s || !std::isfinite(v)) return false;
        if (id == "turnSpeed" && !HasTurn<C>()) return false;
        v = std::min(s->hi, std::max(s->lo, v));
        if (id == "walkSpeed") c.walkSpeed = v;
        else if (id == "runSpeed") c.runSpeed = v;
        else if (id == "crouchSpeed") c.crouchSpeed = v;
        else if (id == "jumpHeight") c.jumpForce = std::sqrt(2.0f * g * v);
        else if (id == "speedUpTime") { c.acceleration = c.runSpeed / v; c.walkAcceleration = c.walkSpeed / v; }
        else if (id == "stopTime") c.deceleration = c.runSpeed / v;
        else if (id == "airControl") c.airControl = v;
        else if (id == "turnSpeed") { if constexpr (HasTurn<C>()) c.turnSpeed = v; }
        return true;
    }
    /// What a fresh controller has for this setting.
    template <class C> static float DefaultOf(const std::string& id, float g = kGravity) { C d; return Get(d, id, g); }
    template <class C> static bool IsDefault(const C& c, const std::string& id, float g = kGravity) {
        return std::fabs(Get(c, id, g) - DefaultOf<C>(id, g)) < 1e-3f;
    }
    /// The "reset to default" button of one setting.
    template <class C> static void Reset(C& c, const std::string& id, float g = kGravity) {
        C d;
        if (id == "speedUpTime") { c.acceleration = d.acceleration; c.walkAcceleration = d.walkAcceleration; return; }
        if (id == "stopTime") { c.deceleration = d.deceleration; return; }
        if (id == "jumpHeight") { c.jumpForce = d.jumpForce; return; }
        Set(c, id, Get(d, id, g), g);
    }

    // ---- validation ----
    struct Issue { std::string id, message, fix; };
    template <class C> static std::vector<Issue> Validate(const C& c, float g = kGravity) {
        std::vector<Issue> v;
        auto add = [&](const char* id, std::string m, std::string f) { v.push_back({id, std::move(m), std::move(f)}); };
        if (c.walkSpeed <= 0.0f) add("walkSpeed", "Walk speed is 0, so the player cannot walk.", "Set it to about 2 m/s.");
        if (c.runSpeed < c.walkSpeed) add("runSpeed", "Sprint speed is lower than walk speed, so sprinting would slow you down.", "Make sprint speed at least as high as walk speed.");
        if (c.crouchSpeed > c.walkSpeed) add("crouchSpeed", "Crouch speed is higher than walk speed, so crouching would speed you up.", "Make crouch speed lower than walk speed.");
        const float h = Get(c, "jumpHeight", g);
        if (h > 3.0f) add("jumpHeight", "A jump this high clears most walls and ceilings (" + Fmt(h) + " m).", "Try 1 to 1.5 m for a normal jump.");
        const float up = Get(c, "speedUpTime"), stop = Get(c, "stopTime");
        if (c.acceleration <= 1e-3f) add("speedUpTime", "The player never speeds up (acceleration is 0).", "Set Time to full speed to about 0.2 s.");
        else if (up > 1.5f) add("speedUpTime", "Time to full speed is " + Fmt(up) + " s, so the player feels stuck in mud.", "Try 0.1 to 0.4 s.");
        if (c.deceleration <= 1e-3f) add("stopTime", "The player never slows down (deceleration is 0) and slides forever.", "Set Time to stop to about 0.15 s.");
        else if (stop > 1.5f) add("stopTime", "Time to stop is " + Fmt(stop) + " s, so the player slides like on ice.", "Try 0.1 to 0.3 s.");
        if (c.airControl < 0.0f || c.airControl > 1.0f) add("airControl", "Air control must be between 0 and 1.", "Use a value from 0 to 1.");
        if (c.slopeLimit > 0.0f && c.slopeLimit < 20.0f) add("slopeLimit", "Slope limit is " + Fmt(c.slopeLimit) + " degrees, so even gentle ramps count as walls.", "Try 45 degrees.");
        return v;
    }

    // ---- save / load as plain text ----
    template <class C> static std::string Save(const C& c, float g = kGravity) {
        std::ostringstream o;
        o << "okaycontroller 1\n";
        for (const Setting* s : SimpleSettings<C>()) o << s->id << ' ' << Fmt(Get(c, s->id, g), 5) << '\n';
        return o.str();
    }
    /// Reads Save() text. Returns how many settings were applied; -1 if it is not a controller file.
    /// Unknown keys and keys that do not apply to this controller are skipped.
    template <class C> static int Load(C& c, const std::string& text, float g = kGravity) {
        std::istringstream in(text);
        std::string head; int ver = 0;
        if (!(in >> head >> ver) || head != "okaycontroller" || ver < 1) return -1;
        std::vector<std::pair<std::string, float>> kv; std::string k; float val;
        while (in >> k >> val) kv.push_back({k, val});
        int n = 0;
        // speeds first: the two time settings are turned into accelerations from them
        for (const char* first : {"walkSpeed", "runSpeed", "crouchSpeed", "jumpHeight", "airControl", "turnSpeed", "speedUpTime", "stopTime"})
            for (auto& p : kv) if (p.first == first && Set(c, p.first, p.second, g)) ++n;
        return n;
    }

private:
    static std::string Fmt(float v, int prec = 2) { char b[32]; std::snprintf(b, sizeof b, "%.*f", prec, (double)v); return b; }
};

} // namespace okay

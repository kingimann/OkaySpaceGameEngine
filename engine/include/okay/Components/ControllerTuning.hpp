#pragma once
// ---------------------------------------------------------------------------
// ControllerTuning: the whole Inspector of a character controller as pure data and functions (no ImGui), so it is
// tested headless. Written for this engine from the controllers' own fields.
//
// There is no Simple / Advanced split. The table below IS the Inspector: a few short sections, every setting with a
// plain name, a unit and a tooltip, a per-setting reset and a good default. Rare features are ONE plain on/off box;
// their one or two numbers appear only while the box is on. Everything else (about 70 to 90 low-level fields) is
// automatic: it keeps a good default or is derived from the plain settings, is not shown, and old scenes still load
// every saved value (nothing is deleted from the file format).
//
// Merges: "Jump height" replaces jumpForce; "Time to full speed" replaces walkAcceleration + acceleration;
// "Time to stop" replaces deceleration; "Jump forgiveness" is coyoteTime + jumpBufferTime in one; "Slide and dive"
// is canSlide + diveOnProneWhileSprinting; "Lean" is the two lean keys; "Climb over obstacles" is vault + mantle +
// ledge grab. Validation gives a plain sentence and a fix. Save / load is plain text (.okaycontroller).
// The controllers read their fields every frame, so a change made in Play mode applies on the next frame.
// ---------------------------------------------------------------------------
#include "okay/Components/FirstPersonController.hpp"
#include "okay/Components/ThirdPersonController.hpp"
#include "okay/Scene/PropertyBuilder.hpp"
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
    struct Setting {
        const char* id;        ///< stable key used in saved files
        const char* label;     ///< plain name shown in the Inspector
        const char* unit;      ///< "m/s", "seconds", "on / off", ...
        const char* tooltip;   ///< one line: what it changes in play
        const char* meaning;   ///< two settings with the same meaning are a duplicate
        float lo, hi;
        const char* section;   ///< Movement, Jumping, Crouching, Camera, Extra moves
        const char* requires_; ///< id of the on/off box that reveals this number, or ""
        bool toggle;           ///< a plain on/off box
    };

    static const std::vector<Setting>& Settings() {
        static const std::vector<Setting> s = {
            {"walkSpeed",      "Walk speed",         "m/s",        "How fast the player moves when walking.", "speed.walk", 0.2f, 12.0f, "Movement", "", false},
            {"runSpeed",       "Sprint speed",       "m/s",        "How fast the player moves while the sprint key is held.", "speed.run", 0.2f, 20.0f, "Movement", "", false},
            {"speedUpTime",    "Time to full speed", "seconds",    "How long it takes to reach full speed from a standstill. Smaller feels snappier.", "time.accel", 0.02f, 3.0f, "Movement", "", false},
            {"stopTime",       "Time to stop",       "seconds",    "How long it takes to stop after you let go of the keys. Smaller stops sooner.", "time.stop", 0.02f, 3.0f, "Movement", "", false},
            {"airControl",     "Air control",        "0 to 1",     "How much you can steer in the air. 0 = none, 1 = as on the ground.", "air.control", 0.0f, 1.0f, "Movement", "", false},
            {"turnSpeed",      "Turn speed",         "per second", "How quickly the character turns to face where it walks (third person only).", "turn.rate", 1.0f, 100.0f, "Movement", "", false},
            {"jumpHeight",     "Jump height",        "m",          "How high a jump lifts the player.", "jump.height", 0.0f, 6.0f, "Jumping", "", false},
            {"jumpForgiveness","Jump forgiveness",   "seconds",    "How long after walking off a ledge, or before landing, a jump press still works.", "jump.forgive", 0.0f, 0.4f, "Jumping", "", false},
            {"crouchSpeed",    "Crouch speed",       "m/s",        "How fast the player moves while crouched.", "speed.crouch", 0.1f, 8.0f, "Crouching", "", false},
            {"lookSensitivity","Look speed",         "degrees per mouse pixel", "How far the view turns for each pixel the mouse moves.", "look.rate", 0.01f, 2.0f, "Camera", "", false},
            {"invertLook",     "Invert up and down look", "on / off", "Moving the mouse up looks down, like a flight stick.", "look.invert", 0.0f, 1.0f, "Camera", "", true},
            {"headBob",        "Head bob",           "on / off",   "The view bobs up and down while walking (first person only).", "feature.bob", 0.0f, 1.0f, "Camera", "", true},
            {"bobAmount",      "Head bob size",      "m",          "How far the view moves up and down while walking.", "bob.size", 0.0f, 0.2f, "Camera", "headBob", false},
            {"canSlide",       "Slide and dive",     "on / off",   "Crouching while sprinting slides; going prone while sprinting dives.", "feature.slide", 0.0f, 1.0f, "Extra moves", "", true},
            {"slideSpeed",     "Slide speed",        "m/s",        "How fast the slide starts.", "slide.speed", 1.0f, 20.0f, "Extra moves", "canSlide", false},
            {"canDodge",       "Dodge",              "on / off",   "A key press makes a quick burst sideways or backwards.", "feature.dodge", 0.0f, 1.0f, "Extra moves", "", true},
            {"dodgeSpeed",     "Dodge speed",        "m/s",        "How fast the dodge burst is.", "dodge.speed", 1.0f, 30.0f, "Extra moves", "canDodge", false},
            {"canLean",        "Lean",               "on / off",   "Q and E lean the view left and right.", "feature.lean", 0.0f, 1.0f, "Extra moves", "", true},
            {"leanAngle",      "Lean angle",         "degrees",    "How far the view tilts when leaning.", "lean.angle", 1.0f, 45.0f, "Extra moves", "canLean", false},
            {"canClimb",       "Climb over obstacles", "on / off", "Vault low walls, mantle onto ledges and hang from edges.", "feature.climb", 0.0f, 1.0f, "Extra moves", "", true},
        };
        return s;
    }
    static const Setting* Find(const std::string& id) {
        for (const Setting& s : Settings()) if (id == s.id) return &s;
        return nullptr;
    }
    /// Third person has a turn setting (first person's body faces the camera); only first person has head bob.
    template <class C> static constexpr bool IsThird() { return std::is_same<C, ThirdPersonController>::value; }
    template <class C> static bool Applies(const Setting& s) {
        const std::string id = s.id;
        if (id == "turnSpeed") return IsThird<C>();
        if (id == "headBob" || id == "bobAmount") return !IsThird<C>();
        return true;
    }
    /// Everything the Inspector can ever show for this controller (revealed numbers included).
    template <class C> static std::vector<const Setting*> Shown() {
        std::vector<const Setting*> v;
        for (const Setting& s : Settings()) if (Applies<C>(s)) v.push_back(&s);
        return v;
    }
    /// The always-visible settings (every feature box off).
    template <class C> static std::vector<const Setting*> Base() {
        std::vector<const Setting*> v;
        for (const Setting* s : Shown<C>()) if (!*s->requires_) v.push_back(s);
        return v;
    }

    static constexpr float kGravity = 9.81f;

    /// The setting an old registered property belongs to ("" = that property is automatic and not shown).
    static const char* SettingFor(const std::string& property) {
        static const std::pair<const char*, const char*> m[] = {
            {"walkSpeed", "walkSpeed"}, {"runSpeed", "runSpeed"}, {"crouchSpeed", "crouchSpeed"}, {"airControl", "airControl"},
            {"turnSpeed", "turnSpeed"}, {"mouseSensitivity", "lookSensitivity"}, {"invertY", "invertLook"}, {"headBob", "headBob"},
            {"bobAmount", "bobAmount"}, {"canSlide", "canSlide"}, {"slideSpeed", "slideSpeed"}, {"canDodge", "canDodge"},
            {"dodgeImpulse", "dodgeSpeed"}, {"leanAngle", "leanAngle"},
        };
        for (auto& p : m) if (property == p.first) return p.second;
        return "";
    }
    /// Properties the Inspector does not show. They keep their saved values and good defaults and old scenes still
    /// load them; the plain settings set or derive them where that makes sense.
    static bool IsAutomatic(const std::string& property) { return !*SettingFor(property); }

    /// Give a registered controller property its Inspector level, plain label, unit and tooltip from the table.
    static void Annotate(PropertyDesc& d) {
        const char* id = SettingFor(d.name);
        if (!*id) { d.level = PropertyLevel::Hidden; d.flags |= PropertyFlag::HideInInspector; return; }
        d.level = PropertyLevel::Simple;
        if (const Setting* s = Find(id)) { d.label = s->label; d.unit = s->unit; d.tooltip = s->tooltip; }
    }

    // ---- values (on / off boxes read as 0 or 1) ----
    template <class C> static float Get(const C& c, const std::string& id, float g = kGravity) {
        if (id == "walkSpeed") return c.walkSpeed;
        if (id == "runSpeed") return c.runSpeed;
        if (id == "crouchSpeed") return c.crouchSpeed;
        if (id == "jumpHeight") return (c.jumpForce * c.jumpForce) / (2.0f * g);
        if (id == "jumpForgiveness") return c.coyoteTime;
        if (id == "speedUpTime") return c.acceleration > 1e-3f ? c.runSpeed / c.acceleration : 3.0f;
        if (id == "stopTime") return c.deceleration > 1e-3f ? c.runSpeed / c.deceleration : 3.0f;
        if (id == "airControl") return c.airControl;
        if (id == "lookSensitivity") return c.mouseSensitivity;
        if (id == "invertLook") return c.invertY ? 1.0f : 0.0f;
        if (id == "canSlide") return c.canSlide ? 1.0f : 0.0f;
        if (id == "slideSpeed") return c.slideSpeed;
        if (id == "canDodge") return c.dodge.canDodge ? 1.0f : 0.0f;
        if (id == "dodgeSpeed") return c.dodge.impulse;
        if (id == "canLean") return (c.leanLeftKey && c.leanRightKey) ? 1.0f : 0.0f;
        if (id == "leanAngle") return c.leanAngle;
        if (id == "canClimb") return (c.canVault && c.canMantle && c.canLedgeGrab) ? 1.0f : 0.0f;
        if constexpr (IsThird<C>()) { if (id == "turnSpeed") return c.turnSpeed; }
        else { if (id == "headBob") return c.headBob ? 1.0f : 0.0f; if (id == "bobAmount") return c.bobAmount; }
        return 0.0f;
    }
    /// Set one setting (clamped to its range). False for an unknown id or one that does not apply here.
    template <class C> static bool Set(C& c, const std::string& id, float v, float g = kGravity) {
        const Setting* s = Find(id);
        if (!s || !Applies<C>(*s) || !std::isfinite(v)) return false;
        v = std::min(s->hi, std::max(s->lo, v));
        const bool on = v > 0.5f;
        if (id == "walkSpeed") c.walkSpeed = v;
        else if (id == "runSpeed") c.runSpeed = v;
        else if (id == "crouchSpeed") c.crouchSpeed = v;
        else if (id == "jumpHeight") c.jumpForce = std::sqrt(2.0f * g * v);
        else if (id == "jumpForgiveness") { c.coyoteTime = v; c.jumpBufferTime = v; }
        else if (id == "speedUpTime") { c.acceleration = c.runSpeed / v; c.walkAcceleration = c.walkSpeed / v; }
        else if (id == "stopTime") c.deceleration = c.runSpeed / v;
        else if (id == "airControl") c.airControl = v;
        else if (id == "lookSensitivity") c.mouseSensitivity = v;
        else if (id == "invertLook") c.invertY = on;
        else if (id == "canSlide") { c.canSlide = on; c.diveOnProneWhileSprinting = on; }
        else if (id == "slideSpeed") c.slideSpeed = v;
        else if (id == "canDodge") c.dodge.canDodge = on;
        else if (id == "dodgeSpeed") c.dodge.impulse = v;
        else if (id == "canLean") { c.leanLeftKey = on ? 'q' : 0; c.leanRightKey = on ? 'e' : 0; }
        else if (id == "leanAngle") c.leanAngle = v;
        else if (id == "canClimb") { c.canVault = on; c.canMantle = on; c.canLedgeGrab = on; }
        else if constexpr (IsThird<C>()) { if (id == "turnSpeed") c.turnSpeed = v; }
        else { if (id == "headBob") c.headBob = on; else if (id == "bobAmount") c.bobAmount = v; }
        return true;
    }
    /// What a fresh controller has for this setting.
    template <class C> static float DefaultOf(const std::string& id, float g = kGravity) { C d; return Get(d, id, g); }
    template <class C> static bool IsDefault(const C& c, const std::string& id, float g = kGravity) {
        return std::fabs(Get(c, id, g) - DefaultOf<C>(id, g)) < 1e-3f;
    }
    /// The per-setting reset button: restores the raw fields behind the setting too.
    template <class C> static void Reset(C& c, const std::string& id, float g = kGravity) {
        C d;
        if (id == "speedUpTime") { c.acceleration = d.acceleration; c.walkAcceleration = d.walkAcceleration; return; }
        if (id == "stopTime") { c.deceleration = d.deceleration; return; }
        if (id == "jumpHeight") { c.jumpForce = d.jumpForce; return; }
        if (id == "jumpForgiveness") { c.coyoteTime = d.coyoteTime; c.jumpBufferTime = d.jumpBufferTime; return; }
        if (id == "canLean") { c.leanLeftKey = d.leanLeftKey; c.leanRightKey = d.leanRightKey; return; }
        if (id == "canClimb") { c.canVault = d.canVault; c.canMantle = d.canMantle; c.canLedgeGrab = d.canLedgeGrab; return; }
        if (id == "canSlide") { c.canSlide = d.canSlide; c.diveOnProneWhileSprinting = d.diveOnProneWhileSprinting; return; }
        Set(c, id, Get(d, id, g), g);
    }
    /// What is on screen right now: the always-visible settings plus the numbers of boxes that are on.
    template <class C> static std::vector<const Setting*> Visible(const C& c) {
        std::vector<const Setting*> v;
        for (const Setting* s : Shown<C>()) if (!*s->requires_ || Get(c, s->requires_) > 0.5f) v.push_back(s);
        return v;
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
        for (const Setting* s : Shown<C>()) o << s->id << ' ' << Fmt(Get(c, s->id, g), 5) << '\n';
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
        // the two speeds first: the two time settings are turned into accelerations from them
        for (const char* first : {"walkSpeed", "runSpeed"})
            for (auto& p : kv) if (p.first == first && Set(c, p.first, p.second, g)) ++n;
        for (auto& p : kv) if (p.first != "walkSpeed" && p.first != "runSpeed" && Set(c, p.first, p.second, g)) ++n;
        return n;
    }

private:
    static std::string Fmt(float v, int prec = 2) { char b[32]; std::snprintf(b, sizeof b, "%.*f", prec, (double)v); return b; }
};

} // namespace okay

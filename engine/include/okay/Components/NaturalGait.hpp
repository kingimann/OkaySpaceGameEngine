#pragma once
// The natural walk and run: the default speeds, the way back to the old ones, and the one function behind the
// Inspector's "Use natural walk and run speeds" button (docs/walk_vs_run.md).
//
// Why these numbers. A person walks at about 1.4 m/s and tops out near 2 m/s: past about 2.1 m/s (for a leg as
// long as the built-in character's) the stride no longer fits a walk and the body breaks into a jog. A jog is
// 2.5 to 3.5 m/s, a hard run 5 to 6, a sprint 8 and up. The controllers used to start at 4.5 m/s "walk" and
// 8 m/s "run": the first is a jog and the second a sprint, so both looked like running. 2.0 and 5.5 m/s are
// the top of a walk and a quick run. Written from those general facts; no table or value from another project.
//
// Header only and light (no controller is included), because the controllers include it for their defaults.
#include "okay/Animation/HumanoidMotion.hpp"
#include <string>

namespace okay {
namespace NaturalGait {

// ---- The natural defaults (m/s) ----------------------------------------------------------------------------
constexpr float kWalkSpeed = 2.0f;         ///< the top of a walk
constexpr float kRunSpeed = 5.5f;          ///< a quick run (First Person and Third Person)
constexpr float kShooterRunSpeed = 5.0f;   ///< the Third Person Shooter's run (it was always a little slower)
constexpr float kCrouchSpeed = 1.0f;       ///< crouched, about half the walk (the old 2.2 would pass the new walk)
constexpr float kProneSpeed = 0.5f;        ///< a crawl, about a quarter of it

// ---- The old defaults: what scenes saved before the change spell out, and what OKAY_OLD_GAIT=1 gives back ----
constexpr float kOldWalkSpeed = 4.5f;
constexpr float kOldRunSpeed = 8.0f;
constexpr float kOldShooterRunSpeed = 7.5f;
constexpr float kOldCrouchSpeed = 2.2f;
constexpr float kOldProneSpeed = 1.1f;

/// True while the old gait is switched on (OKAY_OLD_GAIT=1 in the environment, or HumanoidMotion::SetOldGait(1)).
inline bool Old() { return HumanoidMotion::OldGait(); }

/// What a controller starts with. They follow the switch, so a new object made while OKAY_OLD_GAIT=1 is set
/// gets the old numbers and one made without it gets the natural ones.
inline float WalkSpeed()       { return Old() ? kOldWalkSpeed : kWalkSpeed; }
inline float RunSpeed()        { return Old() ? kOldRunSpeed : kRunSpeed; }
inline float ShooterRunSpeed() { return Old() ? kOldShooterRunSpeed : kShooterRunSpeed; }
inline float CrouchSpeed()     { return Old() ? kOldCrouchSpeed : kCrouchSpeed; }
inline float ProneSpeed()      { return Old() ? kOldProneSpeed : kProneSpeed; }

/// True when `v` is one of the old default speeds. Attaching a character model refits a speed that was never
/// touched, and a scene saved before the change spells the old defaults out: they count as untouched.
inline bool IsOldDefaultWalk(float v) { return v == kOldWalkSpeed; }
inline bool IsOldDefaultRun(float v)  { return v == kOldRunSpeed || v == kOldShooterRunSpeed; }

// ---- The hint under Walk Speed ------------------------------------------------------------------------------
/// Above this walk speed (m/s) the legs have to jog: the stride of a walk does not fit it any more.
constexpr float kJogAbove = 3.0f;
inline bool WalkSpeedNeedsJog(float walk) { return walk > kJogAbove; }
/// The line shown under Walk Speed (empty when the speed is a walk).
inline std::string WalkSpeedHint(float walk) {
    if (!WalkSpeedNeedsJog(walk)) return std::string();
    return "Above about 3 the legs have to jog: a real walk tops out near 2. Use natural walk and run speeds for a clear walk.";
}
/// The tooltip of the "Use natural walk and run speeds" button.
inline const char* ButtonTooltip() {
    return "Sets Walk Speed to 2.0 and Run Speed to 5.5 (5.0 for the shooter): a real walk and a real run, clearly different.\n"
           "Crouch and Prone Speed change to 1.0 and 0.5 only if they are still the old 2.2 and 1.1.\n"
           "Ctrl+Z undoes it. Older projects keep their saved speeds until you press this.";
}

/// What the button sets. `runNatural` is the controller's own natural run speed (kRunSpeed, or kShooterRunSpeed).
/// Walk and run are set outright (the person pressed the button); crouch and prone only when they are still the
/// old defaults, so a speed somebody chose is theirs. Returns how many of the four values changed.
inline int UseNaturalSpeeds(float& walk, float& run, float& crouch, float& prone, float runNatural) {
    int changed = 0;
    auto set = [&](float& v, float to) { if (v != to) { v = to; ++changed; } };
    set(walk, kWalkSpeed);
    set(run, runNatural);
    if (crouch == kOldCrouchSpeed) set(crouch, kCrouchSpeed);
    if (prone == kOldProneSpeed) set(prone, kProneSpeed);
    return changed;
}

} // namespace NaturalGait
} // namespace okay

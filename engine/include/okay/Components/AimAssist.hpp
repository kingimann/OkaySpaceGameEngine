#pragma once
// Controller aim assist (docs/accessibility.md). The right stick already looks
// around; when Accessibility aim assist is above Off, a stick that is already
// moving is pulled a short way toward the nearest living target in a small cone.
// A still stick does nothing (no tracking on its own). Mouse look is not passed
// through here, so mouse-only aim is unchanged at every setting, including Off.
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Core/AccessibilitySettings.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Math/Vec3.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include <cmath>
#include <vector>

namespace okay {

/// Look angles in a controller's own space. Yaw 0 looks down -Z and a negative
/// yaw looks to the right. `pitch` uses that controller's sign.
struct LookAim {
    float yaw = 0.0f;
    float pitch = 0.0f;
};

/// Degrees per second the right stick turns the view at a full push.
constexpr float kStickLookDegPerSec = 140.0f;
/// Stick deflection below this (after the gamepad dead zone) is not a look.
constexpr float kStickLookDeadzone = 0.2f;

/// World point to look angles. `pitchUp`: positive pitch looks up (first person).
/// When false, positive pitch looks down (third-person orbit and the shooter).
inline LookAim LookAimAt(const Vec3& eye, const Vec3& point, bool pitchUp) {
    const Vec3 d = point - eye;
    const float horiz = std::sqrt(d.x * d.x + d.z * d.z);
    LookAim a;
    a.yaw = std::atan2(-d.x, -d.z) * Mathf::Rad2Deg;
    const float up = std::atan2(d.y, horiz > 1e-4f ? horiz : 1e-4f) * Mathf::Rad2Deg;
    a.pitch = pitchUp ? up : -up;
    return a;
}

/// Living targets another object can be helped toward: a Health stat that is
/// not dead, not this body, between 0.75 m and 40 m. The aim point is 1.2 m
/// above the object's origin (about chest height when the origin is at the feet).
inline void GatherAimTargets(Scene* scene, GameObject* self, const Vec3& eye, bool pitchUp,
                             std::vector<LookAim>& out) {
    out.clear();
    if (!scene) return;
    for (const auto& up : scene->Objects()) {
        if (!up || !up->active || !up->transform) continue;
        if (self && (up.get() == self || up->IsSelfOrDescendantOf(self))) continue;
        const HealthStat* hp = up->GetComponent<HealthStat>();
        if (!hp || hp->dead) continue;
        const Vec3 p = up->transform->Position() + Vec3{0.0f, 1.2f, 0.0f};
        const Vec3 d = p - eye;
        const float dist2 = d.x * d.x + d.y * d.y + d.z * d.z;
        if (dist2 < 0.75f * 0.75f || dist2 > 40.0f * 40.0f) continue;
        out.push_back(LookAimAt(eye, p, pitchUp));
    }
}

struct StickAim {
    float yaw = 0.0f;
    float pitch = 0.0f;
    bool pulled = false;   ///< a target moved the result past the stick alone
};

/// Add this frame's stick look (`dYaw`, `dPitch`, already signed for the
/// controller) and, when `strength` is above 0, a capped pull toward the
/// nearest target inside a cone. Strength 0 returns the stick result with no
/// pull. A zero stick delta returns the angles unchanged, target or not.
inline StickAim ApplyStickAim(float yaw, float pitch, float dYaw, float dPitch,
                              float strength, float dt, const LookAim* targets, int count) {
    StickAim o;
    o.yaw = yaw + dYaw;
    o.pitch = pitch + dPitch;
    const bool stick = std::fabs(dYaw) + std::fabs(dPitch) > 1e-4f;
    if (!stick || strength <= 0.0f || !targets || count <= 0 || dt <= 0.0f) return o;
    if (strength > 1.0f) strength = 1.0f;
    const float cone = 8.0f + 14.0f * strength;
    float best = 1.0e9f, by = 0.0f, bp = 0.0f;
    for (int i = 0; i < count; ++i) {
        const float dy = std::remainder(targets[i].yaw - o.yaw, 360.0f);
        const float dp = targets[i].pitch - o.pitch;
        const float ang = std::sqrt(dy * dy + dp * dp);
        if (ang < best) { best = ang; by = dy; bp = dp; }
    }
    if (best > cone || best < 0.05f) return o;
    const float step = std::min(best, (40.0f + 80.0f * strength) * dt);
    o.yaw += (by / best) * step;
    o.pitch += (bp / best) * step;
    o.pulled = true;
    return o;
}

/// Right-stick contribution for one frame, in degrees. `pitchUp` matches LookAimAt.
/// `invertY` flips the vertical stick the way the controller's mouse invert does.
inline void StickLookDelta(float stickX, float stickY, float dt, bool pitchUp, bool invertY,
                           float& dYaw, float& dPitch) {
    const float rate = kStickLookDegPerSec * (dt > 0.05f ? 0.05f : (dt < 0.0f ? 0.0f : dt));
    dYaw = -stickX * rate;
    const float up = (invertY ? -stickY : stickY) * rate;
    dPitch = pitchUp ? up : -up;
}

} // namespace okay

#pragma once
// A dodge impulse for the first- and third-person controllers. The slide
// burst is separate and is not changed here. canDodge stays off, so a key
// does nothing until a game opts in. dodge() from a script still spends
// stamina (when a Stamina stat is present) and shoves the body.
#include "okay/Animation/HumanoidActions.hpp"
#include "okay/Components/SurvivalComponents.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include <cmath>

namespace okay {

struct DodgeMove {
    bool  canDodge = false;     ///< key dodge. Off, so games do not change.
    char  key = 0;              ///< 0 = no key. Script dodge() does not need one.
    float impulse = 7.5f;       ///< horizontal speed for the burst
    float duration = 0.35f;
    float cooldown = 0.55f;
    float stamina = 18.0f;      ///< spent when a Stamina stat is on this body
    bool  iFrames = false;      ///< off: Health still applies damage during a dodge
    float timer = 0.0f;
    float wait = 0.0f;
    Vec3  dir{0.0f, 0.0f, -1.0f};
};

/// 0 forward, 1 back, 2 left, 3 right, in the controller's move frame.
inline int DodgeDir(const Vec3& wish, const Vec3& fwd, const Vec3& right) {
    Vec3 d = wish;
    d.y = 0.0f;
    if (d.SqrMagnitude() < 1e-6f) d = fwd;
    const float f = Vec3::Dot(d, fwd);
    const float r = Vec3::Dot(d, right);
    if (std::fabs(f) >= std::fabs(r)) return f >= 0.0f ? 0 : 1;
    return r >= 0.0f ? 3 : 2;
}

inline bool DodgeBurst(GameObject* go, DodgeMove& d, const Vec3& worldDir) {
    if (!go) return false;
    if (d.timer > 0.0f || d.wait > 0.0f) return false;
    if (auto* st = go->GetComponent<StaminaStat>())
        if (!st->TrySpend(d.stamina)) return false;
    Vec3 dir = worldDir;
    dir.y = 0.0f;
    if (dir.SqrMagnitude() < 1e-6f && go->transform)
        dir = go->transform->TransformDirection(Vec3{0.0f, 0.0f, -1.0f});
    const float m = dir.Magnitude();
    if (m < 1e-4f) return false;
    dir = dir * (1.0f / m);
    d.dir = dir;
    d.timer = d.duration;
    d.wait = d.cooldown;
    if (auto* rb = go->GetComponent<Rigidbody3D>()) {
        rb->velocity.x = dir.x * d.impulse;
        rb->velocity.z = dir.z * d.impulse;
    }
    if (d.iFrames) HumanoidActions::SetDodgeIFrames(go, true);
    return true;
}

inline void DodgeTick(GameObject* go, DodgeMove& d, float dt) {
    if (d.wait > 0.0f) d.wait = d.wait > dt ? d.wait - dt : 0.0f;
    if (d.timer <= 0.0f) return;
    d.timer -= dt;
    if (auto* rb = go ? go->GetComponent<Rigidbody3D>() : nullptr) {
        rb->velocity.x = d.dir.x * d.impulse;
        rb->velocity.z = d.dir.z * d.impulse;
    }
    if (d.timer <= 0.0f && d.iFrames) HumanoidActions::SetDodgeIFrames(go, false);
}

} // namespace okay

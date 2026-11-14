#pragma once
// ---------------------------------------------------------------------------
// Dolphin dive — sprint, then prone. One call from a controller's Update,
// after it has written this frame's velocity (so the launch replaces air
// steering). The collider still goes through the stance code those controllers
// already use. A wall in front keeps the taller crouch shape and does not launch.
// ---------------------------------------------------------------------------
#include "okay/Animation/HumanoidActions.hpp"
#include "okay/Components/StepUp.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include <algorithm>
#include <cmath>

namespace okay {

/// Runtime dive. flight < 0 means not in the air. slide > 0 is the landing glide.
struct DiveMotion {
    float flight = -1.0f;
    float slide = 0.0f;
    float speed = 0.0f;
    Vec3  dir{0.0f, 0.0f, -1.0f};
    int   prevWant = 0;   ///< last stance asked for (0 stand, 1 crouch, 2 prone)
    bool  active() const { return flight >= 0.0f || slide > 0.0f; }
};

struct DiveTune {
    bool  enabled = true;     ///< prone-while-sprinting shortcut (script dive() ignores this)
    float hop = 0.25f;        ///< upward launch, metres
    float speedScale = 1.15f; ///< times current speed
    float speedCap = 16.0f;
    float slideTime = 0.45f;  ///< friction glide after landing, then prone
    float proneSpeed = 1.1f;
    float crouchFrac = 0.65f;
    float proneFrac = 0.33f;
    bool  resize = true;
};

/// When drive is set, the controller copies stance (0/1/2) and level over the
/// keys for this frame and sizes the collider.
struct DivePose { bool drive = false; int stance = 0; float level = 0.0f; };

struct DiveOut {
    bool started = false;
    bool active = false;
    DivePose pose;
};

inline DiveOut TickDive(GameObject* go, Rigidbody3D* rb, DiveMotion& m, StanceBody& shape,
                        const DiveTune& tune, bool grounded, bool wasSprinting, int want,
                        const Vec3& forwardIn, float horizSpeed, float dt) {
    DiveOut out;
    Vec3 forward = forwardIn;
    forward.y = 0.0f;
    const float fl = std::sqrt(forward.x * forward.x + forward.z * forward.z);
    if (fl > 1e-4f) { forward.x /= fl; forward.z /= fl; }
    else forward = {0.0f, 0.0f, -1.0f};

    const bool scripted = HumanoidActions::TakeDivePending(go);
    const bool ask = grounded && !m.active() &&
                     (scripted || (tune.enabled && wasSprinting && want == 2 && m.prevWant != 2));

    auto pose = [&](int stance, float level) {
        out.pose.drive = true;
        out.pose.stance = stance;
        out.pose.level = level;
        if (!tune.resize || !go) return;
        CaptureStance(go, shape);
        const float h = shape.standH * StanceValue(level, 1.0f, tune.crouchFrac, tune.proneFrac);
        ApplyStanceHeight(go, shape, h);
    };
    auto blocked = [&]() {
        if (!go || !go->transform || !go->scene()) return false;
        Scene& sc = *go->scene();
        const Vec3 from = go->transform->Position() + Vec3{0.0f, 0.22f, 0.0f};
        const RaycastHit3D wall = sc.physics3D().Raycast(sc, from, forward, 0.85f, go,
                                                         Physics3D::kAllLayers, false);
        return wall.hit && wall.distance < 0.70f;
    };
    auto ceiling = [&]() {
        if (!go || !go->transform || !go->scene()) return false;
        Scene& sc = *go->scene();
        const Vec3 from = go->transform->Position() + Vec3{0.0f, 1.05f, 0.0f};
        const RaycastHit3D up = sc.physics3D().Raycast(sc, from, Vec3{0.0f, 1.0f, 0.0f}, 0.40f, go,
                                                       Physics3D::kAllLayers, false);
        return up.hit;
    };

    if (ask) {
        if (blocked()) {
            // No room to go flat: keep the larger shape and end crouched.
            pose(1, 1.0f);
            m.prevWant = 1;
        } else {
            m.flight = 0.0f;
            m.slide = 0.0f;
            m.dir = forward;
            m.speed = std::min(tune.speedCap, std::max(horizSpeed, 1.0f) * tune.speedScale);
            if (rb) {
                rb->velocity.x = m.dir.x * m.speed;
                rb->velocity.z = m.dir.z * m.speed;
                if (!ceiling()) {
                    float g = 9.81f;
                    if (go && go->scene()) g = std::fabs(go->scene()->physics3D().gravity.y);
                    if (g < 0.5f) g = 9.81f;
                    const float hop = std::max(0.0f, tune.hop);
                    rb->velocity.y = std::sqrt(2.0f * g * hop);
                }
            }
            HumanoidActions::BeginDive(go);
            out.started = true;
            pose(2, 0.2f);
            m.prevWant = 2;
        }
    } else if (m.flight >= 0.0f) {
        m.flight += dt;
        if (rb) {
            const float y = rb->velocity.y;
            rb->velocity.x = m.dir.x * m.speed;
            rb->velocity.z = m.dir.z * m.speed;
            rb->velocity.y = y;   // air steering stays off; gravity still owns y
        }
        pose(2, std::min(1.0f, m.flight / 0.55f) * 2.0f);
        if (grounded && m.flight > 0.12f) {
            m.flight = -1.0f;
            m.slide = std::max(0.05f, tune.slideTime);
        }
        m.prevWant = 2;
    } else if (m.slide > 0.0f) {
        const float span = std::max(0.05f, tune.slideTime);
        const float k = std::clamp(m.slide / span, 0.0f, 1.0f);
        const float sp = tune.proneSpeed + (m.speed - tune.proneSpeed) * k * k;
        if (rb && grounded) {
            rb->velocity.x = m.dir.x * sp;
            rb->velocity.z = m.dir.z * sp;
        }
        pose(2, 2.0f);
        m.slide -= dt;
        if (m.slide < 0.0f) m.slide = 0.0f;
        m.prevWant = 2;
    } else {
        m.prevWant = want;
    }
    out.active = m.active();
    return out;
}

} // namespace okay

#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Physics/PlayerCollision.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/CharacterIK.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Net/NetOwnership.hpp"
#include "okay/Math/Mathf.hpp"
#include <cmath>

namespace okay {

/// Third-person TOP-DOWN player controller (twin-stick / ARPG / RTS-hero feel). The
/// player moves on the XZ plane with WASD / left-stick and turns to face the way it's
/// travelling; a fixed high, angled camera follows from above. Movement has momentum
/// (accel/decel) for weight. Drives a sibling Rigidbody3D's velocity when present
/// (collisions), else moves the Transform. Drives a Character's walk/run animation.
///
/// Setup: attach to the player; have a main Camera in the scene (the controller
/// positions it each frame). Movement is world-relative by default, or camera-relative.
class TopDownController : public Behaviour {
public:
    // ---- Movement ----
    float walkSpeed = 5.0f;
    float runSpeed  = 8.0f;
    char  sprintKey = 0;                // hold to run (0 = disabled)
    bool  driveAnimation = true;
    bool  footIK = false;               // plant the Character's feet on the ground
    bool  rotateToFace = true;          // turn the body toward travel
    float turnSpeed = 14.0f;
    // Momentum: ramp horizontal velocity toward the target (units/s^2).
    float acceleration = 60.0f;
    float deceleration = 55.0f;
    // Movement axes: world-aligned (W = +world Z forward) or relative to the camera yaw.
    bool  cameraRelative = false;

    // ---- Camera (fixed angle, follows from above) ----
    float cameraDistance = 12.0f;       // how far the camera sits from the player
    float cameraPitch = 60.0f;          // downward tilt (deg); higher = more overhead
    float cameraYaw = 0.0f;             // fixed orbit yaw (deg)
    float lookHeight = 0.6f;            // look target height above the player origin
    float cameraDamping = 12.0f;        // 0 = instant follow; higher = snappier

    void Start() override { if (footIK) AttachCharacterFootIK(gameObject); }

    void Update(float dt) override {
        if (!transform) return;
        if (!IsLocallyControlled(gameObject)) return;   // remote proxy: NetworkSync drives it

        Vec2 axis = Input::AxisWASD();
        Vec2 pad  = Input::GamepadAxis();
        if (Mathf::Abs(pad.x) + Mathf::Abs(pad.y) > 0.15f) axis = pad;
        Quat basis = cameraRelative ? Quat::Euler(0, cameraYaw, 0) : Quat::Identity;
        Vec3 fwd = basis * Vec3{0, 0, -1}, right = basis * Vec3::Right;
        Vec3 dir = fwd * axis.y + right * axis.x;
        float len = std::sqrt(dir.x * dir.x + dir.z * dir.z);
        bool moving = len > 0.01f;
        if (moving) { dir.x /= len; dir.z /= len; }
        bool running = sprintKey && Input::GetKey(sprintKey) && moving;
        float speed = running ? runSpeed : walkSpeed;

        if (auto* rb = gameObject ? gameObject->GetComponent<Rigidbody3D>() : nullptr) {
            Vec3 cur{rb->velocity.x, 0.0f, rb->velocity.z};
            Vec3 dv{dir.x * speed - cur.x, 0.0f, dir.z * speed - cur.z};
            float rate = moving ? acceleration : deceleration;
            float dl = std::sqrt(dv.x * dv.x + dv.z * dv.z), step = rate * dt;
            if (dl > 1e-5f && dl > step) { dv.x = dv.x / dl * step; dv.z = dv.z / dl * step; }
            rb->velocity.x = cur.x + dv.x;
            rb->velocity.z = cur.z + dv.z;
        } else if (moving) {
            transform->Translate(dir * (speed * dt));
            if (gameObject && gameObject->scene())
                ResolvePlayerBody(*gameObject->scene(), gameObject);   // no clipping
        }

        if (rotateToFace && moving) {
            Quat want = Quat::LookRotation({-dir.x, 0.0f, -dir.z});   // mesh faces -Z
            float t = 1.0f - std::exp(-turnSpeed * dt);
            transform->localRotation = Quat::Slerp(transform->localRotation, want, t);
        }

        if (driveAnimation)
            if (Character* ch = FindCharacter())
                ch->anim = moving ? (running ? 3 : 2) : 1;
    }

    void LateUpdate(float dt) override {
        if (!transform || !gameObject) return;
        Scene* sc = gameObject->scene();
        if (!sc || !sc->mainCamera || !sc->mainCamera->transform) return;
        Transform* cam = sc->mainCamera->transform;

        Vec3 target = transform->Position(); target.y += lookHeight;
        float pr = cameraPitch * Mathf::Deg2Rad;
        Vec3 behind = Quat::Euler(0, cameraYaw, 0) * Vec3{0, 0, 1};
        Vec3 offset{behind.x * Mathf::Cos(pr), Mathf::Sin(pr), behind.z * Mathf::Cos(pr)};
        Vec3 desired = target + offset * cameraDistance;

        if (cameraDamping > 0.0f && m_haveCamPos) {
            float t = 1.0f - std::exp(-cameraDamping * dt);
            m_camPos = m_camPos + (desired - m_camPos) * t;
        } else {
            m_camPos = desired;
        }
        m_haveCamPos = true;
        cam->SetPosition(m_camPos);
        // Level horizon: build the look straight from the fixed pitch/yaw (a positive
        // pitch tilts the view down toward the player).
        cam->localRotation = Quat::Euler(-cameraPitch, cameraYaw, 0.0f);
    }

private:
    Vec3 m_camPos{0, 0, 0};
    bool m_haveCamPos = false;

    Character* FindCharacter() const {
        if (gameObject) if (auto* ch = gameObject->GetComponent<Character>()) return ch;
        if (transform)
            for (Transform* c : transform->Children())
                if (c && c->gameObject) if (auto* ch = c->gameObject->GetComponent<Character>()) return ch;
        return nullptr;
    }
};

} // namespace okay

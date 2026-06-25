#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Math/Mathf.hpp"
#include <cmath>

namespace okay {

/// Arcade vehicle controller. Throttle drives the car forward along its facing,
/// steering yaws it (scaled by speed, and reversed when backing up), braking and a
/// handbrake slow it, and lateral "grip" bleeds off sideways slide so it corners
/// instead of skating (drop grip / hold handbrake to drift). A short downward
/// raycast keeps it from being considered airborne on gentle slopes.
///
/// Setup: put this on the car body. With a sibling Rigidbody3D it drives the
/// body's velocity (so it collides and gravity applies); without one it moves the
/// Transform directly. Default keys: W/S or Up/Down = throttle/brake-reverse,
/// A/D or Left/Right = steer, Space = handbrake. Turn on `followCamera` to make the
/// scene's main camera chase it.
///
/// This is the arcade model; per-wheel raycast suspension can layer on top later.
class VehicleController : public Behaviour {
public:
    float acceleration = 18.0f;          // how quickly it reaches top speed (units/s^2)
    float maxSpeed     = 22.0f;          // forward top speed (units/s)
    float reverseSpeed = 8.0f;           // top reverse speed
    float brakeForce   = 36.0f;          // deceleration when braking (units/s^2)
    float drag         = 4.0f;           // rolling resistance while coasting (no throttle)
    float turnSpeed    = 110.0f;         // steering rate at speed (degrees/s)
    float grip         = 7.0f;           // how fast sideways slide is killed (higher = sticky)
    float handbrakeGrip = 0.6f;          // grip while the handbrake is held (low = drifty)
    float groundCheckDistance = 1.2f;    // down-ray length for "on the ground"

    bool  followCamera = false;          // chase the scene's main camera behind the car
    float camDistance  = 8.0f;
    float camHeight    = 3.5f;
    float camLerp      = 6.0f;

    // Throttle (W/S or Up/Down) and steering (A/D or Left/Right) come from the
    // shared WASD/arrow axis. The handbrake key is remappable.
    char handbrakeKey = ' ';

    float Speed() const { return m_speed; }              // signed forward speed (units/s)
    bool  IsGrounded() const { return m_grounded; }

    void Update(float dt) override {
        if (!transform || dt <= 0.0f) return;

        // ---- Read input: WASD/arrow axis (y = throttle, x = steer) ----
        Vec2 axis = Input::AxisWASD();
        float throttle = axis.y, steer = axis.x;
        bool handbrake = handbrakeKey && Input::GetKey(handbrakeKey);

        // Planar facing axes (flatten Y so slopes don't change the drive plane).
        Vec3 F = transform->Forward(); Vec3 R = transform->Right();
        Vec3 fp = Planar(F), rp = Planar(R);

        auto* rb = gameObject ? gameObject->GetComponent<Rigidbody3D>() : nullptr;
        Vec3 vel = rb ? rb->velocity : m_kinematicVel;
        float speedF = vel.x * fp.x + vel.z * fp.z;       // velocity along facing
        float lat    = vel.x * rp.x + vel.z * rp.z;       // sideways velocity

        // ---- Steering: scales in as you gain speed; reverses when backing up ----
        float steerScale = Mathf::Clamp(std::fabs(speedF) / 4.0f, 0.0f, 1.0f);
        float dirSign = (speedF >= -0.05f) ? 1.0f : -1.0f;
        if (std::fabs(steer) > 0.001f && steerScale > 0.001f)
            transform->Rotate({0.0f, steer * turnSpeed * steerScale * dirSign * dt, 0.0f});

        // Recompute facing after the turn so momentum follows the new heading.
        F = transform->Forward(); R = transform->Right();
        fp = Planar(F); rp = Planar(R);

        // ---- Longitudinal: ease forward speed toward the throttle target ----
        float target = throttle > 0.0f ? maxSpeed * throttle
                     : throttle < 0.0f ? -reverseSpeed * (-throttle) : 0.0f;
        float rate = (throttle == 0.0f) ? drag
                   : (throttle < 0.0f && speedF > 0.1f) ? brakeForce      // pressing back while rolling fwd = brake
                   : acceleration;
        speedF = MoveToward(speedF, target, rate * dt);

        // ---- Lateral grip: shed sideways slide (less while handbraking = drift) ----
        float g = handbrake ? handbrakeGrip : grip;
        lat *= Mathf::Clamp(1.0f - g * dt, 0.0f, 1.0f);

        // ---- Reassemble velocity (keep vertical for gravity) ----
        Vec3 planar = fp * speedF + rp * lat;
        m_speed = speedF;
        UpdateGrounded();
        if (rb) {
            rb->velocity.x = planar.x;
            rb->velocity.z = planar.z;
            // leave rb->velocity.y to gravity/collision
        } else {
            m_kinematicVel = {planar.x, 0.0f, planar.z};
            transform->Translate(Vec3{planar.x, 0.0f, planar.z} * dt);
        }

        if (followCamera) UpdateCamera(dt);
    }

private:
    static Vec3 Planar(const Vec3& v) {
        Vec3 p{v.x, 0.0f, v.z};
        float m = std::sqrt(p.x * p.x + p.z * p.z);
        return m > 1e-5f ? Vec3{p.x / m, 0.0f, p.z / m} : Vec3{0.0f, 0.0f, 1.0f};
    }
    static float MoveToward(float a, float b, float maxDelta) {
        float d = b - a;
        if (std::fabs(d) <= maxDelta) return b;
        return a + (d > 0.0f ? maxDelta : -maxDelta);
    }
    void UpdateGrounded() {
        m_grounded = true;
        if (gameObject && gameObject->scene() && transform) {
            RaycastHit3D h = gameObject->scene()->physics3D().Raycast(
                *gameObject->scene(), transform->Position(), Vec3{0, -1, 0},
                groundCheckDistance, gameObject);
            m_grounded = h.hit;
        }
    }
    void UpdateCamera(float dt) {
        Scene* s = gameObject ? gameObject->scene() : nullptr;
        if (!s || !s->mainCamera || !s->mainCamera->gameObject) return;
        Transform* ct = s->mainCamera->gameObject->transform;
        if (!ct || ct == transform) return;
        Vec3 fp = Planar(transform->Forward());
        Vec3 want = transform->Position() - fp * camDistance + Vec3{0, camHeight, 0};
        float t = camLerp > 0.0f ? (1.0f - std::exp(-camLerp * dt)) : 1.0f;
        Vec3 cp = ct->Position();
        Vec3 np = cp + (want - cp) * t;
        ct->SetPosition(np);
        // Orient +Z toward the car (the engine's forward axis).
        Vec3 look = transform->Position() - np;
        if (look.x * look.x + look.y * look.y + look.z * look.z > 1e-6f)
            ct->localRotation = Quat::LookRotation(look);
    }

    float m_speed = 0.0f;
    bool  m_grounded = true;
    Vec3  m_kinematicVel{0, 0, 0};
};

} // namespace okay

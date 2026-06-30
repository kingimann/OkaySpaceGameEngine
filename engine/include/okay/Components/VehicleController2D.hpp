#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Physics/Rigidbody2D.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Net/NetOwnership.hpp"
#include "okay/Math/Mathf.hpp"
#include <cmath>

namespace okay {

/// Arcade 2D vehicle controller — the 2D sibling of VehicleController.
///
/// Top-down (default): throttle drives along the sprite's facing (its local +Y),
/// steering rotates it (scaled by speed, reversed in reverse), and lateral grip
/// bleeds off sideways slide so it corners instead of sliding (low grip / handbrake
/// = drift). Set `sideView` for a side-scroller car: A/D drives along world X under
/// gravity, no steering.
///
/// Drives a sibling Rigidbody2D's velocity when present (so it collides), otherwise
/// moves the Transform. Default keys: W/S or Up/Down = throttle, A/D or Left/Right =
/// steer (top-down) or drive (side view), Space = handbrake.
class VehicleController2D : public Behaviour {
public:
    float acceleration = 16.0f;
    float maxSpeed     = 14.0f;
    float reverseSpeed = 6.0f;
    float brakeForce   = 30.0f;
    float drag         = 4.0f;
    float turnSpeed    = 160.0f;         // deg/s steering (top-down only)
    float grip         = 8.0f;
    float handbrakeGrip = 0.7f;
    bool  sideView = false;              // true = platformer car (drive on X, gravity on)
    char  handbrakeKey = ' ';

    float Speed() const { return m_speed; }

    void Update(float dt) override {
        if (!transform || dt <= 0.0f) return;
        if (!IsLocallyControlled(gameObject)) return;   // remote proxy: NetworkSync drives it
        Vec2 axis = Input::AxisWASD();
        bool handbrake = handbrakeKey && Input::GetKey(handbrakeKey);
        auto* rb = gameObject ? gameObject->GetComponent<Rigidbody2D>() : nullptr;
        Vec2 vel = rb ? rb->velocity : m_kinematicVel;

        if (sideView) {
            // Side-scroller: A/D (or throttle x) drive along world X; gravity does Y.
            float drive = axis.x;
            float target = drive * maxSpeed;
            float rate = (drive == 0.0f) ? drag : acceleration;
            if (drive != 0.0f && ((drive < 0.0f) != (vel.x < 0.0f)) && std::fabs(vel.x) > 0.1f)
                rate = brakeForce;                       // turning around = brake first
            if (handbrake) rate = brakeForce;
            float vx = MoveToward(vel.x, handbrake ? 0.0f : target, rate * dt);
            m_speed = vx;
            if (rb) rb->velocity.x = vx;
            else { m_kinematicVel.x = vx; transform->Translate(Vec3{vx * dt, 0.0f, 0.0f}); }
            return;
        }

        // ---- Top-down: facing is the sprite's local +Y (Up) ----
        float throttle = axis.y, steer = axis.x;
        Vec2 F = Norm2(transform->Up());
        Vec2 R = Norm2(transform->Right());
        float speedF = vel.x * F.x + vel.y * F.y;
        float lat    = vel.x * R.x + vel.y * R.y;

        float steerScale = Mathf::Clamp(std::fabs(speedF) / 3.0f, 0.0f, 1.0f);
        float dirSign = (speedF >= -0.05f) ? 1.0f : -1.0f;
        // In screen space +Z is into the screen, so a right (+x) steer should rotate
        // the sprite clockwise: negative Z. (Flip felt natural in testing.)
        if (std::fabs(steer) > 0.001f && steerScale > 0.001f)
            transform->Rotate({0.0f, 0.0f, -steer * turnSpeed * steerScale * dirSign * dt});

        F = Norm2(transform->Up()); R = Norm2(transform->Right());

        float target = throttle > 0.0f ? maxSpeed * throttle
                     : throttle < 0.0f ? -reverseSpeed * (-throttle) : 0.0f;
        float rate = (throttle == 0.0f) ? drag
                   : (throttle < 0.0f && speedF > 0.1f) ? brakeForce : acceleration;
        speedF = MoveToward(speedF, target, rate * dt);

        float g = handbrake ? handbrakeGrip : grip;
        lat *= Mathf::Clamp(1.0f - g * dt, 0.0f, 1.0f);

        Vec2 v{F.x * speedF + R.x * lat, F.y * speedF + R.y * lat};
        m_speed = speedF;
        if (rb) rb->velocity = v;
        else { m_kinematicVel = v; transform->Translate(Vec3{v.x * dt, v.y * dt, 0.0f}); }
    }

private:
    static Vec2 Norm2(const Vec3& v) {
        Vec2 p{v.x, v.y};
        float m = std::sqrt(p.x * p.x + p.y * p.y);
        return m > 1e-5f ? Vec2{p.x / m, p.y / m} : Vec2{0.0f, 1.0f};
    }
    static float MoveToward(float a, float b, float maxDelta) {
        float d = b - a;
        if (std::fabs(d) <= maxDelta) return b;
        return a + (d > 0.0f ? maxDelta : -maxDelta);
    }
    float m_speed = 0.0f;
    Vec2  m_kinematicVel{0, 0};
};

} // namespace okay

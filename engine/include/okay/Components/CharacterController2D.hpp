#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Physics/Rigidbody2D.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Math/Mathf.hpp"

namespace okay {

/// No-code 2D character movement. Reads WASD / arrow keys and moves the object —
/// top-down (free 8-way) or platformer (left/right + jump). Drives a sibling
/// Rigidbody2D's velocity when present (so collisions work), otherwise moves the
/// Transform directly. The kind of starter you'd otherwise write a script for.
class CharacterController2D : public Behaviour {
public:
    enum class Mode { TopDown, Platformer };

    Mode  mode      = Mode::TopDown;
    float speed     = 5.0f;     // horizontal (and vertical, top-down) move speed
    float jumpForce = 9.0f;     // platformer jump velocity

    void Update(float dt) override {
        if (!transform) return;
        Vec2 axis = Input::AxisWASD();
        auto* rb = gameObject ? gameObject->GetComponent<Rigidbody2D>() : nullptr;

        if (mode == Mode::TopDown) {
            Vec2 v{axis.x * speed, axis.y * speed};
            if (rb) rb->velocity = v;
            else transform->Translate({v.x * dt, v.y * dt, 0.0f});
        } else { // Platformer: horizontal control + jump, gravity from the body.
            float vx = axis.x * speed;
            bool jump = Input::GetKeyDown(' ') || Input::GetKeyDown('w');
            if (rb) {
                rb->velocity.x = vx;
                // Jump only when roughly grounded (small vertical speed).
                if (jump && Mathf::Abs(rb->velocity.y) < 0.5f) rb->velocity.y = jumpForce;
            } else {
                transform->Translate({vx * dt, 0.0f, 0.0f});
            }
        }
    }
};

} // namespace okay

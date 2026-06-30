#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Net/NetOwnership.hpp"
#include "okay/Math/Mathf.hpp"

namespace okay {

/// No-code 3D character movement on the XZ plane from WASD / arrow keys, with an
/// optional jump. Drives a sibling Rigidbody3D's velocity when present (so it
/// collides and gravity applies), otherwise moves the Transform directly.
class CharacterController3D : public Behaviour {
public:
    float speed     = 5.0f;     // move speed on the ground plane
    float jumpForce = 6.0f;     // upward velocity on jump
    bool  canJump   = true;     // platformer-style jump on space

    void Update(float dt) override {
        if (!transform) return;
        if (!IsLocallyControlled(gameObject)) return;   // remote proxy: NetworkSync drives it
        Vec2 axis = Input::AxisWASD();               // x = strafe, y = forward
        auto* rb = gameObject ? gameObject->GetComponent<Rigidbody3D>() : nullptr;
        float vx = axis.x * speed, vz = axis.y * speed;

        if (rb) {
            rb->velocity.x = vx;
            rb->velocity.z = vz;
            if (canJump && Input::GetKeyDown(' ') && Mathf::Abs(rb->velocity.y) < 0.5f)
                rb->velocity.y = jumpForce;
        } else {
            transform->Translate({vx * dt, 0.0f, vz * dt});
        }
    }
};

} // namespace okay

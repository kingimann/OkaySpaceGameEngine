#pragma once
#include "okay/Core/Log.hpp"
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Net/NetOwnership.hpp"
#include "okay/Components/StepUp.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/CharacterIK.hpp"

namespace okay {

/// No-code 3D character movement on the XZ plane from WASD / arrow keys, with an
/// optional jump. Drives a sibling Rigidbody3D's velocity when present (so it
/// collides and gravity applies), otherwise moves the Transform directly. Brought
/// up to the controller family: sprint, momentum (accel/decel), reduced air
/// control, and optional Character animation + foot IK.
class CharacterController3D : public Behaviour {
public:
    float speed     = 5.0f;     // walk speed on the ground plane
    float runSpeed  = 8.0f;     // speed while the sprint key is held
    char  sprintKey = 0;        // hold to run (0 = disabled)
    float jumpForce = 6.0f;     // upward velocity on jump
    bool  canJump   = true;     // platformer-style jump on space
    float acceleration = 60.0f; // how fast velocity ramps toward the target (units/s^2)
    float deceleration = 55.0f; // how fast it ramps down when stopping
    float airControl   = 0.5f;  // accel multiplier while airborne (0..1)
    bool  driveAnimation = true;// set a Character's walk/run/idle anim from movement
    bool  footIK = false;       // plant the Character's feet on the ground

    /// Safety net: falling below this world Y teleports the player back to its
    /// spawn point (fell off the map / through a floor with no collider).
    /// Set to 0 to disable.
    float fallResetY = -100.0f;
    /// Max step height the controller climbs automatically (stairs/curbs). 0 = off.
    float stepOffset = 0.35f;
    /// Steepest terrain slope (degrees) standable; steeper ground slides the
    /// body back down. 0 = no limit.
    float slopeLimit = 50.0f;

    void Start() override {
        if (footIK) AttachCharacterFootIK(gameObject);
        if (transform) { m_spawn = transform->Position(); m_haveSpawn = true; }
    }

    void Update(float dt) override {
        if (!transform) return;
        // Fell out of the world? Teleport home instead of falling forever (a
        // floor without a collider, a hole in the map). fallResetY = 0 disables.
        if (m_haveSpawn && fallResetY != 0.0f && transform->Position().y < fallResetY) {
            transform->SetPosition(m_spawn);
            if (auto* frb = gameObject ? gameObject->GetComponent<Rigidbody3D>() : nullptr)
                frb->velocity = Vec3{0, 0, 0};
            OKAY_WARN("Player fell below Fall Reset Y and was returned to spawn. "
                      "If this keeps happening, the floor is probably missing a collider "
                      "(select it and Add Component > Mesh Collider 3D).");
        }
        if (!IsLocallyControlled(gameObject)) return;   // remote proxy: NetworkSync drives it
        Vec2 axis = Input::AxisWASD();               // x = strafe, y = forward
        auto* rb = gameObject ? gameObject->GetComponent<Rigidbody3D>() : nullptr;
        bool running = sprintKey && Input::GetKey(sprintKey);
        float spd = running ? runSpeed : speed;
        // Normalize diagonal input so W+D isn't ~41% faster than W alone (magnitude √2).
        float alen = Mathf::Sqrt(axis.x * axis.x + axis.y * axis.y);
        if (alen > 1.0f) { axis.x /= alen; axis.y /= alen; }
        float tx = axis.x * spd, tz = axis.y * spd;
        bool moving = (axis.x != 0.0f || axis.y != 0.0f);

        if (rb) {
            bool grounded = Mathf::Abs(rb->velocity.y) < 0.5f;
            float rate = (moving ? acceleration : deceleration) * (grounded ? 1.0f : airControl);
            rb->velocity.x = Mathf::MoveTowards(rb->velocity.x, tx, rate * dt);
            rb->velocity.z = Mathf::MoveTowards(rb->velocity.z, tz, rate * dt);
            if (canJump && Input::GetKeyDown(' ') && grounded)
                rb->velocity.y = jumpForce;
            // Slope limit: too-steep terrain cancels uphill velocity and slides.
            if (slopeLimit > 0.0f && (grounded || rb->groundedOnTerrain) && rb->groundNormal.y < std::cos(slopeLimit * Mathf::Deg2Rad)) {
                Vec3 n = rb->groundNormal;
                float hl = std::sqrt(n.x * n.x + n.z * n.z);
                if (hl > 1e-4f && grounded) {
                    Vec3 dh{n.x / hl, 0.0f, n.z / hl};
                    float up = -(rb->velocity.x * dh.x + rb->velocity.z * dh.z);
                    if (up > 0.0f) { rb->velocity.x += dh.x * up; rb->velocity.z += dh.z * up; }
                    rb->velocity.x += dh.x * 18.0f * dt;
                    rb->velocity.z += dh.z * 18.0f * dt;
                }
            }
            // Stairs: step up onto low obstacles instead of grinding against them.
            if (grounded && moving && gameObject && gameObject->scene())
                TryStepUp(*gameObject->scene(), gameObject, rb, {tx, 0.0f, tz}, grounded, moving, stepOffset);
        } else {
            transform->Translate({tx * dt, 0.0f, tz * dt});
        }

        if (driveAnimation)
            if (Character* ch = FindCharacterIn(gameObject))
                ch->anim = !moving ? 1 : (running ? 3 : 2);   // idle / run / walk
    }

private:
    Vec3 m_spawn{0, 0, 0}; bool m_haveSpawn = false;   // fall-reset home position
};

} // namespace okay

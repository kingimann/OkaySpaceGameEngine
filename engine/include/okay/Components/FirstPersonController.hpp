#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Math/Mathf.hpp"
#include <cmath>

namespace okay {

/// First-person player controller. Mouse looks around (yaw turns the body, pitch
/// tilts a child Camera), WASD walks relative to facing, Space jumps. Drives a
/// sibling Rigidbody3D's velocity when present (so it collides and gravity
/// applies), otherwise moves the Transform directly.
///
/// Setup: put a Camera as a CHILD of this object (at eye height). If there's no
/// child camera, the pitch is applied to this object itself (so you can also just
/// attach this to the camera). If a Character component is on this object or a
/// child, its animation is driven from movement (idle / walk / run / jump) — so
/// your body animates for shadows / other players even in first person.
class FirstPersonController : public Behaviour {
public:
    float walkSpeed = 4.5f;
    float runSpeed  = 8.0f;
    float jumpForce = 6.0f;
    // Momentum: ramp horizontal velocity toward the target (units/s^2) for weighty
    // starts/stops, with reduced authority in the air.
    float acceleration = 60.0f;
    float deceleration = 55.0f;
    float airControl   = 0.40f;
    // Forgiving jump timing: coyote time (jump just after a ledge) + jump buffer
    // (press just before landing).
    float coyoteTime    = 0.12f;
    float jumpBufferTime = 0.12f;
    float mouseSensitivity = 0.15f;     // degrees per pixel of mouse movement
    float minPitch = -85.0f, maxPitch = 85.0f;
    bool  invertY = false;              // invert vertical mouse look
    char  sprintKey = 0;                // hold to run (0 = disabled)
    bool  canJump = true;
    bool  driveAnimation = true;        // set a sibling Character's anim from movement
    bool  showBody = false;             // first person: hide your own body (no head clipping)

    float yaw = 0.0f, pitch = 0.0f;     // look angles (degrees)

    void Start() override { ApplyBodyVisibility(); }

    // First person: the player's own camera should IGNORE the body (so you don't
    // see the inside of your own head) — but the body must still exist for the
    // Scene view, shadows and other cameras. So instead of hiding the mesh, we
    // tell the child camera to skip this object. showBody=true clears that.
    void ApplyBodyVisibility() {
        if (gameObject)
            if (auto* mr = gameObject->GetComponent<MeshRenderer>()) mr->enabled = true;  // never hide globally
        if (Transform* camT = FindCameraChild())
            if (auto* cam = camT->gameObject->GetComponent<Camera>())
                cam->ignoreObject = showBody ? nullptr : gameObject;
    }

    void Update(float dt) override {
        if (!transform) return;
        ApplyBodyVisibility();

        // ---- Mouse look ----
        // Mouse-right turns right and mouse-up looks up. The camera looks down its
        // local -Z, so yaw decreases with rightward mouse movement.
        Vec2 mp = Input::MousePosition();
        if (m_haveMouse) {
            yaw   -= (mp.x - m_lastMouse.x) * mouseSensitivity;
            pitch += (invertY ? 1.0f : -1.0f) * (mp.y - m_lastMouse.y) * mouseSensitivity;
            pitch  = Mathf::Clamp(pitch, minPitch, maxPitch);
        }
        m_lastMouse = mp; m_haveMouse = true;

        if (Transform* cam = FindCameraChild()) {
            transform->localRotation = Quat::Euler(0, yaw, 0);     // body turns
            cam->localRotation       = Quat::Euler(pitch, 0, 0);   // camera tilts
        } else {
            transform->localRotation = Quat::Euler(pitch, yaw, 0); // no child cam: tilt self
        }

        // ---- Movement: forward is where the camera looks (its -Z), flattened. ----
        Vec2 axis = Input::AxisWASD();                 // x strafe, y forward
        Quat flat = Quat::Euler(0, yaw, 0);
        Vec3 fwd = flat * Vec3{0, 0, -1}, right = flat * Vec3::Right;
        Vec3 dir = fwd * axis.y + right * axis.x;
        float len = std::sqrt(dir.x * dir.x + dir.z * dir.z);
        bool moving = len > 0.01f;
        if (moving) { dir.x /= len; dir.z /= len; }
        bool running = sprintKey && Input::GetKey(sprintKey) && moving;
        float speed = running ? runSpeed : walkSpeed;

        // Grounded (from contacts) + coyote time + jump buffer.
        m_groundContact = Mathf::Max(0.0f, m_groundContact - dt);
        bool grounded = m_groundContact > 0.0f;
        m_coyote = grounded ? coyoteTime : Mathf::Max(0.0f, m_coyote - dt);
        if (Input::GetKeyDown(' ')) m_jumpBuf = jumpBufferTime;
        else                        m_jumpBuf = Mathf::Max(0.0f, m_jumpBuf - dt);

        auto* rb = gameObject ? gameObject->GetComponent<Rigidbody3D>() : nullptr;
        // Only a physics body can be airborne; a transform-only player can't fall.
        bool airborne = rb ? !grounded : false;
        if (rb) {
            Vec3 cur{rb->velocity.x, 0.0f, rb->velocity.z};
            Vec3 dv{dir.x * speed - cur.x, 0.0f, dir.z * speed - cur.z};
            float rate = (moving ? acceleration : deceleration) * (grounded ? 1.0f : airControl);
            float dl = std::sqrt(dv.x * dv.x + dv.z * dv.z), step = rate * dt;
            if (dl > 1e-5f && dl > step) { dv.x = dv.x / dl * step; dv.z = dv.z / dl * step; }
            rb->velocity.x = cur.x + dv.x;
            rb->velocity.z = cur.z + dv.z;
            if (canJump && m_jumpBuf > 0.0f && m_coyote > 0.0f) {
                rb->velocity.y = jumpForce;
                m_jumpBuf = 0.0f; m_coyote = 0.0f; m_groundContact = 0.0f; airborne = true;
            }
        } else if (moving) {
            transform->Translate(dir * (speed * dt));
        }

        // ---- Animation ----
        if (driveAnimation)
            if (Character* ch = FindCharacter())
                ch->anim = airborne ? 5 : (moving ? (running ? 3 : 2) : 1);
    }

    // Grounded detection: a roughly-vertical contact with something below the player.
    void OnCollisionEnter3D(const Collision3D& c) override { NoteGround(c); }
    void OnCollisionStay3D(const Collision3D& c)  override { NoteGround(c); }

private:
    void NoteGround(const Collision3D& c) {
        bool vertical = Mathf::Abs(c.normal.y) > 0.5f;
        bool below = c.gameObject && c.gameObject->transform && transform &&
                     c.gameObject->transform->Position().y < transform->Position().y;
        if (vertical && below) m_groundContact = 0.10f;
    }

    Vec2 m_lastMouse{0, 0};
    bool m_haveMouse = false;
    float m_groundContact = 0.0f;
    float m_coyote = 0.0f;
    float m_jumpBuf = 0.0f;

    Transform* FindCameraChild() const {
        if (!transform) return nullptr;
        for (Transform* c : transform->Children())
            if (c && c->gameObject && c->gameObject->GetComponent<Camera>()) return c;
        return nullptr;
    }
    Character* FindCharacter() const {
        if (gameObject) if (auto* ch = gameObject->GetComponent<Character>()) return ch;
        if (transform)
            for (Transform* c : transform->Children())
                if (c && c->gameObject) if (auto* ch = c->gameObject->GetComponent<Character>()) return ch;
        return nullptr;
    }
};

} // namespace okay

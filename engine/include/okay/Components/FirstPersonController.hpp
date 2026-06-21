#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
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

        auto* rb = gameObject ? gameObject->GetComponent<Rigidbody3D>() : nullptr;
        bool airborne = false;
        if (rb) {
            rb->velocity.x = dir.x * speed;
            rb->velocity.z = dir.z * speed;
            if (canJump && Input::GetKeyDown(' ') && Mathf::Abs(rb->velocity.y) < 0.5f)
                rb->velocity.y = jumpForce;
            airborne = Mathf::Abs(rb->velocity.y) > 0.6f;
        } else if (moving) {
            transform->Translate(dir * (speed * dt));
        }

        // ---- Animation ----
        if (driveAnimation)
            if (Character* ch = FindCharacter())
                ch->anim = airborne ? 5 : (moving ? (running ? 3 : 2) : 1);
    }

private:
    Vec2 m_lastMouse{0, 0};
    bool m_haveMouse = false;

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

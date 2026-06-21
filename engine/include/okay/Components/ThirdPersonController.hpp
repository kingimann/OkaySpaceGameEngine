#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Math/Mathf.hpp"
#include <cmath>

namespace okay {

/// Third-person player controller. The scene's main Camera orbits behind the
/// player (mouse to rotate, wheel to zoom); WASD moves relative to the camera and
/// the body smoothly turns to face the way it's moving; Space jumps. Drives a
/// sibling Rigidbody3D's velocity when present (collisions + gravity), else moves
/// the Transform. If a Character component is on this object (or a child), its
/// animation is driven from movement (idle / walk / run / jump).
///
/// Setup: attach to the player. Have a Camera somewhere in the scene (the main
/// one). The controller positions that camera each frame — it does not need to be
/// a child of the player.
class ThirdPersonController : public Behaviour {
public:
    float walkSpeed = 4.5f;
    float runSpeed  = 8.0f;
    float jumpForce = 6.0f;
    float mouseSensitivity = 0.2f;      // degrees per pixel
    float turnSpeed = 12.0f;            // how fast the body turns toward movement
    float distance  = 5.0f;             // camera distance from the player
    float minDistance = 2.0f, maxDistance = 12.0f;
    float cameraHeight = 1.5f;          // look target height above the player origin
    float minPitch = -20.0f, maxPitch = 70.0f;
    char  sprintKey = 0;                // hold to run (0 = disabled)
    bool  canJump = true;
    bool  driveAnimation = true;

    float yaw = 0.0f, pitch = 18.0f;    // camera orbit angles (degrees)

    void Update(float dt) override {
        if (!transform) return;

        // ---- Camera orbit input ----
        Vec2 mp = Input::MousePosition();
        if (m_haveMouse) {
            yaw   += (mp.x - m_lastMouse.x) * mouseSensitivity;
            pitch -= (mp.y - m_lastMouse.y) * mouseSensitivity;
            pitch  = Mathf::Clamp(pitch, minPitch, maxPitch);
        }
        m_lastMouse = mp; m_haveMouse = true;
        float wheel = Input::MouseWheel();
        if (wheel != 0.0f) distance = Mathf::Clamp(distance - wheel, minDistance, maxDistance);

        // ---- Movement relative to the camera's yaw ----
        Vec2 axis = Input::AxisWASD();
        Quat flat = Quat::Euler(0, yaw, 0);
        Vec3 fwd = flat * Vec3{0, 0, -1}, right = flat * Vec3::Right;   // forward = into the view (-Z)
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

        // ---- Turn the body to face movement (smoothly) ----
        if (moving) {
            Quat want = Quat::LookRotation({dir.x, 0, dir.z});
            float t = 1.0f - std::exp(-turnSpeed * dt);
            transform->localRotation = Quat::Slerp(transform->localRotation, want, t);
        }

        // ---- Animation ----
        if (driveAnimation)
            if (Character* ch = FindCharacter())
                ch->anim = airborne ? 5 : (moving ? (running ? 3 : 2) : 1);
    }

    void LateUpdate(float /*dt*/) override {
        if (!transform || !gameObject) return;
        Scene* sc = gameObject->scene();
        if (!sc || !sc->mainCamera) return;
        Transform* cam = sc->mainCamera->transform;
        if (!cam) return;
        Vec3 target = transform->Position();
        target.y += cameraHeight;
        Quat camRot = Quat::Euler(pitch, yaw, 0);
        // The camera looks down its local -Z, so place it on the +Z side of the
        // target at `distance` and it looks back at the target.
        Vec3 camFwd = camRot * Vec3::Forward;
        cam->localRotation = camRot;
        cam->SetPosition(target + camFwd * distance);
    }

private:
    Vec2 m_lastMouse{0, 0};
    bool m_haveMouse = false;

    Character* FindCharacter() const {
        if (gameObject) if (auto* ch = gameObject->GetComponent<Character>()) return ch;
        if (transform)
            for (Transform* c : transform->Children())
                if (c && c->gameObject) if (auto* ch = c->gameObject->GetComponent<Character>()) return ch;
        return nullptr;
    }
};

} // namespace okay

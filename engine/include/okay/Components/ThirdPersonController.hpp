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
/// player (mouse to rotate, wheel to zoom); WASD / left-stick moves relative to
/// the camera and the body smoothly turns to face the way it's moving; Space
/// jumps. Drives a sibling Rigidbody3D's velocity when present (collisions +
/// gravity), else moves the Transform. If a Character component is on this object
/// (or a child), its animation is driven from movement (idle / walk / run / jump).
///
/// Setup: attach to the player. Have a Camera somewhere in the scene (the main
/// one). The controller positions that camera each frame — it does not need to be
/// a child of the player.
class ThirdPersonController : public Behaviour {
public:
    // ---- Movement ----
    float walkSpeed = 4.5f;
    float runSpeed  = 8.0f;
    float jumpForce = 6.0f;
    char  sprintKey = 0;                // hold to run (0 = disabled)
    bool  canJump = true;
    bool  driveAnimation = true;
    float turnSpeed = 12.0f;            // how fast the body turns toward movement

    // ---- Camera orbit ----
    float mouseSensitivity = 0.2f;      // degrees per pixel
    bool  invertY = false;              // invert vertical mouse look
    bool  invertX = false;             // invert horizontal mouse look
    float distance  = 5.0f;             // camera distance from the player
    float minDistance = 2.0f, maxDistance = 12.0f;
    float zoomSpeed = 1.0f;             // wheel zoom step
    float cameraHeight = 1.5f;          // look target height above the player origin
    float minPitch = -20.0f, maxPitch = 70.0f;
    float shoulderOffset = 0.0f;        // lateral camera offset (over-the-shoulder)
    float cameraDamping = 0.0f;         // 0 = instant follow; higher = snappier smoothing

    /// How the body is oriented while playing.
    ///   Movement: turn to face the direction of travel (classic adventure feel).
    ///   Camera:   always face the camera's forward (strafe / shooter / aim feel).
    enum class FaceMode { Movement, Camera };
    FaceMode faceMode = FaceMode::Movement;

    float yaw = 0.0f, pitch = 18.0f;    // camera orbit angles (degrees)

    void Update(float dt) override {
        if (!transform) return;

        // ---- Camera orbit input ----
        // Mouse-right turns the view right and mouse-up looks up — matching the
        // first-person controller's convention (the old code inverted both).
        Vec2 mp = Input::MousePosition();
        if (m_haveMouse) {
            yaw   -= (invertX ? -1.0f : 1.0f) * (mp.x - m_lastMouse.x) * mouseSensitivity;
            pitch += (invertY ? 1.0f : -1.0f) * (mp.y - m_lastMouse.y) * mouseSensitivity;
            pitch  = Mathf::Clamp(pitch, minPitch, maxPitch);
        }
        m_lastMouse = mp; m_haveMouse = true;
        float wheel = Input::MouseWheel();
        if (wheel != 0.0f) distance = Mathf::Clamp(distance - wheel * zoomSpeed, minDistance, maxDistance);

        // ---- Movement relative to the camera's yaw (keyboard or gamepad) ----
        Vec2 axis = Input::AxisWASD();
        Vec2 pad  = Input::GamepadAxis();
        if (Mathf::Abs(pad.x) + Mathf::Abs(pad.y) > 0.15f) axis = pad;   // left stick overrides
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

        // ---- Turn the body (smoothly) ----
        // The character mesh faces -Z (engine camera/controller convention).
        Quat want;
        bool turn = false;
        if (faceMode == FaceMode::Camera) {
            want = Quat::Euler(0, yaw, 0);   // mesh -Z aligns with the camera's forward
            turn = true;
        } else if (moving) {
            // Aim its -Z down the movement direction (LookRotation aligns +Z).
            want = Quat::LookRotation({-dir.x, 0, -dir.z});
            turn = true;
        }
        if (turn) {
            float t = 1.0f - std::exp(-turnSpeed * dt);
            transform->localRotation = Quat::Slerp(transform->localRotation, want, t);
        }

        // ---- Animation ----
        if (driveAnimation)
            if (Character* ch = FindCharacter())
                ch->anim = airborne ? 5 : (moving ? (running ? 3 : 2) : 1);
    }

    void LateUpdate(float dt) override {
        if (!transform || !gameObject) return;
        Scene* sc = gameObject->scene();
        if (!sc || !sc->mainCamera) return;
        Transform* cam = sc->mainCamera->transform;
        if (!cam) return;

        Vec3 target = transform->Position();
        target.y += cameraHeight;

        // Place the camera BEHIND the player (+Z of its facing at yaw 0), lifted by
        // pitch so a positive pitch looks DOWN at the player from above. (The old
        // code put the camera below the target — the "inverted" view.)
        float pr = pitch * Mathf::Deg2Rad;
        Vec3 behind = Quat::Euler(0, yaw, 0) * Vec3{0, 0, 1};   // horizontal, behind the facing
        float cp = Mathf::Cos(pr);
        Vec3 offset{behind.x * cp, Mathf::Sin(pr), behind.z * cp};
        Vec3 desired = target + offset * distance;

        // Over-the-shoulder lateral shift: move the camera and its look point
        // sideways by the same amount so the view stays parallel.
        Vec3 lookAt = target;
        if (shoulderOffset != 0.0f) {
            Vec3 viewDir = (desired - target).Normalized();
            Vec3 side = Vec3::Cross(Vec3::Up, viewDir).Normalized();   // camera right
            desired = desired + side * shoulderOffset;
            lookAt  = target  + side * shoulderOffset;
        }

        // Smooth the follow when damping is on (frame-rate independent).
        if (cameraDamping > 0.0f && m_haveCamPos) {
            float t = 1.0f - std::exp(-cameraDamping * dt);
            m_camPos = m_camPos + (desired - m_camPos) * t;
        } else {
            m_camPos = desired;
        }
        m_haveCamPos = true;

        // The camera looks down its local -Z; orient that toward the look point.
        Vec3 toTarget = lookAt - m_camPos;
        if (toTarget.x * toTarget.x + toTarget.y * toTarget.y + toTarget.z * toTarget.z > 1e-6f)
            cam->localRotation = Quat::LookRotation((m_camPos - lookAt).Normalized());
        cam->SetPosition(m_camPos);
    }

private:
    Vec2 m_lastMouse{0, 0};
    bool m_haveMouse = false;
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

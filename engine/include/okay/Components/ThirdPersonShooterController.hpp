#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Input/Cursor.hpp"
#include "okay/Math/Mathf.hpp"
#include <cmath>

namespace okay {

/// Third-person SHOOTER controller (over-the-shoulder aim). The body always faces the
/// camera direction (so you walk/strafe and shoot where you look), mouse looks around,
/// right mouse aims (camera pulls in over the shoulder), left mouse fires (calls the
/// sibling ScriptComponent's on_fire()). Locks/hides the cursor while playing, like
/// Unity. Movement has momentum and a forgiving jump.
///
/// Setup: attach to the player; have a main Camera in the scene (positioned by this
/// each frame). A Character on the player (or a child) animates from movement.
class ThirdPersonShooterController : public Behaviour {
public:
    // ---- Movement ----
    float walkSpeed = 4.5f;
    float runSpeed  = 7.5f;
    float jumpForce = 6.0f;
    char  sprintKey = 0;
    bool  canJump = true;
    bool  driveAnimation = true;
    float acceleration = 60.0f, deceleration = 55.0f, airControl = 0.4f;
    float coyoteTime = 0.12f, jumpBufferTime = 0.12f;
    float turnSpeed = 18.0f;             // body snaps to the aim direction

    // ---- Look / camera ----
    float mouseSensitivity = 0.18f;
    bool  invertY = false;
    float minPitch = -35.0f, maxPitch = 70.0f;
    float distance = 4.0f;               // hip-fire camera distance
    float cameraHeight = 1.6f;           // shoulder/look height above the player origin
    float shoulderOffset = 0.7f;         // over-the-shoulder lateral offset
    bool  cameraCollision = true;        // pull in so the view never clips walls
    float cameraCollisionSkin = 0.3f;

    // ---- Aim (right mouse) ----
    int   aimButton = 1;                 // SDL right button
    float aimDistance = 2.2f;            // closer when aiming (zoom over the shoulder)
    float aimShoulder = 0.5f;
    float aimSpeed = 12.0f;              // how fast it eases between hip and aim
    // ---- Fire (left mouse) ----
    int   fireButton = 0;
    bool  autoFire = false;              // hold to keep firing vs one shot per click
    float fireRate = 8.0f;               // shots/sec when autoFire

    // ---- Cursor ----
    bool  lockCursor = true;             // hide + lock the cursor while playing (Unity-style)

    float yaw = 0.0f, pitch = 12.0f;

    void Start() override {
        if (lockCursor) Cursor::Capture(true);
    }

    void Update(float dt) override {
        if (!transform) return;
        if (lockCursor && Cursor::lockState == Cursor::LockMode::None) Cursor::Capture(true);

        // ---- Mouse look ----
        // Mouse-right orbits right, mouse-up looks up — same convention as the
        // first/third-person controllers (yaw decreases as the mouse moves right).
        Vec2 mp = Input::MousePosition();
        if (m_haveMouse) {
            yaw   -= (mp.x - m_lastMouse.x) * mouseSensitivity;
            pitch += (invertY ? -1.0f : 1.0f) * (mp.y - m_lastMouse.y) * mouseSensitivity;
            pitch  = Mathf::Clamp(pitch, minPitch, maxPitch);
        }
        m_lastMouse = mp; m_haveMouse = true;

        // ---- Aim blend (right mouse) ----
        m_aiming = Input::GetMouseButton(aimButton);
        float aimT = 1.0f - std::exp(-aimSpeed * dt);
        m_aim += ((m_aiming ? 1.0f : 0.0f) - m_aim) * aimT;

        // ---- Fire (left mouse) ----
        m_fireCooldown = Mathf::Max(0.0f, m_fireCooldown - dt);
        bool wantFire = autoFire ? Input::GetMouseButton(fireButton) : Input::GetMouseButtonDown(fireButton);
        if (wantFire && m_fireCooldown <= 0.0f) {
            m_fireCooldown = autoFire ? (1.0f / Mathf::Max(0.1f, fireRate)) : 0.0f;
            Fire();
        }

        // ---- Movement relative to the camera yaw (strafe shooter) ----
        Vec2 axis = Input::AxisWASD();
        Vec2 pad  = Input::GamepadAxis();
        if (Mathf::Abs(pad.x) + Mathf::Abs(pad.y) > 0.15f) axis = pad;
        Quat flat = Quat::Euler(0, yaw, 0);
        Vec3 fwd = flat * Vec3{0, 0, -1}, right = flat * Vec3::Right;
        Vec3 dir = fwd * axis.y + right * axis.x;
        float len = std::sqrt(dir.x * dir.x + dir.z * dir.z);
        bool moving = len > 0.01f;
        if (moving) { dir.x /= len; dir.z /= len; }
        bool running = sprintKey && Input::GetKey(sprintKey) && moving && !m_aiming;
        float speed = (running ? runSpeed : walkSpeed) * (m_aiming ? 0.6f : 1.0f);

        auto* rb = gameObject ? gameObject->GetComponent<Rigidbody3D>() : nullptr;
        m_groundContact = Mathf::Max(0.0f, m_groundContact - dt);
        bool grounded = (rb && Mathf::Abs(rb->velocity.y) < 0.5f) || m_groundContact > 0.0f;
        m_coyote = grounded ? coyoteTime : Mathf::Max(0.0f, m_coyote - dt);
        if (Input::GetKeyDown(' ')) m_jumpBuf = jumpBufferTime;
        else                        m_jumpBuf = Mathf::Max(0.0f, m_jumpBuf - dt);
        bool airborne = rb ? !grounded : false;

        if (rb) {
            Vec3 cur{rb->velocity.x, 0.0f, rb->velocity.z};
            Vec3 dv{dir.x * speed - cur.x, 0.0f, dir.z * speed - cur.z};
            float rate = (moving ? acceleration : deceleration) * (grounded ? 1.0f : airControl);
            float dl = std::sqrt(dv.x * dv.x + dv.z * dv.z), step = rate * dt;
            if (dl > 1e-5f && dl > step) { dv.x = dv.x / dl * step; dv.z = dv.z / dl * step; }
            rb->velocity.x = cur.x + dv.x; rb->velocity.z = cur.z + dv.z;
            if (canJump && m_jumpBuf > 0.0f && m_coyote > 0.0f) {
                rb->velocity.y = jumpForce; m_jumpBuf = 0.0f; m_coyote = 0.0f; m_groundContact = 0.0f; airborne = true;
            }
        } else if (moving) {
            transform->Translate(dir * (speed * dt));
        }

        // Body always faces the camera/aim direction (shooter).
        Quat want = Quat::Euler(0, yaw, 0);
        float t = 1.0f - std::exp(-turnSpeed * dt);
        transform->localRotation = Quat::Slerp(transform->localRotation, want, t);

        if (driveAnimation)
            if (Character* ch = FindCharacter())
                ch->anim = airborne ? 5 : (moving ? (running ? 3 : 2) : 1);
    }

    void LateUpdate(float dt) override {
        if (!transform || !gameObject) return;
        Scene* sc = gameObject->scene();
        if (!sc || !sc->mainCamera || !sc->mainCamera->transform) return;
        Transform* cam = sc->mainCamera->transform;

        float dist  = Mathf::Lerp(distance, aimDistance, m_aim);
        float shldr = Mathf::Lerp(shoulderOffset, aimShoulder, m_aim);
        Vec3 target = transform->Position(); target.y += cameraHeight;

        float pr = pitch * Mathf::Deg2Rad;
        Vec3 behind = Quat::Euler(0, yaw, 0) * Vec3{0, 0, 1};
        float cp = Mathf::Cos(pr);
        Vec3 desired = target + Vec3{behind.x * cp, Mathf::Sin(pr), behind.z * cp} * dist;
        desired = desired + (Quat::Euler(0, yaw, 0) * Vec3::Right) * shldr;

        // Camera collision: never clip through walls/floors.
        if (cameraCollision) {
            Vec3 d = desired - target; float dl = d.Magnitude();
            if (dl > 1e-4f) {
                Vec3 dn = d * (1.0f / dl);
                RaycastHit3D hit = sc->physics3D().Raycast(*sc, target, dn, dl + cameraCollisionSkin, gameObject);
                if (hit.hit) desired = target + dn * Mathf::Max(0.0f, hit.distance - cameraCollisionSkin);
            }
        }

        cam->SetPosition(desired);
        cam->localRotation = Quat::Euler(-pitch, yaw, 0.0f);
    }

    bool IsAiming() const { return m_aiming; }

private:
    void Fire() {
        if (gameObject)
            if (auto* sc = gameObject->GetComponent<ScriptComponent>())
                if (sc->VM()) sc->VM()->CallEvent("on_fire");
    }
    Character* FindCharacter() const {
        if (gameObject) if (auto* ch = gameObject->GetComponent<Character>()) return ch;
        if (transform)
            for (Transform* c : transform->Children())
                if (c && c->gameObject) if (auto* ch = c->gameObject->GetComponent<Character>()) return ch;
        return nullptr;
    }

    Vec2 m_lastMouse{0, 0};
    bool m_haveMouse = false;
    bool m_aiming = false;
    float m_aim = 0.0f;             // 0 = hip, 1 = aiming
    float m_fireCooldown = 0.0f;
    float m_groundContact = 0.0f, m_coyote = 0.0f, m_jumpBuf = 0.0f;
};

} // namespace okay

#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Physics/PlayerCollision.hpp"
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
    // Momentum: instead of snapping velocity, ramp toward the target so starts/stops
    // feel weighty (units/s^2). Higher = snappier; very high ~= the old instant feel.
    float acceleration = 60.0f;
    float deceleration = 55.0f;
    float airControl   = 0.40f;         // accel multiplier while airborne (0..1)
    // Forgiving jump timing (platformer-grade): coyote time lets you jump just after
    // walking off a ledge; jump buffer remembers a press made just before landing.
    float coyoteTime     = 0.12f;
    float jumpBufferTime  = 0.12f;

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
    float cameraDamping = 15.0f;        // 0 = instant follow; higher = snappier smoothing
    // Camera collision (spring arm): pull the camera in when a wall/floor is between
    // it and the player so it never clips through geometry. `skin` keeps the near
    // plane off the surface.
    bool  cameraCollision = true;
    float cameraCollisionSkin = 0.3f;

    /// How the body is oriented while playing.
    ///   Movement: turn to face the direction of travel (classic adventure feel).
    ///   Camera:   always face the camera's forward (strafe / shooter / aim feel).
    enum class FaceMode { Movement, Camera };
    FaceMode faceMode = FaceMode::Movement;

    float yaw = 0.0f, pitch = 18.0f;    // camera orbit angles (degrees)

    void Update(float dt) override {
        if (!transform) return;

        // ---- Camera orbit input ----
        // Default orbit feel (both axes); flip per-axis with invertX / invertY.
        Vec2 mp = Input::MousePosition();
        if (m_haveMouse) {
            yaw   += (invertX ? -1.0f : 1.0f) * (mp.x - m_lastMouse.x) * mouseSensitivity;
            pitch += (invertY ? -1.0f : 1.0f) * (mp.y - m_lastMouse.y) * mouseSensitivity;
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

        // Grounded: resting on something (low vertical speed) — the reliable signal
        // that works regardless of how the ground is set up — OR a fresh ground
        // contact. Coyote time + a jump buffer make jumping forgiving.
        m_groundContact = Mathf::Max(0.0f, m_groundContact - dt);
        bool grounded = (rb && Mathf::Abs(rb->velocity.y) < 0.5f) || m_groundContact > 0.0f;
        m_coyote = grounded ? coyoteTime : Mathf::Max(0.0f, m_coyote - dt);
        if (Input::GetKeyDown(' ')) m_jumpBuf = jumpBufferTime;
        else                        m_jumpBuf = Mathf::Max(0.0f, m_jumpBuf - dt);

        // Only a physics body can leave the ground; a transform-only player (no
        // gravity) is always grounded for animation purposes.
        bool airborne = rb ? !grounded : false;
        if (rb) {
            // Momentum: ease the horizontal velocity toward the target so starts and
            // stops have weight; reduced authority in the air.
            Vec3 tgt{dir.x * speed, 0.0f, dir.z * speed};
            Vec3 cur{rb->velocity.x, 0.0f, rb->velocity.z};
            float rate = (moving ? acceleration : deceleration) * (grounded ? 1.0f : airControl);
            Vec3 dv{tgt.x - cur.x, 0.0f, tgt.z - cur.z};
            float dl = std::sqrt(dv.x * dv.x + dv.z * dv.z);
            float step = rate * dt;
            if (dl > 1e-5f && dl > step) { dv.x = dv.x / dl * step; dv.z = dv.z / dl * step; }
            rb->velocity.x = cur.x + dv.x;
            rb->velocity.z = cur.z + dv.z;
            // Jump: allowed within coyote time, satisfied by a buffered press.
            if (canJump && m_jumpBuf > 0.0f && m_coyote > 0.0f) {
                rb->velocity.y = jumpForce;
                m_jumpBuf = 0.0f; m_coyote = 0.0f; m_groundContact = 0.0f;
                grounded = false; airborne = true;
            }
        } else if (moving) {
            transform->Translate(dir * (speed * dt));
            if (gameObject && gameObject->scene())
                ResolvePlayerBody(*gameObject->scene(), gameObject);   // no clipping
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

        // Over-the-shoulder: shift the camera sideways along its horizontal right.
        // The rotation stays level (built from yaw/pitch below), so the view is a
        // parallel offset — no roll.
        if (shoulderOffset != 0.0f)
            desired = desired + (Quat::Euler(0, yaw, 0) * Vec3::Right) * shoulderOffset;

        // Smooth the follow when damping is on (frame-rate independent).
        if (cameraDamping > 0.0f && m_haveCamPos) {
            float t = 1.0f - std::exp(-cameraDamping * dt);
            m_camPos = m_camPos + (desired - m_camPos) * t;
        } else {
            m_camPos = desired;
        }
        m_haveCamPos = true;

        // Camera collision (spring arm): cast from the player's head to the smoothed
        // camera position and, if anything is in the way, pull the camera in to the
        // hit point (minus a skin). Clamping AFTER the smoothing guarantees the view
        // never clips through walls/floors even while the follow eases.
        if (cameraCollision) {
            Vec3 from = target;            // head pivot (origin + cameraHeight)
            Vec3 d = m_camPos - from;
            float dist = d.Magnitude();
            if (dist > 1e-4f) {
                Vec3 dn = d * (1.0f / dist);
                RaycastHit3D hit = sc->physics3D().Raycast(*sc, from, dn,
                                                           dist + cameraCollisionSkin, gameObject);
                if (hit.hit) {
                    float pull = Mathf::Max(0.0f, hit.distance - cameraCollisionSkin);
                    m_camPos = from + dn * pull;
                }
            }
        }

        // Never let the camera drop below the player's base. minPitch allows looking
        // up from slightly below, which at close distance could otherwise sink the
        // camera through the floor — from under a single-sided ground plane the floor
        // vanishes, and grazing it reads as a glitchy edge. (target.y - cameraHeight
        // == the player origin, so this stays correct on raised platforms too.)
        float minY = target.y - cameraHeight;
        if (m_camPos.y < minY) m_camPos.y = minY;

        cam->SetPosition(m_camPos);
        // Build the rotation straight from yaw/pitch with NO roll component (z = 0)
        // so the horizon is always level. (LookRotation could tilt the horizon at
        // steep angles — that was the slanted-view bug.) Euler(-pitch, yaw, 0)
        // points the camera's -Z at the target: yaw matches the orbit, and a
        // positive orbit pitch tilts the view down.
        cam->localRotation = Quat::Euler(-pitch, yaw, 0.0f);
    }

    // Grounded detection: a contact counts as ground when it's a roughly-vertical
    // hit with something below the player. Both enter and stay refresh it (the timer
    // in Update lets it survive the frame-order of the physics step).
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
    Vec3 m_camPos{0, 0, 0};
    bool m_haveCamPos = false;
    float m_groundContact = 0.0f;   // time-to-live of the last ground contact
    float m_coyote = 0.0f;          // remaining coyote-time window
    float m_jumpBuf = 0.0f;         // remaining jump-buffer window

    Character* FindCharacter() const {
        if (gameObject) if (auto* ch = gameObject->GetComponent<Character>()) return ch;
        if (transform)
            for (Transform* c : transform->Children())
                if (c && c->gameObject) if (auto* ch = c->gameObject->GetComponent<Character>()) return ch;
        return nullptr;
    }
};

} // namespace okay

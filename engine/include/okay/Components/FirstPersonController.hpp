#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Physics/PlayerCollision.hpp"
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
    char  sprintKey = Input::KeyShift;  // hold (or tap, if toggleRun) to run; 0 = disabled
    bool  toggleRun = false;            // tap sprint to keep running instead of holding
    bool  canJump = true;
    bool  driveAnimation = true;        // set a sibling Character's anim from movement
    bool  showBody = false;             // first person: hide your own body (no head clipping)

    // ---- Stance: crouch & prone ----
    // Lower your profile to fit under things / take cover. Each has its own speed
    // and eye height; the camera eases between heights. Toggle = tap to switch,
    // otherwise hold the key. Running is disabled while crouched or prone.
    char  crouchKey = Input::KeyCtrl;   // 0 = disabled
    char  proneKey  = 'z';              // 0 = disabled
    bool  toggleStance = true;          // tap to toggle vs hold to hold the stance
    float crouchSpeed = 2.2f;
    float proneSpeed  = 1.1f;
    float standEyeHeight  = 1.6f;       // child-camera local Y while standing
    float crouchEyeHeight = 0.9f;
    float proneEyeHeight  = 0.4f;
    float stanceLerp = 12.0f;           // how fast the eye height eases between stances

    enum class Stance { Stand, Crouch, Prone };
    Stance stance() const { return m_stance; }

    // ---- Lean (peek around corners) ----
    char  leanLeftKey  = 'q';           // 0 = disabled
    char  leanRightKey = 'e';           // 0 = disabled
    float leanAngle  = 14.0f;           // camera roll while leaning (degrees)
    float leanOffset = 0.35f;           // sideways camera shift while leaning (units)
    float leanSpeed  = 10.0f;           // how fast the lean eases in/out
    float lean() const { return m_lean; }   // -1 left .. 0 .. +1 right (eased)

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

        // Lean: hold Q/E to peek; the camera rolls and shifts sideways.
        UpdateLean(dt);
        float roll = -m_lean * leanAngle;
        if (Transform* cam = FindCameraChild()) {
            transform->localRotation = Quat::Euler(0, yaw, 0);        // body turns
            cam->localRotation       = Quat::Euler(pitch, 0, roll);  // camera tilts + leans
            cam->localPosition.x     = m_lean * leanOffset;          // peek sideways
        } else {
            transform->localRotation = Quat::Euler(pitch, yaw, roll); // no child cam: tilt self
        }

        // ---- Stance (crouch / prone) ----
        UpdateStance();

        // ---- Movement: forward is where the camera looks (its -Z), flattened. ----
        Vec2 axis = Input::AxisWASD();                 // x strafe, y forward
        Quat flat = Quat::Euler(0, yaw, 0);
        Vec3 fwd = flat * Vec3{0, 0, -1}, right = flat * Vec3::Right;
        Vec3 dir = fwd * axis.y + right * axis.x;
        float len = std::sqrt(dir.x * dir.x + dir.z * dir.z);
        bool moving = len > 0.01f;
        if (moving) { dir.x /= len; dir.z /= len; }
        // Running only stands up: you can't sprint while crouched or prone.
        bool running = m_stance == Stance::Stand && moving && IsRunHeld();
        float speed = m_stance == Stance::Prone  ? proneSpeed
                    : m_stance == Stance::Crouch ? crouchSpeed
                    : (running ? runSpeed : walkSpeed);

        // Ease the child camera to the stance's eye height.
        ApplyEyeHeight(dt);

        auto* rb = gameObject ? gameObject->GetComponent<Rigidbody3D>() : nullptr;

        // Grounded: resting (low vertical speed) — reliable across any ground setup —
        // OR a fresh ground contact. Coyote time + jump buffer make jumping forgiving.
        m_groundContact = Mathf::Max(0.0f, m_groundContact - dt);
        bool grounded = (rb && Mathf::Abs(rb->velocity.y) < 0.5f) || m_groundContact > 0.0f;
        m_coyote = grounded ? coyoteTime : Mathf::Max(0.0f, m_coyote - dt);
        if (Input::GetKeyDown(' ')) m_jumpBuf = jumpBufferTime;
        else                        m_jumpBuf = Mathf::Max(0.0f, m_jumpBuf - dt);

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
            if (gameObject && gameObject->scene())
                ResolvePlayerBody(*gameObject->scene(), gameObject);   // no clipping
        }

        // ---- Animation ----
        if (driveAnimation)
            if (Character* ch = FindCharacter()) {
                ch->anim = airborne ? 5
                         : m_stance == Stance::Prone  ? 7
                         : m_stance == Stance::Crouch ? 6
                         : (moving ? (running ? 3 : 2) : 1);
                // The body turns with yaw, so the head only needs to tilt with the
                // look pitch (visible to other players / shadows in first person).
                ch->lookPitch = pitch;
                ch->lookYaw   = 0.0f;
            }
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

    // Sprint held this frame, honouring the toggle option (tap to latch).
    bool IsRunHeld() {
        if (!sprintKey) return false;
        if (toggleRun) {
            if (Input::GetKeyDown(sprintKey)) m_runToggled = !m_runToggled;
            return m_runToggled;
        }
        return Input::GetKey(sprintKey);
    }

    // Resolve crouch / prone from the keys (tap-to-toggle or hold).
    void UpdateStance() {
        if (toggleStance) {
            if (crouchKey && Input::GetKeyDown(crouchKey))
                m_stance = (m_stance == Stance::Crouch) ? Stance::Stand : Stance::Crouch;
            if (proneKey && Input::GetKeyDown(proneKey))
                m_stance = (m_stance == Stance::Prone) ? Stance::Stand : Stance::Prone;
        } else {
            if (proneKey && Input::GetKey(proneKey))        m_stance = Stance::Prone;
            else if (crouchKey && Input::GetKey(crouchKey)) m_stance = Stance::Crouch;
            else                                            m_stance = Stance::Stand;
        }
    }

    // Ease the lean toward Q/E input (-1 left .. +1 right).
    void UpdateLean(float dt) {
        float target = 0.0f;
        if (leanLeftKey  && Input::GetKey(leanLeftKey))  target -= 1.0f;
        if (leanRightKey && Input::GetKey(leanRightKey)) target += 1.0f;
        float t = leanSpeed > 0.0f ? (1.0f - std::exp(-leanSpeed * dt)) : 1.0f;
        m_lean += (target - m_lean) * t;
    }

    // Ease the child camera's local height toward the active stance's eye height.
    void ApplyEyeHeight(float dt) {
        Transform* cam = FindCameraChild();
        if (!cam) return;
        float target = m_stance == Stance::Prone  ? proneEyeHeight
                     : m_stance == Stance::Crouch ? crouchEyeHeight : standEyeHeight;
        float t = stanceLerp > 0.0f ? (1.0f - std::exp(-stanceLerp * dt)) : 1.0f;
        cam->localPosition.y += (target - cam->localPosition.y) * t;
    }

    Vec2 m_lastMouse{0, 0};
    bool m_haveMouse = false;
    bool m_runToggled = false;
    Stance m_stance = Stance::Stand;
    float m_lean = 0.0f;             // eased lean amount (-1..+1)
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

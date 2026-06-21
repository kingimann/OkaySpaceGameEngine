#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Physics/Rigidbody2D.hpp"
#include "okay/Physics/Physics2D.hpp"            // Collision2D (grounded detection)
#include "okay/Components/SpriteRenderer.hpp"    // face-flipping
#include "okay/Input/Input.hpp"
#include "okay/Math/Mathf.hpp"
#include <cmath>

namespace okay {

/// No-code 2D character movement with real platformer "game feel". Reads WASD /
/// arrow keys (and the left stick) and moves the object — top-down (free 8-way)
/// or platformer (left/right + jump). Drives a sibling Rigidbody2D's velocity
/// when present (so collisions work), otherwise moves the Transform directly.
///
/// Platformer extras: sprinting, acceleration/deceleration, air control, coyote
/// time, jump buffering, variable (hold-for-higher) jumps, multi-jump, a fall
/// clamp and extra fall gravity. Grounded state is read from real collisions, so
/// it stays correct on slopes and moving platforms.
class CharacterController2D : public Behaviour {
public:
    enum class Mode { TopDown, Platformer };
    Mode  mode = Mode::TopDown;

    // ---- Movement (both modes) ----
    float speed       = 5.0f;      // walk / move speed
    float runSpeed    = 8.0f;      // speed while the sprint key is held
    char  sprintKey   = 0;         // hold to run (0 = disabled)
    float acceleration = 0.0f;     // units/s^2 toward the target speed (0 = instant)
    float deceleration = 0.0f;     // units/s^2 back toward 0 with no input (0 = instant)
    bool  normalizeDiagonal = true;// top-down: diagonals aren't faster
    bool  useGamepad  = true;      // left stick also drives movement
    bool  flipSprite  = true;      // flip a sibling SpriteRenderer to face travel

    // ---- Platformer ----
    float jumpForce   = 9.0f;      // upward velocity of a jump
    int   maxJumps    = 1;         // 2 = double jump, etc. (ground counts as one)
    bool  variableJump = true;     // release the jump key early to cut it short
    float jumpCutMultiplier = 0.5f;// how much upward speed remains on an early release
    float coyoteTime  = 0.08f;     // can still jump this long after leaving a ledge (s)
    float jumpBuffer  = 0.10f;     // a press this long before landing still jumps (s)
    float airControl  = 1.0f;      // 0..1 horizontal control while airborne
    float maxFallSpeed = 25.0f;    // clamp downward speed (0 = no clamp)
    float extraFallGravity = 0.0f; // extra downward accel while falling (snappier arc)
    char  jumpKey     = ' ';       // jump button (Space); 'W' / Up also jump

    /// True when standing on something (or within coyote time).
    bool IsGrounded() const { return m_grounded; }

    void Update(float dt) override {
        if (!transform) return;
        auto* rb = gameObject ? gameObject->GetComponent<Rigidbody2D>() : nullptr;

        // Input: keyboard axis, optionally overridden by a pushed gamepad stick.
        Vec2 axis = Input::AxisWASD();
        if (useGamepad) {
            Vec2 pad = Input::GamepadAxis();
            if (Mathf::Abs(pad.x) + Mathf::Abs(pad.y) > 0.15f) axis = pad;
        }
        bool running = sprintKey && Input::GetKey(sprintKey);
        float curSpeed = running ? runSpeed : speed;

        if (mode == Mode::TopDown) {
            Vec2 in = axis;
            if (normalizeDiagonal) {
                float len = std::sqrt(in.x * in.x + in.y * in.y);
                if (len > 1.0f) { in.x /= len; in.y /= len; }
            }
            Vec2 target{in.x * curSpeed, in.y * curSpeed};
            if (rb) {
                rb->velocity.x = Approach(rb->velocity.x, target.x, in.x, dt, true);
                rb->velocity.y = Approach(rb->velocity.y, target.y, in.y, dt, true);
            } else {
                transform->Translate({target.x * dt, target.y * dt, 0.0f});
            }
            Face(axis.x);
            return;
        }

        // ---- Platformer ----
        // Grounded from real contacts, OR (fallback when no ground collider is set
        // up) a near-zero vertical speed — the classic heuristic, so a jump still
        // works in the simplest setups.
        bool grounded = m_grounded || (rb && Mathf::Abs(rb->velocity.y) < 0.5f);

        // Coyote + jump-buffer timers.
        if (grounded) { m_coyote = coyoteTime; m_jumpsLeft = maxJumps; }
        else          { m_coyote = Mathf::Max(0.0f, m_coyote - dt); }

        bool jumpPressed = Input::GetKeyDown(jumpKey) || Input::GetKeyDown('w');
        if (jumpPressed) m_jumpBuf = jumpBuffer;
        else             m_jumpBuf = Mathf::Max(0.0f, m_jumpBuf - dt);

        if (rb) {
            // Horizontal with acceleration + air control.
            float ctrl = (grounded || m_coyote > 0.0f) ? 1.0f : airControl;
            float target = axis.x * curSpeed;
            rb->velocity.x = Approach(rb->velocity.x, target, axis.x, dt, false, ctrl);

            bool canCoyote = m_coyote > 0.0f;
            if (m_jumpBuf > 0.0f && (canCoyote || m_jumpsLeft > 0)) {
                rb->velocity.y = jumpForce;
                m_jumpsLeft = (canCoyote ? maxJumps : m_jumpsLeft) - 1;
                m_jumpBuf = 0.0f; m_coyote = 0.0f; m_grounded = false;
            }

            // Variable jump: cut upward speed when the key is released early.
            bool held = Input::GetKey(jumpKey) || Input::GetKey('w');
            if (variableJump && m_jumpWasHeld && !held && rb->velocity.y > 0.0f)
                rb->velocity.y *= jumpCutMultiplier;
            m_jumpWasHeld = held;

            // Snappier fall + terminal velocity.
            if (extraFallGravity > 0.0f && rb->velocity.y < 0.0f)
                rb->velocity.y -= extraFallGravity * dt;
            if (maxFallSpeed > 0.0f && rb->velocity.y < -maxFallSpeed)
                rb->velocity.y = -maxFallSpeed;
        } else {
            transform->Translate({axis.x * curSpeed * dt, 0.0f, 0.0f});
        }
        Face(axis.x);

        // Consume this frame's grounded reading; physics re-sets it from contacts.
        m_grounded = false;
    }

    // Grounded when a contact sits below us (works for slopes / moving platforms).
    void OnCollisionEnter2D(const Collision2D& c) override { CheckGround(c); }
    void OnCollisionStay2D (const Collision2D& c) override { CheckGround(c); }

private:
    bool  m_grounded = false;
    float m_coyote   = 0.0f;
    float m_jumpBuf  = 0.0f;
    int   m_jumpsLeft = 0;
    bool  m_jumpWasHeld = false;

    // Move `v` toward `target`. With no accel/decel set it snaps (the classic
    // behaviour); otherwise it ramps. `ctrl` scales the rate (air control).
    float Approach(float v, float target, float input, float dt,
                   bool topDown, float ctrl = 1.0f) const {
        bool pushing = Mathf::Abs(input) > 0.01f;
        float rate = pushing ? acceleration : deceleration;
        rate *= ctrl;
        if (rate > 0.0f) return Mathf::MoveTowards(v, target, rate * dt);
        return pushing ? target : (topDown ? 0.0f : target);  // platformer: target is 0 with no input
    }

    void Face(float inputX) {
        if (!flipSprite || !gameObject) return;
        if (Mathf::Abs(inputX) < 0.01f) return;
        if (auto* sr = gameObject->GetComponent<SpriteRenderer>())
            sr->flipX = inputX < 0.0f;
    }

    void CheckGround(const Collision2D& c) {
        if (!c.gameObject || !c.gameObject->transform || !transform) return;
        // A contact roughly below our center means we're standing on it.
        if (c.gameObject->transform->Position().y < transform->Position().y - 1e-3f)
            m_grounded = true;
    }
};

} // namespace okay

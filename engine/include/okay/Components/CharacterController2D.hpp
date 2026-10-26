#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Physics/Rigidbody2D.hpp"
#include "okay/Physics/Physics2D.hpp"            // Collision2D (grounded detection)
#include "okay/Components/SpriteRenderer.hpp"    // face-flipping
#include "okay/Components/SpriteAnimator.hpp"    // idle / run / jump / fall clips
#include "okay/Input/Input.hpp"
#include "okay/Net/NetOwnership.hpp"
#include "okay/Net/PlayerInput.hpp"
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
class CharacterController2D : public Behaviour, public IPlayerSim {
public:
    /// Runs before the animation components (Character, ModelAnimator,
    /// AnimStateMachine at 0, IK at 100), so they pose this frame's movement and
    /// state changes, not last frame's.
    int ExecutionOrder() const override { return -10; }
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
    // ---- Wall slide + wall jump (classic platformer) ----
    bool  wallJump      = false;   // enable: slow-slide down walls + jump off them
    float wallSlideSpeed = 2.5f;   // max downward speed while sliding on a wall
    float wallJumpForce  = 9.0f;   // upward speed of a wall jump
    float wallJumpPush   = 7.0f;   // horizontal push away from the wall

    /// True when standing on something (or within coyote time).
    bool IsGrounded() const { return m_grounded; }

    void Update(float dt) override {
        if (!transform) return;
        if (!IsLocallyControlled(gameObject)) return;   // remote proxy: NetworkSync drives it
        Simulate(ReadLocalInput(), dt);
    }

    /// This frame's keys / stick (the only place the controller reads Input::):
    /// the move axis, sprint, and jump (the jump key or W).
    PlayerInput ReadLocalInput() override {
        PlayerInput in;
        // Input: keyboard axis, optionally overridden by a pushed gamepad stick.
        in.move = Input::AxisWASD();
        if (useGamepad) {
            Vec2 pad = Input::GamepadAxis();
            if (Mathf::Abs(pad.x) + Mathf::Abs(pad.y) > 0.15f) in.move = pad;
        }
        if (sprintKey && Input::GetKey(sprintKey)) in.held |= PlayerInput::Sprint;
        if (Input::GetKey(jumpKey) || Input::GetKey('w'))         in.held |= PlayerInput::Jump;
        if (Input::GetKeyDown(jumpKey) || Input::GetKeyDown('w')) in.pressed |= PlayerInput::Jump;
        return in;
    }

    /// Move by `cmd` for `dt` seconds (local play and a server running this
    /// player's inputs). Reads no Input:: globals.
    void Simulate(const PlayerInput& cmd, float dt) override {
        if (!transform) return;
        auto* rb = gameObject ? gameObject->GetComponent<Rigidbody2D>() : nullptr;
        Vec2 axis = cmd.move;
        bool running = sprintKey && cmd.Held(PlayerInput::Sprint);
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
            Animate((Mathf::Abs(in.x) + Mathf::Abs(in.y)) > 0.01f ? "run" : "idle");
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

        bool jumpPressed = cmd.Pressed(PlayerInput::Jump);
        if (jumpPressed) m_jumpBuf = jumpBuffer;
        else             m_jumpBuf = Mathf::Max(0.0f, m_jumpBuf - dt);

        if (rb) {
            // Horizontal with acceleration + air control.
            float ctrl = (grounded || m_coyote > 0.0f) ? 1.0f : airControl;
            float target = axis.x * curSpeed;
            rb->velocity.x = Approach(rb->velocity.x, target, axis.x, dt, false, ctrl);

            bool canCoyote = m_coyote > 0.0f;
            bool onWall = wallJump && !grounded && m_wallDir != 0;
            if (m_jumpBuf > 0.0f) {
                if (onWall && !canCoyote && m_jumpsLeft <= 0) {
                    // Wall jump: up and away from the wall (restores a mid-air jump).
                    rb->velocity.y = wallJumpForce;
                    rb->velocity.x = -(float)m_wallDir * wallJumpPush;
                    m_jumpBuf = 0.0f; m_coyote = 0.0f; m_jumpsLeft = maxJumps - 1;
                } else if (canCoyote || m_jumpsLeft > 0) {
                    rb->velocity.y = jumpForce;
                    m_jumpsLeft = (canCoyote ? maxJumps : m_jumpsLeft) - 1;
                    m_jumpBuf = 0.0f; m_coyote = 0.0f; m_grounded = false;
                }
            }
            // Wall slide: holding toward the wall while falling slows the descent.
            if (onWall && rb->velocity.y < 0.0f && Mathf::Abs(axis.x) > 0.1f &&
                (axis.x < 0.0f ? -1 : 1) == m_wallDir) {
                if (rb->velocity.y < -wallSlideSpeed) rb->velocity.y = -wallSlideSpeed;
            }

            // Variable jump: cut upward speed when the key is released early.
            bool held = cmd.Held(PlayerInput::Jump);
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

        // Sprite clips: airborne stays sticky until a ground contact (or two still
        // frames), so the jump apex's near-zero speed doesn't flicker to idle.
        if (rb) {
            float vy = rb->velocity.y;
            if (vy > 0.5f || (!grounded && vy < -0.5f)) { m_air = true; m_still = 0; }
            else if (m_air) {
                m_still = Mathf::Abs(vy) < 0.05f ? m_still + 1 : 0;
                if (m_grounded || m_still >= 2) m_air = false;
            }
        }
        bool moveX = Mathf::Abs(axis.x) > 0.01f;
        if (m_air) Animate(rb && rb->velocity.y < 0.0f ? "fall" : "jump");
        else       Animate(moveX ? "run" : "idle");

        // Consume this frame's contact readings; physics re-sets them from contacts.
        m_grounded = false;
        m_wallDir = 0;
    }

    // Grounded when a contact sits below us (works for slopes / moving platforms);
    // a contact to the side marks a wall (for wall slide / wall jump).
    void OnCollisionEnter2D(const Collision2D& c) override { CheckGround(c); CheckWall(c); }
    void OnCollisionStay2D (const Collision2D& c) override { CheckGround(c); CheckWall(c); }

private:
    bool  m_grounded = false;
    int   m_wallDir  = 0;      // -1 wall on the left, +1 on the right, 0 none (this frame)
    float m_coyote   = 0.0f;
    float m_jumpBuf  = 0.0f;
    int   m_jumpsLeft = 0;
    bool  m_jumpWasHeld = false;
    bool  m_air = false;       // airborne, for picking the jump / fall clip
    int   m_still = 0;         // airborne frames with ~zero vertical speed

    // Play a named clip on a sibling SpriteAnimator when it has one ("fall" falls
    // back to "jump"). No animator / no such clip: leave the animation alone.
    void Animate(const char* clip) {
        auto* sa = gameObject ? gameObject->GetComponent<SpriteAnimator>() : nullptr;
        if (!sa || sa->clips.empty()) return;
        if (!sa->Play(clip) && std::string(clip) == "fall") sa->Play("jump");
    }

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

    void CheckWall(const Collision2D& c) {
        if (!wallJump || !c.gameObject || !c.gameObject->transform || !transform) return;
        Vec3 cp = c.gameObject->transform->Position(), me = transform->Position();
        float dx = cp.x - me.x, dy = cp.y - me.y;
        // A contact more to the side than above/below is a wall on that side.
        if (Mathf::Abs(dx) > Mathf::Abs(dy)) m_wallDir = dx > 0.0f ? 1 : -1;
    }
};

} // namespace okay

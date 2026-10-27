#pragma once
#include "okay/Core/Log.hpp"
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Net/NetOwnership.hpp"
#include "okay/Net/PlayerInput.hpp"
#include "okay/Components/StanceControl.hpp"
#include "okay/Math/Mathf.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/CharacterIK.hpp"

namespace okay {

/// No-code 3D character movement on the XZ plane from WASD / arrow keys, with an
/// optional jump. Drives a sibling Rigidbody3D's velocity when present (so it
/// collides and gravity applies), otherwise moves the Transform directly. Brought
/// up to the controller family: sprint, momentum (accel/decel), reduced air
/// control, and optional Character animation + foot IK.
class CharacterController3D : public Behaviour, public IPlayerSim {
public:
    /// Runs before the animation components (Character, ModelAnimator,
    /// AnimStateMachine at 0, IK at 100), so they pose this frame's movement and
    /// state changes, not last frame's.
    int ExecutionOrder() const override { return -10; }
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
    /// Crouch (Ctrl) and prone (Z): a smaller, slower body that eases between
    /// stances; SetStance from scripts. No jumping unless standing.
    StanceControl stanceControl;
    void SetStance(int s) { stanceControl.Set(s); }

    void Start() override {
        if (footIK) AttachCharacterFootIK(gameObject);
        stanceControl.Start(gameObject);
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
        if (!IsLocallyControlled(gameObject)) return;   // remote proxy / a server-driven player
        Simulate(ReadLocalInput(), dt);
    }

    /// WASD, the sprint key and space, as this frame reads them.
    PlayerInput ReadLocalInput() override {
        PlayerInput in;
        in.move = Input::AxisWASD();               // x = strafe, y = forward
        if (sprintKey && Input::GetKey(sprintKey)) in.held |= PlayerInput::Sprint;
        if (Input::GetKey(' ')) in.held |= PlayerInput::Jump;
        if (Input::GetKeyDown(' ')) in.pressed |= PlayerInput::Jump;
        const StanceControl& sc = stanceControl;
        if (sc.crouchKey && Input::GetKey(sc.crouchKey))     in.held    |= PlayerInput::Crouch;
        if (sc.crouchKey && Input::GetKeyDown(sc.crouchKey)) in.pressed |= PlayerInput::Crouch;
        if (sc.proneKey && Input::GetKey(sc.proneKey))       in.held    |= PlayerInput::Prone;
        if (sc.proneKey && Input::GetKeyDown(sc.proneKey))   in.pressed |= PlayerInput::Prone;
        return in;
    }

    /// Move by `in` (local play every frame, or a server with authoritative
    /// movement every tick). Reads no Input:: globals.
    void Simulate(const PlayerInput& in, float dt) override {
        if (!transform) return;
        Vec2 axis = in.move;
        auto* rb = gameObject ? gameObject->GetComponent<Rigidbody3D>() : nullptr;
        if (stanceControl.crouchKey || stanceControl.proneKey)
            stanceControl.Keys(in.Pressed(PlayerInput::Crouch), in.Held(PlayerInput::Crouch),
                               in.Pressed(PlayerInput::Prone), in.Held(PlayerInput::Prone));
        stanceControl.Step(gameObject, dt);
        const bool standing = stanceControl.Stance() == 0;
        bool running = standing && sprintKey && in.Held(PlayerInput::Sprint);
        float spd = stanceControl.Speed(running ? runSpeed : speed);   // eases between the stances' caps
        // Normalize diagonal input so W+D isn't ~41% faster than W alone (magnitude √2).
        float alen = Mathf::Sqrt(axis.x * axis.x + axis.y * axis.y);
        if (alen > 1.0f) { axis.x /= alen; axis.y /= alen; }
        float tx = axis.x * spd, tz = axis.y * spd;
        bool moving = (axis.x != 0.0f || axis.y != 0.0f);
        bool grounded = true, jumped = false;

        if (rb) {
            // Contact or terrain, not merely a quiet vertical speed: the apex of
            // a jump is also |vy| < 0.5, and that used to count as a second jump.
            grounded = ResolveGrounded(m_ground, rb->groundedOnTerrain, rb->velocity.y);
            float rate = (moving ? acceleration : deceleration) * (grounded ? 1.0f : airControl);
            rb->velocity.x = Mathf::MoveTowards(rb->velocity.x, tx, rate * dt);
            rb->velocity.z = Mathf::MoveTowards(rb->velocity.z, tz, rate * dt);
            if (canJump && standing && in.Pressed(PlayerInput::Jump) && grounded) {
                rb->velocity.y = jumpForce;
                jumped = true;
                m_ground.left = true; m_ground.fell = false;
                grounded = false;
            }
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
            // Moving platforms: ride whatever we stand on.
            if (gameObject && gameObject->scene())
                RideMovingPlatform(*gameObject->scene(), gameObject, m_platRide, grounded);
        } else {
            transform->Translate({tx * dt, 0.0f, tz * dt});
        }

        if (driveAnimation)
            if (Character* ch = FindCharacterIn(gameObject)) {
                // The body keeps its facing, so sideways input strafes and back input
                // backpedals; the stride follows the real ground speed.
                Character::Locomotion lo;
                lo.velocity  = rb ? rb->velocity : Vec3{tx, 0.0f, tz};
                lo.facing    = transform->localRotation * Vec3{0, 0, -1};
                lo.onGround  = grounded || (rb && rb->groundedOnTerrain);
                lo.jumped    = jumped;
                lo.sprint    = running;
                lo.crouch    = stanceControl.Stance() == 1;
                lo.prone     = stanceControl.Stance() == 2;
                lo.stance    = stanceControl.Level();
                lo.walkSpeed = speed;
                lo.dt = dt;
                ch->DriveLocomotion(lo);
            }
    }

private:
    PlatformRide m_platRide;   // moving-platform tracking
    GroundLatch  m_ground;     // apex of a jump is not ground

    Vec3 m_spawn{0, 0, 0}; bool m_haveSpawn = false;   // fall-reset home position
};

} // namespace okay

#pragma once
#include "okay/Core/Log.hpp"
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Physics/PlayerCollision.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/CharacterIK.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Net/NetOwnership.hpp"
#include "okay/Net/PlayerInput.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Input/Cursor.hpp"
#include "okay/Components/StepUp.hpp"
#include "okay/Math/Mathf.hpp"
#include <cmath>
#include "okay/Components/ControllerFeel.hpp"

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
class ThirdPersonController : public Behaviour, public IPlayerSim {
public:
    // ---- Movement ----
    float walkSpeed = 4.5f;
    float runSpeed  = 8.0f;
    float jumpForce = 6.0f;
    char  sprintKey = Input::KeyShift;  // hold (or tap, if toggleRun) to run; 0 = disabled
    bool  toggleRun = false;            // tap sprint to keep running instead of holding
    bool  canJump = true;
    bool  driveAnimation = true;
    bool  footIK = false;               // plant the Character's feet on the ground

    // ---- Stance: crouch & prone ----
    // Lower the character to crouch or go prone, each with its own speed; the
    // orbit camera's look target drops to match. Toggle = tap, else hold. Running
    // is disabled while crouched or prone.
    char  crouchKey = Input::KeyCtrl;   // 0 = disabled
    char  proneKey  = 'z';              // 0 = disabled
    bool  toggleStance = true;          // tap to toggle vs hold
    float crouchSpeed = 2.2f;
    float proneSpeed  = 1.1f;
    bool  allowProne = true;            // false: no prone at all (key and gamepad)
    bool  resizeBody = true;            // crouch / prone shrink the collider (crawl under things; no standing into a ceiling)
    float crouchHeight = 0.65f;         // crouched body height, fraction of standing
    float proneHeight  = 0.33f;         // prone body height, fraction of standing
    int   stanceGamepadButton = 1;      // tap = crouch / stand, hold = prone (-1 = none; 1 = B)
    float proneHoldTime = 0.4f;         // seconds the gamepad button is held to go prone
    float crouchHeightDrop = 0.4f;      // Minecraft-style modest dip when crouched
    float proneHeightDrop  = 1.1f;      // ...and when prone
    float stanceLerp = 12.0f;           // how fast the target height eases

    enum class Stance { Stand, Crouch, Prone };
    Stance stance() const { return m_stance; }
    /// The resized body (standing height, current height). Saving a scene writes
    /// the standing collider, not the crouched / prone one.
    const StanceBody& stanceBody() const { return m_body; }
    /// Begin in `s` (a scene saved while crouched / prone loads that way, so a
    /// player saved under low cover isn't stood up inside it). Applied in Start().
    void RestoreStance(Stance s) { m_stance = m_wantStance = m_padWant = m_prevStance = s; }

    // ---- Lean (peek with Q/E) ----
    char  leanLeftKey  = 'q';           // 0 = disabled
    char  leanRightKey = 'e';           // 0 = disabled
    float leanAngle  = 12.0f;           // camera roll while leaning (degrees)
    float leanOffset = 0.5f;            // sideways camera shift while leaning (units)
    float leanSpeed  = 10.0f;           // how fast the lean eases in/out
    float lean() const { return m_lean; }
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
    int   maxJumps = 1;                 // 1 = single jump, 2 = double jump, etc.

    // ---- Camera orbit ----
    float mouseSensitivity = 0.2f;      // degrees per pixel
    bool  invertY = false;              // invert vertical mouse look
    bool  invertX = false;             // invert horizontal mouse look
    bool  lockCursor = true;            // hide + lock the cursor while playing (off: keep the pointer for UI)
    float distance  = 5.0f;             // camera distance from the player
    float minDistance = 2.0f, maxDistance = 12.0f;
    float zoomSpeed = 1.0f;             // wheel zoom step
    float cameraHeight = 1.5f;          // look target height above the player origin
    float minPitch = -20.0f, maxPitch = 70.0f;
    float shoulderOffset = 0.0f;        // lateral camera offset (over-the-shoulder)
    // Follow: the camera orbits a smoothed point at the player's head (the
    // pivot), never lagging behind its own rotation, so the player stays centred
    // while you orbit. Horizontal and vertical ease on their own: the slower
    // vertical soaks up steps, landings and bob. Higher = tighter; 0 = instant.
    float pivotDampingXZ = 20.0f;
    float pivotDampingY  = 10.0f;
    float maxPivotLag = 0.6f;           // the pivot never trails the player by more (m); 0 = no limit
    // Orbit smoothing (seconds): eases the camera's yaw / pitch toward the mouse.
    // 0 = raw. Movement direction always uses the raw angles.
    float orbitSmoothing = 0.0f;
    // Camera collision (spring arm): pull the camera in when a wall/floor is between
    // it and the player so it never clips through geometry. `skin` keeps the near
    // plane off the surface. A sphere of `radius` is swept (triggers and the
    // player's own parts never count); `mask` picks the layers that block it.
    bool  cameraCollision = true;
    float cameraCollisionSkin = 0.3f;
    float cameraCollisionRadius = 0.2f;
    std::uint32_t cameraCollisionMask = Physics3D::kAllLayers;
    float cameraCollisionPullIn = 30.0f;   // how fast a hair-closer obstacle eases the camera in
    // When an obstacle clears, ease the camera back out to its full distance
    // instead of snapping (a classic spring arm). Higher = quicker recovery.
    float cameraCollisionRecover = 6.0f;

    /// How the body is oriented while playing.
    ///   Movement: turn to face the direction of travel (classic adventure feel).
    ///   Camera:   always face the camera's forward (strafe / shooter / aim feel).
    enum class FaceMode { Movement, Camera };
    FaceMode faceMode = FaceMode::Movement;

    float yaw = 0.0f, pitch = 18.0f;    // camera orbit angles (degrees)

    // ---- Feel ----
    float lookSmoothing = 0.0f;         // mouse smoothing time (s); 0 = raw input
    float sprintFovBoost = 6.0f;        // FOV widens this much while sprinting (0 = off)
    float fovLerp = 7.0f;
    // Aim (hold right mouse): over-the-shoulder close-up, the body faces where the
    // camera looks (strafe), lower sensitivity, no sprinting — shooter / bow aiming.
    bool  aimWithRightMouse = true;
    float aimDistance = 2.2f;
    float aimShoulder = 0.65f;
    float aimFov = 48.0f;
    float aimSensitivity = 0.6f;
    float aimLerp = 10.0f;
    // Auto-recenter: after a moment without mouse input, the camera swings back
    // behind the way the character is running.
    bool  autoRecenter = false;
    float recenterDelay = 1.5f;
    float recenterSpeed = 2.5f;
    float landShake = 0.25f;            // camera bump on hard landings (world units, max)
    float strideLength = 1.5f;          // distance per footstep event at walk speed

    // ---- Slide (sprint, then crouch) ----
    bool  canSlide = true;
    float slideSpeed = 10.0f;
    float slideTime  = 0.75f;

    // ---- Hooks ----
    bool  driveStateMachine = true;     // feed an AnimStateMachine speed/grounded/crouch/aim/... + jump/land triggers
    bool  ikLook = true;                // a Full Body IK on the body looks where the camera aims
    std::function<void(bool left)> onFootstep;
    std::function<void()>          onJump;
    std::function<void(float impactSpeed)> onLand;
    int   FootstepCount() const { return m_steps; }
    bool  IsAiming() const { return m_aim; }
    bool  IsSliding() const { return m_slideT > 0.0f; }
    bool  IsGrounded() const { return m_grounded; }


    /// Safety net: falling below this world Y teleports the player back to its
    /// spawn point (fell off the map / through a floor with no collider).
    /// Set to 0 to disable.
    float fallResetY = -100.0f;
    /// Max step height the controller climbs automatically (stairs/curbs). 0 = off.
    float stepOffset = 0.35f;
    /// Steepest terrain slope (degrees) the player can stand on / climb; on
    /// steeper ground the uphill velocity is cancelled and the body slides
    /// back down. 0 = no limit.
    float slopeLimit = 50.0f;
    void Start() override {
        if (footIK) AttachCharacterFootIK(gameObject);
        if (transform) { m_spawn = transform->Position(); m_haveSpawn = true; }
        ApplyRestoredStance();
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
        if (Game::Paused()) return;   // frozen while the pause menu is up
        m_lookDt = dt;   // also for a client's ReadLocalInput in authoritative movement
        if (!IsLocallyControlled(gameObject)) return;   // remote proxy: NetworkSync drives it
        // Optionally hide + lock the cursor while playing (re-assert if freed).
        if (lockCursor && !Cursor::IsLocked()) Cursor::Capture(true);
        Simulate(ReadLocalInput(), dt);
    }

    /// This frame's keyboard / mouse / pad, with the camera orbit applied to yaw
    /// and pitch (absolute angles). The only place the controller reads Input::;
    /// the scroll-wheel zoom (camera only) is applied here too.
    PlayerInput ReadLocalInput() override {
        PlayerInput in;
        // ---- Camera orbit input ----
        // Mouse-right orbits the camera right and mouse-up looks up — matching the
        // first-person controller's convention (yaw decreases as the mouse moves
        // right). Flip per-axis with invertX / invertY.
        Vec2 mp = Input::MousePosition();
        bool uiBlocked = Input::UICaptured();        // a modal UI (open inventory/chest) pauses look
        bool aim = !uiBlocked && Input::GetMouseButton(1);
        in.yaw = yaw; in.pitch = pitch;
        if (m_haveMouse && !uiBlocked) {
            float sens = mouseSensitivity * (aimWithRightMouse && aim ? aimSensitivity : 1.0f);
            float dx = (mp.x - m_lastMouse.x) * sens, dy = (mp.y - m_lastMouse.y) * sens;
            m_smooth.Step(dx, dy, lookSmoothing, m_lookDt);
            if (std::fabs(mp.x - m_lastMouse.x) + std::fabs(mp.y - m_lastMouse.y) > 0.5f) m_mouseIdle = 0.0f;
            else m_mouseIdle += m_lookDt;
            in.yaw   += (invertX ? 1.0f : -1.0f) * dx;
            in.pitch += (invertY ? -1.0f : 1.0f) * dy;
            // Kept here too, so the look adds up where Simulate doesn't run every
            // frame (a client whose host runs its movement).
            yaw = in.yaw;
            pitch = in.pitch = Mathf::Clamp(in.pitch, minPitch, maxPitch);
        }
        // Drop the baseline while a bag is open (locked/free mouse space switch) so the
        // camera doesn't snap on the first frame after the panel closes.
        m_lastMouse = mp; m_haveMouse = !uiBlocked;
        float wheel = uiBlocked ? 0.0f : Input::MouseWheel();
        if (wheel != 0.0f) distance = Mathf::Clamp(distance - wheel * zoomSpeed, minDistance, maxDistance);
        if (aim) in.held |= PlayerInput::AltFire;

        // ---- Movement relative to the camera's yaw (keyboard or gamepad) ----
        in.move = uiBlocked ? Vec2{0, 0} : Input::AxisWASD();   // freeze while a bag is open
        Vec2 pad = Input::GamepadAxis();
        if (Mathf::Abs(pad.x) + Mathf::Abs(pad.y) > 0.15f) in.move = pad;   // left stick overrides
        auto key = [&](char k, std::uint32_t bit) {
            if (!k) return;
            if (Input::GetKey(k))     in.held |= bit;
            if (Input::GetKeyDown(k)) in.pressed |= bit;
        };
        key(' ', PlayerInput::Jump);
        key(sprintKey, PlayerInput::Sprint);
        key(crouchKey, PlayerInput::Crouch);
        key(proneKey, PlayerInput::Prone);
        key(leanLeftKey, PlayerInput::LeanLeft);
        key(leanRightKey, PlayerInput::LeanRight);
        if (stanceGamepadButton >= 0 && Input::GetGamepadButton(stanceGamepadButton)) in.held |= PlayerInput::StanceButton;
        return in;
    }

    /// Orbit, stance, lean and move by `in` for `dt` seconds (local play and a
    /// server running this player's inputs). Reads no Input:: globals.
    void Simulate(const PlayerInput& in, float dt) override {
        if (!transform) return;
        yaw = in.yaw;
        // Clamped when it changes (a script may park the view past the limits, as before).
        if (in.pitch != pitch) pitch = Mathf::Clamp(in.pitch, minPitch, maxPitch);
        m_aim = aimWithRightMouse && in.Held(PlayerInput::AltFire);
        Vec2 axis = in.move;
        Quat flat = Quat::Euler(0, yaw, 0);
        Vec3 fwd = flat * Vec3{0, 0, -1}, right = flat * Vec3::Right;   // forward = into the view (-Z)
        Vec3 dir = fwd * axis.y + right * axis.x;
        float len = std::sqrt(dir.x * dir.x + dir.z * dir.z);
        bool moving = len > 0.01f;
        if (moving) { dir.x /= len; dir.z /= len; }

        // Stance (crouch / prone) + lean (Q/E peek) + run (no sprint while crouched/prone).
        UpdateStance(in, dt);
        UpdateLean(in, dt);
        bool running = m_stance == Stance::Stand && moving && IsRunHeld(in) && !m_aim;
        float speed = m_stance == Stance::Prone  ? proneSpeed
                    : m_stance == Stance::Crouch ? crouchSpeed
                    : (running ? runSpeed : walkSpeed);

        auto* rb = gameObject ? gameObject->GetComponent<Rigidbody3D>() : nullptr;

        // Grounded: resting on something (low vertical speed) — the reliable signal
        // that works regardless of how the ground is set up — OR a fresh ground
        // contact. Coyote time + a jump buffer make jumping forgiving.
        m_groundContact = Mathf::Max(0.0f, m_groundContact - dt);
        // Heightmap terrain has no collider (no collision callbacks); Physics3D flags
        // resting-on-terrain on the body so terrain counts as ground for jumping.
        bool terrainGround = rb && rb->groundedOnTerrain;
        bool grounded = (rb && Mathf::Abs(rb->velocity.y) < 0.5f) || m_groundContact > 0.0f || terrainGround;
        // Jump count refills only on a real ground contact, so the zero-velocity at
        // the jump apex can't grant another jump (no more endless jumping). maxJumps
        // enables double (or more) jumps.
        if (m_groundContact > 0.0f || terrainGround) m_jumpsUsed = 0;
        m_grounded = grounded;
        // Slide: crouch while sprinting on the ground -> a burst that decays to a crouch.
        if (canSlide && rb && grounded && m_wasRunning && m_slideT <= 0.0f &&
            m_stance == Stance::Crouch && m_prevStance == Stance::Stand) {
            m_slideT = slideTime;
            m_slideDir = moving ? dir : (transform->localRotation * Vec3{0, 0, -1});
            m_slideDir.y = 0.0f;
        }
        m_prevStance = m_stance;
        if (m_slideT > 0.0f) { m_slideT -= dt; if (m_stance != Stance::Crouch) m_slideT = 0.0f; }
        m_coyote = grounded ? coyoteTime : Mathf::Max(0.0f, m_coyote - dt);
        if (!grounded && m_coyote <= 0.0f && m_jumpsUsed == 0) m_jumpsUsed = 1;
        if (in.Pressed(PlayerInput::Jump)) m_jumpBuf = jumpBufferTime;
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
            if (m_slideT > 0.0f && grounded) {
                float k = Mathf::Clamp(m_slideT / Mathf::Max(0.01f, slideTime), 0.0f, 1.0f);
                float sp = crouchSpeed + (slideSpeed - crouchSpeed) * k * k;
                float sl = std::sqrt(m_slideDir.x * m_slideDir.x + m_slideDir.z * m_slideDir.z);
                if (sl > 1e-4f) { rb->velocity.x = m_slideDir.x / sl * sp; rb->velocity.z = m_slideDir.z / sl * sp; }
                if (canJump && m_jumpBuf > 0.0f) {
                    // Jumping out of a slide stands you up — only where there is room
                    // to stand; under low cover the slide just carries on.
                    Scene* sc = gameObject->scene();
                    bool room = !resizeBody || !sc || StanceHasRoom(*sc, gameObject, m_body, StanceHeight(Stance::Stand));
                    if (room) { m_slideT = 0.0f; m_stance = m_wantStance = Stance::Stand; m_prevStance = Stance::Stand; ApplyStanceHeight(gameObject, m_body, StanceHeight(Stance::Stand)); }
                    else m_jumpBuf = 0.0f;
                }
            }
            // Jump: first jump needs ground/coyote; extra jumps (double-jump) up to maxJumps.
            if (canJump && m_jumpBuf > 0.0f && m_stance == Stance::Stand) {   // no jumping while crouched/prone
                bool firstOk = (m_jumpsUsed == 0) && (grounded || m_coyote > 0.0f);
                bool extraOk = (m_jumpsUsed >= 1) && (m_jumpsUsed < Mathf::Max(1, maxJumps));
                if (firstOk || extraOk) {
                    rb->velocity.y = jumpForce;
                    ++m_jumpsUsed;
                    m_jumped = true;
                    if (onJump) onJump();
                    m_jumpBuf = 0.0f; m_coyote = 0.0f; m_groundContact = 0.0f;
                    grounded = false; airborne = true;
                }
            }
        } else if (moving) {
            transform->Translate(dir * (speed * dt));
            if (gameObject && gameObject->scene())
                ResolvePlayerBody(*gameObject->scene(), gameObject);   // no clipping
        }
        // Slope limit: on terrain steeper than slopeLimit, cancel the uphill
        // velocity component and accelerate a downhill slide.
        if (rb && (grounded || rb->groundedOnTerrain) && slopeLimit > 0.0f && rb->groundNormal.y < std::cos(slopeLimit * Mathf::Deg2Rad)) {
            Vec3 n = rb->groundNormal;
            float hl = std::sqrt(n.x * n.x + n.z * n.z);
            if (hl > 1e-4f) {
                Vec3 dh{n.x / hl, 0.0f, n.z / hl};                       // downhill direction
                float up = -(rb->velocity.x * dh.x + rb->velocity.z * dh.z);   // uphill speed
                if (up > 0.0f) { rb->velocity.x += dh.x * up; rb->velocity.z += dh.z * up; }
                rb->velocity.x += dh.x * 18.0f * dt;                     // slide
                rb->velocity.z += dh.z * 18.0f * dt;
            }
        }
        // Stairs: a grounded body blocked by a LOW obstacle steps up onto it
        // instead of grinding against the face (stepOffset = max step height).
        if (rb && grounded && moving && gameObject && gameObject->scene())
            TryStepUp(*gameObject->scene(), gameObject, rb, dir, grounded, moving, stepOffset);
        // Moving platforms: ride whatever we stand on (elevators, movers).
        if (gameObject && gameObject->scene())
            RideMovingPlatform(*gameObject->scene(), gameObject, m_platRide, grounded);

        // ---- Turn the body (smoothly) ----
        // The character mesh faces -Z (engine camera/controller convention).
        Quat want;
        bool turn = false;
        if (faceMode == FaceMode::Camera || m_aim) {
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

        // ---- Landing, footsteps, auto-recenter ----
        float hSpeed = rb ? std::sqrt(rb->velocity.x * rb->velocity.x + rb->velocity.z * rb->velocity.z)
                          : (moving ? speed : 0.0f);
        {
            float impact = 0.0f;
            m_landing.SetDt(dt);
            m_landed = m_landing.Step(grounded, rb ? rb->velocity.y : 0.0f, impact);
            if (m_landed) { m_shake = Mathf::Min(landShake, impact * 0.02f); if (onLand) onLand(impact); }
            if (grounded && hSpeed > 0.3f && m_slideT <= 0.0f) {
                float stride = strideLength * Mathf::Max(0.6f, Mathf::Sqrt(Mathf::Max(0.1f, hSpeed / Mathf::Max(0.1f, walkSpeed))));
                m_stepDist += hSpeed * dt;
                if (m_stepDist >= stride) { m_stepDist -= stride; ++m_steps; if (onFootstep) onFootstep((m_steps & 1) != 0); }
            }
            if (autoRecenter && moving && !m_aim && m_mouseIdle > recenterDelay) {
                Vec3 f = transform->localRotation * Vec3{0, 0, -1};
                float bodyYaw = Mathf::Atan2(-f.x, -f.z) * Mathf::Rad2Deg;
                float d = std::fmod(bodyYaw - yaw + 540.0f, 360.0f) - 180.0f;
                yaw += d * (1.0f - std::exp(-recenterSpeed * dt));
            }
        }
        m_wasRunning = running && hSpeed > walkSpeed * 0.8f;
        m_lastSpeed = hSpeed; m_lastVy = rb ? rb->velocity.y : 0.0f; m_running = running;

        // ---- Animation ----
        if (driveAnimation)
            if (Character* ch = FindCharacter()) {
                // State from what the body really does (speed over the ground, in the
                // air or not); the stride follows the speed, and while aiming (body
                // faces the camera) the legs turn toward the travel direction.
                Character::Locomotion lo;
                lo.velocity = rb ? rb->velocity : (moving ? dir * speed : Vec3{0, 0, 0});
                lo.facing   = transform->localRotation * Vec3{0, 0, -1};
                lo.onGround = !rb || m_groundContact > 0.0f || terrainGround;
                lo.jumped   = m_jumped;
                lo.crouch   = m_stance == Stance::Crouch;
                lo.prone    = m_stance == Stance::Prone;
                lo.sprint   = running;
                lo.walkSpeed = walkSpeed;
                lo.dt = dt;
                ch->DriveLocomotion(lo);
                // The head turns to keep looking where the camera points (relative to
                // the body's current facing) AND tilts up/down with the orbit pitch,
                // so the avatar looks around as you aim. (18 = the resting orbit pitch,
                // so the head is level by default; aiming up raises the gaze.)
                //
                // bodyYaw is the body's facing as a world heading; the camera heading
                // in that same convention is -yaw (the camera is built from
                // Euler(0, yaw, 0), whose forward sits at world heading -yaw). The
                // head offset the rig wants is (camera heading) - (body heading) read
                // back through the rig's Euler-Y sense, which works out to yaw +
                // bodyYaw. The old `yaw - bodyYaw` mixed the two sign conventions, so
                // the head turned the wrong way (and cranked to ±2·yaw when the body
                // already faced the camera).
                // If the Character is set to look at the camera/target, let IT drive
                // the head (yaw AND pitch toward the camera) — don't fight it here.
                if (!ch->lookAtCamera && ch->lookAtTarget.empty()) {
                    Vec3 f = transform->localRotation * Vec3{0, 0, -1};
                    float bodyYaw = Mathf::Atan2(f.x, -f.z) * Mathf::Rad2Deg;
                    float rel = yaw + bodyYaw;
                    // Fold into the neck's reach: instead of cranking to a hard
                    // sideways clamp when the camera swings to the front (where the
                    // head would need to turn ~180°), ease back toward neutral — so
                    // the head faces the camera when the body already faces it.
                    ch->lookYaw   = Character::NeckYaw(rel);
                    ch->lookPitch = 18.0f - pitch;
                }
                ch->bodyLean  = m_lean * leanAngle;   // body leans (camera stays put)
            }
        if (driveStateMachine) {
            AnimLink::Frame f;
            f.speed = hSpeed; f.verticalSpeed = rb ? rb->velocity.y : 0.0f; f.grounded = grounded;
            if (rb) f.velocity = rb->velocity;
            f.crouch = m_stance == Stance::Crouch; f.prone = m_stance == Stance::Prone;
            f.sprint = running; f.slide = m_slideT > 0.0f; f.aim = m_aim;
            f.jumped = m_jumped; f.landed = m_landed;
            m_anim.Apply(gameObject, f);
        }
        m_jumped = false;
    }

    void LateUpdate(float dt) override {
        if (!transform || !gameObject) return;
        Scene* sc = gameObject->scene();
        if (!sc || !sc->mainCamera) return;
        Transform* cam = sc->mainCamera->transform;
        if (!cam) return;
        if (sc->mainCamera != m_claimedCam) { ClaimMainCamera(sc->mainCamera, "Third Person Controller"); m_claimedCam = sc->mainCamera; }

        // Ease the look target down when crouched / prone so the camera follows
        // the lower head position.
        float drop = m_stance == Stance::Prone  ? proneHeightDrop
                   : m_stance == Stance::Crouch ? crouchHeightDrop : 0.0f;
        float lt = stanceLerp > 0.0f ? (1.0f - std::exp(-stanceLerp * dt)) : 1.0f;
        m_stanceDrop += (drop - m_stanceDrop) * lt;

        // The pivot: the head, smoothed (see pivotDampingXZ).
        Vec3 head = transform->Position();
        head.y += cameraHeight - m_stanceDrop;
        const Vec3 target = m_pivot.Step(head, pivotDampingXZ, pivotDampingY, maxPivotLag, dt);
        // Aim blend (0 = normal orbit, 1 = over-the-shoulder aim).
        {
            float at = aimLerp > 0.0f ? (1.0f - std::exp(-aimLerp * dt)) : 1.0f;
            m_aimT += ((m_aim ? 1.0f : 0.0f) - m_aimT) * at;
        }
        const float useDistance = distance + (aimDistance - distance) * m_aimT;
        const float useShoulder = shoulderOffset + (aimShoulder - shoulderOffset) * m_aimT;
        m_shake *= 1.0f - EaseFactor(8.0f, dt);

        // The orbit angles: raw, or eased (the shortest way round for yaw) when
        // orbitSmoothing is on. Position and rotation both use the same pair.
        if (orbitSmoothing > 0.0f && m_haveOrbit) {
            const float k = 1.0f - std::exp(-dt / orbitSmoothing);
            m_orbitYaw += std::remainder(yaw - m_orbitYaw, 360.0f) * k;
            m_orbitPitch += (pitch - m_orbitPitch) * k;
        } else {
            m_orbitYaw = yaw; m_orbitPitch = pitch;
        }
        m_haveOrbit = true;

        // Place the camera BEHIND the player (+Z of its facing at yaw 0), lifted by
        // pitch so a positive pitch looks DOWN at the player from above, around the
        // pivot with no lag of its own (so it never trails its rotation).
        float pr = m_orbitPitch * Mathf::Deg2Rad;
        Vec3 behind = Quat::Euler(0, m_orbitYaw, 0) * Vec3{0, 0, 1};   // horizontal, behind the facing
        float cp = Mathf::Cos(pr);
        Vec3 offset{behind.x * cp, Mathf::Sin(pr), behind.z * cp};
        Vec3 camPos = target + offset * useDistance;

        // Over-the-shoulder: shift the camera sideways along its horizontal right.
        // The rotation stays level (built from yaw/pitch below), so the view is a
        // parallel offset — no roll.
        if (useShoulder != 0.0f)
            camPos = camPos + (Quat::Euler(0, m_orbitYaw, 0) * Vec3::Right) * useShoulder;

        // Camera collision (spring arm), from the pivot to the camera, after the
        // smoothing so the view never clips even while the follow eases.
        if (cameraCollision) {
            Vec3 d = camPos - target;
            float dist = d.Magnitude();
            if (dist > 1e-4f) {
                Vec3 dn = d * (1.0f / dist);
                camPos = target + dn * m_arm.Step(*sc, gameObject, target, dn, dist, cameraCollisionRadius,
                                                  cameraCollisionSkin, cameraCollisionMask,
                                                  cameraCollisionPullIn, cameraCollisionRecover, dt);
            }
        }

        // Never let the camera drop below the player's base (the pivot's, so it
        // doesn't dip on a step down). minPitch allows looking up from slightly
        // below, which at close distance could otherwise sink the camera through
        // the floor — from under a single-sided ground plane the floor vanishes,
        // and grazing it reads as a glitchy edge.
        float minY = target.y - cameraHeight;
        if (camPos.y < minY) camPos.y = minY;

        cam->SetPosition(camPos - Vec3{0, m_shake, 0});
        // Build the rotation straight from yaw/pitch with NO roll component (z = 0)
        // so the horizon is always level. Regular third person does NOT move the
        // camera when leaning — the character's body leans instead (see Update).
        cam->localRotation = Quat::Euler(-m_orbitPitch, m_orbitYaw, 0.0f);
        m_fov.Update(sc->mainCamera, m_running && m_lastSpeed > walkSpeed * 0.9f, sprintFovBoost,
                     m_aimT > 0.5f, aimFov, fovLerp, dt);
        // Full Body IK: look where the camera looks (a point well ahead of it).
        if (ikLook)
            if (FullBodyIK* fb = FindInTree<FullBodyIK>(gameObject)) {
                fb->lookEnabled = true; fb->lookTarget = nullptr;
                fb->lookPosition = cam->Position() + (cam->Rotation() * Vec3{0, 0, -1}) * 20.0f;
            }
    }

    // Grounded detection: a contact counts as ground when it's a roughly-vertical
    // hit with something below the player. Both enter and stay refresh it (the timer
    // in Update lets it survive the frame-order of the physics step).
    void OnCollisionEnter3D(const Collision3D& c) override { NoteGround(c); }
    void OnCollisionStay3D(const Collision3D& c)  override { NoteGround(c); }

private:
    PlatformRide m_platRide;   // moving-platform tracking

    Vec3 m_spawn{0, 0, 0}; bool m_haveSpawn = false;   // fall-reset home position

    void NoteGround(const Collision3D& c) {
        bool vertical = Mathf::Abs(c.normal.y) > 0.5f;
        bool below = c.gameObject && c.gameObject->transform && transform &&
                     c.gameObject->transform->Position().y < transform->Position().y;
        if (vertical && below) m_groundContact = 0.10f;
    }

    bool IsRunHeld(const PlayerInput& in) {
        if (!sprintKey) return false;
        if (toggleRun) {
            if (in.Pressed(PlayerInput::Sprint)) m_runToggled = !m_runToggled;
            return m_runToggled;
        }
        return in.Held(PlayerInput::Sprint);
    }
    // Resolve the stance from the keys / gamepad, then size the body for it.
    // Toggle mode remembers what you asked for, so a stand-up blocked by a low
    // ceiling happens on its own once you crawl out from under it.
    void UpdateStance(const PlayerInput& in, float dt) {
        if (toggleStance) {
            if (crouchKey && in.Pressed(PlayerInput::Crouch))
                m_wantStance = (m_wantStance == Stance::Crouch) ? Stance::Stand : Stance::Crouch;
            if (allowProne && proneKey && in.Pressed(PlayerInput::Prone))
                m_wantStance = (m_wantStance == Stance::Prone) ? Stance::Stand : Stance::Prone;
        } else {
            // Held keys win; with none held you're in the stance the gamepad last set.
            if (allowProne && proneKey && in.Held(PlayerInput::Prone))  { m_wantStance = Stance::Prone;  m_padWant = Stance::Stand; }
            else if (crouchKey && in.Held(PlayerInput::Crouch))         { m_wantStance = Stance::Crouch; m_padWant = Stance::Stand; }
            else                                                      m_wantStance = m_padWant;
        }
        // Gamepad: tap the stance button to crouch / stand up, hold it to drop prone.
        if (stanceGamepadButton >= 0) {
            if (in.Held(PlayerInput::StanceButton)) {
                m_padHeld += dt;
                if (allowProne && !m_padProned && m_padHeld >= proneHoldTime) { m_wantStance = m_padWant = Stance::Prone; m_padProned = true; }
            } else if (m_padHeld > 0.0f) {
                if (!m_padProned) m_wantStance = m_padWant = (m_stance == Stance::Stand) ? Stance::Crouch : Stance::Stand;
                m_padHeld = 0.0f; m_padProned = false;
            }
        }
        if (!allowProne && m_wantStance == Stance::Prone) m_wantStance = Stance::Stand;
        Stance next = m_wantStance;
        if (resizeBody && gameObject) {
            CaptureStance(gameObject, m_body);
            Scene* sc = gameObject->scene();
            // Rising needs headroom; from prone, settle for a crouch if only that fits.
            if (sc && m_body.captured && StanceHeight(next) > m_body.curH + 1e-3f &&
                !StanceHasRoom(*sc, gameObject, m_body, StanceHeight(next))) {
                if (next == Stance::Stand && m_stance == Stance::Prone &&
                    StanceHasRoom(*sc, gameObject, m_body, StanceHeight(Stance::Crouch))) next = Stance::Crouch;
                else next = m_stance;
            }
            m_stance = next;
            ApplyStanceHeight(gameObject, m_body, StanceHeight(m_stance));
        } else {
            m_stance = next;
        }
    }
    // A restored crouch / prone (RestoreStance) sizes the body before physics runs.
    void ApplyRestoredStance() {
        if (!resizeBody || !gameObject || m_stance == Stance::Stand) return;
        CaptureStance(gameObject, m_body);
        ApplyStanceHeight(gameObject, m_body, StanceHeight(m_stance));
    }
    /// Collider height for a stance (crouch / prone are fractions of the standing body).
    float StanceHeight(Stance s) const {
        float h = m_body.standH;
        return s == Stance::Prone ? h * proneHeight : (s == Stance::Crouch ? h * crouchHeight : h);
    }
    void UpdateLean(const PlayerInput& in, float dt) {
        float target = 0.0f;
        if (leanLeftKey  && in.Held(PlayerInput::LeanLeft))  target -= 1.0f;
        if (leanRightKey && in.Held(PlayerInput::LeanRight)) target += 1.0f;
        float t = leanSpeed > 0.0f ? (1.0f - std::exp(-leanSpeed * dt)) : 1.0f;
        m_lean += (target - m_lean) * t;
    }

    Vec2 m_lastMouse{0, 0};
    float m_lookDt = 0.0f;           // this frame's dt, for the look smoothing in ReadLocalInput
    bool m_haveMouse = false;
    bool m_runToggled = false;
    float m_lean = 0.0f;             // eased lean amount (-1..+1)
    int   m_jumpsUsed = 0;          // jumps since last grounded (for double-jump)
    Stance m_stance = Stance::Stand;
    Stance m_wantStance = Stance::Stand;   // what the input asks for (m_stance may wait for headroom)
    StanceBody m_body;
    float m_padHeld = 0.0f; bool m_padProned = false;
    Stance m_padWant = Stance::Stand;   // the stance the gamepad button set (hold mode)
    float m_stanceDrop = 0.0f;        // smoothed look-target drop for crouch/prone
    FollowPivot m_pivot;              // the smoothed point the camera orbits
    SpringArm m_arm;                  // camera collision
    float m_orbitYaw = 0.0f, m_orbitPitch = 0.0f;   // the angles the camera uses (orbitSmoothing)
    bool m_haveOrbit = false;
    Camera* m_claimedCam = nullptr;   // the main camera last checked for a competing follow
    float m_groundContact = 0.0f;   // time-to-live of the last ground contact
    float m_coyote = 0.0f;          // remaining coyote-time window
    float m_jumpBuf = 0.0f;         // remaining jump-buffer window
    LookSmoother m_smooth;
    FovKick m_fov;
    Landing m_landing;
    AnimLink m_anim;
    bool  m_aim = false, m_grounded = true, m_wasRunning = false, m_jumped = false, m_landed = false, m_running = false;
    float m_aimT = 0.0f, m_mouseIdle = 0.0f, m_shake = 0.0f, m_stepDist = 0.0f, m_lastSpeed = 0.0f, m_lastVy = 0.0f;
    Stance m_prevStance = Stance::Stand;
    float m_slideT = 0.0f;
    Vec3  m_slideDir{0, 0, -1};
    int   m_steps = 0;

    Character* FindCharacter() const {
        if (gameObject) if (auto* ch = gameObject->GetComponent<Character>()) return ch;
        if (transform)
            for (Transform* c : transform->Children())
                if (c && c->gameObject) if (auto* ch = c->gameObject->GetComponent<Character>()) return ch;
        return nullptr;
    }
};

} // namespace okay

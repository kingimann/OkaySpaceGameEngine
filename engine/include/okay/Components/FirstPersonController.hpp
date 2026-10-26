#pragma once
#include "okay/Core/Log.hpp"
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
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
#include "okay/Components/ControllerFeel.hpp"
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
class FirstPersonController : public Behaviour, public IPlayerSim {
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
    int   maxJumps = 1;                 // 1 = single jump, 2 = double jump, etc.
    float mouseSensitivity = 0.15f;     // degrees per pixel of mouse movement
    float minPitch = -85.0f, maxPitch = 85.0f;
    bool  invertY = false;              // invert vertical mouse look
    // View bob: a subtle up/down camera sway in step with walking, so movement
    // feels grounded instead of gliding. Scales with speed; smooths out when idle.
    bool  headBob   = true;
    float bobAmount = 0.035f;           // bob height (world units)
    float bobSpeed  = 9.0f;             // bob cadence (≈ footstep rate)
    float bobSway   = 0.015f;           // sideways sway (world units), at half the bob's rate
    float bobRunScale = 0.8f;           // bob size at run speed (x the walking bob)
    float bobBlend  = 8.0f;             // how fast the bob fades in / out as you start / stop
    float bobGroundGrace = 0.12f;       // s the bob and landing dip still count you as grounded (stairs, slopes)
    // Eye smoothing: when the body is lifted or dropped while you're on the
    // ground (a step, a curb, a platform), the eye eases there instead of
    // popping. How fast (higher = quicker; 0 = no smoothing).
    float eyeStepSmoothing = 14.0f;
    char  sprintKey = Input::KeyShift;  // hold (or tap, if toggleRun) to run; 0 = disabled
    bool  toggleRun = false;            // tap sprint to keep running instead of holding
    bool  canJump = true;
    bool  driveAnimation = true;        // set a sibling Character's anim from movement
    bool  footIK = false;               // plant the Character's feet on the ground (foot IK)
    bool  showBody = false;             // first person: hide your own body (no head clipping)
    bool  lockCursor = true;            // hide + lock the cursor to the window while playing

    // ---- Stance: crouch & prone ----
    // Lower your profile to fit under things / take cover. Each has its own speed
    // and eye height; the camera eases between heights. Toggle = tap to switch,
    // otherwise hold the key. Running is disabled while crouched or prone.
    char  crouchKey = Input::KeyCtrl;   // 0 = disabled
    char  proneKey  = 'z';              // 0 = disabled
    bool  toggleStance = true;          // tap to toggle vs hold to hold the stance
    float crouchSpeed = 2.2f;
    float proneSpeed  = 1.1f;
    bool  allowProne = true;            // false: no prone at all (key and gamepad)
    bool  resizeBody = true;            // crouch / prone shrink the collider (crawl under things; no standing into a ceiling)
    float crouchHeight = 0.65f;         // crouched body height, fraction of standing
    float proneHeight  = 0.33f;         // prone body height, fraction of standing
    int   stanceGamepadButton = 1;      // tap = crouch / stand, hold = prone (-1 = none; 1 = B)
    float proneHoldTime = 0.4f;         // seconds the gamepad button is held to go prone
    float standEyeHeight  = 1.6f;       // child-camera local Y while standing
    float crouchEyeHeight = 1.2f;       // Minecraft-style: a modest dip, not a deep squat
    float proneEyeHeight  = 0.4f;
    float stanceLerp = 12.0f;           // how fast the eye height eases between stances

    enum class Stance { Stand, Crouch, Prone };
    Stance stance() const { return m_stance; }
    /// The resized body (standing height, current height). Saving a scene writes
    /// the standing collider, not the crouched / prone one.
    const StanceBody& stanceBody() const { return m_body; }
    /// Begin in `s` (a scene saved while crouched / prone loads that way, so a
    /// player saved under low cover isn't stood up inside it). Applied in Start().
    void RestoreStance(Stance s) { m_stance = m_wantStance = m_padWant = m_prevStance = s; }

    // ---- Lean (peek around corners) ----
    char  leanLeftKey  = 'q';           // 0 = disabled
    char  leanRightKey = 'e';           // 0 = disabled
    float leanAngle  = 14.0f;           // camera roll while leaning (degrees)
    float leanOffset = 0.35f;           // sideways camera shift while leaning (units)
    float leanSpeed  = 10.0f;           // how fast the lean eases in/out
    float lean() const { return m_lean; }   // -1 left .. 0 .. +1 right (eased)

    float yaw = 0.0f, pitch = 0.0f;     // look angles (degrees)

    // ---- Feel ----
    float lookSmoothing = 0.0f;         // look smoothing time (s): the view eases toward the look angles; 0 = raw
    float sprintFovBoost = 8.0f;        // FOV widens this much while sprinting (0 = off)
    float fovLerp = 8.0f;               // how fast the FOV eases
    bool  zoomWithRightMouse = true;    // hold right mouse to zoom / aim down sights
    float zoomFov = 40.0f;
    float zoomSensitivity = 0.55f;      // look sensitivity multiplier while zoomed
    float landDip = 0.12f;              // max camera dip on a hard landing (world units)

    // ---- Slide (sprint, then crouch) ----
    bool  canSlide = true;
    float slideSpeed = 10.5f;           // burst speed at the start of a slide
    float slideTime  = 0.75f;           // seconds until it settles into a crouch
    float slideEyeHeight = 0.9f;

    // ---- Hooks ----
    bool  driveStateMachine = true;     // feed an AnimStateMachine speed/grounded/crouch/... + jump/land triggers
    bool  ikLook = true;                // a Full Body IK on the body looks where the camera aims
    std::function<void(bool left)> onFootstep;   // each step while walking on the ground
    std::function<void()>          onJump;
    std::function<void(float impactSpeed)> onLand;
    int   FootstepCount() const { return m_steps; }
    bool  IsSliding() const { return m_slideT > 0.0f; }
    bool  IsZooming() const { return m_zoom; }
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
        ApplyBodyVisibility();
        if (lockCursor) Cursor::Capture(true);   // hide + lock for mouse-look
        if (footIK) AttachCharacterFootIK(gameObject);
        if (transform) { m_spawn = transform->Position(); m_haveSpawn = true; }
        ApplyRestoredStance();
    }

    // First person: the player's own camera should IGNORE the body (so you don't
    // see the inside of your own head) — but the body must still exist for the
    // Scene view, shadows and other cameras. So instead of hiding the mesh, we
    // tell the child camera to skip this object. showBody=true clears that.
    void ApplyBodyVisibility() {
        if (gameObject)
            if (auto* mr = gameObject->GetComponent<MeshRenderer>()) {
                // Normally keep the baked body mesh enabled (hidden from your own view
                // via the camera's ignore, still seen by other cameras). BUT when the
                // Character is a separated rig, the rig renders the body instead — so
                // leave the baked mesh hidden, or you'd see TWO characters overlapping.
                // Same when a custom model replaced the body (Set Character Model
                // switches the Character off, or to drive-external retargeting):
                // re-enabling it drew the blocky body inside the new model.
                auto* ch = gameObject->GetComponent<Character>();
                mr->enabled = !(ch && (ch->separateParts || !ch->enabled || ch->driveExternal));
            }
        if (Transform* camT = FindCameraChild())
            if (auto* cam = camT->gameObject->GetComponent<Camera>())
                cam->ignoreObject = showBody ? nullptr : gameObject;
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
        if (Game::Paused()) return;   // frozen: no mouse-look, no cursor recapture
        m_lookDt = dt;   // also for a client's ReadLocalInput in authoritative movement
        if (!IsLocallyControlled(gameObject)) return;   // remote proxy: NetworkSync drives it
        ApplyBodyVisibility();
        // Keep the cursor hidden + locked while playing (re-assert if something freed
        // it). Turn lockCursor off to author with a normal pointer / click screen UI.
        if (lockCursor && !Cursor::IsLocked()) Cursor::Capture(true);
        Simulate(ReadLocalInput(), dt);
    }

    /// This frame's keyboard / mouse / pad, with the mouse look applied to yaw and
    /// pitch (absolute angles). The only place the controller reads Input::.
    PlayerInput ReadLocalInput() override {
        PlayerInput in;
        // ---- Mouse look ----
        // Mouse-right turns right and mouse-up looks up. The camera looks down its
        // local -Z, so yaw decreases with rightward mouse movement.
        Vec2 mp = Input::MousePosition();
        bool uiBlocked = Input::UICaptured();        // a modal UI (open inventory/chest) pauses look
        bool zoom = !uiBlocked && Input::GetMouseButton(1);
        in.yaw = yaw; in.pitch = pitch;
        if (m_haveMouse && !uiBlocked) {
            float sens = mouseSensitivity * (zoomWithRightMouse && zoom ? zoomSensitivity : 1.0f);
            float dx = (mp.x - m_lastMouse.x) * sens, dy = (mp.y - m_lastMouse.y) * sens;
            in.yaw   -= dx;
            in.pitch += (invertY ? 1.0f : -1.0f) * dy;
            // Kept here too, so the look adds up where Simulate doesn't run every
            // frame (a client whose host runs its movement).
            yaw = in.yaw;
            pitch = in.pitch = Mathf::Clamp(in.pitch, minPitch, maxPitch);
        }
        // Drop the baseline while a bag is open: the fed mouse switches between locked
        // (accumulated relative) and free (absolute) space, so without this the first
        // frame after closing computes one huge delta and snaps the camera.
        m_lastMouse = mp; m_haveMouse = !uiBlocked;
        if (zoom) in.held |= PlayerInput::AltFire;

        in.move = uiBlocked ? Vec2{0, 0} : Input::AxisWASD();   // freeze while a bag is open
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

    /// Look, stance, lean and move by `in` for `dt` seconds (local play and a
    /// server running this player's inputs). Reads no Input:: globals.
    void Simulate(const PlayerInput& in, float dt) override {
        if (!transform) return;
        yaw   = in.yaw;
        // Clamped when it changes (a script may park the view past the limits, as before).
        if (in.pitch != pitch) pitch = Mathf::Clamp(in.pitch, minPitch, maxPitch);
        m_zoom = zoomWithRightMouse && in.Held(PlayerInput::AltFire);

        // Lean: hold Q/E to peek; the camera rolls and shifts sideways.
        UpdateLean(in, dt);
        float roll = -m_lean * leanAngle;
        if (Transform* cam = FindCameraChild()) {
            transform->localRotation = Quat::Euler(0, yaw, 0);        // body turns
            cam->localRotation       = Quat::Euler(pitch, 0, roll);  // camera tilts + leans (shifts sideways in LateUpdate)
        } else {
            transform->localRotation = Quat::Euler(pitch, yaw, roll); // no child cam: tilt self
        }

        // ---- Stance (crouch / prone) ----
        UpdateStance(in, dt);

        // ---- Movement: forward is where the camera looks (its -Z), flattened. ----
        Vec2 axis = in.move;
        Quat flat = Quat::Euler(0, yaw, 0);
        Vec3 fwd = flat * Vec3{0, 0, -1}, right = flat * Vec3::Right;
        Vec3 dir = fwd * axis.y + right * axis.x;
        float len = std::sqrt(dir.x * dir.x + dir.z * dir.z);
        bool moving = len > 0.01f;
        if (moving) { dir.x /= len; dir.z /= len; }
        // Running only stands up: you can't sprint while crouched or prone.
        bool running = m_stance == Stance::Stand && moving && IsRunHeld(in);
        float speed = m_stance == Stance::Prone  ? proneSpeed
                    : m_stance == Stance::Crouch ? crouchSpeed
                    : (running ? runSpeed : walkSpeed);

        // Ease the child camera to the stance's eye height.
        ApplyEyeHeight(dt);

        auto* rb = gameObject ? gameObject->GetComponent<Rigidbody3D>() : nullptr;

        // Grounded: resting (low vertical speed) — reliable across any ground setup —
        // OR a fresh ground contact. Coyote time + jump buffer make jumping forgiving.
        m_groundContact = Mathf::Max(0.0f, m_groundContact - dt);
        // Heightmap terrain has no collider, so it never fires collision callbacks;
        // Physics3D flags resting-on-terrain on the body instead. Treat it as a real
        // ground contact so the jump counter refills (otherwise you get exactly one
        // jump on terrain, then never again).
        bool terrainGround = rb && rb->groundedOnTerrain;
        bool grounded = (rb && Mathf::Abs(rb->velocity.y) < 0.5f) || m_groundContact > 0.0f || terrainGround;
        // The jump COUNT is what stops endless jumping: it refills only on a real
        // ground contact, so the brief zero-velocity at the jump apex can't hand you
        // another jump. maxJumps allows double (or more) jumps.
        if (m_groundContact > 0.0f || terrainGround) m_jumpsUsed = 0;
        m_grounded = grounded;
        // Slide: crouching while sprinting on the ground launches a slide that
        // decays into a crouch. Jumping out of it stands you up (slide-jump).
        if (canSlide && rb && grounded && m_wasRunning && m_slideT <= 0.0f &&
            m_stance == Stance::Crouch && m_prevStance == Stance::Stand) {
            m_slideT = slideTime;
            m_slideDir = moving ? dir : fwd;
        }
        m_prevStance = m_stance;
        if (m_slideT > 0.0f) {
            m_slideT -= dt;
            if (m_stance != Stance::Crouch) m_slideT = 0.0f;   // stood up / went prone: stop
        }
        m_coyote = grounded ? coyoteTime : Mathf::Max(0.0f, m_coyote - dt);
        if (!grounded && m_coyote <= 0.0f && m_jumpsUsed == 0) m_jumpsUsed = 1;  // ground jump spent
        if (in.Pressed(PlayerInput::Jump)) m_jumpBuf = jumpBufferTime;
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
            if (m_slideT > 0.0f && grounded) {
                // Slide velocity: a burst that eases down to crouch speed.
                float k = Mathf::Clamp(m_slideT / Mathf::Max(0.01f, slideTime), 0.0f, 1.0f);
                float sp = crouchSpeed + (slideSpeed - crouchSpeed) * k * k;
                rb->velocity.x = m_slideDir.x * sp;
                rb->velocity.z = m_slideDir.z * sp;
                if (canJump && m_jumpBuf > 0.0f) {
                    // Jumping out of a slide stands you up — only where there is room
                    // to stand; under low cover the slide just carries on.
                    Scene* sc = gameObject->scene();
                    bool room = !resizeBody || !sc || StanceHasRoom(*sc, gameObject, m_body, StanceHeight(Stance::Stand));
                    if (room) { m_slideT = 0.0f; m_stance = m_wantStance = Stance::Stand; m_prevStance = Stance::Stand; ApplyStanceHeight(gameObject, m_body, StanceHeight(Stance::Stand)); }
                    else m_jumpBuf = 0.0f;
                }
            }
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
        // Stairs: step up onto low obstacles instead of grinding against them.
        if (rb && grounded && moving && gameObject && gameObject->scene())
            TryStepUp(*gameObject->scene(), gameObject, rb, dir, grounded, moving, stepOffset);
        // Moving platforms: ride whatever we stand on (elevators, movers).
        if (gameObject && gameObject->scene())
            RideMovingPlatform(*gameObject->scene(), gameObject, m_platRide, grounded);

        // ---- View bob (footstep sway), landing dip, FOV ----
        float hSpeed = rb ? std::sqrt(rb->velocity.x * rb->velocity.x + rb->velocity.z * rb->velocity.z)
                          : (moving ? speed : 0.0f);
        {
            float impact = 0.0f;
            // Stairs and slopes flicker `grounded` for a frame or two: the bob and
            // the landing dip see it a moment longer, so they don't twitch.
            m_sinceGround = grounded ? 0.0f : m_sinceGround + dt;
            const bool groundish = grounded || m_sinceGround < bobGroundGrace;
            m_landing.SetDt(dt);
            m_landed = m_landing.Step(groundish, rb ? rb->velocity.y : 0.0f, impact);
            if (m_landed) {
                m_dip = Mathf::Min(landDip, impact * 0.012f);
                if (onLand) onLand(impact);
            }
            m_dip *= 1.0f - EaseFactor(9.0f, dt);
            ApplyHeadBob(dt, groundish, hSpeed);
            if (Transform* camT = FindCameraChild()) {
                m_fov.Update(camT->gameObject->GetComponent<Camera>(), running && hSpeed > walkSpeed * 0.9f,
                             sprintFovBoost, m_zoom, zoomFov, fovLerp, dt);
            }
        }
        m_wasRunning = running && hSpeed > walkSpeed * 0.8f;

        // ---- Animation ----
        if (driveAnimation)
            if (Character* ch = FindCharacter()) {
                // State from what the body really does (speed over the ground, in the
                // air or not); the stride follows the speed and the legs turn toward
                // the travel direction, so strafing sidesteps and S backpedals while
                // the chest keeps facing the camera.
                Character::Locomotion lo;
                lo.velocity = rb ? rb->velocity : (moving ? dir * speed : Vec3{0, 0, 0});
                lo.facing   = flat * Vec3{0, 0, -1};
                lo.onGround = !rb || m_groundContact > 0.0f || terrainGround;
                lo.jumped   = m_jumped;
                lo.crouch   = m_stance == Stance::Crouch;
                lo.prone    = m_stance == Stance::Prone;
                lo.sprint   = running;
                lo.walkSpeed = walkSpeed;
                lo.dt = dt;
                ch->DriveLocomotion(lo);
                // The body turns with yaw, so the head only needs to tilt with the
                // look pitch (visible to other players / shadows in first person).
                ch->lookPitch = pitch;
                ch->lookYaw   = 0.0f;
                ch->bodyLean  = m_lean * leanAngle;   // body peeks with the camera
            }
        if (driveStateMachine) {
            AnimLink::Frame f;
            f.speed = hSpeed; f.verticalSpeed = rb ? rb->velocity.y : 0.0f; f.grounded = grounded;
            if (rb) f.velocity = rb->velocity;
            f.crouch = m_stance == Stance::Crouch; f.prone = m_stance == Stance::Prone;
            f.sprint = running; f.slide = m_slideT > 0.0f; f.aim = m_zoom;
            f.jumped = m_jumped; f.landed = m_landed;
            m_anim.Apply(gameObject, f);
        }
        m_jumped = false;
        // Full Body IK: the head (and some chest) follow where the camera looks, so
        // other players and shadows see you look up/down/around.
        if (ikLook)
            if (FullBodyIK* fb = FindInTree<FullBodyIK>(gameObject))
                if (Transform* camT = FindCameraChild()) {
                    fb->lookEnabled = true; fb->lookTarget = nullptr;
                    fb->lookPosition = camT->Position() + (camT->Rotation() * Vec3{0, 0, -1}) * 10.0f;
                }
    }

    /// After physics, the eye. A lift or drop of the body while on the ground (a
    /// step, a curb, a moving platform) is eased out (eyeStepSmoothing) instead of
    /// popping; then the camera's local position is put together once: eye
    /// height + bob - landing dip + that step, and the lean's peek + the sway.
    /// With Look Smoothing on, the view (body yaw, camera pitch) shows the eased
    /// angles; movement and the input sent to a server keep the exact ones.
    /// Camera only: Simulate never reads any of it.
    void LateUpdate(float dt) override {
        if (!transform || !m_haveEyeBase) return;   // the eye starts once this peer has simulated the player
        const float y = transform->Position().y;
        if (m_haveBodyY) {
            const float dy = y - m_lastBodyY;
            if (std::fabs(dy) > 1.0f) m_stepOffsetY = 0.0f;   // a teleport or respawn: no glide
            else if (m_grounded && eyeStepSmoothing > 0.0f) {
                // Not a jump or a fall starting (those move the body with real
                // vertical speed; a step-up or a platform doesn't).
                const auto* rb = gameObject ? gameObject->GetComponent<Rigidbody3D>() : nullptr;
                if (!rb || std::fabs(rb->velocity.y) < 1.0f) m_stepOffsetY -= dy;
            }
        }
        m_lastBodyY = y; m_haveBodyY = true;
        const float lim = Mathf::Max(0.0f, stepOffset) + 0.1f;
        m_stepOffsetY = Mathf::Clamp(m_stepOffsetY, -lim, lim) * (1.0f - EaseFactor(eyeStepSmoothing, dt));
        Transform* cam = FindCameraChild();
        if (!cam) return;
        cam->localPosition.y = m_eyeBase + m_bob - m_dip + m_stepOffsetY;
        cam->localPosition.x = m_lean * leanOffset + m_sway;
        m_smooth.Step(yaw, pitch, lookSmoothing, dt);
        if (lookSmoothing > 0.0f) {
            transform->localRotation = Quat::Euler(0, m_smooth.Yaw(), 0);
            cam->localRotation = Quat::Euler(m_smooth.Pitch(), 0, -m_lean * leanAngle);
        }
    }

    // Grounded detection: a roughly-vertical contact with something below the player.
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

    // Sprint held this frame, honouring the toggle option (tap to latch).
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
            else                                                          m_wantStance = m_padWant;
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

    // Ease the lean toward Q/E input (-1 left .. +1 right).
    void UpdateLean(const PlayerInput& in, float dt) {
        float target = 0.0f;
        if (leanLeftKey  && in.Held(PlayerInput::LeanLeft))  target -= 1.0f;
        if (leanRightKey && in.Held(PlayerInput::LeanRight)) target += 1.0f;
        float t = leanSpeed > 0.0f ? (1.0f - std::exp(-leanSpeed * dt)) : 1.0f;
        m_lean += (target - m_lean) * t;
    }

    // Ease the base eye height toward the active stance's height (the view bob is
    // added on top of this each frame in ApplyHeadBob).
    void ApplyEyeHeight(float dt) {
        Transform* cam = FindCameraChild();
        if (!cam) return;
        if (!m_haveEyeBase) { m_eyeBase = cam->localPosition.y; m_haveEyeBase = true; }
        float target = m_stance == Stance::Prone  ? proneEyeHeight
                     : m_slideT > 0.0f ? slideEyeHeight
                     : m_stance == Stance::Crouch ? crouchEyeHeight : standEyeHeight;
        float t = stanceLerp > 0.0f ? (1.0f - std::exp(-stanceLerp * dt)) : 1.0f;
        m_eyeBase += (target - m_eyeBase) * t;   // the camera is placed in LateUpdate
    }

    // The walking head-bob (and sway), layered on the eye height in LateUpdate.
    // Its size eases in and out (bobBlend) instead of switching formulas, so
    // starting, stopping and a flickering `grounded` never jolt the view; the
    // phase restarts from 0 once it has faded out, so the next start does too.
    void ApplyHeadBob(float dt, bool grounded, float hSpeed) {
        const float ref = Mathf::Max(0.1f, walkSpeed);
        const bool walking = grounded && hSpeed > 0.3f && m_slideT <= 0.0f;
        float want = 0.0f;
        if (walking && headBob) {
            const float run = runSpeed > walkSpeed ? Mathf::Clamp((hSpeed - walkSpeed) / (runSpeed - walkSpeed), 0.0f, 1.0f) : 0.0f;
            want = bobAmount * Mathf::Min(1.0f, hSpeed / ref) * (1.0f + (bobRunScale - 1.0f) * run);
        }
        m_bobAmp += (want - m_bobAmp) * EaseFactor(bobBlend, dt);
        if (walking || m_bobAmp > 1e-4f) {
            const float prev = m_bobPhase;
            m_bobPhase += dt * bobSpeed * Mathf::Max(0.4f, hSpeed / ref);
            // A footstep every half bob cycle (left, right, left...).
            if (walking && std::floor(m_bobPhase / 3.14159265f) != std::floor(prev / 3.14159265f)) {
                ++m_steps;
                if (onFootstep) onFootstep((m_steps & 1) != 0);
            }
        } else {
            m_bobAmp = 0.0f; m_bobPhase = 0.0f;
        }
        const float w = bobAmount > 1e-5f ? m_bobAmp / bobAmount : 0.0f;
        m_bob  = std::sin(m_bobPhase) * m_bobAmp;
        m_sway = std::cos(m_bobPhase * 0.5f) * bobSway * w;
    }

    Vec2 m_lastMouse{0, 0};
    bool m_haveMouse = false;
    float m_lookDt = 0.0f;           // this frame's dt, for the look smoothing in ReadLocalInput
    bool m_runToggled = false;
    Stance m_stance = Stance::Stand;
    Stance m_wantStance = Stance::Stand;   // what the input asks for (m_stance may wait for headroom)
    StanceBody m_body;
    float m_padHeld = 0.0f; bool m_padProned = false;
    Stance m_padWant = Stance::Stand;   // the stance the gamepad button set (hold mode)
    float m_eyeBase = 1.6f;          // eased base eye height (bob layered on top)
    bool  m_haveEyeBase = false;
    float m_bobPhase = 0.0f, m_bob = 0.0f;   // walking view-bob state
    float m_bobAmp = 0.0f, m_sway = 0.0f;    // the bob's eased size, the sideways sway
    float m_sinceGround = 0.0f;              // s since grounded (bobGroundGrace)
    float m_stepOffsetY = 0.0f;              // eye smoothing: the step still being eased out
    float m_lastBodyY = 0.0f; bool m_haveBodyY = false;
    float m_lean = 0.0f;             // eased lean amount (-1..+1)
    int   m_jumpsUsed = 0;          // jumps since last grounded (for double-jump)
    LookSmoother m_smooth;
    FovKick m_fov;
    Landing m_landing;
    AnimLink m_anim;
    bool  m_zoom = false, m_grounded = true, m_wasRunning = false, m_jumped = false, m_landed = false;
    Stance m_prevStance = Stance::Stand;
    float m_slideT = 0.0f;
    Vec3  m_slideDir{0, 0, -1};
    float m_dip = 0.0f;
    int   m_steps = 0;
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

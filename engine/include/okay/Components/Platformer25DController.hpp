#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/DeathLock.hpp"
#include "okay/Animation/HumanoidActions.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Components/StepUp.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Net/NetOwnership.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Math/Mathf.hpp"
#include <cmath>

namespace okay {

/// 2.5D PLATFORMER controller in the style of LittleBigPlanet: a 3D world seen from
/// the side, where you run left / right and jump, and hop between a few DEPTH
/// LAYERS (front, middle, back) with W / S (stick or d-pad up / down). A layer
/// switch only happens when there's room on that layer, and slides back if
/// something blocks the way. Hold the grab key (E, or RB) to grab: drag / push a
/// physics object, or catch a ledge in the air and hang from it; then W / up
/// climbs onto it, S / down lets go, jump jumps off. Floaty jump with a variable
/// height (let go early for a hop), coyote time and a jump buffer; low steps are
/// walked up; walls never hold you up (pressing into one doesn't stick). The side
/// camera looks ahead and stays on the level, not on the layer, so switching
/// layers doesn't swing the view. Ctrl crouches and Z goes prone (crawl); the
/// body shrinks so you fit under low things, and won't stand up beneath them.
/// No running crouched or prone, no jumping either, and no layer hops or grabs
/// while prone. Gamepad: stick runs (push it all the way to run), A jumps, stick /
/// d-pad up and down switch layers, RB grabs, B crouches (hold it to go prone).
///
/// Script events on the player's script: on_jump, on_land, on_layer, on_grab,
/// on_release, on_climb. Layer 0 is the front (nearest the camera).
class Platformer25DController : public Behaviour {
public:
    /// Runs before the animation components (Character, ModelAnimator,
    /// AnimStateMachine at 0, IK at 100), so they pose this frame's movement and
    /// state changes, not last frame's.
    int ExecutionOrder() const override { return -10; }
    // ---- Run ----
    float walkSpeed = 5.0f;
    float runSpeed = 7.5f;
    char  sprintKey = Input::KeyShift;
    float acceleration = 45.0f, deceleration = 50.0f;
    float airControl = 0.65f;
    float stepHeight = 0.35f;           ///< walk up ledges this high without jumping
    float stickRunThreshold = 0.9f;     ///< gamepad: push the stick this far to run
    bool  driveAnimation = true;

    // ---- Jump ----
    bool  canJump = true;
    char  jumpKey = ' ';
    int   jumpGamepadButton = 0;        ///< A (-1 = none)
    float jumpForce = 8.5f;
    int   maxJumps = 1;                 ///< 2 = double jump
    float jumpCut = 0.5f;               ///< upward speed kept when you let go early (1 = fixed height)
    float fallGravity = 1.6f;           ///< extra gravity while falling (snappier landings)
    float maxFallSpeed = 22.0f;
    float coyoteTime = 0.12f, jumpBufferTime = 0.12f;

    // ---- Stance: crouch & prone ----
    char  crouchKey = Input::KeyCtrl;   ///< 0 = disabled
    char  proneKey = 'z';               ///< 0 = disabled
    bool  toggleStance = true;          ///< tap to toggle vs hold
    float crouchSpeed = 2.2f;
    float proneSpeed = 1.1f;
    bool  allowProne = true;            ///< false: no prone at all (key and gamepad)
    bool  resizeBody = true;            ///< crouch / prone shrink the collider (fit under low things; no standing into a ceiling)
    float crouchHeight = 0.65f;         ///< crouched body height, fraction of standing
    float proneHeight = 0.33f;          ///< prone body height, fraction of standing
    int   stanceGamepadButton = 1;      ///< B: tap = crouch / stand, hold = prone (-1 = none)
    float proneHoldTime = 0.4f;         ///< seconds the gamepad button is held to go prone

    enum class Stance { Stand, Crouch, Prone };
    Stance stance() const { return m_stance; }
    /// The resized body; saving a scene writes the standing collider.
    const StanceBody& stanceBody() const { return m_body; }
    /// Begin in `s` (a scene saved crouched / prone loads that way). Applied in Start().
    /// Ask for a stance from a script / an NPC / a cutscene (as if pressed): kept
    /// until changed, so a stand-up blocked by a low ceiling happens once there's room.
    void SetStance(Stance s) { m_wantStance = m_padWant = s; }
    void RestoreStance(Stance s) { m_stance = m_wantStance = m_padWant = s; m_stanceLevel = (float)s; }
    /// The body's stance as it moves between stances: 0 stand .. 1 crouch .. 2 prone.
    float stanceLevel() const { return m_stanceLevel; }

    // ---- Depth layers ----
    int   layerCount = 3;               ///< LittleBigPlanet uses 3
    float layerSpacing = 1.5f;          ///< distance between layers (world Z)
    float frontLayerZ = 0.0f;           ///< world Z of layer 0; deeper layers are further from the camera
    int   layer = 0;                    ///< current layer (the starting layer when the scene starts)
    char  layerBackKey = 'w';
    char  layerFrontKey = 's';
    float layerSwitchSpeed = 12.0f;     ///< how quickly you slide between layers
    bool  checkLayerFree = true;        ///< only switch when the other layer has room
    float bodyRadius = 0.35f;           ///< for the room check

    // ---- Facing ----
    float turnSpeed = 14.0f;
    float faceCameraAngle = 25.0f;      ///< turn this far toward the camera so the face shows

    // ---- Grab ----
    bool  canGrab = true;
    char  grabKey = 'e';
    int   grabGamepadButton = 7;        ///< RB
    float grabReach = 0.9f;
    float grabPullSpeed = 0.65f;        ///< speed multiplier while dragging something
    bool  grabHang = true;              ///< catch a ledge (the top edge of something solid) in the air and hang
    bool  ledgeClimb = true;            ///< W / up while hanging climbs onto the ledge
    float climbTime = 0.35f;            ///< seconds to pull yourself up

    // ---- Camera (side view) ----
    float cameraDistance = 10.0f;
    float cameraHeight = 1.8f;
    float cameraPitch = 8.0f;           ///< look down a little (deg)
    float lookAhead = 1.8f;             ///< lead in the direction you're running
    float cameraDamping = 6.0f;
    float verticalDamping = 3.0f;       ///< slower up / down follow, so jumps don't jerk the view
    float speedZoom = 0.25f;            ///< pull back this much per unit of speed
    bool  cameraFollowsLayer = false;   ///< off: the camera stays on the middle of the layers

    // ---- Runtime (read by scripts / HUD) ----
    bool  IsGrounded() const { return m_grounded; }
    bool  IsGrabbing() const { return m_grabbed != nullptr; }
    bool  IsHanging() const { return m_hanging; }
    bool  IsClimbing() const { return m_climbT >= 0.0f; }
    bool  FacingRight() const { return m_facing > 0.0f; }
    GameObject* Grabbed() const { return m_grabbed; }
    float LayerZ(int i) const { return frontLayerZ - (float)Mathf::Clamp(i, 0, Mathf::Max(1, layerCount) - 1) * layerSpacing; }
    float MiddleZ() const { return frontLayerZ - (float)(Mathf::Max(1, layerCount) - 1) * layerSpacing * 0.5f; }

    void Start() override {
        layer = Mathf::Clamp(layer, 0, Mathf::Max(1, layerCount) - 1);
        m_prevLayer = layer;
        if (transform) {
            Vec3 p = transform->Position(); p.z = LayerZ(layer);
            transform->SetPosition(p);
        }
        m_stanceLevel = (float)m_stance;
        if (resizeBody && gameObject && m_stance != Stance::Stand) {   // restored crouched / prone
            CaptureStance(gameObject, m_body);
            ApplyStanceHeight(gameObject, m_body, StanceHeight(m_stance));
        }
    }

    /// Move to another layer if it exists (and has room; not while prone). Returns true on success.
    bool SwitchLayer(int to) {
        if (m_stance == Stance::Prone || to < 0 || to >= Mathf::Max(1, layerCount) || to == layer || !transform || !gameObject) return false;
        if (checkLayerFree && !LayerFree(to)) return false;
        m_prevLayer = layer;
        m_layerStall = 0.0f;
        layer = to;
        CallScript("on_layer");
        return true;
    }

    /// Whether the player would fit on layer `to` at its current height.
    bool LayerFree(int to) const {
        Scene* sc = gameObject ? gameObject->scene() : nullptr;
        if (!sc || !transform) return true;
        Vec3 p = transform->Position();
        const float bodyH = BodyHeight();
        const float heights[] = {Mathf::Max(bodyRadius, stepHeight + 0.05f + bodyRadius * 0.5f), bodyH * 0.75f};
        for (float h : heights) {
            Vec3 c{p.x, p.y + h, LayerZ(to)};
            for (Collider3D* col : sc->physics3D().OverlapSphere(*sc, c, bodyRadius, Physics3D::kAllLayers, false))
                if (col && !col->isTrigger && col->gameObject && col->gameObject != gameObject &&
                    col->gameObject != m_grabbed) return false;
        }
        return true;
    }

    void Update(float dt) override {
        if (!transform || !gameObject) return;
        if (Game::Paused()) return;
        if (!IsLocallyControlled(gameObject)) return;
        if (DeathLocksInput(gameObject)) { StopDeadSlide(gameObject); return; }
        Scene* sc = gameObject->scene();
        const bool ui = Input::UICaptured();
        auto* rb = gameObject->GetComponent<Rigidbody3D>();

        // ---- Input (keyboard, or the gamepad's stick / d-pad / buttons) ----
        Vec2 axis = ui ? Vec2{0, 0} : Input::AxisWASD();
        Vec2 pad = ui ? Vec2{0, 0} : Input::GamepadAxis();
        const bool padMove = Mathf::Abs(pad.x) > 0.15f;
        if (padMove) axis.x = pad.x;
        float mx = Mathf::Clamp(axis.x, -1.0f, 1.0f);
        bool moving = Mathf::Abs(mx) > 0.05f;
        // Up / down: W / S, or the stick pushed past half way, or the d-pad (one step per push).
        const bool padUp = !ui && pad.y > 0.6f && m_padY <= 0.6f;
        const bool padDown = !ui && pad.y < -0.6f && m_padY >= -0.6f;
        m_padY = ui ? 0.0f : pad.y;
        const bool upPressed = !ui && ((layerBackKey && Input::GetKeyDown(layerBackKey)) || padUp || Input::GetGamepadButtonDown(8));
        const bool downPressed = !ui && ((layerFrontKey && Input::GetKeyDown(layerFrontKey)) || padDown || Input::GetGamepadButtonDown(9));
        const bool jumpDown = !ui && ((jumpKey && Input::GetKeyDown(jumpKey)) ||
                                      (jumpGamepadButton >= 0 && Input::GetGamepadButtonDown(jumpGamepadButton)));
        const bool jumpHeld = !ui && ((jumpKey && Input::GetKey(jumpKey)) ||
                                      (jumpGamepadButton >= 0 && Input::GetGamepadButton(jumpGamepadButton)));

        // ---- Climbing onto a ledge: a short scripted move, then back to normal ----
        if (m_climbT >= 0.0f) {
            UpdateClimb(dt, rb);
            if (driveAnimation) DriveAnimation(dt, false);
            return;
        }

        // ---- Ground ----
        bool wasGrounded = m_grounded;
        m_groundContact = Mathf::Max(0.0f, m_groundContact - dt);
        bool onTerrain = rb && rb->groundedOnTerrain;
        m_grounded = m_groundContact > 0.0f || onTerrain || ProbeGround(sc);
        if (m_grounded && rb && rb->velocity.y > 0.5f && m_jumpedAt < 0.15f) m_grounded = false;   // just took off
        m_jumpedAt += dt;
        if (m_grounded) { m_jumpsUsed = 0; m_coyote = coyoteTime; }
        else m_coyote = Mathf::Max(0.0f, m_coyote - dt);
        if (!m_grounded && m_coyote <= 0.0f && m_jumpsUsed == 0) m_jumpsUsed = 1;
        if (m_grounded && !wasGrounded) CallScript("on_land");
        if (!m_hanging) UpdateStance(dt, ui);

        // ---- Hanging from a ledge: up climbs, down drops, jump jumps off ----
        if (m_hanging) {
            if (upPressed && ledgeClimb && StartClimb(sc, rb)) { if (driveAnimation) DriveAnimation(dt, false); return; }
            if (downPressed) { Release(rb); m_noLedge = 0.4f; }   // don't catch it again on the way down
        } else if (!ui) {
            // ---- Layers ----
            if (upPressed) SwitchLayer(layer + 1);
            else if (downPressed) SwitchLayer(layer - 1);
        }

        // ---- Grab: objects on the ground (once per press); ledges in the air (while held) ----
        bool grabHeld = !ui && canGrab && m_stance != Stance::Prone && ((grabKey && Input::GetKey(grabKey)) ||
                                           (grabGamepadButton >= 0 && Input::GetGamepadButton(grabGamepadButton)));
        m_noLedge = Mathf::Max(0.0f, m_noLedge - dt);
        if (grabHeld && !m_grabbed && !m_hanging && (!m_grabTried || !m_grounded)) { TryGrab(sc, rb); m_grabTried = true; }
        if (!grabHeld) { m_grabTried = false; if (m_grabbed || m_hanging) Release(rb); }
        if (m_grabbed && !StillInScene(sc, m_grabbed)) Release(rb);

        // ---- Facing (locked toward a grabbed object or the ledge) ----
        if (moving && !m_grabbed && !m_hanging) m_facing = mx > 0.0f ? 1.0f : -1.0f;
        {
            float a = faceCameraAngle * Mathf::Deg2Rad;
            Vec3 look{m_facing * std::cos(a), 0.0f, std::sin(a)};
            Quat want = Quat::LookRotation({-look.x, 0.0f, -look.z});   // mesh faces -Z
            float t = turnSpeed > 0.0f ? 1.0f - std::exp(-turnSpeed * dt) : 1.0f;
            transform->localRotation = Quat::Slerp(transform->localRotation, want, t);
        }

        // ---- Jump input ----
        if (jumpDown) m_jumpBuf = jumpBufferTime;
        else m_jumpBuf = Mathf::Max(0.0f, m_jumpBuf - dt);

        const bool padRun = padMove && Mathf::Abs(pad.x) >= stickRunThreshold;
        bool running = m_stance == Stance::Stand && ((sprintKey && Input::GetKey(sprintKey)) || padRun) && moving && !m_grabbed;
        // An analog stick walks at the speed it's pushed; keys walk at full speed.
        // The speed cap follows the body from one stance's to the next.
        float speed = StanceValue(m_stanceLevel, running ? runSpeed : walkSpeed, crouchSpeed, proneSpeed) *
                      (m_grabbed ? grabPullSpeed : 1.0f);
        const float targetZ = LayerZ(layer);

        if (rb) {
            if (m_hanging) {
                rb->velocity = Vec3{0, 0, 0};
                if (m_jumpBuf > 0.0f && canJump) {          // jump off the ledge
                    Release(rb);
                    m_noLedge = 0.25f;
                    DoJump(rb);
                }
            } else {
                float cur = rb->velocity.x, wantX = mx * speed;
                float rate = (moving ? acceleration : deceleration) * (m_grounded ? 1.0f : airControl);
                float dv = Mathf::Clamp(wantX - cur, -rate * dt, rate * dt);
                rb->velocity.x = cur + dv;

                // Low ledges are walked up; a wall in the way takes the push (so
                // friction against it can't hold you up in the air).
                if (Mathf::Abs(rb->velocity.x) > 0.01f && !m_grabbed) {
                    const float dir = rb->velocity.x > 0.0f ? 1.0f : -1.0f;
                    if (!(m_grounded || m_coyote > 0.0f) || !TryStepUp(sc, rb, dir))
                        if (WallAhead(sc, dir)) rb->velocity.x = 0.0f;
                }

                if (canJump && m_jumpBuf > 0.0f && m_stance == Stance::Stand) {   // no jumping while crouched / prone
                    bool firstOk = m_jumpsUsed == 0 && (m_grounded || m_coyote > 0.0f);
                    bool extraOk = m_jumpsUsed >= 1 && m_jumpsUsed < Mathf::Max(1, maxJumps);
                    if (firstOk || extraOk) DoJump(rb);
                }
                // Variable height: letting go on the way up cuts the jump.
                if (!jumpHeld && m_jumpCuttable && rb->velocity.y > 0.0f) { rb->velocity.y *= jumpCut; m_jumpCuttable = false; }
                if (rb->velocity.y <= 0.0f) m_jumpCuttable = false;
                if (sc && rb->velocity.y < 0.0f && fallGravity > 1.0f)
                    rb->velocity.y += sc->physics3D().gravity.y * rb->gravityScale * (fallGravity - 1.0f) * dt;
                if (maxFallSpeed > 0.0f && rb->velocity.y < -maxFallSpeed) rb->velocity.y = -maxFallSpeed;

                // Slide toward the layer's depth (velocity, so walls still block it).
                // Stuck on the way (something moved into the gap)? Go back.
                const float z = transform->Position().z;
                float dz = targetZ - z;
                if (Mathf::Abs(dz) > 0.02f && Mathf::Abs(z - m_lastZ) < 0.002f) m_layerStall += dt;
                else m_layerStall = 0.0f;
                m_lastZ = z;
                if (m_layerStall > 0.15f && m_prevLayer != layer) {
                    layer = m_prevLayer;
                    m_layerStall = 0.0f;
                    CallScript("on_layer");
                    dz = LayerZ(layer) - z;
                }
                rb->velocity.z = Mathf::Abs(dz) < 0.005f ? 0.0f : Mathf::Clamp(dz * layerSwitchSpeed, -12.0f, 12.0f);

                // Drag what we hold along with us (and onto our layer).
                if (m_grabbed)
                    if (auto* orb = m_grabbed->GetComponent<Rigidbody3D>()) {
                        float wantObjX = transform->Position().x + m_grabOffset;
                        orb->velocity.x = rb->velocity.x + (wantObjX - m_grabbed->transform->Position().x) * 8.0f;
                        orb->velocity.z = (z - m_grabbed->transform->Position().z) * 6.0f;
                        orb->WakeUp();
                    }
            }
            rb->WakeUp();
        } else {
            Vec3 p = transform->Position();
            p.x += mx * speed * dt;
            float t = 1.0f - std::exp(-layerSwitchSpeed * dt);
            p.z += (targetZ - p.z) * t;
            transform->SetPosition(p);
        }

        if (driveAnimation) DriveAnimation(dt, running);
    }

    void LateUpdate(float dt) override {
        if (!transform || !gameObject) return;
        Scene* sc = gameObject->scene();
        if (!sc || !sc->mainCamera || !sc->mainCamera->transform) return;
        if (!IsLocallyControlled(gameObject)) return;
        Transform* cam = sc->mainCamera->transform;

        Vec3 p = transform->Position();
        auto* rb = gameObject->GetComponent<Rigidbody3D>();
        float vx = rb ? rb->velocity.x : 0.0f;
        // Lead toward where you're running; ease the lead so turning doesn't snap.
        float lt = 1.0f - std::exp(-3.0f * dt);
        m_lead += (m_facing * lookAhead - m_lead) * lt;
        float baseZ = cameraFollowsLayer ? p.z : MiddleZ();
        float dist = cameraDistance + Mathf::Abs(vx) * speedZoom;
        float pr = cameraPitch * Mathf::Deg2Rad;
        Vec3 desired{p.x + m_lead, p.y + cameraHeight + std::sin(pr) * dist, baseZ + std::cos(pr) * dist};

        if (!m_haveCam) { m_camPos = desired; m_haveCam = true; }
        else {
            float th = cameraDamping > 0.0f ? 1.0f - std::exp(-cameraDamping * dt) : 1.0f;
            float tv = verticalDamping > 0.0f ? 1.0f - std::exp(-verticalDamping * dt) : 1.0f;
            m_camPos.x += (desired.x - m_camPos.x) * th;
            m_camPos.z += (desired.z - m_camPos.z) * th;
            m_camPos.y += (desired.y - m_camPos.y) * tv;
        }
        cam->SetPosition(m_camPos);
        cam->localRotation = Quat::Euler(-cameraPitch, 0.0f, 0.0f);
    }

    void OnCollisionEnter3D(const Collision3D& c) override { NoteGround(c); }
    void OnCollisionStay3D(const Collision3D& c) override { NoteGround(c); }
    void OnDestroy() override { if (m_hanging || m_climbT >= 0.0f) Release(gameObject ? gameObject->GetComponent<Rigidbody3D>() : nullptr); }

private:
    float m_facing = 1.0f, m_lead = 0.0f;
    float m_groundContact = 0.0f, m_coyote = 0.0f, m_jumpBuf = 0.0f, m_jumpedAt = 1.0f;
    int   m_jumpsUsed = 0;
    bool  m_grounded = false, m_jumpCuttable = false, m_haveCam = false;
    bool  m_hanging = false, m_grabTried = false;
    float m_savedGravity = 1.0f, m_grabOffset = 0.0f;
    GameObject* m_grabbed = nullptr;
    Vec3  m_camPos{0, 0, 0};
    float m_padY = 0.0f;                       // last stick y (for one layer step per push)
    int   m_prevLayer = 0;                     // where a blocked layer switch goes back to
    float m_layerStall = 0.0f, m_lastZ = 0.0f;
    float m_ledgeY = 0.0f;                     // top of the ledge we hang from
    float m_noLedge = 0.0f;                    // after dropping / jumping off: don't re-catch yet
    float m_climbT = -1.0f;                    // >= 0 while climbing onto a ledge
    Vec3  m_climbFrom{0, 0, 0}, m_climbTo{0, 0, 0};
    Stance m_stance = Stance::Stand;
    Stance m_wantStance = Stance::Stand;      // what the input asks for (m_stance may wait for headroom)
    float  m_stanceLevel = 0.0f;             // the body on its way between stances (0 stand .. 2 prone)
    Stance m_padWant = Stance::Stand;         // the stance the gamepad button set (hold mode)
    StanceBody m_body;
    float m_padHeld = 0.0f; bool m_padProned = false;

    // Resolve the stance from the keys / gamepad, then size the body for it.
    // Toggle mode remembers what you asked for, so a stand-up blocked by a low
    // ceiling happens on its own once you're out from under it.
    void UpdateStance(float dt, bool ui) {
        // (Typing into UI keeps the stance as it is.)
        if (!ui && toggleStance) {
            if (crouchKey && Input::GetKeyDown(crouchKey))
                m_wantStance = (m_wantStance == Stance::Crouch) ? Stance::Stand : Stance::Crouch;
            if (allowProne && proneKey && Input::GetKeyDown(proneKey))
                m_wantStance = (m_wantStance == Stance::Prone) ? Stance::Stand : Stance::Prone;
        } else if (!ui) {
            // Held keys win; with none held you're in the stance the gamepad last set.
            if (allowProne && proneKey && Input::GetKey(proneKey))  { m_wantStance = Stance::Prone;  m_padWant = Stance::Stand; }
            else if (crouchKey && Input::GetKey(crouchKey))         { m_wantStance = Stance::Crouch; m_padWant = Stance::Stand; }
            else                                                      m_wantStance = m_padWant;
        }
        // Gamepad: tap the stance button to crouch / stand up, hold it to drop prone.
        if (stanceGamepadButton >= 0 && !ui) {
            if (Input::GetGamepadButton(stanceGamepadButton)) {
                m_padHeld += dt;
                if (allowProne && !m_padProned && m_padHeld >= proneHoldTime) { m_wantStance = m_padWant = Stance::Prone; m_padProned = true; }
            } else if (m_padHeld > 0.0f) {
                if (!m_padProned) m_wantStance = m_padWant = (m_stance == Stance::Stand) ? Stance::Crouch : Stance::Stand;
                m_padHeld = 0.0f; m_padProned = false;
            }
        }
        if (!allowProne && m_wantStance == Stance::Prone) m_wantStance = Stance::Stand;
        // Rising needs headroom for the target height; then the body (and its
        // collider) eases there over the stance change time.
        m_stance = resizeBody && gameObject
            ? (Stance)SettleStance(gameObject, m_body, (int)m_stance, (int)m_wantStance, crouchHeight, proneHeight)
            : m_wantStance;
        EaseStance(gameObject, m_body, m_stanceLevel, (int)m_stance, dt, StanceTimesOf(gameObject),
                   resizeBody, crouchHeight, proneHeight);
    }
    /// Collider height for a stance (crouch / prone are fractions of the standing body).
    float StanceHeight(Stance s) const {
        float h = m_body.standH;
        return s == Stance::Prone ? h * proneHeight : (s == Stance::Crouch ? h * crouchHeight : h);
    }

    // The body's half width (along X) and height, from its collider.
    float BodyHalfWidth() const {
        if (gameObject) {
            if (auto* b = gameObject->GetComponent<BoxCollider3D>()) return b->HalfExtents().x;
            if (auto* c = gameObject->GetComponent<CapsuleCollider3D>()) return c->WorldRadius();
        }
        return bodyRadius;
    }
    float BodyHeight() const {
        if (gameObject) {
            if (auto* b = gameObject->GetComponent<BoxCollider3D>()) return b->HalfExtents().y * 2.0f;
            if (auto* c = gameObject->GetComponent<CapsuleCollider3D>()) {
                const Vec3 sc = transform ? transform->LossyScale() : Vec3::One;
                return Mathf::Max(c->height * Mathf::Abs(sc.y), c->WorldRadius() * 2.0f);
            }
        }
        return 1.8f;
    }
    RaycastHit3D Ray(Scene* sc, const Vec3& from, const Vec3& dir, float len) const {
        // Solid things only: trigger volumes (pickups, checkpoints) are not ground
        // or walls, and neither are the player's own parts (a held item, a model's pieces).
        Physics3D::IgnoreHierarchy own(gameObject);
        return sc->physics3D().Raycast(*sc, from, dir, len, gameObject, Physics3D::kAllLayers, false);
    }
    bool Solid(const RaycastHit3D& h) const {
        if (!h.hit || !h.gameObject) return h.hit;
        if (h.gameObject == m_grabbed) return false;
        auto* orb = h.gameObject->GetComponent<Rigidbody3D>();
        return !orb || orb->bodyType != Rigidbody3D::BodyType::Dynamic;   // dynamic things get pushed instead
    }

    void DoJump(Rigidbody3D* rb) {
        rb->velocity.y = jumpForce;
        ++m_jumpsUsed;
        m_jumpBuf = 0.0f; m_coyote = 0.0f; m_groundContact = 0.0f; m_jumpedAt = 0.0f;
        m_grounded = false;
        m_jumpCuttable = jumpCut < 1.0f;
        CallScript("on_jump");
    }

    bool ProbeGround(Scene* sc) const {
        if (!sc || !transform) return false;
        const Vec3 o = transform->Position();
        const float hw = BodyHalfWidth() * 0.85f;
        for (float dx : {0.0f, -hw, hw}) {   // middle and both sides, so a heel on an edge still counts
            RaycastHit3D h = Ray(sc, Vec3{o.x + dx, o.y + 0.15f, o.z}, Vec3{0, -1, 0}, 0.25f);
            if (h.hit && h.normal.y > 0.5f) return true;
        }
        return false;
    }

    // Something solid right in front (at knee, chest and head height).
    bool WallAhead(Scene* sc, float dir) const {
        if (!sc || !transform) return false;
        const Vec3 o = transform->Position();
        const float bodyH = BodyHeight(), reach = BodyHalfWidth() + 0.15f;   // contacts settle a little off the face
        for (float h : {stepHeight + 0.05f, bodyH * 0.5f, bodyH - 0.1f}) {
            RaycastHit3D hit = Ray(sc, Vec3{o.x, o.y + h, o.z}, Vec3{dir, 0, 0}, reach);
            if (hit.hit && Mathf::Abs(hit.normal.x) > 0.6f && Solid(hit)) return true;
        }
        return false;
    }

    // A ledge no higher than stepHeight in front of the feet, with room above:
    // lift onto it. Returns true if we stepped (or are clear to keep walking).
    bool TryStepUp(Scene* sc, Rigidbody3D* rb, float dir) {
        if (!sc || !transform || stepHeight <= 0.0f) return false;
        const Vec3 o = transform->Position();
        const float hw = BodyHalfWidth(), reach = hw + 0.15f;
        RaycastHit3D foot = Ray(sc, Vec3{o.x, o.y + 0.04f, o.z}, Vec3{dir, 0, 0}, reach);
        if (!foot.hit || Mathf::Abs(foot.normal.x) < 0.6f || !Solid(foot)) return false;
        RaycastHit3D knee = Ray(sc, Vec3{o.x, o.y + stepHeight + 0.05f, o.z}, Vec3{dir, 0, 0}, reach);
        if (knee.hit && Solid(knee)) return false;   // taller than a step: it's a wall
        // How high is the top, just past the face?
        const float probeX = foot.point.x + dir * 0.08f;
        RaycastHit3D top = Ray(sc, Vec3{probeX, o.y + stepHeight + 0.1f, o.z}, Vec3{0, -1, 0}, stepHeight + 0.12f);
        if (!top.hit || top.normal.y < 0.7f) return false;
        const float rise = top.point.y - o.y;
        if (rise < kMinStepHeight || rise > stepHeight + 0.01f) return false;   // lower is a seam, not a step
        // Room for the body up there?
        const float bodyH = BodyHeight();
        const Vec3 c{o.x + dir * 0.1f, top.point.y + bodyH * 0.5f + 0.02f, o.z};
        for (Collider3D* col : sc->physics3D().OverlapBox(*sc, c, Vec3{hw * 0.9f, bodyH * 0.5f - 0.03f, hw * 0.9f}, Quat::Identity,
                                                          Physics3D::kAllLayers, false, gameObject))
            if (col && col->gameObject && col->gameObject != m_grabbed) return false;
        transform->SetPosition(Vec3{o.x, top.point.y + 0.01f, o.z});
        if (rb && rb->velocity.y < 0.0f) rb->velocity.y = 0.0f;
        m_groundContact = 0.1f;
        return true;
    }

    // The top edge of something solid in front of us that our hands can reach:
    // its height and the x of its face.
    bool FindLedge(Scene* sc, float& ledgeY, float& faceX) const {
        if (!sc || !transform) return false;
        const Vec3 o = transform->Position();
        const float hw = BodyHalfWidth(), bodyH = BodyHeight();
        const float probeX = o.x + m_facing * (hw + 0.25f);
        const float fromY = o.y + bodyH + 0.6f;
        RaycastHit3D top = Ray(sc, Vec3{probeX, fromY, o.z}, Vec3{0, -1, 0}, bodyH);
        if (!top.hit || top.normal.y < 0.7f || !Solid(top)) return false;
        const float y = top.point.y;
        if (y < o.y + bodyH * 0.5f || y > o.y + bodyH + 0.55f) return false;   // out of the hands' reach
        // Its face, just under the edge.
        RaycastHit3D face = Ray(sc, Vec3{o.x, y - 0.08f, o.z}, Vec3{m_facing, 0, 0}, hw + 0.45f);
        if (!face.hit || Mathf::Abs(face.normal.x) < 0.6f) return false;
        ledgeY = y;
        faceX = face.point.x;
        return true;
    }

    // Pull up onto the ledge we hang from, if the body fits on top.
    bool StartClimb(Scene* sc, Rigidbody3D* rb) {
        if (!sc || !transform) return false;
        const Vec3 o = transform->Position();
        const float hw = BodyHalfWidth(), bodyH = BodyHeight();
        const Vec3 to{o.x + m_facing * (hw * 2.0f + 0.12f), m_ledgeY + 0.02f, o.z};
        const Vec3 c{to.x, to.y + bodyH * 0.5f + 0.02f, to.z};
        for (Collider3D* col : sc->physics3D().OverlapBox(*sc, c, Vec3{hw * 0.95f, bodyH * 0.5f - 0.03f, hw * 0.95f}, Quat::Identity,
                                                          Physics3D::kAllLayers, false, gameObject))
            if (col && col->gameObject) return false;   // no room: stay hanging
        m_climbFrom = o;
        m_climbTo = to;
        m_climbT = 0.0f;
        m_hanging = false;   // gravity stays off until the climb ends
        HumanoidActions::SetHanging(gameObject, false);
        if (rb) rb->velocity = Vec3{0, 0, 0};
        CallScript("on_climb");
        return true;
    }

    // Up the face first, then over the edge.
    void UpdateClimb(float dt, Rigidbody3D* rb) {
        m_climbT += dt / Mathf::Max(0.05f, climbTime);
        const float t = Mathf::Min(1.0f, m_climbT);
        const float up = Mathf::Min(1.0f, t / 0.6f), over = Mathf::Clamp((t - 0.45f) / 0.55f, 0.0f, 1.0f);
        const float su = up * up * (3.0f - 2.0f * up), so = over * over * (3.0f - 2.0f * over);
        transform->SetPosition(Vec3{m_climbFrom.x + (m_climbTo.x - m_climbFrom.x) * so,
                                    m_climbFrom.y + (m_climbTo.y - m_climbFrom.y) * su, m_climbFrom.z});
        if (rb) rb->velocity = Vec3{0, 0, 0};
        if (t >= 1.0f) {
            m_climbT = -1.0f;
            if (rb) rb->gravityScale = m_savedGravity;
            m_grounded = true; m_groundContact = 0.1f; m_jumpsUsed = 0;
            CallScript("on_land");
        }
    }

    // Idle / walk / run at the real speed (the stride matches the ground), jump /
    // fall in the air, crouch / crawl, arms up while hanging, a scramble while climbing.
    void DriveAnimation(float dt, bool running) {
        Character* ch = FindCharacter();
        if (!ch) return;
        if (m_hanging) { ch->anim = 19; ch->moveSpeed = 0.0f; return; }
        if (m_climbT >= 0.0f) { ch->anim = 5; ch->moveSpeed = 0.0f; return; }
        Character::Locomotion lo;
        if (auto* rb = gameObject->GetComponent<Rigidbody3D>()) lo.velocity = {rb->velocity.x, rb->velocity.y, 0.0f};
        lo.facing = transform ? transform->localRotation * Vec3{0, 0, -1} : Vec3{m_facing, 0, 0};
        lo.onGround = m_grounded;
        lo.jumped = m_jumpedAt <= 0.0f;                  // DoJump ran this frame
        lo.ignoreAxis = {0.0f, 0.0f, 1.0f};              // layer hops aren't steps
        lo.crouch = m_stance == Stance::Crouch;
        lo.prone = m_stance == Stance::Prone;
        lo.stance = m_stanceLevel;
        lo.sprint = running;
        lo.walkSpeed = walkSpeed;
        lo.dt = dt;
        ch->DriveLocomotion(lo);
    }

    void TryGrab(Scene* sc, Rigidbody3D* rb) {
        if (!sc || !transform) return;
        Vec3 o = transform->Position();
        Vec3 dir{m_facing, 0.0f, 0.0f};
        const float bodyH = BodyHeight();
        const float heights[] = {bodyH * 0.5f, bodyH * 0.89f, bodyH * 0.22f};
        for (float h : heights) {   // something loose within reach: hold it
            RaycastHit3D hit = Ray(sc, Vec3{o.x, o.y + h, o.z}, dir, grabReach + BodyHalfWidth());
            if (!hit.hit || !hit.gameObject) continue;
            auto* orb = hit.gameObject->GetComponent<Rigidbody3D>();
            if (orb && orb->bodyType == Rigidbody3D::BodyType::Dynamic) {
                m_grabbed = hit.gameObject;
                m_grabOffset = hit.gameObject->transform->Position().x - o.x;
                CallScript("on_grab");
                return;
            }
        }
        // In the air: catch a ledge (a wall face alone isn't something to hang from).
        float ledgeY = 0.0f, faceX = 0.0f;
        if (grabHang && m_stance == Stance::Stand && !m_grounded && rb && m_noLedge <= 0.0f && FindLedge(sc, ledgeY, faceX)) {
            // A ledge so low that hanging from it would leave the feet on the floor
            // isn't a hang; keep rising (it's caught higher up the jump, if at all).
            const float hangX = faceX - m_facing * (BodyHalfWidth() + 0.02f), hangY = ledgeY - bodyH - 0.1f;
            RaycastHit3D floor = Ray(sc, Vec3{hangX, hangY + bodyH * 0.5f, o.z}, Vec3{0, -1, 0}, bodyH * 0.5f + 0.08f);
            if (floor.hit && Solid(floor)) return;
            m_hanging = true;
            HumanoidActions::SetHanging(gameObject, true);
            m_ledgeY = ledgeY;
            m_savedGravity = rb->gravityScale;
            rb->gravityScale = 0.0f;
            rb->velocity = Vec3{0, 0, 0};
            // Hang with the hands on the edge, the body against the face.
            transform->SetPosition(Vec3{faceX - m_facing * (BodyHalfWidth() + 0.02f), ledgeY - bodyH - 0.1f, o.z});
            CallScript("on_grab");
        }
    }

    // The held object may have been destroyed: only trust the pointer while the
    // scene still owns it.
    static bool StillInScene(Scene* sc, GameObject* go) {
        if (!sc) return false;
        for (const auto& up : sc->Objects())
            if (up.get() == go) return !go->IsDestroyed() && go->active && go->transform;
        return false;
    }

    void Release(Rigidbody3D* rb) {
        if ((m_hanging || m_climbT >= 0.0f) && rb) rb->gravityScale = m_savedGravity;
        m_climbT = -1.0f;
        const bool had = m_grabbed || m_hanging;
        m_grabbed = nullptr; m_hanging = false;
        HumanoidActions::SetHanging(gameObject, false);
        if (had) CallScript("on_release");
    }

    void NoteGround(const Collision3D& c) {
        bool below = c.gameObject && c.gameObject->transform && transform &&
                     c.gameObject->transform->Position().y < transform->Position().y + 0.2f;
        if (Mathf::Abs(c.normal.y) > 0.5f && below) m_groundContact = 0.10f;
    }

    void CallScript(const char* ev) {
        if (gameObject)
            if (auto* s = gameObject->GetComponent<ScriptComponent>())
                if (s->VM()) s->VM()->CallEvent(ev);
    }

    Character* FindCharacter() const {
        if (auto* ch = gameObject->GetComponent<Character>()) return ch;
        for (Transform* c : transform->Children())
            if (c && c->gameObject) if (auto* ch = c->gameObject->GetComponent<Character>()) return ch;
        return nullptr;
    }
};

} // namespace okay

#pragma once
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Physics/Physics3D.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/ScriptComponent.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Net/NetOwnership.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Math/Mathf.hpp"
#include <cmath>

namespace okay {

/// 2.5D PLATFORMER controller in the style of LittleBigPlanet: a 3D world seen from
/// the side, where you run left / right and jump, and hop between a few DEPTH
/// LAYERS (front, middle, back) with W / S (stick or d-pad up / down). A layer
/// switch only happens when there's room on that layer. Hold the grab key (E, or
/// RB) to grab: drag / push a physics object, or hang from a ledge or a static
/// object in the air. Floaty jump with a variable height (let go early for a hop),
/// coyote time and a jump buffer; the side camera looks ahead and stays on the
/// level, not on the layer, so switching layers doesn't swing the view.
///
/// Script events on the player's script: on_jump, on_land, on_layer, on_grab,
/// on_release. Layer 0 is the front (nearest the camera).
class Platformer25DController : public Behaviour {
public:
    // ---- Run ----
    float walkSpeed = 5.0f;
    float runSpeed = 7.5f;
    char  sprintKey = Input::KeyShift;
    float acceleration = 45.0f, deceleration = 50.0f;
    float airControl = 0.65f;
    bool  driveAnimation = true;

    // ---- Jump ----
    bool  canJump = true;
    char  jumpKey = ' ';
    float jumpForce = 8.5f;
    int   maxJumps = 1;                 ///< 2 = double jump
    float jumpCut = 0.5f;               ///< upward speed kept when you let go early (1 = fixed height)
    float fallGravity = 1.6f;           ///< extra gravity while falling (snappier landings)
    float maxFallSpeed = 22.0f;
    float coyoteTime = 0.12f, jumpBufferTime = 0.12f;

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
    bool  grabHang = true;              ///< grab a static object in the air to hang from it

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
    bool  FacingRight() const { return m_facing > 0.0f; }
    GameObject* Grabbed() const { return m_grabbed; }
    float LayerZ(int i) const { return frontLayerZ - (float)Mathf::Clamp(i, 0, Mathf::Max(1, layerCount) - 1) * layerSpacing; }
    float MiddleZ() const { return frontLayerZ - (float)(Mathf::Max(1, layerCount) - 1) * layerSpacing * 0.5f; }

    void Start() override {
        layer = Mathf::Clamp(layer, 0, Mathf::Max(1, layerCount) - 1);
        if (transform) {
            Vec3 p = transform->Position(); p.z = LayerZ(layer);
            transform->SetPosition(p);
        }
    }

    /// Move to another layer if it exists (and has room). Returns true on success.
    bool SwitchLayer(int to) {
        if (to < 0 || to >= Mathf::Max(1, layerCount) || to == layer || !transform || !gameObject) return false;
        if (checkLayerFree && !LayerFree(to)) return false;
        layer = to;
        CallScript("on_layer");
        return true;
    }

    /// Whether the player would fit on layer `to` at its current height.
    bool LayerFree(int to) const {
        Scene* sc = gameObject ? gameObject->scene() : nullptr;
        if (!sc || !transform) return true;
        Vec3 p = transform->Position();
        const float heights[] = {0.45f, 1.35f};
        for (float h : heights) {
            Vec3 c{p.x, p.y + h, LayerZ(to)};
            for (Collider3D* col : sc->physics3D().OverlapSphere(*sc, c, bodyRadius))
                if (col && !col->isTrigger && col->gameObject && col->gameObject != gameObject &&
                    col->gameObject != m_grabbed) return false;
        }
        return true;
    }

    void Update(float dt) override {
        if (!transform || !gameObject) return;
        if (Game::Paused()) return;
        if (!IsLocallyControlled(gameObject)) return;
        Scene* sc = gameObject->scene();
        const bool ui = Input::UICaptured();
        auto* rb = gameObject->GetComponent<Rigidbody3D>();

        // ---- Input ----
        Vec2 axis = ui ? Vec2{0, 0} : Input::AxisWASD();
        Vec2 pad = ui ? Vec2{0, 0} : Input::GamepadAxis();
        if (Mathf::Abs(pad.x) > 0.15f) axis.x = pad.x;
        float mx = Mathf::Clamp(axis.x, -1.0f, 1.0f);
        bool moving = Mathf::Abs(mx) > 0.05f;

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

        // ---- Layers ----
        if (!ui && !m_hanging) {
            if (layerBackKey && Input::GetKeyDown(layerBackKey)) SwitchLayer(layer + 1);
            else if (layerFrontKey && Input::GetKeyDown(layerFrontKey)) SwitchLayer(layer - 1);
        }

        // ---- Grab ----
        bool grabHeld = !ui && canGrab && ((grabKey && Input::GetKey(grabKey)) ||
                                           (grabGamepadButton >= 0 && Input::GetGamepadButton(grabGamepadButton)));
        if (grabHeld && !m_grabbed && !m_hanging && !m_grabTried) { TryGrab(sc, rb); m_grabTried = true; }
        if (!grabHeld) { m_grabTried = false; if (m_grabbed || m_hanging) Release(rb); }
        if (m_grabbed && !StillInScene(sc, m_grabbed)) Release(rb);

        // ---- Facing (locked toward a grabbed object) ----
        if (moving && !m_grabbed) m_facing = mx > 0.0f ? 1.0f : -1.0f;
        {
            float a = faceCameraAngle * Mathf::Deg2Rad;
            Vec3 look{m_facing * std::cos(a), 0.0f, std::sin(a)};
            Quat want = Quat::LookRotation({-look.x, 0.0f, -look.z});   // mesh faces -Z
            float t = turnSpeed > 0.0f ? 1.0f - std::exp(-turnSpeed * dt) : 1.0f;
            transform->localRotation = Quat::Slerp(transform->localRotation, want, t);
        }

        // ---- Jump input ----
        if (!ui && jumpKey && Input::GetKeyDown(jumpKey)) m_jumpBuf = jumpBufferTime;
        else m_jumpBuf = Mathf::Max(0.0f, m_jumpBuf - dt);
        bool jumpHeld = !ui && jumpKey && Input::GetKey(jumpKey);

        bool running = sprintKey && Input::GetKey(sprintKey) && moving && !m_grabbed;
        float speed = (running ? runSpeed : walkSpeed) * (m_grabbed ? grabPullSpeed : 1.0f);
        const float targetZ = LayerZ(layer);

        if (rb) {
            if (m_hanging) {
                rb->velocity = Vec3{0, 0, 0};
                if (m_jumpBuf > 0.0f && canJump) {          // jump off the ledge
                    Release(rb);
                    DoJump(rb);
                }
            } else {
                float cur = rb->velocity.x, wantX = mx * speed;
                float rate = (moving ? acceleration : deceleration) * (m_grounded ? 1.0f : airControl);
                float dv = Mathf::Clamp(wantX - cur, -rate * dt, rate * dt);
                rb->velocity.x = cur + dv;

                if (canJump && m_jumpBuf > 0.0f) {
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
                float dz = targetZ - transform->Position().z;
                rb->velocity.z = Mathf::Abs(dz) < 0.005f ? 0.0f : Mathf::Clamp(dz * layerSwitchSpeed, -12.0f, 12.0f);

                // Drag what we hold along with us.
                if (m_grabbed)
                    if (auto* orb = m_grabbed->GetComponent<Rigidbody3D>()) {
                        float wantObjX = transform->Position().x + m_grabOffset;
                        orb->velocity.x = rb->velocity.x + (wantObjX - m_grabbed->transform->Position().x) * 8.0f;
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

        if (driveAnimation)
            if (Character* ch = FindCharacter())
                ch->anim = (!m_grounded && !m_hanging) ? 5 : (moving ? (running ? 3 : 2) : 1);
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
    void OnDestroy() override { if (m_hanging) Release(gameObject ? gameObject->GetComponent<Rigidbody3D>() : nullptr); }

private:
    float m_facing = 1.0f, m_lead = 0.0f;
    float m_groundContact = 0.0f, m_coyote = 0.0f, m_jumpBuf = 0.0f, m_jumpedAt = 1.0f;
    int   m_jumpsUsed = 0;
    bool  m_grounded = false, m_jumpCuttable = false, m_haveCam = false;
    bool  m_hanging = false, m_grabTried = false;
    float m_savedGravity = 1.0f, m_grabOffset = 0.0f;
    GameObject* m_grabbed = nullptr;
    Vec3  m_camPos{0, 0, 0};

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
        Vec3 o = transform->Position(); o.y += 0.15f;
        RaycastHit3D h = sc->physics3D().Raycast(*sc, o, Vec3{0, -1, 0}, 0.25f, gameObject);
        return h.hit && h.normal.y > 0.5f;
    }

    void TryGrab(Scene* sc, Rigidbody3D* rb) {
        if (!sc || !transform) return;
        Vec3 o = transform->Position();
        Vec3 dir{m_facing, 0.0f, 0.0f};
        const float heights[] = {0.9f, 1.6f, 0.4f};
        for (float h : heights) {
            RaycastHit3D hit = sc->physics3D().Raycast(*sc, Vec3{o.x, o.y + h, o.z}, dir, grabReach + 0.3f, gameObject);
            if (!hit.hit || !hit.gameObject) continue;
            auto* orb = hit.gameObject->GetComponent<Rigidbody3D>();
            if (orb && orb->bodyType == Rigidbody3D::BodyType::Dynamic) {
                m_grabbed = hit.gameObject;
                m_grabOffset = hit.gameObject->transform->Position().x - o.x;
                CallScript("on_grab");
                return;
            }
            if (grabHang && !m_grounded && rb) {   // hang from something solid
                m_hanging = true;
                m_savedGravity = rb->gravityScale;
                rb->gravityScale = 0.0f;
                rb->velocity = Vec3{0, 0, 0};
                CallScript("on_grab");
                return;
            }
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
        if (m_hanging && rb) rb->gravityScale = m_savedGravity;
        const bool had = m_grabbed || m_hanging;
        m_grabbed = nullptr; m_hanging = false;
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

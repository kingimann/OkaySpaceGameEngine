#pragma once
// ---------------------------------------------------------------------------
// ProceduralObjectHolding — a character picks an object up and holds it with its
// hands: the object sits in front of the chest (or at a socket), the hands go to
// its grip points with the Full Body IK, and the elbows bend down and out.
//
// Each frame, after the animation has posed the body, it places the held object
// relative to the animated chest, keeps every grip inside its arm's reach (the
// object comes closer rather than the arms stretching), and gives the Full Body
// IK one goal per hand: the wrist where the palm meets the grip, turned so the
// palm faces the grip, with an elbow hint below and outside the shoulder. The
// Full Body IK (ExecutionOrder 100) then bends the arms. It reuses the Full Body
// IK on the character, or adds one; on the built-in Character it switches on the
// part rig so the arms have bones to bend. Imported humanoids work the same way
// (bones found by name). Grips come from the object's HoldableObject (one is
// added with automatic settings if the object has none).
//
// Dropping hands the object back to physics (it keeps the holder's velocity) and
// the hands ease back to the animation. Throwing is separate: hold the throw
// input to charge, release to launch the object along the camera aim, strength
// scaled by that charge plus the holder's velocity.
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/Camera.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/CharacterIK.hpp"
#include "okay/Components/FullBodyIK.hpp"
#include "okay/Components/HoldableObject.hpp"
#include "okay/Core/Game.hpp"
#include "okay/Input/Input.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Math/Vec3.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <unordered_set>
#include <vector>

namespace okay {

class ProceduralObjectHolding : public Component {
public:
    int ExecutionOrder() const override { return 90; }   // after animation and FootIK (85), before FullBodyIK (100)

    enum class Hand { Left, Right, Both };
    enum class StyleMode { Automatic, Custom };
    enum class CarryAnim { Automatic, Custom };

    Hand      primaryHand  = Hand::Right;              ///< the hand a one-handed object goes in (Both = support hand too)
    StyleMode holdingStyle = StyleMode::Automatic;     ///< Automatic: the object's own style; Custom: always its grip points
    bool      armIK = true;                            ///< bend the arms onto the grips (off: the object rides along)
    float     transitionTime = 0.3f;                   ///< seconds the object takes to come into the hands
    bool      bodyAdjustment = true;                   ///< heavy / big loads: lean back, walk slower, kept off the body
    CarryAnim carryAnimations = CarryAnim::Automatic;  ///< Automatic: the built-in lean; Custom: your Animator's carry
                                                       ///< states, driven by the holding / carryStyle / carryWeight params

    // Throw. Hold the key, mouse button or pad button to charge; releasing launches
    // the object. Drop() is unchanged (it only lets go, at the holder's velocity).
    char  throwKey = 'g';            ///< hold to charge, release to throw (0 = no key)
    int   throwMouseButton = 2;      ///< mouse button that throws (-1 = none; 0 left, 1 right, 2 middle)
    int   throwGamepadButton = 2;    ///< pad button (0=A 1=B 2=X 3=Y ...; -1 = none)
    float throwChargeTime = 0.75f;   ///< seconds held to reach full strength
    float throwMinSpeed = 4.0f;      ///< launch speed (m/s) with no charge, along the aim
    float throwMaxSpeed = 16.0f;     ///< launch speed (m/s) at full charge
    float pickupRange = 2.0f;        ///< with empty hands, pressing the throw input picks up the nearest object this close

    /// 0..1 while the throw input is held, else 0.
    float ThrowCharge() const { return m_charge; }
    bool  IsChargingThrow() const { return m_charging; }

    /// Take `obj` into the hands. Refused when it's already held (by anyone), is
    /// this character or part of it, or is null. Holding something else drops it.
    bool Pickup(GameObject* obj) {
        if (!obj || obj->IsDestroyed() || !obj->transform || !gameObject) return false;
        if (obj->IsSelfOrDescendantOf(gameObject) || gameObject->IsSelfOrDescendantOf(obj)) return false;
        auto* h = obj->GetComponent<HoldableObject>();
        if (h && h->m_holder && h->m_holder != gameObject) return false;
        if (obj == m_held) return true;
        Drop();
        FinishRelease(true);
        if (!h) h = obj->AddComponent<HoldableObject>();
        h->m_holder = gameObject;
        m_held = obj; m_hold = h;
        m_haveFrom = false; m_t = 0.0f;
        // Physics lets go of it: no falling, no pushing the holder around.
        m_physics.clear();
        if (auto* rb = obj->GetComponent<Rigidbody3D>()) {
            m_rbType = rb->bodyType; m_rb = rb;
            rb->bodyType = Rigidbody3D::BodyType::Kinematic;
            rb->velocity = Vec3::Zero; rb->angularVelocity = Vec3::Zero;
        }
        if (Scene* sc = obj->scene())
            for (const auto& up : sc->Objects())
                if (up && up->IsSelfOrDescendantOf(obj))
                    for (auto* c : up->GetComponents<Collider3D>()) { m_physics.push_back({c, c->isTrigger}); c->isTrigger = true; }
        if (m_ik) for (int l = 0; l < 2; ++l) m_saved[l] = m_ik->effectors[l];
        m_savedValid = m_ik != nullptr;
        return true;
    }

    /// Pick up the nearest free HoldableObject within `range` metres of the
    /// character. Returns false when there's none (or it can't be taken).
    bool PickupNearest(float range = 2.0f) {
        Scene* sc = gameObject ? gameObject->scene() : nullptr;
        if (!sc || !transform) return false;
        const Vec3 at = BodyTransform()->Position();
        HoldableObject* best = nullptr;
        float bestD = range;
        for (auto* h : sc->FindObjectsOfType<HoldableObject>()) {
            if (!h->enabled || !h->gameObject || !h->gameObject->active || h->m_holder || !h->gameObject->transform) continue;
            if (h->gameObject->IsSelfOrDescendantOf(gameObject)) continue;
            const float d = (h->gameObject->transform->Position() - at).Magnitude();
            if (d <= bestD) { bestD = d; best = h; }
        }
        return best && Pickup(best->gameObject);
    }

    /// Let go of the held object: physics takes it back (moving with the holder's
    /// velocity) and the hands ease back to the animation.
    void Drop() {
        if (!m_held) return;
        if (m_rb) {
            m_rb->bodyType = m_rbType;
            if (m_rbType == Rigidbody3D::BodyType::Dynamic)
                if (auto* own = BodyRigidbody()) m_rb->velocity = own->velocity;
        }
        for (auto& p : m_physics) if (p.c) p.c->isTrigger = p.trigger;
        if (m_hold && m_hold->m_holder == gameObject) m_hold->m_holder = nullptr;
        Forget();
    }

    /// Launch the held object along the camera aim (the camera's -Z; the body's
    /// forward if there is no camera). `charge01` < 0 uses the charge built by
    /// holding the throw input. Speed is minSpeed + charge * (maxSpeed - minSpeed),
    /// added to the holder's velocity. A kinematic body becomes dynamic so it can
    /// fly; a static body is let go the way Drop() would. Returns false when
    /// nothing is held.
    bool Throw(float charge01 = -1.0f) {
        if (!m_held || m_held->IsDestroyed()) return false;
        const float c = charge01 < 0.0f ? m_charge : std::clamp(charge01, 0.0f, 1.0f);
        const float lo = std::min(throwMinSpeed, throwMaxSpeed);
        const float hi = std::max(throwMinSpeed, throwMaxSpeed);
        const Vec3 vel = HolderVelocity() + AimDirection() * (lo + c * (hi - lo));
        if (m_rb) {
            if (m_rbType == Rigidbody3D::BodyType::Static) m_rb->bodyType = m_rbType;
            else m_rb->bodyType = Rigidbody3D::BodyType::Dynamic;
            if (m_rb->bodyType == Rigidbody3D::BodyType::Dynamic) {
                m_rb->velocity = vel;
                m_rb->angularVelocity = Vec3::Zero;
                m_rb->continuousCollision = true;   // a fast throw shouldn't tunnel a wall
                m_rb->WakeUp();
            }
        }
        for (auto& p : m_physics) if (p.c) p.c->isTrigger = p.trigger;
        if (m_hold && m_hold->m_holder == gameObject) m_hold->m_holder = nullptr;
        Forget();
        return true;
    }

    GameObject* Held() const { return m_held; }
    bool IsHolding() const { return m_held != nullptr; }
    /// Whether hand `left`/right is on the object this frame.
    bool HandInUse(bool left) const { return m_held && m_use[left ? 0 : 1]; }
    /// Where that hand's palm meets the object this frame (world).
    Vec3 GripPoint(bool left) const { return m_gripW[left ? 0 : 1]; }
    /// The Full Body IK the hands are driven through (null until Play wires it).
    FullBodyIK* IK() const { return m_ik; }
    /// How heavy the load feels, 0 (light) .. 1 (30 kg, or a big heavy-style load).
    float Heaviness() const { return m_held ? m_heavy : 0.0f; }
    /// What movement speed is multiplied by while carrying (1 = unaffected; 0.55
    /// at the heaviest). Controllers read it through MoveSpeedScale.
    float SpeedMultiplier() const { return m_held && bodyAdjustment ? 1.0f - 0.45f * m_heavy : 1.0f; }
    /// The style the held object is carried in this frame (Automatic = a small
    /// item in one hand).
    HoldableObject::Style CarryStyle() const { return m_style; }

    void Start() override { EnsureRig(); }

    // Charge while the throw input is held; release launches. With empty hands the
    // same press picks up the nearest object, so a tap takes it and holding on
    // throws. Paused time and a modal UI (an open inventory) don't charge or throw.
    void Update(float dt) override {
        if (dt <= 0.0f || Input::UICaptured() || Game::Paused()) return;
        if (!m_held) {
            m_charging = false;
            m_charge = 0.0f;
            if (ThrowPressed()) PickupNearest(pickupRange);
            return;
        }
        if (ThrowHeld()) {
            m_charging = true;
            const float dur = throwChargeTime > 1e-3f ? throwChargeTime : 1e-3f;
            m_charge = std::min(1.0f, m_charge + dt / dur);
        } else if (m_charging) {
            Throw(m_charge);
        }
    }

    void LateUpdate(float dt) override {
        if (m_releasing) FinishRelease(false);
        if (m_held && EnsureRig()) Hold(dt);
        if (m_ik) for (int l = 0; l < 2; ++l) CurlFingers(l);
        AdjustBody(dt);
    }

    void OnDestroy() override {
        Drop();
        FinishRelease(true);
    }

    void OnObjectsDestroyed(const std::unordered_set<const GameObject*>& dying) override {
        if (m_held && dying.count(m_held)) {
            m_rb = nullptr; m_physics.clear();
            Forget();
        }
        for (auto& p : m_physics) if (p.c && dying.count(p.c->gameObject)) p.c = nullptr;
        if (m_rb && dying.count(m_rb->gameObject)) m_rb = nullptr;
        for (int l = 0; l < 2; ++l) {
            if (m_grip[l] && dying.count(m_grip[l])) m_grip[l] = nullptr;
            if (m_hint[l] && dying.count(m_hint[l])) m_hint[l] = nullptr;
            for (const Finger& f : m_fingers[l])
                if (dying.count(f.t->gameObject)) { m_fingers[l].clear(); m_fingersFound[l] = false; break; }
        }
        if (m_ik && dying.count(m_ik->gameObject)) { m_ik = nullptr; m_savedValid = false; m_releasing = false; }
        if (m_char && dying.count(m_char->gameObject)) m_char = nullptr;
    }

private:
    // One frame of holding: place the object, aim the hands.
    void Hold(float dt) {
        const Body b = BodyFrame();
        Plan p = MakePlan(b);
        if (!p.fixed) {
            KeepInReach(b, p);
            // (A rifle's stock belongs against the shoulder.)
            if (bodyAdjustment && m_style != HoldableObject::Style::Rifle && KeepOutOfBody(b, p)) KeepInReach(b, p, true);
        }
        // Ease from where it was picked up (kept relative to the body, so walking
        // during the pickup doesn't drag it) into the hold.
        const Quat inv = b.rot.Inverse();
        if (!m_haveFrom) {
            m_fromPos = inv * (m_held->transform->Position() - b.chest);
            m_fromRot = inv * m_held->transform->Rotation();
            m_haveFrom = true;
        }
        m_t = transitionTime > 0.0f ? std::min(1.0f, m_t + dt / transitionTime) : 1.0f;
        const float k = m_t * m_t * (3.0f - 2.0f * m_t);
        const Vec3 pos = b.chest + b.rot * Vec3::Lerp(m_fromPos, inv * (p.pos - b.chest), k);
        const Quat rot = (b.rot * Quat::Slerp(m_fromRot, inv * p.rot, k)).Normalized();
        m_held->transform->SetPosition(pos);
        m_held->transform->SetRotation(rot);
        for (int l = 0; l < 2; ++l) DriveHand(l, b, p, pos, rot);
    }

    // The body this frame: facing, the shoulders and how far each arm reaches.
    struct Body {
        Vec3 up{0, 1, 0}, fwd{0, 0, -1}, side{1, 0, 0}, chest{0, 0, 0};
        Quat rot;                 // object convention: local -Z = fwd, +Y = up
        float s = 1.0f;           // body scale (arm length vs the built-in body's)
        bool  arm[2] = {};
        Vec3  shoulder[2];
        float upper[2] = {}, reach[2] = {};
    };
    // Where the object goes and where the hands touch it (grip frames relative to
    // the object's pose: world = pos + rot * gp).
    struct Plan {
        Vec3 pos{0, 0, 0};
        Quat rot;
        bool use[2] = {};
        Vec3 gp[2], gf[2], gn[2];   // grip point, finger direction, palm normal
        float curl[2] = {}, indexCurl[2] = {};   // how far the fingers (and the index finger) close
        bool fixed = false;          // at a socket: the object doesn't move to suit the arms
        Vec3 bc{0, 0, 0}, bh{0.1f, 0.1f, 0.1f};   // the object's local bounds (centre, half size; scaled)
    };
    struct SavedCollider { Collider3D* c; bool trigger; };

    GameObject*      m_held = nullptr;
    HoldableObject*  m_hold = nullptr;
    Character*       m_char = nullptr;
    FullBodyIK*      m_ik = nullptr;
    Rigidbody3D*     m_rb = nullptr;
    Rigidbody3D::BodyType m_rbType = Rigidbody3D::BodyType::Dynamic;
    std::vector<SavedCollider> m_physics;
    FullBodyIK::Effector m_saved[2];
    bool  m_savedValid = false, m_releasing = false;
    GameObject* m_grip[2] = {};     // runtime hand goals (children of the body)
    GameObject* m_hint[2] = {};     // runtime elbow hints
    bool  m_use[2] = {};
    Vec3  m_gripW[2];
    bool  m_calib[2] = {};          // hand bone axes learned from the rig
    Vec3  m_fingersLocal[2], m_palmLocal[2];
    float m_palmLen[2] = {0.065f, 0.065f};
    // Finger bones under each hand (imported rigs), curled around the grip.
    struct Finger { Transform* t; float scale; bool index; Quat before, set; bool live; };
    std::vector<Finger> m_fingers[2];
    bool  m_fingersFound[2] = {};
    float m_curl[2] = {}, m_indexCurl[2] = {};
    HoldableObject::Style m_style = HoldableObject::Style::Automatic;
    float m_heavy = 0.0f;           // Heaviness() of the held object
    float m_lean = 0.0f;            // eased weight of the lean-back layer
    bool  m_announced = false;      // the Animator has been told about holding at least once
    Vec3  m_fromPos{0, 0, 0};
    Quat  m_fromRot;
    bool  m_haveFrom = false;
    float m_t = 1.0f;
    float m_charge = 0.0f;          ///< 0..1 throw charge while the input is held
    bool  m_charging = false;

    bool ThrowHeld() const {
        if (throwKey && Input::GetKey(throwKey)) return true;
        if (throwMouseButton >= 0 && Input::GetMouseButton(throwMouseButton)) return true;
        if (throwGamepadButton >= 0 && Input::GetGamepadButton(throwGamepadButton)) return true;
        return false;
    }
    bool ThrowPressed() const {
        if (throwKey && Input::GetKeyDown(throwKey)) return true;
        if (throwMouseButton >= 0 && Input::GetMouseButtonDown(throwMouseButton)) return true;
        if (throwGamepadButton >= 0 && Input::GetGamepadButtonDown(throwGamepadButton)) return true;
        return false;
    }
    Vec3 HolderVelocity() const {
        if (auto* own = BodyRigidbody()) return own->velocity;
        return Vec3::Zero;
    }
    /// Where a throw goes: the main camera's aim (local -Z), else the body's forward.
    Vec3 AimDirection() const {
        Scene* sc = gameObject ? gameObject->scene() : nullptr;
        Camera* cam = sc ? sc->mainCamera : nullptr;
        if ((!cam || !cam->transform) && sc)
            for (auto* c : sc->FindObjectsOfType<Camera>())
                if (c && c->enabled && c->transform && c->main) { cam = c; break; }
        if (cam && cam->transform) {
            Vec3 d = cam->transform->Rotation() * Vec3{0, 0, -1};
            if (d.SqrMagnitude() > 1e-8f) return d.Normalized();
        }
        if (Transform* bt = BodyTransform()) {
            Vec3 d = bt->Rotation() * Vec3{0, 0, -1};
            if (d.SqrMagnitude() > 1e-8f) return d.Normalized();
        }
        return {0, 0, -1};
    }

    Transform* BodyTransform() const { return m_char && m_char->transform ? m_char->transform : transform; }
    Rigidbody3D* BodyRigidbody() const {
        for (Transform* t = transform; t; t = t->Parent())
            if (t->gameObject) if (auto* rb = t->gameObject->GetComponent<Rigidbody3D>()) return rb;
        return nullptr;
    }
    static FullBodyIK* FindIKIn(GameObject* go) {
        if (!go) return nullptr;
        if (auto* f = go->GetComponent<FullBodyIK>()) return f;
        if (go->transform)
            for (Transform* c : go->transform->Children())
                if (c && c->gameObject) if (auto* f = FindIKIn(c->gameObject)) return f;
        return nullptr;
    }

    // Find (or add) the Full Body IK and make sure the arms have bones.
    bool EnsureRig() {
        if (m_ik) return true;
        if (!gameObject) return false;
        m_char = FindCharacterIn(gameObject);
        if (m_char && m_char->enabled && !m_char->driveExternal && !m_char->HasCustomBind()) {
            m_char->separateParts = true;
            if (!m_char->PartsBuilt()) m_char->BuildParts();
        }
        m_ik = FindIKIn(gameObject);
        if (!m_ik) m_ik = (m_char ? m_char->gameObject : gameObject)->AddComponent<FullBodyIK>();
        m_ik->MapBones(false);
        if (m_held && !m_savedValid) {
            for (int l = 0; l < 2; ++l) m_saved[l] = m_ik->effectors[l];
            m_savedValid = true;
        }
        return true;
    }

    Body BodyFrame() {
        Body b;
        Transform* bt = BodyTransform();
        b.up = bt->Up();
        b.fwd = bt->Rotation() * Vec3{0, 0, -1};
        b.chest = bt->Position() + b.up * 1.35f;
        Vec3 side = Vec3::Cross(b.fwd, b.up);
        Vec3 sh[2];
        bool both = true;
        for (int l = 0; l < 2; ++l) {
            Transform *a, *m, *e; m_ik->LimbBones(l, a, m, e);
            b.arm[l] = a && m && e;
            if (!b.arm[l]) { both = false; continue; }
            sh[l] = a->Position();
            b.upper[l] = (m->Position() - sh[l]).Magnitude();
            b.reach[l] = b.upper[l] + (e->Position() - m->Position()).Magnitude();
            b.shoulder[l] = sh[l];
            if (!m_calib[l]) Calibrate(l, m, e, b);
        }
        if (both) {
            b.chest = (sh[0] + sh[1]) * 0.5f;
            side = sh[1] - sh[0];
            b.s = std::max(0.2f, (b.reach[0] + b.reach[1]) * 0.5f / 0.54f);
        }
        side = side - b.up * Vec3::Dot(side, b.up);
        if (side.SqrMagnitude() > 1e-8f) b.side = side.Normalized();
        b.fwd = Vec3::Cross(b.up, b.side).Normalized();
        if (Vec3::Dot(b.fwd, bt->Rotation() * Vec3{0, 0, -1}) < 0.0f) b.fwd = b.fwd * -1.0f;
        b.rot = Quat::LookRotation(b.fwd * -1.0f, b.up);
        if (!both)
            for (int l = 0; l < 2; ++l)
                if (!b.arm[l]) b.shoulder[l] = b.chest + b.side * (l == 0 ? -0.22f : 0.22f);
        return b;
    }

    // Learn which way the hand bone's fingers and palm point (rigs disagree on
    // bone axes): fingers continue the forearm, the palm faces the body.
    void Calibrate(int l, Transform* fore, Transform* hand, const Body& b) {
        Vec3 f = hand->Position() - fore->Position();
        const float len = f.Magnitude();
        if (len < 1e-5f) return;
        f = f * (1.0f / len);
        Vec3 side = b.side;
        if (Transform* other = OtherHand(l)) {
            Vec3 d = other->Position() - hand->Position();
            if (d.SqrMagnitude() > 1e-6f) side = d;
        } else if (l == 1) side = side * -1.0f;
        Vec3 n = side - f * Vec3::Dot(side, f);
        if (n.SqrMagnitude() < 1e-8f) return;
        n = n.Normalized();
        const Quat inv = hand->Rotation().Inverse();
        m_fingersLocal[l] = inv * f;
        m_palmLocal[l] = inv * n;
        m_palmLen[l] = 0.25f * len;
        m_calib[l] = true;
    }
    Transform* OtherHand(int l) const {
        Transform *a, *m, *e; m_ik->LimbBones(1 - l, a, m, e);
        return e;
    }
    // Hand bone rotation that points the fingers along F with the palm facing N.
    Quat HandRotation(int l, const Vec3& F, const Vec3& N) const {
        return (Quat::LookRotation(F, N) * Quat::LookRotation(m_fingersLocal[l], m_palmLocal[l]).Inverse()).Normalized();
    }

    Plan MakePlan(const Body& b) {
        Plan p;
        const HoldableObject& h = *m_hold;
        const Vec3 scale = m_held->transform->LossyScale();
        const bool custom = (h.style == HoldableObject::Style::Custom || holdingStyle == StyleMode::Custom) &&
                            (h.leftGrip.enabled || h.rightGrip.enabled);
        Transform* socket = h.socket.empty() ? nullptr : FindSocket(h.socket);
        h.LocalBounds(p.bc, p.bh);
        p.bc = p.bc * scale;
        p.bh = {std::fabs(p.bh.x * scale.x), std::fabs(p.bh.y * scale.y), std::fabs(p.bh.z * scale.z)};
        m_style = custom ? HoldableObject::Style::Custom : h.ResolvedStyle();
        // Heaviness: from 3 kg (nothing) to 30 kg (all); big loads and the heavy style count more.
        m_heavy = std::min(1.0f, std::max(0.0f, (h.Mass() - 3.0f) / 27.0f) +
                                 (std::max(p.bh.x, p.bh.z) * 2.0f > 0.5f * b.s ? 0.25f : 0.0f));
        if (m_style == HoldableObject::Style::Heavy) m_heavy = std::max(m_heavy, 0.6f);
        if (custom) {
            p.rot = b.rot * Quat::Euler(h.holdRotation);
            p.pos = b.chest + (b.side * h.holdOffset.x + b.up * h.holdOffset.y + b.fwd * h.holdOffset.z) * b.s;
            const HoldableObject::Grip* g[2] = {&h.leftGrip, &h.rightGrip};
            // One grip authored: it goes in the primary hand, mirrored across the
            // object's local X if it was authored for the other hand.
            if (g[0]->enabled != g[1]->enabled && primaryHand != Hand::Both) {
                const int have = g[0]->enabled ? 0 : 1, want = primaryHand == Hand::Left ? 0 : 1;
                if (have != want) {
                    HoldableObject::Grip m = *g[have];
                    m.position.x = -m.position.x; m.rotation.y = -m.rotation.y; m.rotation.z = -m.rotation.z;
                    SetGrip(p, want, m, scale);
                    p.use[want] = true;
                } else { SetGrip(p, have, *g[have], scale); p.use[have] = true; }
            } else {
                for (int l = 0; l < 2; ++l) if (g[l]->enabled) { SetGrip(p, l, *g[l], scale); p.use[l] = true; }
            }
        } else {
            StylePlan(b, p, m_style, p.bc, p.bh);
        }
        if (socket) {
            p.pos = socket->Position(); p.rot = socket->Rotation();
            p.fixed = true;
            if (!custom) p.use[0] = p.use[1] = false;   // resting in a holster / on the back
        }
        return p;
    }
    // A named style (or Automatic's small-object carry) laid out around the body:
    // where the object's centre goes, which way it points, where each palm
    // touches it and how far the fingers close. `c`/`half` are the object's local
    // bounds (scaled).
    void StylePlan(const Body& b, Plan& p, HoldableObject::Style st, const Vec3& c, const Vec3& half) const {
        using S = HoldableObject::Style;
        if (st == S::Automatic && primaryHand == Hand::Both) st = S::Box;   // a small item in both hands
        const int P = primaryHand == Hand::Left ? 0 : 1, O = 1 - P;
        const float sp = P == 0 ? -1.0f : 1.0f;                 // the primary hand's side
        const bool support = m_hold->supportHand || primaryHand == Hand::Both;
        const float s = b.s;
        const Vec3 out = b.side * sp;                          // toward the primary hand's side
        // Long objects: the long side becomes the barrel / beam / blade.
        int la = 0; float hl = half.x;
        if (half.y > hl) { la = 1; hl = half.y; }
        if (half.z > hl) { la = 2; hl = half.z; }
        const Vec3 longLocal = la == 0 ? Vec3{1, 0, 0} : la == 1 ? Vec3{0, 1, 0} : Vec3{0, 0, -1};
        const Vec3 refLocal = la == 1 ? Vec3{0, 0, -1} : Vec3{0, 1, 0};
        const float hr = la == 1 ? half.z : half.y;              // along the reference (up / forward) axis
        const float hw = la == 0 ? half.z : half.x;              // across (sideways)
        const Quat fix = Quat::Euler(m_hold->holdRotation);       // corrects a model built another way
        auto align = [&](const Vec3& longW, const Vec3& refW) {
            return (Quat::LookRotation(longW, refW) * Quat::LookRotation(longLocal, refLocal).Inverse() * fix).Normalized();
        };
        auto grip = [&](int l, const Vec3& at, const Vec3& fingers, const Vec3& palm, const Vec3& centre,
                        float curl, float indexCurl) {
            SetGripWorld(p, l, at, fingers.Normalized(), palm.Normalized(), centre, p.rot);
            p.curl[l] = curl; p.indexCurl[l] = indexCurl;
        };
        Vec3 centre;
        switch (st) {
        case S::Pistol: {
            p.rot = align(b.fwd, b.up);
            centre = b.chest + (out * 0.1f + b.up * -0.08f + b.fwd * 0.45f) * s;
            const Vec3 handle = centre - b.fwd * (0.55f * hl) - b.up * (0.45f * hr);
            grip(P, handle + out * hw, b.fwd - b.up * 0.35f, out * -1.0f, centre, 0.85f, 0.35f);
            if (support) grip(O, handle - out * hw - b.up * (0.15f * hr), b.fwd - b.up * 0.2f, out, centre, 0.75f, 0.75f);
            break;
        }
        case S::Rifle: {
            p.rot = align(b.fwd, b.up);
            centre = b.chest + (out * 0.12f + b.up * -0.06f) * s + b.fwd * (hl * 0.85f);
            grip(P, centre - b.fwd * (0.4f * hl) - b.up * (0.6f * hr) + out * hw, b.fwd - b.up * 0.35f, out * -1.0f,
                 centre, 0.85f, 0.35f);
            grip(O, centre + b.fwd * (0.35f * hl) - b.up * hr, b.fwd + out * 0.4f, b.up, centre, 0.55f, 0.55f);
            break;
        }
        case S::Flashlight: {
            p.rot = align(b.fwd, b.up);
            centre = b.chest + (out * 0.2f + b.up * -0.12f + b.fwd * 0.42f) * s;
            grip(P, centre - b.fwd * (0.35f * hl) + out * hw, b.fwd, out * -1.0f, centre, 0.9f, 0.9f);
            break;
        }
        case S::Sword: {
            const Vec3 blade = (b.up * 0.87f + b.fwd * 0.5f).Normalized();
            const Vec3 across = (b.fwd - blade * Vec3::Dot(b.fwd, blade)).Normalized();
            p.rot = align(blade, across);
            const Vec3 hilt = b.chest + (out * (support ? 0.1f : 0.22f) + b.up * -0.42f + b.fwd * 0.32f) * s;
            centre = hilt + blade * (0.8f * hl);
            grip(P, hilt + out * hw, across, out * -1.0f, centre, 1.0f, 1.0f);
            if (support) grip(O, hilt - blade * (0.09f * s) - out * hw, across, out, centre, 1.0f, 1.0f);
            break;
        }
        case S::Cup: {
            // Upright, the handle (+X) toward the holding hand.
            p.rot = (Quat::LookRotation(out, b.up) * Quat::LookRotation({1, 0, 0}, {0, 1, 0}).Inverse() * fix).Normalized();
            centre = b.chest + (out * 0.2f + b.up * -0.3f + b.fwd * 0.3f) * s;
            grip(P, centre + out * half.x, b.fwd, out * -1.0f, centre, 0.9f, 0.9f);
            break;
        }
        case S::Heavy:
        case S::Box: {
            p.rot = (b.rot * fix).Normalized();
            const Vec3 hb = Extents(p.rot, half, b);           // (side, up, forward) half sizes
            const bool heavy = st == S::Heavy;
            // As low as the arms reach: a heavy load hangs on near-straight arms.
            const float gap = (heavy ? 0.06f : 0.15f) * s;
            const float halfShoulders = b.arm[0] && b.arm[1] ? (b.shoulder[1] - b.shoulder[0]).Magnitude() * 0.5f : 0.22f * s;
            const float R = 0.9f * (b.arm[0] && b.arm[1] ? std::min(b.reach[0], b.reach[1]) : 0.54f * s);
            const float dx = std::max(0.0f, hb.x - halfShoulders), dz = hb.z + gap;
            const float drop = std::min((heavy ? 0.55f : 0.3f) * s, std::sqrt(std::max(0.0f, R * R - dx * dx - dz * dz)));
            centre = b.chest - b.up * drop + b.fwd * dz;
            if (heavy) centre = centre + b.up * (0.75f * hb.y);
            for (int l = 0; l < 2; ++l) {
                const float sg = l == 0 ? -1.0f : 1.0f;
                if (heavy) grip(l, centre + b.side * (sg * hb.x) - b.up * (0.75f * hb.y), b.fwd,
                                b.side * -sg + b.up * 0.8f, centre, 0.45f, 0.45f);
                else       grip(l, centre + b.side * (sg * hb.x), b.fwd, b.side * -sg, centre, 0.2f, 0.2f);
            }
            break;
        }
        default: {   // small and light: one hand under it, forearm forward
            p.rot = (b.rot * fix).Normalized();
            const Vec3 hb = Extents(p.rot, half, b);
            centre = b.chest + (out * 0.18f + b.up * -0.32f + b.fwd * 0.32f) * s + b.up * hb.y;
            grip(P, centre - b.up * hb.y, b.fwd, b.up, centre, 0.4f, 0.4f);
            break;
        }
        }
        p.pos = centre - p.rot * c;
        // Re-anchor the grips on the object's origin.
        for (int l = 0; l < 2; ++l) if (p.use[l]) p.gp[l] = p.gp[l] + c;
    }
    // Half sizes of a box with local half extents `half`, turned by `rot`, along
    // the body's side / up / forward.
    static Vec3 Extents(const Quat& rot, const Vec3& half, const Body& b) {
        const Vec3 ax[3] = {rot * Vec3{half.x, 0, 0}, rot * Vec3{0, half.y, 0}, rot * Vec3{0, 0, half.z}};
        Vec3 e{0, 0, 0};
        for (const Vec3& a : ax) {
            e.x += std::fabs(Vec3::Dot(a, b.side));
            e.y += std::fabs(Vec3::Dot(a, b.up));
            e.z += std::fabs(Vec3::Dot(a, b.fwd));
        }
        return e;
    }
    static void SetGrip(Plan& p, int l, const HoldableObject::Grip& g, const Vec3& scale) {
        const Quat r = Quat::Euler(g.rotation);
        p.curl[l] = p.indexCurl[l] = g.curl;
        p.gp[l] = g.position * scale;
        p.gf[l] = r * Vec3{0, 0, -1};
        p.gn[l] = r * Vec3{0, -1, 0};
    }
    // A grip given in world terms, stored relative to the object's centre.
    static void SetGripWorld(Plan& p, int l, const Vec3& at, const Vec3& fingers, const Vec3& palm,
                             const Vec3& centre, const Quat& rot) {
        const Quat inv = rot.Inverse();
        p.gp[l] = inv * (at - centre);
        p.gf[l] = inv * fingers;
        p.gn[l] = inv * palm;
        p.use[l] = true;
    }
    Transform* FindSocket(const std::string& name) const {
        Scene* sc = gameObject ? gameObject->scene() : nullptr;
        if (!sc) return nullptr;
        for (const auto& up : sc->Objects())
            if (up && up->name == name && up->transform && up->IsSelfOrDescendantOf(gameObject)) return up->transform;
        return nullptr;
    }

    Vec3 WristFor(int l, const Body& b, const Vec3& grip, const Vec3& F, const Vec3& N) const {
        return grip - F * m_palmLen[l] - N * (0.02f * b.s);
    }

    // Move the object toward the body until every hand on it is within reach
    // (a little short of full stretch, where the soft IK would start easing).
    // `keepOut`: never move it back toward the body (it was just pushed off it).
    void KeepInReach(const Body& b, Plan& p, bool keepOut = false) const {
        for (int it = 0; it < 4; ++it) {
            Vec3 shift{0, 0, 0};
            float worst = 0.0f;
            for (int l = 0; l < 2; ++l) {
                if (!p.use[l] || !b.arm[l] || b.reach[l] <= 0.0f) continue;
                const Vec3 w = WristFor(l, b, p.pos + p.rot * p.gp[l], p.rot * p.gf[l], p.rot * p.gn[l]);
                const Vec3 d = b.shoulder[l] - w;
                const float dist = d.Magnitude(), maxR = b.reach[l] * 0.95f;
                if (dist > maxR && dist - maxR > worst) { worst = dist - maxR; shift = d * ((dist - maxR) / dist); }
            }
            if (worst <= 1e-4f) break;
            if (keepOut) { const float f = Vec3::Dot(shift, b.fwd); if (f < 0.0f) shift = shift - b.fwd * f; }
            p.pos = p.pos + shift;
        }
    }
    // Keep the object out of the torso (a box from the hips to the shoulders):
    // anything that would sink into the chest or belly is pushed out in front.
    // Returns true when it moved.
    static bool KeepOutOfBody(const Body& b, Plan& p) {
        const Vec3 hb = Extents(p.rot, p.bh, b);
        const Vec3 rel = p.pos + p.rot * p.bc - b.chest;
        const float x = Vec3::Dot(rel, b.side), y = Vec3::Dot(rel, b.up), z = Vec3::Dot(rel, b.fwd);
        const float front = 0.13f * b.s;
        const bool overlaps = std::fabs(x) - hb.x < 0.24f * b.s && y + hb.y > -0.75f * b.s && y - hb.y < 0.15f * b.s &&
                              z - hb.z < front && z + hb.z > -0.14f * b.s;
        if (!overlaps) return false;
        p.pos = p.pos + b.fwd * (front + 0.01f * b.s - (z - hb.z));
        return true;
    }

    GameObject* RuntimeTarget(GameObject*& slot, const char* name) {
        if (slot) return slot;
        Scene* sc = gameObject ? gameObject->scene() : nullptr;
        if (!sc) return nullptr;
        slot = sc->CreateGameObject(name);
        slot->transient = true;
        slot->transform->SetParent(BodyTransform(), false);
        return slot;
    }

    void DriveHand(int l, const Body& b, const Plan& p, const Vec3& pos, const Quat& rot) {
        FullBodyIK::Effector& e = m_ik->effectors[l];
        m_use[l] = armIK && p.use[l] && b.arm[l];
        if (!m_use[l]) { e.enabled = false; return; }
        const Vec3 G = pos + rot * p.gp[l];
        const Vec3 F = (rot * p.gf[l]).Normalized(), N = (rot * p.gn[l]).Normalized();
        m_gripW[l] = G;
        m_curl[l] = p.curl[l]; m_indexCurl[l] = p.indexCurl[l];
        GameObject* g = RuntimeTarget(m_grip[l], l == 0 ? "Holding Grip L" : "Holding Grip R");
        GameObject* h = RuntimeTarget(m_hint[l], l == 0 ? "Holding Elbow L" : "Holding Elbow R");
        if (!g || !h) return;
        g->transform->SetPosition(WristFor(l, b, G, F, N));
        if (m_calib[l]) g->transform->SetRotation(HandRotation(l, F, N));
        // Elbows hang below and outside the shoulder, a little behind it.
        const Vec3 out = b.side * (l == 0 ? -1.0f : 1.0f);
        h->transform->SetPosition(b.shoulder[l] + (b.up * -1.0f + out * 0.6f - b.fwd * 0.3f) * std::max(b.upper[l], 0.05f));
        e.enabled = true;
        e.target = g->transform;
        e.hint = h->transform;
        e.positionWeight = 1.0f;
        e.rotationWeight = m_calib[l] ? 1.0f : 0.0f;
        e.pull = 0.0f;
    }

    // Close the fingers around the grip: every finger joint turns toward the palm
    // (thumb half as far), faded with the hand's IK weight. Last frame's curl is
    // taken off first when nothing re-posed a finger, so it never compounds.
    void CurlFingers(int l) {
        Transform *a, *m, *e; m_ik->LimbBones(l, a, m, e);
        if (e && m_calib[l] && !m_fingersFound[l]) FindFingers(l, e);
        const float w = (m_held || m_releasing) && e && m_calib[l] ? m_ik->EffectiveWeight(l) : 0.0f;
        const Vec3 axis = e && m_calib[l] ? e->Rotation() * Vec3::Cross(m_fingersLocal[l], m_palmLocal[l]) : Vec3{0, 0, 0};
        for (Finger& f : m_fingers[l]) {
            const Quat r = f.t->localRotation;
            if (f.live && std::fabs(Quat::Dot(r, f.set)) > 0.999999f) f.t->localRotation = f.before;
            f.live = false;
            const float k = (f.index ? m_indexCurl[l] : m_curl[l]) * f.scale * w;
            if (k <= 1e-3f || axis.SqrMagnitude() < 1e-8f) continue;
            f.before = f.t->localRotation;
            f.t->SetRotation((Quat::AngleAxis(k * 70.0f, axis) * f.t->Rotation()).Normalized());
            f.set = f.t->localRotation;
            f.live = true;
        }
    }
    void FindFingers(int l, Transform* hand) {
        m_fingersFound[l] = true;
        m_fingers[l].clear();
        std::vector<Transform*> stack(hand->Children().rbegin(), hand->Children().rend());
        while (!stack.empty()) {   // depth first, parents before children
            Transform* t = stack.back(); stack.pop_back();
            if (!t || !t->gameObject) continue;
            std::string n;
            for (char ch : t->gameObject->name) n += (char)std::tolower((unsigned char)ch);
            auto has = [&](const char* k) { return n.find(k) != std::string::npos; };
            if (has("thumb")) m_fingers[l].push_back({t, 0.5f, false, {}, {}, false});
            else if (has("index")) m_fingers[l].push_back({t, 1.0f, true, {}, {}, false});
            else if (has("finger") || has("middle") || has("ring") || has("pinky") || has("little"))
                m_fingers[l].push_back({t, 1.0f, false, {}, {}, false});
            for (auto it = t->Children().rbegin(); it != t->Children().rend(); ++it) stack.push_back(*it);
        }
    }

    // Body adjustment through the character's animation: the Animator gets the
    // holding / carryStyle / carryWeight parameters (its own carry states can use
    // them), and with Automatic carry animations an additive layer leans the torso
    // back against a heavy load, eased in and out.
    static constexpr const char* kLeanClip = "Holding Lean";
    void AdjustBody(float dt) {
        if (m_held || m_announced) {
            if (AnimStateMachine* sm = BaseAnimator()) {
                sm->SetBool("holding", m_held != nullptr);
                sm->SetInt("carryStyle", m_held ? (int)m_style : 0);
                sm->SetFloat("carryWeight", m_held && m_hold ? m_hold->Mass() : 0.0f);
                m_announced = m_held != nullptr;
            }
        }
        const float want = m_held && bodyAdjustment && carryAnimations == CarryAnim::Automatic ? m_heavy : 0.0f;
        m_lean += (want - m_lean) * (1.0f - std::exp(-4.0f * dt));
        if (std::fabs(want - m_lean) < 1e-3f) m_lean = want;
        Character* ch = m_char;
        if (!ch) return;
        const bool mine = ch->LayerClip() == kLeanClip;
        if (m_lean <= 1e-3f) { if (mine) ch->StopLayer(); return; }
        if (mine) { ch->SetLayerWeight(m_lean); return; }
        if (ch->IsLayering()) return;   // a script's layer is playing: leave it be
        if (!ch->HasClip(kLeanClip)) {
            AnimClip c;
            c.name = kLeanClip;
            AnimKey k;
            k.pose.assign(Character::BoneCount(), Vec3{0, 0, 0});
            k.pose[1] = {-10.0f, 0.0f, 0.0f};   // torso: lean back 10 degrees at full weight
            c.keys.push_back(k);
            ch->AddClip(c);
        }
        ch->PlayLayer(kLeanClip, Character::BoneBit(1), true, m_lean);
    }
    AnimStateMachine* BaseAnimator() const {
        GameObject* host = m_char ? m_char->gameObject : gameObject;
        if (!host) return nullptr;
        for (auto* sm : host->GetComponents<AnimStateMachine>()) if (sm->layer == 0) return sm;
        return nullptr;
    }

    // Forget the object; the hands fade out on the goals they had, then the Full
    // Body IK gets its own effectors back.
    void Forget() {
        m_held = nullptr; m_hold = nullptr; m_rb = nullptr; m_physics.clear();
        m_use[0] = m_use[1] = false;
        m_charge = 0.0f; m_charging = false;
        if (m_ik) { m_ik->effectors[0].enabled = false; m_ik->effectors[1].enabled = false; }
        m_releasing = m_savedValid;
    }
    void FinishRelease(bool now) {
        if (!m_releasing || !m_ik) { m_releasing = false; return; }
        if (!now && (m_ik->EffectiveWeight(0) > 0.0f || m_ik->EffectiveWeight(1) > 0.0f)) return;
        for (int l = 0; l < 2; ++l) m_ik->effectors[l] = m_saved[l];
        m_releasing = false; m_savedValid = false;
    }
};

} // namespace okay

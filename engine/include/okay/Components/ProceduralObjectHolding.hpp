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
// Dropping hands the object back to physics and the hands ease back to the
// animation.
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/CharacterIK.hpp"
#include "okay/Components/FullBodyIK.hpp"
#include "okay/Components/HoldableObject.hpp"
#include "okay/Physics/Collider3D.hpp"
#include "okay/Physics/Rigidbody3D.hpp"
#include "okay/Math/Quat.hpp"
#include "okay/Math/Vec3.hpp"
#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <vector>

namespace okay {

class ProceduralObjectHolding : public Component {
public:
    int ExecutionOrder() const override { return 90; }   // after animation, before the IK solve (100)

    enum class Hand { Left, Right, Both };
    enum class StyleMode { Automatic, Custom };

    Hand      primaryHand  = Hand::Right;              ///< the hand a one-handed object goes in (Both = always two hands)
    StyleMode holdingStyle = StyleMode::Automatic;     ///< Automatic: the object's own style; Custom: always its grip points
    bool      armIK = true;                            ///< bend the arms onto the grips (off: the object rides along)
    float     transitionTime = 0.3f;                   ///< seconds the object takes to come into the hands

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

    GameObject* Held() const { return m_held; }
    bool IsHolding() const { return m_held != nullptr; }
    /// Whether hand `left`/right is on the object this frame.
    bool HandInUse(bool left) const { return m_held && m_use[left ? 0 : 1]; }
    /// Where that hand's palm meets the object this frame (world).
    Vec3 GripPoint(bool left) const { return m_gripW[left ? 0 : 1]; }
    /// The Full Body IK the hands are driven through (null until Play wires it).
    FullBodyIK* IK() const { return m_ik; }

    void Start() override { EnsureRig(); }

    void LateUpdate(float dt) override {
        if (m_releasing) FinishRelease(false);
        if (!m_held || !EnsureRig()) return;
        const Body b = BodyFrame();
        Plan p = MakePlan(b);
        if (!p.fixed) KeepInReach(b, p);
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
        }
        if (m_ik && dying.count(m_ik->gameObject)) { m_ik = nullptr; m_savedValid = false; m_releasing = false; }
        if (m_char && dying.count(m_char->gameObject)) m_char = nullptr;
    }

private:
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
        bool fixed = false;          // at a socket: the object doesn't move to suit the arms
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
    Vec3  m_fromPos{0, 0, 0};
    Quat  m_fromRot;
    bool  m_haveFrom = false;
    float m_t = 1.0f;

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
            // Automatic: big or heavy = both hands on its sides in front of the
            // belly; small = one hand under it, forearm forward.
            Vec3 c, half;
            h.LocalBounds(c, half);
            c = c * scale; half = {std::fabs(half.x * scale.x), std::fabs(half.y * scale.y), std::fabs(half.z * scale.z)};
            p.rot = b.rot;
            const bool two = primaryHand == Hand::Both || half.x * 2.0f > 0.3f * b.s || h.Mass() > 5.0f;
            Vec3 centre;
            if (two) {
                centre = b.chest + b.up * (-0.3f * b.s) + b.fwd * (half.z + 0.15f * b.s);
                for (int l = 0; l < 2; ++l) {
                    const float sgn = l == 0 ? -1.0f : 1.0f;
                    SetGripWorld(p, l, centre + b.side * (sgn * half.x), b.fwd, b.side * -sgn, centre, b.rot);
                }
            } else {
                const int l = primaryHand == Hand::Left ? 0 : 1;
                const float sgn = l == 0 ? -1.0f : 1.0f;
                centre = b.chest + (b.side * (sgn * 0.18f) + b.up * -0.32f + b.fwd * 0.32f) * b.s + b.up * half.y;
                SetGripWorld(p, l, centre - b.up * half.y, b.fwd, b.up, centre, b.rot);
            }
            p.pos = centre - p.rot * c;
            // Re-anchor the grips on the object's origin.
            for (int l = 0; l < 2; ++l) if (p.use[l]) p.gp[l] = p.gp[l] + c;
        }
        if (socket) {
            p.pos = socket->Position(); p.rot = socket->Rotation();
            p.fixed = true;
            if (!custom) p.use[0] = p.use[1] = false;   // resting in a holster / on the back
        }
        return p;
    }
    static void SetGrip(Plan& p, int l, const HoldableObject::Grip& g, const Vec3& scale) {
        const Quat r = Quat::Euler(g.rotation);
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
    void KeepInReach(const Body& b, Plan& p) const {
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
            p.pos = p.pos + shift;
        }
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

    // Forget the object; the hands fade out on the goals they had, then the Full
    // Body IK gets its own effectors back.
    void Forget() {
        m_held = nullptr; m_hold = nullptr; m_rb = nullptr; m_physics.clear();
        m_use[0] = m_use[1] = false;
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

#pragma once
// ---------------------------------------------------------------------------
// LedgeHandsIK - puts the hands on the ledge while the body hangs.
// The LedgeGrab traverse (Traversal.hpp) keeps the grabbed top edge (the probe's
// wall hit, at the top's height) in TraverseJobs() during the grab move and in
// HangEdges() / HangPoints() during the hold. While either exists, this switches
// two LimbIK arms on and aims them at points on that edge (Math/LedgeHands.hpp);
// when the hang ends (climb up, let go, component disabled or removed) the arms
// get back what they had before the hang: on/off, target, target object and fade
// time. Outside a hang it never writes to the arms, so arms shared with another
// user (a held object) are left alone.
// Add it next to two LimbIK arm components, or leave leftArm / rightArm empty and
// it uses a LimbIK already on each hand bone, or adds one from the humanoid bone map.
// Runs just before LimbIK (order 104 < 105).
// Open: a ledge that moves or is destroyed mid-hang is not followed (the edge is a
// stored point); an arm LimbIK removed with RemoveComponent (not by destroying its
// object) is not noticed.
// ---------------------------------------------------------------------------
#include "okay/Animation/HumanoidBones.hpp"
#include "okay/Components/LimbIK.hpp"
#include "okay/Components/Traversal.hpp"
#include "okay/Math/LedgeHands.hpp"
#include "okay/Scene/Component.hpp"
#include <unordered_set>

namespace okay {

class LedgeHandsIK : public Component {
public:
    int ExecutionOrder() const override { return 104; }
    LimbIK* leftArm = nullptr;
    LimbIK* rightArm = nullptr;
    float setback = 0.25f;     ///< only for a lip with no stored edge: how far the body hangs out from the edge
    float handSpread = 0.22f;  ///< each hand's distance from the centre, along the edge
    float overEdge = 0.05f;    ///< how far the fingers reach onto the top
    float fadeTime = 0.2f;     ///< blend in / out
    bool  autoWire = true;     ///< find or add the two arms when none are set

    bool Holding() const { return m_holding; }

    void Start() override { if (autoWire) Wire(); }

    void OnDisable() override { EndHold(true); }
    void OnDestroy() override { EndHold(true); }

    void OnObjectsDestroyed(const std::unordered_set<const GameObject*>& dying) override {
        for (int i = 0; i < 2; ++i) {
            LimbIK*& a = Arm(i);
            if (a && dying.count(a->gameObject)) { a = nullptr; m_saved[i].valid = false; }
            Transform*& t = m_saved[i].targetObject;
            if (t && dying.count(t->gameObject)) t = nullptr;
        }
    }

    void LateUpdate(float dt) override {
        Vec3 l, r;
        const bool want = leftArm && rightArm && gameObject && gameObject->transform && Targets(l, r);
        if (want) {
            if (!m_holding) BeginHold();
            Aim(0, l);
            Aim(1, r);
        } else if (m_holding) {
            EndHold(false);
        }
        if (m_restoreLeft > 0.0f) {
            m_restoreLeft -= dt;
            if (m_restoreLeft <= 0.0f) { m_restoreLeft = 0.0f; RestoreTargets(); }
        }
    }

private:
    // What an arm had before the hold, given back when it ends.
    struct Saved {
        bool valid = false;
        bool ikActive = false;
        float fadeTime = 0.0f;
        Transform* targetObject = nullptr;
        Vec3 target{};
        Vec3 aim{};           ///< the last point this component aimed at
    };
    Saved m_saved[2];
    bool m_holding = false;
    float m_restoreLeft = 0.0f;   ///< seconds until the targets go back (after the fade out)

    LimbIK*& Arm(int i) { return i == 0 ? leftArm : rightArm; }

    void BeginHold() {
        // A hold that starts again while the last one is still fading out keeps the
        // values saved before the first one: they are still the arm owner's.
        const bool keep = m_restoreLeft > 0.0f;
        m_restoreLeft = 0.0f;
        for (int i = 0; i < 2; ++i) {
            LimbIK* a = Arm(i);
            Saved& s = m_saved[i];
            if (!a) { s.valid = false; continue; }
            if (!(keep && s.valid)) {
                s.valid = true;
                s.ikActive = a->ikActive; s.fadeTime = a->fadeTime;
                s.targetObject = a->targetObject; s.target = a->target;
            }
            a->ikActive = true;
            a->fadeTime = fadeTime;
        }
        m_holding = true;
    }

    // Give the arms back. An arm that was on before goes back to its own target now;
    // one that was off fades out from the ledge first, then gets its target back
    // (unless `now`: disabled or removed, there is no later frame).
    void EndHold(bool now) {
        if (m_holding) {
            m_holding = false;
            bool wait = false;
            for (int i = 0; i < 2; ++i) {
                LimbIK* a = Arm(i);
                const Saved& s = m_saved[i];
                if (!a || !s.valid) continue;
                if (a->ikActive) a->ikActive = s.ikActive;   // not if someone else switched it off meanwhile
                if (!s.ikActive) wait = true;
            }
            if (wait && !now && fadeTime > 0.0f) { m_restoreLeft = fadeTime; return; }
        }
        m_restoreLeft = 0.0f;
        RestoreTargets();
    }

    // Put back target, target object and fade time, each only if it still holds what
    // this component wrote (someone else may have taken the arm meanwhile).
    void RestoreTargets() {
        for (int i = 0; i < 2; ++i) {
            LimbIK* a = Arm(i);
            Saved& s = m_saved[i];
            if (!a || !s.valid) { s.valid = false; continue; }
            if (!a->targetObject && a->target.x == s.aim.x && a->target.y == s.aim.y && a->target.z == s.aim.z) {
                a->targetObject = s.targetObject;
                a->target = s.target;
            }
            if (a->fadeTime == fadeTime) a->fadeTime = s.fadeTime;
            s.valid = false;
        }
    }

    bool Targets(Vec3& l, Vec3& r) const {
        LedgeGrip g;
        if (!FindGrip(g)) return false;
        return LedgeHandTargets(g, handSpread, overEdge, l, r);
    }
    bool FindGrip(LedgeGrip& g) const {
        TraverseValidate(gameObject);
        const Vec3 body = gameObject->transform->Position();
        auto j = TraverseJobs().find(gameObject);
        if (j != TraverseJobs().end()) {
            const TraverseJob& job = j->second;
            if (!job.rememberLip) return false;
            g = job.haveEdge ? LedgeFromEdge(job.edge, job.edgeNormal) : LedgeFromLip(body, job.lip, setback);
            return g.valid;
        }
        auto h = HangPoints().find(gameObject);
        if (h == HangPoints().end()) return false;
        auto e = HangEdges().find(gameObject);
        g = e != HangEdges().end() ? LedgeFromEdge(e->second.point, e->second.normal)
                                   : LedgeFromLip(body, h->second, setback);
        return g.valid;
    }
    void Aim(int i, const Vec3& want) {
        LimbIK* a = Arm(i);
        a->targetObject = nullptr;
        a->target = a->upper ? ClampToReach(a->upper->Position(), want, 1.15f * ArmLength(a)) : want;
        m_saved[i].aim = a->target;
    }
    static float ArmLength(const LimbIK* a) {
        if (!a->upper || !a->lower || !a->end) return 1e6f;
        return (a->lower->Position() - a->upper->Position()).Magnitude()
             + (a->end->Position() - a->lower->Position()).Magnitude();
    }
    void Wire() {
        if (leftArm && rightArm) return;
        const humanoid::BoneMap m = humanoid::MapHumanoidBones(gameObject);
        const std::vector<LimbIK*> have = gameObject->GetComponentsInChildren<LimbIK>(true);
        auto add = [&](LimbIK*& slot, int u, int lo, int e) {
            if (slot) return;
            GameObject* hand = m.Get(e);
            if (!hand) return;
            // A LimbIK already on this hand (set up by hand, or for a held object) is reused:
            // two solvers on one arm would fight.
            for (LimbIK* a : have)
                if (a && (a->end == hand->transform || (!a->end && !a->endName.empty() && a->endName == hand->name))) {
                    slot = a;
                    return;
                }
            if (!m.Get(u) || !m.Get(lo)) return;
            auto* a = gameObject->AddComponent<LimbIK>();
            a->upper = m.Get(u)->transform; a->lower = m.Get(lo)->transform; a->end = hand->transform;
            a->ikActive = false; a->stretch = 0.1f;
            a->poleMode = LimbIK::PoleMode::Character; a->pole = Vec3{0, -0.5f, -1.0f};
            slot = a;
        };
        add(leftArm, humanoid::LUpperArm, humanoid::LForeArm, humanoid::LHand);
        add(rightArm, humanoid::RUpperArm, humanoid::RForeArm, humanoid::RHand);
    }
};

} // namespace okay

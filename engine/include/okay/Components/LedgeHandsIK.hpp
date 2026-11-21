#pragma once
// ---------------------------------------------------------------------------
// LedgeHandsIK - puts the hands on the ledge while the body hangs.
// The LedgeGrab traverse (Traversal.hpp) keeps the lip it grabbed in
// TraverseJobs() (the grab move) and HangPoints() (the hold). While either
// exists, this switches two LimbIK arms on and aims them at points on the top
// edge (Math/LedgeHands.hpp); when the hang ends (climb up, let go) they fade
// out over `fadeTime`. Add it next to two LimbIK arm components, or leave
// leftArm / rightArm empty and it adds them from the humanoid bone map.
// Runs just before LimbIK (order 104 < 105).
// ---------------------------------------------------------------------------
#include "okay/Animation/HumanoidBones.hpp"
#include "okay/Components/LimbIK.hpp"
#include "okay/Components/Traversal.hpp"
#include "okay/Math/LedgeHands.hpp"
#include "okay/Scene/Component.hpp"

namespace okay {

class LedgeHandsIK : public Component {
public:
    int ExecutionOrder() const override { return 104; }
    LimbIK* leftArm = nullptr;
    LimbIK* rightArm = nullptr;
    float setback = 0.25f;     ///< how far the hanging body is out from the edge (matches the grab probe)
    float handSpread = 0.22f;  ///< each hand's distance from the centre, along the edge
    float overEdge = 0.05f;    ///< how far the fingers reach onto the top
    float fadeTime = 0.2f;     ///< blend in / out
    bool  autoWire = true;     ///< add the two arms from the bone map when none are set

    bool Holding() const { return m_holding; }

    void Start() override { if (autoWire) Wire(); }

    void LateUpdate(float) override {
        m_holding = false;
        if (!leftArm || !rightArm || !gameObject || !gameObject->transform) return;
        Vec3 lip;
        const bool have = FindLip(lip);
        if (have) {
            const LedgeGrip g = LedgeFromLip(gameObject->transform->Position(), lip, setback);
            Vec3 l, r;
            if (LedgeHandTargets(g, handSpread, overEdge, l, r)) {
                m_holding = true;
                Aim(leftArm, l);
                Aim(rightArm, r);
            }
        }
        for (LimbIK* a : {leftArm, rightArm}) { a->ikActive = m_holding; a->fadeTime = fadeTime; }
    }

private:
    bool m_holding = false;

    bool FindLip(Vec3& lip) const {
        TraverseValidate(gameObject);
        auto j = TraverseJobs().find(gameObject);
        if (j != TraverseJobs().end()) {
            if (!j->second.rememberLip) return false;
            lip = j->second.lip;
            return true;
        }
        auto h = HangPoints().find(gameObject);
        if (h == HangPoints().end()) return false;
        lip = h->second;
        return true;
    }
    static void Aim(LimbIK* a, const Vec3& want) {
        a->targetObject = nullptr;
        if (a->upper) a->target = ClampToReach(a->upper->Position(), want, 1.15f * ArmLength(a));
        else a->target = want;
    }
    static float ArmLength(const LimbIK* a) {
        if (!a->upper || !a->lower || !a->end) return 1e6f;
        return (a->lower->Position() - a->upper->Position()).Magnitude()
             + (a->end->Position() - a->lower->Position()).Magnitude();
    }
    void Wire() {
        if (leftArm && rightArm) return;
        const humanoid::BoneMap m = humanoid::MapHumanoidBones(gameObject);
        auto add = [&](LimbIK*& slot, int u, int lo, int e) {
            if (slot) return;
            if (!m.Get(u) || !m.Get(lo) || !m.Get(e)) return;
            auto* a = gameObject->AddComponent<LimbIK>();
            a->upper = m.Get(u)->transform; a->lower = m.Get(lo)->transform; a->end = m.Get(e)->transform;
            a->ikActive = false; a->stretch = 0.1f;
            a->poleMode = LimbIK::PoleMode::Character; a->pole = Vec3{0, -0.5f, -1.0f};
            slot = a;
        };
        add(leftArm, humanoid::LUpperArm, humanoid::LForeArm, humanoid::LHand);
        add(rightArm, humanoid::RUpperArm, humanoid::RForeArm, humanoid::RHand);
    }
};

} // namespace okay

#pragma once
// ---------------------------------------------------------------------------
// IK Target — an empty object a limb reaches for. Put it in the scene, pick
// the character and the limb (hand, foot, or head), and that limb eases onto
// this object's position. Hands and feet drive Full Body IK; the head drives
// Look-At IK. It does not touch item grips.
// ---------------------------------------------------------------------------
#include "okay/Scene/Component.hpp"
#include "okay/Scene/GameObject.hpp"
#include "okay/Scene/Scene.hpp"
#include "okay/Scene/Transform.hpp"
#include "okay/Components/FullBodyIK.hpp"
#include "okay/Components/LookAtIK.hpp"
#include "okay/Math/Mathf.hpp"
#include <cmath>
#include <string>

namespace okay {

class IKTarget : public Component {
public:
    enum Limb { LeftHand = 0, RightHand, LeftFoot, RightFoot, Head, LimbCount };

    int ExecutionOrder() const override { return 70; }   // set the goal before the solvers

    int limb = LeftHand;
    std::string characterName;
    GameObject* character = nullptr;
    float weight = 1.0f;
    float blendTime = 0.2f;

    static const char* LimbName(int limb) {
        switch (limb) {
            case LeftHand: return "Left Hand";
            case RightHand: return "Right Hand";
            case LeftFoot: return "Left Foot";
            case RightFoot: return "Right Foot";
            case Head: return "Head";
            default: return "Limb";
        }
    }

    /// An empty object in `scene`, wired to `character` and `limb`, sitting in
    /// front of the character so it can be dragged.
    static GameObject* Spawn(Scene& scene, GameObject* character, int limb) {
        if (limb < 0 || limb >= LimbCount) limb = LeftHand;
        const char* label = LimbName(limb);
        GameObject* g = scene.CreateGameObject(std::string(label) + " IK");
        auto* ik = g->AddComponent<IKTarget>();
        ik->limb = limb;
        if (character) {
            ik->character = character;
            ik->characterName = character->name;
            Vec3 at = character->transform ? character->transform->Position() : Vec3::Zero;
            float y = limb == Head ? 1.6f : limb == LeftFoot || limb == RightFoot ? 0.1f : 1.2f;
            float x = limb == LeftHand || limb == LeftFoot ? -0.35f : limb == RightHand || limb == RightFoot ? 0.35f : 0.0f;
            g->transform->SetPosition(at + Vec3{x, y, -0.55f});
        }
        return g;
    }

    void Start() override { Resolve(); }

    void LateUpdate(float dt) override { Apply(dt); }

    /// Edit mode: set the goal and run the solver so the limb follows while the
    /// target object is moved. Play mode uses LateUpdate plus the solver's own tick.
    void Preview(float dt) {
        Apply(dt);
        GameObject* host = character;
        if (!host) return;
        if (limb == Head) {
            if (auto* look = host->GetComponent<LookAtIK>()) look->LateUpdate(dt);
        } else if (auto* fb = host->GetComponent<FullBodyIK>()) {
            fb->LateUpdate(dt);
        }
    }

    float ShownWeight() const { return m_w; }

private:
    float m_w = 0.0f;
    bool m_have = false;

    void Resolve() {
        if (character || characterName.empty() || !gameObject || !gameObject->scene()) return;
        character = gameObject->scene()->Find(characterName);
    }

    void Apply(float dt) {
        Resolve();
        GameObject* host = character;
        if (!host || !transform) return;
        const float goal = weight < 0.0f ? 0.0f : weight;
        if (!m_have || blendTime <= 1e-4f) m_w = goal;
        else {
            const float k = 1.0f - std::exp(-dt / blendTime);
            m_w += (goal - m_w) * (k < 0.0f ? 0.0f : (k > 1.0f ? 1.0f : k));
        }
        m_have = true;
        if (limb == Head) ApplyHead(host);
        else ApplyLimb(host);
    }

    void ApplyHead(GameObject* host) {
        LookAtIK* look = host->GetComponent<LookAtIK>();
        if (!look) look = host->AddComponent<LookAtIK>();
        if (look->chain.empty() && look->chainNames.empty()) {
            if (Transform* head = FindClassified(host, FullBodyIK::Head)) {
                look->chain.push_back(head);
                if (head->gameObject) look->chainNames.push_back(head->gameObject->name);
            }
        }
        look->targetObject = transform;
        look->targetName = gameObject ? gameObject->name : std::string{};
        look->aimAtPoint = true;
        look->ikActive = m_w > 0.001f;
        look->weight = m_w;
    }

    void ApplyLimb(GameObject* host) {
        FullBodyIK* fb = host->GetComponent<FullBodyIK>();
        if (!fb) {
            fb = host->AddComponent<FullBodyIK>();
            fb->MapBones(false);
        }
        const int li = limb == LeftHand ? FullBodyIK::LeftHand
                     : limb == RightHand ? FullBodyIK::RightHand
                     : limb == LeftFoot ? FullBodyIK::LeftFoot
                                        : FullBodyIK::RightFoot;
        FullBodyIK::Effector& e = fb->effectors[li];
        e.target = transform;
        e.targetName = gameObject ? gameObject->name : std::string{};
        e.enabled = m_w > 0.001f;
        e.positionWeight = m_w;
        e.exactReach = true;
        const float goalW = weight < 0.0f ? 0.0f : weight;
        if (blendTime <= 1e-3f || std::fabs(m_w - goalW) < 0.02f) {
            e.targetBlendTime = 0.0f;
            fb->SnapEffector(li);
        } else e.targetBlendTime = blendTime;
    }

    static Transform* FindClassified(GameObject* host, int boneId) {
        if (!host || !host->scene()) return nullptr;
        for (const auto& up : host->scene()->Objects()) {
            if (!up || !up->IsSelfOrDescendantOf(host)) continue;
            if (FullBodyIK::Classify(up->name) == boneId) return up->transform;
        }
        return nullptr;
    }
};

} // namespace okay

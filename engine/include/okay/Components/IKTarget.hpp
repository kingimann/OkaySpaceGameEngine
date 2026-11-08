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
            // In front of the character's facing, not world -Z. A turned
            // character used to get the target off to its side.
            Quat rot = character->transform ? character->transform->Rotation() : Quat::Identity;
            Vec3 fwd = rot * Vec3{0, 0, -1};
            Vec3 right = rot * Vec3{1, 0, 0};
            g->transform->SetPosition(at + right * x + Vec3{0, y, 0} + fwd * 0.55f);
        }
        return g;
    }

    void Start() override { Resolve(); }

    void LateUpdate(float dt) override { Apply(dt); }

    void OnDisable() override { ReleaseDrive(); }
    void OnDestroy() override { ReleaseDrive(); }
    void OnObjectsDestroyed(const std::unordered_set<const GameObject*>& dying) override {
        if (character && dying.count(character)) character = nullptr;
        // The body is going away with the effector. Don't write into it.
        if (m_drivenHost && dying.count(m_drivenHost)) {
            m_drivenHost = nullptr;
            m_drivenLimb = -1;
        }
    }

    /// Drop the reach this object was driving (limb switched, disabled, or deleted).
    void ReleaseDrive() {
        Release(m_drivenHost, m_drivenLimb);
        m_drivenHost = nullptr;
        m_drivenLimb = -1;
    }

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
    bool m_pullSet = false;
    int m_drivenLimb = -1;
    GameObject* m_drivenHost = nullptr;

    void Resolve() {
        if (character || characterName.empty() || !gameObject || !gameObject->scene()) return;
        character = gameObject->scene()->Find(characterName);
    }

    void Apply(float dt) {
        Resolve();
        GameObject* host = character;
        if (!host || !transform) return;
        const float goal = weight < 0.0f ? 0.0f : weight;
        // Ease in from rest. The first frame used to snap to the full weight,
        // so Blend time did nothing until the weight was changed again.
        if (!m_have) m_w = 0.0f;
        m_have = true;
        if (blendTime <= 1e-4f) m_w = goal;
        else {
            const float k = 1.0f - std::exp(-dt / blendTime);
            m_w += (goal - m_w) * (k < 0.0f ? 0.0f : (k > 1.0f ? 1.0f : k));
        }
        if (host != m_drivenHost || limb != m_drivenLimb) {
            Release(m_drivenHost, m_drivenLimb);
            m_drivenHost = host;
            m_drivenLimb = limb;
        }
        if (limb == Head) ApplyHead(host);
        else ApplyLimb(host);
    }

    static int EffectorIndex(int limb) {
        return limb == LeftHand ? FullBodyIK::LeftHand
             : limb == RightHand ? FullBodyIK::RightHand
             : limb == LeftFoot ? FullBodyIK::LeftFoot
             : limb == RightFoot ? FullBodyIK::RightFoot
             : -1;
    }

    void Release(GameObject* host, int driven) {
        if (!host || driven < 0) return;
        if (driven == Head) {
            if (LookAtIK* look = host->GetComponent<LookAtIK>()) {
                if (look->targetObject == transform) {
                    look->targetObject = nullptr;
                    look->ikActive = false;
                    look->weight = 0.0f;
                }
            }
            return;
        }
        const int li = EffectorIndex(driven);
        FullBodyIK* fb = host->GetComponent<FullBodyIK>();
        if (!fb || li < 0) return;
        FullBodyIK::Effector& e = fb->effectors[li];
        if (e.target != transform) return;
        e.enabled = false;
        e.positionWeight = 0.0f;
        e.exactReach = false;
        e.target = nullptr;
        e.targetName.clear();
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
            fb->bodyPull = 0.2f;
        } else if (!m_pullSet && std::fabs(fb->bodyPull - 0.6f) < 1e-4f) {
            // The stock pull dips the whole body for a target just past the hand.
            fb->bodyPull = 0.2f;
        }
        m_pullSet = true;
        const int li = EffectorIndex(limb);
        if (li < 0) return;
        FullBodyIK::Effector& e = fb->effectors[li];
        e.target = transform;
        e.targetName = gameObject ? gameObject->name : std::string{};
        e.enabled = m_w > 0.001f;
        e.positionWeight = m_w;
        e.exactReach = m_w > 0.001f;
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

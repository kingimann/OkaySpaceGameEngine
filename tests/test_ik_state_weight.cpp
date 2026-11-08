// The Animator's IK weight reaches Look-At, Aim, Limb and Chain, not only the
// feet. Death, sit, swim and emotes zero look and aim. Also the action
// framework (play death, hit, roll, sit and stand back up) and the #503
// pieces: an IK curve, an additive layer, a mirrored pose, per-state root motion.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Animation/HumanoidLibrary.hpp"
#include "okay/Animation/HumanoidActions.hpp"
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Components/LookAtIK.hpp"
#include "okay/Components/AimIK.hpp"
#include "okay/Components/LimbIK.hpp"
#include "okay/Components/ChainIK.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Components/Seat.hpp"
#include <cmath>
#include <string>

using namespace okay;

static void Tick(Scene& s, int n, float dt = 1.0f / 60.0f) {
    for (int i = 0; i < n; ++i) s.Update(dt);
}
static ModelAnimator::Clip Hold(const char* name, const char* node, float x, float len = 1.0f) {
    ModelAnimator::Clip c; c.name = name;
    ModelAnimator::NodeClip nc; nc.node = node;
    nc.clip.AddKey("position.x", 0.0f, x);
    nc.clip.AddKey("position.x", len, x);
    c.nodes.push_back(nc);
    return c;
}
static float HeadYaw(const Transform* head) {
    Vec3 f = (head->Rotation() * Vec3::Forward).Normalized();
    return std::atan2(f.x, f.z) * 57.29578f;
}
static const AnimStateMachine::State* Find(const AnimStateMachine* sm, const char* name) {
    for (const auto& st : sm->states) if (st.name == name) return &st;
    return nullptr;
}

int main() {
    RUN_SUITE("ik_state_weight");

    // ---- IK curve: empty is the slider, keys lerp, the slider is the gain ----
    {
        using K = AnimStateMachine::State::IkKey;
        CHECK(AnimStateMachine::SampleIkCurve({}, 0.4f, 0.5f) == 0.5f);
        std::vector<K> keys{{0.0f, 0.0f}, {1.0f, 1.0f}};
        CHECK(std::fabs(AnimStateMachine::SampleIkCurve(keys, 0.25f, 1.0f) - 0.25f) < 1e-4f);
        CHECK(std::fabs(AnimStateMachine::SampleIkCurve(keys, 0.25f, 0.5f) - 0.125f) < 1e-4f);
    }

    // ---- Look-At follows the state. Idle tracks; Wave and Death do not. ----
    {
        Scene s("look"); s.physicsEnabled = false;
        GameObject* npc = s.CreateGameObject("NPC");
        GameObject* head = s.CreateGameObject("Head");
        head->transform->SetParent(npc->transform, false);
        head->transform->localPosition = {0, 1.6f, 0};
        auto* ma = npc->AddComponent<ModelAnimator>();
        ma->autoPlay = false; ma->blendTime = 0.0f;
        ma->clips = {Hold("Idle", "Head", 0), Hold("Wave", "Head", 0), Hold("Death", "Head", 0)};
        auto* sm = npc->AddComponent<AnimStateMachine>();
        AnimStateMachine::State idle; idle.name = "Idle"; idle.clip = "Idle";
        AnimStateMachine::State wave; wave.name = "Wave"; wave.clip = "Wave"; wave.lookWeight = 0.0f;
        AnimStateMachine::State death; death.name = "Death"; death.clip = "Death";
        death.ikWeight = 0.0f; death.lookWeight = 0.0f;
        sm->states = {idle, wave, death}; sm->entry = "Idle";
        auto* look = npc->AddComponent<LookAtIK>();
        look->chain = {head->transform};
        look->target = {6.0f, 1.6f, 0.0f};
        look->smoothing = 0.0f; look->clampAngle = 0.0f; look->maxAngle = 180.0f;
        auto* aim = npc->AddComponent<AimIK>();
        aim->bone = head->transform; aim->target = look->target; aim->smoothing = 0.0f; aim->maxAngle = 180.0f;
        GameObject *up = s.CreateGameObject("Up"), *lo = s.CreateGameObject("Lo"), *tip = s.CreateGameObject("Tip");
        up->transform->SetParent(npc->transform, false); up->transform->localPosition = {0, 1, 0};
        lo->transform->SetParent(up->transform, false); lo->transform->localPosition = {0, -0.4f, 0};
        tip->transform->SetParent(lo->transform, false); tip->transform->localPosition = {0, -0.4f, 0};
        auto* limb = npc->AddComponent<LimbIK>();
        limb->upper = up->transform; limb->lower = lo->transform; limb->end = tip->transform;
        limb->target = {1, 1, 1}; limb->weight = 1.0f;
        auto* chain = npc->AddComponent<ChainIK>();
        chain->bones = {up->transform, lo->transform, tip->transform};
        chain->target = {1, 1, 1};
        AnimStateMachine::State half; half.name = "Half"; half.clip = "Idle"; half.ikWeight = 0.25f;
        sm->states.push_back(half);

        s.Start(); Tick(s, 4);
        const float tracking = std::fabs(HeadYaw(head->transform));
        CHECK(tracking > 40.0f);
        CHECK(look->stateWeight > 0.9f);

        sm->GoTo("Wave", 0.0f); Tick(s, 2);
        CHECK(sm->Current() == "Wave");
        CHECK(look->stateWeight < 1e-3f);
        CHECK(aim->stateWeight < 1e-3f);
        CHECK(std::fabs(HeadYaw(head->transform)) < 8.0f);

        sm->GoTo("Death", 0.0f); Tick(s, 2);
        CHECK(look->stateWeight < 1e-3f);
        CHECK(std::fabs(HeadYaw(head->transform)) < 8.0f);

        sm->GoTo("Half", 0.0f); Tick(s, 2);
        CHECK(std::fabs(limb->stateWeight - 0.25f) < 1e-3f);
        CHECK(std::fabs(chain->stateWeight - 0.25f) < 1e-3f);
        CHECK(std::fabs(sm->IKWeight() - 0.25f) < 1e-3f);
        CHECK(std::fabs(sm->LookIKWeight() - 0.25f) < 1e-3f);
        AnimStateMachine::State sit; sit.name = "SitHands"; sit.clip = "Idle"; sit.ikWeight = 0.0f; sit.fullBodyIkWeight = 1.0f;
        sm->states.push_back(sit);
        sm->GoTo("SitHands", 0.0f); Tick(s, 2);
        CHECK(sm->IKWeight() < 1e-3f);
        CHECK(limb->stateWeight > 0.9f);
        CHECK(chain->stateWeight > 0.9f);
    }

    // ---- Built-in graph: gestures and the action states zero look / aim ----
    {
        Scene s("acts"); s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Player");
        auto* ch = go->AddComponent<Character>();
        ch->Apply();
        AnimStateMachine* sm = HumanoidLibrary::EnsureAnimator(go);
        CHECK(sm != nullptr);
        if (sm) {
            HumanoidActions::MergeMissing(*sm, ch, true);
            const AnimStateMachine::State* wave = Find(sm, "Wave");
            const AnimStateMachine::State* death = Find(sm, "Death");
            const AnimStateMachine::State* sit = Find(sm, "Sit");
            const AnimStateMachine::State* swim = Find(sm, "Swim");
            const AnimStateMachine::State* nod = Find(sm, "Emote_Nod");
            CHECK(wave && wave->lookWeight == 0.0f);
            CHECK(death && death->lookWeight == 0.0f && death->ikWeight == 0.0f);
            CHECK(sit && sit->lookWeight == 0.0f);
            CHECK(swim && swim->lookWeight == 0.0f);
            CHECK(nod && nod->lookWeight == 0.0f);
            s.Start(); Tick(s, 2);
            CHECK(HumanoidActions::PlayDeath(go, nullptr));
            CHECK(sm->Current() == "Death");
            CHECK(sm->GetBool("dead"));
            sm->SetBool("dead", false);
            sm->GoTo("Idle", 0.0f);
            CHECK(HumanoidActions::HitReact(go, 0, false));
            bool hit = false;
            for (AnimStateMachine* layer : go->GetComponents<AnimStateMachine>())
                if (layer->layer > 0 && layer->Current() == "Hit") hit = true;
            CHECK(hit);
            CHECK(HumanoidActions::RequestRoll(go, 0));
            CHECK(sm->Current() == "Roll");
            GameObject* chair = s.CreateGameObject("Chair");
            chair->AddComponent<Seat>();
            CHECK(HumanoidActions::RequestSit(go, chair, 1));
            CHECK(sm->Current() == "SitDown");
            HumanoidActions::RequestStandUp(go);
            CHECK(sm->Current() == "StandUp");
        }
    }

    // ---- Additive layer adds the clip's change from its first frame ----
    {
        Scene s("add"); s.physicsEnabled = false;
        GameObject* model = s.CreateGameObject("M");
        GameObject* arm = s.CreateGameObject("arm");
        arm->transform->SetParent(model->transform, false);
        auto* ma = model->AddComponent<ModelAnimator>();
        ma->autoPlay = false;
        ModelAnimator::Clip c; c.name = "bump";
        ModelAnimator::NodeClip nc; nc.node = "arm";
        nc.clip.AddKey("position.x", 0.0f, 0.0f);
        nc.clip.AddKey("position.x", 1.0f, 2.0f);
        c.nodes.push_back(nc);
        ma->clips = {c};
        arm->transform->localPosition = {0, 0, 0};
        ma->PoseLayer({{0, 0.5f}}, {1.0f}, "", 1.0f, true);
        CHECK(std::fabs(arm->transform->localPosition.x - 1.0f) < 1e-3f);
    }

    // ---- A mirrored state swaps left and right ----
    {
        Scene s("mir"); s.physicsEnabled = false;
        GameObject* model = s.CreateGameObject("M");
        GameObject* L = s.CreateGameObject("LeftArm");
        GameObject* R = s.CreateGameObject("RightArm");
        L->transform->SetParent(model->transform, false);
        R->transform->SetParent(model->transform, false);
        auto* ma = model->AddComponent<ModelAnimator>();
        ma->autoPlay = false; ma->blendTime = 0.0f;
        ModelAnimator::Clip c; c.name = "pose";
        ModelAnimator::NodeClip nl; nl.node = "LeftArm";
        nl.clip.AddKey("position.x", 0.0f, 1.0f);
        nl.clip.AddKey("position.x", 1.0f, 1.0f);
        ModelAnimator::NodeClip nr; nr.node = "RightArm";
        nr.clip.AddKey("position.x", 0.0f, 0.0f);
        nr.clip.AddKey("position.x", 1.0f, 0.0f);
        c.nodes = {nl, nr};
        ma->clips = {c};
        auto* sm = model->AddComponent<AnimStateMachine>();
        AnimStateMachine::State st; st.name = "Pose"; st.clip = "pose"; st.mirror = true; st.loop = true;
        sm->states = {st};
        s.Start(); Tick(s, 3);
        CHECK(sm->Current() == "Pose");
        CHECK(std::fabs(R->transform->localPosition.x - (-1.0f)) < 1e-3f);
        CHECK(std::fabs(L->transform->localPosition.x) < 1e-3f);
    }

    // ---- Per-state root motion: On moves the body, Off does not ----
    {
        Scene s("root"); s.physicsEnabled = false;
        GameObject* model = s.CreateGameObject("M");
        GameObject* hips = s.CreateGameObject("Hips");
        hips->transform->SetParent(model->transform, false);
        auto* ma = model->AddComponent<ModelAnimator>();
        ma->autoPlay = false; ma->blendTime = 0.0f; ma->rootMotion = false;
        ma->rootMotionNode = "Hips";
        ModelAnimator::Clip c; c.name = "step";
        ModelAnimator::NodeClip nc; nc.node = "Hips";
        nc.clip.AddKey("position.z", 0.0f, 0.0f);
        nc.clip.AddKey("position.z", 1.0f, 2.0f);
        c.nodes.push_back(nc);
        ma->clips = {c};
        auto* sm = model->AddComponent<AnimStateMachine>();
        AnimStateMachine::State on; on.name = "On"; on.clip = "step"; on.loop = false; on.rootMotion = 1;
        AnimStateMachine::State off; off.name = "Off"; off.clip = "step"; off.loop = false; off.rootMotion = 0;
        sm->states = {on, off}; sm->entry = "On";
        s.Start(); Tick(s, 15);
        CHECK(std::fabs(model->transform->Position().z) > 0.2f);
        const float moved = model->transform->Position().z;
        sm->GoTo("Off", 0.0f);
        model->transform->localPosition = {0, 0, 0};
        Tick(s, 15);
        CHECK(sm->Current() == "Off");
        CHECK(std::fabs(model->transform->Position().z) < 0.05f);
        CHECK(moved > 0.2f);
    }

    TEST_MAIN_RESULT();
}

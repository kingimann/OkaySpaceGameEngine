#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include <cmath>

using namespace okay;

// A model whose one bone sits at x = 0 / 1 / 2 in the idle / walk / run clips
// (and a 1-second one-shot "attack" at x = 9), so the bone's x tells which clip,
// or which mix, the state machine is playing.
static ModelAnimator::Clip ConstClip(const char* name, float x, float len) {
    ModelAnimator::Clip c; c.name = name;
    ModelAnimator::NodeClip nc{"bone", AnimationClip{}};
    nc.clip.AddKey("position.x", 0.0f, x);
    nc.clip.AddKey("position.x", len, x);
    c.nodes.push_back(nc);
    return c;
}
static void Tick(Scene& s, int frames, float dt = 1.0f / 60.0f) { for (int i = 0; i < frames; ++i) s.Update(dt); }

int main() {
    RUN_SUITE("anim_state_machine");
    Scene scene;
    GameObject* model = scene.CreateGameObject("Model");
    GameObject* bone = scene.CreateGameObject("bone");
    bone->transform->SetParent(model->transform, false);
    auto* ma = model->AddComponent<ModelAnimator>();
    ma->autoPlay = false;
    ma->blendTime = 0.0f;
    ma->clips = {ConstClip("idle", 0, 1), ConstClip("walk", 1, 1), ConstClip("run", 2, 1), ConstClip("attack", 9, 1)};
    auto* sm = model->AddComponent<AnimStateMachine>();

    // State 0: a locomotion blend tree on "speed"; state 1: a one-shot attack.
    AnimStateMachine::State loco; loco.name = "Locomotion"; loco.blendTree = true; loco.blendParam = "speed";
    loco.motions = {{"idle", 0.0f}, {"run", 6.0f}, {"walk", 2.0f}};   // unsorted on purpose
    AnimStateMachine::State attack; attack.name = "Attack"; attack.clip = "attack"; attack.loop = false;
    AnimStateMachine::Transition back; back.to = "Locomotion"; back.cond = AnimStateMachine::Cond::Always;
    back.hasExitTime = true; back.exitTime = 0.9f; back.blend = 0.0f;
    attack.transitions.push_back(back);
    sm->states = {loco, attack};
    // Any State -> Attack on the "attack" trigger, but only while "armed".
    AnimStateMachine::Transition any; any.to = "Attack"; any.cond = AnimStateMachine::Cond::Trigger;
    any.param = "attack"; any.blend = 0.0f;
    any.extra.push_back({AnimStateMachine::Cond::BoolTrue, "armed", 0.0f});
    sm->anyState.push_back(any);

    scene.Start();
    Tick(scene, 2);
    CHECK(sm->Current() == "Locomotion");

    // Blend: speed 1 is halfway between idle (0) and walk (2) -> bone x = 0.5.
    sm->SetFloat("speed", 1.0f); Tick(scene, 3);
    CHECK_NEAR(bone->transform->localPosition.x, 0.5f, 1e-3f);
    int lo, hi; float w; sm->BlendInfo(lo, hi, w);
    CHECK(lo == 0 && hi == 2 && std::fabs(w - 0.5f) < 1e-4f);
    // Speed 4: halfway between walk (2) and run (6) -> x = 1.5. Past the top clamps to run.
    sm->SetFloat("speed", 4.0f); Tick(scene, 2);
    CHECK_NEAR(bone->transform->localPosition.x, 1.5f, 1e-3f);
    sm->SetFloat("speed", 50.0f); Tick(scene, 2);
    CHECK_NEAR(bone->transform->localPosition.x, 2.0f, 1e-3f);

    // The trigger alone doesn't fire (the extra "armed" condition fails)...
    sm->SetTrigger("attack"); Tick(scene, 2);
    CHECK(sm->Current() == "Locomotion");
    // ...with "armed" it does, and the attack clip plays.
    sm->SetBool("armed", true); Tick(scene, 3);
    CHECK(sm->Current() == "Attack");
    CHECK(sm->Previous() == "Locomotion");
    CHECK_NEAR(bone->transform->localPosition.x, 9.0f, 1e-3f);
    // Exit time 0.9 of a 1 s clip: still attacking at ~0.5 s, back to locomotion after ~0.9 s.
    Tick(scene, 30);
    CHECK(sm->Current() == "Attack");
    CHECK(sm->NormalizedTime() > 0.4f && sm->NormalizedTime() < 0.7f);
    Tick(scene, 30);
    CHECK(sm->Current() == "Locomotion");

    // Enter/exit callbacks.
    std::string log;
    sm->onStateEnter = [&](const std::string& n) { log += "+" + n; };
    sm->onStateExit  = [&](const std::string& n) { log += "-" + n; };
    sm->SetTrigger("attack"); Tick(scene, 2);
    CHECK(log == "-Locomotion+Attack");

    // PickMotions edge cases.
    AnimStateMachine::PickMotions(loco, -5.0f, lo, hi, w);
    CHECK(lo == 0 && hi == 0);
    AnimStateMachine::State none; AnimStateMachine::PickMotions(none, 1.0f, lo, hi, w);
    CHECK(lo == -1 && hi == -1);

    // Round trip through the scene format keeps blend trees, exit times,
    // extra conditions and Any State transitions.
    std::string text = SceneSerializer::Serialize(scene);
    Scene loaded;
    CHECK(SceneSerializer::Deserialize(loaded, text));
    AnimStateMachine* sm2 = nullptr;
    for (const auto& up : loaded.Objects()) if (auto* m = up->GetComponent<AnimStateMachine>()) sm2 = m;
    CHECK(sm2 != nullptr);
    if (sm2) {
        CHECK(sm2->states.size() == 2);
        CHECK(sm2->states[0].blendTree && sm2->states[0].blendParam == "speed" && sm2->states[0].motions.size() == 3);
        CHECK(sm2->states[0].motions[1].clip == "run" && sm2->states[0].motions[1].threshold == 6.0f);
        CHECK(sm2->states[1].transitions.size() == 1 && sm2->states[1].transitions[0].hasExitTime);
        CHECK_NEAR(sm2->states[1].transitions[0].exitTime, 0.9f, 1e-5f);
        CHECK(sm2->states[1].transitions[0].cond == AnimStateMachine::Cond::Always);
        CHECK(sm2->anyState.size() == 1 && sm2->anyState[0].extra.size() == 1);
        CHECK(sm2->anyState[0].extra[0].param == "armed");
    }

    // ---- Keep phase: Walk -> Run picks up where the walk cycle is (its loops
    // wrap), instead of restarting the stride; without it the target starts at
    // the transition's offset.
    {
        Scene s2;
        GameObject* m2 = s2.CreateGameObject("Model");
        GameObject* b2 = s2.CreateGameObject("bone");
        b2->transform->SetParent(m2->transform, false);
        auto* ma2 = m2->AddComponent<ModelAnimator>();
        ma2->autoPlay = false; ma2->blendTime = 0.0f;
        ma2->clips = {ConstClip("walk", 1, 1), ConstClip("run", 2, 1)};
        auto* sm3 = m2->AddComponent<AnimStateMachine>();
        AnimStateMachine::State walk; walk.name = "Walk"; walk.clip = "walk";
        AnimStateMachine::State run;  run.name = "Run";   run.clip = "run";
        AnimStateMachine::Transition go; go.to = "Run"; go.cond = AnimStateMachine::Cond::Trigger; go.param = "go";
        go.blend = 0.0f; go.syncPhase = true; go.offset = 0.2f;   // the offset is ignored while keeping phase
        walk.transitions.push_back(go);
        AnimStateMachine::Transition back2; back2.to = "Walk"; back2.cond = AnimStateMachine::Cond::Trigger; back2.param = "back";
        back2.blend = 0.0f; back2.offset = 0.2f;
        run.transitions.push_back(back2);
        sm3->states = {walk, run};
        s2.Start();
        Tick(s2, 1 + 60 + 42);   // 1.7 s into the 1 s walk cycle: phase 0.7
        CHECK(sm3->Current() == "Walk");
        sm3->SetTrigger("go"); Tick(s2, 1);
        CHECK(sm3->Current() == "Run");
        CHECK(sm3->NormalizedTime() > 0.68f && sm3->NormalizedTime() < 0.75f);
        CHECK(ma2->Time() > 0.68f && ma2->Time() < 0.75f);
        // Back without keeping phase: the walk starts at its 0.2 offset.
        Tick(s2, 10);
        sm3->SetTrigger("back"); Tick(s2, 1);
        CHECK(sm3->Current() == "Walk");
        CHECK(sm3->NormalizedTime() > 0.19f && sm3->NormalizedTime() < 0.25f);
        // The flag rides along in the scene file.
        std::string t2 = SceneSerializer::Serialize(s2);
        Scene s3("s3");
        CHECK(SceneSerializer::Deserialize(s3, t2));
        auto* sm4 = s3.Find("Model")->GetComponent<AnimStateMachine>();
        CHECK(sm4 && sm4->states.size() == 2 && sm4->states[0].transitions.size() == 1);
        CHECK(sm4 && sm4->states[0].transitions[0].syncPhase && !sm4->states[1].transitions[0].syncPhase);
    }
    TEST_MAIN_RESULT();
}

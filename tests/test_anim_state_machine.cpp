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
    // ---- Editor queries: which parameters the graph reads, and what is wrong
    // with it (the Animator window's "unused" markers and Problems list).
    {
        CHECK(sm->UsesParam("speed"));    // the Locomotion blend tree
        CHECK(sm->UsesParam("attack"));   // the Any State trigger
        CHECK(sm->UsesParam("armed"));    // an extra condition
        CHECK(!sm->UsesParam("nope"));
        CHECK(!sm->UsesParam(""));

        Scene s4;
        GameObject* m4 = s4.CreateGameObject("Model");
        auto* ma4 = m4->AddComponent<ModelAnimator>();
        ma4->clips = {ConstClip("idle", 0, 1), ConstClip("hit", 9, 1)};
        auto* sm5 = m4->AddComponent<AnimStateMachine>();
        sm5->params = {{"speed", 0}, {"go", 2}};
        AnimStateMachine::State a; a.name = "A"; a.clip = "idle";
        AnimStateMachine::State b; b.name = "B"; b.clip = "ghost";        // not in the library
        AnimStateMachine::State c; c.name = "C";                          // no clip, nothing leads here
        AnimStateMachine::State d; d.name = "D"; d.clip = "hit";
        AnimStateMachine::Transition atOnce; atOnce.to = "B"; atOnce.cond = AnimStateMachine::Cond::Always;
        AnimStateMachine::Transition shadowed; shadowed.to = "D"; shadowed.cond = AnimStateMachine::Cond::Trigger; shadowed.param = "go";
        a.transitions = {atOnce, shadowed};
        AnimStateMachine::Transition nowhere; nowhere.to = "Nowhere"; nowhere.cond = AnimStateMachine::Cond::OnClipEnd;
        AnimStateMachine::Transition undeclared; undeclared.to = "A"; undeclared.cond = AnimStateMachine::Cond::FloatGreater; undeclared.param = "mystery";
        b.transitions = {nowhere, undeclared};
        AnimStateMachine::Transition noParam; noParam.to = "D"; noParam.cond = AnimStateMachine::Cond::Trigger;   // param left empty
        sm5->anyState = {noParam};
        sm5->states = {a, b, c, d};
        sm5->entry = "Z";                                                 // does not exist
        auto has = [](const std::vector<AnimStateMachine::Issue>& v, const char* needle, int st, int tr, bool any) {
            for (const auto& is : v)
                if (is.text.find(needle) != std::string::npos && is.state == st && is.transition == tr && is.any == any) return true;
            return false;
        };
        std::vector<AnimStateMachine::Issue> issues = sm5->Validate([&](const std::string& n) { return ma4->FindClip(n) >= 0; });
        CHECK(has(issues, "entry state \"Z\"", -1, -1, false));
        CHECK(has(issues, "clip \"ghost\" is missing", 1, -1, false));
        CHECK(has(issues, "plays no clip", 2, -1, false));
        CHECK(has(issues, "nothing leads here", 2, -1, false));
        CHECK(!has(issues, "nothing leads here", 3, -1, false));          // D: Any State and A lead to it
        for (const auto& is : issues)                                     // a note, not a warning: scripts can anim_goto a state
            if (is.text.find("nothing leads here") != std::string::npos) CHECK(is.severity == 0);
        for (const auto& is : issues)
            if (is.text.find("no such target") != std::string::npos || is.text.find("has no parameter") != std::string::npos) CHECK(is.severity == 2);
        CHECK(has(issues, "fires at once", 0, 0, false));
        CHECK(has(issues, "never tested", 0, 1, false));
        CHECK(has(issues, "no such target state", 1, 0, false));
        CHECK(has(issues, "\"mystery\" is not declared", 1, 1, false));
        CHECK(has(issues, "has no parameter", -1, 0, true));
        // Without a clip check nothing is "missing"; a valid machine reports nothing.
        issues = sm5->Validate();
        CHECK(!has(issues, "is missing", 1, -1, false));
        sm5->entry = "A"; sm5->states[1].clip = "hit"; sm5->states[2].clip = "idle";
        sm5->states[0].transitions.erase(sm5->states[0].transitions.begin());
        sm5->states[1].transitions = {};
        AnimStateMachine::Transition toC; toC.to = "C"; toC.cond = AnimStateMachine::Cond::OnClipEnd;
        AnimStateMachine::Transition toB; toB.to = "B"; toB.cond = AnimStateMachine::Cond::OnClipEnd;
        sm5->states[3].transitions = {toC};
        sm5->states[2].transitions = {toB};   // every state is now led to
        sm5->anyState[0].param = "go";
        CHECK(sm5->Validate([&](const std::string& n) { return ma4->FindClip(n) >= 0; }).empty());
    }

    // ---- Parameter defaults: applied when the machine starts, unless gameplay
    // already set the value; bools from non-zero; triggers have none. Defaults,
    // layer names and the Any State node position ride along in the scene file.
    {
        Scene s5;
        GameObject* m5 = s5.CreateGameObject("Model");
        GameObject* b5 = s5.CreateGameObject("bone");
        b5->transform->SetParent(m5->transform, false);
        auto* ma5 = m5->AddComponent<ModelAnimator>();
        ma5->autoPlay = false; ma5->blendTime = 0.0f;
        ma5->clips = {ConstClip("idle", 0, 1), ConstClip("run", 2, 1)};
        auto* sm6 = m5->AddComponent<AnimStateMachine>();
        sm6->params = {{"speed", 0, 3.0f}, {"grounded", 1, 1.0f}, {"mode", 3, 2.0f}, {"jump", 2, 1.0f}, {"preset", 0, 5.0f}};
        sm6->anyX = 12.0f; sm6->anyY = -34.0f;
        AnimStateMachine::State idle; idle.name = "Idle"; idle.clip = "idle";
        AnimStateMachine::State run; run.name = "Run"; run.clip = "run";
        AnimStateMachine::Transition go; go.to = "Run"; go.cond = AnimStateMachine::Cond::FloatGreater; go.param = "speed"; go.value = 1.0f; go.blend = 0.0f;
        idle.transitions.push_back(go);
        sm6->states = {idle, run};
        auto* lay = m5->AddComponent<AnimStateMachine>();
        lay->layer = 1; lay->layerName = "Upper body"; lay->maskBone = "bone";
        AnimStateMachine::State empty; empty.name = "Empty";
        lay->states = {empty};
        sm6->SetFloat("preset", 9.0f);   // set before the start: the default must not override it
        s5.Start();
        Tick(s5, 3);
        CHECK_NEAR(sm6->GetFloat("speed"), 3.0f, 1e-6f);
        CHECK(sm6->GetBool("grounded"));
        CHECK(sm6->GetInt("mode") == 2);
        CHECK_NEAR(sm6->GetFloat("preset"), 9.0f, 1e-6f);
        CHECK(sm6->Current() == "Run");   // speed defaulted to 3 > 1: the transition fired
        // The jump trigger is still unset: a trigger default means nothing.
        AnimStateMachine::Transition jt; jt.to = "Idle"; jt.cond = AnimStateMachine::Cond::Trigger; jt.param = "jump"; jt.blend = 0.0f;
        sm6->states[1].transitions.push_back(jt);
        Tick(s5, 3);
        CHECK(sm6->Current() == "Run");

        std::string t5 = SceneSerializer::Serialize(s5);
        CHECK(t5.find("animsmpdef") != std::string::npos);
        CHECK(t5.find("animsmlname") != std::string::npos);
        CHECK(t5.find("animsmanypos") != std::string::npos);
        Scene s6("s6");
        CHECK(SceneSerializer::Deserialize(s6, t5));
        std::vector<AnimStateMachine*> ms = s6.Find("Model")->GetComponents<AnimStateMachine>();
        CHECK(ms.size() == 2);
        AnimStateMachine* base = nullptr; AnimStateMachine* top = nullptr;
        for (AnimStateMachine* m : ms) (m->layer == 0 ? base : top) = m;
        CHECK(base && top);
        if (base && top) {
            CHECK(base->params.size() == 5);
            CHECK_NEAR(base->params[0].def, 3.0f, 1e-6f);
            CHECK_NEAR(base->params[1].def, 1.0f, 1e-6f);
            CHECK_NEAR(base->params[2].def, 2.0f, 1e-6f);
            CHECK(base->params[1].type == 1 && base->params[1].name == "grounded");
            CHECK_NEAR(base->anyX, 12.0f, 1e-6f);
            CHECK_NEAR(base->anyY, -34.0f, 1e-6f);
            CHECK(top->layerName == "Upper body");
            CHECK(top->maskBone == "bone");
        }
        // A file from before defaults existed loads with none (every default 0).
        std::string noDef = t5;
        for (std::size_t at; (at = noDef.find("  animsmpdef")) != std::string::npos;)
            noDef.erase(at, noDef.find('\n', at) - at);
        Scene s7("s7");
        CHECK(SceneSerializer::Deserialize(s7, noDef));
        for (AnimStateMachine* m : s7.Find("Model")->GetComponents<AnimStateMachine>())
            if (m->layer == 0) { CHECK(m->params.size() == 5); CHECK(m->params[0].def == 0.0f); }
    }
    TEST_MAIN_RESULT();
}

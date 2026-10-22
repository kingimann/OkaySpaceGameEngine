#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include "okay/Components/FootIK.hpp"
#include <string>
#include <vector>
#include <cmath>

using namespace okay;

// A constant clip: `node`'s position.x held at x for `len` seconds.
static ModelAnimator::Clip Hold(const char* name, std::initializer_list<std::pair<const char*, float>> nodes, float len = 1.0f) {
    ModelAnimator::Clip c; c.name = name;
    for (auto& n : nodes) {
        ModelAnimator::NodeClip nc; nc.node = n.first;
        nc.clip.AddKey("position.x", 0.0f, n.second);
        nc.clip.AddKey("position.x", len, n.second);
        c.nodes.push_back(nc);
    }
    return c;
}
static void Tick(Scene& s, int frames, float dt = 1.0f / 60.0f) { for (int i = 0; i < frames; ++i) s.Update(dt); }

int main() {
    RUN_SUITE("anim_sm_layers");

    // ---- 2D blend weights: exact at the motions, mixed between, sum to 1.
    {
        std::vector<AnimStateMachine::Motion> ms = {
            {"idle", 0, 0}, {"fwd", 0, 1}, {"back", 0, -1}, {"left", -1, 0}, {"right", 1, 0}};
        std::vector<float> w;
        AnimStateMachine::Weights2D(ms, 0.0f, 1.0f, w);
        CHECK(std::fabs(w[1] - 1.0f) < 1e-4f);
        AnimStateMachine::Weights2D(ms, 0.0f, 0.0f, w);
        CHECK(std::fabs(w[0] - 1.0f) < 1e-4f);
        AnimStateMachine::Weights2D(ms, 0.5f, 0.5f, w);
        float sum = 0; for (float x : w) sum += x;
        CHECK(std::fabs(sum - 1.0f) < 1e-4f);
        CHECK(w[1] > 0.1f && w[4] > 0.1f && w[2] < 1e-4f && w[3] < 1e-4f);   // fwd + right, not back/left
        AnimStateMachine::Weights2D(ms, 0.0f, 3.0f, w);                      // far outside: the nearest
        CHECK(std::fabs(w[1] - 1.0f) < 1e-4f);
    }

    Scene scene; scene.physicsEnabled = false;
    GameObject* model = scene.CreateGameObject("Model");
    GameObject* hips = scene.CreateGameObject("Hips");   hips->transform->SetParent(model->transform, false);
    GameObject* leg = scene.CreateGameObject("Leg");     leg->transform->SetParent(hips->transform, false);
    GameObject* spine = scene.CreateGameObject("Spine"); spine->transform->SetParent(hips->transform, false);
    GameObject* arm = scene.CreateGameObject("Arm");     arm->transform->SetParent(spine->transform, false);
    auto* ma = model->AddComponent<ModelAnimator>();
    ma->autoPlay = false; ma->blendTime = 0.0f;
    ma->clips = {Hold("idle",  {{"Leg", 0}, {"Arm", 0}}),
                 Hold("fwd",   {{"Leg", 1}, {"Arm", 1}}),
                 Hold("right", {{"Leg", 2}, {"Arm", 2}}),
                 Hold("wave",  {{"Leg", 9}, {"Arm", 5}}, 0.5f)};

    // ---- Base: a 2D blend tree on moveX / moveZ.
    auto* sm = model->AddComponent<AnimStateMachine>();
    AnimStateMachine::State loco; loco.name = "Move"; loco.blendTree = true; loco.blendDims = 2;
    loco.blendParam = "moveX"; loco.blendParamY = "moveZ";
    loco.motions = {{"idle", 0, 0}, {"fwd", 0, 1}, {"right", 1, 0}};
    AnimStateMachine::State emote; emote.name = "Emote"; emote.clip = "wave"; emote.loop = true;
    emote.speedParam = "emoteSpeed";
    AnimStateMachine::Transition toEmote; toEmote.to = "Emote"; toEmote.cond = AnimStateMachine::Cond::IntEquals;
    toEmote.param = "mode"; toEmote.value = 2; toEmote.blend = 0.0f;
    loco.transitions.push_back(toEmote);
    AnimStateMachine::Transition back; back.to = "Move"; back.cond = AnimStateMachine::Cond::IntNotEquals;
    back.param = "mode"; back.value = 2; back.blend = 0.0f;
    emote.transitions.push_back(back);
    sm->states = {loco, emote};

    scene.Start();
    Tick(scene, 2);
    sm->SetFloat("moveX", 0.0f); sm->SetFloat("moveZ", 1.0f);
    Tick(scene, 3);
    CHECK(std::fabs(leg->transform->localPosition.x - 1.0f) < 1e-3f);    // all "fwd"
    sm->SetFloat("moveX", 0.5f); sm->SetFloat("moveZ", 0.0f);
    Tick(scene, 3);
    float lx = leg->transform->localPosition.x;
    CHECK(lx > 0.5f && lx < 1.5f);                                       // idle + right mix (0..2)
    CHECK(sm->BlendWeights().size() == 3);

    // ---- Int conditions + speed parameter.
    sm->SetInt("mode", 1);
    Tick(scene, 2);
    CHECK(sm->Current() == "Move");
    sm->SetInt("mode", 2);
    Tick(scene, 2);
    CHECK(sm->Current() == "Emote");
    sm->SetFloat("emoteSpeed", 2.0f);
    float nt0 = sm->NormalizedTime();
    Tick(scene, 6);                                                       // 0.1s at 2x of a 0.5s clip
    CHECK(std::fabs((sm->NormalizedTime() - nt0) - 0.4f) < 0.05f);
    sm->SetInt("mode", 0);
    Tick(scene, 2);
    CHECK(sm->Current() == "Move");

    // ---- Layer: plays "wave" on Spine and below only; the legs keep the base pose.
    sm->SetFloat("moveX", 0.0f); sm->SetFloat("moveZ", 1.0f);
    auto* upper = model->AddComponent<AnimStateMachine>();
    upper->layer = 1; upper->maskBone = "Spine"; upper->layerWeight = 1.0f;
    AnimStateMachine::State none; none.name = "Empty"; none.clip = "";
    AnimStateMachine::State wave; wave.name = "Wave"; wave.clip = "wave";
    AnimStateMachine::Transition doWave; doWave.to = "Wave"; doWave.cond = AnimStateMachine::Cond::Trigger;
    doWave.param = "attack"; doWave.blend = 0.0f;
    none.transitions.push_back(doWave);
    upper->states = {none, wave};
    Tick(scene, 3);
    CHECK(std::fabs(arm->transform->localPosition.x - 1.0f) < 1e-3f);    // empty layer: base pose ("fwd")
    sm->SetTrigger("attack");                                             // set on the base, seen by the layer
    Tick(scene, 3);
    CHECK(upper->Current() == "Wave");
    CHECK(std::fabs(arm->transform->localPosition.x - 5.0f) < 1e-3f);    // arm: the layer's clip
    CHECK(std::fabs(leg->transform->localPosition.x - 1.0f) < 1e-3f);    // leg: still the base
    upper->layerWeight = 0.5f;
    Tick(scene, 2);
    CHECK(std::fabs(arm->transform->localPosition.x - 3.0f) < 1e-3f);    // half way between 1 and 5

    // ---- Save / load keeps both machines, the layer settings, 2D trees and int conditions.
    std::string text = SceneSerializer::Serialize(scene);
    Scene loaded;
    CHECK(SceneSerializer::Deserialize(loaded, text));
    GameObject* m2 = loaded.Find("Model");
    CHECK(m2 != nullptr);
    if (m2) {
        auto machines = m2->GetComponents<AnimStateMachine>();
        CHECK(machines.size() == 2);
        if (machines.size() == 2) {
            CHECK(machines[0]->layer == 0 && machines[1]->layer == 1);
            CHECK(machines[1]->maskBone == "Spine" && std::fabs(machines[1]->layerWeight - 0.5f) < 1e-4f);
            CHECK(machines[1]->states.size() == 2 && machines[1]->states[1].clip == "wave");
            const auto& st = machines[0]->states[0];
            CHECK(st.blendDims == 2 && st.blendParamY == "moveZ" && st.motions.size() == 3);
            CHECK(std::fabs(st.motions[1].posY - 1.0f) < 1e-4f);
            CHECK(st.transitions[0].cond == AnimStateMachine::Cond::IntEquals);
            CHECK(machines[0]->states[1].speedParam == "emoteSpeed");
        }
    }

    // ---- State enter/exit events, per-state IK weight, transition offset.
    {
        Scene s2; s2.physicsEnabled = false;
        GameObject* m = s2.CreateGameObject("M");
        GameObject* bone = s2.CreateGameObject("Leg"); bone->transform->SetParent(m->transform, false);
        auto* a2 = m->AddComponent<ModelAnimator>();
        a2->autoPlay = false; a2->blendTime = 0.0f;
        ModelAnimator::Clip ramp; ramp.name = "ramp";              // Leg.x goes 0 -> 10 over 1s
        { ModelAnimator::NodeClip nc; nc.node = "Leg"; nc.clip.smoothKeys = false;
          nc.clip.AddKey("position.x", 0.0f, 0.0f); nc.clip.AddKey("position.x", 1.0f, 10.0f); ramp.nodes.push_back(nc); }
        a2->clips = {Hold("idle", {{"Leg", 0}}), ramp};
        auto* fik = m->AddComponent<FootIK>();
        fik->useRaycast = false;
        auto* m2sm = m->AddComponent<AnimStateMachine>();
        AnimStateMachine::State idle; idle.name = "Idle"; idle.clip = "idle";
        idle.enterEvent = "idle_in"; idle.exitEvent = "idle_out";
        AnimStateMachine::Transition go; go.to = "Jump"; go.cond = AnimStateMachine::Cond::Trigger; go.param = "jump";
        go.blend = 0.2f; go.offset = 0.5f;
        idle.transitions.push_back(go);
        AnimStateMachine::State jump; jump.name = "Jump"; jump.clip = "ramp"; jump.ikWeight = 0.0f; jump.enterEvent = "jump_in";
        m2sm->states = {idle, jump};
        std::vector<std::string> got;
        a2->onAnimEvent = [&](const std::string& e) { got.push_back(e); };
        s2.Start();
        Tick(s2, 2);
        CHECK(got.size() == 1 && got[0] == "idle_in");
        CHECK(std::fabs(m2sm->IKWeight() - 1.0f) < 1e-4f && std::fabs(fik->stateWeight - 1.0f) < 1e-4f);
        m2sm->SetTrigger("jump");
        Tick(s2, 1);
        CHECK(m2sm->Current() == "Jump");
        CHECK(got.size() == 3 && got[1] == "idle_out" && got[2] == "jump_in");
        // Started half way in (the 0.2s crossfade still eases the pose, so check the clock).
        CHECK(a2->Time() >= 0.49f);
        CHECK(m2sm->NormalizedTime() >= 0.5f);
        // The IK weight eases to 0 over the 0.2s blend.
        Tick(s2, 3);
        CHECK(m2sm->IKWeight() > 0.05f && m2sm->IKWeight() < 0.95f);
        Tick(s2, 20);
        CHECK(m2sm->IKWeight() < 1e-4f && fik->stateWeight < 1e-4f);
        CHECK(a2->ConsumeAnimEvents().size() == 3);              // queued for anim_event() too
        // History: start -> Idle -> Jump, with times.
        CHECK(m2sm->History().size() == 2);
        if (m2sm->History().size() == 2) {
            CHECK(m2sm->History()[0].from.empty() && m2sm->History()[0].to == "Idle");
            CHECK(m2sm->History()[1].from == "Idle" && m2sm->History()[1].to == "Jump");
            CHECK(m2sm->History()[1].time > m2sm->History()[0].time);
        }
        // A muted transition never fires.
        m2sm->GoTo("Idle", 0.0f);
        m2sm->states[0].transitions[0].muted = true;
        m2sm->SetTrigger("jump");
        Tick(s2, 3);
        CHECK(m2sm->Current() == "Idle");
        m2sm->states[0].transitions[0].muted = false;
        Tick(s2, 1);
        CHECK(m2sm->Current() == "Jump");                          // the trigger was still set
        // Saved and loaded.
        Scene s3;
        CHECK(SceneSerializer::Deserialize(s3, SceneSerializer::Serialize(s2)));
        if (GameObject* mm = s3.Find("M"))
            if (auto* ld = mm->GetComponent<AnimStateMachine>()) {
                CHECK(ld->states.size() == 2 && ld->states[0].enterEvent == "idle_in" && ld->states[0].exitEvent == "idle_out");
                CHECK(ld->states[1].ikWeight == 0.0f && std::fabs(ld->states[0].transitions[0].offset - 0.5f) < 1e-5f);
                CHECK(!ld->states[0].transitions[0].muted);
            }
    }

    // ---- Transition interruption (per transition, like Unity's Interruption Source).
    {
        using I = AnimStateMachine::Transition::Interrupt;
        auto run = [](I rule, const char* fire2, int framesBetween) {
            Scene sc; sc.physicsEnabled = false;
            GameObject* m = sc.CreateGameObject("M");
            GameObject* b = sc.CreateGameObject("B"); b->transform->SetParent(m->transform, false);
            auto* a = m->AddComponent<ModelAnimator>();
            a->autoPlay = false; a->blendTime = 0.0f;
            a->clips = {Hold("idle", {{"B", 0}}), Hold("dodge", {{"B", 1}}), Hold("attack", {{"B", 2}}), Hold("hit", {{"B", 3}})};
            auto* f = m->AddComponent<AnimStateMachine>();
            AnimStateMachine::State idle;   idle.name = "Idle";     idle.clip = "idle";
            AnimStateMachine::State dodge;  dodge.name = "Dodge";   dodge.clip = "dodge";
            AnimStateMachine::State attack; attack.name = "Attack"; attack.clip = "attack";
            AnimStateMachine::State hit;    hit.name = "Hit";       hit.clip = "hit";
            AnimStateMachine::Transition toDodge; toDodge.to = "Dodge"; toDodge.cond = AnimStateMachine::Cond::Trigger;
            toDodge.param = "dodge"; toDodge.blend = 0.5f; toDodge.interruption = rule;
            AnimStateMachine::Transition toHit; toHit.to = "Hit"; toHit.cond = AnimStateMachine::Cond::Trigger; toHit.param = "hit"; toHit.blend = 0.1f;
            AnimStateMachine::Transition toAttack; toAttack.to = "Attack"; toAttack.cond = AnimStateMachine::Cond::Trigger; toAttack.param = "attack"; toAttack.blend = 0.1f;
            idle.transitions = {toDodge, toHit};
            dodge.transitions = {toAttack};
            f->states = {idle, dodge, attack, hit};
            f->entry = "Idle";
            sc.Start(); Tick(sc, 2);
            f->SetTrigger("dodge"); Tick(sc, 1);
            Tick(sc, framesBetween);
            f->SetTrigger(fire2); Tick(sc, 1);
            return std::make_pair(f->Current(), f->Interruptions());
        };
        // Next State (default): the target's transitions cut in straight away.
        auto r1 = run(I::NextState, "attack", 2);
        CHECK(r1.first == "Attack"); CHECK(r1.second == 1);
        // None: Dodge plays its 0.5 s blend out first...
        auto r2 = run(I::None, "attack", 2);
        CHECK(r2.first == "Dodge"); CHECK(r2.second == 0);
        // ...and the trigger still fires once the blend is over.
        auto r3 = run(I::None, "attack", 40);
        CHECK(r3.first == "Attack"); CHECK(r3.second == 0);
        // Current State: the state being left (Idle) can cancel the dodge.
        auto r4 = run(I::CurrentState, "hit", 2);
        CHECK(r4.first == "Hit"); CHECK(r4.second == 1);
        auto r5 = run(I::CurrentState, "attack", 2);   // ...but the target's can't, yet
        CHECK(r5.first == "Dodge");
        auto r6 = run(I::CurrentThenNext, "attack", 2);
        CHECK(r6.first == "Attack");
        // Saved and loaded with the scene.
        Scene sc; sc.physicsEnabled = false;
        GameObject* m = sc.CreateGameObject("M");
        m->AddComponent<ModelAnimator>();
        auto* f = m->AddComponent<AnimStateMachine>();
        AnimStateMachine::State a; a.name = "A";
        AnimStateMachine::Transition t; t.to = "A"; t.interruption = I::NextThenCurrent;
        a.transitions = {t};
        f->states = {a};
        std::string text = SceneSerializer::Serialize(sc);
        Scene back;
        CHECK(SceneSerializer::Deserialize(back, text));
        AnimStateMachine* f2 = nullptr;
        for (const auto& up : back.Objects()) if (auto* x = up->GetComponent<AnimStateMachine>()) f2 = x;
        CHECK(f2 && !f2->states.empty() && !f2->states[0].transitions.empty() &&
              f2->states[0].transitions[0].interruption == I::NextThenCurrent);
    }

    TEST_MAIN_RESULT();
}

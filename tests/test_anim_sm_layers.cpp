#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Scene/SceneSerializer.hpp"
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
    TEST_MAIN_RESULT();
}

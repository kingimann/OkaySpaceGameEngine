// An AnimStateMachine layer used to keep a raw State* to its current / previous
// state. Adding states while it plays (an editor, a script building a machine
// at runtime) reallocates `states`, leaving those pointers dangling.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include <cmath>
#include <string>
#include <vector>

using namespace okay;

static ModelAnimator::Clip Hold(const char* name, const char* node, float x, float len = 1.0f) {
    ModelAnimator::Clip c; c.name = name;
    ModelAnimator::NodeClip nc; nc.node = node;
    nc.clip.AddKey("position.x", 0.0f, x);
    nc.clip.AddKey("position.x", len, x);
    c.nodes.push_back(nc);
    return c;
}

int main() {
    RUN_SUITE("anim_sm_realloc");

    Scene scene; scene.physicsEnabled = false;
    GameObject* model = scene.CreateGameObject("Model");
    GameObject* arm = scene.CreateGameObject("Arm");
    arm->transform->SetParent(model->transform, false);
    auto* ma = model->AddComponent<ModelAnimator>();
    ma->autoPlay = false; ma->blendTime = 0.0f;
    ma->clips = {Hold("idle", "Arm", 0.0f), Hold("wave", "Arm", 5.0f)};

    auto* base = model->AddComponent<AnimStateMachine>();
    AnimStateMachine::State idle; idle.name = "Idle"; idle.clip = "idle";
    base->states = {idle};

    auto* upper = model->AddComponent<AnimStateMachine>();
    upper->layer = 1; upper->layerWeight = 1.0f;
    AnimStateMachine::State a; a.name = "A"; a.clip = "wave"; a.loop = true;
    AnimStateMachine::State b; b.name = "B"; b.clip = "idle"; b.loop = true;
    AnimStateMachine::Transition ab; ab.to = "B"; ab.cond = AnimStateMachine::Cond::Always; ab.blend = 0.5f;   // crossfade A -> B
    a.transitions.push_back(ab);
    upper->states = {a, b};
    upper->states.shrink_to_fit();

    scene.Start();
    for (int i = 0; i < 3; ++i) scene.Update(1.0f / 60.0f);
    CHECK(upper->Current() == "B");                                   // mid-crossfade: prev = A, cur = B

    // Grow the state list: it reallocates under the playing layer.
    for (int i = 0; i < 64; ++i) {
        AnimStateMachine::State extra; extra.name = "X" + std::to_string(i); extra.clip = "idle";
        upper->states.push_back(extra);
    }
    // Refill the freed block with look-alike states that play "wave": a layer
    // still holding a pointer into the old storage would now read clip "wave"
    // for B. (Without ASan that's how the dangling pointer shows: wrong pose.)
    std::vector<AnimStateMachine::State> reuse(2);
    for (auto& r : reuse) { r.name = "B"; r.clip = "wave"; r.loop = true; }
    for (int i = 0; i < 60; ++i) scene.Update(1.0f / 60.0f);
    CHECK(upper->Current() == "B");
    CHECK(std::isfinite(arm->transform->localPosition.x));
    CHECK(std::fabs(arm->transform->localPosition.x - 0.0f) < 1e-3f);   // B plays "idle" (x = 0), not "wave" (x = 5)

    // Removing the playing state just stops the layer motion (no crash).
    upper->states.erase(upper->states.begin() + 1);
    for (int i = 0; i < 5; ++i) scene.Update(1.0f / 60.0f);
    CHECK(std::isfinite(arm->transform->localPosition.x));

    TEST_MAIN_RESULT();
}

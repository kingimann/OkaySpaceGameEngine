// An imported model with its own clips gets a full Animator graph (ClipGraph):
// every mapped clip has its state, and the parameters the player controllers
// send (speed, grounded, crouch, prone, jump) move it between them.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Animation/ClipGraph.hpp"
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include <cmath>
#include <string>

using namespace okay;

static ModelAnimator::Clip Hold(const char* name, float x, float len = 0.5f) {
    ModelAnimator::Clip c; c.name = name;
    ModelAnimator::NodeClip nc; nc.node = "Arm";
    nc.clip.AddKey("position.x", 0.0f, x);
    nc.clip.AddKey("position.x", len, x);
    c.nodes.push_back(nc);
    return c;
}

static const AnimStateMachine::State* Find(const AnimStateMachine& sm, const char* name) {
    for (const auto& st : sm.states) if (st.name == name) return &st;
    return nullptr;
}

int main() {
    RUN_SUITE("clip_graph");

    // ---- Every clip slot gets its state ----
    {
        Scene scene; scene.physicsEnabled = false;
        GameObject* model = scene.CreateGameObject("Model");
        scene.CreateGameObject("Arm")->transform->SetParent(model->transform, false);
        auto* ma = model->AddComponent<ModelAnimator>();
        ma->autoPlay = false;
        ma->clips = {Hold("Idle", 0), Hold("Walk", 1), Hold("Run", 2), Hold("Jump", 3), Hold("Fall", 4), Hold("Land", 5),
                     Hold("Crouch", 6), Hold("CrouchWalk", 7), Hold("Prone", 8), Hold("Crawl", 9), Hold("GetUp", 10)};
        ma->idleClip = "Idle"; ma->walkClip = "Walk"; ma->runClip = "Run";
        ma->jumpClip = "Jump"; ma->fallClip = "Fall"; ma->landClip = "Land";
        ma->crouchClip = "Crouch"; ma->crouchWalkClip = "CrouchWalk";
        ma->proneClip = "Prone"; ma->crawlClip = "Crawl"; ma->getUpClip = "GetUp";
        ma->walkThreshold = 1.4f; ma->runThreshold = 4.5f;
        ma->driveByMovement = true;

        AnimStateMachine* sm = ClipGraph::Ensure(model);
        CHECK(sm != nullptr);
        if (!sm) { TEST_MAIN_RESULT(); }
        CHECK(!ma->driveByMovement);   // the graph picks the clips now
        CHECK(ClipGraph::Ensure(model) == sm);   // a second call keeps it
        for (const char* n : {"Locomotion", "Crouch", "Prone", "GetUp", "Jump", "Fall", "Land"}) CHECK(Find(*sm, n) != nullptr);
        const auto* loco = Find(*sm, "Locomotion");
        CHECK(loco && loco->blendTree && loco->motions.size() == 3);
        if (loco && loco->motions.size() == 3) {
            CHECK(loco->motions[1].clip == "Walk" && std::fabs(loco->motions[1].threshold - 1.4f) < 1e-5f);
            CHECK(loco->motions[2].clip == "Run" && std::fabs(loco->motions[2].threshold - 4.5f) < 1e-5f);
        }
        const auto* prone = Find(*sm, "Prone");
        CHECK(prone && prone->ikWeight == 0.0f);
        CHECK(sm->anyState.size() == 1 && sm->anyState[0].to == "Jump");

        // Driven like a player controller drives it.
        scene.Start();
        auto step = [&](int frames) { for (int i = 0; i < frames; ++i) scene.Update(1.0f / 60.0f); };
        sm->SetBool("grounded", true);
        step(5);
        CHECK(sm->Current() == "Locomotion");
        sm->SetBool("crouch", true); step(5);
        CHECK(sm->Current() == "Crouch");
        sm->SetBool("prone", true); step(5);
        CHECK(sm->Current() == "Prone");
        sm->SetBool("prone", false); sm->SetBool("crouch", false); step(3);
        CHECK(sm->Current() == "GetUp");
        step(60);                      // the get-up plays through
        CHECK(sm->Current() == "Locomotion");
        sm->SetTrigger("jump"); step(3);
        CHECK(sm->Current() == "Jump");
    }

    // ---- Only some clips: the graph has just those states ----
    {
        Scene scene; scene.physicsEnabled = false;
        GameObject* model = scene.CreateGameObject("Model");
        auto* ma = model->AddComponent<ModelAnimator>();
        ma->clips = {Hold("Breathing", 0), Hold("Walking", 1)};
        ma->walkClip = "Walking";   // no idle mapped: the first clip stands in
        AnimStateMachine sm;
        CHECK(ClipGraph::Build(sm, *ma) == 1);
        CHECK(sm.states[0].blendTree && sm.states[0].motions[0].clip == "Breathing");
        CHECK(sm.anyState.empty() && !Find(sm, "Crouch"));
    }

    // ---- No clips: nothing to build ----
    {
        Scene scene; scene.physicsEnabled = false;
        GameObject* model = scene.CreateGameObject("Model");
        model->AddComponent<ModelAnimator>();
        CHECK(ClipGraph::Ensure(model) == nullptr);
        CHECK(model->GetComponent<AnimStateMachine>() == nullptr);
    }

    TEST_MAIN_RESULT();
}

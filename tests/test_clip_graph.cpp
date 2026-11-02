// An imported model with its own clips gets a full Animator graph (ClipGraph):
// every mapped clip has its state, and the parameters the player controllers
// send (speed, grounded, crouch, prone, jump) move it between them.
#include "test_framework.hpp"
#include "rig_harness.hpp"
#include "okay/Animation/HumanoidBones.hpp"
#include <Okay.hpp>
#include "okay/Animation/ClipGraph.hpp"
#include "okay/Components/AnimStateMachine.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include <cmath>
#include <cstdio>
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
        CHECK(ma->driveByMovement);   // still measured: empty slots and stride stay on
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

    // ---- Walk-only model under a graph: built-in idle/run/jump, stride-matched walk.
    // The same model with no graph (drive-by-movement) is the control.
    {
        const float dt = 1.0f / 60.0f;
        auto make = [](Scene& scene, bool graph) {
            scene.physicsEnabled = false;
            GameObject* go = scene.CreateGameObject("Model");
            auto* ch = go->AddComponent<Character>();
            ch->driveExternal = true;
            auto* ma = go->AddComponent<ModelAnimator>();
            ma->autoPlay = false;
            ma->driveByMovement = true;
            ma->smoothLocomotion = true;
            ma->inPlace = true;
            ma->builtinFallback = true;
            ma->strideMatch = true;
            ma->walkClipSpeed = 1.4f;
            ma->runClipSpeed = 4.0f;
            ma->clips = {Hold("Walking", 1.0f, 1.0f)};
            ma->walkClip = "Walking";
            if (graph) ClipGraph::Ensure(go);
            return ma;
        };
        auto run = [&](bool graph) {
            Scene scene;
            ModelAnimator* ma = make(scene, graph);
            CHECK(ma->driveByMovement);
            CHECK(ma->BuiltinFallbackUsable());
            scene.Start();
            auto move = [&](float spd, float vy, int frames) {
                for (int i = 0; i < frames; ++i) {
                    Vec3 p = ma->gameObject->transform->localPosition;
                    p.z -= spd * dt;
                    p.y += vy * dt;
                    ma->gameObject->transform->localPosition = p;
                    scene.Update(dt);
                }
            };
            move(0.0f, 0.0f, 30);
            CHECK(ma->BuiltinOwnsPose());
            CHECK(ma->ActiveSlot() == ModelAnimator::SlotIdle);
            move(1.4f, 0.0f, 60);
            CHECK(!ma->BuiltinOwnsPose());
            CHECK(ma->ActiveSlot() == ModelAnimator::SlotWalk);
            CHECK_NEAR(ma->CurrentStrideRate(), 1.0f, 0.08f);
            move(2.0f, 0.0f, 60);
            CHECK(ma->ActiveSlot() == ModelAnimator::SlotWalk);
            CHECK_NEAR(ma->CurrentStrideRate(), 2.0f / 1.4f, 0.1f);
            move(4.0f, 0.0f, 60);
            CHECK(ma->BuiltinOwnsPose());
            CHECK(ma->ActiveSlot() == ModelAnimator::SlotRun);
            move(0.0f, 4.0f, 8);   // two rising frames past airUpVel: a jump
            CHECK(ma->InAir());
            CHECK(ma->BuiltinOwnsPose());
        };
        run(false);   // drive-by-movement, no graph
        run(true);    // the same readings under a graph
    }

    // ---- A real walk-only rig (no idle, run or jump clip) jumps on a third-person
    // player: the air frames take the built-in jump / fall pose, never the bind
    // T-pose. As attached, and again after "Create Animator from this model's clips".
    for (int graph = 0; graph < 2; ++graph) {
        righarness::Rig r = righarness::PlayableFromFile("mixamo_t_walk_only.glb");
        CHECK(r);
        if (!r) continue;
        auto* ma = r.model->GetComponent<ModelAnimator>();
        CHECK(ma != nullptr);
        if (!ma) continue;
        CHECK(ma->clips.size() == 1u);
        CHECK(ma->jumpClip.empty());
        CHECK(ma->BuiltinFallbackUsable());
        if (graph) CHECK(ClipGraph::Ensure(r.model) != nullptr);
        Transform* ua[2] = {r.Bone(humanoid::LUpperArm), r.Bone(humanoid::RUpperArm)};
        Transform* fa[2] = {r.Bone(humanoid::LForeArm), r.Bone(humanoid::RForeArm)};
        CHECK(ua[0] && ua[1] && fa[0] && fa[1]);
        if (!ua[0] || !ua[1] || !fa[0] || !fa[1]) continue;
        // Upper arm -> forearm, world y: ~0 with the arms straight out (T-pose),
        // well below 0 with the arms lowered.
        auto armY = [&](int s) { return (fa[s]->Position() - ua[s]->Position()).Normalized().y; };
        CHECK(std::fabs(armY(0)) < 0.1f && std::fabs(armY(1)) < 0.1f);   // the file's bind pose reads as a T-pose
        const float dt = 1.0f / 60.0f;
        r.Step(dt, {}, 60);   // settle on the ground
        const float y0 = r.player->transform->Position().y;
        r.Step(dt, {' '}, 3);
        int air = 0, owned = 0, tpose = 0;
        float rise = 0.0f;
        for (int f = 0; f < 150; ++f) {
            r.Step(dt, {});
            const float h = r.player->transform->Position().y - y0;
            rise = std::fmax(rise, h);
            if (!ma->InAir() || h < 0.05f) continue;
            ++air;
            if (ma->BuiltinOwnsPose()) ++owned;
            if (armY(0) > -0.2f || armY(1) > -0.2f) ++tpose;
        }
        std::printf("  walk-only jump (graph=%d): rise %.2f m, %d air frames, built-in %d, T-pose %d\n",
                    graph, rise, air, owned, tpose);
        CHECK(rise > 0.8f && rise < 3.0f);
        CHECK(air > 20);
        CHECK(owned == air);   // no jump clip: the built-in jump owns every air frame
        CHECK(tpose == 0);     // and the arms are never left out straight
        CHECK(std::fabs(r.player->transform->Position().y - y0) < 0.05f);   // landed again
    }

    TEST_MAIN_RESULT();
}

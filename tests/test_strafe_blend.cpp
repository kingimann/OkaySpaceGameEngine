// Directional locomotion slots. With strafe or back clips the clip graph is a
// 2D blend on moveX / moveZ. Without them it stays the forward speed blend, and
// the hips yaw a little toward the movement so a forward walk is not a moonwalk.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Animation/ClipGraph.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Render/ModelImport.hpp"
#include <cmath>
#include <string>

using namespace okay;

static ModelAnimator::Clip ClipNamed(const char* name) {
    ModelAnimator::Clip c; c.name = name;
    ModelAnimator::NodeClip nc; nc.node = "Hips";
    nc.clip.AddKey("position.y", 0.0f, 0.0f);
    nc.clip.AddKey("position.y", 1.0f, 0.0f);
    c.nodes.push_back(nc);
    return c;
}
static const AnimStateMachine::State* Loco(const AnimStateMachine& sm) {
    for (const auto& st : sm.states) if (st.name == "Locomotion") return &st;
    return nullptr;
}

int main() {
    RUN_SUITE("strafe_blend");

    // Names: Walk Back is not the forward walk. Strafe Left and Run Back map too.
    {
        Scene s("map");
        auto* ma = s.CreateGameObject("M")->AddComponent<ModelAnimator>();
        ma->clips = {ClipNamed("Idle"), ClipNamed("Walk"), ClipNamed("Walk Back"),
                     ClipNamed("Strafe Left"), ClipNamed("Run Back")};
        AutoMapLocomotion(ma);
        CHECK(ma->walkClip == "Walk");
        CHECK(ma->walkBackClip == "Walk Back");
        ma->clips.push_back(ClipNamed("Back Flip"));
        AutoMapLocomotion(ma);
        CHECK(ma->walkBackClip == "Walk Back");
        CHECK(ma->walkClip == "Walk");
        Scene only("flip");
        auto* flip = only.CreateGameObject("F")->AddComponent<ModelAnimator>();
        flip->clips = {ClipNamed("Idle"), ClipNamed("Walk"), ClipNamed("Back Flip")};
        AutoMapLocomotion(flip);
        CHECK(flip->walkClip == "Walk");
        CHECK(flip->walkBackClip.empty());
        CHECK(ma->strafeLeftClip == "Strafe Left");
        CHECK(ma->runBackClip == "Run Back");
        CHECK(ma->HasDirectionalClips());
    }

    // A walk-only set (no strafe clips) keeps today's 1D speed blend.
    {
        Scene s("fwd");
        GameObject* go = s.CreateGameObject("M");
        auto* ma = go->AddComponent<ModelAnimator>();
        ma->clips = {ClipNamed("Idle"), ClipNamed("Walk")};
        ma->idleClip = "Idle"; ma->walkClip = "Walk";
        AnimStateMachine sm;
        CHECK(ClipGraph::Build(sm, *ma) > 0);
        const AnimStateMachine::State* loco = Loco(sm);
        CHECK(loco && loco->blendTree && loco->blendDims == 1);
        CHECK(loco->blendParam == "speed");
    }

    // Strafe and back clips make a 2D blend. Forward is negative moveZ.
    {
        Scene s("dir");
        auto* ma = s.CreateGameObject("M")->AddComponent<ModelAnimator>();
        ma->clips = {ClipNamed("Idle"), ClipNamed("Walk"), ClipNamed("Strafe Right"), ClipNamed("Walk Back")};
        ma->idleClip = "Idle"; ma->walkClip = "Walk";
        ma->strafeRightClip = "Strafe Right"; ma->walkBackClip = "Walk Back";
        ma->walkThreshold = 1.5f;
        AnimStateMachine sm;
        ClipGraph::Build(sm, *ma);
        const AnimStateMachine::State* loco = Loco(sm);
        CHECK(loco && loco->blendDims == 2);
        CHECK(loco->blendParam == "moveX");
        CHECK(loco->blendParamY == "moveZ");
        bool fwd = false, right = false, back = false;
        for (const auto& m : loco->motions) {
            if (m.clip == "Walk") { fwd = true; CHECK(m.posY < 0.0f); CHECK(std::fabs(m.threshold) < 1e-4f); }
            if (m.clip == "Strafe Right") { right = true; CHECK(m.threshold > 0.0f); }
            if (m.clip == "Walk Back") { back = true; CHECK(m.posY > 0.0f); }
        }
        CHECK(fwd && right && back);
    }

    // No directional clips: the hips turn toward a strafe, and a forward walk does not.
    {
        Scene s("yaw"); s.physicsEnabled = false;
        GameObject* player = s.CreateGameObject("Player");
        auto* ch = player->AddComponent<Character>();
        ch->Apply();
        GameObject* model = s.CreateGameObject("Model");
        model->transform->SetParent(player->transform, false);
        GameObject* hips = s.CreateGameObject("Hips");
        hips->transform->SetParent(model->transform, false);
        auto* ma = model->AddComponent<ModelAnimator>();
        ma->clips = {ClipNamed("Idle"), ClipNamed("Walk")};
        ma->idleClip = "Idle"; ma->walkClip = "Walk";
        ch->moveSpeed = 1.5f;
        ch->moveAngle = 0.0f;
        s.Start(); s.Update(1.0f / 60.0f);
        const Quat forward = hips->transform->localRotation;
        ch->moveAngle = 40.0f;
        s.Update(1.0f / 60.0f);
        CHECK(Quat::Angle(forward, hips->transform->localRotation) > 5.0f);
    }

    // A walk-only rig moving backward does not play that walk forward.
    {
        Scene s("moon"); s.physicsEnabled = false;
        GameObject* player = s.CreateGameObject("Player");
        auto* ch = player->AddComponent<Character>();
        GameObject* model = s.CreateGameObject("Model");
        model->transform->SetParent(player->transform, false);
        auto* ma = model->AddComponent<ModelAnimator>();
        ma->clips = {ClipNamed("Idle"), ClipNamed("Walk")};
        ma->idleClip = "Idle"; ma->walkClip = "Walk";
        ma->driveByMovement = true;
        ma->smoothLocomotion = true;
        ma->walkThreshold = 0.4f;
        ch->moveAngle = 180.0f;
        s.Start();
        for (int i = 0; i < 20; ++i) {
            ch->moveAngle = 180.0f;
            model->transform->SetPosition(model->transform->Position() + Vec3{0, 0, 0.05f});
            s.Update(1.0f / 60.0f);
        }
        CHECK(ma->CurrentName() == "Walk");
        CHECK(ma->LocomotionSign() < 0);
    }

    // A back clip plays itself, forward, instead of reversing the forward walk.
    {
        Scene s("backclip"); s.physicsEnabled = false;
        GameObject* player = s.CreateGameObject("Player");
        auto* ch = player->AddComponent<Character>();
        GameObject* model = s.CreateGameObject("Model");
        model->transform->SetParent(player->transform, false);
        auto* ma = model->AddComponent<ModelAnimator>();
        ma->clips = {ClipNamed("Idle"), ClipNamed("Walk"), ClipNamed("Walk Back")};
        ma->idleClip = "Idle"; ma->walkClip = "Walk"; ma->walkBackClip = "Walk Back";
        ma->driveByMovement = true;
        ma->walkThreshold = 0.4f;
        ch->moveAngle = 180.0f;
        s.Start();
        for (int i = 0; i < 12; ++i) {
            ch->moveAngle = 180.0f;
            model->transform->SetPosition(model->transform->Position() + Vec3{0, 0, 0.05f});
            s.Update(1.0f / 60.0f);
        }
        CHECK(ma->CurrentName() == "Walk Back");
        CHECK(ma->LocomotionSign() > 0);
    }

    // Build it on a character graph adds Locomotion and keeps the other states.
    {
        AnimStateMachine sm;
        sm.states.push_back({}); sm.states.back().name = "Idle";
        sm.states.push_back({}); sm.states.back().name = "Death";
        ModelAnimator ma;
        ma.clips = {ClipNamed("Idle"), ClipNamed("Walk"), ClipNamed("Walk Back")};
        ma.idleClip = "Idle"; ma.walkClip = "Walk"; ma.walkBackClip = "Walk Back";
        const int n = ClipGraph::InsertLocomotion(sm, ma);
        CHECK(n == 3);
        bool death = false, loco = false;
        for (const auto& st : sm.states) {
            if (st.name == "Death") death = true;
            if (st.name == "Locomotion") { loco = true; CHECK(st.blendDims == 2); }
        }
        CHECK(death && loco);
    }

    TEST_MAIN_RESULT();
}

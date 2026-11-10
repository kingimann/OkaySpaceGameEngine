// Directional locomotion slots. With strafe or back clips the clip graph is a
// 2D blend on moveX / moveZ. Without them it stays the forward speed blend, and
// the hips yaw a little toward the movement (opt-in: legsFollowTravel) so a forward
// walk is not a moonwalk. Off by default: such a model poses exactly as before.
#include "test_framework.hpp"
#include "rig_harness.hpp"
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

    // Models without directional clips map exactly as before: a lone "Run Left" still
    // fills the run slot, and no directional slot is set.
    {
        Scene s("legacy");
        auto* ma = s.CreateGameObject("M")->AddComponent<ModelAnimator>();
        ma->clips = {ClipNamed("Idle"), ClipNamed("Run Left")};
        AutoMapLocomotion(ma);
        CHECK(ma->idleClip == "Idle");
        CHECK(ma->runClip == "Run Left");
        CHECK(!ma->HasDirectionalClips());
        // The test rigs carry no directional clips: they map to forward slots only.
        const char* rigs[] = {"mixamo_t_cm_clips.glb", "mixamo_t_walk_only.glb", "mixamo_t_stance_clips.glb"};
        for (const char* f : rigs) {
            righarness::Rig R = righarness::PlayableFromFile(f);
            ModelAnimator* rma = nullptr;
            if (R.model)
                for (const auto& up : R.scene->Objects())
                    if (up->IsSelfOrDescendantOf(R.model) && (rma = up->GetComponent<ModelAnimator>())) break;
            CHECK(rma != nullptr);
            if (rma) {
                CHECK(!rma->HasDirectionalClips());
                CHECK(!rma->legsFollowTravel);
                CHECK(!rma->walkClip.empty());
            }
        }
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
        ma->legsFollowTravel = true;
        ch->moveSpeed = 1.5f;
        ch->moveAngle = 0.0f;
        s.Start(); s.Update(1.0f / 60.0f);
        const Quat forward = hips->transform->localRotation;
        ch->moveAngle = 40.0f;
        for (int i = 0; i < 30; ++i) s.Update(1.0f / 60.0f);
        CHECK(Quat::Angle(forward, hips->transform->localRotation) > 5.0f);
    }

    // Strafe hip yaw follows travel (right = +X) and stays upright, including
    // when the hips' parent is rotated so its own up is not world up.
    {
        auto measure = [](bool tilted, float angle, float& faceX, float& tiltDeg) {
            Scene s(tilted ? "yaw-tilt" : "yaw-up"); s.physicsEnabled = false;
            GameObject* player = s.CreateGameObject("Player");
            auto* ch = player->AddComponent<Character>();
            ch->Apply();
            GameObject* model = s.CreateGameObject("Model");
            model->transform->SetParent(player->transform, false);
            Transform* hipParent = model->transform;
            if (tilted) {
                GameObject* arm = s.CreateGameObject("Armature");
                arm->transform->SetParent(model->transform, false);
                arm->transform->localRotation = Quat::AngleAxis(65.0f, Vec3::Right);
                hipParent = arm->transform;
            }
            GameObject* hips = s.CreateGameObject("Hips");
            hips->transform->SetParent(hipParent, false);
            if (tilted) hips->transform->localRotation = hipParent->Rotation().Inverse();
            auto* ma = model->AddComponent<ModelAnimator>();
            ma->clips = {ClipNamed("Idle"), ClipNamed("Walk")};
            ma->idleClip = "Idle"; ma->walkClip = "Walk";
            ma->legsFollowTravel = true;
            ch->moveSpeed = 2.0f;
            ch->moveAngle = angle;
            s.Start();
            for (int i = 0; i < 30; ++i) s.Update(1.0f / 60.0f);
            const Vec3 face = hips->transform->Rotation() * Vec3{0.0f, 0.0f, -1.0f};
            Vec3 up = hips->transform->Rotation() * Vec3::Up;
            up = up.Normalized();
            float d = Vec3::Dot(up, Vec3::Up);
            if (d > 1.0f) d = 1.0f;
            if (d < -1.0f) d = -1.0f;
            faceX = face.x;
            tiltDeg = std::acos(d) * 57.2957795f;
        };
        float faceX = 0.0f, tilt = 90.0f;
        for (bool tilted : {false, true}) {
            measure(tilted, 70.0f, faceX, tilt);
            CHECK(faceX > 0.5f);
            CHECK(tilt < 15.0f);
            measure(tilted, -70.0f, faceX, tilt);
            CHECK(faceX < -0.5f);
            CHECK(tilt < 15.0f);
        }
    }

    // A pure strafe turns the hips, and the chest stays with the body's facing.
    {
        auto chestYaw = [](float angle) {
            Scene s("chest"); s.physicsEnabled = false;
            GameObject* player = s.CreateGameObject("Player");
            auto* ch = player->AddComponent<Character>();
            ch->Apply();
            GameObject* model = s.CreateGameObject("Model");
            model->transform->SetParent(player->transform, false);
            GameObject* hips = s.CreateGameObject("Hips");
            hips->transform->SetParent(model->transform, false);
            GameObject* spine = s.CreateGameObject("Spine");
            spine->transform->SetParent(hips->transform, false);
            GameObject* chest = s.CreateGameObject("Chest");
            chest->transform->SetParent(spine->transform, false);
            auto* ma = model->AddComponent<ModelAnimator>();
            ma->clips = {ClipNamed("Idle"), ClipNamed("Walk")};
            ma->idleClip = "Idle"; ma->walkClip = "Walk";
            ma->legsFollowTravel = true;
            ch->moveSpeed = 2.0f;
            ch->moveAngle = angle;
            s.Start();
            for (int i = 0; i < 30; ++i) s.Update(1.0f / 60.0f);
            const Vec3 body = player->transform->Rotation() * Vec3{0.0f, 0.0f, -1.0f};
            const Vec3 face = chest->transform->Rotation() * Vec3{0.0f, 0.0f, -1.0f};
            const float by = std::atan2(body.x, -body.z);
            const float cy = std::atan2(face.x, -face.z);
            float rel = (cy - by) * 57.2957795f;
            while (rel > 180.0f) rel -= 360.0f;
            while (rel < -180.0f) rel += 360.0f;
            return rel;
        };
        CHECK(std::fabs(chestYaw(90.0f)) < 10.0f);
        CHECK(std::fabs(chestYaw(-90.0f)) < 10.0f);
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
        ma->legsFollowTravel = true;
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

    // Default characters (no opt-in, no directional clips): moving backward plays the
    // walk forward and the hips are never touched, as before directional clips existed.
    {
        Scene s("default"); s.physicsEnabled = false;
        GameObject* player = s.CreateGameObject("Player");
        auto* ch = player->AddComponent<Character>();
        GameObject* model = s.CreateGameObject("Model");
        model->transform->SetParent(player->transform, false);
        GameObject* hips = s.CreateGameObject("Hips");
        hips->transform->SetParent(model->transform, false);
        auto* ma = model->AddComponent<ModelAnimator>();
        ma->clips = {ClipNamed("Idle"), ClipNamed("Walk")};
        ma->idleClip = "Idle"; ma->walkClip = "Walk";
        ma->driveByMovement = true;
        ma->smoothLocomotion = true;
        ma->walkThreshold = 0.4f;
        CHECK(!ma->legsFollowTravel);
        const Quat rest = hips->transform->localRotation;
        s.Start();
        for (int i = 0; i < 30; ++i) {
            ch->moveAngle = (i % 2) ? 170.0f : -170.0f;
            model->transform->SetPosition(model->transform->Position() + Vec3{0, 0, 0.05f});
            s.Update(1.0f / 60.0f);
            CHECK(ma->LocomotionSign() >= 0);
            CHECK(Quat::Angle(rest, hips->transform->localRotation) < 1e-3f);
        }
        CHECK(ma->CurrentName() == "Walk");
    }

    // Rapid A / D flips: the hips turn by a bounded amount per frame, never snap.
    // A held pose (the clip does not rewrite the hips' rotation) must not pile up turns.
    {
        Scene s("flip"); s.physicsEnabled = false;
        GameObject* player = s.CreateGameObject("Player");
        auto* ch = player->AddComponent<Character>();
        ch->Apply();
        GameObject* model = s.CreateGameObject("Model");
        model->transform->SetParent(player->transform, false);
        GameObject* hips = s.CreateGameObject("Hips");
        hips->transform->SetParent(model->transform, false);
        GameObject* spine = s.CreateGameObject("Spine");
        spine->transform->SetParent(hips->transform, false);
        auto* ma = model->AddComponent<ModelAnimator>();
        ma->clips = {ClipNamed("Idle"), ClipNamed("Walk")};
        ma->idleClip = "Idle"; ma->walkClip = "Walk";
        ma->legsFollowTravel = true;
        ch->moveSpeed = 2.0f;
        s.Start();
        Quat prev = hips->transform->localRotation;
        float worst = 0.0f, peak = 0.0f;
        for (int i = 0; i < 120; ++i) {
            ch->moveAngle = (i / 3 % 2) ? 90.0f : -90.0f;
            s.Update(1.0f / 60.0f);
            const Quat q = hips->transform->localRotation;
            worst = std::fmax(worst, Quat::Angle(prev, q));
            peak = std::fmax(peak, Quat::Angle(Quat(), q));
            prev = q;
        }
        CHECK(worst < 13.0f);      // 720 deg/s at 60 fps
        CHECK(peak <= 80.5f);      // clamped
        // Hold a steady 60 degree travel: the turn settles at 60 and stays there.
        ch->moveAngle = 60.0f;
        for (int i = 0; i < 90; ++i) s.Update(1.0f / 60.0f);
        const float settled = Quat::Angle(Quat(), hips->transform->localRotation);
        CHECK(std::fabs(settled - 60.0f) < 2.0f);
        for (int i = 0; i < 90; ++i) s.Update(1.0f / 60.0f);
        CHECK(std::fabs(Quat::Angle(Quat(), hips->transform->localRotation) - settled) < 0.1f);
        // Stopped: it eases back to the rest pose.
        ch->moveSpeed = 0.0f;
        for (int i = 0; i < 90; ++i) s.Update(1.0f / 60.0f);
        CHECK(Quat::Angle(Quat(), hips->transform->localRotation) < 0.5f);
        CHECK(Quat::Angle(Quat(), spine->transform->localRotation) < 0.5f);
    }

    // The backward boundary has hysteresis: hovering around 100 degrees does not flip the sign.
    {
        Scene s("hyst"); s.physicsEnabled = false;
        GameObject* player = s.CreateGameObject("Player");
        auto* ch = player->AddComponent<Character>();
        GameObject* model = s.CreateGameObject("Model");
        model->transform->SetParent(player->transform, false);
        auto* ma = model->AddComponent<ModelAnimator>();
        ma->clips = {ClipNamed("Idle"), ClipNamed("Walk")};
        ma->idleClip = "Idle"; ma->walkClip = "Walk";
        ma->driveByMovement = true;
        ma->smoothLocomotion = true;
        ma->legsFollowTravel = true;
        ma->walkThreshold = 0.4f;
        s.Start();
        int flips = 0, last = 0;
        for (int i = 0; i < 60; ++i) {
            ch->moveAngle = (i % 2) ? 99.0f : 101.0f;
            model->transform->SetPosition(model->transform->Position() + Vec3{0, 0, 0.05f});
            s.Update(1.0f / 60.0f);
            const int sign = ma->LocomotionSign();
            if (i > 5 && sign != last) ++flips;
            last = sign;
        }
        CHECK(flips == 0);
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

    // Strafe clips and no back clip: backward still reverses the forward walk.
    {
        Scene s("strafe-back"); s.physicsEnabled = false;
        GameObject* player = s.CreateGameObject("Player");
        auto* ch = player->AddComponent<Character>();
        GameObject* model = s.CreateGameObject("Model");
        model->transform->SetParent(player->transform, false);
        auto* ma = model->AddComponent<ModelAnimator>();
        ma->clips = {ClipNamed("Idle"), ClipNamed("Walk"), ClipNamed("Strafe Left"), ClipNamed("Strafe Right")};
        ma->idleClip = "Idle"; ma->walkClip = "Walk";
        ma->strafeLeftClip = "Strafe Left"; ma->strafeRightClip = "Strafe Right";
        ma->driveByMovement = true;
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

    // Updating the clip blend keeps the settings that are not the clips.
    {
        AnimStateMachine sm;
        AnimStateMachine::State loco;
        loco.name = "Locomotion";
        loco.speed = 1.7f;
        loco.speedParam = "pace";
        loco.fallbackClip = "IdleStand";
        loco.enterEvent = "step";
        loco.exitEvent = "stop";
        loco.ikWeight = 0.25f;
        loco.fullBodyIkWeight = 0.4f;
        loco.lookWeight = 0.4f;
        loco.ikCurve.push_back({0.2f, 0.5f});
        loco.fullBodyIkCurve.push_back({0.8f, 0.3f});
        loco.matchSpeed = true;
        loco.loop = false;
        loco.interruptible = false;
        loco.mirror = true;
        loco.rootMotion = 1;
        loco.nx = 40.0f; loco.ny = 70.0f;
        AnimStateMachine::Transition keep;
        keep.to = "Death";
        loco.transitions.push_back(keep);
        sm.states.push_back(loco);
        sm.states.push_back({}); sm.states.back().name = "Death";
        ModelAnimator ma;
        ma.clips = {ClipNamed("Idle"), ClipNamed("Walk"), ClipNamed("Strafe Left")};
        ma.idleClip = "Idle"; ma.walkClip = "Walk"; ma.strafeLeftClip = "Strafe Left";
        const int n = ClipGraph::InsertLocomotion(sm, ma);
        CHECK(n == 2);
        const AnimStateMachine::State* got = nullptr;
        for (const auto& st : sm.states) if (st.name == "Locomotion") got = &st;
        CHECK(got != nullptr);
        if (got) {
            CHECK(got->blendDims == 2);
            CHECK(std::fabs(got->speed - 1.7f) < 1e-4f);
            CHECK(got->speedParam == "pace");
            CHECK(got->fallbackClip == "IdleStand");
            CHECK(got->enterEvent == "step" && got->exitEvent == "stop");
            CHECK(std::fabs(got->ikWeight - 0.25f) < 1e-4f);
            CHECK(std::fabs(got->fullBodyIkWeight - 0.4f) < 1e-4f);
            CHECK(std::fabs(got->lookWeight - 0.4f) < 1e-4f);
            CHECK(got->ikCurve.size() == 1 && std::fabs(got->ikCurve[0].t - 0.2f) < 1e-4f);
            CHECK(got->fullBodyIkCurve.size() == 1);
            CHECK(got->matchSpeed && !got->loop && !got->interruptible && got->mirror);
            CHECK(got->rootMotion == 1);
            CHECK(std::fabs(got->nx - 40.0f) < 1e-3f && std::fabs(got->ny - 70.0f) < 1e-3f);
            CHECK(got->transitions.size() == 1 && got->transitions[0].to == "Death");
            bool strafe = false;
            for (const auto& m : got->motions) if (m.clip == "Strafe Left") strafe = true;
            CHECK(strafe);
        }
    }

    TEST_MAIN_RESULT();
}

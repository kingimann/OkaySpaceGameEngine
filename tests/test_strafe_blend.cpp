// Directional locomotion slots. With strafe or back clips the clip graph is a
// 2D blend on moveX / moveZ. Without them it stays the forward speed blend, and
// the hips yaw a little toward the movement so a forward walk is not a moonwalk.
#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Animation/ClipGraph.hpp"
#include "okay/Components/ModelAnimator.hpp"
#include "okay/Components/Character.hpp"
#include "okay/Render/ModelImport.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include <cmath>
#include <string>
#include <tuple>

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
            ch->moveSpeed = 2.0f;
            ch->moveAngle = angle;
            s.Start();
            for (int i = 0; i < 18; ++i) s.Update(1.0f / 60.0f);
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
            ch->moveSpeed = 2.0f;
            ch->moveAngle = angle;
            s.Start();
            for (int i = 0; i < 18; ++i) s.Update(1.0f / 60.0f);
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
        sm.states.push_back({});
        sm.states.back().name = "Locomotion";
        sm.states.back().ikWeight = 0.25f;
        sm.states.back().speed = 1.4f;
        sm.states.back().transitions.push_back({});
        sm.states.back().transitions.back().to = "Death";
        ModelAnimator ma;
        ma.clips = {ClipNamed("Idle"), ClipNamed("Walk"), ClipNamed("Walk Back")};
        ma.idleClip = "Idle"; ma.walkClip = "Walk"; ma.walkBackClip = "Walk Back";
        const int n = ClipGraph::InsertLocomotion(sm, ma);
        CHECK(n == 3);
        bool death = false, loco = false;
        for (const auto& st : sm.states) {
            if (st.name == "Death") death = true;
            if (st.name == "Locomotion") {
                loco = true;
                CHECK(st.blendDims == 2);
                CHECK(std::fabs(st.ikWeight - 0.25f) < 1e-4f);
                CHECK(std::fabs(st.speed - 1.4f) < 1e-4f);
                CHECK(st.transitions.size() == 1 && st.transitions[0].to == "Death");
            }
        }
        CHECK(death && loco);
    }

    // A filled strafe slot is a binding. Auto-map does not replace it.
    {
        Scene s("keep");
        auto* ma = s.CreateGameObject("M")->AddComponent<ModelAnimator>();
        ma->clips = {ClipNamed("Idle"), ClipNamed("Walk"), ClipNamed("Strafe Left"), ClipNamed("Side Step Left")};
        ma->strafeLeftClip = "Side Step Left";
        AutoMapLocomotion(ma);
        CHECK(ma->strafeLeftClip == "Side Step Left");
        CHECK(ma->walkClip == "Walk");
    }

    // Between walk and strafe both clips stay in the blend, and a small move
    // does not drop either one. That is the 2D weight the graph uses.
    {
        AnimStateMachine::Motion walk; walk.clip = "Walk"; walk.posY = -1.5f;
        AnimStateMachine::Motion right; right.clip = "Strafe Right"; right.threshold = 1.5f;
        AnimStateMachine::Motion idle; idle.clip = "Idle";
        std::vector<AnimStateMachine::Motion> mot{idle, walk, right};
        auto weight = [&](float x, float y, const char* clip) {
            std::vector<float> w;
            AnimStateMachine::Weights2D(mot, x, y, w);
            for (std::size_t i = 0; i < mot.size(); ++i) if (mot[i].clip == clip) return w[i];
            return 0.0f;
        };
        const float w0 = weight(0.75f, -0.75f, "Walk");
        const float r0 = weight(0.75f, -0.75f, "Strafe Right");
        CHECK(w0 > 0.2f && r0 > 0.2f);
        const float w1 = weight(0.80f, -0.70f, "Walk");
        const float r1 = weight(0.80f, -0.70f, "Strafe Right");
        CHECK(w1 > 0.15f && r1 > 0.15f);
        CHECK(std::fabs(w1 - w0) < 0.25f && std::fabs(r1 - r0) < 0.25f);
    }

    // The hip offset does not accumulate on a bone the clip never keys, and a
    // VRoid / CC named rig still yaws the hips and keeps the chest facing.
    {
        auto spin = [](const char* hipsName, const char* spineName) {
            Scene s("spin"); s.physicsEnabled = false;
            GameObject* player = s.CreateGameObject("Player");
            auto* ch = player->AddComponent<Character>();
            ch->Apply();
            GameObject* model = s.CreateGameObject("Model");
            model->transform->SetParent(player->transform, false);
            GameObject* hips = s.CreateGameObject(hipsName);
            hips->transform->SetParent(model->transform, false);
            GameObject* spine = s.CreateGameObject(spineName);
            spine->transform->SetParent(hips->transform, false);
            GameObject* chest = s.CreateGameObject("Chest");
            chest->transform->SetParent(spine->transform, false);
            auto* ma = model->AddComponent<ModelAnimator>();
            ma->clips = {ClipNamed("Idle"), ClipNamed("Walk")};
            ma->idleClip = "Idle"; ma->walkClip = "Walk";
            ch->moveSpeed = 2.0f;
            ch->moveAngle = 70.0f;
            s.Start();
            float maxYaw = 0.0f;
            Quat mid{0, 0, 0, 1};
            for (int i = 0; i < 40; ++i) {
                s.Update(1.0f / 60.0f);
                maxYaw = std::fmax(maxYaw, Quat::Angle(Quat{0, 0, 0, 1}, hips->transform->localRotation));
                if (i == 18) mid = hips->transform->localRotation;
            }
            const Vec3 body = player->transform->Rotation() * Vec3{0.0f, 0.0f, -1.0f};
            const Vec3 face = chest->transform->Rotation() * Vec3{0.0f, 0.0f, -1.0f};
            const float by = std::atan2(body.x, -body.z);
            const float cy = std::atan2(face.x, -face.z);
            float rel = (cy - by) * 57.2957795f;
            while (rel > 180.0f) rel -= 360.0f;
            while (rel < -180.0f) rel += 360.0f;
            const float settled = Quat::Angle(mid, hips->transform->localRotation);
            return std::tuple<float, float, float>{maxYaw, settled, rel};
        };
        for (const char* hipsName : {"Hips", "J_Bip_C_Hips", "CC_Base_Hip"}) {
            const char* spineName = hipsName[0] == 'J' ? "J_Bip_C_Spine" : (hipsName[0] == 'C' ? "CC_Base_Waist" : "Spine");
            float maxYaw = 0, settled = 99, rel = 99;
            std::tie(maxYaw, settled, rel) = spin(hipsName, spineName);
            CHECK(maxYaw > 20.0f);
            CHECK(maxYaw < 95.0f);
            CHECK(settled < 8.0f);
            CHECK(std::fabs(rel) < 12.0f);
        }
    }

    // Strafe clips do not also yaw the hips: the clip faces the travel.
    {
        Scene s("no-double"); s.physicsEnabled = false;
        GameObject* player = s.CreateGameObject("Player");
        auto* ch = player->AddComponent<Character>();
        ch->Apply();
        GameObject* model = s.CreateGameObject("Model");
        model->transform->SetParent(player->transform, false);
        GameObject* hips = s.CreateGameObject("Hips");
        hips->transform->SetParent(model->transform, false);
        auto* ma = model->AddComponent<ModelAnimator>();
        ma->clips = {ClipNamed("Idle"), ClipNamed("Walk"), ClipNamed("Strafe Right")};
        ma->idleClip = "Idle"; ma->walkClip = "Walk"; ma->strafeRightClip = "Strafe Right";
        ma->driveByMovement = true;
        ch->moveSpeed = 2.0f;
        ch->moveAngle = 70.0f;
        s.Start();
        for (int i = 0; i < 18; ++i) s.Update(1.0f / 60.0f);
        CHECK(Quat::Angle(Quat{0, 0, 0, 1}, hips->transform->localRotation) < 5.0f);
    }

    // Build it's Locomotion state, not only drive-by-movement, plays the forward
    // walk backward when the set can strafe and has no back clip.
    {
        Scene s("graph-back"); s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("M");
        auto* ma = go->AddComponent<ModelAnimator>();
        ma->clips = {ClipNamed("Idle"), ClipNamed("Walk"), ClipNamed("Strafe Left"), ClipNamed("Strafe Right")};
        ma->idleClip = "Idle"; ma->walkClip = "Walk";
        ma->strafeLeftClip = "Strafe Left"; ma->strafeRightClip = "Strafe Right";
        ma->walkThreshold = 1.5f;
        auto* sm = go->AddComponent<AnimStateMachine>();
        CHECK(ClipGraph::Build(*sm, *ma) > 0);
        sm->SetFloat("moveX", 0.0f);
        sm->SetFloat("moveZ", 1.5f);
        s.Start();
        s.Update(1.0f / 60.0f);   // enter Locomotion
        float prev = ma->BlendPhase();
        bool decreased = false;
        for (int i = 0; i < 8; ++i) {
            sm->SetFloat("moveX", 0.0f);
            sm->SetFloat("moveZ", 1.5f);
            s.Update(1.0f / 60.0f);
            if (ma->BlendPhase() < prev - 1e-4f) decreased = true;
            prev = ma->BlendPhase();
        }
        CHECK(decreased);
        CHECK(ma->speed < 0.0f);
        int walk = -1;
        const AnimStateMachine::State* loco = Loco(*sm);
        CHECK(loco != nullptr);
        if (loco) {
            for (int i = 0; i < (int)loco->motions.size(); ++i)
                if (loco->motions[i].clip == "Walk") walk = i;
            CHECK(walk >= 0);
            if (walk >= 0 && walk < (int)sm->BlendWeights().size())
                CHECK(sm->BlendWeights()[(std::size_t)walk] > 0.9f);
        }
        // A heading that chatters across 100° stays in the back sector.
        bool hold = true;
        AnimStateMachine::State probe;
        probe.blendParam = "moveX"; probe.blendParamY = "moveZ";
        probe.motions.push_back({"Idle", 0.0f, 0.0f});
        probe.motions.push_back({"Walk", 0.0f, -1.5f});
        probe.motions.push_back({"Strafe Right", 1.5f, 0.0f});
        const float rad = 95.0f * 0.017453292f;
        CHECK(AnimStateMachine::BackpedalMotion(probe, std::sin(rad) * 1.5f, -std::cos(rad) * 1.5f, hold) >= 0);
        const float back = 85.0f * 0.017453292f;
        CHECK(AnimStateMachine::BackpedalMotion(probe, std::sin(back) * 1.5f, -std::cos(back) * 1.5f, hold) >= 0);
        hold = false;
        CHECK(AnimStateMachine::BackpedalMotion(probe, std::sin(back) * 1.5f, -std::cos(back) * 1.5f, hold) < 0);
    }

    // A name that is not on the model is not a strafe set: the graph stays the
    // forward blend, and the name is still saved across a reload.
    {
        Scene s("slots");
        GameObject* go = s.CreateGameObject("M");
        auto* ma = go->AddComponent<ModelAnimator>();
        ma->clips = {ClipNamed("Idle"), ClipNamed("Walk"), ClipNamed("Walk Fast"),
                     ClipNamed("Strafe Left"), ClipNamed("Back Flip")};
        ma->driveByMovement = true;
        ma->idleClip = "Idle";
        ma->walkClip = "Walk";
        ma->strafeLeftClip = "Not On Model";
        CHECK(ma->SlotUseOf(ModelAnimator::SlotStrafeLeft) == ModelAnimator::SlotUse::Missing);
        CHECK(!ma->HasDirectionalClips());
        {
            ModelAnimator named = *ma;
            named.strafeLeftClip.clear();
            CHECK(SuggestUnassignedSlot(named, ModelAnimator::SlotStrafeLeft) == "Strafe Left");
            named.strafeLeftClip = "Side Step Left";
            CHECK(SuggestUnassignedSlot(named, ModelAnimator::SlotStrafeLeft).empty());
        }
        AnimStateMachine built;
        ClipGraph::Build(built, *ma);
        const AnimStateMachine::State* loco = Loco(built);
        CHECK(loco && loco->blendDims == 1);

        ma->walkBackClip = "Back Flip";
        CHECK(ma->SlotCaution(ModelAnimator::SlotWalkBack) != nullptr);
        CHECK(ma->SlotCaution(ModelAnimator::SlotWalkBack)[0] != 0);

        AnimStateMachine sm;
        sm.states.push_back({});
        sm.states.back().name = "Death";
        sm.states.back().ikWeight = 0.2f;
        sm.states.back().transitions.push_back({});
        sm.states.back().transitions.back().to = "Idle";
        ClipGraph::InsertLocomotion(sm, *ma);
        ma->walkClip = "Walk Fast";
        ma->strafeLeftClip.clear();
        ma->strafeRightClip = "Strafe Left";
        ClipGraph::InsertLocomotion(sm, *ma);
        bool death = false, fast = false, strafe = false;
        for (const auto& st : sm.states) {
            if (st.name == "Death") {
                death = true;
                CHECK(std::fabs(st.ikWeight - 0.2f) < 1e-4f);
                CHECK(st.transitions.size() == 1 && st.transitions[0].to == "Idle");
            }
            if (st.name == "Locomotion") {
                CHECK(st.blendDims == 2);
                for (const auto& m : st.motions) {
                    if (m.clip == "Walk Fast") fast = true;
                    if (m.clip == "Strafe Left") strafe = true;
                    CHECK(m.clip != "Walk");
                }
            }
        }
        CHECK(death && fast && strafe);

        ma->walkBackClip = "Missing Back";
        std::string err;
        Scene loaded("loaded");
        CHECK(SceneSerializer::Deserialize(loaded, SceneSerializer::Serialize(s), &err));
        ModelAnimator* again = nullptr;
        for (const auto& up : loaded.Objects())
            if (up) if (auto* m = up->GetComponent<ModelAnimator>()) again = m;
        CHECK(again != nullptr);
        if (again) {
            CHECK(again->walkClip == "Walk Fast");
            CHECK(again->strafeRightClip == "Strafe Left");
            CHECK(again->walkBackClip == "Missing Back");
            CHECK(again->SlotUseOf(ModelAnimator::SlotWalkBack) == ModelAnimator::SlotUse::Missing);
            CHECK(!again->HasDirectionalClips() || again->SlotFilled(ModelAnimator::SlotStrafeRight));
            CHECK(again->SlotFilled(ModelAnimator::SlotStrafeRight));
        }
    }

    TEST_MAIN_RESULT();
}

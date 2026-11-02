#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Scripting/OkayScriptVM.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include <cmath>
#include <string>

using namespace okay;

static float Dist(const Vec3& a, const Vec3& b) { return (a - b).Magnitude(); }

static GameObject* Bone(Scene& sc, const char* name, GameObject* parent, Vec3 local) {
    GameObject* o = sc.CreateGameObject(name);
    o->transform->SetParent(parent->transform, false);
    o->transform->localPosition = local;
    return o;
}

int main() {
    RUN_SUITE("anim_ik_components");

    // ---- IK Target: a hand eases onto the object, and the head looks at it.
    {
        Scene sc;
        GameObject* hero = sc.CreateGameObject("Hero");
        GameObject* hips = Bone(sc, "Hips", hero, {0, 1, 0});
        GameObject* chest = Bone(sc, "Chest", hips, {0, 0.4f, 0});
        GameObject* arm = Bone(sc, "LeftArm", chest, {0.2f, 0.1f, 0});
        GameObject* fore = Bone(sc, "LeftForeArm", arm, {0.3f, 0, 0});
        GameObject* hand = Bone(sc, "LeftHand", fore, {0.3f, 0, 0});
        Bone(sc, "Head", chest, {0, 0.25f, 0});
        auto* fb = hero->AddComponent<FullBodyIK>();
        fb->blendSpeed = 0.0f;
        fb->autoMap = false;
        fb->bones[FullBodyIK::Hips] = hips->transform;
        fb->bones[FullBodyIK::Chest] = chest->transform;
        fb->bones[FullBodyIK::LUpperArm] = arm->transform;
        fb->bones[FullBodyIK::LForeArm] = fore->transform;
        fb->bones[FullBodyIK::LHand] = hand->transform;
        for (auto& e : fb->effectors) e.targetBlendTime = 0.0f;
        sc.Start();
        CHECK(fb->bones[FullBodyIK::LHand] == hand->transform);

        GameObject* tgt = IKTarget::Spawn(sc, hero, IKTarget::LeftHand);
        auto* ik = tgt->GetComponent<IKTarget>();
        CHECK(ik && ik->character == hero && ik->limb == IKTarget::LeftHand);
        tgt->transform->SetPosition(arm->transform->Position() + Vec3{0.2f, -0.15f, 0.2f});
        ik->blendTime = 0.0f;
        ik->weight = 1.0f;
        const Vec3 goal = tgt->transform->Position();
        ik->LateUpdate(0.016f);
        CHECK(fb->effectors[FullBodyIK::LeftHand].enabled);
        CHECK(fb->effectors[FullBodyIK::LeftHand].target == tgt->transform);
        CHECK_NEAR(fb->effectors[FullBodyIK::LeftHand].positionWeight, 1.0f, 1e-4f);
        sc.Update(1.0f / 60.0f);
        CHECK(Dist(hand->transform->Position(), goal) < 0.05f);

        GameObject* lookAt = IKTarget::Spawn(sc, hero, IKTarget::Head);
        lookAt->transform->SetPosition(chest->transform->Position() + Vec3{0, 0.2f, -2});
        auto* headIk = lookAt->GetComponent<IKTarget>();
        headIk->blendTime = 0.0f;
        headIk->LateUpdate(0.016f);
        LookAtIK* look = hero->GetComponent<LookAtIK>();
        CHECK(look && look->targetObject == lookAt->transform);
        CHECK(!look->chain.empty());

        // Weight eases in instead of snapping.
        ik->weight = 0.0f;
        ik->blendTime = 0.0f;
        ik->LateUpdate(0.016f);
        CHECK_NEAR(fb->effectors[FullBodyIK::LeftHand].positionWeight, 0.0f, 1e-3f);
        ik->weight = 1.0f;
        ik->blendTime = 1.0f;
        ik->LateUpdate(0.05f);
        CHECK(ik->ShownWeight() > 0.02f && ik->ShownWeight() < 0.5f);
    }

    // ---- Aim Offset: the chest yaws toward a target, recoil kicks and recovers.
    {
        Scene sc;
        GameObject* hero = sc.CreateGameObject("Hero");
        GameObject* spine = Bone(sc, "Spine", hero, {0, 1.0f, 0});
        GameObject* chest = Bone(sc, "Chest", spine, {0, 0.3f, 0});
        Bone(sc, "LeftArm", chest, {-0.3f, 0.1f, 0});
        Bone(sc, "RightArm", chest, {0.3f, 0.1f, 0});
        GameObject* mark = sc.CreateGameObject("Mark");
        mark->transform->SetPosition({3, 1.3f, 0});
        auto* aim = hero->AddComponent<AimOffset>();
        aim->useCamera = false;
        aim->target = mark->transform;
        aim->targetName = "Mark";
        aim->spineWeight = 0.0f;
        aim->armWeight = 0.0f;
        aim->chestWeight = 1.0f;
        aim->weight = 1.0f;
        aim->kick = 12.0f;
        aim->recovery = 0.0f;
        sc.Start();
        const Quat before = chest->transform->Rotation();
        aim->LateUpdate(0.016f);
        CHECK(std::fabs(Quat::Dot(chest->transform->Rotation(), before)) < 0.999f);

        aim->recovery = 0.0f;
        aim->Recoil(1.0f);
        CHECK(aim->RecoilPitch() > 10.0f);
        const Quat kickedFrom = chest->transform->Rotation();
        aim->LateUpdate(0.001f);
        CHECK(std::fabs(Quat::Dot(chest->transform->Rotation(), kickedFrom)) < 0.999f);
        aim->recovery = 30.0f;
        aim->LateUpdate(1.0f);
        CHECK(std::fabs(aim->RecoilPitch()) < 0.5f);

        OkayScriptVM vm;
        ScriptHost host;
        host.gameObject = hero;
        host.transform = hero->transform;
        vm.Bind(&host);
        CHECK(vm.Load("recoil(2)\n"));
        CHECK(aim->RecoilPitch() > 20.0f);
    }

    // ---- Pose Capture: one frame, a short recording, and a script call.
    {
        Scene sc;
        GameObject* hero = sc.CreateGameObject("Hero");
        auto* ma = hero->AddComponent<ModelAnimator>();
        GameObject* arm = Bone(sc, "LeftArm", hero, {0.4f, 1.2f, 0.1f});
        arm->transform->localRotation = Quat::Euler(0, 25, 0);
        auto* cap = hero->AddComponent<PoseCapture>();
        cap->clipName = "WavePose";
        const int idx = cap->CaptureFrame();
        CHECK(idx == 0);
        CHECK(ma->clips.size() == 1 && ma->clips[0].name == "WavePose");
        CHECK(ma->ActiveClip() == 0);
        const Vec3 saved = arm->transform->localPosition;
        const Quat savedR = arm->transform->localRotation;
        arm->transform->localPosition = {0, 0, 0};
        arm->transform->localRotation = Quat::Identity;
        ma->SetTime(0.0f);
        CHECK(Dist(arm->transform->localPosition, saved) < 1e-3f);
        CHECK(std::fabs(Quat::Dot(arm->transform->localRotation, savedR)) > 0.999f);

        arm->transform->localPosition = {0.1f, 0, 0};
        cap->StartRecord(0.05f);
        CHECK(cap->IsRecording());
        for (int i = 0; i < 8; ++i) {
            arm->transform->localPosition.x += 0.05f;
            sc.Update(1.0f / 60.0f);
        }
        CHECK(!cap->IsRecording());
        CHECK(cap->LastClip() >= 0);
        CHECK(ma->clips[cap->LastClip()].name == "WavePose");
        bool longClip = false;
        for (const auto& n : ma->clips[cap->LastClip()].nodes)
            if (n.node == "LeftArm" && n.clip.Length() > 0.05f) longClip = true;
        CHECK(longClip);

        // A second one-frame capture replaces the same name rather than adding another.
        const int again = cap->CaptureFrame();
        CHECK(again == cap->LastClip());
        int named = 0;
        for (const auto& c : ma->clips) if (c.name == "WavePose") ++named;
        CHECK(named == 1);

        OkayScriptVM vm;
        ScriptHost host;
        host.gameObject = hero;
        host.transform = hero->transform;
        vm.Bind(&host);
        arm->transform->localPosition = {1, 2, 3};
        CHECK(vm.Load("capture_pose()\n"));
        arm->transform->localPosition = {0, 0, 0};
        ma->SetTime(0.0f);
        CHECK_NEAR(arm->transform->localPosition.y, 2.0f, 1e-3f);
    }

    // ---- The three components survive a scene save.
    {
        Scene sc;
        GameObject* hero = sc.CreateGameObject("Hero");
        GameObject* tgt = IKTarget::Spawn(sc, hero, IKTarget::RightHand);
        tgt->GetComponent<IKTarget>()->weight = 0.4f;
        tgt->GetComponent<IKTarget>()->blendTime = 0.3f;
        auto* aim = hero->AddComponent<AimOffset>();
        aim->useCamera = false;
        aim->targetName = "Mark";
        aim->kick = 5.0f;
        aim->chestWeight = 0.5f;
        auto* cap = hero->AddComponent<PoseCapture>();
        cap->clipName = "IdleHold";
        cap->recordSeconds = 2.5f;
        Scene loaded;
        CHECK(SceneSerializer::Deserialize(loaded, SceneSerializer::Serialize(sc)));
        IKTarget* t2 = nullptr;
        AimOffset* a2 = nullptr;
        PoseCapture* p2 = nullptr;
        for (const auto& up : loaded.Objects()) {
            if (auto* t = up->GetComponent<IKTarget>()) t2 = t;
            if (auto* a = up->GetComponent<AimOffset>()) a2 = a;
            if (auto* p = up->GetComponent<PoseCapture>()) p2 = p;
        }
        CHECK(t2 && t2->limb == IKTarget::RightHand && t2->characterName == "Hero");
        CHECK(t2 && std::fabs(t2->weight - 0.4f) < 1e-4f && std::fabs(t2->blendTime - 0.3f) < 1e-4f);
        CHECK(a2 && !a2->useCamera && a2->targetName == "Mark");
        CHECK(a2 && std::fabs(a2->kick - 5.0f) < 1e-4f && std::fabs(a2->chestWeight - 0.5f) < 1e-4f);
        CHECK(p2 && p2->clipName == "IdleHold" && std::fabs(p2->recordSeconds - 2.5f) < 1e-4f);
    }

    TEST_MAIN_RESULT();
}

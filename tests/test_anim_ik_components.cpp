#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Graphics/Image.hpp"
#include "okay/Scripting/OkayScriptVM.hpp"
#include "okay/Scene/SceneSerializer.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

using namespace okay;

static float Dist(const Vec3& a, const Vec3& b) { return (a - b).Magnitude(); }

static void RemovePoseTemps() {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::remove_all("/tmp/okay-pose-capture-d5f6", ec);
    for (fs::directory_iterator it("/tmp", ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.rfind("okay-pose-unique-", 0) == 0) fs::remove_all(it->path(), ec);
    }
}

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
        CHECK_NEAR(fb->bodyPull, 0.2f, 1e-3f);
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

    // A reachable point is where the hand ends, including near full stretch.
    {
        Scene sc;
        GameObject* hero = sc.CreateGameObject("Hero");
        GameObject* chest = Bone(sc, "Chest", hero, {0, 1.2f, 0});
        GameObject* arm = Bone(sc, "LeftArm", chest, {0.2f, 0.1f, 0});
        GameObject* fore = Bone(sc, "LeftForeArm", arm, {0.3f, 0, 0});
        GameObject* hand = Bone(sc, "LeftHand", fore, {0.3f, 0, 0});
        auto* fb = hero->AddComponent<FullBodyIK>();
        fb->autoMap = false;
        fb->bones[FullBodyIK::Chest] = chest->transform;
        fb->bones[FullBodyIK::LUpperArm] = arm->transform;
        fb->bones[FullBodyIK::LForeArm] = fore->transform;
        fb->bones[FullBodyIK::LHand] = hand->transform;
        sc.Start();
        const Vec3 shoulder = arm->transform->Position();
        const Vec3 goal = shoulder + Vec3{0.55f, -0.05f, 0.1f};
        GameObject* tgt = IKTarget::Spawn(sc, hero, IKTarget::LeftHand);
        tgt->transform->SetPosition(goal);
        auto* ik = tgt->GetComponent<IKTarget>();
        ik->blendTime = 0.15f;
        ik->weight = 1.0f;
        for (int i = 0; i < 45; ++i) sc.Update(1.0f / 60.0f);
        CHECK(Dist(hand->transform->Position(), tgt->transform->Position()) < 0.05f);
    }

    // Look-At on the spine must not pull a reached hand off its target.
    {
        Scene sc;
        GameObject* hero = sc.CreateGameObject("Hero");
        GameObject* hips = Bone(sc, "Hips", hero, {0, 1.0f, 0});
        GameObject* spine = Bone(sc, "Spine", hips, {0, 0.1f, 0});
        GameObject* spine2 = Bone(sc, "Spine2", spine, {0, 0.15f, 0});
        GameObject* neck = Bone(sc, "Neck", spine2, {0, 0.15f, 0});
        GameObject* head = Bone(sc, "Head", neck, {0, 0.12f, 0});
        GameObject* arm = Bone(sc, "LeftArm", spine2, {0.2f, 0.0f, 0});
        GameObject* fore = Bone(sc, "LeftForeArm", arm, {0.3f, 0, 0});
        GameObject* hand = Bone(sc, "LeftHand", fore, {0.3f, 0, 0});
        auto* fb = hero->AddComponent<FullBodyIK>();
        fb->autoMap = false;
        fb->softness = 0.0f;
        fb->bodyPull = 0.0f;
        fb->spineReach = 0.0f;
        fb->armMinBend = 0.0f;
        fb->blendSpeed = 0.0f;
        fb->bones[FullBodyIK::Hips] = hips->transform;
        fb->bones[FullBodyIK::Spine] = spine->transform;
        fb->bones[FullBodyIK::Chest] = spine2->transform;
        fb->bones[FullBodyIK::Neck] = neck->transform;
        fb->bones[FullBodyIK::Head] = head->transform;
        fb->bones[FullBodyIK::LUpperArm] = arm->transform;
        fb->bones[FullBodyIK::LForeArm] = fore->transform;
        fb->bones[FullBodyIK::LHand] = hand->transform;
        auto* look = hero->AddComponent<LookAtIK>();
        look->chain = {spine->transform, spine2->transform, neck->transform, head->transform};
        look->weight = 1.0f;
        look->smoothing = 0.0f;
        look->maxAngle = 80.0f;
        look->clampAngle = 120.0f;
        look->forwardAxis = Vec3{0, 0, -1};
        GameObject* seen = sc.CreateGameObject("Seen");
        seen->transform->SetPosition(head->transform->Position() + Vec3{2.0f, 0.2f, -0.4f});
        look->targetObject = seen->transform;
        look->aimAtPoint = true;
        sc.Start();
        const Vec3 shoulder = arm->transform->Position();
        GameObject* tgt = IKTarget::Spawn(sc, hero, IKTarget::LeftHand);
        tgt->transform->SetPosition(shoulder + Vec3{0.35f, -0.05f, 0.15f});
        auto* ik = tgt->GetComponent<IKTarget>();
        ik->blendTime = 0.0f;
        ik->weight = 1.0f;
        for (int i = 0; i < 45; ++i) sc.Update(1.0f / 60.0f);
        CHECK(Dist(hand->transform->Position(), tgt->transform->Position()) < 0.01f);
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
        aim->LateUpdate(0.016f);
        const Vec3 aimed = chest->transform->Rotation() * Vec3{0, 0, -1};
        const float pitchBefore = std::atan2(aimed.y, std::sqrt(aimed.x * aimed.x + aimed.z * aimed.z)) * 57.29578f;
        aim->Recoil(1.0f);
        CHECK(aim->RecoilPitch() > 10.0f);
        const Quat kickedFrom = chest->transform->Rotation();
        aim->LateUpdate(0.001f);
        CHECK(std::fabs(Quat::Dot(chest->transform->Rotation(), kickedFrom)) < 0.999f);
        const Vec3 kicked = chest->transform->Rotation() * Vec3{0, 0, -1};
        const float pitchAfter = std::atan2(kicked.y, std::sqrt(kicked.x * kicked.x + kicked.z * kicked.z)) * 57.29578f;
        CHECK(pitchAfter > pitchBefore + 5.0f);
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

    // Several spine bones share one turn. A target 25° up lifts the chest about that far.
    {
        Scene sc;
        GameObject* hero = sc.CreateGameObject("Hero");
        GameObject* s0 = Bone(sc, "Spine", hero, {0, 1.0f, 0});
        GameObject* s1 = Bone(sc, "Spine1", s0, {0, 0.12f, 0});
        GameObject* s2 = Bone(sc, "Spine2", s1, {0, 0.12f, 0});
        GameObject* chest = Bone(sc, "Chest", s2, {0, 0.12f, 0});
        auto* aim = hero->AddComponent<AimOffset>();
        aim->useCamera = false;
        aim->armWeight = 0.85f;
        aim->spineWeight = 0.35f;
        aim->chestWeight = 0.7f;
        aim->pitchClamp = 80.0f;
        GameObject* mark = sc.CreateGameObject("Up");
        sc.Start();
        const Vec3 from = chest->transform->Position();
        const float rad = 25.0f * 0.017453292f;
        mark->transform->SetPosition(from + Vec3{0.0f, std::sin(rad), -std::cos(rad)} * 4.0f);
        aim->target = mark->transform;
        aim->LateUpdate(0.016f);
        const Vec3 f = chest->transform->Rotation() * Vec3{0, 0, -1};
        const float got = std::atan2(f.y, std::sqrt(f.x * f.x + f.z * f.z)) * 57.29578f;
        CHECK(got > 15.0f && got < 35.0f);
    }

    // A third-person camera at its resting downward pitch does not bend the chest.
    {
        Scene sc;
        GameObject* hero = sc.CreateGameObject("Hero");
        auto* tp = hero->AddComponent<ThirdPersonController>();
        tp->pitch = 20.0f;
        GameObject* spine = Bone(sc, "Spine", hero, {0, 1.0f, 0});
        GameObject* chest = Bone(sc, "Chest", spine, {0, 0.3f, 0});
        auto* aim = hero->AddComponent<AimOffset>();
        aim->useCamera = true;
        aim->spineWeight = 0.0f;
        aim->armWeight = 0.0f;
        aim->chestWeight = 1.0f;
        aim->weight = 1.0f;
        GameObject* camGo = sc.CreateGameObject("Cam");
        auto* cam = camGo->AddComponent<Camera>();
        cam->main = true;
        camGo->transform->SetPosition({0.0f, 2.8f, 4.0f});
        camGo->transform->SetRotation(Quat::Euler(-20.0f, 0.0f, 0.0f));
        sc.Start();
        sc.mainCamera = cam;
        aim->LateUpdate(0.016f);
        const Vec3 f = chest->transform->Rotation() * Vec3{0, 0, -1};
        const float got = std::atan2(f.y, std::sqrt(f.x * f.x + f.z * f.z)) * 57.29578f;
        CHECK(std::fabs(got) < 2.0f);
        CHECK_NEAR(tp->restPitch(), 20.0f, 0.01f);
    }

    // Mixamo-like rig (model yawed 180, spine along +Y, head a little +Z):
    // recoil moves the head back and up, not forward into a bow.
    {
        Scene sc;
        GameObject* player = sc.CreateGameObject("Player");
        player->AddComponent<Character>();
        GameObject* model = sc.CreateGameObject("Xbot");
        model->transform->SetParent(player->transform, false);
        model->transform->localRotation = Quat::AngleAxis(180.0f, Vec3::Up);
        GameObject* hips = Bone(sc, "Hips", model, {0, 1.0f, 0});
        GameObject* spine = Bone(sc, "Spine", hips, {0, 0.15f, 0});
        GameObject* spine2 = Bone(sc, "Spine2", spine, {0, 0.2f, 0});
        GameObject* head = Bone(sc, "Head", spine2, {0, 0.25f, 0.08f});
        auto* aim = model->AddComponent<AimOffset>();
        aim->useCamera = false;
        aim->spineWeight = 0.0f;
        aim->armWeight = 0.0f;
        aim->chestWeight = 1.0f;
        aim->weight = 1.0f;
        aim->kick = 28.0f;
        aim->recovery = 0.0f;
        aim->pitchClamp = 80.0f;
        sc.Start();
        aim->LateUpdate(0.016f);
        const Vec3 before = head->transform->Position();
        aim->Recoil(1.0f);
        aim->LateUpdate(0.016f);
        const Vec3 d = head->transform->Position() - before;
        CHECK(d.y > 0.002f);
        CHECK(d.z > 0.01f);
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
        PoseCapture::SetAssetBaseDir("/tmp/okay-pose-capture-d5f6");
        const int idx = cap->CaptureFrame();
        CHECK(!cap->LastFile().empty());
        {
            std::ifstream file(cap->LastFile());
            CHECK(file.good());
        }
        ModelAnimator::Clip fromFile;
        CHECK(PoseCapture::LoadClip(cap->LastFile(), fromFile));
        CHECK(fromFile.name == "WavePose");
        bool armInFile = false;
        for (const auto& n : fromFile.nodes) if (n.node == "LeftArm" && n.clip.Length() > 0.0f) armInFile = true;
        CHECK(armInFile);
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
        RemovePoseTemps();
    }

    // Default captures get unique files. A rig with the same nodes loads both
    // back; a different rig in the scene does not.
    {
        const std::string dir = "/tmp/okay-pose-unique-d5f6";
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        PoseCapture::SetAssetBaseDir(dir);
        Scene sc;
        GameObject* hero = sc.CreateGameObject("Hero");
        auto* ma = hero->AddComponent<ModelAnimator>();
        Bone(sc, "LeftArm", hero, {0.2f, 1.0f, 0});
        auto* cap = hero->AddComponent<PoseCapture>();
        cap->clipName = "Captured";
        const int a = cap->CaptureFrame();
        const int b = cap->CaptureFrame();
        CHECK(a >= 0 && b >= 0 && a != b);
        CHECK(ma->clips[a].name == "Captured");
        CHECK(ma->clips[b].name == "Captured_1");
        CHECK(cap->LastFile().find("Captured_1.okayclip") != std::string::npos);

        Scene again;
        GameObject* hero2 = again.CreateGameObject("Hero");
        auto* ma2 = hero2->AddComponent<ModelAnimator>();
        Bone(again, "LeftArm", hero2, {0.2f, 1.0f, 0});
        GameObject* other = again.CreateGameObject("Other");
        auto* maOther = other->AddComponent<ModelAnimator>();
        Bone(again, "Spine", other, {0, 1.0f, 0});
        again.CreateGameObject("Loader")->AddComponent<PoseCapture>();
        again.Start();
        CHECK(PoseCapture::ImportFolder(*ma2, dir) == 2);
        CHECK(PoseCapture::ImportFolder(*maOther, dir) == 0);
        CHECK(maOther->clips.empty());
        bool first = false, second = false;
        for (const auto& c : ma2->clips) {
            if (c.name == "Captured") first = true;
            if (c.name == "Captured_1") second = true;
        }
        CHECK(first && second);
        RemovePoseTemps();
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

    // Blend time eases from rest on the first frame, and a turned character
    // gets the target in front of it. Switching limbs lets the old one go.
    {
        Scene sc;
        GameObject* hero = sc.CreateGameObject("Hero");
        hero->transform->SetRotation(Quat::Euler(0, 90, 0));
        GameObject* chest = Bone(sc, "Chest", hero, {0, 1.2f, 0});
        GameObject* arm = Bone(sc, "LeftArm", chest, {0.2f, 0.1f, 0});
        Bone(sc, "LeftForeArm", arm, {0.3f, 0, 0});
        Bone(sc, "LeftHand", arm, {0.25f, 0, 0});
        Bone(sc, "RightArm", chest, {-0.2f, 0.1f, 0});
        auto* fb = hero->AddComponent<FullBodyIK>();
        fb->bodyPull = 0.4f;
        sc.Start();
        GameObject* tgt = IKTarget::Spawn(sc, hero, IKTarget::LeftHand);
        const Vec3 fwd = hero->transform->Rotation() * Vec3{0, 0, -1};
        const Vec3 off = tgt->transform->Position() - hero->transform->Position();
        CHECK(Vec3::Dot(off, fwd) > 0.4f);
        auto* ik = tgt->GetComponent<IKTarget>();
        ik->blendTime = 1.0f;
        ik->weight = 1.0f;
        ik->LateUpdate(0.05f);
        CHECK(ik->ShownWeight() > 0.02f && ik->ShownWeight() < 0.5f);
        CHECK_NEAR(fb->bodyPull, 0.4f, 1e-3f);
        ik->blendTime = 0.0f;
        ik->LateUpdate(0.016f);
        CHECK(fb->effectors[FullBodyIK::LeftHand].enabled);
        ik->limb = IKTarget::RightHand;
        ik->LateUpdate(0.016f);
        CHECK(!fb->effectors[FullBodyIK::LeftHand].enabled);
        CHECK(fb->effectors[FullBodyIK::LeftHand].target == nullptr);
        CHECK(fb->effectors[FullBodyIK::RightHand].enabled);
        CHECK(fb->effectors[FullBodyIK::RightHand].target == tgt->transform);
        sc.Destroy(tgt);
        sc.FlushDestroyed();
        CHECK(!fb->effectors[FullBodyIK::RightHand].enabled);
        CHECK(fb->effectors[FullBodyIK::RightHand].target == nullptr);
    }

    // Chest weight edited after Start still aims, and only the arms aim when
    // the spine and chest weights are zero.
    {
        Scene sc;
        GameObject* hero = sc.CreateGameObject("Hero");
        GameObject* spine = Bone(sc, "Spine", hero, {0, 1.0f, 0});
        GameObject* chest = Bone(sc, "Chest", spine, {0, 0.3f, 0});
        GameObject* arm = Bone(sc, "LeftArm", chest, {0.3f, 0.1f, 0});
        auto* aim = hero->AddComponent<AimOffset>();
        aim->useCamera = false;
        aim->spineWeight = 0.0f;
        aim->chestWeight = 0.0f;
        aim->armWeight = 0.0f;
        sc.Start();
        GameObject* mark = sc.CreateGameObject("Side");
        mark->transform->SetPosition(chest->transform->Position() + Vec3{4, 0, 0});
        aim->target = mark->transform;
        aim->targetName = "Side";
        aim->chestWeight = 1.0f;
        const Quat before = chest->transform->Rotation();
        aim->LateUpdate(0.016f);
        CHECK(std::fabs(Quat::Dot(chest->transform->Rotation(), before)) < 0.999f);

        aim->chestWeight = 0.0f;
        aim->armWeight = 1.0f;
        const Quat armBefore = arm->transform->localRotation;
        aim->LateUpdate(0.016f);
        CHECK(std::fabs(Quat::Dot(arm->transform->localRotation, armBefore)) < 0.999f);

        GameObject* gun = Bone(sc, "Gun", hero, {0.2f, 1.0f, 0});
        OkayScriptVM vm;
        ScriptHost host;
        host.gameObject = gun;
        host.transform = gun->transform;
        vm.Bind(&host);
        const float kick = aim->RecoilPitch();
        CHECK(vm.Load("recoil(1)\n"));
        CHECK(aim->RecoilPitch() > kick + 5.0f);

        sc.Destroy(mark);
        sc.FlushDestroyed();
        aim->LateUpdate(0.016f);
        CHECK(aim->target == nullptr);
    }

    // A shoulder-offset camera looking down at its rest pitch does not hunch
    // or twist the chest, even when the crosshair hits the ground nearby.
    {
        Scene sc;
        GameObject* hero = sc.CreateGameObject("Hero");
        auto* tp = hero->AddComponent<ThirdPersonController>();
        tp->pitch = 18.0f;
        GameObject* spine = Bone(sc, "Spine", hero, {0, 1.0f, 0});
        GameObject* chest = Bone(sc, "Chest", spine, {0, 0.3f, 0});
        auto* aim = hero->AddComponent<AimOffset>();
        aim->useCamera = true;
        aim->spineWeight = 0.0f;
        aim->armWeight = 0.0f;
        aim->chestWeight = 1.0f;
        GameObject* floor = sc.CreateGameObject("Floor");
        floor->transform->SetPosition({0, -0.5f, 0});
        floor->transform->localScale = {80, 1, 80};
        floor->AddComponent<BoxCollider3D>();
        floor->isStatic = true;
        GameObject* camGo = sc.CreateGameObject("Cam");
        auto* cam = camGo->AddComponent<Camera>();
        cam->main = true;
        camGo->transform->SetPosition({0.7f, 2.6f, 3.4f});
        camGo->transform->SetRotation(Quat::Euler(-18.0f, 0.0f, 0.0f));
        sc.Start();
        sc.Update(0.0f);
        sc.mainCamera = cam;
        aim->LateUpdate(0.016f);
        const Vec3 f = chest->transform->Rotation() * Vec3{0, 0, -1};
        const float pitch = std::atan2(f.y, std::sqrt(f.x * f.x + f.z * f.z)) * 57.29578f;
        const float yaw = std::atan2(f.x, -f.z) * 57.29578f;
        CHECK(std::fabs(pitch) < 3.0f);
        CHECK(std::fabs(yaw) < 8.0f);
        CHECK_NEAR(tp->restPitch(), 18.0f, 0.01f);
    }

    // A hand stays on its target while an unclassified head bone still looks.
    {
        Scene sc;
        GameObject* hero = sc.CreateGameObject("Hero");
        GameObject* hips = Bone(sc, "Hips", hero, {0, 1.0f, 0});
        GameObject* spine = Bone(sc, "Spine", hips, {0, 0.15f, 0});
        GameObject* skull = Bone(sc, "Skull", spine, {0, 0.25f, 0});
        GameObject* arm = Bone(sc, "LeftArm", spine, {0.2f, 0, 0});
        GameObject* fore = Bone(sc, "LeftForeArm", arm, {0.3f, 0, 0});
        GameObject* hand = Bone(sc, "LeftHand", fore, {0.3f, 0, 0});
        auto* fb = hero->AddComponent<FullBodyIK>();
        fb->autoMap = false;
        fb->softness = 0.0f;
        fb->bodyPull = 0.0f;
        fb->spineReach = 0.0f;
        fb->armMinBend = 0.0f;
        fb->blendSpeed = 0.0f;
        fb->bones[FullBodyIK::Hips] = hips->transform;
        fb->bones[FullBodyIK::Spine] = spine->transform;
        fb->bones[FullBodyIK::LUpperArm] = arm->transform;
        fb->bones[FullBodyIK::LForeArm] = fore->transform;
        fb->bones[FullBodyIK::LHand] = hand->transform;
        auto* look = hero->AddComponent<LookAtIK>();
        look->chain = {spine->transform, skull->transform};
        look->weight = 1.0f;
        look->smoothing = 0.0f;
        look->maxAngle = 80.0f;
        look->clampAngle = 120.0f;
        look->forwardAxis = Vec3{0, 0, -1};
        GameObject* seen = sc.CreateGameObject("Seen");
        seen->transform->SetPosition(skull->transform->Position() + Vec3{1.5f, 0.3f, -0.2f});
        look->targetObject = seen->transform;
        look->aimAtPoint = true;
        sc.Start();
        const Quat spineBefore = spine->transform->Rotation();
        GameObject* tgt = IKTarget::Spawn(sc, hero, IKTarget::LeftHand);
        tgt->transform->SetPosition(arm->transform->Position() + Vec3{0.4f, -0.1f, 0.1f});
        auto* ik = tgt->GetComponent<IKTarget>();
        ik->blendTime = 0.0f;
        for (int i = 0; i < 30; ++i) sc.Update(1.0f / 60.0f);
        CHECK(Dist(hand->transform->Position(), tgt->transform->Position()) < 0.02f);
        CHECK(std::fabs(Quat::Dot(spine->transform->Rotation(), spineBefore)) > 0.999f);
        CHECK(std::fabs(Quat::Dot(skull->transform->Rotation(), Quat::Identity)) < 0.999f);
    }

    // A pose file is the bones, not the model's name or position. A subfolder
    // still loads, a truncated file does not, and a renamed rig with the same
    // bones does.
    {
        const std::string root = "/tmp/okay-pose-unique-bones";
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
        const std::string sub = root + "/Takes";
        std::filesystem::create_directories(sub, ec);
        PoseCapture::SetAssetBaseDir(sub);
        Scene sc;
        GameObject* hero = sc.CreateGameObject("Hero");
        hero->transform->localPosition = {4, 0, 1};
        auto* ma = hero->AddComponent<ModelAnimator>();
        GameObject* arm = Bone(sc, "Left Arm", hero, {0.2f, 1.0f, 0});
        arm->transform->localRotation = Quat::Euler(0, 15, 0);
        auto* cap = hero->AddComponent<PoseCapture>();
        cap->clipName = "Hold";
        const int idx = cap->CaptureFrame();
        CHECK(idx >= 0);
        hero->transform->localPosition = {0, 0, 0};
        ma->SetTime(0.0f);
        CHECK(Dist(hero->transform->localPosition, Vec3{0, 0, 0}) < 1e-3f);
        ModelAnimator::Clip loaded;
        CHECK(PoseCapture::LoadClip(cap->LastFile(), loaded));
        bool hostKeyed = false, spaced = false;
        for (const auto& n : loaded.nodes) {
            if (n.node == "Hero") hostKeyed = true;
            if (n.node == "Left Arm") spaced = true;
        }
        CHECK(!hostKeyed);
        CHECK(spaced);
        {
            std::ofstream bad(root + "/short.okayclip");
            bad << "okayclip 1\nname \"Short\"\nnodes 1\n";
        }
        ModelAnimator::Clip junk;
        CHECK(!PoseCapture::LoadClip(root + "/short.okayclip", junk));

        Scene again;
        GameObject* villain = again.CreateGameObject("Villain");
        auto* maV = villain->AddComponent<ModelAnimator>();
        Bone(again, "Left Arm", villain, {0.2f, 1.0f, 0});
        GameObject* other = again.CreateGameObject("Other");
        auto* maO = other->AddComponent<ModelAnimator>();
        Bone(again, "Spine", other, {0, 1, 0});
        CHECK(PoseCapture::ImportFolder(*maV, root) == 1);
        PoseCapture::ImportStats skipped;
        CHECK(PoseCapture::ImportFolder(*maO, root, &skipped) == 0);
        CHECK(skipped.skippedRig >= 1);
        CHECK(maV->clips.size() == 1 && maV->clips[0].name == "Hold");
        CHECK(PoseCapture::ImportFile(*maV, cap->LastFile(), true) == 1);
        CHECK(maV->clips.size() == 1);
        CHECK(PoseCapture::ImportFile(*maO, cap->LastFile(), true) == 0);
        std::filesystem::remove_all(root, ec);
        PoseCapture::SetAssetBaseDir({});
    }

    // Offscreen pose of the real solvers: a reached hand, and recoil that
    // lifts the head. This is not an editor Play session.
    {
        auto save = [](const Scene& sc, GameObject* root, const Vec3* mark, const std::string& path) {
            Image img(640, 480);
            img.Fill(Color(0.10f, 0.11f, 0.14f, 1.0f));
            const Vec3 origin = root && root->transform ? root->transform->Position() : Vec3::Zero;
            auto project = [&](const Vec3& p) {
                const float x = 320.0f + (-(p.z - origin.z)) * 260.0f;
                const float y = 400.0f - (p.y - origin.y) * 260.0f;
                return std::pair<int, int>((int)x, (int)y);
            };
            auto dot = [&](int x, int y, Color c, int r) {
                for (int dy = -r; dy <= r; ++dy)
                    for (int dx = -r; dx <= r; ++dx)
                        if (dx * dx + dy * dy <= r * r) img.SetPixel(x + dx, y + dy, c);
            };
            auto line = [&](Vec3 a, Vec3 b, Color c) {
                auto pa = project(a), pb = project(b);
                int x0 = pa.first, y0 = pa.second, x1 = pb.first, y1 = pb.second;
                int dx = std::abs(x1 - x0), dy = std::abs(y1 - y0);
                int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx - dy;
                for (int n = 0; n < 2000; ++n) {
                    dot(x0, y0, c, 1);
                    if (x0 == x1 && y0 == y1) break;
                    int e2 = 2 * err;
                    if (e2 > -dy) { err -= dy; x0 += sx; }
                    if (e2 < dx) { err += dx; y0 += sy; }
                }
            };
            for (const auto& up : sc.Objects()) {
                if (!up || !up->transform || !up->transform->Parent()) continue;
                if (root && !up->IsSelfOrDescendantOf(root)) continue;
                line(up->transform->Parent()->Position(), up->transform->Position(), Color(0.95f, 0.78f, 0.35f, 1.0f));
                auto j = project(up->transform->Position());
                dot(j.first, j.second, Color(1.0f, 0.95f, 0.8f, 1.0f), 3);
            }
            if (mark) {
                auto m = project(*mark);
                dot(m.first, m.second, Color(0.35f, 0.75f, 1.0f, 1.0f), 6);
            }
            std::filesystem::create_directories("/opt/cursor/artifacts");
            img.SavePNG(path);
        };

        Scene reach;
        GameObject* hero = reach.CreateGameObject("Hero");
        GameObject* chest = Bone(reach, "Chest", hero, {0, 1.2f, 0});
        GameObject* arm = Bone(reach, "LeftArm", chest, {0.15f, 0.1f, 0});
        GameObject* fore = Bone(reach, "LeftForeArm", arm, {0.3f, 0, 0});
        GameObject* hand = Bone(reach, "LeftHand", fore, {0.3f, 0, 0});
        hero->AddComponent<FullBodyIK>()->autoMap = false;
        auto* fb = hero->GetComponent<FullBodyIK>();
        fb->bones[FullBodyIK::Chest] = chest->transform;
        fb->bones[FullBodyIK::LUpperArm] = arm->transform;
        fb->bones[FullBodyIK::LForeArm] = fore->transform;
        fb->bones[FullBodyIK::LHand] = hand->transform;
        reach.Start();
        GameObject* tgt = IKTarget::Spawn(reach, hero, IKTarget::LeftHand);
        tgt->transform->SetPosition(arm->transform->Position() + Vec3{0.45f, 0.15f, -0.2f});
        tgt->GetComponent<IKTarget>()->blendTime = 0.0f;
        for (int i = 0; i < 20; ++i) reach.Update(1.0f / 60.0f);
        CHECK(Dist(hand->transform->Position(), tgt->transform->Position()) < 0.05f);
        const Vec3 reachAt = tgt->transform->Position();
        save(reach, hero, &reachAt, "/opt/cursor/artifacts/ik-hand-reach.png");

        Scene kick;
        GameObject* player = kick.CreateGameObject("Player");
        player->AddComponent<Character>();
        GameObject* model = kick.CreateGameObject("Xbot");
        model->transform->SetParent(player->transform, false);
        model->transform->localRotation = Quat::AngleAxis(180.0f, Vec3::Up);
        GameObject* hips = Bone(kick, "Hips", model, {0, 1.0f, 0});
        GameObject* spine = Bone(kick, "Spine", hips, {0, 0.2f, 0});
        GameObject* spine2 = Bone(kick, "Spine2", spine, {0, 0.2f, 0});
        GameObject* head = Bone(kick, "Head", spine2, {0, 0.25f, 0.08f});
        auto* aim = model->AddComponent<AimOffset>();
        aim->useCamera = false;
        aim->spineWeight = 0.0f;
        aim->chestWeight = 1.0f;
        aim->armWeight = 0.0f;
        aim->kick = 28.0f;
        aim->recovery = 0.0f;
        aim->pitchClamp = 70.0f;
        kick.Start();
        aim->LateUpdate(0.016f);
        save(kick, player, nullptr, "/opt/cursor/artifacts/aim-before-recoil.png");
        const Vec3 headBefore = head->transform->Position();
        aim->Recoil(1.0f);
        aim->LateUpdate(0.016f);
        const Vec3 headDelta = head->transform->Position() - headBefore;
        CHECK(headDelta.y > 0.002f);
        CHECK(headDelta.z > 0.01f);
        save(kick, player, nullptr, "/opt/cursor/artifacts/aim-recoil-up.png");
    }

    TEST_MAIN_RESULT();
}

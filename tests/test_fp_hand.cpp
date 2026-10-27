#include "test_framework.hpp"
#include <Okay.hpp>
#include "okay/Components/FpHandsPreview.hpp"
#include "okay/Scripting/OkayScriptVM.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>

using namespace okay;

// FirstPersonHand builds the part rig and raises a COPY of the screen-right arm
// for the owner's camera only. The real arm bones stay on the animation. The
// head and the neck stay in the world; this camera skips them.
int main() {
    RUN_SUITE("fp_hand");

    {
        Scene scene("FPS");
        GameObject* player = scene.CreateGameObject("Player");
        auto* ch = player->AddComponent<Character>(); ch->Apply();   // not separated yet
        player->AddComponent<FirstPersonController>();
        player->AddComponent<FirstPersonHands>()->showWhenEmpty = true;   // this scene still checks the raised hand

        GameObject* cam = scene.CreateGameObject("Eye");
        cam->transform->SetParent(player->transform, false);
        cam->transform->localPosition = {0, 1.62f, 0};
        auto* camera = cam->AddComponent<Camera>();
        cam->AddComponent<FirstPersonHand>();

        scene.Start();
        for (int i = 0; i < 3; ++i) scene.Update(0.016f);

        // The hand auto-builds the rig. The raised arm is a copy. The real bones
        // (including the other arm) stay out of this camera and on the animation.
        CHECK(ch->separateParts);
        CHECK(ch->PartsBuilt());
        CHECK(ch->firstPersonArm);
        CHECK(ch->fpArmBase == 3);
        CHECK(ch->Part(3) && ch->Part(3)->active && !ch->Part(3)->firstPersonViewmodel);
        CHECK(ch->Part(4) && !ch->Part(4)->firstPersonViewmodel);
        CHECK(ch->Part(5) && !ch->Part(5)->firstPersonViewmodel);
        // The head stays active (shadows, other cameras) but this camera skips it.
        // The shirt stays in this camera: its lid is what a look down sees, and
        // without it the open top shows the world inside the chest.
        CHECK(ch->Part(2) && ch->Part(2)->active && !ch->Part(2)->firstPersonViewmodel);  // head
        CHECK(ch->Part(1) && ch->Part(1)->active && ch->Part(1)->firstPersonViewmodel);   // shirt
        CHECK(ch->Part(6) && !ch->Part(6)->firstPersonViewmodel);                         // other arm, real bone
        GameObject* copy = ch->OwnerViewArm();
        CHECK(copy && copy->active && copy->firstPersonOnly && copy->firstPersonViewmodel);
        if (copy) if (auto* cmr = copy->GetComponent<MeshRenderer>()) CHECK(!cmr->castShadows);
        GameObject* neck = nullptr;
        if (ch->Part(1) && ch->Part(1)->transform)
            for (Transform* c : ch->Part(1)->transform->Children())
                if (c && c->gameObject && c->gameObject->name == "Neck") neck = c->gameObject;
        CHECK(neck && neck->active && !neck->firstPersonViewmodel);
        if (ch->Part(1))
            if (auto* tmr = ch->Part(1)->GetComponent<MeshRenderer>()) CHECK(!tmr->doubleSided);
        CHECK(camera->nearClip < 0.06f);
        // Body hidden from the OWNER only where it is not the viewmodel: the camera ignores the player subtree.
        CHECK(camera->ignoreObject == player);
        // The old camera-child "FP_Arm" is not used. The copy lives under the rig.
        CHECK(scene.Find("FP_Arm") == nullptr);
        CHECK(player->GetComponent<MeshRenderer>() && !player->GetComponent<MeshRenderer>()->enabled);
        // The hand is the one brick from before the finger mesh (commit 6585c7c1,
        // #430): eight corners, centred on the wrist. A palm, thumb and three
        // finger boxes was five cubes (40 vertices) on this same bone.
        CHECK(Character::BoneCount() == 15);
        for (int i = 0; i < Character::BoneCount(); ++i) {
            const char* name = Character::BoneName(i);
            CHECK(name && !std::strstr(name, "finger") && !std::strstr(name, "thumb"));
        }
        std::vector<int> bone;
        Mesh rig = ch->BuildRig(bone);
        int handVerts = 0;
        const int hand = Character::BoneIndex("l_hand");
        Vec3 mn{1e9f, 1e9f, 1e9f}, mx{-1e9f, -1e9f, -1e9f};
        for (int i = 0; i < (int)bone.size() && i < (int)rig.vertices.size(); ++i) {
            if (bone[i] != hand) continue;
            ++handVerts;
            const Vec3& v = rig.vertices[i];
            mn.x = std::min(mn.x, v.x); mn.y = std::min(mn.y, v.y); mn.z = std::min(mn.z, v.z);
            mx.x = std::max(mx.x, v.x); mx.y = std::max(mx.y, v.y); mx.z = std::max(mx.z, v.z);
        }
        CHECK(handVerts == 8);
        CHECK_NEAR(mn.x, -0.33f, 1e-3f); CHECK_NEAR(mx.x, -0.21f, 1e-3f);
        CHECK_NEAR(mn.y,  0.74f, 1e-3f); CHECK_NEAR(mx.y,  0.86f, 1e-3f);
        CHECK_NEAR(mn.z, -0.065f, 1e-3f); CHECK_NEAR(mx.z, 0.105f, 1e-3f);

        // Running takes the hands off the owner's screen. Walking leaves them up.
        auto* fpc = player->GetComponent<FirstPersonController>();
        fpc->driveAnimation = false;   // keep the anim we set; the body is standing still
        ch->anim = 3;
        scene.Update(0.016f);
        CHECK(ch->HideFirstPersonHands());
        CHECK(ch->fpArmUp == 0.0f);
        CHECK(ch->Part(3) && !ch->Part(3)->firstPersonViewmodel);
        CHECK(ch->Part(5) && !ch->Part(5)->firstPersonViewmodel);
        CHECK(ch->Part(6) && !ch->Part(6)->firstPersonViewmodel);   // both arms, not just the raised one
        CHECK(ch->OwnerViewArm());   // a run eases the copy off; one frame has not finished that
        for (int i = 0; i < 45; ++i) scene.Update(0.016f);
        CHECK(ch->OwnerViewArm() && !ch->OwnerViewArm()->active);   // the copy is down once the ease finishes
        CHECK(ch->Part(1) && ch->Part(1)->active && ch->Part(1)->firstPersonViewmodel);  // shirt stays while the arms drop
        CHECK(ch->Part(2) && ch->Part(2)->active && !ch->Part(2)->firstPersonViewmodel);
        ch->anim = 2;
        for (int i = 0; i < 30; ++i) scene.Update(0.016f);   // the run style eases out over ~0.2 s
        CHECK(!ch->HideFirstPersonHands());
        CHECK(ch->fpArmUp == 1.0f);
        CHECK(ch->Part(5) && !ch->Part(5)->firstPersonViewmodel);   // the real hand stays the world's
        CHECK(ch->OwnerViewArm() && ch->OwnerViewArm()->active && ch->OwnerViewArm()->firstPersonOnly);
        {
            const Quat raised = Quat::Euler(ch->fpRaise - ch->fpPitch, 0.0f, 0.0f);
            CHECK(Quat::Angle(ch->Part(3)->transform->localRotation, raised) > 40.0f);
            // A small copy on the player, in front of the near plane. Not the old
            // full-size bone cranked to fpRaise.
            GameObject* arm = ch->OwnerViewArm();
            Transform* armParent = arm->transform->Parent();
            CHECK(armParent == player->transform
                || (armParent && armParent->gameObject && armParent->gameObject->name == "FP_ViewAnchor"
                    && armParent->Parent() == player->transform));
            CHECK(arm->transform->localScale.x > 0.25f);
            CHECK(arm->transform->localScale.x < 0.85f);
            float minAhead = 1.0e9f;
            auto walk = [&](auto&& self, GameObject* g) -> void {
                if (!g || !g->active || !g->transform) return;
                if (auto* mr = g->GetComponent<MeshRenderer>())
                    for (const Vec3& v : mr->mesh.vertices) {
                        const Vec3 local = cam->transform->InverseTransformPoint(g->transform->TransformPoint(v));
                        minAhead = std::min(minAhead, -local.z);
                    }
                for (Transform* c : g->transform->Children())
                    if (c && c->gameObject) self(self, c->gameObject);
            };
            walk(walk, arm);
            CHECK(minAhead >= 0.15f);
        }
        // The sleeve stops short of the hand, so the two cubes do not share a face.
        {
            const int fore = Character::BoneIndex("l_fore");
            float foreMinY = 1.0e9f, handMaxY = -1.0e9f;
            for (int i = 0; i < (int)bone.size() && i < (int)rig.vertices.size(); ++i) {
                if (bone[i] == fore) foreMinY = std::min(foreMinY, rig.vertices[i].y);
                if (bone[i] == hand) handMaxY = std::max(handMaxY, rig.vertices[i].y);
            }
            CHECK(foreMinY >= handMaxY - 1e-4f);
        }
    }

    // Left-arm option.
    {
        Scene scene("L");
        GameObject* player = scene.CreateGameObject("Player");
        player->AddComponent<Character>()->Apply();
        player->AddComponent<FirstPersonController>();
        auto* hands = player->AddComponent<FirstPersonHands>();
        hands->showWhenEmpty = true;
        hands->holdMode = FirstPersonHands::HoldMode::Left;
        GameObject* cam = scene.CreateGameObject("Eye");
        cam->transform->SetParent(player->transform, false);
        cam->AddComponent<Camera>();
        cam->AddComponent<FirstPersonHand>()->showLeftArm = true;
        scene.Start();
        for (int i = 0; i < 3; ++i) scene.Update(0.016f);
        auto* ch = player->GetComponent<Character>();
        CHECK(ch->fpArmBase == 6);                       // the raised (screen-left) arm
        CHECK(ch->Part(6) && !ch->Part(6)->firstPersonViewmodel);
        CHECK(ch->Part(3) && !ch->Part(3)->firstPersonViewmodel);  // both real arms stay the body
        CHECK(ch->OwnerViewArm() && ch->OwnerViewArm()->firstPersonOnly && ch->OwnerViewArm()->active);
        CHECK(ch->Part(2) && !ch->Part(2)->firstPersonViewmodel);  // head stays off this camera
    }

    // A sprint drops the raised arm (and a held item) out of the local view.
    {
        Scene scene("Run");
        GameObject* player = scene.CreateGameObject("Player");
        auto* ch = player->AddComponent<Character>(); ch->Apply();
        player->AddComponent<FirstPersonController>();
        GameObject* cam = scene.CreateGameObject("Eye");
        cam->transform->SetParent(player->transform, false);
        cam->AddComponent<Camera>();
        auto* fh = cam->AddComponent<FirstPersonHand>();
        GameObject* item = scene.CreateGameObject("Crate");
        item->transform->localPosition = {0.4f, 1.0f, -0.4f};
        scene.Start();
        for (int i = 0; i < 3; ++i) scene.Update(0.016f);
        CHECK(player->GetComponent<ProceduralObjectHolding>() == nullptr);
        player->AddComponent<ProceduralObjectHolding>()->Pickup(item);
        player->GetComponent<FirstPersonController>()->driveAnimation = false;
        ch->anim = 3;
        for (int i = 0; i < 4; ++i) scene.Update(0.016f);
        CHECK(ch->fpArmUp <= 0.05f);
        CHECK(ch->Part(3) && !ch->Part(3)->firstPersonViewmodel);
        CHECK(ch->Part(4) && !ch->Part(4)->firstPersonViewmodel);
        CHECK(ch->Part(5) && !ch->Part(5)->firstPersonViewmodel);
        CHECK(item->ownerCameraSkip == player);

        fh->showWhileRunning = true;
        for (int i = 0; i < 4; ++i) scene.Update(0.016f);
        CHECK(ch->fpArmUp > 0.5f);
        CHECK(ch->Part(3) && !ch->Part(3)->firstPersonViewmodel);
        CHECK(item->ownerCameraSkip == player);

        ch->anim = 2;
        fh->showWhileRunning = false;
        // Walk and run share a style blend. The copy stays down until that
        // blend has left the run, about a fifth of a second from a full sprint.
        for (int i = 0; i < 20; ++i) scene.Update(0.016f);
        CHECK(ch->fpArmUp > 0.5f);
        CHECK(ch->Part(3) && !ch->Part(3)->firstPersonViewmodel);
        CHECK(item->ownerCameraSkip == player);
    }

    // Serialization round-trips the settings.
    {
        Scene a("A");
        GameObject* go = a.CreateGameObject("Cam");
        go->AddComponent<Camera>();
        auto* fh = go->AddComponent<FirstPersonHand>();
        fh->attackButton = 1; fh->holdToSwing = false; fh->showLeftArm = true;
        fh->followPitch = false; fh->bobbing = false;
        std::string text = SceneSerializer::Serialize(a);
        Scene b("B");
        CHECK(SceneSerializer::Deserialize(b, text));
        auto* lf = b.Find("Cam") ? b.Find("Cam")->GetComponent<FirstPersonHand>() : nullptr;
        CHECK(lf != nullptr);
        if (lf) {
            CHECK(lf->attackButton == 1);
            CHECK(lf->holdToSwing == false);
            CHECK(lf->showLeftArm == true);
            CHECK(lf->followPitch == false);
            CHECK(lf->bobbing == false);
        }
    }

    // The owner's camera skips the head. Another camera still sees it, and the
    // head's shadow is part of that picture. The neck goes with the head.
    {
        ShadowMapResolution() = 256;
        ShadowCascades() = 1;
        Scene sc("own");
        sc.physicsEnabled = false;
        GameObject* light = sc.CreateGameObject("Directional Light");
        light->AddComponent<Light>();
        light->transform->localRotation = Quat::Euler({50, -30, 0});
        GameObject* ground = sc.CreateGameObject("Ground");
        ground->transform->localPosition = {0, -0.5f, 0};
        ground->transform->localScale = {12, 1, 12};
        auto* gmr = ground->AddComponent<MeshRenderer>();
        gmr->mesh = Mesh::Cube();
        gmr->color = Color::FromBytes(180, 180, 180);
        GameObject* player = sc.CreateGameObject("Player");
        player->transform->localPosition = {0, 0, 0};
        auto* ch = player->AddComponent<Character>();
        ch->Apply();
        player->AddComponent<FirstPersonHands>()->showWhenEmpty = true;
        auto* fp = player->AddComponent<FirstPersonController>();
        fp->lookSmoothing = 0.0f; fp->headBob = false;
        GameObject* eye = sc.CreateGameObject("Eye");
        auto* owner = eye->AddComponent<Camera>();
        eye->transform->SetParent(player->transform, false);
        eye->transform->localPosition = {0, 1.6f, 0};
        auto* pv = player->AddComponent<PlayerView>();
        pv->allowToggle = true; pv->defaultView = PlayerView::View::First; pv->blendTime = 0.05f;
        sc.Start();
        Input::FeedKeys({});
        for (int i = 0; i < 10; ++i) sc.Update(1.0f / 60.0f);

        CHECK(ch->PartsBuilt());
        CHECK(!ch->hideOwnHead);
        GameObject* head = ch->Part(2);
        CHECK(head && head->active && !head->firstPersonViewmodel);
        CHECK(head->transform && (head->transform->localScale - Vec3::One).Magnitude() < 1e-3f);
        auto* hmr = head->GetComponent<MeshRenderer>();
        CHECK(hmr && hmr->enabled && hmr->castShadows && !hmr->mesh.vertices.empty());
        const Vec3 rel = eye->transform->Position() - player->transform->Position();
        CHECK(rel.z < -0.18f);
        CHECK(owner->ignoreObject == player);

        auto shot = [&](Camera* cam, int W, int H) {
            ApplySceneLight(sc);
            const float aspect = (float)W / (float)H;
            Mat4 vp = cam->ProjectionMatrix(aspect) * cam->ViewMatrix();
            Raster work; std::vector<std::uint32_t> out;
            const std::uint32_t* px = RenderMeshesSS(work, out, sc, vp, cam->transform->Position(), W, H, 1, cam->ignoreObject);
            std::vector<std::uint32_t> copy(px, px + W * H);
            return copy;
        };
        auto differ = [](const std::vector<std::uint32_t>& a, const std::vector<std::uint32_t>& b) {
            int n = 0;
            for (std::size_t i = 0; i < a.size(); ++i) if (a[i] != b[i]) ++n;
            return n;
        };
        const int W = 160, H = 90;
        auto ownA = shot(owner, W, H);
        hmr->enabled = false;
        auto ownB = shot(owner, W, H);
        hmr->enabled = true;
        CHECK(differ(ownA, ownB) == 0);   // the owner's lens does not see the head

        GameObject* side = sc.CreateGameObject("Side");
        auto* sideCam = side->AddComponent<Camera>();
        sideCam->main = false;
        sideCam->nearClip = 0.05f;
        side->transform->SetPosition({2.4f, 1.4f, 0.2f});
        side->transform->SetRotation(Quat::LookRotation(side->transform->Position() - Vec3{0, 1.3f, 0}));
        auto sideA = shot(sideCam, W, H);
        hmr->enabled = false;
        auto sideB = shot(sideCam, W, H);
        hmr->enabled = true;
        CHECK(differ(sideA, sideB) > 40);   // another camera still draws the head

        // That camera also draws the real blocky hand, and not the raised copy.
        GameObject* copyHand = nullptr;
        if (GameObject* arm = ch->OwnerViewArm())
            if (arm->transform)
                for (Transform* fore : arm->transform->Children())
                    if (fore)
                        for (Transform* h : fore->Children())
                            if (h && h->gameObject && h->gameObject->name == "FP_OwnerHand")
                                copyHand = h->gameObject;
        CHECK(copyHand && copyHand->firstPersonOnly && copyHand->active);
        auto* realHand = ch->Part(5) ? ch->Part(5)->GetComponent<MeshRenderer>() : nullptr;
        CHECK(realHand && (int)realHand->mesh.vertices.size() == 36);
        auto aim = [&](const Vec3& at, const Vec3& eye) {
            side->transform->SetPosition(eye);
            side->transform->SetRotation(Quat::LookRotation(eye - at));
        };
        // Close on the real hand. The wide shot that frames the head makes the
        // brick a few pixels, which is not a useful difference.
        const Vec3 handAt = ch->Part(5)->transform->Position();
        aim(handAt, handAt + Vec3{0.38f, 0.12f, -0.30f});
        auto sideHand = shot(sideCam, W, H);
        realHand->enabled = false;
        auto sideNoHand = shot(sideCam, W, H);
        realHand->enabled = true;
        CHECK(differ(sideHand, sideNoHand) > 40);
        auto* copyMr = copyHand->GetComponent<MeshRenderer>();
        const Vec3 copyAt = copyHand->transform->Position();
        aim(copyAt, copyAt + Vec3{0.38f, 0.10f, -0.30f});
        auto sideWithCopy = shot(sideCam, W, H);
        copyMr->enabled = false;
        auto sideNoCopy = shot(sideCam, W, H);
        copyMr->enabled = true;
        CHECK(differ(sideWithCopy, sideNoCopy) == 0);

        auto ownHand = shot(owner, W, H);
        copyMr->enabled = false;
        auto ownNoCopy = shot(owner, W, H);
        copyMr->enabled = true;
        CHECK(differ(ownHand, ownNoCopy) > 40);   // the owner sees the copy
        auto ownReal = shot(owner, W, H);
        realHand->enabled = false;
        auto ownNoReal = shot(owner, W, H);
        realHand->enabled = true;
        CHECK(differ(ownReal, ownNoReal) == 0);   // and not the animated hand

        // Crouch, prone, walk, run and a look down do not take the head or the
        // real hand off this other camera, and they do not pitch the real arm.
        fp->driveAnimation = false;
        ch->animSpeed = 0.0f;
        const Quat armBefore = ch->Part(3)->transform->localRotation;
        fp->pitch = 70.0f;
        for (int i = 0; i < 8; ++i) sc.Update(1.0f / 60.0f);
        CHECK(Quat::Angle(ch->Part(3)->transform->localRotation, armBefore) < 2.0f);
        CHECK(Quat::Angle(ch->OwnerViewArm()->transform->localRotation, armBefore) > 20.0f);
        fp->pitch = 0.0f;
        auto poseFromSide = [&](int state) {
            ch->anim = state;
            ch->animTime = 0.0f;
            for (int i = 0; i < 4; ++i) sc.Update(1.0f / 60.0f);
            CHECK(ch->Part(2)->active);
            CHECK((ch->Part(2)->transform->localScale - Vec3::One).Magnitude() < 1e-3f);
            const Vec3 headAt = ch->Part(2)->transform->Position();
            aim(headAt, headAt + Vec3{1.1f, 0.15f, 0.85f});
            auto a = shot(sideCam, W, H);
            hmr->enabled = false;
            auto b = shot(sideCam, W, H);
            hmr->enabled = true;
            CHECK(differ(a, b) > 40);
            const Vec3 at = ch->Part(5)->transform->Position();
            aim(at, at + Vec3{0.38f, 0.12f, -0.30f});
            auto c0 = shot(sideCam, W, H);
            realHand->enabled = false;
            auto c1 = shot(sideCam, W, H);
            realHand->enabled = true;
            CHECK(differ(c0, c1) > 40);
            if (copyHand->active) {
                const Vec3 cat = copyHand->transform->Position();
                aim(cat, cat + Vec3{0.38f, 0.10f, -0.30f});
                auto d0 = shot(sideCam, W, H);
                copyMr->enabled = false;
                auto d1 = shot(sideCam, W, H);
                copyMr->enabled = true;
                CHECK(differ(d0, d1) == 0);
            }
        };
        poseFromSide(1);
        poseFromSide(2);
        poseFromSide(6);
        poseFromSide(7);
        poseFromSide(3);
        for (int i = 0; i < 45; ++i) sc.Update(1.0f / 60.0f);
        CHECK(ch->OwnerViewArm() && !ch->OwnerViewArm()->active);   // a run eases the copy away

        // Per-pixel shading is what samples the shadow map. The head's shadow
        // changes that picture while the head mesh itself stays drawn.
        ch->anim = 1;
        ch->animTime = 0.0f;
        fp->pitch = 0.0f;
        for (int i = 0; i < 5; ++i) sc.Update(1.0f / 60.0f);
        PerPixelLighting() = true;
        const Vec3 feet = player->transform->Position();
        side->transform->SetPosition(feet + Vec3{3.2f, 1.6f, 2.4f});
        side->transform->SetRotation(Quat::LookRotation(side->transform->Position() - (feet + Vec3{0, 0.6f, 0})));
        hmr->castShadows = true;
        auto shA = shot(sideCam, W, H);
        hmr->castShadows = false;
        auto shB = shot(sideCam, W, H);
        hmr->castShadows = true;
        PerPixelLighting() = false;
        CHECK(differ(shA, shB) > 0);
    }

    // Host on 127.0.0.1, one client. The host plays in first person. The
    // client's copy of that player keeps the animation, the head, and the
    // blocky hands. The raised arm never arrives.
    {
        Scene host("Host");
        auto* srv = host.CreateGameObject("Net")->AddComponent<NetworkManager>();
        srv->interpolationRate = 0.0f;
        CHECK(srv->StartServer(0));
        GameObject* player = host.CreateGameObject("Player");
        player->transform->localPosition = {0, 1, 0};
        auto* ch = player->AddComponent<Character>();
        ch->Apply();
        player->AddComponent<FirstPersonHands>()->showWhenEmpty = true;
        ch->separateParts = true;
        auto* tp = player->AddComponent<ThirdPersonController>();
        tp->driveAnimation = false;
        tp->lookSmoothing = 0.0f;
        tp->yaw = 0.0f;
        tp->pitch = 0.0f;
        host.CreateGameObject("Camera")->AddComponent<Camera>();
        auto* pv = player->AddComponent<PlayerView>();
        pv->allowToggle = true;
        pv->defaultView = PlayerView::View::First;
        pv->blendTime = 0.05f;
        auto* ns = player->AddComponent<NetworkSync>();
        ns->netId = "pawn";
        ns->authority = NetworkSync::Authority::Host;

        Scene client("Client");
        auto* cli = client.CreateGameObject("Net")->AddComponent<NetworkManager>();
        cli->interpolationRate = 0.0f;
        CHECK(cli->StartClient("127.0.0.1", srv->ServerPort()));
        GameObject* ghost = client.CreateGameObject("Player");
        ghost->transform->localPosition = {0, 1, 0};
        auto* gh = ghost->AddComponent<Character>();
        gh->Apply();
        gh->separateParts = true;
        auto* gns = ghost->AddComponent<NetworkSync>();
        gns->netId = "pawn";
        gns->authority = NetworkSync::Authority::Host;
        host.Start();
        client.Start();

        auto pump = [&](int n) {
            for (int i = 0; i < n; ++i) {
                host.Update(1.0f / 60.0f);
                client.Update(1.0f / 60.0f);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        };
        for (int i = 0; i < 200 && !(cli->Joined() && srv->PeerCount() >= 1); ++i) pump(1);
        CHECK(cli->Joined());

        auto match = [&](int state) {
            ch->anim = state;
            ch->animSpeed = 0.0f;
            ch->animTime = 0.0f;
            gh->animSpeed = 0.0f;
            gh->animTime = 0.0f;
            pump(25);
            CHECK(gh->anim == state);
            CHECK(!gh->firstPersonArm);
            CHECK(gh->fpArmBase == -1);
            CHECK(gh->fpPitch == 0.0f);
            CHECK(!gh->gameObject->GetComponent<PlayerView>());
            CHECK(gh->OwnerViewArm() == nullptr);
            CHECK(gh->PartsBuilt());
            CHECK(gh->Part(2) && gh->Part(2)->active);
            CHECK((gh->Part(2)->transform->localScale - Vec3::One).Magnitude() < 1e-3f);
            if (auto* hmr = gh->Part(2)->GetComponent<MeshRenderer>()) CHECK(hmr->castShadows);
            CHECK(gh->Part(5) && gh->Part(5)->active);
            auto* hand = gh->Part(5)->GetComponent<MeshRenderer>();
            CHECK(hand && hand->enabled && (int)hand->mesh.vertices.size() == 36);
            CHECK(ch->Part(3) && gh->Part(3));
            if (state == 1) {
                CHECK(Quat::Angle(gh->Part(3)->transform->localRotation,
                                  ch->Part(3)->transform->localRotation) < 5.0f);
                if (tp->pitch > -1.0f && tp->pitch < 1.0f && ch->OwnerViewArm() && ch->OwnerViewArm()->active) {
                    const Quat raised = Quat::Euler(ch->fpRaise - ch->fpPitch, 0.0f, 0.0f);
                    CHECK(Quat::Angle(ch->Part(3)->transform->localRotation, raised) > 40.0f);
                    Transform* armParent = ch->OwnerViewArm()->transform->Parent();
                    CHECK(armParent == player->transform
                        || (armParent && armParent->gameObject && armParent->gameObject->name == "FP_ViewAnchor"
                            && armParent->Parent() == player->transform));
                    CHECK(ch->OwnerViewArm()->transform->localScale.x > 0.25f);
                    CHECK(ch->OwnerViewArm()->transform->localScale.x < 0.85f);
                }
            }
            if (ch->OwnerViewArm() && ch->OwnerViewArm()->active)
                CHECK(Quat::Angle(gh->Part(3)->transform->localRotation,
                                  ch->OwnerViewArm()->transform->localRotation) > 25.0f);
            CHECK((ch->Part(2)->transform->localScale - Vec3::One).Magnitude() < 1e-3f);
        };
        pump(20);
        CHECK(ch->firstPersonArm);
        CHECK(ch->OwnerViewArm() && ch->OwnerViewArm()->firstPersonOnly);
        match(1);
        {
            const Quat realLevel = ch->Part(3)->transform->localRotation;
            const Quat copyLevel = ch->OwnerViewArm()->transform->localRotation;
            const Quat ghostLevel = gh->Part(3)->transform->localRotation;
            tp->pitch = 80.0f;
            pump(8);
            CHECK(Quat::Angle(ch->Part(3)->transform->localRotation, realLevel) < 3.0f);
            CHECK(ch->OwnerViewArm() && ch->OwnerViewArm()->active);   // a look down keeps the copy in the corner
            CHECK(Quat::Angle(gh->Part(3)->transform->localRotation, ghostLevel) < 8.0f);
            (void)copyLevel;
        }
        tp->pitch = 0.0f;
        match(2);
        match(3);
        CHECK(ch->OwnerViewArm() && !ch->OwnerViewArm()->active);
        match(6);
        match(7);
        pv->SetView(1);
        pump(15);
        CHECK(!ch->firstPersonArm);
        pv->SetView(0);
        pump(20);
        CHECK(ch->firstPersonArm);
        match(1);
        CHECK(gh->Part(2)->active);
        CHECK((int)gh->Part(5)->GetComponent<MeshRenderer>()->mesh.vertices.size() == 36);
    }

    // Entering first person must not log a warning, and a run bobs the eye
    // up and down without rolling it or sliding it sideways.
    {
        std::vector<std::string> warns;
        auto prev = Log::sink;
        Log::sink = [&](Log::Level lv, const std::string& m) {
            if (lv == Log::Level::Warning || lv == Log::Level::Error) warns.push_back(m);
        };
        {
            Scene sc("enter");
            sc.physicsEnabled = true;
            Templates::ThirdPerson(sc);
            sc.Start();
            for (int i = 0; i < 5; ++i) sc.Update(1.0f / 60.0f);
            sc.Find("Player")->GetComponent<PlayerView>()->SetView(0);
            for (int i = 0; i < 40; ++i) sc.Update(1.0f / 60.0f);
            if (!warns.empty()) std::cerr << "  FP warning: " << warns[0] << "\n";
            CHECK(warns.empty());
        }
        warns.clear();
        {
            Scene sc("bob");
            sc.physicsEnabled = true;
            Templates::FPS(sc);
            GameObject* player = nullptr;
            Camera* cam = nullptr;
            for (auto& o : sc.Objects()) {
                if (o->name == "Player") player = o.get();
                if (auto* c = o->GetComponent<Camera>()) cam = c;
            }
            player->transform->localPosition = {0, 1.0f, 8.0f};   // open ground, away from the crates
            auto* fpc = player->GetComponent<FirstPersonController>();
            fpc->sprintKey = 'r';
            fpc->bobSway = 0.05f;   // an old scene value; it must not move the camera
            fpc->yaw = 0.0f;
            sc.Start();
            float maxRoll = 0.0f, maxAbsX = 0.0f, minY = 1.0e9f, maxY = -1.0e9f;
            for (int i = 0; i < 120; ++i) {
                Input::FeedKeys({'w', 'r'});
                sc.Update(1.0f / 60.0f);
                if (i < 40) continue;   // let the body land, then measure the stride
                const Quat q = cam->transform->Rotation();
                const Vec3 right = q * Vec3{1, 0, 0};
                const Vec3 up = q * Vec3{0, 1, 0};
                const float roll = std::atan2(right.y, up.y) * 57.2957795f;
                maxRoll = std::max(maxRoll, std::fabs(roll));
                maxAbsX = std::max(maxAbsX, std::fabs(cam->transform->localPosition.x));
                minY = std::min(minY, cam->transform->localPosition.y);
                maxY = std::max(maxY, cam->transform->localPosition.y);
            }
            if (!warns.empty()) std::cerr << "  FPS warning: " << warns[0] << "\n";
            CHECK(warns.empty());
            CHECK(maxRoll < 0.5f);
            CHECK(maxAbsX < 0.004f);
            CHECK(maxY - minY > 0.008f);   // the up/down bob is still there
        }
        Log::sink = std::move(prev);
    }

    auto armActive = [](Character* ch) {
        GameObject* arm = ch ? ch->OwnerViewArm() : nullptr;
        return arm && arm->active;
    };

    // Empty hands draw nothing. Holding a block draws one hand. A crate draws both.
    {
        Scene scene("empty");
        GameObject* player = scene.CreateGameObject("Player");
        auto* ch = player->AddComponent<Character>(); ch->Apply();
        player->AddComponent<FirstPersonController>();
        GameObject* cam = scene.CreateGameObject("Eye");
        cam->transform->SetParent(player->transform, false);
        cam->transform->localPosition = {0, 1.62f, 0};
        cam->AddComponent<Camera>();
        cam->AddComponent<FirstPersonHand>();
        scene.Start();
        for (int i = 0; i < 8; ++i) scene.Update(1.0f / 60.0f);
        CHECK(ch->fpArmUp == 0.0f);
        CHECK(!armActive(ch));

        auto* hands = player->AddComponent<FirstPersonHands>();
        for (int i = 0; i < 6; ++i) scene.Update(1.0f / 60.0f);
        CHECK(!armActive(ch));
        hands->showWhenEmpty = true;                 // a live edit, the way the inspector writes it
        for (int i = 0; i < 48; ++i) scene.Update(1.0f / 60.0f);
        CHECK(ch->fpArmUp == 1.0f);
        CHECK(armActive(ch));
        CHECK(!ch->OwnerViewArm2() || !ch->OwnerViewArm2()->active);

        hands->showWhenEmpty = false;
        auto* hold = player->AddComponent<ProceduralObjectHolding>();
        GameObject* block = scene.CreateGameObject("Block");
        block->transform->localPosition = {0.2f, 1.0f, -0.5f};
        block->transform->localScale = {0.12f, 0.12f, 0.12f};
        auto* bmr = block->AddComponent<MeshRenderer>();
        bmr->mesh = Mesh::Cube();
        bmr->color = Color::FromBytes(210, 120, 40);
        block->AddComponent<HoldableObject>()->weight = 0.3f;
        CHECK(hold->Pickup(block));
        for (int i = 0; i < 48; ++i) scene.Update(1.0f / 60.0f);
        CHECK(armActive(ch));
        CHECK(!ch->OwnerViewArm2() || !ch->OwnerViewArm2()->active);
        GameObject* item = scene.Find("FP_OwnerItem");
        CHECK(item && item->active && item->firstPersonOnly);
        // The hold sits close to the lens, and a forearm runs down from the wrist
        // so the hand is not a brick floating in the middle of the view.
        {
            GameObject* fore = nullptr;
            GameObject* hand = nullptr;
            if (ch->OwnerViewArm() && ch->OwnerViewArm()->transform)
                for (Transform* c : ch->OwnerViewArm()->transform->Children()) {
                    if (!c || !c->gameObject) continue;
                    if (c->gameObject->name == "FP_OwnerFore") fore = c->gameObject;
                    for (Transform* h : c->Children())
                        if (h && h->gameObject && h->gameObject->name == "FP_OwnerHand") hand = h->gameObject;
                }
            auto* fmr = fore ? fore->GetComponent<MeshRenderer>() : nullptr;
            CHECK(fore && fore->active && fmr && fmr->enabled && fmr->mesh.vertices.size() >= 8);
            auto* hmr = hand ? hand->GetComponent<MeshRenderer>() : nullptr;
            CHECK(hand && hmr && !hmr->mesh.vertices.empty());
            float handMinY = 1.0e9f, foreMinY = 1.0e9f, handFar = 0.0f;
            auto measure = [&](GameObject* g, float& minY, float* far) {
                auto* mr = g->GetComponent<MeshRenderer>();
                for (const Vec3& v : mr->mesh.vertices) {
                    const Vec3 local = cam->transform->InverseTransformPoint(g->transform->TransformPoint(v));
                    minY = std::min(minY, local.y);
                    if (far) *far = std::max(*far, -local.z);
                }
            };
            measure(hand, handMinY, &handFar);
            measure(fore, foreMinY, nullptr);
            CHECK(foreMinY < handMinY - 0.04f);
            CHECK(handFar < 0.42f);
            CHECK(handFar > 0.15f);
        }

        hold->Drop();
        GameObject* crate = scene.CreateGameObject("Crate");
        crate->transform->localPosition = {0.2f, 1.0f, -0.8f};
        crate->transform->localScale = {0.5f, 0.35f, 0.35f};
        auto* cmr = crate->AddComponent<MeshRenderer>();
        cmr->mesh = Mesh::Cube();
        cmr->color = Color::FromBytes(170, 120, 60);
        auto* cho = crate->AddComponent<HoldableObject>();
        cho->twoHanded = true;
        cho->style = HoldableObject::Style::Box;
        cho->leftGrip.enabled = true; cho->leftGrip.position = {-0.4f, 0, 0};
        cho->rightGrip.enabled = true; cho->rightGrip.position = {0.4f, 0, 0};
        CHECK(cho->WantsTwoHands());
        CHECK(hold->Pickup(crate));
        for (int i = 0; i < 48; ++i) scene.Update(1.0f / 60.0f);
        CHECK(armActive(ch));
        CHECK(ch->OwnerViewArm2() && ch->OwnerViewArm2()->active && ch->OwnerViewArm2()->firstPersonOnly);
        GameObject* hand2 = nullptr;
        if (ch->OwnerViewArm2()->transform)
            for (Transform* fore : ch->OwnerViewArm2()->transform->Children())
                if (fore)
                    for (Transform* hnd : fore->Children())
                        if (hnd && hnd->gameObject && hnd->gameObject->name == "FP_OwnerHand2")
                            hand2 = hnd->gameObject;
        CHECK(hand2 && hand2->active);
        if (item && item->transform) {
            const float ahead = -(cam->transform->InverseTransformPoint(item->transform->Position()).z);
            CHECK(ahead < 0.50f);
            CHECK(ahead > 0.20f);
        }

        ch->anim = 3;
        player->GetComponent<FirstPersonController>()->driveAnimation = false;
        for (int i = 0; i < 48; ++i) scene.Update(1.0f / 60.0f);
        CHECK(!armActive(ch));
        CHECK(!ch->OwnerViewArm2()->active);
        hands->showWhileRunning = true;
        for (int i = 0; i < 48; ++i) scene.Update(1.0f / 60.0f);
        CHECK(ch->fpArmUp == 1.0f);
        CHECK(armActive(ch));
        CHECK(ch->OwnerViewArm2()->active);
    }

    // The panel round-trips, and a script can read and write the same fields.
    {
        Scene a("Save");
        GameObject* go = a.CreateGameObject("Player");
        auto* h = go->AddComponent<FirstPersonHands>();
        h->showWhenEmpty = true;
        h->showWhileRunning = true;
        h->bobAmount = 0.4f;
        h->holdMode = FirstPersonHands::HoldMode::Two;
        h->handFov = 48.0f;
        h->right.position = {0.1f, -0.2f, 0.05f};
        h->right.sleeve = true;
        h->right.sleeveLength = 0.4f;
        h->left.enabled = false;
        h->othersArmLift = 0.15f;
        h->othersItemOffset = {0.02f, 0.1f, 0.2f};
        std::string text = SceneSerializer::Serialize(a);
        Scene b("Load");
        CHECK(SceneSerializer::Deserialize(b, text));
        auto* lf = b.Find("Player") ? b.Find("Player")->GetComponent<FirstPersonHands>() : nullptr;
        CHECK(lf);
        if (lf) {
            CHECK(lf->showWhenEmpty && lf->showWhileRunning);
            CHECK_NEAR(lf->bobAmount, 0.4f, 1e-4f);
            CHECK(lf->holdMode == FirstPersonHands::HoldMode::Two);
            CHECK_NEAR(lf->handFov, 48.0f, 1e-3f);
            CHECK_NEAR(lf->right.position.x, 0.1f, 1e-4f);
            CHECK(lf->right.sleeve);
            CHECK_NEAR(lf->right.sleeveLength, 0.4f, 1e-4f);
            CHECK(!lf->left.enabled);
            CHECK_NEAR(lf->othersArmLift, 0.15f, 1e-4f);
            CHECK_NEAR(lf->othersItemOffset.z, 0.2f, 1e-4f);
        }

        Scene s("Script");
        GameObject* player = s.CreateGameObject("Player");
        OkayScriptVM vm;
        ScriptHost host;
        host.gameObject = player;
        host.transform = player->transform;
        vm.Bind(&host);
        CHECK(vm.Load(
            "AddComponent(\"FirstPersonHands\")\n"
            "let h = GetComponent(\"FirstPersonHands\")\n"
            "h.showWhenEmpty = true\n"
            "h.bobAmount = 0.25\n"
            "h.holdMode = 1\n"
            "h.rightPosition = vec3(0.1, -0.2, 0.3)\n"
            "h.ResetPlayerView()\n"));
        auto* got = player->GetComponent<FirstPersonHands>();
        CHECK(got);
        if (got) {
            CHECK(!got->showWhenEmpty);          // ResetPlayerView put it back
            CHECK(got->holdMode == FirstPersonHands::HoldMode::Right);
            CHECK_NEAR(got->bobAmount, 1.0f, 1e-4f);
        }
        CHECK(vm.Load(
            "let h = GetComponent(\"FirstPersonHands\")\n"
            "h.othersArmLift = 0.2\n"
            "h.leftSleeve = true\n"));
        if (got) {
            CHECK_NEAR(got->othersArmLift, 0.2f, 1e-4f);
            CHECK(got->left.sleeve);
            CHECK_NEAR(got->right.position.x, 0.0f, 1e-4f);
        }
    }

    // The preview draws the owner's camera and a front view, and the two example
    // items do not look the same.
    {
        FpHandsPreview preview;
        FirstPersonHands src;
        preview.CopyFrom(src);
        preview.SetExample(0);
        preview.Step(40);
        auto oneFp = preview.Render(true, 160, 90);
        auto oneTp = preview.Render(false, 160, 90);
        auto differ = [](const std::vector<std::uint32_t>& a, const std::vector<std::uint32_t>& b) {
            int n = 0;
            std::size_t m = a.size() < b.size() ? a.size() : b.size();
            for (std::size_t i = 0; i < m; ++i) if (a[i] != b[i]) ++n;
            return n;
        };
        auto ink = [](const std::vector<std::uint32_t>& a) {
            int n = 0;
            for (std::uint32_t p : a) if (p != 0xFFC8AA8Cu) ++n;
            return n;
        };
        CHECK(ink(oneFp) > 30);
        CHECK(ink(oneTp) > 30);
        CHECK(differ(oneFp, oneTp) > 40);
        preview.SetExample(1);
        preview.Step(24);
        auto twoFp = preview.Render(true, 160, 90);
        auto twoTp = preview.Render(false, 160, 90);
        CHECK(ink(twoFp) > 30);
        CHECK(differ(oneFp, twoFp) > 20);
        CHECK(differ(oneTp, twoTp) > 20);
        src.othersArmLift = 0.35f;
        preview.CopyFrom(src);
        preview.Step(8);
        auto lifted = preview.Render(false, 160, 90);
        CHECK(differ(twoTp, lifted) > 10);
    }

    // Stock First Person and Third Person templates can be tried out of the box:
    // hands on the player, one one-handed block, one two-handed crate, E to pick
    // up, E again or G to drop, and a screen prompt.
    {
        auto press = [](Scene& sc, char key) {
            Input::FeedKeys({key});
            sc.Update(1.0f / 60.0f);
            Input::FeedKeys({});
            sc.Update(1.0f / 60.0f);
        };
        auto checkTemplate = [&](void (*build)(Scene&), bool firstPerson) {
            Scene sc("hold");
            sc.physicsEnabled = false;
            build(sc);
            GameObject* player = sc.Find("Player");
            GameObject* block = sc.Find("Block");
            GameObject* crate = sc.Find("Carry Crate");
            GameObject* label = sc.Find("Hold Prompt");
            CHECK(player && block && crate && label);
            if (!player || !block || !crate || !label) return;
            CHECK(player->tag == "Player");
            CHECK(player->GetComponent<FirstPersonHands>());
            CHECK(player->GetComponent<ProceduralObjectHolding>());
            CHECK(player->GetComponent<HoldPrompt>());
            auto* pb = block->GetComponent<ItemPickup>();
            auto* pc = crate->GetComponent<ItemPickup>();
            auto* hb = block->GetComponent<HoldableObject>();
            auto* hc = crate->GetComponent<HoldableObject>();
            CHECK(pb && pb->holdInHands && pb->interactKey == 'e' && !pb->pickupOnTouch && pb->autoRadius == 0.0f);
            CHECK(pc && pc->holdInHands && pc->interactKey == 'e' && !pc->pickupOnTouch);
            CHECK(hb && !hb->WantsTwoHands());
            CHECK(hc && hc->twoHanded && hc->WantsTwoHands());
            auto* tr = label->GetComponent<TextRenderer>();
            CHECK(tr && tr->screenSpace);
            sc.Start();
            sc.Update(1.0f / 60.0f);
            CHECK(tr->text == "E to pick up");
            CHECK(tr->background);
            press(sc, 'e');
            auto* hold = player->GetComponent<ProceduralObjectHolding>();
            CHECK(hold && hold->Held() == block);
            CHECK(tr->text == "E or G to drop");
            if (firstPerson) {
                auto* ch = player->GetComponent<Character>();
                for (int i = 0; i < 48; ++i) sc.Update(1.0f / 60.0f);
                GameObject* arm = ch->OwnerViewArm();
                CHECK(arm && arm->active);
                CHECK(!ch->OwnerViewArm2() || !ch->OwnerViewArm2()->active);
            }
            press(sc, 'g');
            CHECK(hold && !hold->IsHolding());
            CHECK(tr->text == "E to pick up");
            // Stand on the crate so E takes that, not the nearer block.
            player->transform->SetPosition(crate->transform->Position() + Vec3{0.0f, 0.0f, 0.9f});
            block->transform->SetPosition(crate->transform->Position() + Vec3{0.0f, 0.0f, 6.0f});
            sc.Update(1.0f / 60.0f);
            CHECK(tr->text == "E to pick up");
            press(sc, 'e');
            CHECK(hold && hold->Held() == crate);
            if (firstPerson) {
                auto* ch = player->GetComponent<Character>();
                for (int i = 0; i < 48; ++i) sc.Update(1.0f / 60.0f);
                CHECK(ch->OwnerViewArm() && ch->OwnerViewArm()->active);
                CHECK(ch->OwnerViewArm2() && ch->OwnerViewArm2()->active);
            }
            press(sc, 'e');
            CHECK(hold && !hold->IsHolding());   // E again drops, and does not grab it straight back
            CHECK(!pc->Held());

            std::string text = SceneSerializer::Serialize(sc);
            Scene back("back");
            std::string err;
            CHECK(SceneSerializer::Deserialize(back, text, &err));
            GameObject* again = back.Find("Player");
            CHECK(again && again->GetComponent<FirstPersonHands>() && again->GetComponent<ProceduralObjectHolding>());
            auto* hp = again ? again->GetComponent<HoldPrompt>() : nullptr;
            CHECK(hp && hp->dropKey == 'e' && hp->altDropKey == 'g');
            GameObject* crate2 = back.Find("Carry Crate");
            auto* pk2 = crate2 ? crate2->GetComponent<ItemPickup>() : nullptr;
            CHECK(pk2 && pk2->holdInHands && pk2->interactKey == 'e');
            auto* ho2 = crate2 ? crate2->GetComponent<HoldableObject>() : nullptr;
            CHECK(ho2 && ho2->twoHanded);
        };
        checkTemplate(Templates::FPS, true);
        checkTemplate(Templates::ThirdPerson, false);
    }

    // Play starts level. First person looks at the horizon. Third person rests
    // on the slight downward orbit, not pitched up at the sky.
    {
        auto forwardY = [](Camera* cam) {
            Vec3 f = cam->transform->Rotation() * Vec3{0.0f, 0.0f, -1.0f};
            return f.y;
        };
        {
            Scene sc("start-fp");
            Templates::FPS(sc);
            sc.Start();
            Input::FeedMouse({400.0f, 300.0f}, 0);
            for (int i = 0; i < 8; ++i) {
                Input::FeedMouse({400.0f, 300.0f}, 0);
                sc.Update(1.0f / 60.0f);
            }
            Camera* cam = sc.mainCamera;
            CHECK(cam && cam->transform);
            auto* fp = sc.Find("Player")->GetComponent<FirstPersonController>();
            CHECK(fp && std::fabs(fp->pitch) < 0.01f);
            CHECK(forwardY(cam) > -0.08f);
            CHECK(forwardY(cam) < 0.08f);
            const Vec3 eye = sc.Find("Player")->transform->InverseTransformPoint(cam->transform->Position());
            CHECK(eye.z < -0.15f);   // in front of the face, not inside the head
        }
        {
            Scene sc("start-tp");
            Templates::ThirdPerson(sc);
            sc.Start();
            Input::FeedMouse({400.0f, 300.0f}, 0);
            for (int i = 0; i < 8; ++i) {
                Input::FeedMouse({400.0f, 300.0f}, 0);
                sc.Update(1.0f / 60.0f);
            }
            Camera* cam = sc.mainCamera;
            CHECK(cam && cam->transform);
            auto* tp = sc.Find("Player")->GetComponent<ThirdPersonController>();
            CHECK(tp && std::fabs(tp->pitch - 18.0f) < 0.5f);
            const float y = forwardY(cam);
            CHECK(y < -0.05f);   // the resting orbit looks slightly down
            CHECK(y > -0.45f);   // and not up at the sky, and not at the feet
        }
    }

    TEST_MAIN_RESULT();
}

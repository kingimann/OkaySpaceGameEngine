#include "test_framework.hpp"
#include <Okay.hpp>
#include <algorithm>
#include <cstring>

using namespace okay;

// FirstPersonHand shows the character's OWN real arm: it builds the part rig if needed,
// raises the screen-right arm, and hides the rest of the body from the OWNER's camera
// only (camera ignore + baked mesh disabled). Nothing is spawned or duplicated.
int main() {
    RUN_SUITE("fp_hand");

    {
        Scene scene("FPS");
        GameObject* player = scene.CreateGameObject("Player");
        auto* ch = player->AddComponent<Character>(); ch->Apply();   // not separated yet
        player->AddComponent<FirstPersonController>();

        GameObject* cam = scene.CreateGameObject("Eye");
        cam->transform->SetParent(player->transform, false);
        cam->transform->localPosition = {0, 1.62f, 0};
        auto* camera = cam->AddComponent<Camera>();
        cam->AddComponent<FirstPersonHand>();

        scene.Start();
        for (int i = 0; i < 3; ++i) scene.Update(0.016f);

        // The hand auto-builds the rig and raises the real screen-right arm (bones 3-5).
        CHECK(ch->separateParts);
        CHECK(ch->PartsBuilt());
        CHECK(ch->firstPersonArm);
        CHECK(ch->fpArmBase == 3);
        CHECK(ch->Part(3) && ch->Part(3)->active && ch->Part(3)->firstPersonViewmodel);
        CHECK(ch->Part(4) && ch->Part(4)->firstPersonViewmodel);
        CHECK(ch->Part(5) && ch->Part(5)->firstPersonViewmodel);
        // The head stays active (shadows, other cameras) but this camera skips it.
        // The shirt stays in this camera: its lid is what a look down sees, and
        // without it the open top shows the world inside the chest.
        CHECK(ch->Part(2) && ch->Part(2)->active && !ch->Part(2)->firstPersonViewmodel);  // head
        CHECK(ch->Part(1) && ch->Part(1)->active && ch->Part(1)->firstPersonViewmodel);   // shirt
        CHECK(ch->Part(6) && ch->Part(6)->firstPersonViewmodel);                          // other arm
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
        // No spawned arm object (no "second arm"), and the baked mesh stays hidden.
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
        CHECK(ch->Part(1) && ch->Part(1)->active && ch->Part(1)->firstPersonViewmodel);  // shirt stays while the arms drop
        CHECK(ch->Part(2) && ch->Part(2)->active && !ch->Part(2)->firstPersonViewmodel);
        ch->anim = 2;
        for (int i = 0; i < 30; ++i) scene.Update(0.016f);   // the run style eases out over ~0.2 s
        CHECK(!ch->HideFirstPersonHands());
        CHECK(ch->fpArmUp == 1.0f);
        CHECK(ch->Part(5) && ch->Part(5)->firstPersonViewmodel);
    }

    // Left-arm option.
    {
        Scene scene("L");
        GameObject* player = scene.CreateGameObject("Player");
        player->AddComponent<Character>()->Apply();
        player->AddComponent<FirstPersonController>();
        GameObject* cam = scene.CreateGameObject("Eye");
        cam->transform->SetParent(player->transform, false);
        cam->AddComponent<Camera>();
        cam->AddComponent<FirstPersonHand>()->showLeftArm = true;
        scene.Start();
        for (int i = 0; i < 3; ++i) scene.Update(0.016f);
        auto* ch = player->GetComponent<Character>();
        CHECK(ch->fpArmBase == 6);                       // the raised (screen-left) arm
        CHECK(ch->Part(6) && ch->Part(6)->firstPersonViewmodel);
        CHECK(ch->Part(3) && ch->Part(3)->firstPersonViewmodel);   // the other arm is still the body
        CHECK(ch->Part(2) && !ch->Part(2)->firstPersonViewmodel);  // head stays off this camera
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

        // Per-pixel shading is what samples the shadow map. The head's shadow
        // changes that picture while the head mesh itself stays drawn.
        PerPixelLighting() = true;
        side->transform->SetPosition({3.2f, 1.6f, 2.4f});
        side->transform->SetRotation(Quat::LookRotation(side->transform->Position() - Vec3{0, 0.6f, 0}));
        hmr->castShadows = true;
        auto shA = shot(sideCam, W, H);
        hmr->castShadows = false;
        auto shB = shot(sideCam, W, H);
        hmr->castShadows = true;
        PerPixelLighting() = false;
        CHECK(differ(shA, shB) > 0);
    }

    TEST_MAIN_RESULT();
}

#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

// FirstPersonHand does it the Minecraft/Unturned way: the body is hidden from the
// OWNER's own camera (camera ignore), never deactivated, and an arm viewmodel is
// shown only to the owner. A separate path raises the rig's real arm instead.
int main() {
    RUN_SUITE("fp_hand");

    // ---- Default: standalone viewmodel arm, no rig required ----
    {
        Scene scene("FPS");
        GameObject* player = scene.CreateGameObject("Player");
        auto* ch = player->AddComponent<Character>(); ch->Apply();   // single mesh (no rig)
        player->AddComponent<FirstPersonController>();

        GameObject* cam = scene.CreateGameObject("Eye");
        cam->transform->SetParent(player->transform, false);
        cam->transform->localPosition = {0, 1.62f, 0};
        auto* camera = cam->AddComponent<Camera>();
        cam->AddComponent<FirstPersonHand>();

        scene.Start();
        for (int i = 0; i < 3; ++i) scene.Update(0.016f);

        // A viewmodel arm was spawned under the camera, flagged so the owner's own
        // camera renders it even though the body subtree is ignored.
        GameObject* arm = scene.Find("FP_Arm");
        CHECK(arm != nullptr);
        CHECK(arm && arm->firstPersonViewmodel == true);
        CHECK(arm && arm->transform->Parent() == cam->transform);
        CHECK(arm && arm->GetComponent<MeshRenderer>() != nullptr);

        // Body hidden from the OWNER only: the camera ignores the player subtree.
        CHECK(camera->ignoreObject == player);
        // No rig was built and the body is NOT driven as a first-person arm.
        CHECK(!ch->PartsBuilt());
        CHECK(!ch->firstPersonArm);

        // The viewmodel is regenerated each run, so it is NOT written to the scene file.
        std::string text = SceneSerializer::Serialize(scene);
        CHECK(text.find("FP_Arm") == std::string::npos);
    }

    // ---- Advanced: raise the separated rig's own arm (viewmodelArm = false) ----
    {
        Scene scene("FPS2");
        GameObject* player = scene.CreateGameObject("Player");
        auto* ch = player->AddComponent<Character>(); ch->Apply();
        ch->separateParts = true;
        player->AddComponent<FirstPersonController>();

        GameObject* cam = scene.CreateGameObject("Eye");
        cam->transform->SetParent(player->transform, false);
        auto* fh = cam->AddComponent<FirstPersonHand>();
        fh->viewmodelArm = false;     // use the real rig arm
        cam->AddComponent<Camera>();

        scene.Start();
        for (int i = 0; i < 3; ++i) scene.Update(0.016f);

        CHECK(ch->PartsBuilt());
        CHECK(ch->firstPersonArm);
        CHECK(ch->fpArmBase == 3);
        // Nothing is deactivated — the arm is flagged as the viewmodel, the rest are not.
        CHECK(ch->Part(3) && ch->Part(3)->active && ch->Part(3)->firstPersonViewmodel);
        CHECK(ch->Part(5) && ch->Part(5)->firstPersonViewmodel);
        CHECK(ch->Part(1) && ch->Part(1)->active && !ch->Part(1)->firstPersonViewmodel);  // torso: still there, just culled for the owner
        // No standalone viewmodel object in this mode.
        CHECK(scene.Find("FP_Arm") == nullptr);
        // The baked single mesh stays hidden so you never see "two characters".
        CHECK(player->GetComponent<MeshRenderer>() && !player->GetComponent<MeshRenderer>()->enabled);
    }

    // ---- Serialization round-trips the settings ----
    {
        Scene a("A");
        GameObject* go = a.CreateGameObject("Cam");
        go->AddComponent<Camera>();
        auto* fh = go->AddComponent<FirstPersonHand>();
        fh->attackButton = 1; fh->holdToSwing = false; fh->showLeftArm = true;
        fh->viewmodelArm = false; fh->matchSkin = false;
        fh->armWidth = 0.2f; fh->armLength = 0.7f; fh->swingAmount = 0.5f;
        fh->armColor = Color::FromBytes(10, 20, 30);
        std::string text = SceneSerializer::Serialize(a);
        Scene b("B");
        CHECK(SceneSerializer::Deserialize(b, text));
        auto* lf = b.Find("Cam") ? b.Find("Cam")->GetComponent<FirstPersonHand>() : nullptr;
        CHECK(lf != nullptr);
        if (lf) {
            CHECK(lf->attackButton == 1);
            CHECK(lf->holdToSwing == false);
            CHECK(lf->showLeftArm == true);
            CHECK(lf->viewmodelArm == false);
            CHECK(lf->matchSkin == false);
            CHECK(std::abs(lf->armWidth - 0.2f) < 1e-4f);
            CHECK(std::abs(lf->armLength - 0.7f) < 1e-4f);
            CHECK(std::abs(lf->swingAmount - 0.5f) < 1e-4f);
        }
    }

    TEST_MAIN_RESULT();
}

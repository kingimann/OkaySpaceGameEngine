#include "test_framework.hpp"
#include <Okay.hpp>

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
        // Other parts stay active but are NOT flagged, so the owner's camera culls them.
        CHECK(ch->Part(2) && ch->Part(2)->active && !ch->Part(2)->firstPersonViewmodel);  // head
        CHECK(ch->Part(1) && ch->Part(1)->active && !ch->Part(1)->firstPersonViewmodel);  // torso
        CHECK(ch->Part(6) && !ch->Part(6)->firstPersonViewmodel);                          // other arm
        // Body hidden from the OWNER only: the camera ignores the player subtree.
        CHECK(camera->ignoreObject == player);
        // No spawned arm object (no "second arm"), and the baked mesh stays hidden.
        CHECK(scene.Find("FP_Arm") == nullptr);
        CHECK(player->GetComponent<MeshRenderer>() && !player->GetComponent<MeshRenderer>()->enabled);
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
        CHECK(ch->fpArmBase == 6);                       // the other (screen-left) arm
        CHECK(ch->Part(6) && ch->Part(6)->firstPersonViewmodel);
        CHECK(ch->Part(3) && !ch->Part(3)->firstPersonViewmodel);
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

    TEST_MAIN_RESULT();
}

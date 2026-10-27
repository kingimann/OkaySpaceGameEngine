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
        // The head stays active (shadows, other cameras) but this camera skips it.
        // The torso and the other arm are drawn, solid, so looking down is not a cavity.
        CHECK(ch->Part(2) && ch->Part(2)->active && !ch->Part(2)->firstPersonViewmodel);  // head
        CHECK(ch->Part(1) && ch->Part(1)->active && ch->Part(1)->firstPersonViewmodel);   // torso
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
        // The hand part is one block. A palm plus a thumb and finger segments
        // was five cubes (40 vertices) on this bone.
        std::vector<int> bone;
        ch->BuildRig(bone);
        int handVerts = 0;
        const int hand = Character::BoneIndex("l_hand");
        for (int b : bone) if (b == hand) ++handVerts;
        CHECK(handVerts == 8);

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
        CHECK(ch->Part(1) && ch->Part(1)->active && ch->Part(1)->firstPersonViewmodel);
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

    TEST_MAIN_RESULT();
}

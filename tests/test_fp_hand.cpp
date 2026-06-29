#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

// FirstPersonHand, on a separated-parts character, hides every part except the arm
// (so first person shows only your real arm). Nothing is spawned.
int main() {
    RUN_SUITE("fp_hand");

    {
        Scene scene("FPS");
        GameObject* player = scene.CreateGameObject("Player");
        auto* ch = player->AddComponent<Character>(); ch->Apply();
        ch->separateParts = true;
        player->AddComponent<FirstPersonController>();

        GameObject* cam = scene.CreateGameObject("Eye");
        cam->transform->SetParent(player->transform, false);
        cam->transform->localPosition = {0, 1.62f, 0};
        cam->AddComponent<Camera>();
        cam->AddComponent<FirstPersonHand>();

        std::size_t before = scene.Objects().size();
        scene.Start();
        for (int i = 0; i < 3; ++i) scene.Update(0.016f);

        CHECK(ch->PartsBuilt());
        // Only the right-arm parts (6,7,8) are active; the rest are hidden.
        CHECK(ch->Part(6) && ch->Part(6)->active == true);     // R upper arm shown
        CHECK(ch->Part(7) && ch->Part(7)->active == true);     // R forearm shown
        CHECK(ch->Part(2) && ch->Part(2)->active == false);    // head hidden
        CHECK(ch->Part(1) && ch->Part(1)->active == false);    // torso hidden
        CHECK(ch->Part(12) && ch->Part(12)->active == false);  // R thigh hidden

        // The only objects added were the part rig (Rig + parts), created by the
        // Character's own BuildParts — the hand component itself spawns nothing extra.
        std::size_t partObjs = scene.Objects().size() - before;
        CHECK(partObjs >= 13);   // Rig root + ~14 parts
    }

    // Serialization round-trips the settings.
    {
        Scene a("A");
        GameObject* go = a.CreateGameObject("Cam");
        go->AddComponent<Camera>();
        auto* fh = go->AddComponent<FirstPersonHand>();
        fh->attackButton = 1; fh->holdToSwing = false; fh->showLeftArm = true;
        std::string text = SceneSerializer::Serialize(a);
        Scene b("B");
        CHECK(SceneSerializer::Deserialize(b, text));
        auto* lf = b.Find("Cam") ? b.Find("Cam")->GetComponent<FirstPersonHand>() : nullptr;
        CHECK(lf != nullptr);
        if (lf) {
            CHECK(lf->attackButton == 1);
            CHECK(lf->holdToSwing == false);
            CHECK(lf->showLeftArm == true);
        }
    }

    TEST_MAIN_RESULT();
}

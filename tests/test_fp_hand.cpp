#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

// FirstPersonHand uses the CHARACTER'S OWN arm (no extra object): it freezes the
// body and raises the real arm into the first-person view; empty-handed it rests
// low, holding raises it.
int main() {
    RUN_SUITE("fp_hand");

    {
        Scene scene("FPS");
        GameObject* player = scene.CreateGameObject("Player");
        auto* ch = player->AddComponent<Character>(); ch->Apply();
        auto* fpc = player->AddComponent<FirstPersonController>();

        GameObject* cam = scene.CreateGameObject("Eye");
        cam->transform->SetParent(player->transform, false);
        cam->transform->localPosition = {0, 1.62f, 0};
        cam->AddComponent<Camera>();
        auto* fh = cam->AddComponent<FirstPersonHand>();

        std::size_t before = scene.Objects().size();
        scene.Start();
        scene.Update(0.016f);

        CHECK(fpc->showBody == true);                  // body shown (its arm renders)
        CHECK(ch->firstPersonArm == true);             // character drives the first-person arm
        CHECK(scene.Objects().size() == before);       // NO separate/extra arm object created

        auto* camC = cam->GetComponent<Camera>();
        const int W = 320, H = 200;
        auto avgY = [&](int& lit) {
            Mat4 vp = camC->ProjectionMatrix((float)W/(float)H) * camC->ViewMatrix();
            Vec3 e = cam->transform->Position();
            Raster work; std::vector<std::uint32_t> outbuf;
            const std::uint32_t* px = RenderMeshesSS(work, outbuf, scene, vp, e, W, H, 1, nullptr);
            lit = 0; long sumY = 0;
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x)
                    if (px[y*W+x] & 0xFF000000u) { ++lit; sumY += y; }
            return lit ? (int)(sumY / lit) : 0;
        };

        // Empty-handed (default): the arm rests low in the lower-right and is visible.
        // It must show even while WALKING — the body is frozen, so no torso/legs appear.
        ch->anim = 2;                                  // "walk"
        for (int i = 0; i < 30; ++i) scene.Update(0.016f);
        int litDn; int yDn = avgY(litDn);
        CHECK(litDn > 800);                            // empty hand clearly shown, low in view

        // Holding a weapon/item raises the hand higher (smaller average Y).
        fh->holdingItem = true;
        for (int i = 0; i < 30; ++i) scene.Update(0.016f);
        int litUp; int yUp = avgY(litUp);
        CHECK(litUp > 800);
        CHECK(yUp < yDn);                              // raised pose sits higher than the resting one
    }

    // Serialization round-trips the settings.
    {
        Scene a("A");
        GameObject* go = a.CreateGameObject("Cam");
        go->AddComponent<Camera>();
        auto* fh = go->AddComponent<FirstPersonHand>();
        fh->attackButton = 1; fh->holdToSwing = false; fh->holdingItem = true;
        fh->handRaise = -110.0f; fh->elbowBend = 25.0f;
        std::string text = SceneSerializer::Serialize(a);
        Scene b("B");
        CHECK(SceneSerializer::Deserialize(b, text));
        auto* lf = b.Find("Cam") ? b.Find("Cam")->GetComponent<FirstPersonHand>() : nullptr;
        CHECK(lf != nullptr);
        if (lf) {
            CHECK(lf->attackButton == 1);
            CHECK(lf->holdToSwing == false);
            CHECK(lf->holdingItem == true);
            CHECK_NEAR(lf->handRaise, -110.0f, 0.01f);
            CHECK_NEAR(lf->elbowBend, 25.0f, 0.01f);
        }
    }

    TEST_MAIN_RESULT();
}

#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

// FirstPersonHand hides the body and draws ONLY a short hand viewmodel (coloured
// from the Character) in the lower-right of the first-person view.
int main() {
    RUN_SUITE("fp_hand");

    {
        Scene scene("FPS");
        GameObject* player = scene.CreateGameObject("Player");
        auto* ch = player->AddComponent<Character>(); ch->Apply();
        ch->shirtStyle = 2;                            // long sleeve -> forearm uses the shirt colour
        auto* fpc = player->AddComponent<FirstPersonController>();
        fpc->showBody = true;                          // start "wrong" to prove the hand hides it

        GameObject* cam = scene.CreateGameObject("Eye");
        cam->transform->SetParent(player->transform, false);
        cam->transform->localPosition = {0, 1.62f, 0};
        cam->AddComponent<Camera>();
        auto* fh = cam->AddComponent<FirstPersonHand>();

        scene.Start();
        scene.Update(0.016f);

        CHECK(fpc->showBody == false);                 // body hidden in first person
        GameObject* hg = scene.Find("FP Hand");
        CHECK(hg != nullptr);                          // a hand viewmodel exists
        if (hg) {
            CHECK(hg->layer == 31);                    // reserved viewmodel layer (Scene view hides it)
            auto* mr = hg->GetComponent<MeshRenderer>();
            CHECK(mr && mr->mesh.HasFaceColors());     // coloured from the character
        }

        auto* camC = cam->GetComponent<Camera>();
        const int W = 320, H = 200;
        Mat4 vp = camC->ProjectionMatrix((float)W/(float)H) * camC->ViewMatrix();
        Vec3 eye = cam->transform->Position();
        auto count = [&](int& lit, int& lowerRight, int& avgY) {
            Raster work; std::vector<std::uint32_t> outbuf;
            const std::uint32_t* px = RenderMeshesSS(work, outbuf, scene, vp, eye, W, H, 1, player);
            lit = lowerRight = 0; long sumY = 0;
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x)
                    if (px[y*W+x] & 0xFF000000u) { ++lit; sumY += y; if (y > H/2 && x > W/2) ++lowerRight; }
            avgY = lit ? (int)(sumY / lit) : 0;
        };

        // Holding a weapon/item: hand raised into the lower-right.
        fh->holdingItem = true;
        for (int i = 0; i < 20; ++i) scene.Update(0.016f);
        int litUp, lrUp, yUp; count(litUp, lrUp, yUp);
        CHECK(litUp > 500);                  // clearly on screen
        CHECK(lrUp > litUp * 7 / 10);        // in the lower-right

        // Empty-handed: the hand rests low (further down the screen).
        fh->holdingItem = false;
        for (int i = 0; i < 30; ++i) scene.Update(0.016f);
        int litDn, lrDn, yDn; count(litDn, lrDn, yDn);
        CHECK(litDn > 100);                  // empty hand still peeks at the bottom
        CHECK(yDn > yUp);                    // but rests lower than the held pose
    }

    // Serialization round-trips the settings (incl. placement).
    {
        Scene a("A");
        GameObject* go = a.CreateGameObject("Cam");
        go->AddComponent<Camera>();
        auto* fh = go->AddComponent<FirstPersonHand>();
        fh->attackButton = 1; fh->holdToSwing = false;
        fh->armLength = 0.1f; fh->posX = 0.4f; fh->roll = -30.0f;
        std::string text = SceneSerializer::Serialize(a);
        Scene b("B");
        CHECK(SceneSerializer::Deserialize(b, text));
        auto* lf = b.Find("Cam") ? b.Find("Cam")->GetComponent<FirstPersonHand>() : nullptr;
        CHECK(lf != nullptr);
        if (lf) {
            CHECK(lf->attackButton == 1);
            CHECK(lf->holdToSwing == false);
            CHECK_NEAR(lf->armLength, 0.1f, 0.001f);
            CHECK_NEAR(lf->posX, 0.4f, 0.001f);
            CHECK_NEAR(lf->roll, -30.0f, 0.001f);
        }
    }

    TEST_MAIN_RESULT();
}

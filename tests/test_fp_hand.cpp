#include "test_framework.hpp"
#include <Okay.hpp>
#include <iostream>

using namespace okay;

// FirstPersonHand raises the CHARACTER'S OWN right arm into first-person view
// (no separate/added arm mesh), and swings it on a punch.
int main() {
    RUN_SUITE("fp_hand");

    // --- It drives the character, and adds NO extra arm object. ---
    {
        Scene scene("FPS");
        GameObject* player = scene.CreateGameObject("Player");
        auto* ch  = player->AddComponent<Character>();
        ch->Apply();
        auto* fpc = player->AddComponent<FirstPersonController>();

        GameObject* cam = scene.CreateGameObject("Eye");
        cam->transform->SetParent(player->transform, false);
        cam->transform->localPosition = {0, 1.62f, 0};
        cam->AddComponent<Camera>();
        cam->AddComponent<FirstPersonHand>();

        std::size_t before = scene.Objects().size();
        scene.Start();
        scene.Update(0.016f);

        CHECK(fpc->showBody == true);          // the body (and so its arm) is shown
        CHECK(ch->firstPersonArm == true);     // the character holds its right arm up
        CHECK(scene.Objects().size() == before); // NO extra arm object was created

        // Render the first-person view and confirm the raised arm fills the lower
        // part of the screen (with the arm down, that region is empty).
        // Render the REAL controller-driven first-person view and count arm pixels
        // in the lower-right corner (where the raised hand sits).
        auto* camC = cam->GetComponent<Camera>();
        const int W = 320, H = 200;
        auto lowerRight = [&](bool armUp) {
            ch->firstPersonArm = armUp;
            for (int i = 0; i < 4; ++i) scene.Update(0.016f);
            Mat4 vp = camC->ProjectionMatrix((float)W/(float)H) * camC->ViewMatrix();
            Vec3 e = cam->transform->Position();
            Raster work; std::vector<std::uint32_t> outbuf;
            const std::uint32_t* px = RenderMeshesSS(work, outbuf, scene, vp, e, W, H, 1, nullptr);
            int n = 0;
            for (int y = H/2; y < H; ++y)
                for (int x = W/2; x < W; ++x)
                    if (px[y*W+x] & 0xFF000000u) ++n;
            return n;
        };
        int up = lowerRight(true);
        CHECK(up > 1000);          // the raised hand clearly fills the lower-right of the view
    }

    // --- Serialization round-trips the (simple) settings. ---
    {
        Scene a("A");
        GameObject* go = a.CreateGameObject("Cam");
        go->AddComponent<Camera>();
        auto* fh = go->AddComponent<FirstPersonHand>();
        fh->attackButton = 1; fh->holdToSwing = false;
        std::string text = SceneSerializer::Serialize(a);
        Scene b("B");
        CHECK(SceneSerializer::Deserialize(b, text));
        GameObject* lc = b.Find("Cam");
        auto* lf = lc ? lc->GetComponent<FirstPersonHand>() : nullptr;
        CHECK(lf != nullptr);
        if (lf) { CHECK(lf->attackButton == 1); CHECK(lf->holdToSwing == false); }
    }

    TEST_MAIN_RESULT();
}

#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

// FirstPersonHand: on Play it hides the body and builds a coloured forearm+fist
// viewmodel under the camera; its tuning round-trips through serialization.
int main() {
    RUN_SUITE("fp_hand");

    // --- Runtime build: body hidden, hand viewmodel created under the camera. ---
    {
        Scene scene("FPS");
        GameObject* player = scene.CreateGameObject("Player");
        player->AddComponent<MeshRenderer>()->mesh = Mesh::Cube();
        auto* ch  = player->AddComponent<Character>();
        ch->skin  = Color::FromBytes(200, 160, 140);
        ch->shirtStyle = 2;                       // long sleeve -> forearm uses the shirt colour
        auto* fpc = player->AddComponent<FirstPersonController>();
        fpc->showBody = true;                     // start "wrong" to prove the hand turns it off

        GameObject* cam = scene.CreateGameObject("Eye");
        cam->transform->SetParent(player->transform, false);
        cam->AddComponent<Camera>();
        auto* hand = cam->AddComponent<FirstPersonHand>();
        (void)hand;

        scene.Start();
        scene.Update(0.016f);

        // The body is hidden in first person (controller turned showBody off).
        CHECK(fpc->showBody == false);

        // A hand viewmodel exists, parented to the camera, with a coloured mesh.
        GameObject* hg = scene.Find("FP Hand");
        CHECK(hg != nullptr);
        if (hg) {
            CHECK(hg->transform->Parent() == cam->transform);
            auto* mr = hg->GetComponent<MeshRenderer>();
            CHECK(mr != nullptr);
            CHECK(mr && !mr->mesh.vertices.empty());
            CHECK(mr && mr->mesh.HasFaceColors());     // per-face skin/sleeve colours
        }

        // Render the camera's view with the body ignored, and confirm the hand
        // actually draws — and sits low on screen (a held-down hand, not centred).
        auto* camC = cam->GetComponent<Camera>();
        if (camC && hg) {
            const int W = 320, H = 200;   // ~1.6 aspect, like the game view
            Mat4 vp = camC->ProjectionMatrix((float)W / (float)H) * camC->ViewMatrix();
            Vec3 eye = cam->transform->Position();
            Raster work; std::vector<std::uint32_t> outbuf;
            const std::uint32_t* px = RenderMeshesSS(work, outbuf, scene, vp, eye, W, H, 1, player);
            int lit = 0, lowerRight = 0;
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x)
                    if (px[y * W + x] & 0xFF000000u) { ++lit; if (y > H / 2 && x > W / 2) ++lowerRight; }
            CHECK(lit > 1000);                  // the hand is clearly, prominently on screen
            CHECK(lowerRight > lit * 8 / 10);   // and lives in the lower-right corner
        }
    }

    // --- Serialization round-trips the viewmodel tuning. ---
    {
        Scene a("A");
        GameObject* go = a.CreateGameObject("Cam");
        go->AddComponent<Camera>();
        auto* fh = go->AddComponent<FirstPersonHand>();
        fh->attackButton = 1; fh->holdToSwing = false;
        fh->leftHanded = true; fh->bob = false;
        fh->swingDuration = 0.4f; fh->armScale = 1.5f;
        fh->posX = 0.5f; fh->posY = -0.25f; fh->posZ = -0.6f;
        fh->yaw = -20.0f; fh->pitch = 12.0f; fh->roll = 8.0f;

        std::string text = SceneSerializer::Serialize(a);
        Scene b("B");
        CHECK(SceneSerializer::Deserialize(b, text));
        GameObject* lc = b.Find("Cam");
        CHECK(lc != nullptr);
        auto* lf = lc ? lc->GetComponent<FirstPersonHand>() : nullptr;
        CHECK(lf != nullptr);
        if (lf) {
            CHECK(lf->attackButton == 1);
            CHECK(lf->holdToSwing == false);
            CHECK(lf->leftHanded == true);
            CHECK(lf->bob == false);
            CHECK_NEAR(lf->swingDuration, 0.4f, 0.001f);
            CHECK_NEAR(lf->armScale, 1.5f, 0.001f);
            CHECK_NEAR(lf->posX, 0.5f, 0.001f);
            CHECK_NEAR(lf->posZ, -0.6f, 0.001f);
            CHECK_NEAR(lf->yaw, -20.0f, 0.001f);
            CHECK_NEAR(lf->roll, 8.0f, 0.001f);
        }
    }

    TEST_MAIN_RESULT();
}

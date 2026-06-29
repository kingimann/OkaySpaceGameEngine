#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

// FirstPersonHand hides the whole body from the first-person camera and renders
// ONLY the character's real arm (split out by bone) on a separate object — so you
// never see your body, even looking down.
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
        ch->anim = 2;                                  // "walk" — body must never appear
        fh->holdingItem = true;
        for (int i = 0; i < 20; ++i) scene.Update(0.016f);

        CHECK(ch->firstPersonArm == true);
        CHECK(scene.Objects().size() == before);       // NOTHING spawned into the scene

        // The player's own mesh is now arm-only — far fewer triangles than the full
        // body — so only the arm can render (no torso/legs, even looking down).
        auto* bodyMr = player->GetComponent<MeshRenderer>();
        CHECK(bodyMr != nullptr);
        int armTris = bodyMr ? bodyMr->mesh.TriangleCount() : 0;
        ch->firstPersonArm = false;                    // build the full body to compare
        scene.Update(0.016f);
        int fullTris = bodyMr ? bodyMr->mesh.TriangleCount() : 0;
        CHECK(armTris > 0);
        CHECK(armTris < fullTris / 3);                 // arm-only is a small fraction of the body
        ch->firstPersonArm = true;
        for (int i = 0; i < 5; ++i) scene.Update(0.016f);

        // Render the real first-person view and confirm the arm fills the lower-right.
        auto* camC = cam->GetComponent<Camera>();
        const int W = 320, H = 200;
        Mat4 vp = camC->ProjectionMatrix((float)W/(float)H) * camC->ViewMatrix();
        Vec3 eye = cam->transform->Position();
        Raster work; std::vector<std::uint32_t> outbuf;
        const std::uint32_t* px = RenderMeshesSS(work, outbuf, scene, vp, eye, W, H, 1, nullptr);
        int lit = 0, lowerRight = 0;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                if (px[y*W+x] & 0xFF000000u) { ++lit; if (y > H/2 && x > W/2) ++lowerRight; }
        CHECK(lit > 500);                  // the arm is clearly on screen
        CHECK(lowerRight > lit / 3);       // weighted to the lower-right
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
            CHECK(lf->holdingItem == true);
            CHECK_NEAR(lf->handRaise, -110.0f, 0.01f);
            CHECK_NEAR(lf->elbowBend, 25.0f, 0.01f);
        }
    }

    TEST_MAIN_RESULT();
}

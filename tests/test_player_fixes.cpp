#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>

using namespace okay;

int main() {
    RUN_SUITE("player_fixes");

    // ---- Lean: a SEPARATED character eases its body lean (used to stay 0 because the
    // separate-parts path returned before the easing ran — "camera leans, player not"). ----
    {
        Scene s("L");
        auto* ch = s.CreateGameObject("Hero")->AddComponent<Character>();
        ch->Apply(); ch->separateParts = true;
        ch->bodyLean = 30.0f;            // a controller would set this each frame
        s.Start();
        for (int i = 0; i < 30; ++i) { ch->bodyLean = 30.0f; s.Update(0.05f); }
        CHECK(ch->BodyLean() > 10.0f);   // eased toward the target (was stuck at 0)
        // The torso part actually rolled (its rotation is no longer identity).
        GameObject* torso = ch->Part(1);
        CHECK(torso != nullptr);
        if (torso) {
            Quat r = torso->transform->localRotation;
            CHECK(std::fabs(std::fabs(r.w) - 1.0f) > 1e-3f);   // not the identity pose
        }
    }

    // ---- Flashlight: adopts an existing beam and removes duplicates instead of
    // stacking new ones (Play→Stop→Play / reload used to spawn multiple). ----
    {
        Scene s("F");
        auto* camO = s.CreateGameObject("Cam");
        camO->AddComponent<Camera>();
        // Two leftover "Flashlight" beams under the camera (as a prior run would leave).
        for (int i = 0; i < 2; ++i) {
            auto* g = s.CreateGameObject("Flashlight");
            g->transform->SetParent(camO->transform, false);
            g->AddComponent<Light>();
        }
        s.CreateGameObject("Player")->AddComponent<Flashlight>();
        s.Start();
        s.Update(0.0f);   // flush the deferred destroy of the duplicate

        int n = 0;
        for (Transform* c : camO->transform->Children())
            if (c && c->gameObject && c->gameObject->name == "Flashlight") ++n;
        CHECK(n == 1);    // adopted one, removed the rest, created none
    }

    TEST_MAIN_RESULT();
}

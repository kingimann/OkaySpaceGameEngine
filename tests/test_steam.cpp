#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

// Exercises the Steam API surface through the always-available simulation
// backend (the real Steamworks backend is enabled with -DOKAY_WITH_STEAM).
int main() {
    RUN_SUITE("steam");

    auto svc = CreateSteamService();
    CHECK(svc != nullptr);

    SteamConfig cfg;
    cfg.appId = 480;
    CHECK(svc->Initialize(cfg));

    // Achievements
    CHECK(!svc->IsAchievementUnlocked("FIRST_FLIGHT"));
    CHECK(svc->UnlockAchievement("FIRST_FLIGHT"));
    CHECK(svc->IsAchievementUnlocked("FIRST_FLIGHT"));
    CHECK(svc->ClearAchievement("FIRST_FLIGHT"));
    CHECK(!svc->IsAchievementUnlocked("FIRST_FLIGHT"));

    // Stats
    svc->SetStat("kills", 7.0f);
    CHECK_NEAR(svc->GetStat("kills"), 7.0f, 0.001f);
    CHECK(svc->StoreStats());

    svc->SetRichPresence("status", "In the asteroid belt");
    svc->RunCallbacks();
    svc->Shutdown();

    // SteamManager component lifecycle inside a scene.
    {
        Scene scene("SteamScene");
        GameObject* go = scene.CreateGameObject("Steam");
        auto* mgr = go->AddComponent<SteamManager>();
        scene.Start();
        CHECK(mgr->Service() != nullptr);
        CHECK(mgr->UnlockAchievement("BOOT"));
        scene.Update(0.016f);
        scene.Destroy(go);
        scene.Update(0.016f);
    }

    TEST_MAIN_RESULT();
}

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

    // Stats (+ increment)
    svc->SetStat("kills", 7.0f);
    CHECK_NEAR(svc->GetStat("kills"), 7.0f, 0.001f);
    CHECK_NEAR(svc->IncrementStat("kills", 3.0f), 10.0f, 0.001f);
    CHECK_NEAR(svc->GetStat("kills"), 10.0f, 0.001f);
    CHECK(svc->StoreStats());

    // Achievement progress auto-unlocks when it reaches the max.
    CHECK(!svc->IsAchievementUnlocked("MARATHON"));
    svc->IndicateAchievementProgress("MARATHON", 50, 100);
    CHECK(!svc->IsAchievementUnlocked("MARATHON"));
    svc->IndicateAchievementProgress("MARATHON", 100, 100);
    CHECK(svc->IsAchievementUnlocked("MARATHON"));

    // Leaderboards: best score is kept, Top-N is ranked high-to-low.
    CHECK(svc->UploadLeaderboardScore("high_score", 100));
    CHECK(svc->UploadLeaderboardScore("high_score", 250));
    CHECK(svc->UploadLeaderboardScore("high_score", 175));  // not a new best
    {
        auto top = svc->DownloadLeaderboardTop("high_score", 5);
        CHECK(top.size() == 1);                 // one player in simulation
        CHECK(top[0].score == 250);             // kept the best
        CHECK(top[0].rank == 1);
    }

    // Steam Cloud: write / read / has / delete.
    CHECK(!svc->CloudHasFile("save.dat"));
    CHECK(svc->CloudWrite("save.dat", "level=3;coins=12"));
    CHECK(svc->CloudHasFile("save.dat"));
    CHECK(svc->CloudRead("save.dat") == "level=3;coins=12");
    CHECK(svc->CloudDelete("save.dat"));
    CHECK(!svc->CloudHasFile("save.dat"));
    CHECK(svc->CloudRead("missing.dat").empty());

    svc->ActivateOverlay("achievements");   // no-op in sim, just shouldn't crash
    CHECK(svc->FriendCount() == 0);

    // Apps / DLC / locale (the simulation grants ownership).
    CHECK(svc->OwnsApp(480));
    CHECK(svc->IsDlcInstalled(12345));
    CHECK(svc->Language() == "english");
    svc->UnlockAchievement("A1");
    svc->UnlockAchievement("A2");
    CHECK(svc->AchievementCount() >= 2);          // A1 + A2 (plus any earlier)
    CHECK(svc->IsAchievementUnlocked("A1"));
    CHECK(!svc->AchievementName(0).empty());

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

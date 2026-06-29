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

    // Steam Cloud: enumerate stored files.
    CHECK(svc->CloudEnabled());
    CHECK(svc->CloudFiles().empty());
    CHECK(svc->CloudWrite("a.sav", "1"));
    CHECK(svc->CloudWrite("b.sav", "2"));
    {
        auto files = svc->CloudFiles();
        CHECK(files.size() == 2);
        CHECK(files[0] == "a.sav" && files[1] == "b.sav");   // sorted
    }

    // Steam Workshop: publish auto-subscribes, query browses, (un)subscribe toggles.
    {
        WorkshopItem item;
        item.title = "Crimson Canyon";
        item.description = "A desert combat map";
        item.contentPath = "/maps/crimson";
        item.tags = {"map", "pvp"};
        std::uint64_t id = svc->WorkshopPublish(item);
        CHECK(id != 0);
        CHECK(svc->WorkshopIsSubscribed(id));
        CHECK(svc->WorkshopItemPath(id) == "/maps/crimson");
        CHECK(svc->WorkshopSubscribedItems().size() == 1);

        std::uint64_t id2 = svc->WorkshopPublish({0, "Forest Arena", "", "/maps/forest", {"map"}, false, false});
        CHECK(id2 != 0 && id2 != id);
        CHECK(svc->WorkshopQuery("", 10).size() == 2);          // all items
        CHECK(svc->WorkshopQuery("crimson", 10).size() == 1);   // title match
        CHECK(svc->WorkshopQuery("pvp", 10).size() == 1);       // tag match
        CHECK(svc->WorkshopQuery("map", 10).size() == 2);       // tag on both

        CHECK(svc->WorkshopUnsubscribe(id));
        CHECK(!svc->WorkshopIsSubscribed(id));
        CHECK(svc->WorkshopItemPath(id).empty());               // no longer installed
        CHECK(svc->WorkshopSubscribedItems().size() == 1);      // only Forest Arena left
        CHECK(svc->WorkshopSubscribe(id));
        CHECK(svc->WorkshopIsSubscribed(id));
    }

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
    CHECK(svc->GetRichPresence("status") == "In the asteroid belt");

    // Integer stats (kills/wins).
    svc->SetStatInt("kills", 3);
    CHECK(svc->GetStatInt("kills") == 3);
    CHECK(svc->IncrementStatInt("kills", 2) == 5);
    CHECK(svc->GetStatInt("misses") == 0);

    svc->RunCallbacks();
    svc->Shutdown();

    // --- Steam lobbies: two independent simulated services see + join one lobby ---
    {
        auto host = CreateSteamService();  auto join = CreateSteamService();
        host->Initialize(SteamConfig{});   join->Initialize(SteamConfig{});

        std::uint64_t lobby = host->CreateLobby(4, "Dust II");
        CHECK(lobby != 0);
        CHECK(host->CurrentLobby() == lobby);
        host->SetLobbyData("map", "dust2");

        // The other client browses the list and joins.
        auto list = join->LobbyList();
        bool found = false;
        for (const auto& l : list) if (l.id == lobby) { found = true; CHECK(l.memberCount == 1); CHECK(l.maxMembers == 4); }
        CHECK(found);
        CHECK(join->JoinLobby(lobby));
        CHECK(join->CurrentLobby() == lobby);
        CHECK(join->GetLobbyData(lobby, "map") == "dust2");   // lobby data is visible to members
        CHECK((int)host->LobbyMembers(lobby).size() == 2);    // host + joiner

        CHECK(host->InviteFriend(123456ULL));                 // sim records the invite

        join->LeaveLobby();
        CHECK(join->CurrentLobby() == 0);
        CHECK((int)host->LobbyMembers(lobby).size() == 1);    // joiner left
        host->LeaveLobby();
        CHECK(host->LobbyList().empty());                     // empty lobby is cleaned up
    }

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

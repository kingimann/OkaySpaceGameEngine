#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

// Exercises the PlayFab API surface via the always-available simulation backend
// (the real REST backend is enabled with -DOKAY_WITH_PLAYFAB).
int main() {
    RUN_SUITE("playfab");

    auto svc = CreatePlayFabService();
    CHECK(svc != nullptr);

    PlayFabConfig cfg;
    cfg.titleId = "DEMO1";
    CHECK(svc->Initialize(cfg));

    // The live REST backend needs a real PlayFab title and network; only the
    // simulation backend can be asserted against deterministically here.
    if (svc->IsRealBackend()) {
        std::cout << "  (real PlayFab backend active; skipping offline assertions)\n";
        TEST_MAIN_RESULT();
    }

    // Login
    CHECK(!svc->IsLoggedIn());
    CHECK(svc->LoginWithCustomId("player-42"));
    CHECK(svc->IsLoggedIn());
    CHECK(!svc->PlayFabId().empty());

    // Player data
    CHECK(svc->SetUserData("ship", "interceptor"));
    CHECK(svc->GetUserData("ship") == "interceptor");
    CHECK(svc->GetUserData("missing").empty());

    // Statistics + leaderboard
    CHECK(svc->UpdateStatistic("high_score", 1337));
    CHECK(svc->GetStatistic("high_score") == 1337);
    auto board = svc->GetLeaderboard("high_score", 10);
    CHECK(board.size() == 1);
    if (!board.empty()) CHECK(board[0].value == 1337);

    svc->Shutdown();

    // Manager component with auto-login inside a scene.
    {
        Scene scene("PFScene");
        GameObject* go = scene.CreateGameObject("PlayFab");
        auto* mgr = go->AddComponent<PlayFabManager>();
        mgr->config.titleId = "DEMO1";
        mgr->autoLoginCustomId = "device-abc";
        scene.Start();
        CHECK(mgr->Service() != nullptr);
        CHECK(mgr->IsLoggedIn());
        scene.Destroy(go);
        scene.Update(0.016f);
    }

    TEST_MAIN_RESULT();
}

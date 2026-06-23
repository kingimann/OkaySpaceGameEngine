#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

// The steam_* OkayScript builtins drive the shared process-wide Steam service
// (the in-memory simulation in tests), so games can do achievements/stats/cloud
// from script with no setup.
int main() {
    RUN_SUITE("steam_script");

    Steam::Shutdown();                 // fresh simulation backend
    Steam::Get().ClearAchievement("WIN");
    Steam::Get().SetStat("kills", 0.0f);

    Scene s("Steam"); s.physicsEnabled = false;
    GameObject* go = s.CreateGameObject("Logic");
    auto* sc = go->AddComponent<ScriptComponent>("okayscript");
    sc->LoadSource(
        "function start() {\n"
        "  steam_set_stat(\"kills\", 3);\n"
        "  steam_inc_stat(\"kills\", 2);\n"   // 5
        "  if (steam_get_stat(\"kills\") >= 5) { steam_unlock(\"WIN\"); }\n"
        "  steam_cloud_write(\"save\", \"level=2\");\n"
        "  steam_leaderboard(\"high\", 500);\n"
        "  steam_workshop_publish(\"My Map\", \"/maps/mine\", \"a level\");\n"
        "}\n");
    s.Start();
    s.Update(0.016f);

    // The shared service reflects what the script did.
    CHECK_NEAR(Steam::Get().GetStat("kills"), 5.0f, 1e-4f);
    CHECK(Steam::Get().IsAchievementUnlocked("WIN"));
    CHECK(Steam::Get().CloudRead("save") == "level=2");
    CHECK(Steam::Get().WorkshopSubscribedItems().size() == 1);   // publish auto-subscribes
    {
        auto top = Steam::Get().DownloadLeaderboardTop("high", 5);
        CHECK(top.size() == 1);
        CHECK(top[0].score == 500);
    }

    // --- NetworkManager no-code setup: Auto-Host on Start, and round-trips ---
    {
        Scene net("Net"); net.physicsEnabled = false;
        GameObject* ng = net.CreateGameObject("Net");
        auto* nm = ng->AddComponent<NetworkManager>();
        nm->autoStart = NetworkManager::AutoStart::Host;
        nm->autoPort = 0;                 // ephemeral port for the test
        nm->startName = "Server";
        net.Start();                      // Start() should host automatically
        CHECK(nm->IsServer());
        CHECK(nm->LocalName() == "Server");
        nm->Stop();

        // The component's settings survive a scene save/load.
        nm->autoStart = NetworkManager::AutoStart::Join;
        nm->autoPort = 45123;
        nm->autoHost = "10.0.0.5";
        std::string txt = SceneSerializer::Serialize(net);
        Scene net2("x"); SceneSerializer::Deserialize(net2, txt);
        auto* nm2 = net2.Find("Net") ? net2.Find("Net")->GetComponent<NetworkManager>() : nullptr;
        CHECK(nm2 != nullptr);
        if (nm2) {
            CHECK(nm2->autoStart == NetworkManager::AutoStart::Join);
            CHECK(nm2->autoPort == 45123);
            CHECK(nm2->autoHost == "10.0.0.5");
        }
        if (nm2) nm2->Stop();
    }

    TEST_MAIN_RESULT();
}

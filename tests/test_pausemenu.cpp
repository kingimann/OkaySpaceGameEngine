#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

// Pause menu + global game control: pausing freezes time, quit raises a flag the
// host honours, and the menu builds/clears its overlay.
int main() {
    RUN_SUITE("pausemenu");

    Game::Reset();

    // --- Global Game: pause freezes the time scale; resume restores it ---------
    {
        Time::SetTimeScale(1.0f);
        CHECK(!Game::Paused());
        Game::SetPaused(true);
        CHECK(Game::Paused());
        CHECK_NEAR(Time::TimeScale(), 0.0f, 1e-6f);   // gameplay frozen
        Game::SetPaused(false);
        CHECK(!Game::Paused());
        CHECK_NEAR(Time::TimeScale(), 1.0f, 1e-6f);   // restored

        CHECK(!Game::QuitRequested());
        Game::RequestQuit();
        CHECK(Game::QuitRequested());
        Game::Reset();
        CHECK(!Game::QuitRequested());
        CHECK(!Game::Paused());
    }

    // --- PauseMenu builds the overlay on Pause and clears it on Resume ---------
    {
        Scene s("PM");
        GameObject* go = s.CreateGameObject("Pause");
        auto* pm = go->AddComponent<PauseMenu>();
        pm->title = "Paused";
        CHECK(s.Find("PauseMenu UI") == nullptr);    // nothing built yet

        pm->Pause();
        CHECK(Game::Paused());
        GameObject* ui = s.Find("PauseMenu UI");
        CHECK(ui != nullptr);                        // overlay created
        CHECK(ui && ui->active);                     // and shown
        CHECK(s.FindObjectOfType<EventSystem>() != nullptr);   // input routing exists
        CHECK(s.Find("Pause_Resume") != nullptr);
        CHECK(s.Find("Pause_Quit") != nullptr);

        pm->Resume();
        CHECK(!Game::Paused());
        CHECK(!ui->active);                          // overlay hidden, not destroyed

        // OnDestroy unpauses so a stopped game never stays frozen.
        pm->Pause();
        CHECK(Game::Paused());
        s.Destroy(go);
        s.Update(0.0f);                              // flush destruction -> OnDestroy
        CHECK(!Game::Paused());
    }

    Game::Reset();

    // --- Serialization round-trip ---------------------------------------------
    {
        Scene s("PMS"); s.physicsEnabled = false;
        GameObject* go = s.CreateGameObject("Pause");
        auto* pm = go->AddComponent<PauseMenu>();
        pm->toggleKey = 'p'; pm->title = "Game Paused"; pm->showQuit = true;
        pm->showResume = false; pm->mainMenuScene = "menu.okayscene";
        pm->dimColor = Color::FromBytes(0, 0, 0, 200);

        Scene s2("x"); SceneSerializer::Deserialize(s2, SceneSerializer::Serialize(s));
        auto* p2 = s2.Find("Pause") ? s2.Find("Pause")->GetComponent<PauseMenu>() : nullptr;
        CHECK(p2 != nullptr);
        if (p2) {
            CHECK(p2->toggleKey == 'p');
            CHECK(p2->title == "Game Paused");
            CHECK(p2->showResume == false);
            CHECK(p2->showQuit == true);
            CHECK(p2->mainMenuScene == "menu.okayscene");
            CHECK_NEAR(p2->dimColor.a, 200.0f / 255.0f, 0.02f);
        }
    }

    Game::Reset();
    TEST_MAIN_RESULT();
}

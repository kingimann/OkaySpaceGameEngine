#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

static int countNamed(Scene& s, const char* name) {
    int n = 0;
    for (const auto& up : s.Objects()) if (up && up->name == name) ++n;
    return n;
}

// The pause menu builds EDITABLE UI objects in the scene (not spawned at Play),
// they serialize, and Play adopts them (no duplicate) + starts hidden.
int main() {
    RUN_SUITE("pause_menu");

    Scene s("S");
    auto* pm = s.CreateGameObject("PauseHolder")->AddComponent<PauseMenu>();
    CHECK(!pm->HasUI());
    pm->EnsureBuilt();                       // editor does this when you add the component
    CHECK(pm->HasUI());
    CHECK(countNamed(s, "PauseMenu UI") == 1);
    CHECK(countNamed(s, "Pause_Card") == 1);
    CHECK(s.Find("Pause_Resume") != nullptr);   // real, editable button objects exist
    CHECK(s.Find("Pause_Quit") != nullptr);

    // Round-trip: the UI persists in the scene file.
    std::string text = SceneSerializer::Serialize(s);
    Scene b("B");
    CHECK(SceneSerializer::Deserialize(b, text));
    CHECK(countNamed(b, "PauseMenu UI") == 1);
    CHECK(countNamed(b, "Pause_Card") == 1);

    // Play: the PauseMenu adopts the existing UI (no second copy) and starts hidden.
    b.Start();
    b.Update(0.016f);
    CHECK(countNamed(b, "PauseMenu UI") == 1);   // adopted, NOT rebuilt/duplicated
    CHECK(countNamed(b, "Pause_Card") == 1);
    GameObject* root = b.Find("PauseMenu UI");
    CHECK(root != nullptr);
    CHECK(root && root->active == false);        // hidden until paused

    // The pause menu is a real input-context consumer: open blocks gameplay
    // actions, close gives them back, and an outside unpause does too.
    {
        Prefs::Clear();
        InputActions::Restore();
        InputActions::ClearContexts();
        Input::FeedKeys({});
        Input::SetUICaptured(false);
        Game::SetPaused(false);

        Scene p("pause-input");
        auto* pm = p.CreateGameObject("PauseHolder")->AddComponent<PauseMenu>();
        p.Start();
        p.Update(0.016f);
        CHECK(!Game::Paused());
        CHECK(!Input::UICaptured());
        CHECK(!InputActions::ContextActive(PauseMenu::InputContextName()));

        Input::FeedKeys({' '});
        InputActions::Tick();
        CHECK(InputActions::Get("Jump"));
        Input::FeedKeys({});
        InputActions::Tick();

        Input::FeedKeys({27});
        p.Update(0.016f);                              // Escape opens the menu and pushes the context
        CHECK(Game::Paused());
        CHECK(Input::UICaptured());
        CHECK(InputActions::ContextActive(PauseMenu::InputContextName()));
        CHECK(InputActions::TopContext() == PauseMenu::InputContextName());
        CHECK(p.Find("PauseMenu UI") && p.Find("PauseMenu UI")->active);

        Input::FeedKeys({' ', 27});
        InputActions::Tick();
        CHECK(!InputActions::Get("Jump"));             // Block: gameplay sees nothing
        CHECK(!InputActions::Get("Pause"));            // the Pause action is gameplay too
        CHECK(Input::GetKey(27));                      // raw keys still read, so the menu can close
        CHECK(InputActions::KeyConsumed(' '));
        CHECK(InputActions::KeyConsumed(27));

        InputActions::DefineIn(PauseMenu::InputContextName(), "MenuConfirm", {"key:return"});
        Input::FeedKeys({'\r'});
        InputActions::Tick();
        CHECK(InputActions::Get("MenuConfirm"));       // an action in the menu's context still fires
        CHECK(!InputActions::Get("Jump"));

        Input::FeedKeys({});
        p.Update(0.016f);
        Input::FeedKeys({27});
        p.Update(0.016f);                              // Escape closes it and pops the context
        CHECK(!Game::Paused());
        CHECK(!Input::UICaptured());
        CHECK(!InputActions::ContextActive(PauseMenu::InputContextName()));
        CHECK(p.Find("PauseMenu UI") && !p.Find("PauseMenu UI")->active);
        Input::FeedKeys({' '});
        InputActions::Tick();
        CHECK(InputActions::Get("Jump"));

        // A context pushed underneath is still there after the menu closes.
        Input::FeedKeys({});
        InputActions::Tick();
        InputActions::PushContext("UI", InputActions::ContextMode::Consume);
        pm->Pause();
        CHECK(InputActions::TopContext() == PauseMenu::InputContextName());
        CHECK(InputActions::ContextActive("UI"));
        pm->Resume();
        CHECK(InputActions::TopContext() == "UI");
        CHECK(!InputActions::ContextActive(PauseMenu::InputContextName()));
        InputActions::ClearContexts();

        // Something else unpausing must release the capture and the context.
        pm->Pause();
        CHECK(Input::UICaptured() && InputActions::ContextActive(PauseMenu::InputContextName()));
        Game::SetPaused(false);
        p.Update(0.016f);
        CHECK(!Game::Paused());
        CHECK(!Input::UICaptured());
        CHECK(!InputActions::ContextActive(PauseMenu::InputContextName()));

        pm->Pause();
        p.DestroyImmediate(pm->gameObject);
        CHECK(!Game::Paused());
        CHECK(!Input::UICaptured());
        CHECK(!InputActions::ContextActive(PauseMenu::InputContextName()));

        Input::FeedKeys({});
        InputActions::ClearContexts();
        InputActions::Restore();
        Prefs::Clear();
        Game::SetPaused(false);
        Input::SetUICaptured(false);
    }

    TEST_MAIN_RESULT();
}

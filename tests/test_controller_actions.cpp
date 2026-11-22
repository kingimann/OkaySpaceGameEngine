// Built-in controllers read named actions, so a player's rebinding reaches them
// (foundation item 10). With the default bindings every controller reads exactly
// the keys it read before; once the player rebinds "Jump", "Sprint", "Crouch",
// "Move", "Aim", "Fire", "Reload" or "Interact", the controller follows the new
// key and the old one stops working.
#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>

using namespace okay;

namespace {

// One frame: the held keys and mouse buttons, then the actions sampled (as the player does).
void Frame(std::vector<char> keys, unsigned mouse = 0) {
    Input::FeedKeys(keys);
    Input::FeedMouse({0.0f, 0.0f}, mouse);
    Input::FeedGamepad({0.0f, 0.0f}, 0);
    InputActions::Tick(1.0f / 60.0f);
}
// Press a key from nothing held, so its "pressed this frame" edge is seen.
void Press(std::vector<char> keys, unsigned mouse = 0) { Frame({}); Frame(keys, mouse); }

void Fresh() { Prefs::Clear(); InputActions::Restore(); Frame({}); Frame({}); }

// The jump / sprint / crouch / move checks shared by the first- and third-person controllers.
template <class C> void PersonChecks(C* c) {
    c->lockCursor = false;
    // ---- Defaults: today's keys, unchanged ----
    Fresh();
    Press({' '});
    PlayerInput r = c->ReadLocalInput();
    CHECK(r.Pressed(PlayerInput::Jump) && r.Held(PlayerInput::Jump));
    Press({Input::KeyShift});
    CHECK(c->ReadLocalInput().Held(PlayerInput::Sprint));
    Press({Input::KeyCtrl});
    CHECK(c->ReadLocalInput().Held(PlayerInput::Crouch));
    Press({'w'});
    CHECK(c->ReadLocalInput().move.y > 0.9f);
    Press({Input::KeyUp});
    CHECK(c->ReadLocalInput().move.y > 0.9f);
    Press({'j'});
    r = c->ReadLocalInput();
    CHECK(!r.Pressed(PlayerInput::Jump) && !r.Held(PlayerInput::Jump));

    // ---- Rebound: the new keys work, the old ones do not ----
    CHECK(InputActions::Rebind("Jump", "key:j"));
    CHECK(InputActions::Rebind("Sprint", "key:v"));
    CHECK(InputActions::Rebind("Crouch", "key:c"));
    CHECK(InputActions::Rebind("Move", "keys:i,k,u,o"));
    Press({'j'});
    r = c->ReadLocalInput();
    CHECK(r.Pressed(PlayerInput::Jump) && r.Held(PlayerInput::Jump));
    Press({' '});
    r = c->ReadLocalInput();
    CHECK(!r.Pressed(PlayerInput::Jump) && !r.Held(PlayerInput::Jump));
    Press({'v'});
    CHECK(c->ReadLocalInput().Held(PlayerInput::Sprint));
    Press({Input::KeyShift});
    CHECK(!c->ReadLocalInput().Held(PlayerInput::Sprint));
    Press({'c'});
    CHECK(c->ReadLocalInput().Held(PlayerInput::Crouch));
    Press({Input::KeyCtrl});
    CHECK(!c->ReadLocalInput().Held(PlayerInput::Crouch));
    Press({'i'});
    CHECK(c->ReadLocalInput().move.y > 0.9f);
    Press({'w'});
    CHECK(std::fabs(c->ReadLocalInput().move.y) < 0.01f);

    // ---- Aim on the right mouse button follows the "Aim" action ----
    Fresh();
    Press({}, 1u << 1);
    CHECK(c->ReadLocalInput().Held(PlayerInput::AltFire));
    CHECK(InputActions::Rebind("Aim", "key:f"));
    Press({'f'});
    CHECK(c->ReadLocalInput().Held(PlayerInput::AltFire));
    Press({}, 1u << 1);
    CHECK(!c->ReadLocalInput().Held(PlayerInput::AltFire));

    // ---- Reset puts the old keys back ----
    InputActions::Reset();
    Press({' '});
    CHECK(c->ReadLocalInput().Pressed(PlayerInput::Jump));
    Press({'j'});
    CHECK(!c->ReadLocalInput().Pressed(PlayerInput::Jump));
    Frame({}); Frame({});
}

} // namespace

int main() {
    RUN_SUITE("controller_actions");

    {
        Scene s("FP");
        auto* c = s.CreateGameObject("P")->AddComponent<FirstPersonController>();
        s.Start();
        PersonChecks(c);
    }
    {
        Scene s("TP");
        auto* c = s.CreateGameObject("P")->AddComponent<ThirdPersonController>();
        s.Start();
        PersonChecks(c);
    }

    // ---- Character Controller 3D: jump, sprint (its own key field), crouch ----
    {
        Scene s("CC3");
        auto* c = s.CreateGameObject("P")->AddComponent<CharacterController3D>();
        c->sprintKey = Input::KeyShift;
        s.Start();
        Fresh();
        Press({' '});
        CHECK(c->ReadLocalInput().Pressed(PlayerInput::Jump));
        Press({Input::KeyShift});
        CHECK(c->ReadLocalInput().Held(PlayerInput::Sprint));
        CHECK(InputActions::Rebind("Jump", "key:j"));
        CHECK(InputActions::Rebind("Sprint", "key:v"));
        Press({'j'});
        CHECK(c->ReadLocalInput().Pressed(PlayerInput::Jump));
        Press({' '});
        CHECK(!c->ReadLocalInput().Pressed(PlayerInput::Jump));
        Press({'v'});
        CHECK(c->ReadLocalInput().Held(PlayerInput::Sprint));
        Press({Input::KeyShift});
        CHECK(!c->ReadLocalInput().Held(PlayerInput::Sprint));
        // A designer's own key (not the action's default) stays theirs after a rebind.
        c->sprintKey = 'x';
        Press({'x'});
        CHECK(c->ReadLocalInput().Held(PlayerInput::Sprint));
    }

    // ---- Character Controller 2D: jump (Space, and W as today) ----
    {
        Scene s("CC2");
        auto* c = s.CreateGameObject("P")->AddComponent<CharacterController2D>();
        s.Start();
        Fresh();
        Press({' '});
        CHECK(c->ReadLocalInput().Pressed(PlayerInput::Jump));
        Press({'w'});
        CHECK(c->ReadLocalInput().Pressed(PlayerInput::Jump));
        CHECK(InputActions::Rebind("Jump", "key:j"));
        Press({'j'});
        CHECK(c->ReadLocalInput().Pressed(PlayerInput::Jump));
        Press({' '});
        CHECK(!c->ReadLocalInput().Pressed(PlayerInput::Jump));
    }

    // ---- Top-down 2D: fire on the left mouse button follows "Fire" ----
    {
        Scene s("TD2");
        auto* c = s.CreateGameObject("P")->AddComponent<TopDownController2D>();
        c->fireButton = 0;
        s.Start();
        Fresh();
        Press({}, 1u);
        CHECK(c->ReadLocalInput().Held(PlayerInput::Fire));
        CHECK(InputActions::Rebind("Fire", "key:g"));
        Press({'g'});
        CHECK(c->ReadLocalInput().Held(PlayerInput::Fire));
        Press({}, 1u);
        CHECK(!c->ReadLocalInput().Held(PlayerInput::Fire));
    }

    // ======== Part 2: vehicles, the pause menu, free roam, platformer, dash, lean and prone ========

    // ---- Vehicle Controller (older arcade model): throttle / brake / steer / handbrake ----
    {
        // A fresh car each time, the keys held for two short frames; returns its signed speed.
        auto drive = [](std::vector<char> keys, bool openWorld) {
            Scene s("Car");
            auto* car = s.CreateGameObject("Car")->AddComponent<VehicleController>();
            car->openWorld = openWorld;
            car->usePlayerSettings = false;
            s.Start();
            Press(keys);
            car->Update(0.1f);
            car->Update(0.1f);
            return car->Speed();
        };
        Fresh();
        CHECK(drive({'w'}, false) > 0.1f);
        CHECK(drive({Input::KeyUp}, false) > 0.1f);
        CHECK(drive({'s'}, false) < -0.1f);
        CHECK(InputActions::Has("Throttle") && InputActions::Has("Brake") && InputActions::Has("SteerLeft") &&
              InputActions::Has("SteerRight") && InputActions::Has("Handbrake"));
        CHECK(InputActions::Rebind("Throttle", "key:i"));
        CHECK(InputActions::Rebind("Brake", "key:k"));
        CHECK(drive({'i'}, false) > 0.1f);
        CHECK(std::fabs(drive({'w'}, false)) < 0.01f);
        CHECK(drive({'k'}, false) < -0.1f);
        CHECK(std::fabs(drive({'s'}, false)) < 0.01f);
        // Open-world model: the same actions drive it.
        CHECK(drive({'i'}, true) > 0.01f);
        CHECK(std::fabs(drive({'w'}, true)) < 0.001f);
        InputActions::Reset();
        CHECK(drive({'w'}, true) > 0.01f);
    }

    // ---- Vehicle Controller (open world): horn, headlights, gears ----
    {
        Scene s("Car2");
        auto* car = s.CreateGameObject("Car")->AddComponent<VehicleController>();
        car->openWorld = true;
        car->usePlayerSettings = false;
        car->gearbox = 1;   // manual: the gear keys work
        s.Start();
        Fresh();
        Frame({'h'});
        car->Update(0.016f);
        CHECK(car->HornOn());
        Press({'l'});
        car->Update(0.016f);
        CHECK(car->LightsOn());
        Frame({}); car->Update(0.016f);
        const int g = car->Gear();
        Press({'x'});
        car->Update(0.016f);
        CHECK(car->Gear() == g + 1);
        Frame({}); car->Update(0.016f);

        CHECK(InputActions::Rebind("Horn", "key:n"));
        CHECK(InputActions::Rebind("Headlights", "key:o"));
        CHECK(InputActions::Rebind("GearUp", "key:u"));
        Frame({'h'});
        car->Update(0.016f);
        CHECK(!car->HornOn());
        Frame({'n'});
        car->Update(0.016f);
        CHECK(car->HornOn());
        Frame({}); car->Update(0.016f);
        Press({'l'});
        car->Update(0.016f);
        CHECK(car->LightsOn());          // L no longer toggles them off
        Press({'o'});
        car->Update(0.016f);
        CHECK(!car->LightsOn());
        Frame({}); car->Update(0.016f);
        const int g2 = car->Gear();
        Press({'x'});
        car->Update(0.016f);
        CHECK(car->Gear() == g2);
        Frame({}); car->Update(0.016f);
        Press({'u'});
        car->Update(0.016f);
        CHECK(car->Gear() == g2 + 1);
        Frame({}); car->Update(0.016f);
    }

    // ---- The driving actions do not clash with walking: they live in the "Vehicle" context ----
    {
        Fresh();
        const InputActions::Action* t = InputActions::Find("Throttle");
        CHECK(t && t->context == "Vehicle");
        CHECK(RemapAllConflicts().empty());
        CHECK(RemapConflicts("Handbrake", "key:h").size() == 1);   // Horn, in the same context
        CHECK(RemapConflicts("Jump", "key:h").empty());            // a walking action may share a driving key
    }

    // ---- Pause Menu: a rebound "Pause" opens and closes it, although the open menu blocks actions ----
    {
        Game::SetPaused(false);
        Scene s("Pause");
        auto* pm = s.CreateGameObject("PauseHolder")->AddComponent<PauseMenu>();
        s.Start();
        Fresh();
        Press({27});
        pm->Update(0.016f);
        CHECK(Game::Paused());
        Press({27});
        pm->Update(0.016f);
        CHECK(!Game::Paused());

        CHECK(InputActions::Rebind("Pause", "key:p"));
        Press({27});
        pm->Update(0.016f);
        CHECK(!Game::Paused());          // Escape no longer opens it
        Press({'p'});
        pm->Update(0.016f);
        CHECK(Game::Paused());
        CHECK(InputActions::ContextActive(PauseMenu::InputContextName()));
        Press({27});
        pm->Update(0.016f);
        CHECK(Game::Paused());           // nor closes it
        Press({'p'});
        pm->Update(0.016f);
        CHECK(!Game::Paused());          // P closes it, through the menu's Block context
        CHECK(!InputActions::ContextActive(PauseMenu::InputContextName()));
        Game::SetPaused(false);
    }

    // ---- Free Roam: move, rise (Jump), descend (Crouch), boost (Sprint) ----
    {
        Game::SetPaused(false);
        Scene s("Fly");
        auto* c = s.CreateGameObject("Cam")->AddComponent<FreeRoamController>();
        c->acceleration = 0.0f;
        s.Start();
        auto step = [&](std::vector<char> keys) {
            const Vec3 a = c->transform->Position();
            Press(keys);
            c->Update(0.1f);
            const Vec3 b = c->transform->Position();
            return Vec3{b.x - a.x, b.y - a.y, b.z - a.z};
        };
        Fresh();
        CHECK(step({' '}).y > 0.1f);
        CHECK(step({Input::KeyCtrl}).y < -0.1f);
        CHECK(step({'w'}).Magnitude() > 0.1f);
        const float slow = step({'w'}).Magnitude();
        CHECK(step({'w', Input::KeyShift}).Magnitude() > slow * 1.5f);
        CHECK(InputActions::Rebind("Jump", "key:j"));
        CHECK(InputActions::Rebind("Crouch", "key:v"));
        CHECK(InputActions::Rebind("Move", "keys:i,k,u,o"));
        CHECK(InputActions::Rebind("Sprint", "key:b"));
        CHECK(step({'j'}).y > 0.1f);
        CHECK(std::fabs(step({' '}).y) < 0.01f);
        CHECK(step({'v'}).y < -0.1f);
        CHECK(std::fabs(step({Input::KeyCtrl}).y) < 0.01f);
        CHECK(step({'i'}).Magnitude() > 0.1f);
        CHECK(step({'w'}).Magnitude() < 0.01f);
        CHECK(step({'i', 'b'}).Magnitude() > slow * 1.5f);
        CHECK(step({'i', Input::KeyShift}).Magnitude() < slow * 1.2f);
    }

    // ---- Platformer 2.5D: the layer keys are Move's up and down keys ----
    {
        Game::SetPaused(false);
        Scene s("P25");
        auto* c = s.CreateGameObject("P")->AddComponent<Platformer25DController>();
        c->checkLayerFree = false;
        s.Start();
        Fresh();
        c->layer = 0;
        Press({'w'});
        c->Update(1.0f / 60.0f);
        CHECK(c->layer == 1);
        Press({'s'});
        c->Update(1.0f / 60.0f);
        CHECK(c->layer == 0);
        CHECK(InputActions::Rebind("Move", "keys:i,k,j,l"));
        Press({'w'});
        c->Update(1.0f / 60.0f);
        CHECK(c->layer == 0);
        Press({'i'});
        c->Update(1.0f / 60.0f);
        CHECK(c->layer == 1);
        Press({'k'});
        c->Update(1.0f / 60.0f);
        CHECK(c->layer == 0);
        Frame({}); c->Update(1.0f / 60.0f);
    }

    // ---- Top Down Shooter: dash (Space) follows a "Dash" action once the player binds one ----
    {
        Game::SetPaused(false);
        auto dashes = [](std::vector<char> keys) {
            Scene s("TDS");
            auto* c = s.CreateGameObject("P")->AddComponent<TopDownShooterController>();
            s.Start();
            Press(keys);
            c->Update(1.0f / 60.0f);
            return c->IsDashing();
        };
        Fresh();
        CHECK(dashes({' '}));
        CHECK(!dashes({'f'}));
        CHECK(InputActions::Has("Dash"));
        CHECK(InputActions::Rebind("Dash", "key:f"));
        CHECK(dashes({'f'}));
        CHECK(!dashes({' '}));
    }

    // ---- Lean and prone: actions "LeanLeft", "LeanRight", "Prone" ----
    {
        Scene s("Lean");
        auto* c = s.CreateGameObject("P")->AddComponent<FirstPersonController>();
        c->lockCursor = false;
        s.Start();
        Fresh();
        Press({'q'});
        CHECK(c->ReadLocalInput().Held(PlayerInput::LeanLeft));
        Press({'e'});
        CHECK(c->ReadLocalInput().Held(PlayerInput::LeanRight));
        Press({'z'});
        CHECK(c->ReadLocalInput().Held(PlayerInput::Prone));
        CHECK(InputActions::Has("LeanLeft") && InputActions::Has("LeanRight") && InputActions::Has("Prone"));
        CHECK(InputActions::Rebind("LeanLeft", "key:1"));
        CHECK(InputActions::Rebind("LeanRight", "key:3"));
        CHECK(InputActions::Rebind("Prone", "key:x"));
        Press({'1'});
        CHECK(c->ReadLocalInput().Held(PlayerInput::LeanLeft));
        Press({'q'});
        CHECK(!c->ReadLocalInput().Held(PlayerInput::LeanLeft));
        Press({'3'});
        CHECK(c->ReadLocalInput().Held(PlayerInput::LeanRight));
        Press({'e'});
        CHECK(!c->ReadLocalInput().Held(PlayerInput::LeanRight));
        Press({'x'});
        CHECK(c->ReadLocalInput().Held(PlayerInput::Prone));
        Press({'z'});
        CHECK(!c->ReadLocalInput().Held(PlayerInput::Prone));
        Frame({}); Frame({});
    }
    {
        // StanceControl (the shared crouch / prone part) follows "Prone" too.
        Scene s("CC3P");
        auto* c = s.CreateGameObject("P")->AddComponent<CharacterController3D>();
        s.Start();
        Fresh();
        Press({'z'});
        CHECK(c->ReadLocalInput().Held(PlayerInput::Prone));
        CHECK(InputActions::Rebind("Prone", "key:x"));
        Press({'x'});
        CHECK(c->ReadLocalInput().Held(PlayerInput::Prone));
        Press({'z'});
        CHECK(!c->ReadLocalInput().Held(PlayerInput::Prone));
    }

    Prefs::Clear();
    InputActions::Restore();
    TEST_MAIN_RESULT();
}

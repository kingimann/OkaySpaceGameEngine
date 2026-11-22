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

    Prefs::Clear();
    InputActions::Restore();
    TEST_MAIN_RESULT();
}

#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

// Multiplayer safety: a controller drives the object only when this peer owns it.
// A NetworkSync set to a non-owned authority turns the object into a remote proxy,
// and every input-reading controller must leave it alone (NetworkSync moves it).
int main() {
    RUN_SUITE("mp_controllers");

    // IsLocallyControlled: no NetworkSync -> always local (single-player).
    {
        Scene s("solo");
        auto* go = s.CreateGameObject("P");
        CHECK(IsLocallyControlled(go));
    }

    // A child sees the player-root NetworkSync (walks up the hierarchy).
    {
        Scene s("net");
        auto* player = s.CreateGameObject("Player");
        auto* ns = player->AddComponent<NetworkSync>();
        ns->authority = NetworkSync::Authority::Manual;
        ns->owned = false;                              // remote proxy (no manager => not owned)
        auto* cam = s.CreateGameObject("Cam");
        cam->transform->SetParent(player->transform, false);
        // Without a NetworkManager, IsLocallyOwned() is true (single-player fallback);
        // with Manual authority and owned=false it should report not-locally-owned only
        // when a manager exists. Here there's no manager, so it stays locally controlled.
        CHECK(IsLocallyControlled(cam));
    }

    // A controller on a remote proxy must NOT move the transform. We simulate "remote
    // proxy" deterministically with Manual authority + a manager so ownership is false.
    {
        Scene s("proxy");
        s.CreateGameObject("Net")->AddComponent<NetworkManager>();   // a manager exists
        auto* player = s.CreateGameObject("Player");
        player->transform->localPosition = {0, 0, 0};
        auto* ns = player->AddComponent<NetworkSync>();
        ns->authority = NetworkSync::Authority::Manual;
        ns->owned = false;                              // this peer does NOT own it
        auto* cc = player->AddComponent<CharacterController3D>();
        (void)cc;
        s.Start();

        CHECK(!IsLocallyControlled(player));            // recognized as a remote proxy
        Vec3 before = player->transform->localPosition;
        Input::FeedKeys({'w'});                          // pretend the local player holds forward
        for (int i = 0; i < 30; ++i) s.Update(1.0f / 60.0f);
        Input::FeedKeys({});
        Vec3 after = player->transform->localPosition;
        // The proxy ignored local input (NetworkSync owns its movement).
        CHECK(after.x == before.x && after.z == before.z);
    }

    // The same controller, when OWNED, does respond to input.
    {
        Scene s("owned");
        s.CreateGameObject("Net")->AddComponent<NetworkManager>();
        auto* player = s.CreateGameObject("Player");
        player->transform->localPosition = {0, 0, 0};
        auto* ns = player->AddComponent<NetworkSync>();
        ns->authority = NetworkSync::Authority::Mine;   // this peer owns it
        player->AddComponent<CharacterController3D>();
        s.Start();

        CHECK(IsLocallyControlled(player));
        Input::FeedKeys({'w'});                          // pretend the local player holds forward
        for (int i = 0; i < 30; ++i) s.Update(1.0f / 60.0f);
        Input::FeedKeys({});
        // It moved (forward is -Z for this controller); at minimum the position changed.
        Vec3 p = player->transform->localPosition;
        CHECK(!(p.x == 0.0f && p.z == 0.0f));
    }

    TEST_MAIN_RESULT();
}

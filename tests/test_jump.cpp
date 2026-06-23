#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

// Builds a player on a ground plane with a rigidbody + the given controller, lets
// it settle, and returns the pieces so each test can drive jumps.
template <class Ctrl>
struct Rig {
    Scene scene{"J"};
    GameObject* player = nullptr;
    Rigidbody3D* rb = nullptr;
    Ctrl* ctrl = nullptr;
    Rig() {
        GameObject* g = scene.CreateGameObject("Ground");
        g->transform->SetPosition({0, -0.5f, 0});
        g->AddComponent<BoxCollider3D>()->size = {100, 1, 100};
        player = scene.CreateGameObject("Player");
        player->transform->SetPosition({0, 1.0f, 0});
        rb = player->AddComponent<Rigidbody3D>();
        player->AddComponent<BoxCollider3D>()->size = {1, 2, 1};
        ctrl = player->AddComponent<Ctrl>();
        scene.Start();
        settle();
    }
    void settle() { for (int i = 0; i < 90; ++i) { Input::FeedKeys({}); scene.Update(1.0f / 60.0f); } }
    void step(bool space) { Input::FeedKeys(space ? std::vector<char>{' '} : std::vector<char>{}); scene.Update(1.0f / 60.0f); }
    // One "tap" = a release then a press, so GetKeyDown sees a fresh edge.
    void tap() { step(false); step(true); }
};

int main() {
    RUN_SUITE("jump");

    // --- Single jump: tapping mid-air must NOT re-launch (no endless jumping) ---
    {
        Rig<FirstPersonController> r;
        r.ctrl->maxJumps = 1;
        CHECK(Mathf::Abs(r.rb->velocity.y) < 0.5f);     // resting on the ground
        r.tap();
        CHECK(r.rb->velocity.y > 1.0f);                  // jumped up

        // Spam taps for the whole arc; with one jump it must still come back down.
        bool fell = false;
        for (int i = 0; i < 240; ++i) {
            r.tap();
            if (r.rb->velocity.y < -0.5f) { fell = true; break; }   // descending = didn't re-jump at apex
        }
        CHECK(fell);                                     // it stopped going up — no infinite jump

        // And once landed, it can jump again.
        r.settle();
        CHECK(Mathf::Abs(r.rb->velocity.y) < 0.5f);
        r.tap();
        CHECK(r.rb->velocity.y > 1.0f);
    }

    // --- Double jump: a single mid-air tap gives a second launch ---
    {
        Rig<ThirdPersonController> r;
        r.ctrl->maxJumps = 2;
        r.tap();
        CHECK(r.rb->velocity.y > 1.0f);                  // first jump
        // Rise a few frames so we're clearly airborne and slowing.
        for (int i = 0; i < 8; ++i) r.step(false);
        float beforeVy = r.rb->velocity.y;
        r.tap();                                         // second (air) jump
        CHECK(r.rb->velocity.y > beforeVy + 1.0f);       // got another upward kick

        // A THIRD tap must do nothing (only two jumps).
        for (int i = 0; i < 8; ++i) r.step(false);
        float vyBefore3 = r.rb->velocity.y;
        r.tap();
        CHECK(r.rb->velocity.y <= vyBefore3 + 0.5f);     // no third launch
    }

    Input::FeedKeys({});
    TEST_MAIN_RESULT();
}

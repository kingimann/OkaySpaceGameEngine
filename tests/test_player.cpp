#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("player");

    // --- Third-person controller drives the Character animation from movement ---
    {
        Scene sc("s");
        GameObject* p = sc.CreateGameObject("Player");
        auto* ch = p->AddComponent<Character>();
        auto* tp = p->AddComponent<ThirdPersonController>();
        ch->anim = 0;

        Input::FeedKeys({});            // no input -> idle
        tp->Update(0.016f);
        CHECK(ch->anim == 1);

        Input::FeedKeys({'w'});         // forward -> walk
        tp->Update(0.016f);
        CHECK(ch->anim == 2);

        tp->sprintKey = 'r';            // run while moving + sprint held
        Input::FeedKeys({'w', 'r'});
        tp->Update(0.016f);
        CHECK(ch->anim == 3);
    }

    // --- First-person controller: same animation driving, and it moves ---
    {
        Scene sc("s");
        GameObject* p = sc.CreateGameObject("Player");
        auto* ch = p->AddComponent<Character>();
        auto* fp = p->AddComponent<FirstPersonController>();
        ch->anim = 0;

        Vec3 start = p->transform->localPosition;
        Input::FeedKeys({'w'});
        fp->Update(0.1f);
        CHECK(ch->anim == 2);                                   // walking
        CHECK((p->transform->localPosition - start).Magnitude() > 0.01f);  // moved
    }

    TEST_MAIN_RESULT();
}

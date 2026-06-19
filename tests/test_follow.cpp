#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("follow");

    // A follower moves toward its named target and stops within stopDistance.
    {
        Scene s("F"); s.physicsEnabled = false;
        GameObject* player = s.CreateGameObject("Player");
        player->transform->localPosition = {10, 0, 0};
        GameObject* enemy = s.CreateGameObject("Enemy");
        auto* ft = enemy->AddComponent<FollowTarget2D>();
        ft->target = "Player"; ft->speed = 4.0f; ft->stopDistance = 1.0f;
        s.Start();
        for (int i = 0; i < 200; ++i) s.Update(1.0f / 60.0f);
        float dx = player->transform->localPosition.x - enemy->transform->localPosition.x;
        CHECK(enemy->transform->localPosition.x > 5.0f);      // moved toward player
        CHECK_NEAR(dx, 1.0f, 0.2f);                           // stopped ~stopDistance away
    }

    // Missing target = no movement; serialization round-trips.
    {
        Scene s("F2"); s.physicsEnabled = false;
        GameObject* e = s.CreateGameObject("E");
        auto* ft = e->AddComponent<FollowTarget2D>();
        ft->target = "Ghost"; ft->speed = 5.0f; ft->stopDistance = 2.5f;
        s.Start(); s.Update(0.1f);
        CHECK_NEAR(e->transform->localPosition.x, 0.0f, 1e-5f);

        std::string txt = SceneSerializer::Serialize(s);
        Scene s2("x"); SceneSerializer::Deserialize(s2, txt);
        auto* f2 = s2.Find("E") ? s2.Find("E")->GetComponent<FollowTarget2D>() : nullptr;
        CHECK(f2 != nullptr);
        if (f2) { CHECK(f2->target == "Ghost"); CHECK_NEAR(f2->stopDistance, 2.5f, 1e-4f); }
    }

    TEST_MAIN_RESULT();
}

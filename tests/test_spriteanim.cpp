#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("spriteanim");

    // --- Animator drives the SpriteRenderer's texture over time ---
    {
        Scene scene("Anim");
        GameObject* go = scene.CreateGameObject("Hero");
        auto* sr = go->AddComponent<SpriteRenderer>();
        auto* an = go->AddComponent<SpriteAnimator>();
        an->frames = {"a.png", "b.png", "c.png"};
        an->fps = 10.0f; // 0.1s per frame
        an->loop = true;

        scene.Start();
        CHECK(sr->texture == "a.png");      // frame 0 applied on Start
        scene.Update(0.1f);
        CHECK(an->CurrentFrame() == 1);
        CHECK(sr->texture == "b.png");
        scene.Update(0.1f);
        CHECK(sr->texture == "c.png");
        scene.Update(0.1f);
        CHECK(sr->texture == "a.png");      // wrapped around (loop)
    }

    // --- Non-looping animation stops on the last frame ---
    {
        Scene scene("Once");
        GameObject* go = scene.CreateGameObject("FX");
        go->AddComponent<SpriteRenderer>();
        auto* an = go->AddComponent<SpriteAnimator>();
        an->frames = {"f0", "f1"};
        an->fps = 10.0f;
        an->loop = false;

        scene.Start();
        for (int i = 0; i < 10; ++i) scene.Update(0.1f);
        CHECK(an->CurrentFrame() == 1); // clamped to last
        CHECK(!an->playing);            // stopped
    }

    // --- Frames + params survive serialization ---
    {
        Scene scene("Ser");
        GameObject* go = scene.CreateGameObject("A");
        go->AddComponent<SpriteRenderer>();
        auto* an = go->AddComponent<SpriteAnimator>();
        an->frames = {"walk_0.png", "walk_1.png", "walk_2.png"};
        an->fps = 12.0f;
        an->loop = false;

        std::string text = SceneSerializer::Serialize(scene);
        Scene loaded("L");
        std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        auto* r = loaded.Find("A")->GetComponent<SpriteAnimator>();
        CHECK(r != nullptr);
        CHECK(r->frames.size() == 3);
        CHECK(r->frames[2] == "walk_2.png");
        CHECK_NEAR(r->fps, 12.0f, 0.001f);
        CHECK(!r->loop);
    }

    TEST_MAIN_RESULT();
}

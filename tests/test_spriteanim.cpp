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

    // --- Atlas mode walks texture cells via the SpriteRenderer's uv region ---
    {
        Scene scene("Atlas");
        GameObject* go = scene.CreateGameObject("Sheet");
        auto* sr = go->AddComponent<SpriteRenderer>();
        sr->texture = "sheet.png";
        auto* an = go->AddComponent<SpriteAnimator>();
        an->atlasColumns = 4; // 4x1 strip
        an->atlasRows = 1;
        an->fps = 10.0f;
        an->loop = true;

        CHECK(an->FrameCount() == 4);
        scene.Start();
        // Frame 0 = leftmost quarter of the texture.
        CHECK_NEAR(sr->uvMin.x, 0.0f, 0.001f);
        CHECK_NEAR(sr->uvMax.x, 0.25f, 0.001f);
        scene.Update(0.1f); // -> frame 1
        CHECK(an->CurrentFrame() == 1);
        CHECK_NEAR(sr->uvMin.x, 0.25f, 0.001f);
        CHECK_NEAR(sr->uvMax.x, 0.5f, 0.001f);
        // Texture itself is unchanged in atlas mode.
        CHECK(sr->texture == "sheet.png");
    }

    // --- uv region + atlas fields survive serialization ---
    {
        Scene scene("UV");
        GameObject* go = scene.CreateGameObject("S");
        auto* sr = go->AddComponent<SpriteRenderer>();
        sr->texture = "t.png";
        sr->uvMin = {0.25f, 0.5f};
        sr->uvMax = {0.5f, 1.0f};
        auto* an = go->AddComponent<SpriteAnimator>();
        an->atlasColumns = 3; an->atlasRows = 2; an->atlasCount = 5;

        std::string text = SceneSerializer::Serialize(scene);
        Scene loaded("L");
        std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        auto* rs = loaded.Find("S")->GetComponent<SpriteRenderer>();
        CHECK(rs != nullptr);
        CHECK_NEAR(rs->uvMin.x, 0.25f, 0.001f);
        CHECK_NEAR(rs->uvMax.y, 1.0f, 0.001f);
        auto* ra = loaded.Find("S")->GetComponent<SpriteAnimator>();
        CHECK(ra->atlasColumns == 3);
        CHECK(ra->atlasRows == 2);
        CHECK(ra->atlasCount == 5);
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

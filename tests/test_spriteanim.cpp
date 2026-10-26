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

    // --- Named clips: Play(name) switches, current-clip query, and save/load ---
    {
        Scene scene("Clips");
        GameObject* go = scene.CreateGameObject("P");
        auto* sr = go->AddComponent<SpriteRenderer>();
        sr->texture = "sheet.png";
        auto* an = go->AddComponent<SpriteAnimator>();
        an->atlasColumns = 4; an->atlasRows = 3;
        SpriteClip idle; idle.name = "idle"; idle.row = 0; idle.fps = 10.0f;
        SpriteClip run;  run.name = "run";   run.row = 2;  run.fps = 8.0f;
        SpriteClip jump; jump.name = "jump"; jump.frames = {"j0.png", "j1.png"}; jump.loop = false;
        an->clips = {idle, run, jump};

        scene.Start();
        CHECK(an->CurrentClip().empty());           // base animation until a clip plays
        CHECK(!an->Play("nope"));
        CHECK(an->Play("run"));
        CHECK(an->CurrentClip() == "run");
        CHECK(an->FrameCount() == 4);               // one atlas row
        CHECK_NEAR(sr->uvMin.y, 2.0f / 3.0f, 0.001f);
        for (int i = 0; i < 1000; ++i) scene.Update(0.125f);  // long run: clock wraps
        CHECK(an->CurrentFrame() == 0);             // 1000 frames % 4
        scene.Update(0.125f);
        CHECK(an->CurrentFrame() == 1);
        CHECK(an->Play("jump"));
        CHECK(sr->texture == "j0.png");

        std::string text = SceneSerializer::Serialize(scene);
        Scene loaded("L");
        std::string err;
        CHECK(SceneSerializer::Deserialize(loaded, text, &err));
        auto* ra = loaded.Find("P")->GetComponent<SpriteAnimator>();
        CHECK(ra->clips.size() == 3);
        CHECK(ra->clips[1].name == "run");
        CHECK(ra->clips[1].row == 2);
        CHECK(ra->clips[2].frames.size() == 2);
        CHECK(!ra->clips[2].loop);
        CHECK(ra->atlasColumns == 4);
    }

    // --- Platformer controller picks idle / run / jump from movement ---
    {
        Scene scene("Ctl");
        GameObject* go = scene.CreateGameObject("Hero");
        go->AddComponent<SpriteRenderer>();
        auto* rb = go->AddComponent<Rigidbody2D>();
        auto* cc = go->AddComponent<CharacterController2D>();
        cc->mode = CharacterController2D::Mode::Platformer;
        auto* an = go->AddComponent<SpriteAnimator>();
        an->frames = {"base.png"};
        SpriteClip a; a.name = "idle"; a.frames = {"idle.png"};
        SpriteClip b; b.name = "jump"; b.frames = {"jump.png"};
        an->clips = {a, b};
        scene.physicsEnabled = false;
        scene.Start();
        scene.Update(0.016f);
        CHECK(an->CurrentClip() == "idle");         // standing still
        rb->velocity.y = 5.0f;                      // airborne going up
        scene.Update(0.016f);
        CHECK(an->CurrentClip() == "jump");
        rb->velocity.y = -5.0f;                     // no "fall" clip -> stays on jump
        scene.Update(0.016f);
        CHECK(an->CurrentClip() == "jump");
    }

    TEST_MAIN_RESULT();
}

#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("script_components");

    // --- set_text updates a sibling TextRenderer (e.g. a score HUD) ---
    {
        Scene scene("HUD");
        GameObject* go = scene.CreateGameObject("Score");
        auto* tr = go->AddComponent<TextRenderer>();
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "var n = 0;\n"
            "function update(d) { n = n + 1; set_text(\"Score: \" + n); }\n"));
        scene.Start();
        scene.Update(0.016f);
        CHECK(tr->text == "Score: 1");
        scene.Update(0.016f);
        CHECK(tr->text == "Score: 2");
    }

    // --- set_color tints a sibling SpriteRenderer ---
    {
        Scene scene("Flash");
        GameObject* go = scene.CreateGameObject("Box");
        auto* sr = go->AddComponent<SpriteRenderer>();
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource("function start() { set_color(1, 0, 0, 1); }"));
        scene.Start();
        CHECK_NEAR(sr->color.r, 1.0f, 0.001f);
        CHECK_NEAR(sr->color.g, 0.0f, 0.001f);
    }

    // --- set_texture and play_sound reach their sibling components ---
    {
        Scene scene("FX");
        GameObject* go = scene.CreateGameObject("Thing");
        auto* sr = go->AddComponent<SpriteRenderer>();
        auto* au = go->AddComponent<AudioSource>();
        au->clip = AudioClip::Sine(440.0f, 0.1f);
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "function start() { set_texture(\"hero.png\"); play_sound(); }"));
        scene.Start();
        CHECK(sr->texture == "hero.png");
        CHECK(au->IsPlaying());
    }

    TEST_MAIN_RESULT();
}

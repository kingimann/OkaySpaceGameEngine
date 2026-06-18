#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("script_fx");

    // --- emit() bursts particles; particles_alive() reads the count; off stops ---
    {
        Scene scene("FX");
        GameObject* go = scene.CreateGameObject("Sparks");
        auto* ps = go->AddComponent<ParticleSystem>();
        ps->maxParticles = 50;
        ps->playing = true;
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource(
            "function start() { emit(10); set_x(particles_alive()); particles_on(false); }"));
        scene.Start();
        CHECK_NEAR(go->transform->localPosition.x, 10.0f, 0.001f); // 10 emitted & alive
        CHECK(ps->playing == false);                               // particles_on(false)
    }

    // --- play_anim() / stop_anim() drive a sibling SpriteAnimator ---
    {
        Scene scene("Anim");
        GameObject* go = scene.CreateGameObject("Hero");
        auto* an = go->AddComponent<SpriteAnimator>();
        an->frames = {"a.png", "b.png"};
        an->playing = true;
        auto* sc = go->AddComponent<ScriptComponent>("okayscript");
        CHECK(sc->LoadSource("function stopit() { stop_anim(); }\n"
                             "function goit() { play_anim(); }"));
        scene.Start();
        sc->VM()->CallEvent("stopit");
        CHECK(an->playing == false);
        sc->VM()->CallEvent("goit");
        CHECK(an->playing == true);     // Restart() re-enables playback
    }

    TEST_MAIN_RESULT();
}

#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

// OkayScript can drive a sibling Character's custom clips: load + play by name,
// query/stop, and set the built-in anim index — the script-facing half of the
// easy-animation feature.
int main() {
    RUN_SUITE("script_anim");

    Scene scene("Anim"); scene.physicsEnabled = false;
    GameObject* go = scene.CreateGameObject("Hero");
    Character* ch = go->AddComponent<Character>();
    int loaded = ch->LoadClips("clip wave loop\nkey 0\n  r_uparm 0 0 0\nkey 1\n  r_uparm 0 0 -90\n");
    CHECK(loaded == 1);
    CHECK(ch->PlayClip("wave"));   // direct C++ path works
    CHECK(ch->IsPlayingClip());
    ch->StopClip();

    auto* sc = go->AddComponent<ScriptComponent>("okayscript");
    sc->LoadSource(
        "var started = 0; var a = 0; var halt = 0;\n"
        "function start() { started = play_clip(\"wave\"); set_anim(3); a = get_anim(); }\n"
        "function update(d) { if (halt == 1) { stop_clip(); } }\n");

    scene.Start();
    // play_clip returned 1, the clip is playing, and set_anim took effect.
    CHECK(ch->IsPlayingClip());
    CHECK(ch->PlayingClip() == "wave");
    CHECK_NEAR(sc->VM()->GetGlobal("started").AsFloat(), 1.0f, 0.001f);
    CHECK_NEAR(sc->VM()->GetGlobal("a").AsFloat(), 3.0f, 0.001f);
    CHECK(ch->anim == 3);

    // Telling the script to halt stops the clip.
    sc->VM()->SetGlobal("halt", vs::VsValue{1.0f});
    scene.Update(0.1f);
    CHECK(!ch->IsPlayingClip());

    TEST_MAIN_RESULT();
}

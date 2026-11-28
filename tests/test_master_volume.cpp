#include "test_framework.hpp"
#include <Okay.hpp>
#include <vector>

using namespace okay;

int main() {
    RUN_SUITE("master_volume");

    // A source playing a constant 1.0 signal.
    Scene scene("A");
    GameObject* go = scene.CreateGameObject("Src");
    auto* src = go->AddComponent<AudioSource>();
    src->clip = AudioClip::Const(1.0f, 64);
    src->volume = 1.0f;
    src->Play();

    std::vector<float> buf(16, 0.0f);

    // --- masterVolume scales the mix ---
    AudioMixer::muted = false;
    AudioMixer::masterVolume = 0.5f;
    AudioMixer::Render(scene, buf.data(), (int)buf.size());
    CHECK_NEAR(buf[0], 0.5f, 0.001f);

    // --- mute silences it ---
    src->Play(); // rewind
    std::fill(buf.begin(), buf.end(), 0.0f);
    AudioMixer::muted = true;
    AudioMixer::Render(scene, buf.data(), (int)buf.size());
    CHECK_NEAR(buf[0], 0.0f, 0.001f);

    // --- full volume passes through (clamped to 1) ---
    src->Play();
    std::fill(buf.begin(), buf.end(), 0.0f);
    AudioMixer::muted = false;
    AudioMixer::masterVolume = 1.0f;
    AudioMixer::Render(scene, buf.data(), (int)buf.size());
    CHECK_NEAR(buf[0], 1.0f, 0.001f);

    // --- OkayScript set_volume keeps the master volume in 0..1 and ignores NaN (#888 F48) ---
    // It wrote the value as given: 5 made the mix five times louder, -1 inverted it, and NaN made every block NaN.
    {
        Scene s("Vol");
        s.physicsEnabled = false;
        auto* sc = s.CreateGameObject("Logic")->AddComponent<ScriptComponent>("okayscript");
        sc->LoadSource(
            "function start() {\n"
            "  set_volume(5);\n"
            "  steam_cloud_write(\"v_high\", volume());\n"
            "  set_volume(0 - 1);\n"
            "  steam_cloud_write(\"v_low\", volume());\n"
            "  set_volume(0.25);\n"
            "  set_volume(sqrt(0 - 1));\n"
            "  steam_cloud_write(\"v_nan\", volume());\n"
            "  set_master_volume(2);\n"
            "  steam_cloud_write(\"v_alias\", volume());\n"
            "}\n");
        s.Start();
        CHECK(Steam::Get().CloudRead("v_high") == "1");
        CHECK(Steam::Get().CloudRead("v_low") == "0");
        CHECK(Steam::Get().CloudRead("v_nan") == "0.25");   // NaN is ignored: the volume stays what it was
        CHECK(Steam::Get().CloudRead("v_alias") == "1");
        CHECK(AudioMixer::masterVolume == 1.0f);
        AudioMixer::masterVolume = 1.0f;
    }

    TEST_MAIN_RESULT();
}

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

    TEST_MAIN_RESULT();
}

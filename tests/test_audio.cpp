#include "test_framework.hpp"
#include <Okay.hpp>

using namespace okay;

int main() {
    RUN_SUITE("audio");

    // Clip generators.
    AudioClip sine = AudioClip::Sine(440.0f, 0.1f);
    CHECK(sine.samples.size() == (size_t)(0.1f * 44100));
    CHECK_NEAR(sine.Duration(), 0.1f, 0.01f);

    // Mixing one source.
    Scene scene("A");
    scene.physicsEnabled = false;
    GameObject* go = scene.CreateGameObject("Speaker");
    auto* src = go->AddComponent<AudioSource>();
    src->clip = AudioClip::Const(0.5f, 10);
    src->volume = 1.0f;
    scene.Start();

    float buf[8] = {0};
    src->Play();
    int mixed = AudioMixer::Render(scene, buf, 5);
    CHECK(mixed == 1);
    for (int i = 0; i < 5; ++i) CHECK_NEAR(buf[i], 0.5f, 1e-4);
    CHECK(src->Playhead() == 5);

    // Past the end (no loop): remaining frames are silent and it stops.
    float buf2[8] = {0};
    AudioMixer::Render(scene, buf2, 8); // playhead 5..9 valid, then stop
    CHECK_NEAR(buf2[0], 0.5f, 1e-4);
    CHECK_NEAR(buf2[4], 0.5f, 1e-4);
    CHECK_NEAR(buf2[5], 0.0f, 1e-4);
    CHECK(!src->IsPlaying());

    // Looping wraps around.
    src->loop = true;
    src->Play();
    float buf3[25] = {0};
    AudioMixer::Render(scene, buf3, 25); // 25 frames over a 10-sample clip
    CHECK_NEAR(buf3[24], 0.5f, 1e-4);
    CHECK(src->IsPlaying());

    // Volume scaling + clamping when two sources sum past 1.
    GameObject* go2 = scene.CreateGameObject("Speaker2");
    auto* src2 = go2->AddComponent<AudioSource>();
    src2->clip = AudioClip::Const(0.8f, 10);
    src->loop = false; src->Play();
    src2->Play();
    float buf4[4] = {0};
    AudioMixer::Render(scene, buf4, 4); // 0.5 + 0.8 = 1.3 -> clamp 1.0
    CHECK_NEAR(buf4[0], 1.0f, 1e-4);

    TEST_MAIN_RESULT();
}

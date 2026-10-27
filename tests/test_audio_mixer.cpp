#include "test_framework.hpp"
#include <Okay.hpp>
#include <cstdio>
#include <fstream>

using namespace okay;

namespace {

void ResetMixer() {
    AudioMixer::sampleRate = 44100;
    AudioMixer::masterVolume = 1.0f;
    AudioMixer::muted = false;
    for (int i = 0; i < (int)AudioGroup::Count; ++i) {
        AudioMixer::groupVolume[i] = 1.0f;
        AudioMixer::groupMuted[i] = false;
    }
    AudioMixer::SetListener(Vec3::Zero);
}

AudioSource* MakeSource(Scene& s, const char* name, float value, int count) {
    auto* a = s.CreateGameObject(name)->AddComponent<AudioSource>();
    a->clip = AudioClip::Const(value, count);
    return a;
}

} // namespace

int main() {
    RUN_SUITE("audio_mixer");

    // Pan: centre is unchanged on both sides; hard left silences the right.
    {
        ResetMixer();
        Scene s("Pan"); s.physicsEnabled = false;
        auto* a = MakeSource(s, "A", 0.5f, 64);
        s.Start();
        float st[8] = {0};
        a->Play();
        AudioMixer::RenderStereo(s, st, 4);
        CHECK_NEAR(st[0], 0.5f, 1e-4); CHECK_NEAR(st[1], 0.5f, 1e-4);

        a->pan = -1.0f; a->Play();
        AudioMixer::RenderStereo(s, st, 4);
        CHECK_NEAR(st[0], 0.5f, 1e-4); CHECK_NEAR(st[1], 0.0f, 1e-4);

        a->pan = 0.5f; a->Play();
        AudioMixer::RenderStereo(s, st, 4);
        CHECK_NEAR(st[0], 0.25f, 1e-4); CHECK_NEAR(st[1], 0.5f, 1e-4);
    }

    // Pitch: 2x steps two samples a frame; 0.5x interpolates between samples.
    {
        ResetMixer();
        Scene s("Pitch"); s.physicsEnabled = false;
        auto* a = s.CreateGameObject("A")->AddComponent<AudioSource>();
        a->clip.samples = {0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f};
        s.Start();
        a->pitch = 2.0f; a->Play();
        float m[3] = {0};
        AudioMixer::Render(s, m, 3);
        CHECK_NEAR(m[0], 0.0f, 1e-4); CHECK_NEAR(m[1], 0.4f, 1e-4); CHECK_NEAR(m[2], 0.8f, 1e-4);
        CHECK(a->Playhead() == 6);

        a->pitch = 0.5f; a->Play();
        AudioMixer::Render(s, m, 3);
        CHECK_NEAR(m[1], 0.1f, 1e-4); CHECK_NEAR(m[2], 0.2f, 1e-4);
    }

    // A clip at half the output rate plays at its own speed (two frames a sample).
    {
        ResetMixer();
        Scene s("Rate"); s.physicsEnabled = false;
        auto* a = s.CreateGameObject("A")->AddComponent<AudioSource>();
        a->clip.samples = {0.0f, 1.0f};
        a->clip.sampleRate = 22050;
        s.Start();
        a->Play();
        float m[3] = {0};
        AudioMixer::Render(s, m, 3);
        CHECK_NEAR(m[1], 0.5f, 1e-4); CHECK_NEAR(m[2], 1.0f, 1e-4);
    }

    // Groups: a group's volume and mute sit under the master.
    {
        ResetMixer();
        Scene s("Groups"); s.physicsEnabled = false;
        auto* music = MakeSource(s, "Music", 0.4f, 64);
        music->group = AudioGroup::Music;
        auto* sfx = MakeSource(s, "Sfx", 0.2f, 64);
        s.Start();
        AudioMixer::groupVolume[(int)AudioGroup::Music] = 0.5f;
        music->Play(); sfx->Play();
        float m[2] = {0};
        AudioMixer::Render(s, m, 2);
        CHECK_NEAR(m[0], 0.4f, 1e-4);   // 0.4*0.5 + 0.2

        AudioMixer::groupMuted[(int)AudioGroup::SFX] = true;
        music->Play(); sfx->Play();
        AudioMixer::Render(s, m, 2);
        CHECK_NEAR(m[0], 0.2f, 1e-4);

        AudioGroup g;
        CHECK(AudioMixer::GroupFromName("MUSIC", g) && g == AudioGroup::Music);
        CHECK(AudioMixer::GroupFromName("voice", g) && g == AudioGroup::Voice);
        CHECK(!AudioMixer::GroupFromName("drums", g));
    }

    // One-shots overlap on one source, run out on their own, and leave the
    // main clip alone.
    {
        ResetMixer();
        Scene s("Shots"); s.physicsEnabled = false;
        auto* a = MakeSource(s, "A", 0.1f, 4);
        s.Start();
        a->PlayOneShot(AudioClip::Const(0.2f, 3));
        a->PlayOneShot(AudioClip::Const(0.3f, 3), 0.5f);
        CHECK(a->ActiveOneShots() == 2);
        CHECK(!a->IsPlaying());
        float m[4] = {0};
        int n = AudioMixer::Render(s, m, 4);
        CHECK(n == 1);
        CHECK_NEAR(m[0], 0.35f, 1e-4);  // 0.2 + 0.3*0.5
        CHECK_NEAR(m[3], 0.0f, 1e-4);
        CHECK(a->ActiveOneShots() == 0);

        for (int i = 0; i < 40; ++i) a->PlayOneShot();
        CHECK(a->ActiveOneShots() == AudioSource::kMaxOneShots);
        a->Stop();
        CHECK(a->ActiveOneShots() == 0);
    }

    // Rolloff curves: all full at min and silent at max; inverse is half at 2x min.
    {
        AudioSource a;
        a.minDistance = 2.0f; a.maxDistance = 40.0f;
        for (int r = 0; r <= (int)AudioRolloff::Logarithmic; ++r) {
            a.rolloff = (AudioRolloff)r;
            CHECK_NEAR(AudioMixer::Attenuation(a, 1.0f), 1.0f, 1e-5);
            CHECK_NEAR(AudioMixer::Attenuation(a, 40.0f), 0.0f, 1e-5);
            const float mid = AudioMixer::Attenuation(a, 10.0f);
            CHECK(mid > 0.0f && mid < 1.0f);
        }
        a.rolloff = AudioRolloff::Inverse;
        CHECK_NEAR(AudioMixer::Attenuation(a, 4.0f), 0.5f, 1e-4);
        a.rolloff = AudioRolloff::Logarithmic;
        const float lg = AudioMixer::Attenuation(a, 10.0f);
        a.rolloff = AudioRolloff::Linear;
        CHECK(lg < AudioMixer::Attenuation(a, 10.0f));   // log drops faster up close
    }

    // 3D pan: a source to the listener's right is louder on the right.
    {
        ResetMixer();
        Scene s("Spatial"); s.physicsEnabled = false;
        auto* a = MakeSource(s, "A", 1.0f, 64);
        a->spatial = true; a->minDistance = 10.0f; a->maxDistance = 20.0f;
        s.Start();
        a->transform->localPosition = {3, 0, 0};
        AudioMixer::SetListener({0, 0, 0}, {1, 0, 0});
        float st[4] = {0};
        a->Play();
        AudioMixer::RenderStereo(s, st, 2);
        CHECK_NEAR(st[1], 1.0f, 1e-4);
        CHECK_NEAR(st[0], 1.0f - AudioMixer::kSpatialPan, 1e-4);   // far ear still hears it

        a->transform->localPosition = {0, 0, 3};   // straight ahead -> centred
        a->Play();
        AudioMixer::RenderStereo(s, st, 2);
        CHECK_NEAR(st[0], 1.0f, 1e-4); CHECK_NEAR(st[1], 1.0f, 1e-4);

        AudioMixer::SetListener({0, 0, 0});      // no facing -> no 3D pan
        a->transform->localPosition = {3, 0, 0};
        a->Play();
        AudioMixer::RenderStereo(s, st, 2);
        CHECK_NEAR(st[0], 1.0f, 1e-4);
    }

    // The new fields survive a save and load; an old line without them loads
    // with the defaults.
    {
        ResetMixer();
        Scene s("Save"); s.physicsEnabled = false;
        auto* a = s.CreateGameObject("Speaker")->AddComponent<AudioSource>();
        a->pitch = 1.5f; a->pan = -0.25f; a->group = AudioGroup::Voice;
        a->spatial = true; a->rolloff = AudioRolloff::Logarithmic;
        const std::string path = "test_audio_mixer.okayscene";
        CHECK(SceneSerializer::SaveToFile(s, path));
        Scene back("Back");
        CHECK(SceneSerializer::LoadFromFile(back, path));
        GameObject* sp = back.Find("Speaker");
        auto* b = sp ? sp->GetComponent<AudioSource>() : nullptr;
        CHECK(b != nullptr);
        if (b) {
            CHECK_NEAR(b->pitch, 1.5f, 1e-4);
            CHECK_NEAR(b->pan, -0.25f, 1e-4);
            CHECK(b->group == AudioGroup::Voice);
            CHECK(b->rolloff == AudioRolloff::Logarithmic);
        }
        std::remove(path.c_str());
    }

    ResetMixer();
    TEST_MAIN_RESULT();
}

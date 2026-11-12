// Stereo clips: channels survive loading, and the mixer places them correctly
// (2D stereo: pan is a balance; 3D: averaged to mono, then panned).
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
    AudioMixer::focusMuted = false;
    for (int i = 0; i < (int)AudioGroup::Count; ++i) {
        AudioMixer::groupVolume[i] = 1.0f;
        AudioMixer::groupMuted[i] = false;
    }
    AudioMixer::duckMusicUnderVoice = false;
    AudioMixer::SetListener(Vec3::Zero);
}

AudioClip StereoConst(float l, float r, int frames, int rate = 44100) {
    return AudioClip::FromChannels(std::vector<float>((std::size_t)frames, l), std::vector<float>((std::size_t)frames, r), rate);
}

AudioSource* Make(Scene& s, const AudioClip& c) {
    auto* a = s.CreateGameObject("A")->AddComponent<AudioSource>();
    a->clip = c;
    return a;
}

// First frame of the next RenderStereo.
void First(Scene& s, AudioSource* a, float& l, float& r) {
    float st[8] = {0};
    a->Play();
    AudioMixer::RenderStereo(s, st, 4);
    l = st[0]; r = st[1];
}

} // namespace

int main() {
    RUN_SUITE("audio_stereo");

    // Clip basics: channels, frame count, duration, downmix, resample, WAV round trip.
    {
        AudioClip st = StereoConst(0.5f, -0.25f, 1000, 1000);
        CHECK(st.channels == 2 && st.samples.size() == 2000 && st.FrameCount() == 1000);
        CHECK_NEAR(st.Duration(), 1.0f, 1e-6);
        AudioClip mono = st.Downmixed();
        CHECK(mono.channels == 1 && mono.FrameCount() == 1000);
        CHECK_NEAR(mono.samples[10], 0.125f, 1e-6);
        CHECK(AudioClip::Sine(440, 0.1f).Downmixed().samples.size() == AudioClip::Sine(440, 0.1f).samples.size());

        AudioClip half = st.Resampled(500);
        CHECK(half.channels == 2 && half.FrameCount() == 500);
        CHECK_NEAR(half.samples[2 * 100], 0.5f, 1e-5);
        CHECK_NEAR(half.samples[2 * 100 + 1], -0.25f, 1e-5);

        const std::string wav = "test_audio_stereo_rt.wav";
        CHECK(st.SaveWAV(wav));
        AudioClip back;
        CHECK(back.LoadWAV(wav));
        CHECK(back.channels == 2 && back.FrameCount() == 1000 && back.sampleRate == 1000);
        CHECK_NEAR(back.samples[0], 0.5f, 0.001);
        CHECK_NEAR(back.samples[1], -0.25f, 0.001);
        AudioClip down;
        CHECK(down.LoadWAV(wav, nullptr, true));
        CHECK(down.channels == 1 && down.FrameCount() == 1000);
        CHECK_NEAR(down.samples[5], 0.125f, 0.001);
        auto cached = AudioClip::LoadCached(wav, 0);
        auto cachedMono = AudioClip::LoadCached(wav, 0, nullptr, true);
        CHECK(cached && cachedMono && cached->channels == 2 && cachedMono->channels == 1);
        std::remove(wav.c_str());
    }

    // 2D stereo: left and right are kept, the centre leaves both alone.
    {
        ResetMixer();
        Scene s("Place"); s.physicsEnabled = false;
        auto* a = Make(s, StereoConst(0.5f, 0.0f, 64));      // left-only clip
        s.Start();
        float l, r;
        First(s, a, l, r);
        CHECK_NEAR(l, 0.5f, 1e-5); CHECK_NEAR(r, 0.0f, 1e-7);        // stays left

        a->clip = StereoConst(0.0f, 0.4f, 64);                        // right-only clip
        First(s, a, l, r);
        CHECK_NEAR(l, 0.0f, 1e-7); CHECK_NEAR(r, 0.4f, 1e-5);

        a->clip = StereoConst(0.6f, 0.2f, 64);                        // both channels differ
        First(s, a, l, r);
        CHECK_NEAR(l, 0.6f, 1e-5); CHECK_NEAR(r, 0.2f, 1e-5);

        // Balance: pan right turns the LEFT channel down, the right stays.
        a->pan = 0.5f;
        First(s, a, l, r);
        CHECK_NEAR(l, 0.3f, 1e-5); CHECK_NEAR(r, 0.2f, 1e-5);
        a->pan = 1.0f;
        First(s, a, l, r);
        CHECK_NEAR(l, 0.0f, 1e-6); CHECK_NEAR(r, 0.2f, 1e-5);
        a->pan = -0.5f;
        First(s, a, l, r);
        CHECK_NEAR(l, 0.6f, 1e-5); CHECK_NEAR(r, 0.1f, 1e-5);
        a->pan = -1.0f;
        First(s, a, l, r);
        CHECK_NEAR(l, 0.6f, 1e-5); CHECK_NEAR(r, 0.0f, 1e-6);

        // A left-only clip panned hard left is still full left; panned right it is silent.
        a->clip = StereoConst(0.5f, 0.0f, 64);
        a->pan = -1.0f; First(s, a, l, r);
        CHECK_NEAR(l, 0.5f, 1e-5); CHECK_NEAR(r, 0.0f, 1e-7);
        a->pan = 1.0f; First(s, a, l, r);
        CHECK_NEAR(l, 0.0f, 1e-6); CHECK_NEAR(r, 0.0f, 1e-7);

        // Volume, master and group levels scale both channels.
        a->pan = 0.0f; a->clip = StereoConst(0.6f, 0.2f, 64); a->volume = 0.5f;
        AudioMixer::masterVolume = 0.5f;
        First(s, a, l, r);
        CHECK_NEAR(l, 0.15f, 1e-5); CHECK_NEAR(r, 0.05f, 1e-5);
    }

    // A mono clip still comes out unchanged and centred on both sides (as before).
    {
        ResetMixer();
        Scene s("Mono"); s.physicsEnabled = false;
        auto* a = Make(s, AudioClip::Const(0.5f, 64));
        s.Start();
        float l, r;
        First(s, a, l, r);
        CHECK_NEAR(l, 0.5f, 1e-6); CHECK_NEAR(r, 0.5f, 1e-6);
        a->pan = -1.0f; First(s, a, l, r);
        CHECK_NEAR(l, 0.5f, 1e-6); CHECK_NEAR(r, 0.0f, 1e-7);
        // Render() is the mono mix: the two channels averaged.
        a->pan = 0.0f; a->clip = StereoConst(0.8f, 0.0f, 64);
        a->Play();
        float m[4] = {0};
        AudioMixer::Render(s, m, 4);
        CHECK_NEAR(m[0], 0.4f, 1e-5);
    }

    // 3D: a stereo clip is averaged to mono first, then placed by distance and direction.
    {
        ResetMixer();
        Scene s("Spatial"); s.physicsEnabled = false;
        auto* a = Make(s, StereoConst(0.6f, 0.0f, 64));
        a->spatial = true; a->minDistance = 1.0f; a->maxDistance = 100.0f; a->rolloff = AudioRolloff::Linear;
        a->transform->SetPosition(Vec3(0, 0, 10));
        s.Start();
        AudioMixer::SetListener(Vec3::Zero, Vec3(1, 0, 0));
        float l, r;
        First(s, a, l, r);                       // straight ahead: centred, so both ears get the mono mix
        const float att = AudioMixer::Attenuation(*a, 10.0f);
        CHECK(att > 0.0f && att < 1.0f);
        CHECK_NEAR(l, 0.3f * att, 1e-5); CHECK_NEAR(r, 0.3f * att, 1e-5);

        a->transform->SetPosition(Vec3(10, 0, 0));    // to the right: right louder than left
        First(s, a, l, r);
        const float att2 = AudioMixer::Attenuation(*a, 10.0f);
        CHECK_NEAR(r, 0.3f * att2, 1e-5);
        CHECK_NEAR(l, 0.3f * att2 * (1.0f - AudioMixer::kSpatialPan), 1e-5);
        CHECK(r > l);
        a->transform->SetPosition(Vec3(-10, 0, 0));   // and to the left
        First(s, a, l, r);
        CHECK(l > r);
        AudioMixer::SetListener(Vec3::Zero);
    }

    // Stereo clips resample and pitch per channel: a left-only ramp never leaks into the right.
    {
        ResetMixer();
        AudioMixer::sampleRate = 44100;
        Scene s("Rate"); s.physicsEnabled = false;
        AudioClip c; c.channels = 2; c.sampleRate = 22050;
        for (int i = 0; i < 400; ++i) { c.samples.push_back(i * 0.001f); c.samples.push_back(0.0f); }
        auto* a = Make(s, c);
        a->pitch = 1.5f;
        s.Start();
        a->Play();
        std::vector<float> st(2 * 100);
        AudioMixer::RenderStereo(s, st.data(), 100);
        bool rightSilent = true, leftRises = true;
        for (int i = 0; i < 100; ++i) {
            if (st[2 * i + 1] != 0.0f) rightSilent = false;
            if (i > 0 && st[2 * i] <= st[2 * (i - 1)]) leftRises = false;
        }
        CHECK(rightSilent && leftRises);
        // Step is 22050/44100*1.5 = 0.75 clip frames per output frame.
        CHECK_NEAR(st[2 * 40], 0.030f, 1e-4);
        CHECK_NEAR(a->RemainingSeconds(), (400.0 - 75.0) / 22050.0 / 1.5, 1e-4);
    }

    // One-shots keep their channels too; seeking and the playhead count frames.
    {
        ResetMixer();
        Scene s("Shot"); s.physicsEnabled = false;
        auto* a = s.CreateGameObject("A")->AddComponent<AudioSource>();
        a->PlayOneShot(StereoConst(0.0f, 0.5f, 64), 1.0f);
        s.Start();
        float st[8] = {0};
        AudioMixer::RenderStereo(s, st, 4);
        CHECK_NEAR(st[0], 0.0f, 1e-7); CHECK_NEAR(st[1], 0.5f, 1e-5);

        auto* b = Make(s, StereoConst(0.5f, 0.25f, 44100));
        CHECK_NEAR(b->Duration(), 1.0f, 1e-5);
        b->Seek(0.5f);
        CHECK(b->Playhead() == 22050);
        CHECK_NEAR(b->Time(), 0.5f, 1e-5);
        b->Seek(99.0f);
        CHECK(b->Playhead() == 44100);
        b->Seek(-3.0f);
        CHECK(b->Playhead() == 0);
    }

    // Low-pass filters each channel on its own.
    {
        ResetMixer();
        Scene s("LP"); s.physicsEnabled = false;
        auto* a = Make(s, StereoConst(1.0f, 0.0f, 4096));
        a->lowPass = 500.0f;
        s.Start();
        a->Play();
        std::vector<float> st(2 * 256);
        AudioMixer::RenderStereo(s, st.data(), 256);
        CHECK(st[2 * 255] > 0.5f && st[2 * 255] < 1.0f);   // smoothed up toward 1 on the left
        CHECK(st[2 * 255 + 1] == 0.0f);                    // right stays silent
    }

    TEST_MAIN_RESULT();
}

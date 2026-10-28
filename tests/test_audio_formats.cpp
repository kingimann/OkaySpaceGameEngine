#include "test_framework.hpp"
#include <Okay.hpp>
#include <cmath>
#include <cstdio>
#include <fstream>

using namespace okay;

namespace {

const std::string kData = std::string(OKAY_SOURCE_DIR) + "/tests/data/audio/";

// Rough pitch from upward zero crossings.
float EstimateHz(const AudioClip& c) {
    int ups = 0;
    for (std::size_t i = 1; i < c.samples.size(); ++i)
        if (c.samples[i - 1] < 0.0f && c.samples[i] >= 0.0f) ++ups;
    return c.Duration() > 0.0f ? ups / c.Duration() : 0.0f;
}

float Peak(const AudioClip& c) {
    float p = 0.0f;
    for (float s : c.samples) p = std::max(p, std::fabs(s));
    return p;
}

} // namespace

int main() {
    RUN_SUITE("audio_formats");

    // OGG Vorbis: a 0.25 s, 440 Hz stereo file at 22.05 kHz comes in mono at its own rate.
    {
        AudioClip c;
        std::string err;
        CHECK(c.LoadOGG(kData + "tone_stereo.ogg", &err));
        CHECK(c.sampleRate == 22050);
        CHECK_NEAR(c.Duration(), 0.25f, 0.02f);
        CHECK_NEAR(EstimateHz(c), 440.0f, 15.0f);
        CHECK(Peak(c) > 0.07f && Peak(c) < 0.15f);   // ffmpeg reads this file's peak as -20.9 dB (0.09)
    }

    // MP3: 0.25 s, 440 Hz mono at 44.1 kHz (encoder padding allowed).
    {
        AudioClip c;
        std::string err;
        CHECK(c.LoadMP3(kData + "tone_mono.mp3", &err));
        CHECK(c.sampleRate == 44100);
        CHECK(c.Duration() > 0.22f && c.Duration() < 0.35f);
        CHECK_NEAR(EstimateHz(c), 440.0f, 30.0f);
        CHECK(Peak(c) > 0.1f && Peak(c) < 0.15f);
    }

    // Load picks the decoder by extension; unknown and broken files fail cleanly.
    {
        AudioClip a, b;
        CHECK(a.Load(kData + "tone_stereo.ogg") && a.sampleRate == 22050);
        CHECK(b.Load(kData + "tone_mono.mp3") && b.sampleRate == 44100);
        CHECK(AudioClip::IsAudioFile("x/Theme.OGG"));
        CHECK(AudioClip::IsAudioFile("step.mp3") && AudioClip::IsAudioFile("a.wav"));
        CHECK(!AudioClip::IsAudioFile("notes.txt"));

        const std::string junk = "test_audio_formats_junk.ogg";
        { std::ofstream f(junk, std::ios::binary); f << "this is not audio"; }
        AudioClip j;
        std::string err;
        CHECK(!j.Load(junk, &err));
        CHECK(!err.empty());
        CHECK(j.samples.empty());
        CHECK(!j.Load("does/not/exist.mp3"));
        std::remove(junk.c_str());
    }

    // WAV still loads through Load (round trip).
    {
        const std::string wav = "test_audio_formats.wav";
        CHECK(AudioClip::Sine(220.0f, 0.1f).SaveWAV(wav));
        AudioClip c;
        CHECK(c.Load(wav));
        CHECK_NEAR(c.Duration(), 0.1f, 0.01f);
        std::remove(wav.c_str());
    }

    // The cache decodes a file once, per output rate.
    {
        auto a = AudioClip::LoadCached(kData + "tone_stereo.ogg", 44100);
        auto b = AudioClip::LoadCached(kData + "tone_stereo.ogg", 44100);
        CHECK(a != nullptr);
        CHECK(a.get() == b.get());
        if (a) {
            CHECK(a->sampleRate == 44100);
            CHECK_NEAR(a->Duration(), 0.25f, 0.02f);
        }
        auto native = AudioClip::LoadCached(kData + "tone_stereo.ogg");
        CHECK(native && native.get() != a.get() && native->sampleRate == 22050);
        CHECK(AudioClip::LoadCached("missing.ogg", 44100) == nullptr);
    }

    // LoadSceneAudio fills every source that names a file, relative to a base dir.
    {
        Scene s("Load"); s.physicsEnabled = false;
        auto* music = s.CreateGameObject("Music")->AddComponent<AudioSource>();
        music->clipPath = "tone_stereo.ogg";
        auto* step = s.CreateGameObject("Step")->AddComponent<AudioSource>();
        step->clipPath = "tone_mono.mp3";
        auto* none = s.CreateGameObject("None")->AddComponent<AudioSource>();
        auto* bad = s.CreateGameObject("Bad")->AddComponent<AudioSource>();
        bad->clipPath = "nope.ogg";
        std::vector<std::string> failed;
        CHECK(LoadSceneAudio(s, kData, 44100, &failed) == 2);
        CHECK(failed.size() == 1 && failed[0] == "nope.ogg");
        CHECK(music->clip.sampleRate == 44100 && !music->clip.samples.empty());
        CHECK(!step->clip.samples.empty());
        CHECK(none->clip.samples.empty());

        // And it plays through the mixer.
        s.Start();
        music->Play();
        float buf[512] = {0};
        CHECK(AudioMixer::Render(s, buf, 512) == 1);
        float peak = 0.0f;
        for (float v : buf) peak = std::max(peak, std::fabs(v));
        CHECK(peak > 0.05f);
    }

    TEST_MAIN_RESULT();
}

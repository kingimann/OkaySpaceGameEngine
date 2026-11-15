// The IMA ADPCM sample shipped for the own WAV decoder (examples/own_wav/ima_tone.wav, made by scripts/gen_own_wav_samples.py; the
// scene examples/own_wav/own_wav_stream_test.okayscene plays it through the STREAMING path, which docs/try_it.md section 9 tells
// the owner to try). It must decode to the 440 Hz tone the docs promise, the streamed decode must equal the whole-file decode, the
// stream must open with OKAY_OWN_WAV=1, and with OKAY_OWN_WAV=0 the old streaming reader must refuse it (that refusal is what makes
// the sample tell the two apart). Also checks that the scene names the file and sets the Stream flag.
#include "test_framework.hpp"
#include "okay/Audio/AudioStream.hpp"
#include "okay/Audio/WavDecoder.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace okay;

#ifndef OKAY_SOURCE_DIR
#error "OKAY_SOURCE_DIR is not defined"
#endif

static void SetEnv(const char* v) {
#ifdef _WIN32
    _putenv_s("OKAY_OWN_WAV", v ? v : "");
#else
    if (v) setenv("OKAY_OWN_WAV", v, 1); else unsetenv("OKAY_OWN_WAV");
#endif
}

int main() {
    RUN_SUITE("own_wav_sample");
    const std::string dir = std::string(OKAY_SOURCE_DIR) + "/examples/own_wav/";
    std::ifstream f(dir + "ima_tone.wav", std::ios::binary);
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    CHECK(bytes.size() > 40000);

    // the whole-file decode: mono, 22,050 Hz, IMA ADPCM, 175 blocks of 505 frames, a 440 Hz sine of amplitude 0.5
    wav::WavAudio a; std::string err;
    CHECK(wav::DecodeWav(bytes.data(), bytes.size(), a, &err));
    if (!err.empty()) std::fprintf(stderr, "  decode: %s\n", err.c_str());
    CHECK(a.channels == 1 && a.sampleRate == 22050 && a.encoding == wav::Encoding::ImaAdpcm && a.bitsPerSample == 4);
    CHECK(a.FrameCount() == 175u * 505u);
    float peak = 0, worst = 0;
    for (std::size_t n = 0; n < a.FrameCount(); ++n) {
        const float want = 0.5f * (float)std::sin(2.0 * 3.14159265358979323846 * 440.0 * (double)n / 22050.0);
        peak = std::fmax(peak, std::fabs(a.samples[n]));
        worst = std::fmax(worst, std::fabs(a.samples[n] - want));
    }
    CHECK(peak > 0.48f && peak < 0.53f);
    CHECK(worst < 0.02f);   // 4-bit ADPCM noise on a loud sine: about 0.01 measured against Python's audioop

    // the streamed decode (what the editor does for an AudioSource with Stream on) equals it, sample for sample
    SetEnv("1");
    CHECK(wav::OwnWavEnabled());
    {
        std::string e2;
        auto st = AudioStream::OpenFile(dir + "ima_tone.wav", &e2);
        if (!st) std::fprintf(stderr, "  stream: %s\n", e2.c_str());
        CHECK(st != nullptr);
        if (st) {
            CHECK(st->Channels() == 1 && st->SampleRate() == 22050 && st->TotalFrames() == (std::int64_t)a.FrameCount());
            std::vector<float> got(a.FrameCount() + 1000);
            std::size_t n = 0, r;
            while (n < got.size() && (r = st->Read(got.data() + n, got.size() - n)) > 0) n += r;
            CHECK(n == a.FrameCount());
            bool same = true;
            for (std::size_t i = 0; i < n && i < a.samples.size(); ++i) if (got[i] != a.samples[i]) { same = false; break; }
            CHECK(same);
        }
    }
    // the old streaming reader cannot read ADPCM: the stream does not open (this is the difference the owner hears: silence)
    SetEnv("0");
    CHECK(!wav::OwnWavEnabled());
    {
        std::string e3;
        auto st = AudioStream::OpenFile(dir + "ima_tone.wav", &e3);
        CHECK(st == nullptr && !e3.empty());
    }
    SetEnv(nullptr);

    // the scene plays this file with the Stream flag on (the 12th number of the audio line)
    std::ifstream sc(dir + "own_wav_stream_test.okayscene");
    const std::string scene((std::istreambuf_iterator<char>(sc)), std::istreambuf_iterator<char>());
    const std::size_t at = scene.find("audio \"ima_tone.wav\" 0.7 1 1 1 2 30 1 0 0 0 22000 1 ");
    CHECK(at != std::string::npos);
    TEST_MAIN_RESULT();
}

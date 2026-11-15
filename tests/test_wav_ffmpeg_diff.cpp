// The own WAV decoder (okay::wav::DecodeWav) against an outside reference: files encoded by ffmpeg from our own
// synthetic signal, and ffmpeg's own decode of each (tests/data/audio/wav_ffmpeg/, made by
// scripts/gen_wav_ffmpeg_fixtures.py with ffmpeg 6.1.1). ffmpeg is used as a program only; none of its code was read.
// Every coding must give the same 16-bit samples as ffmpeg, bit for bit, except IMA ADPCM: there our decoder follows
// the IMA recommended practice's shift-and-add step (and matches Python's audioop, see own_wav), while ffmpeg's
// output differs by a few steps, so IMA is held to a small bound instead.
#include "test_framework.hpp"
#include "okay/Audio/WavDecoder.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace okay;

namespace {
const std::string kDir = std::string(OKAY_SOURCE_DIR) + "/tests/data/audio/wav_ffmpeg/";

std::vector<unsigned char> Slurp(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<unsigned char>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

struct Case { const char* name; int channels; wav::Encoding enc; int maxDiff; };
} // namespace

int main() {
    RUN_SUITE("wav_ffmpeg_diff");
    const Case cases[] = {
        {"ms_mono", 1, wav::Encoding::MsAdpcm, 0},
        {"ms_mono_b256", 1, wav::Encoding::MsAdpcm, 0},
        {"ms_stereo", 2, wav::Encoding::MsAdpcm, 0},
        {"ms_stereo_b2048", 2, wav::Encoding::MsAdpcm, 0},
        {"ima_mono", 1, wav::Encoding::ImaAdpcm, 32},
        {"ima_stereo", 2, wav::Encoding::ImaAdpcm, 32},
        {"alaw_stereo", 2, wav::Encoding::ALaw, 0},
        {"mulaw_mono", 1, wav::Encoding::MuLaw, 0},
        {"u8_mono", 1, wav::Encoding::Pcm, 0},
        {"s24_stereo", 2, wav::Encoding::Pcm, 0},
        {"f32_mono", 1, wav::Encoding::Float, 0},
    };
    for (const Case& c : cases) {
        const std::vector<unsigned char> file = Slurp(kDir + c.name + ".wav");
        const std::vector<unsigned char> ref = Slurp(kDir + c.name + ".s16");
        CHECK(!file.empty() && !ref.empty());
        if (file.empty() || ref.empty()) { std::printf("  %s: fixture missing\n", c.name); continue; }
        wav::WavAudio a; std::string err;
        const bool ok = wav::DecodeWav(file.data(), file.size(), a, &err);
        CHECK(ok);
        if (!ok) { std::printf("  %s: %s\n", c.name, err.c_str()); continue; }
        CHECK(a.channels == c.channels);
        CHECK(a.sampleRate == 11025);
        CHECK(a.encoding == c.enc);
        const std::vector<std::int16_t> ours = wav::ToS16(a);
        const std::size_t n = ref.size() / 2;
        CHECK(ours.size() == n);
        int maxDiff = 0; std::size_t differ = 0;
        for (std::size_t i = 0; i < n && i < ours.size(); ++i) {
            const int r = (int)(std::int16_t)(ref[2 * i] | (ref[2 * i + 1] << 8));
            const int d = std::abs(r - (int)ours[i]);
            if (d) ++differ;
            if (d > maxDiff) maxDiff = d;
        }
        std::printf("  %-16s %zu samples, %zu differ, largest difference %d (allowed %d)\n", c.name, n, differ, maxDiff, c.maxDiff);
        CHECK(maxDiff <= c.maxDiff);
        CHECK(a.warnings.empty());
    }
    TEST_MAIN_RESULT();
}

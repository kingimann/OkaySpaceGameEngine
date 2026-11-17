// The WAV default switch in both build-time settings. Built twice from the decoder's own two sources (no engine link):
//   test_own_wav_default_on   as a normal build: our decoder is the default (owner's approval, 2026-11-16);
//   test_own_wav_default_off  with OKAY_OWN_WAV_DEFAULT_OFF, what -DOKAY_OWN_WAV=OFF sets: the old path is the default
//                             for this one release.
// In both, OKAY_OWN_WAV=0 picks the old path, OKAY_OWN_WAV=1 ours, and any other value (or none) the build default.
#include "test_framework.hpp"
#include "okay/Audio/WavDecoder.hpp"
#include <cstdlib>
#include <string>
#include <vector>

#ifndef OKAY_EXPECT_OWN_WAV_DEFAULT
#error "OKAY_EXPECT_OWN_WAV_DEFAULT must be 0 or 1"
#endif

using namespace okay;

static void Set(const char* v) {
#ifdef _WIN32
    _putenv_s("OKAY_OWN_WAV", v ? v : "");
#else
    if (v) setenv("OKAY_OWN_WAV", v, 1); else unsetenv("OKAY_OWN_WAV");
#endif
}

int main() {
    const bool def = OKAY_EXPECT_OWN_WAV_DEFAULT != 0;
    Set(nullptr);   CHECK(wav::OwnWavEnabled() == def);
    Set("0");       CHECK(!wav::OwnWavEnabled());
    Set("1");       CHECK(wav::OwnWavEnabled());
    Set("");        CHECK(wav::OwnWavEnabled() == def);
    Set("true");    CHECK(wav::OwnWavEnabled() == def);   // only the first character, and only 0 or 1, counts
    Set("10");      CHECK(wav::OwnWavEnabled());
    Set(nullptr);   CHECK(wav::OwnWavEnabled() == def);
    // The decoder itself works the same in either build: one 16-bit mono frame, 0x4000 = 0.5.
    const std::vector<unsigned char> w = {'R', 'I', 'F', 'F', 38, 0, 0, 0, 'W', 'A', 'V', 'E',
                                          'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0, 0x40, 0x1F, 0, 0, 0x80, 0x3E, 0, 0, 2, 0, 16, 0,
                                          'd', 'a', 't', 'a', 2, 0, 0, 0, 0x00, 0x40};
    wav::WavAudio a; std::string e;
    CHECK(wav::DecodeWav(w.data(), w.size(), a, &e) && a.samples.size() == 1 && a.samples[0] == 0.5f && a.sampleRate == 8000);
    TEST_MAIN_RESULT();
}

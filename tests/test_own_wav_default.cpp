// Compile-time default for the own WAV decoder: when the build was configured with -DOKAY_OWN_WAV=ON (tests/CMakeLists.txt then defines
// OKAY_EXPECT_OWN_WAV for this test), the decoder must be on with nothing set at run time, for both whole-file loads and streamed playback.
// This fails if the engine did not get OKAY_OWN_WAV_DEFAULT (for example the option dropped from engine/CMakeLists.txt by a merge).
// With the option OFF it checks that the default did not turn on by accident (the old readers cannot read the IMA ADPCM sample, so
// the stream must not open). Same pattern as tests/test_own_ter_default.cpp and tests/test_own_e4_default.cpp.
#include "test_framework.hpp"
#include "okay/Audio/AudioStream.hpp"
#include "okay/Audio/WavDecoder.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace okay;

#ifndef OKAY_SOURCE_DIR
#error "OKAY_SOURCE_DIR is not defined"
#endif

int main() {
    RUN_SUITE("own_wav_default");
#ifdef _WIN32
    _putenv_s("OKAY_OWN_WAV", "");
#else
    unsetenv("OKAY_OWN_WAV");
#endif
    const std::string sample = std::string(OKAY_SOURCE_DIR) + "/examples/own_wav/ima_tone.wav";
    std::string err;
    auto st = AudioStream::OpenFile(sample, &err);
#ifdef OKAY_EXPECT_OWN_WAV
    CHECK(wav::OwnWavEnabled());
    if (!st) std::fprintf(stderr, "  stream: %s\n", err.c_str());
    CHECK(st != nullptr);   // streamed IMA ADPCM: only our decoder reads it
#else
    CHECK(!wav::OwnWavEnabled());   // default OFF: the old readers are used unless asked
    CHECK(st == nullptr);
#endif
    TEST_MAIN_RESULT();
}

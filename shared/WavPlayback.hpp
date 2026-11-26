#pragma once
// The one place the launcher (and anything else) loads a WAV for playback, in the form okay::platform's audio device takes
// (okay::platform::AudioSpec: rate, channels, S16 or F32). The file is read by our own decoder, okay::wav (engine), and
// converted by our own code (okay/Audio/WavPlayback.hpp): float32 stays float, everything else is 16-bit native-endian.
// SDL_LoadWAV was deleted; tests/check_wav_seam_wiring.cmake keeps it out. No SDL type is used here any more.
#include "okay/Audio/WavPlayback.hpp"
#include "okay/Platform/Platform.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace okay {
namespace wav {

/// Fills `spec` (freq, channels, format; the other fields keep their defaults) and `samples` with the clip's bytes.
/// False on failure (and sets *error); `samples` is then empty.
inline bool LoadWavClip(const std::string& path, okay::platform::AudioSpec& spec, std::vector<std::uint8_t>& samples,
                        std::string* error = nullptr) {
    spec = okay::platform::AudioSpec{};
    samples.clear();
    PlaybackClip c;
    if (!LoadForPlayback(path, c, error)) return false;
    spec.freq = c.sampleRate;
    spec.channels = c.channels;
    spec.format = c.format == PlaybackFormat::F32 ? okay::platform::SampleFormat::F32 : okay::platform::SampleFormat::S16;
    samples.assign(c.bytes.begin(), c.bytes.end());
    return true;
}

} // namespace wav
} // namespace okay

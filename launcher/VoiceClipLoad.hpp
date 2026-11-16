#pragma once
// The launcher's voice-clip reading, with no SDL: a WAV file read by our own decoder (okay::wav, engine/src/Audio/
// WavDecoder.cpp and WavPlayback.cpp, written from the RIFF / WAVE format documents) and turned into what the
// launcher's audio device (okay::platform::AudioDevice) plays: native-endian signed 16-bit, or 32-bit float for float
// files, with the file's channel count and rate. It replaces SDL_LoadWAV plus the launcher's own "convert to 16-bit"
// pass (docs/own_code_roadmap.md, SDL2 row, stage A). Tested by tests/test_launcher_wav_window.cpp against SDL_LoadWAV.
// Header-only on purpose: the test includes it without the launcher's main.cpp.
#include "okay/Audio/WavPlayback.hpp"
#include "okay/Platform/Platform.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

namespace okay {
namespace launcher {

/// Reads the WAV at `path` into `out` (interleaved samples ready for the device). False (and *error) when the file is
/// missing, empty, damaged, has no samples or is over the decoder's budgets; `out` is then empty.
inline bool LoadVoiceClipFile(const std::string& path, wav::PlaybackClip& out, std::string* error = nullptr) {
    return wav::LoadForPlayback(path, out, error);
}

/// The device sample format for a clip.
inline platform::SampleFormat VoiceDeviceFormat(const wav::PlaybackClip& c) {
    return c.format == wav::PlaybackFormat::F32 ? platform::SampleFormat::F32 : platform::SampleFormat::S16;
}

/// Bytes in one frame (one sample for every channel); 0 for an empty clip description.
inline int VoiceFrameBytes(const wav::PlaybackClip& c) {
    return c.channels > 0 ? platform::BytesPerSample(VoiceDeviceFormat(c)) * c.channels : 0;
}

/// The waveform: the loudest sample (0..1, any channel) in each of `n` equal slices of a 16-bit clip. False for a float
/// clip or an empty one, and `bars` is left as it was (the launcher then keeps its placeholder bars).
inline bool VoicePeakBars(const wav::PlaybackClip& c, float* bars, int n) {
    const int fb = VoiceFrameBytes(c);
    if (c.format != wav::PlaybackFormat::S16 || fb <= 0 || !bars || n <= 0) return false;
    const std::size_t frames = c.bytes.size() / std::size_t(fb);
    if (frames == 0) return false;
    for (int b = 0; b < n; ++b) {
        const std::size_t from = frames * std::size_t(b) / std::size_t(n), to = frames * std::size_t(b + 1) / std::size_t(n);
        int peak = 0;
        for (std::size_t f = from; f < to; ++f)
            for (int ch = 0; ch < c.channels; ++ch) {
                std::int16_t v;
                std::memcpy(&v, c.bytes.data() + f * std::size_t(fb) + std::size_t(ch) * 2, 2);
                peak = std::max(peak, std::abs(int(v)));
            }
        bars[b] = float(peak) / 32768.0f;
    }
    return true;
}

} // namespace launcher
} // namespace okay

#pragma once
// The one place the launcher (and anything else) loads a WAV for playback. Include <SDL.h> first (only for the
// SDL_AudioSpec the launcher's audio device takes; the decoding is ours).
// The file is read by our own decoder, okay::wav (engine), and converted by our own code (okay/Audio/WavPlayback.hpp):
// float32 stays float, everything else is 16-bit. SDL_LoadWAV was deleted; tests/check_wav_seam_wiring.cmake keeps it out.
#include "okay/Audio/WavPlayback.hpp"
#include <string>
#include <vector>

namespace okay {
namespace wav {

/// Fills `spec` (format, channels, freq; the callback and samples fields are left zero) and `samples`
/// with the clip's bytes. False on failure (and sets *error).
inline bool LoadWavForSdl(const std::string& path, SDL_AudioSpec& spec, std::vector<Uint8>& samples,
                          std::string* error = nullptr) {
    spec = SDL_AudioSpec{};
    samples.clear();
    PlaybackClip c;
    if (!LoadForPlayback(path, c, error)) return false;
    spec.freq = c.sampleRate;
    spec.channels = static_cast<Uint8>(c.channels);
    spec.format = c.format == PlaybackFormat::F32 ? AUDIO_F32SYS : AUDIO_S16SYS;
    samples.assign(c.bytes.begin(), c.bytes.end());
    return true;
}

} // namespace wav
} // namespace okay

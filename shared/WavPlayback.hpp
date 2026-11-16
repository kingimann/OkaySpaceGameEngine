#pragma once
// The one place the launcher (and anything else) loads a WAV for playback. Include <SDL.h> first.
//   own decoder off (the default): SDL_LoadWAV, exactly as before.
//   own decoder on (OKAY_OWN_WAV CMake option or env OKAY_OWN_WAV=1): okay::wav (engine), converted
//   by our own code (okay/Audio/WavPlayback.hpp): float32 stays float, everything else is 16-bit.
// This is the only file that may name SDL_LoadWAV (tests/check_wav_seam_wiring.cmake).
#include "okay/Audio/WavDecoder.hpp"
#include "okay/Audio/WavPlayback.hpp"
#include <string>
#include <utility>
#include <vector>

namespace okay {
namespace wav {

/// Fills `spec` (format, channels, freq; the callback and samples fields are left zero) and `samples`
/// with the clip's bytes (copied; nothing of SDL's is left to free). False on failure.
inline bool LoadWavForSdl(const std::string& path, SDL_AudioSpec& spec, std::vector<Uint8>& samples,
                          std::string* error = nullptr) {
    spec = SDL_AudioSpec{};
    samples.clear();
    if (OwnWavEnabled()) {
        PlaybackClip c;
        if (!LoadForPlayback(path, c, error)) return false;
        spec.freq = c.sampleRate;
        spec.channels = static_cast<Uint8>(c.channels);
        spec.format = c.format == PlaybackFormat::F32 ? AUDIO_F32SYS : AUDIO_S16SYS;
        samples = std::move(c.bytes);   // no copy: the clip's buffer becomes the caller's
        return true;
    }
    Uint8* buf = nullptr; Uint32 len = 0; SDL_AudioSpec s{};
    if (!SDL_LoadWAV(path.c_str(), &s, &buf, &len)) { if (error) *error = SDL_GetError(); return false; }
    spec = s;
    if (buf && len) samples.assign(buf, buf + len);
    SDL_FreeWAV(buf);
    return true;
}

} // namespace wav
} // namespace okay

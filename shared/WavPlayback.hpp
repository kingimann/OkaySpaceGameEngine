#pragma once
// The one place the launcher (and anything else) loads a WAV for playback. Include PlainTypes.hpp (SDL.h) first.
//   own decoder off (the default): SDL_LoadWAV, exactly as before.
//   own decoder on (OKAY_OWN_WAV CMake option or env OKAY_OWN_WAV=1): okay::wav (engine), converted
//   by our own code (okay/Audio/WavPlayback.hpp): float32 stays float, everything else is 16-bit.
// This is the only file that may name SDL_LoadWAV (tests/check_wav_seam_wiring.cmake).
#include "okay/Audio/WavDecoder.hpp"
#include "okay/Audio/WavPlayback.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace okay {
namespace wav {

/// What a loaded clip's samples look like, with no SDL type: the launcher's voice player reads and converts clips with this.
struct ClipSpec {
    int freq = 0, channels = 0, samples = 0;
    int bits = 16;                       // 8, 16, 24 or 32 bits per sample
    bool isFloat = false, isSigned = true, bigEndian = false;
    /// Native-endian signed 16-bit / 32-bit float (what the platform audio layer plays); little-endian hosts only, which is every host we ship for.
    bool IsS16Native() const { return bits == 16 && !isFloat && isSigned && !bigEndian; }
    bool IsF32Native() const { return bits == 32 && isFloat && !bigEndian; }
    void SetS16Native() { bits = 16; isFloat = false; isSigned = true; bigEndian = false; }
};

/// Our own decoder only (no SDL): fills `spec` and `samples` (the clip's bytes). False on failure; `error` says why.
inline bool LoadWavClip(const std::string& path, ClipSpec& spec, std::vector<std::uint8_t>& samples, std::string* error = nullptr) {
    spec = ClipSpec{};
    samples.clear();
    PlaybackClip c;
    if (!LoadForPlayback(path, c, error)) return false;
    spec.freq = c.sampleRate;
    spec.channels = c.channels;
    if (c.format == PlaybackFormat::F32) { spec.bits = 32; spec.isFloat = true; } else spec.SetS16Native();
    samples.assign(c.bytes.begin(), c.bytes.end());
    return true;
}

#ifndef OKAY_NO_SDL
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
        samples.assign(c.bytes.begin(), c.bytes.end());
        return true;
    }
    Uint8* buf = nullptr; Uint32 len = 0; SDL_AudioSpec s{};
    if (!SDL_LoadWAV(path.c_str(), &s, &buf, &len)) { if (error) *error = SDL_GetError(); return false; }
    spec = s;
    if (buf && len) samples.assign(buf, buf + len);
    SDL_FreeWAV(buf);
    return true;
}
#endif   // !OKAY_NO_SDL (LoadWavForSdl)
#ifndef OKAY_NO_SDL

/// The same as LoadWavClip, but with SDL_LoadWAV unless the own decoder is switched on (OKAY_OWN_WAV): any PCM layout SDL reads comes back as it is.
inline bool LoadWavClipSdlOrOwn(const std::string& path, ClipSpec& spec, std::vector<std::uint8_t>& samples, std::string* error = nullptr) {
    if (OwnWavEnabled()) return LoadWavClip(path, spec, samples, error);
    SDL_AudioSpec s;
    if (!LoadWavForSdl(path, s, samples, error)) return false;
    spec = ClipSpec{};
    spec.freq = s.freq; spec.channels = s.channels; spec.samples = s.samples;
    spec.bits = (int)SDL_AUDIO_BITSIZE(s.format); spec.isFloat = SDL_AUDIO_ISFLOAT(s.format) != 0; spec.isSigned = SDL_AUDIO_ISSIGNED(s.format) != 0; spec.bigEndian = SDL_AUDIO_ISBIGENDIAN(s.format) != 0;
    return true;
}
#endif

/// What the launcher calls: SDL_LoadWAV (own decoder when OKAY_OWN_WAV is on) in a normal build; our own decoder only with OKAY_NO_SDL.
inline bool LoadWavClipAny(const std::string& path, ClipSpec& spec, std::vector<std::uint8_t>& samples, std::string* error = nullptr) {
#ifdef OKAY_NO_SDL
    return LoadWavClip(path, spec, samples, error);
#else
    return LoadWavClipSdlOrOwn(path, spec, samples, error);
#endif
}

} // namespace wav
} // namespace okay

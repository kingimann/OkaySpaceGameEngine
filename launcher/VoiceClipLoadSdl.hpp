#pragma once
// Which reader the launcher uses for a voice clip (docs/own_code_roadmap.md, WAV row; CLAUDE.md rule 17):
//   default (OKAY_OWN_WAV off): SDL_LoadWAV through the WAV seam (shared/WavPlayback.hpp), then the launcher's own pass
//            that turns any layout other than native 16-bit or float into native 16-bit, as the launcher always did;
//   OKAY_OWN_WAV=1 (or the OKAY_OWN_WAV CMake option): our own decoder, launcher/VoiceClipLoad.hpp.
// The switch is okay::wav::OwnWavEnabled(), the same one AudioClip::LoadWAV and shared/WavPlayback.hpp read.
// Include <SDL.h> first. Tested by tests/test_launcher_wav_window.cpp (both paths).
#include "VoiceClipLoad.hpp"
#include "WavPlayback.hpp"   // shared/: okay::wav::LoadWavForSdl, the only place that calls SDL_LoadWAV

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace okay {
namespace launcher {

/// The SDL path: SDL_LoadWAV (via okay::wav::LoadWavForSdl) and the launcher's conversion to native 16-bit for any
/// layout the device cannot take (8-bit, 24/32-bit integer, big-endian). Float32 and native 16-bit are kept as they are.
/// Note that LoadWavForSdl itself follows OKAY_OWN_WAV; call this through LoadVoiceClip unless a test wants it directly.
inline bool LoadVoiceClipSdl(const std::string& path, wav::PlaybackClip& out, std::string* error = nullptr) {
    out = wav::PlaybackClip{};
    SDL_AudioSpec spec{};
    std::vector<Uint8> raw;
    if (!wav::LoadWavForSdl(path, spec, raw, error)) return false;
    const SDL_AudioFormat f = spec.format;
    out.channels = spec.channels;
    out.sampleRate = spec.freq;
    if (f == AUDIO_F32SYS) { out.format = wav::PlaybackFormat::F32; out.bytes.assign(raw.begin(), raw.end()); }
    else if (f == AUDIO_S16SYS) { out.format = wav::PlaybackFormat::S16; out.bytes.assign(raw.begin(), raw.end()); }
    else {
        const int width = SDL_AUDIO_BITSIZE(f) / 8;
        if (width < 1 || width > 4) { if (error) *error = "unsupported sample width"; out = wav::PlaybackClip{}; return false; }
        const std::size_t n = raw.size() / std::size_t(width);
        out.format = wav::PlaybackFormat::S16;
        out.bytes.resize(n * 2);
        for (std::size_t i = 0; i < n; ++i) {
            const Uint8* p = raw.data() + i * std::size_t(width);
            float v = 0.0f;   // -1..1
            if (SDL_AUDIO_ISFLOAT(f)) {
                std::uint32_t u = 0;
                for (int b = 0; b < 4; ++b) u |= std::uint32_t(p[SDL_AUDIO_ISBIGENDIAN(f) ? 3 - b : b]) << (8 * b);
                std::memcpy(&v, &u, 4);
            } else if (width == 1) {
                v = SDL_AUDIO_ISSIGNED(f) ? float(Sint8(p[0])) / 128.0f : (float(p[0]) - 128.0f) / 128.0f;
            } else {
                std::uint32_t u = 0;
                for (int b = 0; b < width; ++b) u |= std::uint32_t(p[SDL_AUDIO_ISBIGENDIAN(f) ? width - 1 - b : b]) << (8 * b);
                const long long top = 1ll << (8 * width - 1);
                const long long sv = SDL_AUDIO_ISSIGNED(f) ? (long long)(Sint32(u << (32 - 8 * width)) >> (32 - 8 * width))
                                                           : (long long)u - top;
                v = float(sv) / float(top);
            }
            const Sint16 s16 = Sint16(std::clamp(v * 32767.0f, -32768.0f, 32767.0f));
            std::memcpy(out.bytes.data() + i * 2, &s16, 2);
        }
    }
    if (out.bytes.empty() || out.channels <= 0 || out.sampleRate <= 0) { if (error) *error = "no samples"; out = wav::PlaybackClip{}; return false; }
    return true;
}

/// What the launcher calls: our own decoder when OKAY_OWN_WAV is on (okay::wav::OwnWavEnabled()), SDL otherwise.
inline bool LoadVoiceClip(const std::string& path, wav::PlaybackClip& out, std::string* error = nullptr) {
    return wav::OwnWavEnabled() ? LoadVoiceClipFile(path, out, error) : LoadVoiceClipSdl(path, out, error);
}

} // namespace launcher
} // namespace okay

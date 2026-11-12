// A decoded voice note for the launcher's chat player, kept free of ImGui so it can be tested
// (tests/test_launcher_voice_clip.cpp). The samples SDL_LoadWAV returns are SDL's allocation and must go back to
// SDL_FreeWAV: a clip that is converted to 16-bit keeps its own copy and gives SDL's buffer back at once; a clip that
// plays as loaded gives it back when the clip is destroyed.
#pragma once
#include <SDL.h>
#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace okay::voice {

static constexpr int kVoiceBars = 30;
struct VoiceClip {
    bool ok = false;                 // decoded (false = unplayable; don't retry)
    Uint8* pcm = nullptr; Uint32 len = 0;   // the samples to play: SDL's buffer (wav) or the converted copy (owned)
    SDL_AudioSpec spec{};
    float secs = 0, bytesPerSec = 1;
    float bars[kVoiceBars] = {};
    Uint8* wav = nullptr;            // SDL_LoadWAV's allocation while this clip still holds it (null once freed)
    void (*freeWav)(Uint8*) = &SDL_FreeWAV;   // a test swaps this to count frees
    std::vector<Uint8> owned;        // the converted samples when the file was not native 16-bit / float
    std::map<int, std::vector<Uint8>> fast;   // time-stretched copies by speed index (built on first use)
    VoiceClip() = default;
    VoiceClip(const VoiceClip&) = delete;
    VoiceClip& operator=(const VoiceClip&) = delete;
    ~VoiceClip() { ReleaseWav(); }
    void ReleaseWav() { if (wav) { freeWav(wav); wav = nullptr; } }
};
// The platform audio layer plays native-endian 16-bit or float samples. A clip in any other
// PCM layout (8-bit, 32-bit integer, big-endian) is converted to 16-bit once, at load.
inline void VoiceClipToPlayable(VoiceClip& c) {
    const SDL_AudioFormat f = c.spec.format;
    if (f == AUDIO_S16SYS || f == AUDIO_F32SYS || !c.pcm || !c.len) return;
    const int width = SDL_AUDIO_BITSIZE(f) / 8;
    if (width < 1 || width > 4) { c.len = 0; c.pcm = nullptr; c.ReleaseWav(); return; }
    const std::size_t n = c.len / (std::size_t)width;
    std::vector<Uint8> out(n * 2);
    for (std::size_t i = 0; i < n; ++i) {
        const Uint8* p = c.pcm + i * (std::size_t)width;
        float v = 0.0f;   // -1..1
        if (SDL_AUDIO_ISFLOAT(f)) {
            Uint32 u = 0;
            for (int b = 0; b < 4; ++b) u |= (Uint32)p[SDL_AUDIO_ISBIGENDIAN(f) ? 3 - b : b] << (8 * b);
            std::memcpy(&v, &u, 4);
        } else if (width == 1) {
            v = SDL_AUDIO_ISSIGNED(f) ? (float)(Sint8)p[0] / 128.0f : ((float)p[0] - 128.0f) / 128.0f;
        } else {
            Uint32 u = 0;
            for (int b = 0; b < width; ++b) u |= (Uint32)p[SDL_AUDIO_ISBIGENDIAN(f) ? width - 1 - b : b] << (8 * b);
            const long long top = 1ll << (8 * width - 1);
            long long sv = SDL_AUDIO_ISSIGNED(f) ? (long long)(Sint32)(u << (32 - 8 * width)) >> (32 - 8 * width) : (long long)u - top;
            v = (float)sv / (float)top;
        }
        const Sint16 s16 = (Sint16)std::clamp(v * 32767.0f, -32768.0f, 32767.0f);
        std::memcpy(out.data() + i * 2, &s16, 2);
    }
    c.owned = std::move(out);
    c.pcm = c.owned.data(); c.len = (Uint32)c.owned.size();
    c.spec.format = AUDIO_S16SYS;
    c.ReleaseWav();   // the converted copy replaces SDL's buffer
}

/// Loads a WAV into `c` (decoded to something the audio layer can play). False when SDL cannot read it.
inline bool VoiceClipLoadFile(VoiceClip& c, const std::string& path) {
    if (!SDL_LoadWAV(path.c_str(), &c.spec, &c.wav, &c.len)) return false;
    c.pcm = c.wav;
    VoiceClipToPlayable(c);
    return true;
}

}  // namespace okay::voice
